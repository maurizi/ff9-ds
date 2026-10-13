/* libcd: a virtual CD drive that reads the user's raw disc image (2352-byte sectors, as the decomp's disks/).
 *
 * The game's own CD module (main: cd_startRequest, cd_onSectorReady, g_cdLoad ...) runs unchanged on top. It is
 * an interrupt-driven state machine: it issues a command, and the drive's interrupt calls back the sync callback
 * (command complete) or the ready callback (a sector is ready, fetched with CdGetSector). It also busy-waits on
 * its own state with no library call inside the loop, so the work has to be finished before control returns to
 * the game: psx_cd_pump() runs at the end of every libcd call and executes commands and delivers sectors,
 * re-entrantly, until the game stops reading ("fast CD"). Commands issued from inside a callback are queued.
 *
 * Disc layout: Mode 2 Form 1, user data at offset 24 of each raw sector. Disc swaps: TODO (M4).
 */
#include "psyq_port.h"

#include <stdio.h>

#include "platform.h"

#define RAW_SECTOR 2352
#define USER_OFFSET 24
#define MAX_SECTORS_PER_PUMP 4096 /* a streaming read that never pauses continues next frame */

/* libcd command numbers (libcd.h CdlXxx) */
enum {
    CdlNop = 0x01, CdlSetloc = 0x02, CdlPlay = 0x03, CdlForward = 0x04, CdlBackward = 0x05, CdlReadN = 0x06,
    CdlStandby = 0x07, CdlStop = 0x08, CdlPause = 0x09, CdlInit = 0x0A, CdlMute = 0x0B, CdlDemute = 0x0C,
    CdlSetfilter = 0x0D, CdlSetmode = 0x0E, CdlGetparam = 0x0F, CdlGetlocL = 0x10, CdlGetlocP = 0x11,
    CdlGetTN = 0x13, CdlGetTD = 0x14, CdlSeekL = 0x15, CdlSeekP = 0x16, CdlReadS = 0x1B,
};
/* interrupt kinds passed to the callbacks */
enum { CdlNoIntr = 0, CdlDataReady = 1, CdlComplete = 2, CdlAcknowledge = 3, CdlDataEnd = 4, CdlDiskError = 5 };
/* status bits */
#define STAT_MOTOR 0x02
#define STAT_READ 0x20
#define STAT_SEEK 0x40

typedef void (*CdCallback)(u8 intr, u8 *result);

/* the game's names for libcd's callback setters are still placeholders in the decomp */
static CdCallback sync_cb, ready_cb, read_cb;

static FILE *disc;
static int disc_no = 1;

void psx_cd_select_disc(unsigned selected) {
    if(selected<1 || selected>4) port_fatal("invalid disc %u",selected);
    if(disc) port_fatal("disc selection must precede game initialization");
    disc_no=(int)selected;
}

/* libcd's own globals, which game C reads directly (func_80021310 CdStatus, func_80021320 CdMode, CdLastPos):
 * kept in PS1 RAM at their original addresses so those reads see the drive's state. */
extern u8 D_8006825C;      /* status byte */
extern s32 D_8006826C;     /* mode */
extern u8 D_80068268[4];   /* last position (CdlLOC) */
#define status D_8006825C
#define mode D_8006826C
#define last_pos D_80068268
static unsigned int loc, read_lba; /* Setloc target, next sector to deliver */
static int reading;
static u8 sector[RAW_SECTOR];
static unsigned int sector_lba; /* LBA of `sector` */
static unsigned int sector_pos; /* bytes of `sector`'s user data already taken by CdGetSector */

#define QUEUE 16
static struct {
    u8 com;
    u8 param[4];
} queue[QUEUE];
static int q_head, q_tail, pumping;
/* debug snapshot for the platform's watchdog */
unsigned int psx_cd_debug[6];
static int last_com_done;

/* ---- disc image ----------------------------------------------------------------------------------------- */

static int open_disc(void) {
    if (disc) {
        return 1;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/raw/FF9 CD%d.img", plat_data_dir(), disc_no);
    disc = fopen(path, "rb");
    if (!disc) {
        port_fatal("can't open disc image %s", path);
    }
    return 1;
}

static void read_raw(unsigned int lba, u8 *buf) {
    open_disc();
    if (fseek(disc, (long)lba * RAW_SECTOR, SEEK_SET) != 0 || fread(buf, 1, RAW_SECTOR, disc) != RAW_SECTOR) {
        port_fatal("disc %d: can't read sector %u", disc_no, lba);
    }
}

static void read_user(unsigned int lba, u8 *buf2048) {
    u8 raw[RAW_SECTOR];
    read_raw(lba, raw);
    __builtin_memcpy(buf2048, raw + USER_OFFSET, 2048);
}

/* ---- positions ------------------------------------------------------------------------------------------ */

static u8 bcd(int v) {
    return (u8)(((v / 10) << 4) | (v % 10));
}

static int unbcd(u8 v) {
    return (v >> 4) * 10 + (v & 0xF);
}

int CdPosToInt(CdlLOC *p) {
    return (unbcd(p->minute) * 60 + unbcd(p->second)) * 75 + unbcd(p->sector) - 150;
}

CdlLOC *CdIntToPos(int i, CdlLOC *p) {
    i += 150;
    p->sector = bcd(i % 75);
    p->second = bcd(i / 75 % 60);
    p->minute = bcd(i / 75 / 60);
    return p;
}

/* ---- ISO9660 lookup (CdSearchFile) ---------------------------------------------------------------------- */

static unsigned int le32(const u8 *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

/* Find `name` ("\\DIR\\FILE.EXT;1") on the disc: fills lba and size. */
static int iso_lookup(const char *name, unsigned int *lba, unsigned int *size) {
    u8 buf[2048];
    read_user(16, buf);
    if (buf[1] != 'C' || buf[2] != 'D') {
        return 0;
    }
    unsigned int dir = le32(buf + 156 + 2), dir_size = le32(buf + 156 + 10);
    const char *p = name;
    while (*p == '\\' || *p == '/') {
        p++;
    }
    while (*p) {
        char part[32];
        int n = 0;
        while (*p && *p != '\\' && *p != '/' && n < 31) {
            part[n++] = *p++;
        }
        part[n] = 0;
        while (*p == '\\' || *p == '/') {
            p++;
        }
        int found = 0;
        for (unsigned int s = 0; s < (dir_size + 2047) / 2048 && !found; s++) {
            read_user(dir + s, buf);
            for (unsigned int i = 0; i < 2048 && buf[i] && !found;) {
                unsigned int len = buf[i];
                unsigned int nl = buf[i + 32];
                const char *fn = (const char *)buf + i + 33;
                int match = nl == (unsigned int)n || (nl > (unsigned int)n && fn[n] == ';' && !__builtin_strchr(part, ';'));
                for (int k = 0; match && k < n; k++) {
                    char a = fn[k], b = part[k];
                    if (a >= 'a' && a <= 'z') a -= 32;
                    if (b >= 'a' && b <= 'z') b -= 32;
                    match = a == b;
                }
                if (match) {
                    dir = le32(buf + i + 2);
                    dir_size = le32(buf + i + 10);
                    found = 1;
                }
                i += len;
            }
        }
        if (!found) {
            return 0;
        }
    }
    *lba = dir;
    *size = dir_size;
    return 1;
}

CdlFILE *CdSearchFile(CdlFILE *fp, char *name) {
    unsigned int lba, size;
    if (!iso_lookup(name, &lba, &size)) {
        return NULL;
    }
    CdIntToPos((int)lba, &fp->pos);
    fp->size = size;
    __builtin_strncpy(fp->name, name, sizeof fp->name - 1);
    fp->name[sizeof fp->name - 1] = 0;
    return fp;
}

/* ---- FF9.IMG code overlays ------------------------------------------------------------------------------
 * Directory 0 of FF9.IMG holds the code overlays (docs/ff9img.md in the decomp). When their sectors are copied to
 * the overlay's slot, the relocated data image is put back over them (psx_overlay_patch). */
#define MAX_CODE_FILES 32
static struct {
    unsigned int lba, end, id;
} code_files[MAX_CODE_FILES];
static int n_code_files = -1;

static void scan_ff9img(void) {
    unsigned int base, size;
    u8 hdr[2048], tbl[2048];
    n_code_files = 0;
    if (!iso_lookup("\\FF9.IMG;1", &base, &size)) {
        return;
    }
    read_user(base, hdr);
    if (__builtin_memcmp(hdr, "FF9 ", 4) != 0) {
        return;
    }
    /* directory list at +0x10: {type, count, dir_id, first_sector}; dir 0 is the first entry */
    unsigned int type = le32(hdr + 0x10), count = le32(hdr + 0x14);
    if (type != 2 || count > MAX_CODE_FILES) {
        return;
    }
    /* file tables are packed from FF9.IMG sector 1, dir 0's first: {file_id, sector} pairs, then {-1, end} */
    read_user(base + 1, tbl);
    for (unsigned int i = 0; i < count && i * 8 + 12 < 2048; i++) {
        code_files[i].id = le32(tbl + i * 8);
        code_files[i].lba = base + le32(tbl + i * 8 + 4);
        code_files[i].end = base + le32(tbl + i * 8 + 12);
    }
    n_code_files = (int)count;
}

/* The code overlay `lba` belongs to, and the sector's offset in that file (bytes); 0 if none. */
static unsigned int code_file_of(unsigned int lba, unsigned int *file_off) {
    if (n_code_files < 0) {
        scan_ff9img();
    }
    for (int i = 0; i < n_code_files; i++) {
        if (lba >= code_files[i].lba && lba < code_files[i].end) {
            *file_off = (lba - code_files[i].lba) * 2048;
            return code_files[i].id;
        }
    }
    return 0;
}

/* ---- commands ------------------------------------------------------------------------------------------- */

static void result_status(u8 *result) {
    if (result) {
        result[0] = status;
    }
}

/* Execute one command now. Returns the interrupt it completes with (CdlComplete for the second-response
 * commands, CdlAcknowledge otherwise). */
static int execute(u8 com, const u8 *param, u8 *result) {
    psx_cd_debug[0] = com;
    psx_cd_debug[1]++;
    if (param) {
        param = PSX_PTR(param);
    }
    switch (com) {
    case CdlSetloc:
        if (param) {
            loc = (unsigned int)CdPosToInt((CdlLOC *)param);
        }
        break;
    case CdlSetmode:
        if (param) {
            mode = param[0];
        }
        break;
    case CdlReadN:
    case CdlReadS:
        read_lba = loc;
        CdIntToPos((int)loc, (CdlLOC *)last_pos);
        reading = 1;
        status = STAT_MOTOR | STAT_READ;
        break;
    case CdlSeekL:
    case CdlSeekP:
        read_lba = loc;
        result_status(result);
        return CdlComplete;
    case CdlPause:
    case CdlStop:
    case CdlInit:
        reading = 0;
        status = STAT_MOTOR;
        result_status(result);
        return CdlComplete;
    case CdlGetlocL:
    case CdlGetlocP:
        if (result) {
            CdlLOC l;
            CdIntToPos((int)read_lba, &l);
            __builtin_memset(result, 0, 8);
            result[0] = l.minute, result[1] = l.second, result[2] = l.sector;
        }
        return CdlAcknowledge;
    case CdlGetTN:
        if (result) {
            result[0] = status, result[1] = 1, result[2] = 1;
        }
        return CdlAcknowledge;
    default:
        break;
    }
    result_status(result);
    return CdlAcknowledge;
}

/* libcd calls the sync callback with CdlComplete when a command finishes: on its second response for the
 * two-stage commands (seek, pause, stop, init), on the acknowledge for the others (Setloc, Setmode, ReadN, ...).
 * The game's CD state machine advances on exactly these. */
static void complete(int intr, u8 *result) {
    u8 r[8] = {status};
    last_com_done = 1;
    if (sync_cb) {
        sync_cb(CdlComplete, result ? result : r);
    }
}

static int control(u8 com, u8 *param, u8 *result) {
    if (pumping) {
        /* issued from inside a callback: run it after the callback returns */
        int next = (q_tail + 1) % QUEUE;
        if (next == q_head) {
            port_fatal("libcd command queue full");
        }
        queue[q_tail].com = com;
        if (param) {
            __builtin_memcpy(queue[q_tail].param, param, 4);
        }
        q_tail = next;
        result_status(result);
        return 1;
    }
    int intr = execute(com, param, result);
    pumping = 1;
    complete(intr, result);
    pumping = 0;
    psx_cd_pump();
    return 1;
}

int CdControl(u8 com, u8 *param, u8 *result) {
    return control(com, param, result);
}

int CdControlF(u8 com, u8 *param) {
    return control(com, param, NULL);
}

int CdControlB(u8 com, u8 *param, u8 *result) {
    return control(com, param, result);
}

void psx_cd_pump(void) {
    psx_cd_debug[4] = pumping | reading << 4 | (ready_cb != NULL) << 8 | (sync_cb != NULL) << 12;
    psx_cd_debug[5] = (unsigned)(q_tail - q_head);
    if (pumping) {
        return;
    }
    pumping = 1;
    int delivered = 0;
    for (;;) {
        if (q_head != q_tail) {
            u8 com = queue[q_head].com;
            u8 param[4];
            __builtin_memcpy(param, queue[q_head].param, 4);
            q_head = (q_head + 1) % QUEUE;
            complete(execute(com, param, NULL), NULL);
            continue;
        }
        if (reading && ready_cb && delivered < MAX_SECTORS_PER_PUMP) {
            sector_lba = read_lba;
            read_raw(read_lba++, sector);
            psx_cd_debug[2]++;
            psx_cd_debug[3] = sector_lba;
            sector_pos = 0;
            u8 r[8] = {status};
            ready_cb(CdlDataReady, r);
            delivered++;
            continue;
        }
        break;
    }
    pumping = 0;
}

/* CD_getsector(madr, size): copy `size` words of the current sector's user data (libcd's DMA from the drive's
 * buffer). Successive calls continue where the last one stopped. Returns 0 on success. */
int CD_getsector(void *madr, int size) {
    unsigned int bytes = (unsigned int)size * 4;
    unsigned int base = (mode & 0x20) ? 12 : USER_OFFSET; /* CdlModeSize1: 2340-byte sectors from the header */
    __builtin_memcpy(madr, sector + base + sector_pos, bytes);
    /* user data of a code overlay landing at that overlay's slot: put the relocated image back over it */
    unsigned int file_off, id;
    if (base + sector_pos >= USER_OFFSET && (id = code_file_of(sector_lba, &file_off)) != 0) {
        static const unsigned int slots[] = PSX_OVL_SLOT_ADDRS;
        unsigned int off = (u32)madr - (u32)psx_ram;
        unsigned int at = file_off + (base + sector_pos - USER_OFFSET);
        for (unsigned int k = 0; k < 3; k++) {
            if (off < PSX_RAM_SIZE && off == ((slots[k] + at) & (PSX_RAM_SIZE - 1))) {
                psx_overlay_patch(id, off, bytes);
            }
        }
    }
    sector_pos += bytes;
    return 0;
}

int CD_getsector2(void *madr, int size) {
    return CD_getsector(madr, size);
}

/* libcd callback setters (decomp placeholders func_80021380 / func_800213A0 / func_800236A4): each stores the
 * callback and returns the previous one. */
CdCallback func_80021380(CdCallback f) {
    CdCallback old = sync_cb;
    sync_cb = f;
    return old;
}

CdCallback func_800213A0(CdCallback f) {
    CdCallback old = ready_cb;
    ready_cb = f;
    return old;
}

CdCallback func_800236A4(CdCallback f) {
    CdCallback old = read_cb;
    read_cb = f;
    return old;
}

/* CdLastPos, CdFlush, CdReady (decomp placeholders func_80021330 ... func_80021360); CdStatus and CdMode are
 * matched C in the decomp and read the globals above. */
u8 *func_80021330(void) {
    return last_pos;
}

void func_80021340(void) {
}

/* CdReady(mode, result): a sector is waiting (CdlDataReady) or not (CdlNoIntr). Sectors are delivered through
 * the ready callback, so a polling caller gets the pump. */
s32 func_80021360(s32 m, u8 *result) {
    psx_cd_pump();
    result_status(result);
    return CdlNoIntr;
}

int CdInit(void) {
    open_disc();
    status = STAT_MOTOR;
    return 1;
}

int CdReset(int m) {
    reading = 0;
    q_head = q_tail = 0;
    return 1;
}

int CdDiskReady(int m) {
    return CdlComplete;
}

int CdGetDiskType(void) {
    return 2; /* CdlCdromFormat */
}

int CdMix(CdlATV *vol) {
    return 1;
}

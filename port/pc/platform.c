/* PC host platform (32-bit Linux): the same game code and port/psx layer, headless, for fast iteration, gdb and
 * lockstep comparison with PCSX-Redux.
 *
 * psx_ram is placed at 0x02000000 by the link (--defsym) and mapped here before main, so PS1 pointers in the
 * game's ordering tables rebuild as on the DS (0x02000000 | addr24).
 *
 * Usage: ff9ds [--data DIR] [--frames N] [--status EVERY_N_FRAMES (0: off)] [--press BUTTON@FRAME[+FRAMES]]...
 *              [--watch ADDR:SIZE]... [--render] [--dump EVERY_N_FRAMES]
 * --render runs the software reference GPU (port/psx/softgpu.c); --dump also writes the displayed area as
 * build/frames/frame_NNNNNN.ppm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "input_script.h"
#include "platform.h"
#include "psx_mem.h"
#include "psx_runtime.h"

static const char *data_dir = "disks/us";
static long frame_limit = -1;
static long frames;
static long status_every = 60;
static long dump_every; /* --dump N: write the displayed area every N frames to build/frames/ */
static long vram_at = -1; /* --vram FRAME: also write all of VRAM at that frame */

void psx_softgpu_enable(int on);
extern unsigned short *psx_vram;

typedef struct {
    short x, y, w, h;
} DispRect;
const DispRect *psx_gpu_dispenv(void); /* DISPENV starts with its display RECT */

/* A VRAM area as a binary PPM (15-bit BGR expanded to 24-bit RGB): the displayed one, or all of it. */
static void dump_area(const char *name, int x0, int y0, int w, int h) {
    char path[256];
    snprintf(path, sizeof path, "build/frames/%s_%06ld.ppm", name, frames);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned short c = psx_vram[((y0 + y) & 511) * 1024 + ((x0 + x) & 1023)];
            unsigned char rgb[3] = {(unsigned char)((c & 31) << 3), (unsigned char)(((c >> 5) & 31) << 3),
                                    (unsigned char)(((c >> 10) & 31) << 3)};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static void dump_frame(void) {
    const DispRect *d = psx_gpu_dispenv();
    dump_area("frame", d->x, d->y, d->w ? d->w : 320, d->h ? d->h : 240);
}

__attribute__((constructor(101))) static void map_psx_ram(void) {
    void *p = mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                   -1, 0);
    if (p != (void *)psx_ram) {
        fprintf(stderr, "can't map psx_ram at %p\n", (void *)psx_ram);
        exit(1);
    }
}

extern unsigned char *g_gameState; /* PS1 0x8006794C: GameState * */

unsigned int plat_pad_buttons(int port) {
    return port == 0 ? input_script_buttons(frames) : 0;
}

/* --watch ADDR:SIZE: PS1 memory to print with each status line */
static struct {
    unsigned int addr, size;
} watches[16];
static int n_watches;

void plat_wait_frame(void) {
    frames++;
    if (dump_every && frames % dump_every == 0) {
        dump_frame();
    }
    if (frames == vram_at) {
        dump_area("vram", 0, 0, 1024, 512);
    }
    if (status_every && frames % status_every == 0) {
        unsigned char *gs = g_gameState;
        fprintf(stderr, "[frame %6ld] mode %d  overlays %02X/%02X/%02X", frames, gs ? gs[8] : -1,
                psx_ovl_resident[0], psx_ovl_resident[1], psx_ovl_resident[2]);
        for (int i = 0; i < n_watches; i++) {
            const unsigned char *p = psx_ram + (watches[i].addr & (PSX_RAM_SIZE - 1));
            unsigned int v = watches[i].size == 1 ? p[0] : watches[i].size == 2 ? *(const unsigned short *)p
                                                                                  : *(const unsigned int *)p;
            fprintf(stderr, "  %08X=%X", watches[i].addr, v);
        }
        fprintf(stderr, "\n");
    }
    if (frame_limit >= 0 && frames >= frame_limit) {
        fprintf(stderr, "frame limit %ld reached\n", frame_limit);
        exit(0);
    }
}

void plat_vlog(const char *fmt, va_list ap) {
    vfprintf(stderr, fmt, ap);
}

void plat_halt(void) {
    exit(2);
}

void plat_video_init(void) {
}

const char *plat_data_dir(void) {
    return data_dir;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
            if (!input_script_add(argv[++i])) {
                fprintf(stderr, "bad --press %s (BUTTON@FRAME[+FRAMES])\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--watch") && i + 1 < argc && n_watches < 16) {
            if (sscanf(argv[++i], "%x:%u", &watches[n_watches].addr, &watches[n_watches].size) != 2) {
                fprintf(stderr, "bad --watch %s (ADDR:SIZE)\n", argv[i]);
                return 1;
            }
            n_watches++;
        } else if (!strcmp(argv[i], "--render")) {
            psx_softgpu_enable(1);
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_every = atol(argv[++i]);
            psx_softgpu_enable(1);
            if (system("mkdir -p build/frames") != 0) {
                return 1;
            }
        } else if (!strcmp(argv[i], "--trace-gpu")) {
            extern int psx_trace_gpu;
            psx_trace_gpu = 1;
        } else if (!strcmp(argv[i], "--vram") && i + 1 < argc) {
            vram_at = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--status") && i + 1 < argc) {
            status_every = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atol(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--data DIR] [--frames N] [--status N] [--watch ADDR:SIZE] [--press BUTTON@FRAME[+N]] [--render] [--dump N]\n", argv[0]);
            return 1;
        }
    }
    psx_run_game();
}

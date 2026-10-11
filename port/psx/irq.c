/* libetc and the BIOS interrupt/event services, as virtual interrupts.
 *
 * On the PS1 these run in interrupt context whenever the hardware fires. The port delivers them at defined points
 * instead: once per frame in VSync() (and the CD at every libcd call, cd.c). That is deterministic, which the
 * lockstep comparison with PCSX-Redux needs, and it keeps the game's interrupt handlers running unchanged.
 *
 * Per frame (psx_frame): CD pump, root counter 2 events (AKAO's 240 Hz tick: 4 per frame), DrawSync callback, pad
 * buffers, the VSync callback, then the platform's frame wait.
 */
#include "platform.h"
#include "psyq_port.h"

#define RCNT_HZ 4233600 /* root counter 2 at sysclk/8: 33.8688 MHz / 8 */

static unsigned int vblanks;      /* VSync(-1) */
static unsigned int last_vsync;   /* vblank count at the previous VSync(n >= 0) */
static void (*vsync_cb)(void);

/* ---- root counters -------------------------------------------------------------------------------------- */

typedef struct {
    unsigned int target, mode, running;
    unsigned int count;      /* counter value */
    unsigned int frac;       /* sub-tick accumulator (RCNT_HZ / 60 isn't an integer) */
} RCnt;

static RCnt rcnt[3];

static int rcnt_index(unsigned long spec) {
    return (int)(spec & 3); /* RCntCNT0..2 = 0xF2000000..0xF2000002 */
}

long SetRCnt(unsigned long spec, unsigned short target, long mode) {
    RCnt *c = &rcnt[rcnt_index(spec)];
    c->target = target;
    c->mode = (unsigned int)mode;
    c->count = 0;
    return 1;
}

long GetRCnt(unsigned long spec) {
    return (long)rcnt[rcnt_index(spec)].count;
}

long StartRCnt(unsigned long spec) {
    rcnt[rcnt_index(spec)].running = 1;
    return 1;
}

long StopRCnt(unsigned long spec) {
    rcnt[rcnt_index(spec)].running = 0;
    return 1;
}

long ResetRCnt(unsigned long spec) {
    rcnt[rcnt_index(spec)].count = 0;
    return 1;
}

/* ---- BIOS events (B(08h) OpenEvent ...) ------------------------------------------------------------------ */

#define MAX_EVENTS 16
#define EV_ENABLED 0x2000
#define EV_READY 0x4000

typedef struct {
    unsigned long desc, spec, mode;
    void (*func)(void);
    unsigned int status; /* 0 free, else EV_ENABLED / EV_READY bits | 0x1000 (open) */
} Event;

static Event events[MAX_EVENTS];

long OpenEvent(unsigned long desc, long spec, long mode, long (*func)(void)) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (!events[i].status) {
            events[i] = (Event){desc, (unsigned long)spec, (unsigned long)mode, (void (*)(void))func, 0x1000};
            return 0xF1000000 | i;
        }
    }
    return -1;
}

static Event *event_of(unsigned long ev) {
    unsigned int i = ev & 0xFFFF;
    return i < MAX_EVENTS && events[i].status ? &events[i] : NULL;
}

long CloseEvent(unsigned long ev) {
    Event *e = event_of(ev);
    if (e) {
        e->status = 0;
    }
    return 1;
}

long EnableEvent(unsigned long ev) {
    Event *e = event_of(ev);
    if (e) {
        e->status |= EV_ENABLED;
    }
    return 1;
}

long DisableEvent(unsigned long ev) {
    Event *e = event_of(ev);
    if (e) {
        e->status &= ~EV_ENABLED;
    }
    return 1;
}

long TestEvent(unsigned long ev) {
    Event *e = event_of(ev);
    if (e && (e->status & EV_READY)) {
        e->status &= ~EV_READY;
        return 1;
    }
    return 0;
}

long WaitEvent(unsigned long ev) {
    Event *e = event_of(ev);
    while (e && (e->status & EV_ENABLED) && !(e->status & EV_READY)) {
        psx_frame();
    }
    if (e) {
        e->status &= ~EV_READY;
    }
    return 1;
}

/* Raise every enabled event matching (desc, spec): interrupt-mode events (EvMdINTR 0x1000) run their handler,
 * the others become ready for TestEvent/WaitEvent. */
void DeliverEvent(unsigned long desc, long spec) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        Event *e = &events[i];
        if ((e->status & EV_ENABLED) && e->desc == desc && (e->spec & (unsigned long)spec)) {
            if ((e->mode & 0x1000) && e->func) {
                e->func();
            } else {
                e->status |= EV_READY;
            }
        }
    }
}

void UnDeliverEvent(unsigned long desc, long spec) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (events[i].desc == desc && (events[i].spec & (unsigned long)spec)) {
            events[i].status &= ~EV_READY;
        }
    }
}

/* ---- libetc ---------------------------------------------------------------------------------------------- */

void ResetCallback(void) {
    vsync_cb = NULL;
}

void VSyncCallback(void (*f)(void)) {
    vsync_cb = f;
}

/* Advance the root counters by one frame and raise their target events (AKAO's tick on counter 2). */
static void rcnt_frame(void) {
    for (int i = 0; i < 3; i++) {
        RCnt *c = &rcnt[i];
        if (!c->running || !c->target) {
            continue;
        }
        unsigned int ticks = (RCNT_HZ + c->frac) / 60;
        c->frac = (RCNT_HZ + c->frac) % 60;
        if (i != 2) {
            continue; /* counters 0/1 count dot/hblank clocks: only their value is read */
        }
        while (ticks) {
            unsigned int step = c->target - c->count;
            if (step > ticks) {
                c->count += ticks;
                break;
            }
            ticks -= step;
            c->count = 0;
            DeliverEvent(0xF2000002, 0x0002); /* RCntCNT2, EvSpINT */
        }
    }
}

void psx_frame(void) {
    psx_cd_pump();
    psx_drawsync_pending();
    rcnt_frame();
    vblanks++;
    psx_pad_update();
    if (vsync_cb) {
        vsync_cb();
    }
    plat_wait_frame();
}

/* VSync(mode) (libetc): -1 the vblank count; 1 hblanks since the last vblank (no wait); 0 wait for the next
 * vblank; n > 1 wait until n vblanks have passed since the previous VSync call. Virtual time only advances here,
 * so VSync(n) is n frames. Returns the hblanks since the previous call (263 per NTSC frame). */
long VSync(long mode) {
    if (mode < 0) {
        return (long)vblanks;
    }
    if (mode == 1) {
        return 0;
    }
    unsigned int want = mode == 0 ? 1 : (unsigned int)mode;
    unsigned int start = vblanks;
    while (vblanks - last_vsync < want || vblanks == start) {
        psx_frame();
        if (mode == 0) {
            break;
        }
    }
    long h = (long)(vblanks - last_vsync) * 263;
    last_vsync = vblanks;
    return h;
}

/* ---- BIOS / SN runtime ---------------------------------------------------------------------------------- */

void __main(void) {
    /* SN startup: walks an empty constructor list */
}

/* B(17h)-style critical sections (func_80012D08 = EnterCriticalSection, func_80012D18 = ExitCriticalSection):
 * nothing runs asynchronously in the port. */
long func_80012D08(void) {
    return 1;
}

void func_80012D18(void) {
}

void FlushCache(void) {
}

void SystemError(char c, long n) {
    port_fatal("SystemError('%c', %ld)", c, n);
}

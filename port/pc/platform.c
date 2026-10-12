/* PC host platform (32-bit Linux): the same game code and port/psx layer, headless, for fast iteration, gdb and
 * lockstep comparison with PCSX-Redux.
 *
 * psx_ram is placed at 0x02000000 by the link (--defsym) and mapped here before main, so PS1 pointers in the
 * game's ordering tables rebuild as on the DS (0x02000000 | addr24).
 *
 * Usage: ff9ds [--data DIR] [--frames N] [--status EVERY_N_FRAMES (0: off)] [--press BUTTON@FRAME[+FRAMES]]...
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
        } else if (!strcmp(argv[i], "--status") && i + 1 < argc) {
            status_every = atol(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atol(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--data DIR] [--frames N] [--status N] [--watch ADDR:SIZE] [--press BUTTON@FRAME[+N]]\n", argv[0]);
            return 1;
        }
    }
    psx_run_game();
}

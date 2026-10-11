/* PC host platform (32-bit Linux): the same game code and port/psx layer, headless, for fast iteration, gdb and
 * lockstep comparison with PCSX-Redux.
 *
 * psx_ram is placed at 0x02000000 by the link (--defsym) and mapped here before main, so PS1 pointers in the
 * game's ordering tables rebuild as on the DS (0x02000000 | addr24).
 *
 * Usage: ff9ds [--data DIR] [--frames N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "platform.h"
#include "psx_mem.h"
#include "psx_runtime.h"

static const char *data_dir = "disks/us";
static long frame_limit = -1;
static long frames;

__attribute__((constructor(101))) static void map_psx_ram(void) {
    void *p = mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                   -1, 0);
    if (p != (void *)psx_ram) {
        fprintf(stderr, "can't map psx_ram at %p\n", (void *)psx_ram);
        exit(1);
    }
}

void plat_wait_frame(void) {
    frames++;
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
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atol(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--data DIR] [--frames N]\n", argv[0]);
            return 1;
        }
    }
    psx_run_game();
}

/* DS platform: entry point, console, frame pacing. */
#include <nds.h>
#include <stdio.h>

#include "platform.h"
#include "psx_mem.h"
#include "psx_runtime.h"

unsigned char psx_ram[PSX_RAM_SIZE] __attribute__((aligned(32)));

void plat_wait_frame(void) {
    swiWaitForVBlank();
}

void plat_vlog(const char *fmt, va_list ap) {
    vprintf(fmt, ap);
}

void plat_halt(void) {
    for (;;) {
        swiWaitForVBlank();
    }
}

const char *plat_data_dir(void) {
    return "sd:/ff9ds";
}

int main(void) {
    consoleDemoInit();
    printf("ff9-ds (%s mode)\n", isDSiMode() ? "DSi" : "DS");
    printf("psx_ram %p\n", (void *)psx_ram);
#ifndef PORT_AUTORUN /* `make AUTORUN=1`: run straight away, for automated emulator checks */
    printf("START: run the game\n");
    do {
        swiWaitForVBlank();
        scanKeys();
    } while (!(keysDown() & KEY_START));
#endif
    psx_run_game();
}

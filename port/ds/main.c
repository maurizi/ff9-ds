/* ARM9 entry point. M0: bring up the console, load main's data image into psx_ram, report, and on START enter
 * the game's main loop, which stops at the first routine the port doesn't provide yet (port_stub_hit). */
#include <nds.h>
#include <stdarg.h>
#include <stdio.h>

#include "psx_mem.h"
#include "psx_runtime.h"

extern unsigned char g_gameState[]; /* PS1 0x8006794C, resolved into psx_ram by psx_syms.ld */
extern unsigned char g_allocTop[];

void func_800127A0(void); /* the game's main loop (decomp src/main/main.c) */

void port_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("\nFATAL: ");
    vprintf(fmt, ap);
    va_end(ap);
    for (;;) {
        swiWaitForVBlank();
    }
}

void port_stub_hit(const char *name) {
    port_fatal("not implemented: %s", name);
}

int main(void) {
    consoleDemoInit();
    printf("ff9-ds M0 (%s mode)\n", isDSiMode() ? "DSi" : "DS");

    psx_load_image(psximg_main);
    if (!psx_overlay_load(0x14)) { /* referenced here so every overlay is linked in (M1 hooks the real loader) */
        port_fatal("no overlay 0x14");
    }
    printf("psx_ram     %p\n", (void *)psx_ram);
    printf("g_gameState %p (+0x%X)\n", (void *)g_gameState, (unsigned)(g_gameState - psx_ram));
    printf("g_allocTop  = 0x%08X\n", *(unsigned *)g_allocTop);
    printf("\nSTART: run the game until it\nreaches something unported\n");

#ifdef PORT_AUTORUN /* `make AUTORUN=1`: run straight away, for automated emulator checks */
    func_800127A0();
#endif
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) {
            func_800127A0();
        }
    }
}

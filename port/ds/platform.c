/* DS platform: entry point, console, frame pacing. */
#include <fat.h>
#include <nds.h>
#include <stdio.h>

#include "input_script.h"
#include "platform.h"
#include "psx_mem.h"
#include "psx_runtime.h"

unsigned char psx_ram[PSX_RAM_SIZE] __attribute__((aligned(32)));

static long frames;

/* libcd's debug snapshot and the game's CD state, shown on the status line (port/psx/cd.c) */
extern unsigned int psx_cd_debug[6];
extern unsigned char g_cdLoad[];

extern unsigned char *g_gameState; /* PS1 0x8006794C: GameState * */

static void wd_put(int x, int y, const char *s) {
    u16 *map = console->fontBgMap + y * console->consoleWidth;
    for (; *s && x < console->consoleWidth; s++, x++) {
        map[x] = (u16)((*s - console->font.asciiOffset + console->fontCharOffset) | (console->fontPalIndex << 12));
    }
}

static char *wd_hex(char *p, unsigned int v, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        p[i] = "0123456789ABCDEF"[v & 0xF];
        v >>= 4;
    }
    p[digits] = ' ';
    return p + digits + 1;
}

static void watchdog(void) {
    if (++ticks % 30 || !console) {
        return;
    }
    char line[64], *p = line;
    p = wd_hex(p, ticks / 60, 3);
    p = wd_hex(p, (unsigned int)frames, 5);
    p = wd_hex(p, g_cdLoad[0], 2);
    p = wd_hex(p, psx_cd_debug[0], 2);
    p = wd_hex(p, psx_cd_debug[1], 4);
    p = wd_hex(p, psx_cd_debug[2], 4);
    *p = 0;
    wd_put(0, 21, line);
    p = line;
    p = wd_hex(p, psx_cd_debug[3], 6);
    p = wd_hex(p, psx_cd_debug[4], 4);
    p = wd_hex(p, psx_cd_debug[5], 2);
    *p = 0;
    wd_put(0, 22, line);
}

void plat_wait_frame(void) {
    swiWaitForVBlank();
    frames++;
    if (frames % 30 == 0) {
        unsigned char *gs = g_gameState;
        /* fixed line at the bottom of the console */
        printf("\x1b[23;0H[%6ld] mode %d ovl %02X/%02X/%02X ", frames, gs ? gs[8] : -1, psx_ovl_resident[0],
               psx_ovl_resident[1], psx_ovl_resident[2]);
        printf("\x1b[22;0Hcd %02X c%02X n%u s%u@%u p%03X q%u   ", g_cdLoad[0], psx_cd_debug[0], psx_cd_debug[1],
               psx_cd_debug[2], psx_cd_debug[3], psx_cd_debug[4], psx_cd_debug[5]);
    }
}

/* DS keys to the PS1 pad (docs/dual-screen.md): A = Cross (confirm), B = Circle (cancel), X = Triangle (menu),
 * Y = Square, L/R = L1/R1, L+R together = L2+R2, plus the scripted presses of PORT_AUTOPRESS builds. */
unsigned int plat_pad_buttons(int port) {
    if (port != 0) {
        return 0;
    }
    scanKeys();
    unsigned int k = keysHeld(), b = 0;
    if (k & KEY_A) b |= 0x4000;
    if (k & KEY_B) b |= 0x2000;
    if (k & KEY_X) b |= 0x1000;
    if (k & KEY_Y) b |= 0x8000;
    if (k & KEY_L) b |= 0x0400;
    if (k & KEY_R) b |= 0x0800;
    if ((k & KEY_L) && (k & KEY_R)) b |= 0x0300;
    if (k & KEY_START) b |= 0x0008;
    if (k & KEY_SELECT) b |= 0x0001;
    if (k & KEY_UP) b |= 0x0010;
    if (k & KEY_RIGHT) b |= 0x0020;
    if (k & KEY_DOWN) b |= 0x0040;
    if (k & KEY_LEFT) b |= 0x0080;
    return b | input_script_buttons(frames);
}

void plat_vlog(const char *fmt, va_list ap) {
    vprintf(fmt, ap);
}

void plat_halt(void) {
    for (;;) {
        swiWaitForVBlank();
    }
}

/* ff9ds/ at the root of the boot drive: the DSi's SD card ("sd:"), or a DS flashcart via DLDI ("fat:"). */
static char data_dir[32] = "fat:/ff9ds";

const char *plat_data_dir(void) {
    return data_dir;
}

int main(void) {
    consoleDemoInit();
    printf("ff9-ds (%s mode)\n", isDSiMode() ? "DSi" : "DS");
    printf("psx_ram %p\n", (void *)psx_ram);
    if (!fatInitDefault()) {
        printf("no SD card / flashcart filesystem\n");
    } else {
        snprintf(data_dir, sizeof data_dir, "%sff9ds", fatGetDefaultDrive());
    }
    printf("data: %s\n", data_dir);
#ifdef PORT_AUTOPRESS
    input_script_add_list(PORT_AUTOPRESS);
#endif
#ifndef PORT_AUTORUN /* `make AUTORUN=1`: run straight away, for automated emulator checks */
    printf("START: run the game\n");
    do {
        swiWaitForVBlank();
        scanKeys();
    } while (!(keysDown() & KEY_START));
#endif
    psx_run_game();
}

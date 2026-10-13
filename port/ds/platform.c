/* DS platform: entry point, console, frame pacing. */
#include <fat.h>
#include <nds.h>
#include <stdio.h>

#include "display.h"
#include "input_script.h"
#include "platform.h"
#include "psx_mem.h"
#include "psx_runtime.h"

unsigned char psx_ram[PSX_RAM_SIZE] __attribute__((aligned(32)));

static long frames;

/* Keep each status row inside 31 columns: writing the final column wraps and scrolls the console. */
static void status_row(int row, const char *fmt, ...) {
    char text[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    printf("\x1b[%d;0H%-31s", row, text);
}

/* libcd's debug snapshot and the game's CD state, shown on the status line (port/psx/cd.c) */
extern unsigned int psx_cd_debug[6];
extern unsigned char g_cdLoad[];

extern unsigned char *g_gameState; /* PS1 0x8006794C: GameState * */
extern int psx_gx_stats_polys, psx_gx_stats_dropped;
extern int psx_gx_stats_texture_fail, psx_gx_stats_budget_fail;
extern int psx_gx_stats_composited, psx_gx_stats_blend_fallback;

void plat_wait_frame(void) {
#ifndef PORT_SOFT_DISPLAY
    swiWaitForVBlank();
#endif
    display_present();
    frames++;
    if (frames % 30 == 0) {
#ifndef PORT_SOFT_DISPLAY
        status_row(19, "texfail %d budget %d", psx_gx_stats_texture_fail, psx_gx_stats_budget_fail);
        status_row(20, "gx %d polys drop %d", psx_gx_stats_polys, psx_gx_stats_dropped);
        status_row(21, "blend %d exact %d approx", psx_gx_stats_composited, psx_gx_stats_blend_fallback);
#endif
        unsigned char *gs = g_gameState;
        /* fixed line at the bottom of the console */
        status_row(23, "f%6ld m%d ovl %02X/%02X/%02X", frames, gs ? gs[8] : -1, psx_ovl_resident[0],
               psx_ovl_resident[1], psx_ovl_resident[2]);
        status_row(22, "cd%02X c%02X n%u s%u@%u p%X q%u", g_cdLoad[0], psx_cd_debug[0], psx_cd_debug[1],
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

void plat_video_init(void) {
    display_init();
}

const char *plat_data_dir(void) {
    return data_dir;
}

int main(void) {
    defaultExceptionHandler();
    /* A 4bpp font and one tile map fit in bank H. Leave all four 128 KiB banks for GX textures. */
    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, false, true);
    if (isDSiMode()) {
        setCpuClock(true); /* the DSi's 134 MHz ARM9 (BlocksDS leaves it at 67 MHz) */
    }
    printf("ff9-ds (%s mode)\n", isDSiMode() ? "DSi" : "DS");
    printf("psx_ram %p\n", (void *)psx_ram);
    if (!fatInitDefault()) {
        printf("no SD card / flashcart filesystem\n");
    } else {
        snprintf(data_dir, sizeof data_dir, "%sff9ds", fatGetDefaultDrive());
    }
    printf("data: %s\n", data_dir);
#ifdef PORT_SCENARIO
    port_scenario_load(PORT_SCENARIO);
#endif
#ifdef PORT_ITCM_GUARD
    void port_itcm_guard_init(void);
    port_itcm_guard_init();
    printf("ITCM code write guard enabled\n");
#endif
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

/* Top-screen display, software path: the reference GPU (port/psx/softgpu.c) draws into the VRAM shadow, and each
 * frame the area the game displays (DISPENV) is copied into a 16-bit bitmap background on the main engine. The 2D
 * engine's affine unit scales it 320 -> 256 (x0.8, uniform; docs/design.md §5.1), letterboxed vertically.
 *
 * Exact (every blend mode, every primitive) but CPU-bound; the 3D-hardware renderer replaces it for speed.
 */
#include <nds.h>

#include "display.h"
#include "psx_mem.h"

typedef struct {
    short x, y, w, h;
} DispRect;
const DispRect *psx_gpu_dispenv(void);
extern unsigned short *psx_vram;
void psx_softgpu_enable(int on);

#define BMP_W 512 /* the bitmap background is 512x256 (banks A+B) */

static int bg = -1;

void display_init(void) {
    if (!psx_vram) {
        return; /* 4 MB DS: no VRAM shadow, nothing to show */
    }
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    bg = bgInit(3, BgType_Bmp16, BgSize_B16_512x256, 0, 0);
    bgWrapOff(bg);
    /* 320 PS1 pixels across 256 screen pixels: 1.25 in 8.8 fixed point, the same vertically */
    bgSetRotateScale(bg, 0, 320, 320);
    bgSetScroll(bg, 0, -8); /* 224 lines -> 179 on screen: centre them (6 screen lines, 8 PS1 lines) */
    bgUpdate();
    psx_softgpu_enable(1);
}

/* Copy the displayed area into the bitmap (bit 15 set: opaque). */
void display_present(void) {
    if (bg < 0) {
        return;
    }
    const DispRect *d = psx_gpu_dispenv();
    int w = d->w ? d->w : 320, h = d->h ? d->h : 224;
    if (w > BMP_W) {
        w = BMP_W;
    }
    if (h > 256) {
        h = 256;
    }
    u16 *dst = bgGetGfxPtr(bg);
    for (int y = 0; y < h; y++) {
        const u16 *src = &psx_vram[((d->y + y) & 511) * 1024];
        u16 *row = dst + y * BMP_W;
        for (int x = 0; x < w; x++) {
            row[x] = src[(d->x + x) & 1023] | 0x8000;
        }
    }
}

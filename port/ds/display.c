/* Top-screen software display: shrink the PSX framebuffer 4/5 into a bitmap.
 * Font sprite regions use area coverage to retain thin strokes; scene pixels
 * keep nearest-neighbour sampling. PSX VRAM and game coordinates stay intact.
 */
#include <nds.h>
#include <string.h>

#include "display.h"
#include "display_scale.h"
#include "psx_mem.h"

typedef struct {
    short x, y, w, h;
} DispRect;
const DispRect *psx_gpu_dispenv(void);
extern unsigned short *psx_vram;
void psx_softgpu_enable(int on);

static int bg = -1;
static int previous_w[2], previous_h[2];
static int back_buffer=1;
static DsTextRect text_rects[1024];
static DsTextRect previous_rects[1024];
static unsigned text_count;
static unsigned previous_count;
static int drawing;
unsigned int psx_display_font_calls, psx_display_font_presented;

void psx_display_drawing(void) {
#ifdef PORT_SOFT_DISPLAY
    if (!drawing) {
        memcpy(previous_rects,text_rects,text_count*sizeof *text_rects);
        previous_count=text_count;
        text_count=0;
        drawing=1;
    }
#endif
}

void psx_display_glyph(int x, int y, int w, int h) {
#ifdef PORT_SOFT_DISPLAY
    psx_display_font_calls++;
    if (text_count < sizeof text_rects / sizeof text_rects[0])
        text_rects[text_count++] = (DsTextRect){x,y,w,h};
#endif
}

void render_gx_init(void);

void display_init(void) {
    if (!psx_vram) {
        return; /* 4 MB DS: no VRAM shadow, nothing to show */
    }
#ifndef PORT_SOFT_DISPLAY /* `make ds SOFTDISPLAY=1`: the software reference renderer on the DS, for comparison */
    render_gx_init();
    return;
#endif
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    memset(BG_BMP_RAM_MAIN(0),0,0x40000);
    bgWrapOff(bg);
    /* The bitmap is already scaled; use the 2D engine at native DS size. */
    bgSetRotateScale(bg, 0, 256, 256);
    bgSetScroll(bg, 0, -6);
    bgUpdate();
    psx_softgpu_enable(1);
}

/* Copy the displayed area into the bitmap (bit 15 set: opaque). */
void display_present(void) {
#ifndef PORT_SOFT_DISPLAY
    return;
#endif
    if (bg < 0) {
        swiWaitForVBlank();
        return;
    }
    const DispRect *d = psx_gpu_dispenv();
    int w = d->w ? d->w : 320, h = d->h ? d->h : 224;
    if (w > 320) {
        w = 320;
    }
    if (h > 240) {
        h = 240;
    }
    u16 *dst = BG_BMP_RAM_MAIN(back_buffer*8);
    if (w != previous_w[back_buffer] || h != previous_h[back_buffer]) {
        memset(dst, 0, 256*256*sizeof *dst);
        previous_w[back_buffer]=w; previous_h[back_buffer]=h;
    }
    psx_display_font_presented=text_count;
    ds_scale_display_text_pair(dst, psx_vram, d->x, d->y, w, h,
                              text_rects, text_count, previous_rects, previous_count);
    /* The scaler has a nearest pass followed by font coverage. Never let
     * scanout see the intermediate bitmap, even if rendering outlasts blank. */
    swiWaitForVBlank();
    bgSetMapBase(bg,back_buffer*8);
    bgSetScroll(bg,0,-(192-h*4/5)/2);
    bgUpdate();
    back_buffer^=1;
    /* An extra VSync without drawing must retain the same filtered glyphs. */
    drawing = 0;
}

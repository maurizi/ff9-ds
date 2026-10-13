#include "display_scale.h"
#include <string.h>

/* Ten bits per channel leave room for the 25-fold coverage sum without
 * carries between RGB channels. Ignore PSX STP and set DS opacity at output. */
static inline uint32_t spread(uint16_t c) {
    return (c & 31) | ((uint32_t)(c & 0x3e0) << 5)
                   | ((uint32_t)(c & 0x7c00) << 10);
}

static uint32_t text_mask[192][8];

#ifdef PORT_SCALE_ITCM
__attribute__((section(".itcm"), long_call))
#endif
void ds_scale_display_text_pair(uint16_t *dst, const uint16_t *vram,
                          int x, int y, int width, int height,
                          const DsTextRect *rects, unsigned count,
                          const DsTextRect *previous, unsigned previous_count) {
    if (width <= 0 || height <= 0) return;
    if (width > 320) width = 320;
    if (height > 240) height = 240;
    int out_w=width*4/5, out_h=height*4/5;
    /* Scene pixels keep the existing nearest-neighbour appearance. */
    for(int oy=0;oy<out_h;++oy) {
        const uint16_t *src=vram+((y+oy*5/4)&511)*1024;
        uint16_t *out=dst+oy*256;
        for(int ox=0;ox<out_w;++ox) out[ox]=src[(x+ox*5/4)&1023]|0x8000;
    }
    if (!count && !previous_count) return;
    memset(text_mask,0,sizeof text_mask);
    for(unsigned i=0;i<count+previous_count;++i) {
        const DsTextRect *rect=i<count ? rects+i : previous+i-count;
        int rx=(rect->x-x)&1023, ry=(rect->y-y)&511;
        if(rx>=512) rx-=1024;
        if(ry>=256) ry-=512;
        /* Include output cells whose two source samples cross the glyph's
         * border. The same cell can cover adjacent glyphs; filter it once. */
        int x0=rx>0 ? (rx-1)*4/5 : 0;
        int y0=ry>0 ? (ry-1)*4/5 : 0;
        int x1=(rx+rect->w)*4/5+1;
        int y1=(ry+rect->h)*4/5+1;
        if(x1>out_w) x1=out_w;
        if(y1>out_h) y1=out_h;
        for(int oy=y0;oy<y1;++oy) for(int ox=x0;ox<x1;++ox)
            text_mask[oy][ox>>5]|=1u<<(ox&31);
    }
    for(int oy=0;oy<out_h;++oy) {
        int sy=oy*5/4, fy=oy&3;
        int next_y=sy+1<height ? sy+1 : height-1;
        const uint16_t *a=vram+((y+sy)&511)*1024;
        const uint16_t *b=vram+((y+next_y)&511)*1024;
        for(int group=0;group<8;++group) {
            uint32_t mask=text_mask[oy][group];
            while(mask) {
                unsigned bit=__builtin_ctz(mask);
                int ox=group*32+bit, sx=ox*5/4, fx=ox&3;
                int next_x=sx+1<width ? sx+1 : width-1;
                unsigned x0=(x+sx)&1023,x1=(x+next_x)&1023;
                uint32_t top=spread(a[x0])*(4-fx)+spread(a[x1])*(fx+1);
                uint32_t bottom=spread(b[x0])*(4-fx)+spread(b[x1])*(fx+1);
                uint32_t c=top*(4-fy)+bottom*(fy+1);
                unsigned r=(((c&1023)+12)*1311)>>15;
                unsigned g=((((c>>10)&1023)+12)*1311)>>15;
                unsigned bl=(((c>>20)+12)*1311)>>15;
                dst[oy*256+ox]=0x8000|r|(g<<5)|(bl<<10);
                mask&=mask-1;
            }
        }
    }
}

void ds_scale_display_text(uint16_t *dst, const uint16_t *vram,
                          int x, int y, int width, int height,
                          const DsTextRect *rects, unsigned count) {
    ds_scale_display_text_pair(dst,vram,x,y,width,height,rects,count,0,0);
}

#ifdef PORT_SCALE_ITCM
__attribute__((section(".itcm"), long_call))
#endif
void ds_scale_display(uint16_t *dst, const uint16_t *vram,
                      int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width > 320) width = 320;
    if (height > 240) height = 240;
    int out_w = width * 4 / 5, out_h = height * 4 / 5;
    uint32_t rows[5][256];
    for (int block = 0; block < out_h; block += 4) {
        int source_y = block * 5 / 4;
        for (int j = 0; j < 5; ++j) {
            int sy = source_y + j;
            if (sy >= height) sy = height - 1;
            const uint16_t *src = vram + ((y + sy) & 511) * 1024;
            int ox = 0;
            for (; ox + 4 <= out_w; ox += 4) {
                int sx = ox * 5 / 4;
                uint32_t a = spread(src[(x + sx) & 1023]);
                uint32_t b = spread(src[(x + sx + 1) & 1023]);
                uint32_t c = spread(src[(x + sx + 2) & 1023]);
                uint32_t d = spread(src[(x + sx + 3) & 1023]);
                uint32_t e = spread(src[(x + sx + 4) & 1023]);
                rows[j][ox] = 4*a+b;
                rows[j][ox+1] = 3*b+2*c;
                rows[j][ox+2] = 2*c+3*d;
                rows[j][ox+3] = d+4*e;
            }
            for (; ox < out_w; ++ox) {
                int sx = ox * 5 / 4, fraction = ox & 3;
                int next = sx + 1 < width ? sx + 1 : width - 1;
                rows[j][ox] = spread(src[(x + sx) & 1023]) * (4 - fraction)
                             + spread(src[(x + next) & 1023]) * (fraction + 1);
            }
        }
        for (int j = 0; j < 4 && block + j < out_h; ++j) {
            uint16_t *out = dst + (block + j) * 256;
            for (int ox = 0; ox < out_w; ++ox) {
                uint32_t c = rows[j][ox] * (4 - j) + rows[j + 1][ox] * (j + 1);
                /* Exact rounded division by 25 for the channel's 0..775
                 * range, using a small multiply instead of a wide UMULL. */
                unsigned r = (((c & 1023) + 12) * 1311) >> 15;
                unsigned g = ((((c >> 10) & 1023) + 12) * 1311) >> 15;
                unsigned b = (((c >> 20) + 12) * 1311) >> 15;
                out[ox] = 0x8000 | r | (g << 5) | (b << 10);
            }
        }
    }
}

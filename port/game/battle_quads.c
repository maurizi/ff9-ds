/* Project packed 10-byte effect rectangles and emit POLY_FT4 packets. The
 * handwritten routine deliberately tests existing MAC0 after RTPT, and stops
 * the entire batch when it is zero; do not substitute a new NCLIP operation. */
#include "psyq_port.h"
#include "gte.h"
static u32 word(const void *p, unsigned offset) { return *(const u32 *)((const u8 *)p + offset); }
/* Single textured effect rectangle. Preserve MAC0's existing cull decision;
 * the original RTPT sequence does not issue NCLIP. */
void o0b_func_800D57D0(const u8 *rect,void *context,u8 *out,u32 *ot) {
    const u8 *uv=(const u8 *)word(context,16);
    u32 half=(u32)*(const u16 *)(rect+6)*2;
    u32 top=0-word(context,12),bottom=top+(u32)*(const u16 *)(rect+4)*4;
    for(unsigned i=0;i<3;i++) gte_ctc2(21+i,0);
    gte_mtc2(4,half);*(u32 *)((u8 *)context+28)=half;
    gte_mtc2(0,(0-half)&65535);gte_mtc2(2,(0-half)&65535);
    gte_mtc2(1,top);gte_mtc2(5,top);gte_mtc2(3,bottom);
    *(u32 *)((u8 *)context+32)=bottom;
    u32 u=uv[0],v=(u32)uv[1]<<8;
    gte_cmd(0x280030);
    *(u32 *)(out+12)=u|v|((u32)*(const u16 *)(rect+2)<<16);
    u32 right=u+(u32)*(const u16 *)(rect+8)-1;
    *(u32 *)(out+20)=right|v|(((u32)*(const u16 *)rect|word(context,24))<<16);
    v+=((u32)*(const u16 *)(rect+10)-1)<<8;
    *(u16 *)(out+28)=(u16)(u|v);*(u16 *)(out+36)=(u16)(right|v);
    for(unsigned i=0;i<3;i++) *(u32 *)(out+8+i*8)=gte_mfc2(12+i);
    if(!gte_mfc2(24)) return;
    u32 depth=gte_mfc2(19);
    gte_mtc2(0,half);gte_mtc2(1,bottom);
    const u8 *color=rect+12+word(context,20)*4;
    gte_cmd(0x180001);
    u32 bucket=(u32)((s32)(((s32)depth>>2)-word(context,4))>>2);
    if(bucket<4096) {
        u32 previous=ot[bucket];*(u32 *)out=0x09000000|(previous&0xffffff);
        ot[bucket]=(previous&0xff000000)|((u32)out&0xffffff);
    }
    u32 rgb=*(const u16 *)color|((u32)color[2]<<16)|word(context,0);
    *(u32 *)(out+32)=gte_mfc2(14);
    u32 fade=word(context,8);
    if((s32)fade>=0) {gte_mtc2(8,4096-fade);gte_mtc2(6,rgb);gte_cmd(0x780010);rgb=gte_mfc2(22);}
    *(u32 *)(out+4)=rgb;
}
void o0b_func_800D437C(const u8 *points,const void *context,u8 *out,u32 *ot) {
    const u8 *colors=(const u8 *)word(context,4);
    u32 fade=word(context,24),count=word(context,28),code=word(context,36)<<24;
    if(!count) port_fatal("zero-count battle point batch");
    gte_mtc2(1,0);for(unsigned i=0;i<3;i++) gte_ctc2(21+i,0);
    for(u32 i=0;i<count;i++,points+=3,out+=12) {
        u32 xy=(((u32)(s32)(s8)points[0]<<2)&65535)|((u32)(s32)(s8)points[1]<<18);
        const u8 *color=colors+(points[2]>>4)*4;
        gte_mtc2(0,xy);gte_cmd(0x180001);
        *(u32 *)(out+8)=gte_mfc2(14);
        u32 rgb=*(const u16 *)color|((u32)color[2]<<16)|code;
        if((s32)fade>=0) {gte_mtc2(8,4096-fade);gte_mtc2(6,rgb);gte_cmd(0x780010);rgb=gte_mfc2(22);}
        *(u32 *)(out+4)=rgb;
        u32 previous=*ot;*(u32 *)out=0x02000000|(previous&0xffffff);
        *ot=(previous&0xff000000)|((u32)out&0xffffff);
    }
}
/* Fixed-size textured sprites, projected individually into one OT bucket. */
void o0b_func_800D4228(const u8 *points,const void *context,u8 *out,u32 *ot) {
    const u16 *uv=(const u16 *)word(context,0);
    const u8 *colors=(const u8 *)word(context,4);
    u32 half=word(context,8)>>1,clut=word(context,20)<<16;
    u32 fade=word(context,24),count=word(context,28),code=word(context,36)<<24;
    if(!count) port_fatal("zero-count fixed battle sprite batch");
    gte_mtc2(1,0);for(unsigned i=0;i<3;i++) gte_ctc2(21+i,0);
    for(u32 i=0;i<count;i++,points+=3,out+=16) {
        u32 xy=(((u32)(s32)(s8)points[0]<<2)&65535)|((u32)(s32)(s8)points[1]<<18);
        gte_mtc2(0,xy);gte_cmd(0x180001);
        u32 selector=points[2];const u8 *color=colors+(selector>>4)*4;
        *(u32 *)(out+12)=uv[selector&15]|clut;
        xy=gte_mfc2(14);
        *(u16 *)(out+8)=(u16)((u32)(s32)(s16)xy-half);
        *(u16 *)(out+10)=(u16)((u32)(s32)(s16)(xy>>16)-half);
        u32 rgb=*(const u16 *)color|((u32)color[2]<<16)|code;
        if((s32)fade>=0) {gte_mtc2(8,4096-fade);gte_mtc2(6,rgb);gte_cmd(0x780010);rgb=gte_mfc2(22);}
        *(u32 *)(out+4)=rgb;
        u32 previous=*ot;*(u32 *)out=0x03000000|(previous&0xffffff);
        *ot=(previous&0xff000000)|((u32)out&0xffffff);
    }
}
void o0b_func_800D3ED4(const u8 *points,const void *context,u8 *out,u32 *ot) {
    const u16 *uv=(const u16 *)word(context,0);
    const u8 *colors=(const u8 *)word(context,4);
    u32 width=word(context,8),height=word(context,12),page=word(context,16)<<16;
    u32 clut=word(context,20)<<16,fade=word(context,24),count=word(context,28);
    u32 bias=word(context,32),code=word(context,36)<<24;
    gte_mtc2(1,0);gte_mtc2(3,0);gte_mtc2(5,0);
    for(unsigned i=0;i<3;i++) gte_ctc2(21+i,0);
    for(u32 i=0;i<count;i++,points+=3,out+=40) {
        u32 x=(u32)(s32)(s8)points[0]<<2,y=(u32)(s32)(s8)points[1]<<2;
        u32 x0=(x-width)&65535,x1=(x+width)&65535,y0=(y-height)<<16,y1=(y+height)<<16;
        gte_mtc2(0,x0|y0);gte_mtc2(2,x1|y0);gte_mtc2(4,x0|y1);
        gte_cmd(0x280030);
        u32 selector=points[2],u=uv[selector&15]&255,v=uv[selector&15]>>8;
        u32 u1=u+(width>>1),v1=(v+(height>>1))<<8;v<<=8;
        *(u32 *)(out+12)=u|v|clut;*(u32 *)(out+20)=u1|v|page;
        *(u32 *)(out+28)=u|v1;*(u32 *)(out+36)=u1|v1;
        for(unsigned j=0;j<3;j++) *(u32 *)(out+8+j*8)=gte_mfc2(12+j);
        if(!gte_mfc2(24)) return;
        u32 depth=gte_mfc2(19);gte_mtc2(0,x1|y1);
        const u8 *color=colors+(selector>>4)*4;
        gte_cmd(0x180001);
        u32 rgb=*(const u16 *)color|((u32)color[2]<<16)|code;
        *(u32 *)(out+32)=gte_mfc2(14);
        if((s32)fade>=0) {gte_mtc2(8,4096-fade);gte_mtc2(6,rgb);gte_cmd(0x780010);rgb=gte_mfc2(22);}
        *(u32 *)(out+4)=rgb;
        u32 index=(u32)((s32)((u32)((s32)depth>>2)-bias)>>2);
        if(index<4096) {
            u32 previous=ot[index];*(u32 *)out=0x09000000|(previous&0xffffff);
            ot[index]=(previous&0xff000000)|((u32)out&0xffffff);
        }
    }
}
static void effect_link(u8 *packet, u32 *ot, u32 length) {
    u32 previous = *ot;
    *(u32 *)packet = length << 24 | (previous & 0xffffff);
    *ot = (previous & 0xff000000) | ((u32)packet & 0xffffff);
}
/* Ten-byte source sprites and a separate stream of draw-mode packets. The
 * previous page's mode is linked when the page changes, before the next
 * sprite; page zero deliberately emits no mode packet. */
u8 *o0b_func_800D45FC(const u8 *rect, const void *context, u8 *out, u32 *ot) {
    u32 page = 0, count = word(context, 20);
    u8 *mode = (u8 *)word(context, 24);
    for (u32 i = 0; i < count; i++, rect += 10, out += 20) {
        *(u32 *)(out + 4) = word(context, 0);
        *(u16 *)(out + 8) = (u16)((s32)(s8)rect[0] * 4 + word(context, 4));
        *(u16 *)(out + 10) = (u16)((s32)(s8)rect[1] * 4 + word(context, 8));
        *(u16 *)(out + 16) = rect[2];
        *(u16 *)(out + 18) = rect[3];
        *(u32 *)(out + 12) = *(const u16 *)(rect + 4) | word(context, 16) << 16;
        u32 next_page = *(const u16 *)(rect + 8);
        if (next_page != page) {
            if (page) {
                *(u32 *)(mode + 4) = 0xe1000200 | page | word(context, 12);
                effect_link(mode, ot, 1);mode += 8;
            }
            page = next_page;
        }
        effect_link(out, ot, 4);
    }
    if (page) {
        *(u32 *)(mode + 4) = 0xe1000200 | page | word(context, 12);
        effect_link(mode, ot, 1);mode += 8;
    }
    return mode;
}
static u32 strip_color(u32 color, u32 fade) {
    gte_mtc2(8, 4096 - fade);
    gte_mtc2(6, color);
    gte_cmd(0x780010);
    return gte_mfc2(22);
}
/* Original handwritten Gouraud strip emitter, including its MAC0 early exit
 * and separate draw-mode packet. Context offsets follow Unk800D4A30Ctx. */
void o0b_func_800D4A30(u32 stride, void *context, u8 *out, u32 *ot) {
    u32 *ctx = context;
    const u16 *scales = (const u16 *)word(context, 8);
    const u16 *sizes = (const u16 *)word(context, 12);
    const u16 *points = (const u16 *)word(context, 16);
    const u32 *colors0 = (const u32 *)word(context, 20);
    const u32 *colors1 = (const u32 *)word(context, 32);
    for (unsigned i = 0; i < 3; i++) gte_ctc2(21 + i, 0);
    do {
        for (unsigned axis = 0; axis < 2; axis++) {
            u32 size = (u32)*sizes << 2, scaled = size;
            if (!axis) sizes++;
            if (scales[0]) {
                gte_mtc2(8, scales[1]);
                gte_mtc2(9, size);
                gte_cmd(0x98003d);
                scaled = gte_mfc2(9);
            }
            ctx[11 + axis] = size;
            ctx[13 + axis] = scaled;
        }
        u32 previous0 = 0, previous1 = 0;
        for (s32 row = 0; row <= (s32)ctx[7]; row++) {
            u32 xy[2];
            for (unsigned side = 0; side < 2; side++) {
                gte_mtc2(9, points[0]);
                gte_mtc2(10, points[1]);
                gte_mtc2(8, ctx[11 + side + 2 * (row & 1)]);
                gte_cmd(0x98003d);
                xy[side] = (gte_mfc2(9) & 65535) | (gte_mfc2(10) << 16);
            }
            points += 2;
            if (row) {
                gte_mtc2(0, previous1);
                gte_mtc2(2, xy[1]);
                gte_mtc2(4, previous0);
                gte_cmd(0x280030);
                *(u32 *)(out + 40) = 0xe1000200 | ctx[10];
                for (unsigned j = 0; j < 3; j++) *(u32 *)(out + 8 + j * 8) = gte_mfc2(12 + j);
                if (!gte_mfc2(24)) return;
                u32 depth = gte_mfc2(19);
                gte_mtc2(0, xy[0]);
                gte_cmd(0x180001);
                u32 index = (u32)((s32)((u32)((s32)depth >> 2) - ctx[6]) >> 2);
                if (index < 4096) {
                    u32 previous = ot[index];
                    *(u32 *)out = 0x08000000 | (previous & 0xffffff);
                    *(u32 *)(out + 36) = 0x01000000 | ((u32)out & 0xffffff);
                    ot[index] = (previous & 0xff000000) | ((u32)(out + 36) & 0xffffff);
                }
                *(u32 *)(out + 32) = gte_mfc2(14);
                const u32 *first = (row & 1) ? colors1 : colors0;
                const u32 *second = (row & 1) ? colors0 : colors1;
                *(u32 *)(out + 20) = strip_color(first[0], ctx[1]);
                *(u32 *)(out + 28) = strip_color(second[0], ctx[1]);
                *(u32 *)(out + 4) = strip_color(first[stride], ctx[1]) | ctx[0];
                *(u32 *)(out + 12) = strip_color(second[stride], ctx[1]);
                out += 44;
            }
            previous0 = xy[0];previous1 = xy[1];
        }
        colors0 += stride;colors1 += stride;
    } while (--ctx[9]);
}
void o0b_func_800D4478(const u8 *rect, const void *context, u8 *out, u32 *ot) {
    u32 clut = word(context, 16) << 16;
    u32 count = word(context, 20), bias = word(context, 24);
    u32 color = word(context, 0), page = word(context, 12);
    u32 origin_x = word(context, 4) << 2, origin_y = word(context, 8) << 2;
    gte_mtc2(1, 0); gte_mtc2(3, 0); gte_mtc2(5, 0);
    for (u32 i = 0; i < count; i++, rect += 10, out += 40) {
        u32 x0 = ((u32)(s32)(s8)rect[0]*16 + origin_x) & 65535;
        u32 y0 = (u32)(s32)(s8)rect[1]*16 + origin_y;
        u32 x1 = (x0 + ((u32)rect[2] << 2)) & 65535;
        u32 y1 = y0 + ((u32)rect[3] << 2);
        gte_mtc2(0, x0 | (y0 << 16));
        gte_mtc2(2, x1 | (y0 << 16));
        gte_mtc2(4, x0 | (y1 << 16));
        *(u32 *)(out + 4) = color;
        gte_cmd(0x280030); /* RTPT */
        u32 uv0 = *(const u16 *)(rect + 4), uv1 = *(const u16 *)(rect + 6);
        u32 u0 = uv0 & 255, v0 = uv0 & 0xFF00;
        u32 u1 = uv1 & 255, v1 = uv1 & 0xFF00;
        *(u32 *)(out + 12) = u0 | v0 | clut;
        *(u32 *)(out + 20) = u1 | v0 | ((*(const u16 *)(rect + 8) | page) << 16);
        for (unsigned j = 0; j < 3; j++) *(u32 *)(out + 8 + j*8) = gte_mfc2(12 + j);
        *(u32 *)(out + 28) = u0 | v1;
        if (!gte_mfc2(24)) return;
        u32 depth = gte_mfc2(19);
        gte_mtc2(0, x1 | (y1 << 16));
        *(u32 *)(out + 36) = u1 | v1;
        gte_cmd(0x180001); /* RTPS */
        u32 index = (u32)((s32)((u32)((s32)depth >> 2) - bias) >> 2);
        if (index < 4096) {
            u32 previous = ot[index];
            *(u32 *)out = 0x09000000 | (previous & 0xFFFFFF);
            ot[index] = (previous & 0xFF000000) | ((u32)out & 0xFFFFFF);
        }
        *(u32 *)(out + 32) = gte_mfc2(14);
    }
}
void o0b_func_800D40CC(const u8 *points, const void *context, u8 *out, u32 *ot) {
    const u16 *uv = (const u16 *)word(context, 0);
    const u8 *colors = (const u8 *)word(context, 4);
    u32 width = word(context, 8), height = word(context, 12);
    u32 clut = word(context, 20) << 16, fade = word(context, 24);
    u32 count = word(context, 28), code = word(context, 36) << 24;
    if (!count) port_fatal("zero-count battle sprite batch");
    gte_mtc2(1, 0);
    for (unsigned i = 0; i < 3; i++) gte_ctc2(21 + i, 0);
    do {
        u32 xy = (((u32)(s32)(s8)points[0] << 2) & 65535) |
                 ((u32)(s32)(s8)points[1] << 18);
        gte_mtc2(0, xy);
        *(u16 *)(out + 16) = (u16)width;
        *(u16 *)(out + 18) = (u16)height;
        gte_cmd(0x180001);
        u32 selector = points[2]; points += 3;
        const u8 *color = colors + (selector >> 4)*4;
        *(u32 *)(out + 12) = uv[selector & 15] | clut;
        u32 rgb = *(const u16 *)color | ((u32)color[2] << 16) | code;
        xy = gte_mfc2(14);
        *(u16 *)(out + 8) = (u16)((u32)(s32)(s16)xy - (width >> 1));
        *(u16 *)(out + 10) = (u16)((u32)(s32)(s16)(xy >> 16) - (height >> 1));
        if ((s32)fade >= 0) {
            gte_mtc2(8, 4096 - fade); gte_mtc2(6, rgb);
            gte_cmd(0x780010); /* DPCS sf=1 */
            rgb = gte_mfc2(22);
        }
        *(u32 *)(out + 4) = rgb;
        u32 previous = *ot;
        *(u32 *)out = 0x04000000 | (previous & 0xFFFFFF);
        *ot = (previous & 0xFF000000) | ((u32)out & 0xFFFFFF);
        out += 20;
    } while (--count);
}

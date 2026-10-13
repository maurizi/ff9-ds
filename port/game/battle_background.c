/* ovl_0b's handwritten battle-background transform and packet emitter.
 * Record/UV indices are byte offsets, not vertex numbers. Keep the two scratch
 * arrays and ordering-table rules used by the original MIPS code. */
#include "psyq_port.h"
#include "gte.h"

static u16 half(const void *p, unsigned off) {
    return *(const u16 *)((const u8 *)p + off);
}
static u32 word(const void *p, unsigned off) {
    return *(const u32 *)((const u8 *)p + off);
}
static void store(void *p, unsigned off, u32 v) {
    *(u32 *)((u8 *)p + off) = v;
}
static void transform_three(const u8 *vertices, unsigned offset) {
    /* Packed vertices have six bytes each; GTE vector registers use eight. */
    for (unsigned i = 0; i < 3; i++) {
        gte_mtc2(i * 2, half(vertices, i * 6) | ((u32)half(vertices, i * 6 + 2) << 16));
        gte_mtc2(i * 2 + 1, (((offset / 4) + i) & 1)
            ? half(vertices, i * 6 + 4) : word(vertices, i * 6 + 4));
    }
    gte_cmd(0x280030); /* RTPT, sf=1 */
    for (unsigned i = 0; i < 3; i++) {
        store(psx_scratchpad, offset + i * 4, gte_mfc2(12 + i));
        store(psx_scratchpad, 0x200 + offset + i * 4, gte_mfc2(17 + i) >> 4);
    }
}

/* Original batches are padded to six vertices, with a lookahead read after the
 * final batch. That read has no observable output; preserve the final GTE state. */
void o0b_func_800CF448(u32 unused0, u32 unused1, const u8 *vertices, u32 count) {
    (void)unused0; (void)unused1;
    if (!count || count % 6 || count > 128)
        port_fatal("battle background: invalid padded vertex count %u", count);
    for (unsigned i = 0; i < count; i += 3)
        transform_three(vertices + i * 6, i * 4);
}

static u32 shade(u32 color, u16 brightness) {
    u32 rgb = color & 0xFFFFFF;
    if (brightness != 0x80) {
        gte_mtc2(8, brightness);
        gte_mtc2(9, color & 255);
        gte_mtc2(10, (color >> 8) & 255);
        gte_mtc2(11, (color >> 16) & 255);
        gte_cmd(0x90003D); /* GPF, sf=0 */
        rgb = (gte_mfc2(9) >> 7) + ((gte_mfc2(10) >> 7) << 8)
            + ((gte_mfc2(11) >> 7) << 16);
    } else {
        /* MTC2 IR0 is in the branch delay slot even at default brightness. */
        gte_mtc2(8, brightness);
    }
    return rgb;
}

u8 *o0b_func_800CEED4(const void *record, u8 *out, u32 *ot, const void *group) {
    const u8 *rec = record, *grp = group;
    u32 flags = half(rec, 0), shift = half((const void *)PSX_ADDR(0x800CF5A0), 0);
    u16 brightness = half((const void *)PSX_ADDR(0x800CF588), 0);
    const u8 *colors = rec + half(rec, 6);
    const u8 *indices = rec + half(rec, 8);
    const u8 *uvs = rec + half(rec, 10);
    const u8 *pages = grp + half(grp, 10);
    o0b_func_800CF448(0, 0, grp + half(grp, 14) + half(rec, 4) * 6, half(rec, 2));
    for (unsigned triangle = 0; triangle < 2; triangle++) {
        unsigned n = half(rec, triangle ? 14 : 12);
        unsigned stride = triangle ? 6 : 8;
        for (unsigned p = 0; p < n; p++, indices += stride, uvs += stride, colors += 4) {
            u32 xy[4], depth[4];
            unsigned vertices = triangle ? 3 : 4;
            for (unsigned i = 0; i < vertices; i++) {
                unsigned index = half(indices, i * 2);
                xy[i] = word(psx_scratchpad, index);
                /* Triangle's third depth is a word; the other reads are LHU. */
                depth[i] = triangle && i == 2 ? word(psx_scratchpad, 0x200 + index)
                    : half(psx_scratchpad, 0x200 + index);
            }
            for (unsigned i = 0; i < 3; i++) gte_mtc2(12 + i, xy[i]);
            gte_cmd(0x400006); /* NCLIP */
            if ((s32)gte_mfc2(24) < 0) continue;
            u32 bucket;
            if (triangle) {
                bucket = (((depth[0] + depth[1]) >> 1) + depth[2]) >> 1;
                if (!flags) {
                    if (!bucket) continue;
                    bucket = (bucket << (shift & 31)) << 2;
                    if (bucket & 0xC000) bucket = 0x3FFC;
                }
            } else {
                bucket = depth[0] + depth[1] + depth[2] + depth[3];
                if (!flags) {
                    bucket = (bucket << (shift & 31)) & 0xFFFC;
                    if (!bucket) continue;
                    if (bucket & 0xC000) bucket = 0x3FFC;
                }
            }
            if (flags) {
                if (flags & 8) bucket = 0x3FC;
                else if (flags & 2) bucket = 0;
                else {
                    bucket >>= triangle ? 4 : 6;
                    if (bucket & 0xFF00) bucket = 0x3FC;
                    else {
                        bucket <<= 2;
                        if (!bucket) continue;
                    }
                }
            }
            u32 color = word(colors, 0);
            u32 command = (triangle ? 0x24000000u : 0x2C000000u)
                + ((color & 0x20000000) ? 0x02000000u : 0);
            u32 rgb = shade(color, brightness);
            store(out, 4, triangle ? command | rgb : command + rgb);
            for (unsigned i = 0; i < vertices; i++) store(out, 8 + i * 8, xy[i]);
            u32 page = word(pages, (color >> 22) & 0x7C) | ((color >> 25) & 0x60);
            store(out, 12, half(uvs, 0) | (page & 0xFFFF0000));
            store(out, 20, half(uvs, 2) | (page << 16));
            *(u16 *)(out + 28) = half(uvs, 4);
            if (!triangle) *(u16 *)(out + 36) = half(uvs, 6);
            u32 *entry = (u32 *)((u8 *)ot + bucket);
            store(out, 0, ((triangle ? 7u : 9u) << 24) | (*entry & 0xFFFFFF));
            *entry = (*entry & 0xFF000000) | ((u32)out & 0xFFFFFF);
            out += triangle ? 32 : 40;
        }
    }
    return out;
}

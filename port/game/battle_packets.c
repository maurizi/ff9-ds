/* Battle mesh packet emitter, 800C58E4. Source meshes contain eight consecutive
 * primitive lists. Vertex projection/lighting is performed by the existing C
 * path; this routine packs those results and links them into the active OT. */
#include "psyq_port.h"
#include "gte.h"
extern u32 g_vtxScreenXY[], g_vtxScreenZ[];
static u16 h(const void *p, unsigned off) { return *(const u16 *)((const u8 *)p + off); }
static u32 w(const void *p, unsigned off) { return *(const u32 *)((const u8 *)p + off); }
static void putw(void *p, unsigned off, u32 value) { *(u32 *)((u8 *)p + off) = value; }
static void *ptr(const void *p, unsigned off) { return (void *)w(p, off); }
struct Format { u8 source_size, packet_size, vertices, flags, uv, colors, texture, command; };
static const struct Format formats[8] = {
    {24,40,4,21,8,16,19,0x2C}, {20,32,3,18,12,8,6,0x24},
    {32,52,4,28,8,16,29,0x3C}, {24,40,3,15,6,16,22,0x34},
    {24,36,4,20,0,8,0,0x38}, {20,28,3,19,0,8,0,0x30},
    {16,24,4,13,0,8,0,0x28}, {12,20,3,11,0,8,0,0x20}
};
u8 *o0b_func_800C58E4(void *model, u8 mesh_index, u8 *out) {
    if (!(h(model, 0) & 0x10)) {
        if (!w(model, 16)) putw(model, 16, (u32)out);
        const u8 *mesh = (const u8 *)ptr(ptr(model, 8), 16) + mesh_index * 40;
        const u8 *src = ptr(mesh, 28), *uv = ptr(mesh, 32), *colors = ptr(mesh, 36);
        const u8 *pages = ptr(model, 24);
        const u8 *gs = *(u8 **)(void *)PSX_ADDR(0x8006794C), *gd = ptr(gs, 28);
        u8 *ot = (u8 *)ptr(gd, 16 + gd[8] * 4) - (s8)mesh[19] * 4;
        for (unsigned type = 0; type < 8; type++) {
            const struct Format *f = &formats[type];
            unsigned count = h(mesh, 2 + type * 2);
            int textured = type < 4, gouraud = type >= 2 && type < 6;
            unsigned step = 4 + (textured ? 4 : 0) + (gouraud ? 4 : 0);
            for (unsigned p = 0; p < count; p++, src += f->source_size) {
                u32 xy[4], z[4];
                u8 flags = src[f->flags];
                for (unsigned i = 0; i < f->vertices; i++) {
                    unsigned index = h(src, i * 2);
                    xy[i] = g_vtxScreenXY[index]; z[i] = g_vtxScreenZ[index];
                }
                /* Every triangle cull branch has SW xy0 in its delay slot. */
                if (f->vertices == 3) putw(out, 8, xy[0]);
                if (!(flags & 2)) {
                    for (unsigned i = 0; i < 3; i++) gte_mtc2(12 + i, xy[i]);
                    gte_cmd(0x400006);
                    if ((s32)gte_mfc2(24) <= 0) continue;
                }
                u32 bucket;
                if (f->vertices == 4) bucket = ((z[0] + z[1] + z[2] + z[3]) >> 6) << 2;
                else {
                    for (unsigned i = 0; i < 3; i++) gte_mtc2(17 + i, z[i]);
                    gte_cmd(0x58002D); /* AVSZ3 */
                    bucket = gte_mfc2(7) & ~3u;
                }
                u32 command = (u32)f->command << 24;
                if (textured && (flags & 1)) command |= 0x02000000;
                u32 page = textured ? w(pages, src[f->texture] * 4) : 0;
                for (unsigned i = 0; i < f->vertices; i++) {
                    putw(out, 8 + i * step, xy[i]);
                    if (gouraud || !i) {
                        u32 color = gouraud ? w(colors, h(src, f->colors + i * 2) * 4) : w(src, f->colors);
                        if (textured && !gouraud) color &= 0xFFFFFF;
                        if (!i) color |= command;
                        putw(out, i ? 4 + i * step : 4, color);
                    }
                    if (textured) {
                        u32 pair = h(uv, h(src, f->uv + i * 2) * 2);
                        if (!i) pair |= page & 0xFFFF0000;
                        if (i == 1) pair |= (page | (flags & 0xE0)) << 16;
                        putw(out, 12 + i * step, pair);
                    }
                }
                u32 *entry = (u32 *)(ot + bucket);
                putw(out, 0, *entry | ((f->packet_size / 4 - 1u) << 24));
                *entry = (u32)out & 0xFFFFFF;
                out += f->packet_size;
            }
        }
    }
    out[7] = 0xFF;
    return out + 8;
}

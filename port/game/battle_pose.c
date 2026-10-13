/* Battle model pose builder (ovl_0b 800C7DAC). Packed animation keys use the
 * resident decoder; bone composition preserves the original GTE register path. */
#include "psyq_port.h"
#include "gte.h"
extern void func_80031264(void *, void *);
extern void port_ldrotkey(const void *, const void *, s32, void *);
extern void port_ldrotkey_lo(const void *, const void *, s32, void *);
static u16 h(const void *p, unsigned off) { return *(const u16 *)((const u8 *)p + off); }
static u32 w(const void *p, unsigned off) { return *(const u32 *)((const u8 *)p + off); }
static void putw(void *p, unsigned off, u32 value) { *(u32 *)((u8 *)p + off) = value; }
static void *ptr(const void *p, unsigned off) { return (void *)w(p, off); }
static void copy_matrix(void *dst, const void *src) {
    for (unsigned i = 0; i < 32; i += 4) putw(dst, i, w(src, i));
}
static void load_rotation(const void *m) {
    for (unsigned i = 0; i < 5; i++) gte_ctc2(i, w(m, i * 4));
}
static void load_translation(const void *m) {
    for (unsigned i = 0; i < 3; i++) gte_ctc2(5 + i, w(m, 20 + i * 4));
}
static void rotate_columns(void *matrix) {
    for (unsigned col = 0; col < 3; col++) {
        for (unsigned row = 0; row < 3; row++) gte_mtc2(9 + row, h(matrix, col * 2 + row * 6));
        gte_cmd(0x49E012); /* MVMVA sf=1, rotation, IR vector, no translation */
        for (unsigned row = 0; row < 3; row++)
            *(u16 *)((u8 *)matrix + col * 2 + row * 6) = (u16)gte_mfc2(9 + row);
    }
}
static void transform_translation(void *translation) {
    gte_mtc2(0, h(translation, 0) | ((u32)h(translation, 4) << 16));
    gte_mtc2(1, w(translation, 8));
    gte_cmd(0x480012); /* MVMVA sf=1, rotation, V0, translation */
    for (unsigned i = 0; i < 3; i++) putw(translation, i * 4, gte_mfc2(25 + i));
}
static void parent_root(const void *model, void *matrix) {
    const u8 *parent = ptr(model, 28);
    copy_matrix(matrix, (const u8 *)ptr(parent, 32) + ((const u8 *)model)[4] * 32);
    load_rotation(matrix);
    gte_mtc2(0, w(model, 116)); gte_mtc2(1, w(model, 120));
    gte_cmd(0x486012); /* MVMVA without translation */
    for (unsigned i = 0; i < 3; i++)
        putw(matrix, 20 + i * 4, w(matrix, 20 + i * 4) + (s32)(s16)gte_mfc2(9 + i));
}

u8 *o0b_func_800C7DAC(void *model, s32 frame, u8 *out) {
    const u8 *anim = ptr(model, 12), *mesh = ptr(model, 8);
    putw(model, 32, (u32)out);
    if (!anim) {
        if (ptr(model, 28)) parent_root(model, out);
        else {
            for (unsigned i = 0; i < 9; i++) {
                u16 value = h(model, 36 + i * 2);
                if (i % 3) value = (u16)(-value);
                *(u16 *)(out + i * 2) = value;
            }
            for (unsigned i = 0; i < 3; i++) putw(out, 20 + i * 4, w(model, 56 + i * 4));
        }
        return out + 32;
    }
    unsigned count = mesh[2];
    if (!count) port_fatal("battle pose: zero bone count");
    u8 *matrices = count < 31 ? psx_scratchpad + 24 : out;
    const u8 *keys = ptr(anim, 12), *low = ptr(anim, 16);
    putw(psx_scratchpad, 8, 0); putw(psx_scratchpad, 12, 0);
    for (unsigned i = 0; i < 3; i++) {
        unsigned field = h(anim, 4 + i * 2);
        s32 value = (h(anim, 10) & (1u << i)) ? (s16)field
            : (s16)h(anim, field + ((u32)frame & 65535) * 2);
        putw(matrices, 20 + i * 4, (u32)value);
    }
    for (unsigned i = 0; i < count; i++) {
        port_ldrotkey(keys + i * 8, anim, frame, psx_scratchpad);
        if (low) port_ldrotkey_lo(low + i * 8, anim, frame, psx_scratchpad);
        func_80031264(psx_scratchpad, matrices + i * 32);
    }
    if (ptr(model, 28)) parent_root(model, matrices);
    else {
        load_rotation((const u8 *)model + 36);
        load_translation((const u8 *)model + 36);
        rotate_columns(matrices);
        transform_translation(matrices + 20);
    }
    const u8 *bones = (const u8 *)ptr(mesh, 12) + 4;
    for (unsigned i = 1; (i < count || i == 1); i++, bones += 4) {
        const u8 *parent = matrices + bones[3] * 32;
        load_rotation(parent);
        /* Translation loads happen after the first column's MVMVA; they do not
         * affect that command (cv=3). Preserve the same final register state. */
        load_translation(parent);
        rotate_columns(matrices + i * 32);
        putw(psx_scratchpad, 16, (s32)(s16)h(bones, 0));
        gte_mtc2(0, h(psx_scratchpad, 8) | ((u32)h(psx_scratchpad, 12) << 16));
        gte_mtc2(1, w(psx_scratchpad, 16));
        gte_cmd(0x480012);
        for (unsigned axis = 0; axis < 3; axis++)
            putw(matrices, i * 32 + 20 + axis * 4, gte_mfc2(25 + axis));
    }
    if (matrices != out) {
        for (unsigned i = 0; i < count; i++) copy_matrix(out + i * 32, matrices + i * 32);
    }
    return out + count * 32;
}

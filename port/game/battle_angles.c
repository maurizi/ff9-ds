/* Battle orientation helpers. Keep the handwritten signed comparisons and
 * 32-bit wrapping in their square-root loop, including large-distance cases. */
#include "psyq_port.h"
#include "gte.h"
static s32 radial_root(u32 remainder) {
    u32 root = 0, trial = 1;
    for (int shift = 30; shift >= 0; shift -= 2) {
        u32 bit = trial << shift;
        root <<= 1;
        if ((s32)remainder >= (s32)bit) { root |= 4; remainder -= bit; }
        trial = root + 1;
    }
    return (s32)root >> 2;
}
void o0b_func_800CF808(const s16 *from, const s16 *to, s16 *rotation) {
    s32 dx = (s32)to[0] - from[0], dz = (s32)to[2] - from[2];
    s32 dy = (s32)to[1] - from[1];
    gte_mtc2(9, (u32)dx); gte_mtc2(10, (u32)dz);
    gte_cmd(0xA00428); /* SQR sf=0 */
    rotation[1] = (s16)(0x400 - ratan2(dz, dx));
    s32 radius = radial_root(gte_mfc2(25) + gte_mfc2(26));
    rotation[2] = 0;
    rotation[0] = (s16)(-ratan2(dy, radius));
}
s32 o0b_func_800CF91C(const SVECTOR *vector, s32 *depth, s32 *radius) {
    gte_mtc2(0, *(const u32 *)vector);
    gte_mtc2(1, *((const u32 *)vector + 1));
    gte_cmd(0x480012);
    s32 x = (s32)gte_mfc2(25), y = (s32)gte_mfc2(26);
    s32 z = (s32)gte_mfc2(27);
    gte_cmd(0xA00428);
    *depth = z;
    *radius = radial_root(gte_mfc2(25) + gte_mfc2(26));
    return ratan2(x, y);
}
void o0b_func_800CF8EC(s32 depth, s32 packed_xy, s16 *out) {
    gte_mtc2(0, (u32)packed_xy); gte_mtc2(1, (u32)depth);
    gte_cmd(0x480012);
    for (unsigned i = 0; i < 3; i++) out[i] = (s16)gte_mfc2(25 + i);
}
void o0b_func_800CFA00(const s16 *from, const s16 *to, s32 factor, s16 *out) {
    gte_mtc2(8, (u32)factor);
    for (unsigned i = 0; i < 3; i++) {
        gte_mtc2(9 + i, (u32)(s32)from[i]);
        gte_ctc2(21 + i, (u32)(s32)to[i]);
    }
    gte_cmd(0x980011); /* INTPL sf=1 */
    for (unsigned i = 0; i < 3; i++) out[i] = (s16)gte_mfc2(25 + i);
}
s32 o0b_func_800CF9B0(s32 from, s32 to, s32 factor) {
    gte_mtc2(8, (u32)factor);
    gte_mtc2(9, (u32)from);
    gte_ctc2(21, (u32)to);
    gte_cmd(0x980011);
    return (s32)gte_mfc2(25);
}

/* Shared particle integration entries: drag at 0x20 or 0x2A, signed timestep
 * at 0x1E, 20.12 positions and packed velocity. Preserve the GTE GPL path:
 * gravity changes the stored velocity but not the IR used for this step. */
#include "psyq_port.h"
#include "gte.h"
static void integrate(u8 *entry, s32 gravity, const s16 *acceleration, s16 *out,
                      unsigned drag_offset) {
    gte_mtc2(8, (u32)(s32)*(s16 *)(entry + drag_offset));
    for (unsigned i = 0; i < 2; i++)
        gte_mtc2(9 + i, (u32)(s32)*(s16 *)(entry + 12 + i*2));
    gte_mtc2(11, *(u32 *)(entry + 16));
    gte_cmd(0x198003D); /* GPF sf=1 */
    s32 velocity[3];
    for (unsigned i = 0; i < 3; i++) velocity[i] = (s32)gte_mfc2(9 + i);
    s32 step = *(s8 *)(entry + 30);
    velocity[1] = (s32)((u32)velocity[1] +
        (u32)((s32)((u32)gravity * (u32)step) >> 7));
    gte_mtc2(8, 64);
    for (unsigned i = 0; i < 3; i++) gte_mtc2(25 + i, *(u32 *)(entry + i*4));
    gte_cmd(0x1A0003E); /* GPL sf=0 */
    for (unsigned i = 0; i < 3; i++) *(s16 *)(entry + 12 + i*2) = (s16)velocity[i];
    u32 position[3];
    for (unsigned i = 0; i < 3; i++) position[i] = gte_mfc2(25 + i);
    gte_mtc2(8, (u32)step << 6);
    for (unsigned i = 0; i < 3; i++) gte_mtc2(9 + i, (u32)(s32)acceleration[i]);
    gte_cmd(0x198003D);
    for (unsigned i = 0; i < 3; i++) {
        u32 value = position[i] + (gte_mfc2(25 + i) << 6);
        *(u32 *)(entry + i*4) = value;
        out[i] = (s16)((s32)value >> 12);
    }
}
void o0b_func_800DFE4C(void *entry, s32 gravity, const s16 *acceleration, SVECTOR *out) {
    integrate(entry, gravity, acceleration, (s16 *)out, 0x20);
}
void o0b_func_800DFE54(void *entry, s32 gravity, const s16 *acceleration, SVECTOR *out) {
    integrate(entry, gravity, acceleration, (s16 *)out, 0x2A);
}

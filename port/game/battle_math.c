/* Scalar helpers from ovl_0b's handwritten battle-effect block. Keep the original
 * scratchpad RNG and code-adjacent state in the PSX shadow; no new random stream. */
#include "psyq_port.h"
#include "gte.h"

static u32 next_random(void) {
    u32 *seed = (u32 *)(psx_scratchpad + 0x3FC);
    *seed = *seed * 0x41C64E6Du + 0x3039u;
    return *seed >> 16;
}
void o0b_func_800CF58C(s32 value) {
    *(s32 *)PSX_ADDR(0x800CF5A0) = value;
}
void o0b_func_800CF560(s32 value) {
    *(s32 *)PSX_ADDR(0x800CF588) = value;
}
s32 o0b_func_800CF574(void) {
    return *(s32 *)PSX_ADDR(0x800CF588);
}
s32 o0b_func_800CF690(void) {
    return (s32)next_random();
}
s32 o0b_func_800CF708(s32 from, s32 to) {
    if (from == to) return from;
    s32 span = (s32)((u32)to - (u32)from);
    return (s32)((u32)from + (u32)((s32)next_random() % span));
}
s32 o0b_func_800CF748(s32 span) {
    if (!span) return 0;
    return (s32)((u32)((s32)next_random() % span) - ((u32)span >> 1));
}
void o0b_func_800CF788(SVECTOR *rot) {
    u32 r = next_random();
    u16 *phase = (u16 *)PSX_ADDR(0x800FE528);
    u32 n = (*phase + 1) & 7;
    *phase = (u16)n;
    rot->vx = (s16)((r & 0x3FF) + ((n << 9) & 0xC00));
    s32 y = (r >> 6) & 0x3FF;
    rot->vy = (s16)((n & 1) ? y : -y);
    rot->vz = 0;
}
s32 o0b_func_800CF6B8(s32 value, s32 iterations) {
    s32 estimate = value;
    do {
        if (!estimate) return 0;
        if (value == (s32)0x80000000u && estimate == -1)
            port_fatal("battle sqrt: MIPS divide overflow");
        s32 quotient = value / estimate;
        estimate = (s32)(((u32)estimate + (u32)quotient) >> 1);
    } while ((iterations = (s32)((u32)iterations - 1)) > 0);
    return estimate;
}
s32 o0b_func_800CF9DC(s32 from, s32 to, s32 step, s32 duration) {
    s32 product = (s32)(((u32)to - (u32)from) * (u32)step);
    s32 quotient;
    /* This entry uses raw DIV, without the compiler's trap instructions. */
    if (!duration) quotient = product < 0 ? 1 : -1;
    else if (product == (s32)0x80000000u && duration == -1) quotient = product;
    else quotient = product / duration;
    return (s32)((u32)from + (u32)quotient);
}

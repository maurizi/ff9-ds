/* gte_ldrotkey / gte_ldrotkey_lo (psyq.h): CPU-only animation key decode that the decomp writes as inline asm.
 * Translated from the asm with its branch delay slots executed (the original is noreorder).
 *
 * key: two words; their three u16 halves (x = w0 lo, y = w0 hi, z = w1 lo) are either a constant (flag bit set in
 * w1's high half: 1 x, 2 y, 4 z) or an offset into `anim` of a per-frame s8 track.
 */
#include <stdint.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int32_t s32;
typedef uint32_t u32;

static s32 key_byte(const u8 *anim, u32 field, u32 flags, u32 bit, s32 index) {
    return (flags & bit) ? (s32)field : (s32)(s8)anim[field + index];
}

/* rot = { x << 4, y << 4, z << 4 } from the high (8-bit) tracks */
void port_ldrotkey(const void *key, const void *anim, s32 frame, void *rot) {
    const u32 *k = key;
    u32 flags = k[1] >> 16;
    s32 x = key_byte(anim, k[0] & 0xFFFF, flags, 1, frame);
    s32 y = key_byte(anim, k[0] >> 16, flags, 2, frame);
    s32 z = key_byte(anim, k[1] & 0xFFFF, flags, 4, frame);
    ((u32 *)rot)[0] = ((u32)y << 20) | (((u32)x << 4) & 0xFFFF);
    ((u32 *)rot)[1] = (u32)z << 4;
}

/* ORs the low 4 bits from the nibble tracks into rot: two frames per byte, odd frames in the high nibble. */
void port_ldrotkey_lo(const void *key, const void *anim, s32 frame, void *rot) {
    const u32 *k = key;
    u32 flags = k[1] >> 16;
    s32 half = frame >> 1;
    s32 sh = (frame & 1) ? 4 : 0;
    u32 x, y, z;
    x = (flags & 1) ? (k[0] & 0xFFFF) : ((u32)(s32)(s8)((const u8 *)anim)[(k[0] & 0xFFFF) + half] >> sh);
    y = (flags & 2) ? (k[0] >> 16) : ((u32)(s32)(s8)((const u8 *)anim)[(k[0] >> 16) + half] >> sh);
    z = (flags & 4) ? (k[1] & 0xFFFF) : ((u32)(s32)(s8)((const u8 *)anim)[(k[1] & 0xFFFF) + half] >> sh);
    ((u32 *)rot)[0] |= (x & 0xF) | ((y & 0xF) << 16);
    ((u32 *)rot)[1] |= z & 0xF;
}

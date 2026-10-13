/* 800CF5A4: append a packed position/width to an effect's circular trail. */
#include "psyq_port.h"
#include "effect_vm.h"
s32 o0b_func_800CF5A4(u8 *buffer, s32 capacity, s32 width, s32 position) {
    const u8 *source = (const u8 *)port_effect_data((u32)position);
    s32 index = (s32)buffer[1] - 1;
    if (index < 0) index = capacity;
    u8 *entry = buffer + (u32)index * 8;
    u32 xy, packed;
    u16 z;
    __builtin_memcpy(&xy, source, 4);
    __builtin_memcpy(&z, source + 4, 2);
    packed = ((u32)width << 17) | z;
    __builtin_memcpy(entry + 4, &xy, 4);
    __builtin_memcpy(entry + 8, &packed, 4);
    buffer[1] = (u8)index;
    s32 count = buffer[2];
    buffer[2] = (u8)(count == capacity ? count : count + 1);
    return count;
}

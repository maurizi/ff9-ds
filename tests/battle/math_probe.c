#include "psyq_port.h"
#include <stdio.h>
#include <stdlib.h>

unsigned char psx_ram[PSX_RAM_SIZE] __attribute__((aligned(32)));
unsigned char psx_scratchpad[0x400] __attribute__((aligned(32)));
void port_fatal(const char *fmt, ...) { (void)fmt; abort(); }
void o0b_func_800CF58C(s32);
void o0b_func_800CF560(s32);
s32 o0b_func_800CF574(void);
s32 o0b_func_800CF690(void);
s32 o0b_func_800CF708(s32, s32);
s32 o0b_func_800CF748(s32);
void o0b_func_800CF788(SVECTOR *);
s32 o0b_func_800CF6B8(s32, s32);
s32 o0b_func_800CF9DC(s32, s32, s32, s32);
int main(void) {
    unsigned int kind, seed, a, b, c, d, phase;
    while (scanf("%u %x %x %x %x %x %x", &kind, &seed, &a, &b, &c, &d, &phase) == 7) {
        *(u32 *)(psx_scratchpad + 0x3FC) = seed;
        *(u32 *)(psx_ram + 0xCF588) = c;
        *(u32 *)(psx_ram + 0xCF5A0) = d;
        *(u16 *)(psx_ram + 0xFE528) = phase;
        SVECTOR rot = {0};
        u32 value = 0;
        switch (kind) {
        case 0: o0b_func_800CF58C(a); break;
        case 1: o0b_func_800CF560(a); break;
        case 2: value = o0b_func_800CF574(); break;
        case 3: value = o0b_func_800CF690(); break;
        case 4: value = o0b_func_800CF708(a, b); break;
        case 5: value = o0b_func_800CF748(a); break;
        case 6: o0b_func_800CF788(&rot); break;
        case 7: value = o0b_func_800CF6B8(a, b); break;
        case 8: value = o0b_func_800CF9DC(a, b, c, d); break;
        default: abort();
        }
        printf("%08x %08x %08x %08x %04x %04x %04x %04x\n", value,
               *(u32 *)(psx_scratchpad + 0x3FC), *(u32 *)(psx_ram + 0xCF588),
               *(u32 *)(psx_ram + 0xCF5A0), *(u16 *)(psx_ram + 0xFE528),
               (u16)rot.vx, (u16)rot.vy, (u16)rot.vz);
    }
    return 0;
}

#define main background_unused_main
#define gte_cmd background_gte_cmd
#include "background_probe.c"
#undef main
#undef gte_cmd
static s32 sign16(u32 v) { return (s16)v; }
void gte_ctc2(int r, u32 v) { g[32+r] = v; }
void gte_cmd(u32 op) {
    if ((op & 63) == 0x3D || (op & 63) == 0x3E) {
        unsigned shift = (op & (1u << 19)) ? 12 : 0;
        for (unsigned i = 0; i < 3; i++) {
            long long value = (long long)sign16(g[8])*sign16(g[9+i]);
            if ((op & 63) == 0x3E) value += (long long)(s32)g[25+i]*(1u << shift);
            value >>= shift;
            g[25+i] = (u32)value;
            g[9+i] = (u32)(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
        }
        return;
    }
    if ((op & 63) == 0x11) {
        for (unsigned i = 0; i < 3; i++) {
            s32 source = sign16(g[9+i]);
            long long delta = (long long)(s32)g[53+i] - source;
            if (delta < -32768) delta = -32768;
            if (delta > 32767) delta = 32767;
            long long value = ((long long)source*4096 + (long long)delta*sign16(g[8])) >> 12;
            g[25+i] = (u32)value;
            g[9+i] = (u32)(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
        }
        return;
    }
    if ((op & 63) != 0x12) { background_gte_cmd(op); return; }
    s32 matrix[9], vector[3];
    for (unsigned i = 0; i < 9; i++) matrix[i] = sign16(g[32+i/2] >> ((i&1)*16));
    for (unsigned i = 0; i < 3; i++) vector[i] = ((op >> 15) & 3) == 3 ? sign16(g[9+i])
        : sign16(i < 2 ? g[0] >> (i*16) : g[1]);
    for (unsigned row = 0; row < 3; row++) {
        long long value = ((op >> 13) & 3) == 0 ? (long long)(s32)g[37+row]*4096 : 0;
        for (unsigned col = 0; col < 3; col++) value += (long long)matrix[row*3+col]*vector[col];
        value >>= 12;
        g[25+row] = (u32)value;
        g[9+row] = (u32)(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
    }
}
void func_80031264(void *rotation, void *matrix) {
    /* Shared deterministic rotation callback: no trigonometry/GTE validation. */
    s16 *r = rotation, *m = matrix;
    for (unsigned i = 0; i < 9; i++) m[i] = (s16)((i%4 == 0 ? 4096 : 0) + r[i%3]);
}
u8 *o0b_func_800C7DAC(void *, s32, u8 *);
#ifndef POSE_LIBRARY
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u32 frame;
    while (fread(&frame, 4, 1, stdin) == 1) {
        memset(psx_ram, 0, sizeof psx_ram); memset(psx_scratchpad, 0, sizeof psx_scratchpad); memset(g, 0, sizeof g);
        if (fread(psx_ram + 0x180000, 8192, 1, stdin) != 1) abort();
        u8 *out = psx_ram + 0x190000;
        u32 result[2] = {(u32)(o0b_func_800C7DAC(psx_ram+0x180000, frame, out)-out), *(u32 *)(psx_ram+0x180020) & 0xffffff};
        fwrite(result, sizeof result, 1, stdout); fwrite(g, sizeof g, 1, stdout);
        fwrite(psx_scratchpad, sizeof psx_scratchpad, 1, stdout); fwrite(out, 4096, 1, stdout);
    }
}

#endif

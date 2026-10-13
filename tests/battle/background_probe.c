/* CPU/packet equivalence probe: deterministic GTE model, not a GTE accuracy test. */
#include "psyq_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

unsigned char psx_scratchpad[0x400] __attribute__((aligned(32)));
void *memset(void *, int, size_t);
static u32 g[64];
void port_fatal(const char *fmt, ...) { (void)fmt; abort(); }
void gte_mtc2(int r, u32 v) { g[r] = v; }
u32 gte_mfc2(int r) { return g[r]; }
void gte_cmd(u32 op) {
    switch (op & 63) {
    case 0x10:
        /* Deterministic shared DPCS callback; this probe checks packet paths. */
        g[22] = g[6] ^ g[8];
        break;
    case 1:
        g[12] = g[13]; g[13] = g[14]; g[14] = g[0];
        g[17] = g[18]; g[18] = g[19]; g[19] = g[1] & 65535;
        break;
    case 0x30:
        for (int i = 0; i < 3; i++) { g[12+i] = g[i*2]; g[17+i] = g[i*2+1] & 65535; }
        break;
    case 6: {
        s32 x[3], y[3];
        for (int i = 0; i < 3; i++) { x[i] = (s16)g[12+i]; y[i] = (s16)(g[12+i] >> 16); }
        g[24] = (u32)(x[0]*(y[1]-y[2])+x[1]*(y[2]-y[0])+x[2]*(y[0]-y[1]));
        break;
    }
    case 0x28:
        for (int i = 9; i <= 11; i++) {
            s32 v = (s16)g[i]; u32 square = (u32)(v*v);
            g[16+i] = square; g[i] = square > 32767 ? 32767 : square;
        }
        break;
    case 0x2D:
        g[7] = ((g[17] + g[18] + g[19]) >> 4) & 65535;
        break;
    case 0x3D:
        for (int i = 9; i <= 11; i++) {
            u32 v = g[8] * g[i]; g[i] = v > 32767 ? 32767 : v;
        }
        break;
    default: abort();
    }
}
u8 *o0b_func_800CEED4(const void *, u8 *, u32 *, const void *);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u32 settings[2];
    while (fread(settings, sizeof settings, 1, stdin) == 1) {
        memset(psx_ram, 0, sizeof psx_ram); memset(psx_scratchpad, 0, sizeof psx_scratchpad);
        memset(g, 0, sizeof g);
        if (fread(psx_ram + 0x180000, 512, 1, stdin) != 1 || fread(psx_ram + 0x181000, 512, 1, stdin) != 1) abort();
        *(u32 *)(psx_ram + 0xCF588) = settings[0]; *(u32 *)(psx_ram + 0xCF5A0) = settings[1];
        u8 *out = psx_ram + 0x190000;
        u32 size = o0b_func_800CEED4(psx_ram + 0x180000, out, (u32 *)(psx_ram + 0x188000), psx_ram + 0x181000) - out;
        fwrite(&size, 4, 1, stdout); fwrite(g, sizeof g, 1, stdout);
        fwrite(psx_scratchpad, sizeof psx_scratchpad, 1, stdout);
        fwrite(psx_ram + 0x188000, 16384, 1, stdout); fwrite(out, 4096, 1, stdout);
    }
}

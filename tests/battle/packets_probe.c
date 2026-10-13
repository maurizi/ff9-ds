#define main background_unused_main
#include "background_probe.c"
#undef main
extern u32 g_vtxScreenXY[], g_vtxScreenZ[];
u8 *o0b_func_800C58E4(void *, u8, u8 *);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u32 mesh;
    while (fread(&mesh, 4, 1, stdin) == 1) {
        memset(psx_ram, 0, sizeof psx_ram); memset(psx_scratchpad, 0, sizeof psx_scratchpad); memset(g, 0, sizeof g);
        if (fread(psx_ram+0x180000, 8192, 1, stdin) != 1) abort();
        for (unsigned i = 0; i < 32; i++) {
            g_vtxScreenXY[i] = *(u32 *)(psx_ram+0x180200+i*4);
            g_vtxScreenZ[i] = *(u32 *)(psx_ram+0x180280+i*4);
        }
        *(u32 *)(psx_ram+0x6794C) = 0x02180F80;
        u8 *out = psx_ram+0x190000;
        u32 result[2] = {(u32)(o0b_func_800C58E4(psx_ram+0x180000, mesh, out)-out), *(u32 *)(psx_ram+0x180010) & 0xffffff};
        fwrite(result,sizeof result,1,stdout); fwrite(g,sizeof g,1,stdout);
        fwrite(psx_ram+0x188000,16384,1,stdout); fwrite(out,4096,1,stdout);
    }
}

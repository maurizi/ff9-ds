#define main background_unused_main
#include "background_probe.c"
#undef main
void gte_ctc2(int r, u32 v) { g[32+r]=v; }
void o0b_func_800D4A30(u32, void *, u8 *, u32 *);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u8 *data=psx_ram+0x180000, *out=data+0x1000;
    u32 *ot=(u32 *)(data+0x8000), stride;
    while (fread(&stride,4,1,stdin)==1) {
        if (fread(data,1024,1,stdin)!=1 || fread(out,1024,1,stdin)!=1 || fread(ot,16384,1,stdin)!=1 || fread(g,sizeof g,1,stdin)!=1) abort();
        o0b_func_800D4A30(stride,data,out,ot);
        fwrite(data+36,24,1,stdout);fwrite(out,1024,1,stdout);fwrite(ot,16384,1,stdout);fwrite(g,sizeof g,1,stdout);
    }
}

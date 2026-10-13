#define POSE_LIBRARY
#include "pose_probe.c"
void o0b_func_800DFE4C(void *, s32, const s16 *, SVECTOR *);
void o0b_func_800DFE54(void *, s32, const s16 *, SVECTOR *);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u32 args[2];
    while (fread(args,sizeof args,1,stdin)==1) {
        u8 *data=psx_ram+0x180000;
        if (fread(data,64,1,stdin)!=1 || fread(g,sizeof g,1,stdin)!=1) abort();
        if (args[0]) o0b_func_800DFE54(data,(s32)args[1],(s16 *)(data+48),(SVECTOR *)(data+56));
        else o0b_func_800DFE4C(data,(s32)args[1],(s16 *)(data+48),(SVECTOR *)(data+56));
        fwrite(data,64,1,stdout); fwrite(g,sizeof g,1,stdout);
    }
}

#define POSE_LIBRARY
#include "pose_probe.c"
long ratan2(long y, long x) { return (s32)((u32)y*3 + (u32)x*5); }
void o0b_func_800CF808(const s16 *, const s16 *, s16 *);
s32 o0b_func_800CF91C(const SVECTOR *, s32 *, s32 *);
void o0b_func_800CF8EC(s32, s32, s16 *);
void o0b_func_800CFA00(const s16 *, const s16 *, s32, s16 *);
s32 o0b_func_800CF9B0(s32, s32, s32);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u32 kind;
    while (fread(&kind,4,1,stdin)==1) {
        memset(psx_ram,0,sizeof psx_ram);
        if (fread(psx_ram+0x180000,16,1,stdin)!=1 || fread(g,sizeof g,1,stdin)!=1) abort();
        s16 *from=(s16 *)(psx_ram+0x180000), *to=from+4, *out=from+8;
        u32 result=0;
        if (kind==4) result=o0b_func_800CF9B0(*(s32 *)from,*(s32 *)(from+2),*(s32 *)(from+4));
        else if (kind==0) o0b_func_800CF808(from,to,out);
        else if (kind==1) result=o0b_func_800CF91C((SVECTOR *)from,(s32 *)out,(s32 *)(out+2));
        else if (kind==3) o0b_func_800CFA00(from,to,*(s32 *)(from+6),out);
        else o0b_func_800CF8EC(*(s32 *)from,*(s32 *)(from+2),out);
        fwrite(&result,4,1,stdout); fwrite(out,8,1,stdout); fwrite(g,sizeof g,1,stdout);
    }
}

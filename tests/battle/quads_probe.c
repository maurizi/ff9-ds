#define main background_unused_main
#include "background_probe.c"
#undef main
void gte_ctc2(int r, u32 v) { g[32+r]=v; }
void o0b_func_800D40CC(const u8 *, const void *, u8 *, u32 *);
void o0b_func_800D4478(const u8 *, const void *, u8 *, u32 *);
u8 *o0b_func_800D45FC(const u8 *, const void *, u8 *, u32 *);
void o0b_func_800D3ED4(const u8 *,const void *,u8 *,u32 *);
void o0b_func_800D4228(const u8 *,const void *,u8 *,u32 *);
void o0b_func_800D437C(const u8 *,const void *,u8 *,u32 *);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u8 *data=psx_ram+0x180000, *out=data+0x1000; u32 *ot=(u32 *)(data+0x8000);
    u32 kind;
    while (fread(&kind,4,1,stdin)==1) {
        if (fread(data,288,1,stdin)!=1 || fread(out,256,1,stdin)!=1 || fread(ot,16384,1,stdin)!=1 || fread(g,sizeof g,1,stdin)!=1) abort();
        if (kind==5) o0b_func_800D437C(data,data+224,out,ot);
        else if (kind==4) o0b_func_800D4228(data,data+224,out,ot);
        else if (kind==3) o0b_func_800D3ED4(data,data+224,out,ot);
        else if (kind==2) *(u32 *)(out+252)=(u32)o0b_func_800D45FC(data,data+224,out,ot)&0xffffff;
        else if (kind) o0b_func_800D40CC(data,data+224,out,ot);
        else o0b_func_800D4478(data,data+224,out,ot);
        fwrite(out,256,1,stdout); fwrite(ot,16384,1,stdout); fwrite(g,sizeof g,1,stdout);
    }
}

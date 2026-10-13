#include "psyq_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../../port/psx/gte.c"
u8 psx_scratchpad[0x400];
void port_fatal(const char *fmt,...) { (void)fmt;abort(); }
void o0b_func_800D5C30(s32,void *,u8 *,u32 *);
void o0b_func_800DFF5C(u8 *,s32,const s16 *,s16 *);
void o0b_func_800DFF64(u8 *,s32,const s16 *,s16 *);
int main(void) {
    if(mmap(psx_ram,PSX_RAM_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=psx_ram) abort();
    if(fread(psx_ram+0xcfa00,512,1,stdin)!=1) abort();
    u8 *data=psx_ram+0x180000,*out=data+0x1000;u32 *ot=(u32 *)(data+0x8000),count,kind;
    while(fread(&kind,4,1,stdin)==1) {
        if(fread(&count,4,1,stdin)!=1) abort();
        if(fread(data,512,1,stdin)!=1 || fread(out,1024,1,stdin)!=1 || fread(ot,16384,1,stdin)!=1 || fread(&g,sizeof g,1,stdin)!=1) abort();
        if(!kind) o0b_func_800D5C30((s32)count,data,out,ot);
        else if(kind==1) o0b_func_800DFF5C(data,(s32)count,(s16 *)(data+128),(s16 *)out);
        else o0b_func_800DFF64(data,(s32)count,(s16 *)(data+128),(s16 *)out);
        fwrite(data,512,1,stdout);fwrite(out,1024,1,stdout);fwrite(ot,16384,1,stdout);fwrite(&g,sizeof g,1,stdout);
    }
}

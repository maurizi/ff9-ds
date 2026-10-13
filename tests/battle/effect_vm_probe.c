#define main background_unused_main
#include "background_probe.c"
#undef main
#include "effect_vm.h"
void gte_ctc2(int r,u32 v) { g[32+r]=v; }
u32 gte_cfc2(int r) { return g[32+r]; }
static u32 exported(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 h,u32 i,
                    u32 j,u32 k,u32 l,u32 m,u32 n,u32 o,u32 p,u32 q) {
    u32 v[]={a,b,c,d,e,f,h,i,j,k,l,m,n,o,p,q},sum=0;
    for(unsigned x=0;x<16;x++) sum+=v[x]*(x+1);
    return sum;
}
static u32 pointer_export(u32 input,u32 scalar,u32 output) {
    if(input!=(u32)(psx_ram+0x180000) || scalar!=0x80008000 || output!=(u32)(psx_ram+0x180004)) abort();
    *(u32 *)output=0xfeed1234;return 0x76543210;
}
static void pointer_bridge_test(void) {
    u32 *code=(u32 *)(psx_ram+0x1e7700),target=(u32)pointer_export;
    u32 *context=(u32 *)(psx_ram+0x170000),*table=(u32 *)(psx_ram+0x170100);
    *(u32 *)(psx_ram+0xf78a4)=(u32)context;context[4]=(u32)table;
    table[0x2b0/4]=target; /* beyond the formerly truncated export scan */
    u32 words[]={0x27bdffc0,0xafbf003c,0x3c048018,0x34840000,
                 0x3c058000,0x34a58000,0x3c068018,0x34c60004,
                 0x3c190000|(target>>16),0x37390000|(target&65535),
                 0x0320f809,0,0x8fbf003c,0,0x03e00008,0x27bd0040};
    __builtin_memcpy(code,words,sizeof words);
    if(port_effect_call(0x801e7700,0,0,0,0,0)!=0x76543210 || *(u32 *)(psx_ram+0x180004)!=0xfeed1234) abort();
    *(u32 *)(psx_ram+0xf78a4)=0;
    if(port_effect_data(0)!=0 || port_effect_data(0x80180000)!=(u32)(psx_ram+0x180000) ||
       port_effect_data((u32)(psx_ram+0x180000))!=(u32)(psx_ram+0x180000)) abort();
}
static void bridge_test(void) {
    u32 *code=(u32 *)(psx_ram+0x1e7700),target=(u32)exported;
    /* Save RA, prepare 12 stack arguments, and call a native 16-word export. */
    unsigned n=0;code[n++]=0x27bdffb0;code[n++]=0xafbf004c;
    for(unsigned i=4;i<16;i++){code[n++]=0x24080000|(i+1);code[n++]=0xafa80000|(i*4);}
    code[n++]=0x3c190000|(target>>16);code[n++]=0x37390000|(target&65535);
    code[n++]=0x0320f809;code[n++]=0;
    code[n++]=0x8fbf004c;code[n++]=0;code[n++]=0x03e00008;code[n++]=0x27bd0050;
    u32 got=port_effect_call(0x801e7700,1,2,3,4,0),want=0;
    for(unsigned i=1;i<=16;i++) want+=i*i;
    if(got!=want) abort();
}
static void incoming_words_test(void) {
    u32 *code=(u32 *)(psx_ram+0x1e7700);
    u32 args[16];
    for(unsigned i=0;i<16;i++) args[i]=0x87654321u+i*0x1234567u;
    for(unsigned count=6;count<=9;count+=3) {
        unsigned n=0;u32 sum=0,weighted=0;
        code[n++]=0x00851021; /* addu v0,a0,a1 */
        code[n++]=0x00461021;code[n++]=0x00471021;
        for(unsigned i=4;i<count;i++) {
            code[n++]=0x8fa80000|i*4; /* lw t0,i*4(sp) */
            code[n++]=0; /* load delay */
            code[n++]=0x00481021;
        }
        code[n++]=0x03e00008;code[n++]=0;
        for(unsigned i=0;i<count;i++) {sum+=args[i];weighted+=args[i]*(i+1);}
        if(port_effect_call_words(0x801e7700,args,count)!=sum) abort();
        if(port_effect_call_words((u32)exported,args,count)!=weighted) abort();
    }
}
int main(void) {
    if(mmap(psx_ram,PSX_RAM_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=psx_ram) abort();
    bridge_test();
    pointer_bridge_test();
    incoming_words_test();
    u32 args[5],size;
    while(fread(&size,4,1,stdin)==1) {
        if(size>0x5000 || fread(args,20,1,stdin)!=1 || fread(psx_ram+0x1e7700,size,1,stdin)!=1 || fread(psx_ram+0x180000,128,1,stdin)!=1 || fread(g,sizeof g,1,stdin)!=1) abort();
        u32 result=port_effect_call(0x801e7700,args[0],args[1],args[2],args[3],args[4]);
        fwrite(&result,4,1,stdout);fwrite(psx_ram+0x180000,128,1,stdout);fwrite(g,sizeof g,1,stdout);
    }
}

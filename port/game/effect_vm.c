/* CD-loaded effect modules contain MIPS independently of resident overlays.
 * Execute their glue against the existing native integer/pointer-word engine
 * exports. This is not a replacement for the native game/graphics engines. */
#include "psyq_port.h"
#include "gte.h"
#include "effect_vm.h"
#include <stdint.h>
typedef u32 (*NativeExport)(u32,u32,u32,u32,u32,u32,u32,u32,
                           u32,u32,u32,u32,u32,u32,u32,u32);
_Static_assert(sizeof(void *)==4, "effect bridge requires 32-bit native ABI");
static int guest_code(u32 a) { return a>=0x801e7700 && a<0x801f1700; }
static u8 *memory(u32 a) {
    if(a<PSX_RAM_SIZE || (a>=0x80000000 && a<0x80200000)) return (u8 *)PSX_ADDR(a);
    if(a>=0x1f800000 && a<0x1f800400) return psx_scratchpad+(a&0x3ff);
    return (u8 *)(uintptr_t)a;
}
/* Decode a pointer from module-owned data at its native dereference boundary.
 * Keep the stored value intact so module pointer comparisons remain valid. */
u32 port_effect_data(u32 address) {
    return address ? (u32)(uintptr_t)memory(address) : 0;
}
static u32 read32(u32 a) { u32 v;__builtin_memcpy(&v,memory(a),4);return v; }
static u32 read16(u32 a) { u16 v;__builtin_memcpy(&v,memory(a),2);return v; }
static void write32(u32 a,u32 v) { __builtin_memcpy(memory(a),&v,4); }
static void write16(u32 a,u32 v) { u16 h=(u16)v;__builtin_memcpy(memory(a),&h,2); }
static u32 native_call(u32 target,u32 *r) {
    if(!target) port_fatal("effect VM: null engine export");
    u32 a[16];for(unsigned i=0;i<4;i++) a[i]=r[4+i];
    for(unsigned i=4;i<16;i++) a[i]=read32(r[29]+i*4);
    /* Reviewed export argument types. D044C's second argument is a pointer
     * carried as s32; scalar flags must never be guessed to be pointers. */
    /* 216 function exports precede the pool/data pointers at table +0x360. */
    static const u16 pointer_mask[216]={
        [0x14/4]=3,[0x20/4]=3,[0xbc/4]=11,[0xd4/4]=7,
        [0xf0/4]=11,[0x110/4]=0x85,[0x114/4]=7,[0x134/4]=1,
        [0x234/4]=4,[0x27c/4]=1,
        /* Model-pool initialization receives descriptors within the module. */
        [0x70/4]=1,[0x74/4]=1,
        [0xd8/4]=5,[0xdc/4]=11,[0xe0/4]=4,
        [0x2b0/4]=5,[0x2b4/4]=3,[0x338/4]=1,
        [0x13c/4]=6,[0x148/4]=2,[0x14c/4]=15,
        /* These s32 wrappers forward descriptor/position pointers to the pools. */
        [0x1d4/4]=3,[0x1d8/4]=3,
    };
    u32 ctx=read32((u32)PSX_ADDR(0x800f78a4));
    if(ctx) {
        u32 table=read32(ctx+16);
        for(unsigned index=0;index<216;index++) if(read32(table+index*4)==target) {
            u16 mask=pointer_mask[index];
            for(unsigned i=0;i<16;i++) if(a[i] && (mask&(1u<<i))) a[i]=(u32)(uintptr_t)memory(a[i]);
            break;
        }
    }
    return ((NativeExport)(uintptr_t)target)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],
                                            a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15]);
}
u32 port_effect_call_words(u32 target,const u32 *arguments,u32 count) {
    if(count>16) port_fatal("effect VM: too many callback arguments");
    if(!guest_code(target)) {
        if(!target) port_fatal("effect VM: null effect callback");
        u32 a[16]={0};for(unsigned i=0;i<count;i++) a[i]=arguments[i];
        return ((NativeExport)(uintptr_t)target)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],
                                                a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15]);
    }
    /* Real native stack pointers allow C exports to consume local structures.
     * Space above initial SP holds o32 arguments; space below holds frames. */
    u32 stack[1024]={0},r[32]={0},hi=0,lo=0;
    for(unsigned i=0;i<count && i<4;i++) r[4+i]=arguments[i];
    r[29]=(u32)(uintptr_t)(stack+992);r[31]=0xffffffff;
    for(unsigned i=4;i<count;i++) write32(r[29]+i*4,arguments[i]);
    u32 pc=target,delayed=0;int has_delay=0;
    for(unsigned steps=0;steps<1000000;steps++) {
        if(pc==0xffffffff) return r[2];
        if(!guest_code(pc)) {
            if(pc>=0x80000000 && pc<0x80200000) port_fatal("effect VM: unresolved guest call %08X",pc);
            r[2]=native_call(pc,r);pc=r[31];continue;
        }
        if(pc&3) port_fatal("effect VM: unaligned PC %08X",pc);
        u32 ins=read32(pc),op=ins>>26;
        unsigned rs=(ins>>21)&31,rt=(ins>>16)&31,rd=(ins>>11)&31;
        unsigned sh=(ins>>6)&31,fun=ins&63;
        u32 imm=ins&65535,a=r[rs],b=r[rt],addr=a+(u32)(s32)(s16)imm;
        u32 next=has_delay?delayed:pc+4,branch=0;int take=0;has_delay=0;
        switch(op) {
        case 0:
            switch(fun) {
            case 0:r[rd]=b<<sh;break;case 2:r[rd]=b>>sh;break;case 3:r[rd]=(u32)((s32)b>>sh);break;
            case 4:r[rd]=b<<(a&31);break;case 6:r[rd]=b>>(a&31);break;case 7:r[rd]=(u32)((s32)b>>(a&31));break;
            case 8:branch=a;take=1;break;case 9:r[rd]=pc+8;branch=a;take=1;break;
            case 16:r[rd]=hi;break;case 17:hi=a;break;case 18:r[rd]=lo;break;case 19:lo=a;break;
            case 24:{int64_t v=(int64_t)(s32)a*(s32)b;lo=(u32)v;hi=(u32)((uint64_t)v>>32);break;}
            case 25:{uint64_t v=(uint64_t)a*b;lo=(u32)v;hi=(u32)(v>>32);break;}
            case 26:
                if(!b){lo=(s32)a<0?1:0xffffffff;hi=a;}
                else if(a==0x80000000 && b==0xffffffff){lo=a;hi=0;}
                else {lo=(u32)((s32)a/(s32)b);hi=(u32)((s32)a%(s32)b);}break;
            case 27:if(!b){lo=0xffffffff;hi=a;}else{lo=a/b;hi=a%b;}break;
            case 32:case 33:r[rd]=a+b;break;case 34:case 35:r[rd]=a-b;break;
            case 36:r[rd]=a&b;break;case 37:r[rd]=a|b;break;case 38:r[rd]=a^b;break;case 39:r[rd]=~(a|b);break;
            case 42:r[rd]=(s32)a<(s32)b;break;case 43:r[rd]=a<b;break;
            default:goto unsupported;
            }break;
        case 1:
            if(rt==0 || rt==16) take=(s32)a<0;else if(rt==1 || rt==17) take=(s32)a>=0;else goto unsupported;
            if(rt>=16) r[31]=pc+8;
            branch=pc+4+((u32)(s32)(s16)imm<<2);break;
        case 2:case 3:if(op==3)r[31]=pc+8;branch=((pc+4)&0xf0000000)|((ins&0x3ffffff)<<2);take=1;break;
        case 4:take=a==b;goto conditional;case 5:take=a!=b;goto conditional;
        case 6:take=(s32)a<=0;goto conditional;case 7:take=(s32)a>0;
        conditional:branch=pc+4+((u32)(s32)(s16)imm<<2);break;
        case 8:case 9:r[rt]=addr;break;
        case 10:r[rt]=(s32)a<(s32)(s16)imm;break;case 11:r[rt]=a<(u32)(s32)(s16)imm;break;
        case 12:r[rt]=a&imm;break;case 13:r[rt]=a|imm;break;case 14:r[rt]=a^imm;break;case 15:r[rt]=imm<<16;break;
        case 18:
            if(ins&(1u<<25)) gte_cmd(ins&0x1ffffff);
            else if(rs==0) r[rt]=gte_mfc2(rd);else if(rs==2) r[rt]=gte_cfc2(rd);
            else if(rs==4) gte_mtc2(rd,b);else if(rs==6) gte_ctc2(rd,b);else goto unsupported;
            break;
        case 32:r[rt]=(u32)(s32)(s8)*memory(addr);break;case 33:r[rt]=(u32)(s32)(s16)read16(addr);break;
        case 35:r[rt]=read32(addr);break;case 36:r[rt]=*memory(addr);break;case 37:r[rt]=read16(addr);break;
        case 40:*memory(addr)=(u8)b;break;case 41:write16(addr,b);break;case 43:write32(addr,b);break;
        case 50:gte_mtc2(rt,read32(addr));break;case 58:write32(addr,gte_mfc2(rt));break;
        default:goto unsupported;
        }
        if(r[29]<(u32)(uintptr_t)stack || r[29]>(u32)(uintptr_t)(stack+992))
            port_fatal("effect VM: stack outside reserved frames at %08X",pc);
        r[0]=0;if(take){delayed=branch;has_delay=1;}pc=next;continue;
    unsupported:port_fatal("effect VM: unsupported instruction %08X at %08X",ins,pc);
    }
    port_fatal("effect VM: instruction limit at %08X",pc);return 0;
}
u32 port_effect_call(u32 target,u32 a0,u32 a1,u32 a2,u32 a3,u32 a4) {
    u32 arguments[5]={a0,a1,a2,a3,a4};
    return port_effect_call_words(target,arguments,5);
}

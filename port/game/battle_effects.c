/* Battle effects frame orchestration (800E3EEC). Native C keeps the platform
 * stack; the PS1's temporary scratchpad stack switches are unnecessary here. */
#include "psyq_port.h"
extern void o0b_func_800D0940(void);
extern void o0b_func_800E702C(void);
extern void o0b_func_800D650C(void);
extern void o0b_func_800DA92C(void);
extern s32 o0b_func_800D7094(void);
extern void o0b_func_800E3C98(void);
extern void o0b_func_800E3E08(void);
extern void o0b_func_800E9578(void);
extern void func_80055698(s32);
extern void func_80054B78(void);
static u8 *context(void) { return *(u8 **)(void *)PSX_ADDR(0x800F78A4); }
static u32 state(unsigned address) { return *(u32 *)(void *)PSX_ADDR(address); }
static void set_state(unsigned address, u32 value) { *(u32 *)(void *)PSX_ADDR(address) = value; }
static u8 *getptr(const u8 *ctx, unsigned off) { return *(u8 **)(ctx + off); }
u8 *o0b_func_800E3EEC(u8 *out) {
    set_state(0x800F7278, 0);
    *(u8 **)(context() + 36) = out;
    o0b_func_800D0940();
    o0b_func_800E702C();
    u8 *stp = getptr(context(), 36);
    *(u8 **)(context() + 36) = stp + 24;
    SetDrawStp((DR_STP *)stp, 1);
    AddPrim(getptr(context(), 32) + 0x3FFC, stp);
    *(u32 *)(psx_scratchpad + 0x3FC) = state(0x800F78A8);
    getptr(context(), 0)[32] = 0;
    o0b_func_800D650C();
    o0b_func_800DA92C();
    if (!getptr(context(), 0)[32]) {
        if (state(0x800F78AC)) {
            set_state(0x800F78AC, 0);
            func_80055698(2);
        }
        o0b_func_800D7094();
        o0b_func_800E3C98();
        func_80054B78();
    } else {
        o0b_func_800E3E08();
        getptr(context(), 44)[93] = 1;
    }
    o0b_func_800E9578();
    set_state(0x800F78A8, *(u32 *)(psx_scratchpad + 0x3FC));
    SetDrawStp((DR_STP *)(stp + 12), 0);
    AddPrim(getptr(context(), 32) + 0x3FFC, stp + 12);
    return getptr(context(), 36);
}

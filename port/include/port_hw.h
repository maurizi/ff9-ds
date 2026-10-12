/* Port replacements for the decomp's hardware macros (psyq.h, ff9.h, ovl_0a/0b/0c/64.h).
 *
 * Included by port_prelude.h *after* the binary's header, so every macro here is #undef'd first and redefined.
 * The GTE macros are translated instruction by instruction from their MIPS bodies: lwc2/swc2/mtc2/mfc2/ctc2/cfc2
 * become calls into the software GTE (port/psx/gte.c) and each `.word 0x4Axxxxxx` becomes gte_cmd(). The
 * register state persists between macros, as the split sequences (`_b`, `_nn`, ...) expect.
 */
#ifndef PORT_HW_H
#define PORT_HW_H

#include "gte.h"
#include "psx_mem.h"
#include "port_decls.h"

/* ---- GTE: loads ----------------------------------------------------------------------------------------- */
#define GTE_LW(p, off) (*(const u32 *)((const u8 *)(p) + (off)))
#define GTE_LHU(p, off) ((u32)*(const u16 *)((const u8 *)(p) + (off)))

#undef gte_SetRotMatrix
#define gte_SetRotMatrix(r0) do { const void *_p = (r0); \
    gte_ctc2(0, GTE_LW(_p, 0)); gte_ctc2(1, GTE_LW(_p, 4)); gte_ctc2(2, GTE_LW(_p, 8)); \
    gte_ctc2(3, GTE_LW(_p, 12)); gte_ctc2(4, GTE_LW(_p, 16)); } while (0)
#undef gte_SetLightMatrix
#define gte_SetLightMatrix(r0) do { const void *_p = (r0); \
    gte_ctc2(8, GTE_LW(_p, 0)); gte_ctc2(9, GTE_LW(_p, 4)); gte_ctc2(10, GTE_LW(_p, 8)); \
    gte_ctc2(11, GTE_LW(_p, 12)); gte_ctc2(12, GTE_LW(_p, 16)); } while (0)
#undef gte_SetTransMatrix
#define gte_SetTransMatrix(r0) do { const void *_p = (r0); \
    gte_ctc2(5, GTE_LW(_p, 20)); gte_ctc2(6, GTE_LW(_p, 24)); gte_ctc2(7, GTE_LW(_p, 28)); } while (0)
#undef gte_ldtr
#define gte_ldtr(r0, r1, r2) (gte_ctc2(5, (u32)(r0)), gte_ctc2(6, (u32)(r1)), gte_ctc2(7, (u32)(r2)))
#undef gte_ldtr0
#define gte_ldtr0() (gte_ctc2(5, 0), gte_ctc2(6, 0), gte_ctc2(7, 0))
#undef gte_ReadGeomScreen_r
#define gte_ReadGeomScreen_r(r) ((r) = gte_cfc2(26))
#undef gte_SetGeomScreen
#define gte_SetGeomScreen(r0) gte_ctc2(26, (u32)(r0))
#undef gte_SetGeomOffset
#define gte_SetGeomOffset(r0, r1) (gte_ctc2(24, (u32)(r0) << 16), gte_ctc2(25, (u32)(r1) << 16))

#undef gte_ldv0
#define gte_ldv0(r0) do { const void *_p = (r0); GTE_LWC2(0, _p, 0); GTE_LWC2(1, _p, 4); } while (0)
#undef gte_ldv1
#define gte_ldv1(r0) do { const void *_p = (r0); GTE_LWC2(2, _p, 0); GTE_LWC2(3, _p, 4); } while (0)
#undef gte_ldv2
#define gte_ldv2(r0) do { const void *_p = (r0); GTE_LWC2(4, _p, 0); GTE_LWC2(5, _p, 4); } while (0)
#undef gte_ldv3
#define gte_ldv3(r0, r1, r2) do { gte_ldv0(r0); gte_ldv1(r1); gte_ldv2(r2); } while (0)
#undef gte_ldv3c
#define gte_ldv3c(r0) do { const void *_p = (r0); \
    GTE_LWC2(0, _p, 0); GTE_LWC2(1, _p, 4); GTE_LWC2(2, _p, 8); \
    GTE_LWC2(3, _p, 12); GTE_LWC2(4, _p, 16); GTE_LWC2(5, _p, 20); } while (0)
#undef gte_ldv3xyz
#define gte_ldv3xyz(r0, r1) do { const void *_a = (r0), *_b = (r1); \
    GTE_LWC2(0, _a, 0); GTE_LWC2(1, _b, 0); GTE_LWC2(2, _a, 4); \
    GTE_LWC2(3, _b, 4); GTE_LWC2(4, _a, 8); GTE_LWC2(5, _b, 8); } while (0)
#undef gte_ldxy0
#define gte_ldxy0(r0) GTE_LWC2(0, (r0), 0)
#undef gte_ldxy3
#define gte_ldxy3(r0, r1, r2) do { GTE_LWC2(0, (r0), 0); GTE_LWC2(2, (r1), 0); GTE_LWC2(4, (r2), 0); } while (0)
#undef gte_zero_vz012
#define gte_zero_vz012() (gte_mtc2(1, 0), gte_mtc2(3, 0), gte_mtc2(5, 0))
#undef gte_ldvz012_0
#define gte_ldvz012_0() (gte_mtc2(1, 0), gte_mtc2(3, 0), gte_mtc2(5, 0))
#undef gte_ldlv0
#define gte_ldlv0(r0) do { const void *_p = (r0); \
    gte_mtc2(0, GTE_LHU(_p, 0) | (GTE_LHU(_p, 4) << 16)); GTE_LWC2(1, _p, 8); } while (0)
#undef gte_ldclmv
#define gte_ldclmv(r0) do { const void *_p = (r0); \
    gte_mtc2(9, GTE_LHU(_p, 0)); gte_mtc2(10, GTE_LHU(_p, 6)); gte_mtc2(11, GTE_LHU(_p, 12)); } while (0)
#undef gte_ldsv
#define gte_ldsv(r0) do { const void *_p = (r0); \
    gte_mtc2(9, GTE_LHU(_p, 0)); gte_mtc2(10, GTE_LHU(_p, 2)); gte_mtc2(11, GTE_LHU(_p, 4)); } while (0)
#undef gte_ldir
#define gte_ldir(r0) do { const void *_p = (r0); GTE_LWC2(9, _p, 0); GTE_LWC2(10, _p, 4); GTE_LWC2(11, _p, 8); } while (0)
#undef gte_ldopv1
#define gte_ldopv1(r0) do { const void *_p = (r0); \
    gte_ctc2(0, GTE_LW(_p, 0)); gte_ctc2(2, GTE_LW(_p, 4)); gte_ctc2(4, GTE_LW(_p, 8)); } while (0)
#undef gte_ldopv2
#define gte_ldopv2(r0) do { const void *_p = (r0); GTE_LWC2(11, _p, 8); GTE_LWC2(9, _p, 0); GTE_LWC2(10, _p, 4); } while (0)
#undef gte_ldir0123
#define gte_ldir0123(r0, r1, r2, r3) \
    (gte_mtc2(8, (u32)(r0)), gte_mtc2(9, (u32)(r1)), gte_mtc2(10, (u32)(r2)), gte_mtc2(11, (u32)(r3)))
#undef gte_ldir12
#define gte_ldir12(r1, r2) (gte_mtc2(9, (u32)(r1)), gte_mtc2(10, (u32)(r2)))
#undef gte_lddp
#define gte_lddp(r0) gte_mtc2(8, (u32)(r0))
#undef gte_ldsxy3
#define gte_ldsxy3(r0, r1, r2) (gte_mtc2(12, (u32)(r0)), gte_mtc2(14, (u32)(r2)), gte_mtc2(13, (u32)(r1)))
#undef gte_ldsz3
#define gte_ldsz3(r0, r1, r2) (gte_mtc2(17, (u32)(r0)), gte_mtc2(18, (u32)(r1)), gte_mtc2(19, (u32)(r2)))
#undef gte_ldsz4
#define gte_ldsz4(r0, r1, r2, r3) \
    (gte_mtc2(16, (u32)(r0)), gte_mtc2(17, (u32)(r1)), gte_mtc2(18, (u32)(r2)), gte_mtc2(19, (u32)(r3)))
#undef gte_ldlzc
#define gte_ldlzc(r0) gte_mtc2(30, (u32)(r0))

/* ---- GTE: stores ---------------------------------------------------------------------------------------- */
#define GTE_SH(p, off, v) (*(u16 *)((u8 *)(p) + (off)) = (u16)(v))

#undef gte_stsxy
#define gte_stsxy(r0) GTE_SWC2(14, (r0), 0)
#undef gte_stsxy3
#define gte_stsxy3(r0, r1, r2) do { GTE_SWC2(12, (r0), 0); GTE_SWC2(13, (r1), 0); GTE_SWC2(14, (r2), 0); } while (0)
#undef gte_stsxy3c
#define gte_stsxy3c(r0) do { void *_p = (r0); GTE_SWC2(12, _p, 0); GTE_SWC2(13, _p, 4); GTE_SWC2(14, _p, 8); } while (0)
#undef gte_stsz
#define gte_stsz(r0) GTE_SWC2(19, (r0), 0)
#undef gte_stsz3c
#define gte_stsz3c(r0) do { void *_p = (r0); GTE_SWC2(17, _p, 0); GTE_SWC2(18, _p, 4); GTE_SWC2(19, _p, 8); } while (0)
#undef gte_stsz3h
#define gte_stsz3h(r0) do { void *_p = (r0); \
    GTE_SH(_p, 0, gte_mfc2(17)); GTE_SH(_p, 4, gte_mfc2(18)); GTE_SH(_p, 8, gte_mfc2(19)); } while (0)
#undef gte_stszotz
#define gte_stszotz(r0) (*(s32 *)(r0) = (s32)gte_mfc2(19) >> 2)
#undef gte_stotz
#define gte_stotz(r0) GTE_SWC2(7, (r0), 0)
#undef gte_stopz
#define gte_stopz(r0) GTE_SWC2(24, (r0), 0)
#undef gte_stflg
#define gte_stflg(r0) (*(u32 *)(r0) = gte_cfc2(31))
#undef gte_stlzc
#define gte_stlzc(r0) GTE_SWC2(31, (r0), 0)
#undef gte_stlvnl
#define gte_stlvnl(r0) do { void *_p = (r0); GTE_SWC2(25, _p, 0); GTE_SWC2(26, _p, 4); GTE_SWC2(27, _p, 8); } while (0)
#undef gte_stir
#define gte_stir(r0) do { void *_p = (r0); GTE_SWC2(9, _p, 0); GTE_SWC2(10, _p, 4); GTE_SWC2(11, _p, 8); } while (0)
#undef gte_strgb
#define gte_strgb(r0) GTE_SWC2(22, (r0), 0)
#undef gte_stclmv
#define gte_stclmv(r0) do { void *_p = (r0); \
    GTE_SH(_p, 0, gte_mfc2(9)); GTE_SH(_p, 6, gte_mfc2(10)); GTE_SH(_p, 12, gte_mfc2(11)); } while (0)
#undef gte_stsv
#define gte_stsv(r0) do { void *_p = (r0); \
    GTE_SH(_p, 0, gte_mfc2(9)); GTE_SH(_p, 2, gte_mfc2(10)); GTE_SH(_p, 4, gte_mfc2(11)); } while (0)
#undef gte_stmac123
#define gte_stmac123(r1, r2, r3) ((r1) = gte_mfc2(25), (r2) = gte_mfc2(26), (r3) = gte_mfc2(27))
#undef gte_stmac12
#define gte_stmac12(r1, r2) ((r1) = gte_mfc2(25), (r2) = gte_mfc2(26))
/* IR1 -> r0[0]; IR2 -> r0[1], raised to r2 when it isn't below it; r1 = IR3 & 0xFFFF, | 0x10000000 when raised.
 * The `and` sits in the bnez delay slot in the original (ovl_0a 0x1460C), so it always runs. */
#undef gte_stsvclamp
#define gte_stsvclamp(r0, r1, r2) do { \
    u32 _x = gte_mfc2(9), _y = gte_mfc2(10), _z = gte_mfc2(11); \
    GTE_SH((r0), 0, _x); \
    _z &= 0xFFFF; \
    if (!((s32)_y < (s32)(r2))) { _y = (u32)(r2); _z |= 0x10000000; } \
    GTE_SH((r0), 2, _y); \
    *(u32 *)(r1) = _z; } while (0)

/* ---- GTE: commands -------------------------------------------------------------------------------------- */
#define GTE_CMD(w) gte_cmd((w) & 0x1FFFFFF)
#undef gte_rtps
#define gte_rtps() GTE_CMD(0x4A180001)
#undef gte_rtps_b
#define gte_rtps_b() GTE_CMD(0x4A180001)
#undef gte_rtps_nn
#define gte_rtps_nn() GTE_CMD(0x4A180001)
#undef gte_rtpt
#define gte_rtpt() GTE_CMD(0x4A280030)
#undef gte_rtpt_b
#define gte_rtpt_b() GTE_CMD(0x4A280030)
#undef gte_nclip
#define gte_nclip() GTE_CMD(0x4B400006)
#undef gte_nclip_b
#define gte_nclip_b() GTE_CMD(0x4B400006)
#undef gte_avsz3
#define gte_avsz3() GTE_CMD(0x4B58002D)
#undef gte_avsz3_b
#define gte_avsz3_b() GTE_CMD(0x4B58002D)
#undef gte_avsz4
#define gte_avsz4() GTE_CMD(0x4B68002E)
#undef gte_avsz4_b
#define gte_avsz4_b() GTE_CMD(0x4B68002E)
#undef gte_rtir
#define gte_rtir() GTE_CMD(0x4A49E012)
#undef gte_rtir_b
#define gte_rtir_b() GTE_CMD(0x4A49E012)
#undef gte_rtir_n1
#define gte_rtir_n1() GTE_CMD(0x4A49E012)
#undef gte_rtir_sf0
#define gte_rtir_sf0() GTE_CMD(0x4A41E012)
#undef gte_rtv0
#define gte_rtv0() GTE_CMD(0x4A486012)
#undef gte_rtv0tr
#define gte_rtv0tr() GTE_CMD(0x4A480012)
#undef gte_rtv0tr_b
#define gte_rtv0tr_b() GTE_CMD(0x4A480012)
#undef gte_rtv0tr_n1
#define gte_rtv0tr_n1() GTE_CMD(0x4A480012)
#undef gte_rtv0tr_nn
#define gte_rtv0tr_nn() GTE_CMD(0x4A480012)
#undef gte_op0
#define gte_op0() GTE_CMD(0x4B70000C)
#undef gte_op12
#define gte_op12() GTE_CMD(0x4B78000C)
#undef gte_gpf0
#define gte_gpf0() GTE_CMD(0x4B90003D)
#undef gte_gpf12
#define gte_gpf12() GTE_CMD(0x4B98003D)
#undef gte_gpf12_b
#define gte_gpf12_b() GTE_CMD(0x4B98003D)
#undef gte_gpf12_n1
#define gte_gpf12_n1() GTE_CMD(0x4B98003D)
#undef gte_gpl0
#define gte_gpl0() GTE_CMD(0x4BA0003E)
#undef gte_gpl12
#define gte_gpl12() GTE_CMD(0x4BA8003E)

/* hooks the decomp's src_equiv.h leaves to the port (its defaults are MIPS) */
#define gte_ldrgbc(v) gte_mtc2(6, (u32)(v))
#define gte_strgb2(r) ((r) = gte_mfc2(22))
#define gte_dpcl() GTE_CMD(0x4A680029)
#define gte_nct() GTE_CMD(0x4AD80420)
#define gte_dpcs() GTE_CMD(0x4A780010)
#define gte_strgb3(r0) do { void *_p = (r0); GTE_SWC2(20, _p, 0); GTE_SWC2(21, _p, 4); GTE_SWC2(22, _p, 8); } while (0)
#define SCRATCHPAD ((u8 *)psx_scratchpad)

/* depth-cue rgb toward the far colour by (0x1000 - p): FC = 0, then DPCS (sf=1) on the colour at rgb */
#undef gte_DpqColorFar_nst
#define gte_DpqColorFar_nst(rgb, p) do { \
    gte_ctc2(21, 0); gte_ctc2(22, 0); gte_ctc2(23, 0); \
    GTE_LWC2(6, (rgb), 0); gte_mtc2(8, (u32)(0x1000 - (s32)(p))); GTE_CMD(0x4A780010); } while (0)
#undef gte_DpqColorFar
#define gte_DpqColorFar(rgb, p) do { gte_DpqColorFar_nst(rgb, p); GTE_SWC2(22, (rgb), 0); } while (0)

/* ---- animation key decode (pure CPU, written as asm in the decomp for register allocation) -------------- */
void port_ldrotkey(const void *key, const void *anim, s32 frame, void *rot);
void port_ldrotkey_lo(const void *key, const void *anim, s32 frame, void *rot);
#undef gte_ldrotkey
#define gte_ldrotkey(r0, r1, r2, r3) port_ldrotkey((r0), (r1), (s32)(r2), (r3))
#undef gte_ldrotkey_lo
#define gte_ldrotkey_lo(r0, r1, r2, r3) port_ldrotkey_lo((r0), (r1), (s32)(r2), (r3))

/* ---- ordering table ------------------------------------------------------------------------------------- */
/* addPrim with the length word supplied pre-shifted (len << 24); `len` is left holding p's 24-bit address. */
#undef addPrimLen
#define addPrimLen(ot, p, len) do { \
    u32 _t = *(u32 *)(ot); \
    *(u32 *)(p) = (_t & 0xFFFFFF) | (u32)(len); \
    (len) = (u32)(p) & 0xFFFFFF; \
    *(u32 *)(ot) = (_t & 0xFF000000) | (len); } while (0)
#undef addPrimLenIn
#define addPrimLenIn(ot, p, len) do { \
    u32 _t = *(u32 *)(ot); \
    *(u32 *)(p) = (_t & 0xFFFFFF) | (u32)(len); \
    *(u32 *)(ot) = (_t & 0xFF000000) | ((u32)(p) & 0xFFFFFF); } while (0)

/* ---- scratchpad stack: the port keeps running on the normal stack -------------------------------------- */
#undef SetSpadStack
#define SetSpadStack(addr) ((void)(addr))
#undef ResetSpadStack
#define ResetSpadStack() ((void)0)
#undef SetSpadStack12
#define SetSpadStack12(addr) ((void)(addr))
#undef ResetSpadStack12
#define ResetSpadStack12() ((void)0)

/* ---- pointers that travel through 26-bit script values --------------------------------------------------- */
/* The event-script evaluator keeps object pointers in 26 bits and the original restores KSEG0 with | 0x80000000.
 * DS pointers (0x02xxxxxx) survive the 26-bit mask unchanged, so the port's restore is the identity. Used by the
 * decomp's src_equiv/ (requested 2026-10-11). */
#define SCRIPT_PTR_RESTORE(v) ((u32)(v))
/* A pointer popped from a 26-bit script value (config/patches.txt): drop the sign extension of bit 25. */
#define PORT_SCRIPT_PTR(v) ((u32)(v) & 0x03FFFFFF)
/* "Is this a RAM pointer (not a file offset)?" where the PS1 tests > 0x80000000 (config/patches.txt). */
#define PORT_IS_RAM_PTR(p) ((u32)(p) >= (u32)psx_ram && (u32)(p) < (u32)psx_ram + PSX_RAM_SIZE)

/* ---- SPU registers: the emulated register file (port/psx/spu.c) ---------------------------------------- */
/* SPU_REG16(a) keeps the decomp's definition: tools/portify.py already wraps its 0x1F80xxxx argument in PSX_HW. */
#undef SPU_VOICE_REG
#define SPU_VOICE_REG(v, r) (*(u16 *)PSX_HW(0x1F801C00 + (r) + (v) * 16))
#undef SPU_VOICE_REG8
#define SPU_VOICE_REG8(v, r) (*(u8 *)PSX_HW(0x1F801C00 + (r) + (v) * 16))

#endif

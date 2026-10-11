/* Software GTE (PS1 COP2): a register-level model, so the decomp's GTE macro sequences (load, command, store,
 * split across C statements) translate one instruction at a time and keep their state between statements.
 * Behaviour follows psx-spx ("Geometry Transformation Engine"), including saturation and the UNR division.
 */
#ifndef PORT_GTE_H
#define PORT_GTE_H

typedef unsigned int gte_u32;

/* MIPS mtc2/mfc2 (data registers 0-31) and ctc2/cfc2 (control registers 0-31). */
void gte_mtc2(int reg, gte_u32 value);
gte_u32 gte_mfc2(int reg);
void gte_ctc2(int reg, gte_u32 value);
gte_u32 gte_cfc2(int reg);

/* Execute a COP2 command word (the low 25 bits of the `.word 0x4Axxxxxx` the macros emit). */
void gte_cmd(gte_u32 op);

/* lwc2/swc2 */
#define GTE_LWC2(reg, base, off) gte_mtc2((reg), *(const gte_u32 *)((const unsigned char *)(base) + (off)))
#define GTE_SWC2(reg, base, off) (*(gte_u32 *)((unsigned char *)(base) + (off)) = gte_mfc2(reg))

#endif

/* PS1 address space on the DS.
 *
 * PS1 data lives in psx_ram[], a 2 MB image of PS1 RAM: every data symbol of every binary (linker-script
 * assignments in build/gen/psx_syms.ld), every hard-coded address (PSX_ADDR) and the overlay slots resolve into
 * it at their original offsets, so named and literal accesses to the same memory agree. Code is native ARM and
 * is not in the image.
 *
 * Hardware addresses: the scratchpad (0x1F800000-0x1F8003FF) maps to psx_scratchpad (DTCM on the DS), the SPU
 * registers (0x1F801C00-0x1F801FFF) to the emulated register file.
 */
#ifndef PSX_MEM_H
#define PSX_MEM_H

#define PSX_RAM_SIZE 0x200000

extern unsigned char psx_ram[PSX_RAM_SIZE];
extern unsigned char psx_scratchpad[0x400];
extern unsigned short psx_spu_regs[0x200];

/* KSEG0/KUSEG address (0x80xxxxxx / 0x00xxxxxx) -> pointer into psx_ram, as an integer like the literal was. */
#define PSX_ADDR(a) ((unsigned int)psx_ram + ((unsigned int)(a) & (PSX_RAM_SIZE - 1)))

/* A pointer the game passes to a library call that is really a small PS1 address (KUSEG, below 2 MB): Square's
 * code sometimes passes a constant where a pointer is expected (CdControlB(CdlSetmode, (u8 *)0x80, 0) in
 * func_80022A28), which on the PS1 reads that physical address. Map such values into psx_ram. */
#define PSX_PTR(p) ((unsigned int)(p) < PSX_RAM_SIZE ? (void *)(psx_ram + (unsigned int)(p)) : (void *)(p))

/* A pointer the game may read through while it is NULL (config/patches.txt): the PS1 reads low RAM there. */
#define PSX_NZ(p) ((p) ? (p) : (__typeof__(p))(void *)psx_ram)

/* 0x1F80xxxx hardware address -> scratchpad or SPU register file (anything else traps in psx_hw_bad). */
unsigned int psx_hw_bad(unsigned int a);
#define PSX_HW(a) \
    ((unsigned int)(a) < 0x1F800400 ? (unsigned int)psx_scratchpad + ((unsigned int)(a) & 0x3FF) \
     : ((unsigned int)(a) & 0xFFFFFC00) == 0x1F801C00 ? (unsigned int)psx_spu_regs + ((unsigned int)(a) & 0x3FF) \
     : psx_hw_bad(a))

#endif

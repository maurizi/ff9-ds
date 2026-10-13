/* Debug-only MPU guard: catch the first write into raster instructions.
 * Region 7 normally covers DTCM, which also has RW coverage from region 6.
 * Reusing it preserves DTCM access while overriding region 4 for the first
 * 16 KiB of ITCM. IRQ tables after this range remain writable. */
#include <nds.h>
#include <stdio.h>

/* The stock handler decodes the faulting instruction to infer its address.
 * Preserve the first exception before any decoder/console fault can replace
 * libnds' shared register array. This is only installed in guard builds. */
volatile unsigned port_itcm_first_fault[21];

static void guard_exception(void) {
    if (!port_itcm_first_fault[0]) {
        for (unsigned i=0; i<16; ++i)
            port_itcm_first_fault[i+1]=(unsigned)exceptionRegisters[i];
        volatile unsigned *bios=(volatile unsigned *)0x02fffd90;
        for (unsigned i=0; i<3; ++i)
            port_itcm_first_fault[i+17]=bios[i];
        __asm__ volatile("mrs %0, cpsr" : "=r"(port_itcm_first_fault[20]));
        port_itcm_first_fault[0]=1;
    }
    REG_IME=0;
    consoleClear();
    printf("First DS exception preserved\n");
    printf("pc %08lx cpsr %08lx\n",
           (unsigned long)port_itcm_first_fault[16],
           (unsigned long)port_itcm_first_fault[17]);
    for (unsigned i=0; i<16; ++i)
        printf("r%-2u %08lx\n", i, (unsigned long)port_itcm_first_fault[i+1]);
    for (;;) __asm__ volatile("nop");
}

void port_itcm_guard_init(void) {
    unsigned data, instruction;
    unsigned ime=enterCriticalSection();
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 2\n"
                     "mrc p15, 0, %1, c5, c0, 3"
                     : "=r"(data), "=r"(instruction));
    /* Region 2 already covers 0..4095. ITCM is mirrored there, so a
     * corrupt near-null primitive pointer can overwrite the same code
     * without using its linked 0x01000000 address. Keep reads permitted
     * but disallow writes through that alias as well. */
    data=(data & 0x0ffff0ff) | 0x60000600;
    instruction=(instruction & 0x0fffffff) | 0x60000000;
    unsigned region=0x0100001b; /* 16 KiB, enabled */
    __asm__ volatile("mcr p15, 0, %0, c5, c0, 2\n"
                     "mcr p15, 0, %1, c5, c0, 3\n"
                     "mcr p15, 0, %2, c6, c7, 0\n"
                     "mcr p15, 0, %2, c7, c5, 4"
                     : : "r"(data), "r"(instruction), "r"(region) : "memory");
    leaveCriticalSection(ime);
    setExceptionHandler(guard_exception);
}

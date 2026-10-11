/* PS1 BIOS functions whose results the game depends on, where the C library's version differs.
 * (memcpy, memset, strcmp, sprintf, ... come from libc: same results.)
 */
#include "psx_runtime.h"

/* A(2Fh) rand / A(30h) srand (psx-spx "BIOS Memory/String Functions").
 * The seed's power-on value isn't documented; it only matters for lockstep comparisons, which seed both sides. */
static unsigned int rand_seed = 0;

int rand(void) {
    rand_seed = rand_seed * 0x41C64E6D + 0x3039;
    return (rand_seed >> 16) & 0x7FFF;
}

void srand(unsigned int seed) {
    rand_seed = seed;
}

/* B(0Bh) TestEvent (decomp placeholder func_80065208): irq.c keeps the events */
long TestEvent(unsigned long ev);
long func_80065208(unsigned long ev) {
    return TestEvent(ev);
}

/* A(19h) strcpy (func_80065238) */
char *func_80065238(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
    }
    return dst;
}

/* Overlay 0B explicitly supplies the MIPS hidden structure-return argument.
 * Route that call through a normal native struct-return call, so both x86 and
 * ARM use their own ABI without treating the output buffer as a text string. */
#define M2CTX
#include "ff9.h"
void port_measure_text_into(void *out, const void *text) {
    TextSize size = func_8002AF70((u8 *)text);
    __builtin_memcpy(out, &size, sizeof size);
}

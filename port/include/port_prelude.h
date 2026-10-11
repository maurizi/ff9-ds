/* Force-included (-include) before every decomp C file.
 *
 * 1. Turn off the decomp's asm includes: INCLUDE_ASM/INCLUDE_RODATA become nothing, so a function still in asm is
 *    an undefined symbol (supplied by src_equiv/ or port/, or reported by the link).
 * 2. Include the file's own header (PORT_HEADER: ff9.h or ovl_XX.h) now, then replace the hardware macros it
 *    defined (GTE, SPU registers, scratchpad stack, inline-asm addPrim) with port versions. The file's own
 *    #include of that header is then a no-op thanks to its include guard.
 */
#ifndef PORT_PRELUDE_H
#define PORT_PRELUDE_H

#define INCLUDE_ASM(FOLDER, NAME)
#define INCLUDE_RODATA(FOLDER, NAME)
#define M2CTX /* include_asm.h: skip the top-level `.include "labels.inc"` */

#include PORT_HEADER
#include "port_hw.h"

#endif

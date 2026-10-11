/* For port/psx modules that implement PsyQ: the decomp's own psyq.h, so every implementation is checked against
 * the prototypes the game was compiled with. (Don't also include <string.h>: psyq.h declares memcpy the
 * game's way. Use __builtin_memcpy/__builtin_memset.) */
#ifndef PSYQ_PORT_H
#define PSYQ_PORT_H

#define M2CTX /* include_asm.h: no MIPS .include */
#include "psyq.h"

#include "psx_mem.h"
#include "psx_runtime.h"

/* Virtual interrupts (irq.c): the PS1's interrupt-driven work happens at defined points, deterministically. */
void psx_frame(void);           /* one 60 Hz frame: CD, root counters, pad, VSync callback, then wait */
void psx_drawsync_pending(void); /* gpu.c: a DrawSyncCallback is owed */

/* CD (cd.c): run queued commands and deliver sectors until the game stops asking. */
void psx_cd_pump(void);

/* Pad (pad.c): refresh the buffers PadInitDirect registered (called once per frame). */
void psx_pad_update(void);
/* Platform pad input: PS1 digital buttons, active-high (bit layout as the pad sends it, inverted). */
unsigned int plat_pad_buttons(int port);

#endif

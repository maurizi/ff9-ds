/* What port/psx needs from the platform (port/ds, port/pc). Kept small: everything else is shared. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdarg.h>

/* Block until the next 60 Hz frame boundary (DS: the real VBlank; PC: a 60 Hz pace, or none when headless). */
void plat_wait_frame(void);

/* Called once the PS1 memory (VRAM/SPU shadows) is allocated: platform video setup. */
void plat_video_init(void);

/* Debug output (DS: bottom-screen console; PC: stderr). */
void plat_vlog(const char *fmt, va_list ap);

/* Stop for good: after a fatal error. */
void plat_halt(void) __attribute__((noreturn));

/* Directory holding the game data (ff9ds/ on the SD card, or a path given on the PC command line), and the
 * raw disc images for the read-the-image-directly CD backend. */
const char *plat_data_dir(void);

#endif

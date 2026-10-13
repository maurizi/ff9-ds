#ifndef DS_DISPLAY_SCALE_H
#define DS_DISPLAY_SCALE_H
#include <stdint.h>

/* Shrink the PSX display by 4/5 with area coverage, into a 256-wide bitmap.
 * The source is wrapped 1024x512 VRAM; output is capped at 256x192. */
void ds_scale_display(uint16_t *dst, const uint16_t *vram,
                      int x, int y, int width, int height);
typedef struct { int16_t x, y, w, h; } DsTextRect;
void ds_scale_display_text(uint16_t *dst, const uint16_t *vram,
                          int x, int y, int width, int height,
                          const DsTextRect *rects, unsigned count);
void ds_scale_display_text_pair(uint16_t *dst, const uint16_t *vram,
                               int x, int y, int width, int height,
                               const DsTextRect *rects, unsigned count,
                               const DsTextRect *previous, unsigned previous_count);
#endif

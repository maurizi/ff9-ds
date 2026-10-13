#ifndef PSX_RASTER_H
#define PSX_RASTER_H
#include "gp0.h"
/* Rasterise geometry into the GX destination shadow, sampling textures from
 * PSX VRAM. Known pixels permit exact subsequent sprite compositing. */
void psx_softgpu_shadow_poly(const Gp0Mode *, const Gp0Vtx *, int,
                            unsigned short *, unsigned char *, int, int, int, int);
#endif

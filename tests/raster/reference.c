/* Keep the generic colour-interpolation path as a differential oracle. */
#define PORT_SOFTGPU_REFERENCE
#define psx_softgpu psx_softgpu_reference
#define psx_softgpu_enable psx_softgpu_reference_enable
#define psx_softgpu_shadow_poly psx_softgpu_reference_shadow_poly
#include "../../port/psx/softgpu.c"

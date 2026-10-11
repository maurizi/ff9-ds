/* The PS1 address space: psx_ram (see psx_mem.h), the scratchpad, the SPU register file, and the data images. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_mem.h"
#include "psx_runtime.h"

unsigned char psx_ram[PSX_RAM_SIZE] __attribute__((aligned(32)));
unsigned char psx_scratchpad[0x400] __attribute__((aligned(32)));
unsigned short psx_spu_regs[0x200];

/* File id of the overlay loaded in each slot (0x800A7000, 0x801EDF00, 0x80198700); 0 = none. */
unsigned char psx_ovl_resident[3];

unsigned int psx_hw_bad(unsigned int a) {
    port_fatal("access to unmapped PS1 hardware address 0x%08X", a);
}

void psx_bad_overlay_call(const char *tramp) {
    port_fatal("%s called with overlays %02X/%02X/%02X resident", tramp, psx_ovl_resident[0], psx_ovl_resident[1],
               psx_ovl_resident[2]);
}

void psx_load_image(const PsxImageChunk *img) {
    for (; img->size != 0; img++) {
        memcpy(psx_ram + img->offset, img->data, img->size);
    }
}

int psx_overlay_load(unsigned int id) {
    for (const PsxOverlay *o = psx_overlays; o->image != NULL; o++) {
        if (o->id == id) {
            psx_load_image(o->image);
            psx_ovl_resident[o->slot] = (unsigned char)id;
            return 1;
        }
    }
    return 0;
}

/* The PS1 address space: psx_ram (see psx_mem.h), the scratchpad, the SPU register file, and the data images. */
#include <stdlib.h>
#include <string.h>

#include "psx_mem.h"
#include "platform.h"
#include "psx_runtime.h"

/* psx_ram itself is defined by the platform: a bss array on the DS, a fixed mapping at 0x02000000 on the PC. */
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

void func_800127A0(void); /* the game's main loop (decomp src/main/main.c) */

extern unsigned short *psx_vram;
extern unsigned char *psx_spu_ram;

/* The VRAM and SPU RAM shadows (1.5 MB): allocated, so a 4 MB DS can still run the game logic without them. */
static void psx_mem_init(void) {
    psx_vram = malloc(1024 * 512 * 2);
    psx_spu_ram = malloc(0x80000);
    if (!psx_vram || !psx_spu_ram) {
        port_log("no room for the VRAM/SPU shadows: running without graphics and sound memory\n");
    }
    if (psx_vram) {
        memset(psx_vram, 0, 1024 * 512 * 2);
    }
}

void psx_run_game(void) {
    psx_mem_init();
    plat_video_init();
    psx_load_image(psximg_main);
    func_800127A0();
    port_fatal("the game's main loop returned");
}

/* Raw overlay bytes from the disc just landed in psx_ram at arena offset `off` (length `len`), and they are
 * overlay `id`'s file bytes at its slot. Overwrite them with the relocated image where the two overlap
 * (rodata/data: pointers fixed up for the port; code bytes stay raw and are never executed), and mark the
 * overlay resident. Called from libcd for every sector of a dir-0 code file. */
void psx_overlay_patch(unsigned int id, unsigned int off, unsigned int len) {
    for (const PsxOverlay *o = psx_overlays; o->image != NULL; o++) {
        if (o->id != id) {
            continue;
        }
        psx_ovl_resident[o->slot] = (unsigned char)id;
        for (const PsxImageChunk *c = o->image; c->size; c++) {
            unsigned int lo = off > c->offset ? off : c->offset;
            unsigned int hi = off + len < c->offset + c->size ? off + len : c->offset + c->size;
            if (lo < hi) {
                memcpy(psx_ram + lo, (const unsigned char *)c->data + (lo - c->offset), hi - lo);
            }
        }
        return;
    }
}

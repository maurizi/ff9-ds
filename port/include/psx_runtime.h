/* Port runtime services shared by port/psx and port/ds. */
#ifndef PSX_RUNTIME_H
#define PSX_RUNTIME_H

/* One chunk of a binary's initialised data (generated img_<bin>.s): copy `size` bytes to psx_ram + offset. */
typedef struct {
    unsigned int offset;
    unsigned int size;
    const void *data;
} PsxImageChunk;

extern const PsxImageChunk psximg_main[];

/* Code overlays (generated): FF9.IMG dir-0 file id, slot (0x800A7000, 0x801EDF00, 0x80198700), data image. */
typedef struct {
    unsigned int id;
    unsigned int slot;
    const PsxImageChunk *image;
} PsxOverlay;

extern const PsxOverlay psx_overlays[];

/* "Load" code overlay `id`: copy its data image into its slot and mark it resident (0 if `id` isn't code). */
int psx_overlay_load(unsigned int id);
void psx_overlay_patch(unsigned int id, unsigned int off, unsigned int len);

/* Load addresses of the code overlay slots (index = PsxOverlay.slot). */
#define PSX_OVL_SLOT_ADDRS {0x800A7000, 0x801EDF00, 0x80198700}
extern unsigned char psx_ovl_resident[3];

void psx_load_image(const PsxImageChunk *img);

/* Load main's data image and enter the game's main loop (decomp func_800127A0). Never returns. */
void psx_run_game(void);
void port_scenario_load(const char *path);
void port_scenario_bootstrap(void);
int port_scenario_seed(int ordinary_seed);

/* Print to the debug console. */
void port_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Print to the debug console and stop. */
void port_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* Called by generated stubs for routines the port doesn't provide yet. */
void port_stub_hit(const char *name) __attribute__((noreturn));

#endif

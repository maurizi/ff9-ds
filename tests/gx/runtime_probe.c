/* Run the real GX translator with the real game on PC. Models a contiguous 512 KiB
 * texture allocator and a 64 KiB shared palette allocator; no rasterisation,
 * timing or bank remapping. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nds.h"

#define NAMES 16384
#define BLOCKS (512 * 1024 / 8)
static unsigned char occupied[BLOCKS];
#define PAL_BLOCKS (64*1024/16)
static unsigned char pal_occupied[PAL_BLOCKS];
static struct { int used, offset, blocks, palette; } textures[NAMES];
static struct { int refs, offset, blocks; } palettes[NAMES];
static int pal_live, pal_peak, pal_failures;
static int bound, live_bytes, peak_bytes, submission, failures;
extern int psx_gx_stats_polys, psx_gx_stats_texture_fail, psx_gx_stats_budget_fail;

int glGenTextures(int n, int *names) {
    assert(n == 1);
    for (int i = 1; i < NAMES; i++) if (!textures[i].used) {
        textures[i].used = 1; textures[i].blocks = 0; textures[i].palette=0; *names = i; return 1;
    }
    return 0;
}
void glBindTexture(int target, int name) { (void)target; bound = name; }
static void release_palette(int name) {
    int id=textures[name].palette;
    if (!id) return;
    assert(palettes[id].refs>0);
    if (!--palettes[id].refs) {
        memset(pal_occupied+palettes[id].offset,0,palettes[id].blocks);
        pal_live-=palettes[id].blocks*16;
    }
    textures[name].palette=0;
}
void glDeleteTextures(int n, const int *names) {
    assert(n == 1);
    int name = *names;
    assert(name > 0 && name < NAMES && textures[name].used);
    memset(occupied + textures[name].offset, 0, textures[name].blocks);
    live_bytes -= textures[name].blocks * 8;
    release_palette(name);
    textures[name].used = 0; textures[name].blocks = 0;
}
int glTexImage2D(int target, int level, GL_TEXTURE_TYPE_ENUM type, int w, int h,
                 int border, int params, const void *data) {
    (void)target; (void)level; (void)border; (void)params; (void)data;
    assert(bound > 0 && bound < NAMES && textures[bound].used && !textures[bound].blocks);
    int bytes = w * h * (type == GL_RGBA ? 2 : 1) / (type == GL_RGB16 ? 2 : 1);
    int blocks = (bytes + 7) / 8, run = 0;
    for (int i = 0; i < BLOCKS; i++) {
        run = occupied[i] ? 0 : run + 1;
        if (run == blocks) {
            int at = i + 1 - run;
            memset(occupied + at, 1, blocks);
            textures[bound].offset = at; textures[bound].blocks = blocks;
            live_bytes += blocks * 8;
            if (live_bytes > peak_bytes) peak_bytes = live_bytes;
            return 1;
        }
    }
    if (failures++ < 12) fprintf(stderr, "gx allocator: submission %d request %dx%d/%d bytes, live %d\n",
                                 submission, w, h, bytes, live_bytes);
    return 0;
}
int glColorTableEXT(int target, int unused, size_t count, int a, int b, const void *palette) {
    (void)target; (void)unused; (void)a; (void)b; (void)palette;
    release_palette(bound);
    int id=1; while(id<NAMES && palettes[id].refs) id++;
    assert(id<NAMES);
    int blocks=(count*2+15)/16,run=0;
    for(int i=0;i<PAL_BLOCKS;i++) {
        run=pal_occupied[i]?0:run+1;
        if(run==blocks) {
            int at=i+1-run; memset(pal_occupied+at,1,blocks);
            palettes[id].refs=1;palettes[id].offset=at;palettes[id].blocks=blocks;
            textures[bound].palette=id;pal_live+=blocks*16;
            if(pal_live>pal_peak) pal_peak=pal_live;
            return 1;
        }
    }
    pal_failures++; return 0;
}
int glAssignColorTable(int target, int name) {
    (void)target;int id=textures[name].palette;
    if(id) palettes[id].refs++;
    release_palette(bound);textures[bound].palette=id;
    return id!=0;
}
void glVertex3v16(v16 x, v16 y, v16 z) { (void)x; (void)y; (void)z; }
void glFlush(int mode) {
    (void)mode; submission++;
    static int last_fail;
    int fail = psx_gx_stats_texture_fail + psx_gx_stats_budget_fail;
    if (!(submission % 100) || fail != last_fail) {
        fprintf(stderr, "gx submission %d: polys %d texture-fail %d budget-fail %d live %d peak %d palettes %d/%d palette-fail %d\n",
                submission, psx_gx_stats_polys, psx_gx_stats_texture_fail,
                psx_gx_stats_budget_fail, live_bytes, peak_bytes,pal_live,pal_peak,pal_failures);
        last_fail = fail;
    }
}

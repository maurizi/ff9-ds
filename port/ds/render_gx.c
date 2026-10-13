/* DS 3D-hardware backend for the GP0 decoder (gp0.h): PS1 primitives become GX polygons (docs/design.md §5.1).
 *
 * - Coordinates: PS1 pixels relative to the drawing area's origin, through an orthographic projection onto a
 *   256x179 viewport (x0.8, letterboxed). Vertices are v16 with 1 unit = 1 PS1 pixel.
 * - Order: each primitive gets the next depth (later = nearer), so the depth buffer reproduces the PS1's painter's
 *   order; translucent polygons are drawn in submission order (manual sort).
 * - Textures: the used region of a PS1 texture page becomes a power-of-two DS texture (4-bit -> 16-colour, 8-bit ->
 *   256-colour, 15-bit -> direct). PS1 transparency is by colour (0x0000) and DS's by palette index 0, so indices
 *   are remapped when the page is converted. Converted pages are cached (LRU) and rebuilt when the VRAM blocks
 *   they came from change (psx_vram_touch). Palettes are shared; submitted textures are pinned until rasterised.
 * - Semi-transparency: subtraction over known sprite pixels is CPU-composited. Other blends use polygon alpha;
 *   subtractive geometry and mixed STP textures outside that sprite path are approximated.
 * - Drawing to areas smaller than a screen (offscreen effects) is skipped for now.
 */
#include <nds.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "gp0.h"
#include "psx_raster.h"
#include "psx_mem.h"
#include "psx_runtime.h"

extern unsigned short *psx_vram;

#define MAX_POLYS 2000 /* the hardware holds 2048 per frame */
#define MAX_VERTICES 6144
#define CACHE 2048
#define CACHE_BUCKETS 4096

typedef unsigned int CompareWord __attribute__((may_alias));
/* Cache data and palettes are usually word-aligned. Picolibc's generic byte
 * comparison is expensive on ARM9; only aligned pointers use word loads. */
static int gx_memdiff(const void *left, const void *right, size_t size) {
    const u8 *a = left, *b = right;
    if (!(((uintptr_t)a | (uintptr_t)b) & 3)) {
        const CompareWord *aw = (const CompareWord *)a, *bw = (const CompareWord *)b;
        while (size >= 16) {
            if (aw[0] != bw[0] || aw[1] != bw[1] || aw[2] != bw[2] || aw[3] != bw[3]) return 1;
            aw += 4; bw += 4; size -= 16;
        }
        while (size >= 4) {
            if (*aw++ != *bw++) return 1;
            size -= 4;
        }
        a = (const u8 *)aw; b = (const u8 *)bw;
    }
    while (size--) if (*a++ != *b++) return 1;
    return 0;
}

static unsigned int pixel_hash(const void *pixels, size_t bytes) {
    unsigned int hash = 2166136261u;
    const u8 *p = pixels;
    if (!((uintptr_t)p & 3)) {
        const CompareWord *words = (const CompareWord *)p;
        while (bytes >= 4) { hash = (hash ^ *words++) * 16777619u; bytes -= 4; }
        p = (const u8 *)words;
    }
    while (bytes--) hash = (hash ^ *p++) * 16777619u;
    return hash;
}

typedef struct {
    int used;
    u64 key;
    int tex;     /* GL texture name */
    unsigned int stamp;
    unsigned int src_gen; /* source_gen() when it was built */
    unsigned int frame; /* last submitted frame; textures remain live through rasterisation */
    unsigned int window; /* texture-window state baked into the converted region */
    unsigned int palette_key, palette_gen;
    u16 bucket, next; /* cache lookup chains; indices are stored plus one */
    u16 page_bucket, page_next;
    u16 content_bucket, content_next, image;
    u16 palette_bucket, palette_next, palette_count;
    u8 *pixels;
    u16 *palette;
    int params;
} TexEntry;

static TexEntry cache[CACHE];
static u16 cache_heads[CACHE_BUCKETS];
static u16 page_heads[CACHE_BUCKETS];
static u16 content_heads[CACHE_BUCKETS];
static u16 palette_heads[CACHE_BUCKETS];
/* Indexed images can be used with several PSX CLUTs. Keep pixel storage
 * independent of palette handles; queued polygons retain their palette RAM. */
typedef struct {
    int used, tex, refs, width, height, params;
    GL_TEXTURE_TYPE_ENUM format;
    u16 bucket, next;
    u8 *pixels;
} TextureImage;
static TextureImage images[CACHE];
static u16 image_heads[CACHE_BUCKETS];

static unsigned int stamp;
static unsigned int frame = 2;

static unsigned int cache_bucket(u64 key, unsigned int window) {
    unsigned int hash = (unsigned int)key ^ (unsigned int)(key >> 32) ^ window;
    hash ^= hash >> 16;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15;
    return hash & (CACHE_BUCKETS - 1);
}

static TexEntry *evictable_entry(int allocated_only) {
    TexEntry *oldest = NULL;
    for (int i = 0; i < CACHE; i++) {
        TexEntry *e = &cache[i];
        if (!e->used) {
            if (!allocated_only) return e;
            continue;
        }
        if (allocated_only == 2 && !e->palette) continue;
        /* GX renders the preceding submission while the CPU prepares this one. */
        if (frame - e->frame > 1 && (!oldest || e->stamp < oldest->stamp)) oldest = e;
    }
    return oldest;
}

/* VRAM write generations per 64x8 block (16 x 64 blocks): a converted page is rebuilt only when a block it was made
 * from (its page or its CLUT) changed since. */
static unsigned int block_gen[64][16];
static unsigned int palette_row_gen[512][16];
static unsigned int gen_counter = 1;
static void flush_rect(void);
static void flush_buffer(void);
static int buffer_depends(int x, int y, int w, int h);

/* gpu.c: VRAM area written (uploads, fills, copies) */
void psx_vram_touch(int x, int y, int w, int h) {
    /* gpu.c calls this before changing pixels or palettes. */
    if (buffer_depends(x, y, w, h)) flush_buffer();
    flush_rect();
    if (w <= 0 || h <= 0) {
        return;
    }
    gen_counter++;
    int nx = ((x & 63) + w + 63) / 64;
    int ny = ((y & 7) + h + 7) / 8;
    if (nx > 16) nx = 16;
    if (ny > 64) ny = 64;
    for (int by = 0; by < ny; by++) {
        for (int bx = 0; bx < nx; bx++) {
            block_gen[((y & 511) / 8 + by) & 63][((x & 1023) / 64 + bx) & 15] = gen_counter;
        }
    }
    int rows = h > 512 ? 512 : h;
    for (int row = 0; row < rows; row++) for (int bx = 0; bx < nx; bx++)
        palette_row_gen[(y + row) & 511][((x & 1023) / 64 + bx) & 15] = gen_counter;
}

/* newest generation among the blocks a texture page (and CLUT) is made from */
static unsigned int source_gen(const Gp0Mode *m, int u0, int v0, int width, int height) {
    unsigned int xs = 0, ys[2] = {0, 0};
    int shift = m->depth == 0 ? 2 : m->depth == 1 ? 1 : 0;
    /* Windows operate in eight-texel units; sample each unit to cover every source block. */
    for (int u = u0 & ~7; u < u0 + width; u += 8) {
        int tu = (u & ~gp0_state.twin_mask_x) | (gp0_state.twin_off_x & gp0_state.twin_mask_x);
        xs |= 1u << (((m->tx + (tu >> shift)) & 1023) >> 6);
    }
    for (int v = v0 & ~7; v < v0 + height; v += 8) {
        int tv = (v & ~gp0_state.twin_mask_y) | (gp0_state.twin_off_y & gp0_state.twin_mask_y);
        int row = ((m->ty + tv) & 511) >> 3;
        ys[row >> 5] |= 1u << (row & 31);
    }
    unsigned int g = 0;
    for (int half = 0; half < 2; half++) for (unsigned int rows = ys[half]; rows; rows &= rows - 1) {
        int by = half * 32 + __builtin_ctz(rows);
        for (unsigned int cols = xs; cols; cols &= cols - 1) {
            int bx = __builtin_ctz(cols);
            unsigned int b = block_gen[by][bx];
            g = b > g ? b : g;
        }
    }
    if (m->depth < 2) {
        int ncol = m->depth == 0 ? 16 : 256;
        int blocks = ((m->clut_x & 63) + ncol + 63) / 64;
        for (int i = 0; i < blocks; i++) {
            unsigned int b = palette_row_gen[m->clut_y & 511][((m->clut_x >> 6) + i) & 15];
            g = b > g ? b : g;
        }
    }
    return g;
}
static int z, polys, vertices, cur_tex = -1;
static int clear_pending;
static int tex_u0, tex_v0;
static int texture_origin_u, texture_origin_v;
static int origin_x, origin_y;
static int target_w, target_h;
/* Sprite pixels support subtractive compositing; polygon coverage invalidates
 * the shadow unless the experimental geometry replay is enabled. */
#define SHADOW_W 320
#define SHADOW_H 224
static u16 sprite_shadow[SHADOW_W * SHADOW_H];
static u8 sprite_known[SHADOW_W * SHADOW_H];
static int shadow_ready;
static u16 clear_rgb;
static u8 convbuf[256 * 256] __attribute__((aligned(4)));      /* conversion scratch (8-bit page) */
static u16 convbuf16[256 * 256] __attribute__((aligned(4)));   /* 15-bit page */
int psx_gx_stats_polys, psx_gx_stats_dropped;
int psx_gx_stats_texture_fail, psx_gx_stats_budget_fail;
int psx_gx_stats_composited, psx_gx_stats_blend_fallback;

/* Adjacent opaque tiles with contiguous UVs can share one GX rectangle. */
static struct {
    int active, w, h, draw[4];
    Gp0Mode mode;
    Gp0Vtx vertex;
    Gp0State state;
} rect_run;
static int flushing_rect;
static void flush_rect(void);
static void gx_rect_now(const Gp0Mode *, const Gp0Vtx *, int, int);

static inline u16 ds_color(u16 c) {
    return c & 0x7FFF;
}

/* ---- textures ---------------------------------------------------------------------------------------- */

static unsigned int tex_key(const Gp0Mode *m) {
    unsigned int k = (unsigned)m->depth << 30 | (unsigned)(m->tx >> 6) << 26 | (unsigned)(m->ty >> 8) << 25;
    if (m->depth < 2) {
        k |= (unsigned)(m->clut_x >> 4) << 9 | (unsigned)m->clut_y;
    }
    return k;
}

static void release_image(int id) {
    TextureImage *image = &images[id - 1];
    if (--image->refs) return;
    u16 *link = &image_heads[image->bucket];
    while (*link && &images[*link - 1] != image) link = &images[*link - 1].next;
    if (*link) *link = image->next;
    glDeleteTextures(1, &image->tex);
    free(image->pixels); image->pixels = NULL; image->used = 0;
}

static void free_pixels(TexEntry *e) {
    if (e->image) release_image(e->image);
    else free(e->pixels);
    e->image = 0; e->pixels = NULL;
}

static void free_entry(TexEntry *e) {
    if (e->used) {
        u16 *link = &cache_heads[e->bucket];
        while (*link && &cache[*link - 1] != e) link = &cache[*link - 1].next;
        if (*link) *link = e->next;
        if (!(e->key >> 63)) {
            link = &page_heads[e->page_bucket];
            while (*link && &cache[*link - 1] != e) link = &cache[*link - 1].page_next;
            if (*link) *link = e->page_next;
            if (((e->key >> 30) & 3) == 2) {
                link = &content_heads[e->content_bucket];
                while (*link && &cache[*link - 1] != e) link = &cache[*link - 1].content_next;
                if (*link) *link = e->content_next;
            }
        }
        if (e->palette) {
            link = &palette_heads[e->palette_bucket];
            while (*link && &cache[*link - 1] != e) link = &cache[*link - 1].palette_next;
            if (*link) *link = e->palette_next;
        }
        glDeleteTextures(1, &e->tex);
        free_pixels(e);
        free(e->palette);
        e->pixels = NULL; e->palette = NULL;
        e->used = 0;
    }
}

static int acquire_image(GL_TEXTURE_TYPE_ENUM format, int width, int height, int params,
                         const void *pixels, size_t bytes) {
    unsigned int hash = pixel_hash(pixels, bytes);
    unsigned int bucket = cache_bucket(hash, width | (height << 10) | (format << 20)) ^ cache_bucket(params, 0);
    for (unsigned int index = image_heads[bucket]; index; index = images[index - 1].next) {
        TextureImage *image = &images[index - 1];
        if (image->format == format && image->width == width && image->height == height
            && image->params == params && !gx_memdiff(image->pixels, pixels, bytes)) {
            image->refs++; return index;
        }
    }
    TextureImage *image = NULL;
    for (int i = 0; i < CACHE; i++) if (!images[i].used) { image = &images[i]; break; }
    if (!image) return 0;
    image->pixels = malloc(bytes);
    if (!image->pixels || !glGenTextures(1, &image->tex)) {
        free(image->pixels); image->pixels = NULL; return 0;
    }
    memcpy(image->pixels, pixels, bytes);
    glBindTexture(0, image->tex);
    int uploaded;
    while (!(uploaded = glTexImage2D(0, 0, format, width, height, 0, params, pixels))) {
        TexEntry *old = evictable_entry(1);
        if (!old) break;
        free_entry(old); glBindTexture(0, image->tex);
    }
    if (!uploaded) {
        glDeleteTextures(1, &image->tex); free(image->pixels); image->pixels = NULL;
        return 0;
    }
    image->used = image->refs = 1; image->format = format;
    image->width = width; image->height = height; image->params = params;
    image->bucket = bucket; image->next = image_heads[bucket];
    int id = image - images + 1; image_heads[bucket] = id;
    return id;
}

static int bind_entry(TexEntry *e) {
    if (!e->image) { glBindTexture(0, e->tex); return 1; }
    glBindTexture(0, images[e->image - 1].tex);
    return glAssignColorTable(0, e->tex);
}

static int upload_palette(TexEntry *e, int count) {
    unsigned int hash=2166136261u;
    for (int i=0;i<count;i++) hash=(hash^e->palette[i])*16777619u;
    e->palette_bucket=cache_bucket(hash,count);
    e->palette_count=count;
    for (unsigned int index=palette_heads[e->palette_bucket];index;index=cache[index-1].palette_next) {
        TexEntry *other=&cache[index-1];
        if (other->palette_count==count && !gx_memdiff(other->palette,e->palette,count*sizeof(u16))
            && glAssignColorTable(0,other->tex)) return 1;
    }
    while (!glColorTableEXT(0,0,count,0,0,e->palette)) {
        TexEntry *old=evictable_entry(2);
        if (!old) return 0;
        free_entry(old);
        glBindTexture(0,e->tex);
    }
    return 1;
}
static void link_palette(TexEntry *e) {
    if (!e->palette) return;
    e->palette_next=palette_heads[e->palette_bucket];
    palette_heads[e->palette_bucket]=(u16)(e-cache+1);
}

static TexEntry *get_texture(const Gp0Mode *m, int u0, int v0, int width, int height) {
    texture_origin_u = u0;
    texture_origin_v = v0;
    u64 key = tex_key(m) | ((u64)u0 << 32) | ((u64)v0 << 40)
              | ((u64)(width / 16) << 48) | ((u64)(height / 16) << 53);
    unsigned int gen = source_gen(m, u0, v0, width, height);
    unsigned int window = (gp0_state.twin_mask_x >> 3) | ((gp0_state.twin_mask_y >> 3) << 5)
                          | ((gp0_state.twin_off_x >> 3) << 10) | ((gp0_state.twin_off_y >> 3) << 15);
    unsigned int palette_key = (m->depth << 16) | (m->clut_y << 6) | (m->clut_x >> 4);
    unsigned int palette_gen = 0;
    if (m->depth < 2) {
        int blocks = ((m->clut_x & 63) + (m->depth ? 256 : 16) + 63) / 64;
        for (int i = 0; i < blocks; i++) {
            unsigned int b = palette_row_gen[m->clut_y & 511][((m->clut_x >> 6) + i) & 15];
            if (b > palette_gen) palette_gen = b;
        }
    }
    unsigned int bucket = cache_bucket(key, window);
    unsigned int page_bucket = cache_bucket(tex_key(m), window);
    for (unsigned int index = cache_heads[bucket]; index; index = cache[index - 1].next) {
        TexEntry *e = &cache[index - 1];
        if (e->used && e->key == key && e->src_gen == gen && e->window == window) {
            e->stamp = ++stamp;
            e->frame = frame;
            return e;
        }
    }
    /* A large face can already have uploaded the texels needed by a smaller face.
     * Reuse that enclosing region instead of duplicating it under another region key.
     * Validate the whole parent, since its dirty generation may differ from the child. */
    TexEntry *parent = NULL;
    for (unsigned int index = page_heads[page_bucket]; index; index = cache[index - 1].page_next) {
        TexEntry *e = &cache[index - 1];
        /* Preserve the old array-order preference while examining only this page. */
        if (parent && e > parent) continue;
        if (!e->used || (unsigned int)e->key != tex_key(m) || e->window != window || (e->key >> 63)) continue;
        int ex = (e->key >> 32) & 255, ey = (e->key >> 40) & 255;
        int ew = ((e->key >> 48) & 31) * 16, eh = ((e->key >> 53) & 31) * 16;
        if (ex <= u0 && ey <= v0 && ex + ew >= u0 + width && ey + eh >= v0 + height
            && e->src_gen == source_gen(m, ex, ey, ew, eh)) {
            parent = e;
        }
    }
    if (parent) {
        texture_origin_u = (parent->key >> 32) & 255;
        texture_origin_v = (parent->key >> 40) & 255;
        parent->stamp = ++stamp; parent->frame = frame;
        return parent;
    }
    u16 pal[256];
    int ncol = m->depth == 0 ? 16 : 256;
    int transparent = 0;
    u16 clut[256];
    u8 map[256];
    if (m->depth < 2) {
        for (int i = 0; i < ncol; i++) {
            clut[i] = psx_vram[(m->clut_y & 511) * 1024 + ((m->clut_x + i) & 1023)];
        }
        /* PS1 transparency is colour 0x0000; DS's is palette index 0: move transparent colours to index 0 */
        int first_t = -1;
        for (int i = 0; i < ncol; i++) {
            if (clut[i] == 0 && first_t < 0) {
                first_t = i;
            }
        }
        for (int i = 0; i < ncol; i++) {
            map[i] = (u8)i;
        }
        if (first_t >= 0) {
            transparent = 1;
            map[first_t] = 0;
            map[0] = (u8)first_t;
            for (int i = 0; i < ncol; i++) {
                if (clut[i] == 0) {
                    map[i] = 0;
                }
            }
        }
        for (int i = 0; i < ncol; i++) {
            pal[i] = 0;
        }
        for (int i = 0; i < ncol; i++) {
            if (clut[i] != 0 || !transparent) {
                pal[map[i]] = ds_color(clut[i]);
            }
        }
    }

    GL_TEXTURE_TYPE_ENUM format;
    const void *pixels;
    int params = TEXGEN_OFF | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
    if (transparent) params |= GL_TEXTURE_COLOR0_TRANSPARENT;
    if (m->depth == 0) {
        u8 lut[256];
        for (int b = 0; b < 256; b++) {
            lut[b] = (u8)(map[b & 15] | map[b >> 4] << 4);
        }
        for (int y = 0; y < height; y++) {
            int tv = ((v0 + y) & ~gp0_state.twin_mask_y) | (gp0_state.twin_off_y & gp0_state.twin_mask_y);
            const u8 *src = (const u8 *)&psx_vram[((m->ty + tv) & 511) * 1024];
            u8 *dst = convbuf + y * (width / 2);
            for (int x = 0; x < width / 2; x++) {
                int tu = ((u0 + x * 2) & ~gp0_state.twin_mask_x) | (gp0_state.twin_off_x & gp0_state.twin_mask_x);
                dst[x] = lut[src[(m->tx * 2 + tu / 2) & 2047]];
            }
        }
        format = GL_RGB16;
        pixels = convbuf;
    } else if (m->depth == 1) {
        for (int y = 0; y < height; y++) {
            int tv = ((v0 + y) & ~gp0_state.twin_mask_y) | (gp0_state.twin_off_y & gp0_state.twin_mask_y);
            const u8 *src = (const u8 *)&psx_vram[((m->ty + tv) & 511) * 1024];
            u8 *dst = convbuf + y * width;
            for (int x = 0; x < width; x++) {
                int tu = ((u0 + x) & ~gp0_state.twin_mask_x) | (gp0_state.twin_off_x & gp0_state.twin_mask_x);
                dst[x] = map[src[(m->tx * 2 + tu) & 2047]];
            }
        }
        format = GL_RGB256;
        pixels = convbuf;
    } else {
        for (int y = 0; y < height; y++) {
            int tv = ((v0 + y) & ~gp0_state.twin_mask_y) | (gp0_state.twin_off_y & gp0_state.twin_mask_y);
            const u16 *row = &psx_vram[((m->ty + tv) & 511) * 1024];
            u16 *dst = convbuf16 + y * width;
            for (int x = 0; x < width; x++) {
                int tu = ((u0 + x) & ~gp0_state.twin_mask_x) | (gp0_state.twin_off_x & gp0_state.twin_mask_x);
                u16 c = row[(m->tx + tu) & 1023];
                dst[x] = c ? (u16)(c | 0x8000) : 0; /* DS bit 15 = opaque */
            }
        }
        format = GL_RGBA;
        pixels = convbuf16;
    }
    size_t bytes = (size_t)width * height * (m->depth == 2 ? 2 : 1) / (m->depth == 0 ? 2 : 1);
    /* A dirty block can cover unchanged texels inside an enclosing allocation.
     * Compare the requested rows and palette without declaring the entire parent
     * clean: another part of that parent may really have changed. */
    parent = NULL;
    for (unsigned int index = page_heads[page_bucket]; index; index = cache[index - 1].page_next) {
        TexEntry *e = &cache[index - 1];
        if (parent && e > parent) continue;
        if (!e->used || (unsigned int)e->key != tex_key(m) || e->window != window
            || (e->key >> 63) || e->params != params) continue;
        int ex = (e->key >> 32) & 255, ey = (e->key >> 40) & 255;
        int ew = ((e->key >> 48) & 31) * 16, eh = ((e->key >> 53) & 31) * 16;
        if (ex > u0 || ey > v0 || ex + ew < u0 + width || ey + eh < v0 + height) continue;
        if (m->depth < 2 && gx_memdiff(e->palette, pal, ncol * sizeof(u16))) continue;
        int row_bytes = width * (m->depth == 2 ? 2 : 1) / (m->depth == 0 ? 2 : 1);
        int parent_row = ew * (m->depth == 2 ? 2 : 1) / (m->depth == 0 ? 2 : 1);
        int offset = (u0 - ex) * (m->depth == 2 ? 2 : 1) / (m->depth == 0 ? 2 : 1);
        int same = 1;
        for (int y = 0; y < height; y++) {
            if (gx_memdiff((const u8 *)pixels + y * row_bytes,
                (const u8 *)e->pixels + (v0 - ey + y) * parent_row + offset, row_bytes)) {
                same = 0; break;
            }
        }
        if (same) parent = e;
    }
    if (parent) {
        texture_origin_u = (parent->key >> 32) & 255;
        texture_origin_v = (parent->key >> 40) & 255;
        parent->stamp = ++stamp; parent->frame = frame;
        return parent;
    }
    /* Dirty blocks can contain unrelated framebuffer or palette writes. Keep identical textures pinned in place. */
    for (unsigned int index = cache_heads[bucket]; index; index = cache[index - 1].next) {
        TexEntry *old = &cache[index - 1];
        if (old->key == key && old->window == window && old->params == params && !gx_memdiff(old->pixels, pixels, bytes)
            && (m->depth == 2 || !gx_memdiff(old->palette, pal, ncol * sizeof(u16)))) {
            old->src_gen = gen; old->palette_gen = palette_gen;
            old->stamp = ++stamp; old->frame = frame;
            return old;
        }
    }
    /* Different PSX regions may contain identical direct pixels. A shared GPU
     * image keeps the requested UV origin, independently of its owner's key. */
    unsigned int content_bucket = 0;
    if (m->depth == 2) {
        unsigned int hash = pixel_hash(convbuf16, bytes);
        content_bucket = cache_bucket(hash, width | (height << 10));
        for (unsigned int index = content_heads[content_bucket]; index; index = cache[index - 1].content_next) {
            TexEntry *same = &cache[index - 1];
            int sw = ((same->key >> 48) & 31) * 16, sh = ((same->key >> 53) & 31) * 16;
            if (sw == width && sh == height && same->params == params && !gx_memdiff(same->pixels, pixels, bytes)) {
                same->stamp = ++stamp; same->frame = frame;
                return same;
            }
        }
    }
    /* Changed pixels get a new allocation: earlier polygons retain old bytes. */
    TexEntry *e = evictable_entry(0);
    if (!e) return NULL;
    free_entry(e);
    e->image = 0;
#ifdef PORT_GX_SHARE_INDEXED
    if (m->depth < 2) {
        e->image = acquire_image(format, width, height, params, pixels, bytes);
        if (!e->image) return NULL;
        e->pixels = images[e->image - 1].pixels;
    } else
#endif
    e->pixels = malloc(bytes);
    e->palette = m->depth < 2 ? malloc(ncol * sizeof(u16)) : NULL;
    if (!e->pixels || (m->depth < 2 && !e->palette) || !glGenTextures(1, &e->tex)) {
        free_pixels(e); free(e->palette); e->palette = NULL;
        return NULL;
    }
    if (!e->image) memcpy(e->pixels, pixels, bytes);
    if (e->palette) memcpy(e->palette, pal, ncol * sizeof(u16));
    glBindTexture(0, e->tex);
    int uploaded = e->image != 0;
    if (!uploaded) {
        while (!(uploaded = glTexImage2D(0, 0, format, width, height, 0, params, pixels))) {
            TexEntry *old = evictable_entry(1);
            if (!old) break;
            free_entry(old); glBindTexture(0, e->tex);
        }
    }
    if (uploaded && m->depth < 2) uploaded = upload_palette(e, ncol);
    if (!uploaded) {
        glDeleteTextures(1, &e->tex);
        free_pixels(e); free(e->palette); e->palette = NULL; cur_tex = -1;
        return NULL;
    }
    e->used = 1;
    e->key = key;
    e->stamp = ++stamp;
    e->src_gen = gen;
    e->frame = frame;
    e->window = window;
    e->palette_key = palette_key;
    e->palette_gen = palette_gen;
    e->params = params;
    e->bucket = bucket;
    e->next = cache_heads[bucket];
    cache_heads[bucket] = (u16)(e - cache + 1);
    e->page_bucket = page_bucket;
    e->page_next = page_heads[page_bucket];
    page_heads[page_bucket] = (u16)(e - cache + 1);
    if (m->depth == 2) {
        e->content_bucket = content_bucket;
        e->content_next = content_heads[content_bucket];
        content_heads[content_bucket] = (u16)(e - cache + 1);
    }
    link_palette(e);
    return e;
}

/* Composited sprite pixels use a separate cache namespace and exact content comparison. */
static TexEntry *get_composite(int width, int height) {
    size_t bytes = (size_t)width * height * sizeof(u16);
    unsigned int hash = pixel_hash(convbuf16, bytes);
    u64 key = ((u64)1 << 63) | ((u64)width << 32) | ((u64)height << 41) | hash;
    unsigned int bucket = cache_bucket(key, 0);
    for (unsigned int index = cache_heads[bucket]; index; index = cache[index - 1].next) {
        TexEntry *e = &cache[index - 1];
        if (e->key == key && !gx_memdiff(e->pixels, convbuf16, bytes)) {
            e->stamp = ++stamp; e->frame = frame;
            return e;
        }
    }
    /* Many light tiles contain at most sixteen exact colours. Store those
     * as indexed pixels without reducing colour precision. Keep the original
     * direct pixels in the CPU cache for content comparison. */
    u16 colours[256] = {0}, palette[256] = {0};
    int ncol = 0, indexed = 1, transparent = 0;
#ifndef PORT_GX_EXPERIMENTAL
    indexed = 0;
#endif
    size_t count = (size_t)width * height;
    for (size_t i = 0; indexed && i < count; i++) {
        u16 c = convbuf16[i];
        int j = 0;
        while (j < ncol && colours[j] != c) j++;
        if (j == ncol) {
            if (ncol == 256) { indexed = 0; break; }
            colours[ncol++] = c;
        }
    }
    if (indexed) {
        for (int i = 0; i < ncol; i++) if (!colours[i]) {
            u16 swap = colours[0]; colours[0] = 0; colours[i] = swap;
            transparent = 1; break;
        }
        for (int i = 0; i < ncol; i++) palette[i] = colours[i] & 0x7FFF;
        memset(convbuf, 0, ncol<=16 ? count/2 : count);
        for (size_t i = 0; i < count; i++) {
            int j = 0;
            while (colours[j] != convbuf16[i]) j++;
            if (ncol<=16) convbuf[i / 2] |= (u8)(j << ((i & 1) * 4));
            else convbuf[i]=(u8)j;
        }
    }
    TexEntry *e = evictable_entry(0);
    if (!e) return NULL;
    free_entry(e);
    e->image = 0;
    e->pixels = malloc(bytes); e->palette = NULL;
    if (!e->pixels || !glGenTextures(1, &e->tex)) {
        free(e->pixels); e->pixels = NULL;
        return NULL;
    }
    memcpy(e->pixels, convbuf16, bytes);
    int palette_count=(ncol+15)&~15;
    if (indexed) {
        e->palette = malloc(palette_count*sizeof(u16));
        if (!e->palette) {
            glDeleteTextures(1, &e->tex); free(e->pixels); e->pixels = NULL;
            return NULL;
        }
        memcpy(e->palette, palette, palette_count*sizeof(u16));
    }
    glBindTexture(0, e->tex);
    int uploaded;
    int params = TEXGEN_OFF | (transparent ? GL_TEXTURE_COLOR0_TRANSPARENT : 0);
    while (!(uploaded = glTexImage2D(0, 0, indexed ? (ncol<=16 ? GL_RGB16 : GL_RGB256) : GL_RGBA,
                                    width, height, 0, params, indexed ? (void *)convbuf : (void *)convbuf16))) {
        TexEntry *old = evictable_entry(1);
        if (!old) break;
        free_entry(old);
        glBindTexture(0, e->tex);
    }
    if (uploaded && indexed) {
        uploaded = upload_palette(e,palette_count);
        if (!uploaded) {
            /* Palette RAM and pixel RAM have separate limits. Preserve exact
             * pixels with a direct texture when the palette heap is full. */
            glDeleteTextures(1,&e->tex);
            free(e->palette); e->palette=NULL; e->palette_count=0;
            if (!glGenTextures(1,&e->tex)) {
                free(e->pixels); e->pixels=NULL; cur_tex=-1; return NULL;
            }
            glBindTexture(0,e->tex);
            while (!(uploaded=glTexImage2D(0,0,GL_RGBA,width,height,0,TEXGEN_OFF,convbuf16))) {
                TexEntry *old=evictable_entry(1);
                if (!old) break;
                free_entry(old); glBindTexture(0,e->tex);
            }
        }
    }
    if (!uploaded) {
        glDeleteTextures(1, &e->tex);
        free(e->pixels); free(e->palette);
        e->pixels = NULL; e->palette = NULL; cur_tex = -1;
        return NULL;
    }
    e->used = 1; e->key = key; e->window = 0;
    e->stamp = ++stamp; e->frame = frame;
    /* A generated direct texture must never share a PSX CLUT. */
    e->palette_key = ~0u; e->palette_gen = 0;
    e->bucket = bucket; e->next = cache_heads[bucket];
    cache_heads[bucket] = (u16)(e - cache + 1);
    link_palette(e);
    return e;
}

static void shadow_start(void) {
    if (shadow_ready) return;
    for (int i = 0; i < SHADOW_W * SHADOW_H; i++) sprite_shadow[i] = clear_rgb;
    memset(sprite_known, 1, sizeof sprite_known);
    shadow_ready = 1;
}

static void shadow_invalidate(int x, int y, int w, int h) {
    shadow_start();
    x -= origin_x; y -= origin_y;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SHADOW_W) x1 = SHADOW_W;
    if (y1 > SHADOW_H) y1 = SHADOW_H;
    if (x >= x1 || y >= y1) return;
    for (; y < y1; y++) memset(sprite_known + y * SHADOW_W + x, 0, x1 - x);
}

static u16 sprite_sample(const Gp0Mode *m, int u, int v, const u16 *palette) {
    u = ((u & ~gp0_state.twin_mask_x) | (gp0_state.twin_off_x & gp0_state.twin_mask_x)) & 255;
    v = ((v & ~gp0_state.twin_mask_y) | (gp0_state.twin_off_y & gp0_state.twin_mask_y)) & 255;
    int x = m->tx + (u >> (m->depth == 0 ? 2 : m->depth == 1 ? 1 : 0));
    u16 t = psx_vram[((m->ty + v) & 511) * 1024 + (x & 1023)];
    if (m->depth < 2) {
        int index = m->depth == 0 ? (t >> ((u & 3) * 4)) & 15 : (t >> ((u & 1) * 8)) & 255;
        t = palette ? palette[index] : psx_vram[(m->clut_y & 511) * 1024 + ((m->clut_x + index) & 1023)];
    }
    return t;
}

static u16 sprite_texel(const Gp0Mode *m, int u, int v) {
    return sprite_sample(m, u, v, NULL);
}

/* PSX modulation precedes subtraction; STP-clear texels overwrite instead of blending. */
static u16 sprite_pixel_texel(const Gp0Mode *m, const Gp0Vtx *v, u16 t, u16 back) {
    if (m->textured && !t) return back;
    int r, g, b;
    if (m->textured) {
        r = t & 31; g = (t >> 5) & 31; b = (t >> 10) & 31;
        if (!m->raw) {
            r = r * v->r >> 7; g = g * v->g >> 7; b = b * v->b >> 7;
            if (r > 31) r = 31;
            if (g > 31) g = 31;
            if (b > 31) b = 31;
        }
    } else { r = v->r >> 3; g = v->g >> 3; b = v->b >> 3; }
    if (m->semi && m->abr == 2 && (!m->textured || (t & 0x8000))) {
        r = (back & 31) - r; g = ((back >> 5) & 31) - g; b = ((back >> 10) & 31) - b;
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
    }
    return (u16)(r | (g << 5) | (b << 10));
}

static u16 sprite_pixel(const Gp0Mode *m, const Gp0Vtx *v, int u, int vv, u16 back) {
    return sprite_pixel_texel(m, v, m->textured ? sprite_texel(m, u, vv) : 0, back);
}

static int shadow_rect_known(const Gp0Vtx *v, int w, int h) {
    shadow_start();
    int x = v->x - origin_x, y = v->y - origin_y;
    if (x < 0 || y < 0 || x + w > SHADOW_W || y + h > SHADOW_H) return 0;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) if (!sprite_known[(y + j) * SHADOW_W + x + i]) return 0;
    }
    return 1;
}

static void shadow_rect(const Gp0Mode *m, const Gp0Vtx *v, int w, int h) {
    if (gp0_state.set_mask || gp0_state.check_mask || m->semi) {
        shadow_invalidate(v->x, v->y, w, h);
        return;
    }
    shadow_start();
    int x = v->x - origin_x, y = v->y - origin_y;
    u16 palette[256];
    int ncol = m->depth == 0 ? 16 : 256;
    int shaded = m->textured && m->depth < 2 && w * h >= ncol * 2;
    u16 solid = m->textured ? 0 : sprite_pixel_texel(m, v, 0, 0);
    if (shaded) for (int i = 0; i < ncol; i++) {
        u16 t = psx_vram[(m->clut_y & 511) * 1024 + ((m->clut_x + i) & 1023)];
        palette[i] = t ? (0x8000 | sprite_pixel_texel(m, v, t, 0)) : 0;
    }
    for (int j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= SHADOW_H) continue;
        for (int i = 0; i < w; i++) {
            if (x + i < 0 || x + i >= SHADOW_W) continue;
            u16 t = m->textured ? sprite_sample(m, v->u + i, v->v + j, shaded ? palette : NULL) : 0;
            if (m->textured && !t) continue;
            int offset = (y + j) * SHADOW_W + x + i;
            sprite_shadow[offset] = shaded ? t & 0x7FFF : m->textured ? sprite_pixel_texel(m, v, t, 0) : solid;
            sprite_known[offset] = 1;
        }
    }
}

/* Deferred decoded primitives retain their drawing state and framebuffer. */
#define BUFFERED_PRIMS 4096
static struct {
    Gp0Mode mode;
    Gp0Vtx vertices[4];
    Gp0State state;
    int draw[4], kind, n, w, h;
    unsigned int rgb;
} primitive_buffer[BUFFERED_PRIMS];
static int buffered_count, buffer_executing, buffer_draw[4];

/* ---- primitives ------------------------------------------------------------------------------------- */

static int wrapped_overlap(int a, int length_a, int b, int length_b, int limit) {
    if (length_a <= 0 || length_b <= 0) return 0;
    if (length_a >= limit || length_b >= limit) return 1;
    return ((b - a) & (limit - 1)) < length_a || ((a - b) & (limit - 1)) < length_b;
}

static int buffer_depends(int x, int y, int w, int h) {
    for (int i = 0; i < buffered_count; i++) {
        const Gp0Mode *m = &primitive_buffer[i].mode;
        if (primitive_buffer[i].kind == 3 || !m->textured) continue;
        int page_w = 256 >> (m->depth == 0 ? 2 : m->depth == 1 ? 1 : 0);
        if (wrapped_overlap(x, w, m->tx, page_w, 1024)
            && wrapped_overlap(y, h, m->ty, 256, 512)) return 1;
        if (m->depth < 2 && wrapped_overlap(x, w, m->clut_x, m->depth == 0 ? 16 : 256, 1024)
            && wrapped_overlap(y, h, m->clut_y, 1, 512)) return 1;
    }
    return 0;
}

/* Small GP0 clipping areas inside a full-size DRAWENV include dialogue glyphs. */
static int drawing_to_screen(void) {
    int w, h;
    if (flushing_rect) {
        origin_x = rect_run.draw[0]; origin_y = rect_run.draw[1];
        w = rect_run.draw[2]; h = rect_run.draw[3];
    } else if (buffer_executing) {
        origin_x = buffer_draw[0]; origin_y = buffer_draw[1];
        w = buffer_draw[2]; h = buffer_draw[3];
    } else psx_gpu_draw_area(&origin_x, &origin_y, &w, &h);
    if (!w || !h) {
        origin_x = gp0_state.area_x0;
        origin_y = gp0_state.area_y0;
        w = gp0_state.area_x1 - origin_x + 1;
        h = gp0_state.area_y1 - origin_y + 1;
    }
    target_w = w; target_h = h;
    return w >= 256 && h >= 192 && gp0_state.area_x0 < origin_x + w && gp0_state.area_x1 >= origin_x
           && gp0_state.area_y0 < origin_y + h && gp0_state.area_y1 >= origin_y;
}

/* Collect a batch before allocating its regions. This avoids uploading
 * overlapping slices before discovering that one enclosing image is cheaper. */
#define PLAN_PAGES 256
#define PLAN_REGIONS 4096
static struct {
    unsigned int key, window, area;
    int x0, y0, x1, y1, w, h, count, combine;
    u16 coverage[16];
} texture_plans[PLAN_PAGES];
static u64 planned_regions[PLAN_REGIONS];
static int planning, plan_active, plan_count;

static unsigned int texture_window(void) {
    return gp0_state.twin_mask_x | (gp0_state.twin_mask_y << 8)
        | (gp0_state.twin_off_x << 16) | (gp0_state.twin_off_y << 24);
}

static void texture_bounds(const Gp0Vtx *v, int n, int w, int h,
                           int *u0, int *v0, int *width, int *height, int alignment) {
    int min_u = v[0].u, max_u = min_u, min_v = v[0].v, max_v = min_v;
    for (int i = 1; i < n; i++) {
        if (v[i].u < min_u) min_u = v[i].u;
        if (v[i].u > max_u) max_u = v[i].u;
        if (v[i].v < min_v) min_v = v[i].v;
        if (v[i].v > max_v) max_v = v[i].v;
    }
    if (w) max_u = min_u + w - 1;
    if (h) max_v = min_v + h - 1;
    *u0 = min_u & ~(alignment - 1);
    *v0 = alignment == 16 ? min_v & ~15 : min_v;
    *width = *height = 16;
    while (*width < max_u - *u0 + 1 && *width < 256) *width *= 2;
    while (*height < max_v - *v0 + 1 && *height < 256) *height *= 2;
    if (max_u > 255) { *u0 = 0; *width = 256; }
    else if (*u0 + *width > 256) {
        if (alignment == 16) { *u0 = 0; *width = 256; }
        else *u0 = 256 - *width;
    }
    if (max_v > 255) { *v0 = 0; *height = 256; }
    else if (*v0 + *height > 256) {
        if (alignment == 16) { *v0 = 0; *height = 256; }
        else *v0 = 256 - *height;
    }
}

static unsigned int plan_key(const Gp0Mode *m) {
    return tex_key(m);
}

static int find_plan(const Gp0Mode *m) {
    unsigned int key = plan_key(m), window = texture_window();
    for (int i = 0; i < plan_count; i++)
        if (texture_plans[i].key == key && texture_plans[i].window == window) return i;
    return -1;
}

static void collect_region(const Gp0Mode *m, int u, int v, int w, int h) {
    /* Framebuffer effects share small exact direct images. Enlarging those
     * regions pulls unrelated framebuffer/padding texels into the allocation. */
    if (m->depth == 2) return;
    int i = find_plan(m);
    if (i < 0) {
        if (plan_count == PLAN_PAGES) return;
        i = plan_count++;
        texture_plans[i].key = plan_key(m); texture_plans[i].window = texture_window();
        texture_plans[i].x0 = texture_plans[i].y0 = 256;
    }
    if (u < texture_plans[i].x0) texture_plans[i].x0 = u;
    if (v < texture_plans[i].y0) texture_plans[i].y0 = v;
    if (u + w > texture_plans[i].x1) texture_plans[i].x1 = u + w;
    if (v + h > texture_plans[i].y1) texture_plans[i].y1 = v + h;
    u64 key = ((u64)(i + 1) << 32) | u | ((u64)v << 8) | ((u64)(w / 16) << 16) | ((u64)(h / 16) << 21);
    unsigned int slot = cache_bucket(key, 0);
    unsigned int guard = 0;
    while (planned_regions[slot] && planned_regions[slot] != key && guard++ < PLAN_REGIONS)
        slot = (slot + 1) & (PLAN_REGIONS - 1);
    if (!planned_regions[slot]) {
        planned_regions[slot] = key;
        texture_plans[i].area += w * h; texture_plans[i].count++;
    }
    int first = u / 16, last = (u + w + 15) / 16;
    u16 mask = (u16)(((1u << (last - first)) - 1) << first);
    for (int y = v / 16; y < (v + h + 15) / 16; y++) texture_plans[i].coverage[y] |= mask;
}

static void gx_plan(int phase) {
    planning = phase == 1; plan_active = phase == 0;
    if (phase == 1) {
        memset(texture_plans, 0, sizeof texture_plans);
        memset(planned_regions, 0, sizeof planned_regions); plan_count = 0;
    } else if (phase == 0) {
        for (int i = 0; i < plan_count; i++) {
            int w = 16, h = 16, covered = 0;
            while (w < texture_plans[i].x1 - texture_plans[i].x0) w *= 2;
            while (h < texture_plans[i].y1 - texture_plans[i].y0) h *= 2;
            if (texture_plans[i].x0 + w > 256) texture_plans[i].x0 = 256 - w;
            if (texture_plans[i].y0 + h > 256) texture_plans[i].y0 = 256 - h;
            for (int y = 0; y < 16; y++) covered += __builtin_popcount(texture_plans[i].coverage[y]) * 256;
            texture_plans[i].w = w; texture_plans[i].h = h;
            texture_plans[i].combine = texture_plans[i].count > 1
                && w * h <= (int)texture_plans[i].area && w * h <= covered * 2;
        }
    }
}

static int begin_prim(const Gp0Mode *m, const Gp0Vtx *v, int n, int w, int h) {
    if (!drawing_to_screen()) {
        return 0;
    }
    int needed = (w || h) ? 4 : n == 1 ? 3 : n;
    if (polys >= MAX_POLYS || vertices + needed > MAX_VERTICES) {
        psx_gx_stats_dropped++;
        psx_gx_stats_budget_fail++;
        return 0;
    }
    if (planning && !m->textured) return 0;
    int alpha = 31, id = 0;
    if (m->semi) {
        alpha = m->abr == 3 ? 8 : 16;
        if (m->abr == 2) {
            /* Approximate B-F with a black overlay; white fade packets must darken the scene. */
            alpha = (v[0].r + v[0].g + v[0].b) * 31 / (3 * 255);
            if (!alpha) return 0; /* DS alpha zero requests wireframe rather than transparency */
        }
        id = 1 + (polys & 31); /* translucent polygons with the same ID don't blend over each other */
    }
    if (!planning) glPolyFmt(POLY_ALPHA(alpha) | POLY_ID(id) | POLY_CULL_NONE);
    if (m->textured) {
        int width, height;
        int alignment = 16;
        texture_bounds(v, n, w, h, &tex_u0, &tex_v0, &width, &height, alignment);
        if (planning) { collect_region(m, tex_u0, tex_v0, width, height); return 0; }
        int plan = plan_active ? find_plan(m) : -1;
        if (plan >= 0 && texture_plans[plan].combine) {
            tex_u0 = texture_plans[plan].x0; tex_v0 = texture_plans[plan].y0;
            width = texture_plans[plan].w; height = texture_plans[plan].h;
        }
        TexEntry *e = get_texture(m, tex_u0, tex_v0, width, height);
        if (!e && plan >= 0 && texture_plans[plan].combine) {
            /* An atlas is optional: fragmented VRAM may still fit this face's
             * smaller region. Preserve the preceding frame's pinned images. */
            texture_plans[plan].combine = 0;
            texture_bounds(v, n, w, h, &tex_u0, &tex_v0, &width, &height, alignment);
            e = get_texture(m, tex_u0, tex_v0, width, height);
        }
        if (!e) {
            psx_gx_stats_dropped++;
            psx_gx_stats_texture_fail++;
            return 0;
        }
        /* The cache may return an enclosing region, with a different UV origin. */
        tex_u0 = texture_origin_u; tex_v0 = texture_origin_v;
        /* Conversion binds textures too, and deleted names can be reused. */
        if (!bind_entry(e)) {
            psx_gx_stats_dropped++; psx_gx_stats_texture_fail++; return 0;
        }
        cur_tex = e->tex;
    } else if (cur_tex != 0) {
        glBindTexture(0, 0);
        cur_tex = 0;
    }
    polys++;
    vertices += needed;
    return 1;
}

static inline void color(const Gp0Mode *m, const Gp0Vtx *v) {
    int r, g, b;
    if (m->semi && m->abr == 2) {
        r = g = b = 0;
    } else if (m->textured && m->raw) {
        r = g = b = 31;
    } else if (m->textured) { /* PS1: 128 = x1.0; DS: 31 = x1.0 (brighter than x1.0 clamps) */
        r = v->r >> 2, g = v->g >> 2, b = v->b >> 2;
        r = r > 31 ? 31 : r, g = g > 31 ? 31 : g, b = b > 31 ? 31 : b;
    } else {
        r = v->r >> 3, g = v->g >> 3, b = v->b >> 3;
    }
    glColor3b((u8)(r << 3), (u8)(g << 3), (u8)(b << 3));
}

static inline void vertex(const Gp0Mode *m, const Gp0Vtx *v, int u, int vv) {
    if (m->textured) {
        glTexCoord2t16((t16)((u - tex_u0) * 16), (t16)((vv - tex_v0) * 16));
    }
    glVertex3v16((v16)(v->x - origin_x), (v16)(v->y - origin_y), (v16)z);
}

static int triangle_area(const Gp0Vtx *a, const Gp0Vtx *b, const Gp0Vtx *c) {
    return (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
}

static void gx_poly_unclipped(const Gp0Mode *m, const Gp0Vtx *v, int n) {
    /* The PSX draws no pixels for zero-area triangles. Avoid spending GX
     * vertices, texture allocations, or shadow invalidation on those faces. */
    if (!triangle_area(v, v + 1, v + 2)
        && (n == 3 || !triangle_area(v + 1, v + 2, v + 3))) return;
    if (!begin_prim(m, v, n, 0, 0)) {
        return;
    }
#ifdef PORT_GX_EXPERIMENTAL
    shadow_start();
    psx_softgpu_shadow_poly(m, v, n, sprite_shadow, sprite_known,
                           origin_x, origin_y, SHADOW_W, SHADOW_H);
#else
    int x0 = v[0].x, y0 = v[0].y, x1 = x0, y1 = y0;
    for (int i = 1; i < n; i++) {
        if (v[i].x < x0) x0 = v[i].x;
        if (v[i].x > x1) x1 = v[i].x;
        if (v[i].y < y0) y0 = v[i].y;
        if (v[i].y > y1) y1 = v[i].y;
    }
    shadow_invalidate(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
#endif
    z++;
    if (n == 4) {
        glBegin(GL_QUADS);
        /* PS1 quads are (0,1,2) + (1,2,3): as a DS quad the order is 0,1,3,2 */
        static const int order[4] = {0, 1, 3, 2};
        for (int i = 0; i < 4; i++) {
            const Gp0Vtx *p = &v[order[i]];
            if (i == 0 || m->gouraud) {
                color(m, p);
            }
            vertex(m, p, p->u, p->v);
        }
    } else {
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 3; i++) {
            if (i == 0 || m->gouraud) {
                color(m, &v[i]);
            }
            vertex(m, &v[i], v[i].u, v[i].v);
        }
    }
    glEnd();
}

/* Clip screen-space triangles before GX submission: changing DR_AREA must not move the framebuffer origin. */
static int clip_inside(const Gp0Vtx *v, int edge, int bound) {
    int value = edge < 2 ? v->x : v->y;
    return (edge & 1) ? value <= bound : value >= bound;
}

static Gp0Vtx clip_intersection(const Gp0Vtx *a, const Gp0Vtx *b, int edge, int bound) {
    int av = edge < 2 ? a->x : a->y;
    int bv = edge < 2 ? b->x : b->y;
    Gp0Vtx p;
#define INTERPOLATE(member) p.member = a->member + (int)((long long)(b->member - a->member) * (bound - av) / (bv - av))
    INTERPOLATE(x); INTERPOLATE(y);
    INTERPOLATE(r); INTERPOLATE(g); INTERPOLATE(b);
    INTERPOLATE(u); INTERPOLATE(v);
#undef INTERPOLATE
    if (edge < 2) p.x = bound;
    else p.y = bound;
    return p;
}

static void gx_clipped_triangle(const Gp0Mode *m, const Gp0Vtx *v) {
    Gp0Vtx scratch[2][8];
    memcpy(scratch[0], v, 3 * sizeof(*v));
    int count = 3, current = 0;
    int bounds[4] = {gp0_state.area_x0, gp0_state.area_x1 + 1, gp0_state.area_y0, gp0_state.area_y1 + 1};
    for (int edge = 0; edge < 4 && count; edge++) {
        int out = 0;
        const Gp0Vtx *a = &scratch[current][count - 1];
        int a_in = clip_inside(a, edge, bounds[edge]);
        for (int i = 0; i < count; i++) {
            const Gp0Vtx *b = &scratch[current][i];
            int b_in = clip_inside(b, edge, bounds[edge]);
            if (a_in != b_in) scratch[current ^ 1][out++] = clip_intersection(a, b, edge, bounds[edge]);
            if (b_in) scratch[current ^ 1][out++] = *b;
            a = b; a_in = b_in;
        }
        count = out; current ^= 1;
    }
    for (int i = 1; i + 1 < count; i++) {
        Gp0Vtx triangle[3] = {scratch[current][0], scratch[current][i], scratch[current][i + 1]};
        gx_poly_unclipped(m, triangle, 3);
    }
}

static void gx_poly_now(const Gp0Mode *m, const Gp0Vtx *v, int n) {
    flush_rect();
    int clipped = 0;
    for (int i = 0; i < n; i++) {
        if (v[i].x < gp0_state.area_x0 || v[i].x > gp0_state.area_x1 + 1
            || v[i].y < gp0_state.area_y0 || v[i].y > gp0_state.area_y1 + 1) clipped = 1;
    }
    if (!clipped) { gx_poly_unclipped(m, v, n); return; }
    Gp0Vtx triangle[3] = {v[0], v[1], v[2]};
    gx_clipped_triangle(m, triangle);
    if (n == 4) {
        triangle[0] = v[1]; triangle[1] = v[2]; triangle[2] = v[3];
        gx_clipped_triangle(m, triangle);
    }
}

static void gx_line_now(const Gp0Mode *m, const Gp0Vtx *a, const Gp0Vtx *b) {
    flush_rect();
    if (!begin_prim(m, a, 1, 0, 0)) {
        return;
    }
    int x = a->x < b->x ? a->x : b->x, y = a->y < b->y ? a->y : b->y;
    shadow_invalidate(x, y, abs(b->x - a->x) + 1, abs(b->y - a->y) + 1);
    z++;
    /* a triangle with two equal vertices is drawn by the DS as a line */
    glBegin(GL_TRIANGLES);
    color(m, a);
    vertex(m, a, 0, 0);
    if (m->gouraud) {
        color(m, b);
    }
    vertex(m, b, 0, 0);
    vertex(m, b, 0, 0);
    glEnd();
}

static void gx_rect_now(const Gp0Mode *m, const Gp0Vtx *v, int w, int h) {
    if (w <= 0 || h <= 0) return;
    Gp0Vtx clipped = *v;
    if (clipped.x < gp0_state.area_x0) {
        int cut = gp0_state.area_x0 - clipped.x;
        clipped.x += cut; clipped.u += cut; w -= cut;
    }
    if (clipped.y < gp0_state.area_y0) {
        int cut = gp0_state.area_y0 - clipped.y;
        clipped.y += cut; clipped.v += cut; h -= cut;
    }
    if (clipped.x + w > gp0_state.area_x1 + 1) w = gp0_state.area_x1 + 1 - clipped.x;
    if (clipped.y + h > gp0_state.area_y1 + 1) h = gp0_state.area_y1 + 1 - clipped.y;
    if (w <= 0 || h <= 0) return;
    v = &clipped;
    if (!drawing_to_screen()) return;
    if (planning) { begin_prim(m, v, 1, w, h); return; }
    Gp0Mode composite_mode;
    int composite = 0;
    if (m->semi && m->abr == 2 && !gp0_state.set_mask && !gp0_state.check_mask
        && w <= 256 && h <= 256 && shadow_rect_known(v, w, h)
        && polys < MAX_POLYS && vertices + 4 <= MAX_VERTICES) {
        int tw = 16, th = 16;
        while (tw < w) tw *= 2;
        while (th < h) th *= 2;
        memset(convbuf16, 0, tw * th * sizeof(u16));
        int x = v->x - origin_x, y = v->y - origin_y;
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
            convbuf16[j * tw + i] = 0x8000 | sprite_pixel(m, v, v->u + i, v->v + j,
                                                        sprite_shadow[(y + j) * SHADOW_W + x + i]);
        }
        TexEntry *e = get_composite(tw, th);
        if (e) {
            Gp0Mode plain = {0};
            begin_prim(&plain, v, 1, w, h);
            glBindTexture(0, e->tex); cur_tex = e->tex;
            for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
                sprite_shadow[(y + j) * SHADOW_W + x + i] = convbuf16[j * tw + i] & 0x7FFF;
            composite_mode = (Gp0Mode){.textured = 1, .raw = 1};
            m = &composite_mode;
            clipped.u = clipped.v = tex_u0 = tex_v0 = 0;
            composite = 1;
            psx_gx_stats_composited++;
        }
    }
    if (!composite) {
        if (!begin_prim(m, v, 1, w, h)) return;
        shadow_rect(m, v, w, h);
        if (m->semi && m->abr == 2) psx_gx_stats_blend_fallback++;
    }
    z++;
    Gp0Vtx c[4] = {*v, *v, *v, *v};
    c[1].x += w;
    c[2].x += w, c[2].y += h;
    c[3].y += h;
    glBegin(GL_QUADS);
    color(m, v);
    vertex(m, &c[0], v->u, v->v);
    vertex(m, &c[1], v->u + w, v->v);
    vertex(m, &c[2], v->u + w, v->v + h);
    vertex(m, &c[3], v->u, v->v + h);
    glEnd();
}

static void flush_rect(void) {
    if (!rect_run.active) return;
    Gp0State saved = gp0_state;
    gp0_state = rect_run.state;
    flushing_rect = 1;
    gx_rect_now(&rect_run.mode, &rect_run.vertex, rect_run.w, rect_run.h);
    flushing_rect = 0;
    gp0_state = saved;
    rect_run.active = 0;
}

static void gx_rect_immediate(const Gp0Mode *m, const Gp0Vtx *v, int w, int h) {
#if (!defined(PORT_GX_EXPERIMENTAL) && !defined(PORT_GX_MERGE)) || defined(PORT_GX_NO_MERGE)
    gx_rect_now(m, v, w, h);
    return;
#endif
    if (m->semi || !m->textured || gp0_state.set_mask || gp0_state.check_mask
        || w <= 0 || h <= 0 || w > 256 || h > 256) {
        flush_rect(); gx_rect_now(m, v, w, h); return;
    }
    int draw[4];
    if (buffer_executing) memcpy(draw, buffer_draw, sizeof draw);
    else psx_gpu_draw_area(&draw[0], &draw[1], &draw[2], &draw[3]);
    if (rect_run.active && !gx_memdiff(&rect_run.mode, m, sizeof *m)
        && !gx_memdiff(&rect_run.state, &gp0_state, sizeof gp0_state)
        && !gx_memdiff(rect_run.draw, draw, sizeof draw)
        && rect_run.vertex.r == v->r && rect_run.vertex.g == v->g && rect_run.vertex.b == v->b) {
        const Gp0Vtx *a = &rect_run.vertex;
        if (v->y == a->y && v->v == a->v && h == rect_run.h
            && v->x == a->x + rect_run.w && v->u == a->u + rect_run.w && rect_run.w + w <= 256) {
            rect_run.w += w; return;
        }
        if (v->x == a->x && v->u == a->u && w == rect_run.w
            && v->y == a->y + rect_run.h && v->v == a->v + rect_run.h && rect_run.h + h <= 256) {
            rect_run.h += h; return;
        }
    }
    flush_rect();
    rect_run.active = 1; rect_run.mode = *m; rect_run.vertex = *v;
    rect_run.w = w; rect_run.h = h; rect_run.state = gp0_state;
    memcpy(rect_run.draw, draw, sizeof draw);
}

/* A fill into the frame buffer being drawn (absolute VRAM coordinates, like the drawing area): a full-screen one
 * sets the clear colour, others become quads. */
static void gx_fill_now(int x, int y, int w, int h, unsigned int rgb) {
    if (planning) return;
    flush_rect();
    if (!drawing_to_screen()) {
        return;
    }
    if (x >= origin_x + target_w || y >= origin_y + target_h || x + w <= origin_x || y + h <= origin_y) {
        return;
    }
    if (x <= origin_x && y <= origin_y && x + w >= origin_x + target_w && y + h >= origin_y + target_h) {
        glClearColor((u8)((rgb & 0xFF) >> 3), (u8)(((rgb >> 8) & 0xFF) >> 3), (u8)(((rgb >> 16) & 0xFF) >> 3), 31);
        clear_rgb = (u16)(((rgb & 255) >> 3) | (((rgb >> 8) & 255) >> 3) << 5
                          | (((rgb >> 16) & 255) >> 3) << 10);
        shadow_ready = 0;
        shadow_start();
        clear_pending = 1;
        return;
    }
    Gp0Mode m = {0};
    Gp0Vtx v = {x, y, (int)(rgb & 0xFF), (int)((rgb >> 8) & 0xFF), (int)((rgb >> 16) & 0xFF), 0, 0};
    /* GP0 fill ignores the drawing area's clipping rectangle. */
    Gp0State saved = gp0_state;
    gp0_state.area_x0 = origin_x; gp0_state.area_y0 = origin_y;
    gp0_state.area_x1 = origin_x + target_w - 1; gp0_state.area_y1 = origin_y + target_h - 1;
    gx_rect_immediate(&m, &v, w, h);
    gp0_state = saved;
}

static void replay_primitive(int i) {
    gp0_state = primitive_buffer[i].state;
    memcpy(buffer_draw, primitive_buffer[i].draw, sizeof buffer_draw);
    const Gp0Mode *m = &primitive_buffer[i].mode;
    const Gp0Vtx *v = primitive_buffer[i].vertices;
    switch (primitive_buffer[i].kind) {
    case 0: gx_poly_now(m, v, primitive_buffer[i].n); break;
    case 1: gx_line_now(m, v, v + 1); break;
    case 2: gx_rect_immediate(m, v, primitive_buffer[i].w, primitive_buffer[i].h); break;
    case 3: gx_fill_now(v->x, v->y, primitive_buffer[i].w, primitive_buffer[i].h, primitive_buffer[i].rgb); break;
    }
}

static void flush_buffer(void) {
    if (!buffered_count || buffer_executing) return;
    Gp0State saved = gp0_state;
    flush_rect();
    buffer_executing = 1;
    gx_plan(1);
    for (int i = 0; i < buffered_count; i++) replay_primitive(i);
    flush_rect();
    gx_plan(0);
    for (int i = 0; i < buffered_count; i++) replay_primitive(i);
    flush_rect();
    gx_plan(-1);
    gp0_state = saved;
    buffered_count = buffer_executing = 0;
}

static void queue_primitive(int kind, const Gp0Mode *m, const Gp0Vtx *v, int n, int w, int h, unsigned int rgb) {
    if (buffered_count == BUFFERED_PRIMS) flush_buffer();
    int i = buffered_count++;
    primitive_buffer[i].kind = kind; primitive_buffer[i].n = n;
    primitive_buffer[i].w = w; primitive_buffer[i].h = h; primitive_buffer[i].rgb = rgb;
    if (m) primitive_buffer[i].mode = *m;
    memcpy(primitive_buffer[i].vertices, v, n * sizeof(*v));
    primitive_buffer[i].state = gp0_state;
    psx_gpu_draw_area(&primitive_buffer[i].draw[0], &primitive_buffer[i].draw[1],
                      &primitive_buffer[i].draw[2], &primitive_buffer[i].draw[3]);
}

static void gx_poly(const Gp0Mode *m, const Gp0Vtx *v, int n) {
#ifdef PORT_GX_PLAN
    queue_primitive(0, m, v, n, 0, 0, 0);
#else
    gx_poly_now(m, v, n);
#endif
}
static void gx_line(const Gp0Mode *m, const Gp0Vtx *a, const Gp0Vtx *b) {
#ifdef PORT_GX_PLAN
    Gp0Vtx v[2] = {*a, *b}; queue_primitive(1, m, v, 2, 0, 0, 0);
#else
    gx_line_now(m, a, b);
#endif
}
static void gx_rect(const Gp0Mode *m, const Gp0Vtx *v, int w, int h) {
#ifdef PORT_GX_PLAN
    queue_primitive(2, m, v, 1, w, h, 0);
#else
    gx_rect_immediate(m, v, w, h);
#endif
}
static void gx_fill(int x, int y, int w, int h, unsigned int rgb) {
#ifdef PORT_GX_PLAN
    Gp0Vtx v = {.x = x, .y = y}; queue_primitive(3, NULL, &v, 1, w, h, rgb);
#else
    gx_fill_now(x, y, w, h, rgb);
#endif
}

static void gx_frame(void) {
    flush_buffer();
    flush_rect();
    if (polys || clear_pending) psx_gx_stats_polys = polys;
    /* VBlank waits with no new geometry must retain the previous display list. */
    if (polys || clear_pending) {
        glFlush(GL_TRANS_MANUALSORT);
        frame++;
    }
    z = 0;
    polys = 0;
    vertices = 0;
    clear_pending = 0;
    cur_tex = -1;
    shadow_ready = 0;
}

static const Gp0Backend gx_backend = {gx_poly, gx_line, gx_rect, gx_fill, gx_frame};

void render_gx_init(void) {
    videoSetMode(MODE_0_3D);
    glInit();
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_TEXTURE);
    vramSetBankC(VRAM_C_TEXTURE);
    vramSetBankD(VRAM_D_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
#ifdef PORT_GX_EXPERIMENTAL
    vramSetBankF(VRAM_F_TEX_PALETTE_SLOT4);
    vramSetBankG(VRAM_G_TEX_PALETTE_SLOT5);
#endif
    glEnable(GL_TEXTURE_2D | GL_BLEND);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 6, 255, 6 + 179 - 1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* v16 units: 1 = one PS1 pixel; 320x224 PS1 pixels fill the viewport. Later primitives get larger z. */
    glOrthof32(0, 320, 224, 0, -4096, 4096);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gp0_backend = &gx_backend;
}

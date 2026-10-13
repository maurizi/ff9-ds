/* Exercise the production conversion/cache code with a recording libnds mock. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nds.h"
#include "gp0.h"

static u16 vram[1024 * 512];
u16 *psx_vram = vram;
Gp0State gp0_state;
const Gp0Backend *gp0_backend;
static int next_name, uploads, deletes, flushes, palette_uploads, palette_shares;
static int reject_upload, reject_palette, max_upload_bytes;
static u8 captured[256 * 256 * 2];
static u16 captured_palette[256];
static u16 named_palettes[16384][256];
static u8 *named_pixels[16384];
static size_t named_bytes[16384];
static GL_TEXTURE_TYPE_ENUM named_types[16384];
static int named_params[16384];
static int bound_name, captured_params;
static GL_TEXTURE_TYPE_ENUM captured_type;
static u16 captured_pixel(int i) {
    if (captured_type == GL_RGBA) return ((u16 *)captured)[i];
    int index = captured_type == GL_RGB16 ? (captured[i/2] >> ((i&1)*4)) & 15 : captured[i];
    return (captured_params & GL_TEXTURE_COLOR0_TRANSPARENT) && !index ? 0 : captured_palette[index] | 0x8000;
}
static int draw_x, draw_y, draw_w, draw_h;
static int emitted, min_x, max_x, min_y, max_y;
void glVertex3v16(v16 x, v16 y, v16 z) {
    (void)z;
    if (!emitted || x < min_x) min_x = x;
    if (!emitted || x > max_x) max_x = x;
    if (!emitted || y < min_y) min_y = y;
    if (!emitted || y > max_y) max_y = y;
    emitted++;
}
void psx_gpu_draw_area(int *x, int *y, int *w, int *h) {
    *x = draw_x; *y = draw_y; *w = draw_w; *h = draw_h;
}

int glGenTextures(int n, int *names) { assert(n == 1); *names = ++next_name; return 1; }
void glBindTexture(int target, int name) {
    (void)target; bound_name = name;
    if (named_pixels[name]) {
        memcpy(captured, named_pixels[name], named_bytes[name]);
        captured_type = named_types[name]; captured_params = named_params[name];
    }
    memcpy(captured_palette, named_palettes[name], sizeof captured_palette);
}
void glDeleteTextures(int n, const int *names) {
    for (int i = 0; i < n; i++) {
        free(named_pixels[names[i]]); named_pixels[names[i]] = NULL;
        memset(named_palettes[names[i]], 0, sizeof named_palettes[names[i]]);
    }
    deletes += n;
}
int glTexImage2D(int target, int level, GL_TEXTURE_TYPE_ENUM type, int w, int h, int border, int params, const void *data) {
    (void)target; (void)level; (void)border; (void)params;
    if (reject_upload) return 0;
    captured_type=type; captured_params=params;
    size_t bytes = w * h * (type == GL_RGBA ? 2 : 1) / (type == GL_RGB16 ? 2 : 1);
    if (max_upload_bytes && bytes > (size_t)max_upload_bytes) return 0;
    memcpy(captured, data, bytes);
    named_pixels[bound_name] = malloc(bytes); assert(named_pixels[bound_name]);
    memcpy(named_pixels[bound_name], data, bytes); named_bytes[bound_name] = bytes;
    named_types[bound_name] = type; named_params[bound_name] = params;
    uploads++;
    return 1;
}
int glColorTableEXT(int target, int unused, size_t count, int a, int b, const void *palette) {
    (void)target; (void)unused; (void)a; (void)b;
    if (reject_palette) return 0;
    memcpy(captured_palette, palette, count * 2);
    memcpy(named_palettes[bound_name],palette,count*2);
    palette_uploads++;
    return 1;
}
int glAssignColorTable(int target, int name) {
    (void)target;
    memcpy(captured_palette,named_palettes[name],sizeof captured_palette);
    memcpy(named_palettes[bound_name],named_palettes[name],sizeof captured_palette);
    palette_shares++; return 1;
}
void glFlush(int mode) { (void)mode; flushes++; }

#include "../../port/ds/render_gx.c"

static void reset(void) {
    for (int i = 0; i < CACHE; i++) free_entry(&cache[i]);
    memset(cache, 0, sizeof cache);
    memset(cache_heads, 0, sizeof cache_heads);
    memset(page_heads, 0, sizeof page_heads);
    memset(content_heads, 0, sizeof content_heads);
    memset(images, 0, sizeof images);
    memset(image_heads, 0, sizeof image_heads);
    memset(palette_heads, 0, sizeof palette_heads);
    memset(block_gen, 0, sizeof block_gen);
    memset(palette_row_gen, 0, sizeof palette_row_gen);
    memset(vram, 0, sizeof vram);
    memset(&gp0_state, 0, sizeof gp0_state);
    frame = 2; stamp = 0; gen_counter = 1; polys = vertices = 0;
    clear_pending = 0; shadow_ready = 0; clear_rgb = 0;
    rect_run.active = flushing_rect = 0;
    planning = plan_active = plan_count = buffered_count = buffer_executing = 0;
    draw_x = draw_y = draw_w = draw_h = 0;
    emitted = 0;
    next_name = 0;
    uploads = deletes = flushes = palette_uploads = palette_shares = reject_upload = reject_palette = 0;
    max_upload_bytes = 0;
}

int main(void) {
    reset();
    draw_w = 320; draw_h = 224;
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Mode atlas_mode = {.textured = 1, .depth = 1, .tx = 576, .ty = 256,
                          .clut_x = 256, .clut_y = 236};
    Gp0Vtx atlas_face[4] = {{.x = 1, .y = 1, .u = 95, .v = 249},
                          {.x = 11, .y = 1, .u = 86, .v = 251},
                          {.x = 1, .y = 11, .u = 82, .v = 243},
                          {.x = 11, .y = 11, .u = 73, .v = 245}};
    plan_active = plan_count = 1;
    texture_plans[0].key = plan_key(&atlas_mode);
    texture_plans[0].window = texture_window();
    texture_plans[0].combine = 1;
    texture_plans[0].x0 = 0; texture_plans[0].y0 = 128;
    texture_plans[0].w = 256; texture_plans[0].h = 128;
    max_upload_bytes = 1024; /* Atlas fails; the 32x16 face region fits. */
    int failures_before = psx_gx_stats_texture_fail;
    assert(begin_prim(&atlas_mode, atlas_face, 4, 0, 0));
    assert(psx_gx_stats_texture_fail == failures_before && uploads == 1);
    assert(!texture_plans[0].combine);
    assert(tex_u0 == 64 && tex_v0 == 240 && named_bytes[bound_name] == 512);
    reset();
    u8 compare_a[520], compare_b[520];
    for (int i = 0; i < 520; i++) compare_a[i] = compare_b[i] = (u8)(i * 13);
    for (int offset_a = 0; offset_a < 8; offset_a++) for (int offset_b = 0; offset_b < 8; offset_b++) {
        memcpy(compare_b + offset_b, compare_a + offset_a, 500);
        for (int size = 0; size <= 500; size++)
            assert(!gx_memdiff(compare_a + offset_a, compare_b + offset_b, size));
        for (int i = 0; i < 500; i++) {
            compare_b[offset_b + i] ^= 1;
            assert(gx_memdiff(compare_a + offset_a, compare_b + offset_b, 500));
            compare_b[offset_b + i] ^= 1;
        }
    }
    reset();
    /* Opaque palette shading must preserve holes and opaque black, and
     * match PSX modulation before the fast per-pixel lookup. */
    for (int depth = 0; depth < 2; depth++) {
        reset();
        int colours = depth ? 256 : 16;
        int width = depth ? 32 : 8, height = 16;
        Gp0Mode opaque = {.textured = 1, .depth = depth, .tx = 512, .clut_y = 300};
        Gp0Vtx shade = {.x = 0, .y = 0, .r = 64, .g = 192, .b = 255};
        shadow_start();
        for (int i = 0; i < colours; i++)
            vram[300 * 1024 + i] = i == 0 ? 0 : i == 1 ? 0x8000 : (i * 127) & 0x7fff;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            int index = (y * width + x) % colours;
            int shift = depth ? 8 * (x & 1) : 4 * (x & 3);
            int at = y * 1024 + 512 + (x >> (depth ? 1 : 2));
            vram[at] = (vram[at] & ~((depth ? 255 : 15) << shift)) | (index << shift);
            sprite_shadow[y * SHADOW_W + x] = 0x1234;
        }
        shadow_rect(&opaque, &shade, width, height);
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            u16 t = vram[300 * 1024 + (y * width + x) % colours];
            int r = (t & 31) * 64 >> 7;
            int g = ((t >> 5) & 31) * 192 >> 7;
            int b = ((t >> 10) & 31) * 255 >> 7;
            if (g > 31) g = 31;
            if (b > 31) b = 31;
            assert(sprite_shadow[y * SHADOW_W + x] == (t ? r | (g << 5) | (b << 10) : 0x1234));
        }
    }
    reset();
    psx_vram_touch(1008, 500, 32, 24);
    assert(block_gen[63][15] && block_gen[63][0] && block_gen[0][15] && block_gen[0][0]);
    assert(!block_gen[1][1]);

    reset();
    Gp0Mode separated = {.depth = 0, .tx = 512, .clut_y = 300};
    psx_vram_touch(512, 192, 16, 16);
    psx_vram_touch(0, 300, 16, 1);
    unsigned stable_generation = source_gen(&separated, 0, 192, 16, 16);
    psx_vram_touch(512, 224, 16, 1); /* Same old 64-row block, different texels. */
    psx_vram_touch(0, 301, 16, 1); /* Adjacent CLUT row is independent. */
    assert(source_gen(&separated, 0, 192, 16, 16) == stable_generation);
    psx_vram_touch(15, 300, 1, 1);
    assert(source_gen(&separated, 0, 192, 16, 16) > stable_generation);
    reset();

    Gp0Mode m = {.depth = 1, .tx = 960, .ty = 256, .clut_x = 48, .clut_y = 10};
    unsigned before = source_gen(&m, 0, 0, 256, 256);
    psx_vram_touch(290, 10, 1, 1); /* final block of an unaligned 256-colour CLUT */
    assert(source_gen(&m, 0, 0, 256, 256) > before);

    reset();
    m = (Gp0Mode){.depth = 0, .tx = 960, .ty = 256, .clut_x = 1008, .clut_y = 10};
    for (int i = 0; i < 16; i++) vram[10 * 1024 + ((1008 + i) & 1023)] = i + 1;
    vram[(256 + 16) * 1024 + 964] = 0x3210;
    TexEntry *first = get_texture(&m, 16, 16, 16, 16);
    assert(first && captured[0] == 0x10 && captured[1] == 0x32);
    assert(get_texture(&m, 16, 16, 16, 16) == first && uploads == 1);
    psx_vram_touch(970, 400, 1, 1); /* another part of this page is outside the cached region */
    assert(get_texture(&m, 16, 16, 16, 16) == first && uploads == 1);
    assert(get_texture(&m, 32, 16, 16, 16));
    assert(palette_uploads == 1 && palette_shares == 1);
    psx_vram_touch(964, 272, 1, 1);
    assert(get_texture(&m, 16, 16, 16, 16) == first); /* unchanged bytes must not allocate again */
    vram[272 * 1024 + 964] = 0x5432;
    psx_vram_touch(964, 272, 1, 1);
    assert(get_texture(&m, 16, 16, 16, 16) != first);
    assert(deletes == 0); /* earlier geometry still references this allocation */

    reset();
    m = (Gp0Mode){.textured = 1, .depth = 0, .tx = 960, .ty = 256};
    for (int i = 1; i < 16; i++) vram[i] = (u16)i;
    TexEntry *parent = get_texture(&m, 16, 16, 32, 32);
    assert(parent && get_texture(&m, 32, 32, 16, 16) == parent && uploads == 1);
    draw_w = 320; draw_h = 224;
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Vtx child = {.x = 20, .y = 20, .u = 32, .v = 32};
    assert(begin_prim(&m, &child, 1, 4, 4));
    assert(tex_u0 == 16 && tex_v0 == 16 && cur_tex == parent->tex);
    psx_vram_touch(968, 280, 1, 1);
    assert(get_texture(&m, 32, 32, 16, 16) == parent); /* unrelated dirty bytes */
    vram[280 * 1024 + 968] = 0x1111; /* changed parent, outside the child */
    psx_vram_touch(968, 280, 1, 1);
    assert(get_texture(&m, 32, 32, 16, 16) == parent);
    assert(get_texture(&m, 16, 16, 32, 32) != parent); /* parent must stay dirty */
    vram[288 * 1024 + 968] = 0x2222;
    psx_vram_touch(968, 288, 1, 1);
    assert(get_texture(&m, 32, 32, 16, 16) != parent); /* changed child cannot reuse old pixels */

    reset();
    m = (Gp0Mode){.depth = 2, .tx = 960, .ty = 256};
    vram[272 * 1024] = 0x001F;
    vram[272 * 1024 + 15] = 0x83E0;
    assert(get_texture(&m, 64, 16, 16, 16));
    assert(((u16 *)captured)[0] == 0x801F && ((u16 *)captured)[15] == 0x83E0);
    gp0_state.twin_mask_x = 8; gp0_state.twin_off_x = 8;
    vram[272 * 1024 + 8] = 0x7C00;
    assert(get_texture(&m, 64, 16, 16, 16));
    assert(((u16 *)captured)[0] == 0xFC00 && uploads == 2);

    reset();
    m = (Gp0Mode){.textured = 1, .raw = 1, .depth = 2};
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        vram[y * 1024 + x] = (u16)(1 + x + y * 16);
        vram[(64 + y) * 1024 + 96 + x] = (u16)(1 + x + y * 16);
    }
    TexEntry *shared = get_texture(&m, 0, 0, 16, 16);
    m.tx = 64;
    draw_w = 320; draw_h = 224; gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Vtx alias = {.x = 10, .y = 10, .u = 32, .v = 64};
    assert(begin_prim(&m, &alias, 1, 16, 16));
    assert(cur_tex == shared->tex && uploads == 1 && tex_u0 == 32 && tex_v0 == 64);
    vram[64 * 1024 + 96] = 0x7FFF;
    psx_vram_touch(96, 64, 1, 1);
    assert(get_texture(&m, 32, 64, 16, 16) != shared && uploads == 2 && deletes == 0);
    m.tx = 0;
    assert(get_texture(&m, 0, 0, 16, 16) == shared && uploads == 2);

    reset();
    m = (Gp0Mode){.textured = 1, .raw = 1, .depth = 1, .clut_y = 255};
    draw_w = 320; draw_h = 224; gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Vtx planned = {.x = 10, .y = 10};
    gx_plan(1);
    gx_rect_now(&m, &planned, 16, 16);
    planned.u = 16; gx_rect_now(&m, &planned, 16, 16);
    assert(uploads == 0 && polys == 0 && !shadow_ready);
    gx_plan(0);
    planned.u = 0; gx_rect_now(&m, &planned, 16, 16);
    planned.u = 16; gx_rect_now(&m, &planned, 16, 16);
    assert(uploads == 1 && polys == 2 && tex_u0 == 0);
    gx_plan(-1);
    assert(!planning && !plan_active);

    reset();
    draw_w = 320; draw_h = 224;
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Mode red_plan = {.textured = 1, .raw = 1, .depth = 0, .tx = 512,
                        .ty = 256, .clut_y = 120};
    Gp0Mode blue_plan = red_plan; blue_plan.clut_y = 121;
    vram[120 * 1024 + 1] = 0x001F;
    vram[121 * 1024 + 1] = 0x7C00;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 8; x++)
        vram[(256 + y) * 1024 + 512 + x] = 0x1111;
    planned = (Gp0Vtx){.x = 10, .y = 10};
    gx_plan(1);
    gx_rect_now(&red_plan, &planned, 16, 16);
    planned.u = 16; gx_rect_now(&blue_plan, &planned, 16, 16);
    gx_plan(0);
    planned.u = 0; gx_rect_now(&red_plan, &planned, 16, 16);
    assert(captured_pixel(0) == 0x801F);
    planned.u = 16; gx_rect_now(&blue_plan, &planned, 16, 16);
    assert(captured_pixel(0) == 0xFC00);
    assert(plan_count == 2);
#ifdef PORT_GX_SHARE_INDEXED
    assert(uploads == 1);
#else
    assert(uploads == 2);
#endif
    gx_plan(-1);

    reset();
    m = (Gp0Mode){.textured = 1, .raw = 1, .depth = 1, .clut_y = 255};
    draw_w = 320; draw_h = 224; gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    planned = (Gp0Vtx){.x = 10, .y = 10};
    for (int i = 1; i <= 64; i++) vram[255 * 1024 + i] = i;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 32; x++)
        vram[y * 1024 + x] = (1 + x * 2) | ((2 + x * 2) << 8);
    queue_primitive(2, &m, &planned, 1, 32, 16, 0);
    queue_primitive(2, &m, &planned, 1, 64, 16, 0);
    assert(uploads == 0 && polys == 0);
    flush_buffer();
    assert(uploads == 1 && polys == 2 && !buffered_count && max_x == 74);
    assert(captured_pixel(63) == (0x8000 | 64));
    queue_primitive(2, &m, &planned, 1, 32, 16, 0);
    psx_vram_touch(800, 400, 1, 1); /* unrelated writes can retain the full batch */
    assert(buffered_count == 1);
    psx_vram_touch(0, 0, 1, 1); /* flush before the source changes */
    assert(!buffered_count && captured_pixel(0) == 0x8001);
    vram[0] = 0x0240;
    queue_primitive(2, &m, &planned, 1, 32, 16, 0);
    flush_buffer();
    assert(uploads == 2 && captured_pixel(0) == 0x8040 && deletes == 0);

    reset();
    m = (Gp0Mode){.textured = 1, .raw = 1, .depth = 1, .clut_y = 10};
    for (int y = 0; y < 16; y++) for (int x = 0; x < 8; x++) vram[y * 1024 + x] = 0x0101;
    vram[10 * 1024 + 1] = 0x001F; vram[11 * 1024 + 1] = 0x7C00;
    TexEntry *red = get_texture(&m, 0, 0, 16, 16);
    m.clut_y = 11;
    TexEntry *blue = get_texture(&m, 0, 0, 16, 16);
    assert(red && blue && red != blue && red->palette[1] != blue->palette[1]);
#ifdef PORT_GX_SHARE_INDEXED
    assert(uploads == 1 && red->image == blue->image && red->pixels == blue->pixels);
#else
    assert(uploads == 2 && !red->image && !blue->image);
#endif
    assert(bind_entry(red) && captured_pixel(0) == 0x801F);
    assert(bind_entry(blue) && captured_pixel(0) == 0xFC00);
    int shared_image = blue->image;
    free_entry(red);
    if (shared_image) assert(images[shared_image - 1].used && images[shared_image - 1].refs == 1);
    assert(bind_entry(blue) && captured_pixel(0) == 0xFC00);
    free_entry(blue);
    if (shared_image) assert(!images[shared_image - 1].used && deletes == 3);
    else assert(deletes == 2);

    reset();
    draw_w = 320; draw_h = 224; gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Vtx invisible[4] = {{10,10}, {20,20}, {30,30}, {40,40}};
    m = (Gp0Mode){.textured = 1, .depth = 2};
    gx_poly_now(&m, invisible, 3); gx_poly_now(&m, invisible, 4);
    assert(!polys && !vertices && !uploads && !shadow_ready);
    assert(wrapped_overlap(1008, 32, 0, 8, 1024));
    assert(!wrapped_overlap(1008, 16, 0, 8, 1024));
    assert(wrapped_overlap(500, 24, 8, 16, 512));

    reset();
    m = (Gp0Mode){.depth = 1, .clut_y = 10};
    for (int i = 0; i < 256; i++) vram[10 * 1024 + i] = i + 1;
    vram[10 * 1024] = 0x001F;
    vram[10 * 1024 + 7] = 0; /* transparent index is not zero */
    vram[0] = 0x0700;
    assert(get_texture(&m, 0, 0, 16, 16));
    assert(captured[0] == 7 && captured[1] == 0 && captured_palette[7] == 0x001F);

    reset();
    reject_upload = 1;
    assert(!get_texture(&m, 0, 0, 16, 16) && deletes == 1);
    gx_frame();
    assert(flushes == 0);
    polys = 1; gx_frame();
    assert(flushes == 1);

    reset();
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Mode plain = {0};
    Gp0Vtx corners[4] = {{0}};
    vertices = MAX_VERTICES - 3;
    assert(!begin_prim(&plain, corners, 4, 0, 0));
    assert(begin_prim(&plain, corners, 3, 0, 0));
    assert(vertices == MAX_VERTICES && polys == 1);

    reset();
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    gx_fill(0, 0, 320, 224, 0);
    gx_frame();
    assert(flushes == 1); /* a clear-only frame is still a new display list */
    gx_fill(512, 256, 320, 224, 0xFFFFFF);
    gx_frame();
    assert(flushes == 1); /* a texture-area fill must not clear the display */

    reset();
    draw_y = 256; draw_w = 320; draw_h = 224;
    gp0_state.area_x0 = 100; gp0_state.area_x1 = 180;
    gp0_state.area_y0 = 300; gp0_state.area_y1 = 320;
    assert(drawing_to_screen() && origin_x == 0 && origin_y == 256);
    corners[0].x = 90; corners[0].y = 300;
    gx_rect_now(&plain, corners, 8, 8);
    assert(polys == 0); /* completely outside the dialogue clipping rectangle */
    corners[0].x = 104;
    gx_rect_now(&plain, corners, 8, 8);
    assert(polys == 1);

    reset();
    draw_w = 320; draw_h = 224;
    gp0_state.area_x0 = 100; gp0_state.area_x1 = 120;
    gp0_state.area_y0 = 50; gp0_state.area_y1 = 80;
    Gp0Vtx triangle[3] = {{90, 40, 255, 0, 0, 0, 0}, {140, 40, 0, 255, 0, 50, 0},
                          {110, 100, 0, 0, 255, 20, 60}};
    plain.gouraud = 1;
    gx_poly(&plain, triangle, 3);
    assert(emitted && min_x >= 100 && max_x <= 121 && min_y >= 50 && max_y <= 81);
    Gp0Vtx outside[3] = {{0, 0}, {10, 0}, {0, 10}};
    int previous = emitted;
    gx_poly(&plain, outside, 3);
    assert(emitted == previous);

    gx_fill(100, 50, 21, 31, 0);
    assert(!clear_pending); /* covering a dialogue clip does not cover the framebuffer */
    previous = emitted;
    gx_fill(0, 0, 16, 16, 0);
    assert(emitted > previous); /* fill ignores the dialogue clipping rectangle */
    reset();
    draw_w = 320; draw_h = 224;
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Mode subtract = {.textured = 1, .semi = 1, .abr = 2, .depth = 2, .tx = 512};
    Gp0Vtx sprite = {.x = 32, .y = 32, .r = 64, .g = 96, .b = 128};
    shadow_start();
    u16 expected[256];
    for (int i = 0; i < 256; i++) {
        u16 back = i ? (u16)((i * 73) & 0x7FFF) : 0x1234;
        u16 front = (u16)((i * 127) & 0x7FFF);
        if (i & 1) front |= 0x8000;
        if (i == 2) front = 0x8000; /* opaque black STP texel is not a hole */
        vram[(i / 16) * 1024 + 512 + i % 16] = front;
        sprite_shadow[(32 + i / 16) * SHADOW_W + 32 + i % 16] = back;
        u16 result = 0;
        if (!front) result = back;
        else for (int c = 0; c < 3; c++) {
            int intensity[3] = {64, 96, 128};
            int f = ((front >> (c * 5)) & 31) * intensity[c] / 128;
            int channel = (front & 0x8000) ? ((back >> (c * 5)) & 31) - f : f;
            if (channel < 0) channel = 0;
            result |= (u16)(channel << (c * 5));
        }
        expected[i] = result | 0x8000;
    }
    gx_rect_now(&subtract, &sprite, 16, 16);
    assert(uploads == 1 && polys == 1);
    for(int i=0;i<256;i++) assert(captured_pixel(i)==expected[i]);
    for (int i = 0; i < 256; i++)
        assert(sprite_shadow[(32 + i / 16) * SHADOW_W + 32 + i % 16] == (expected[i] & 0x7FFF));
    Gp0Mode raw_subtract = subtract;
    raw_subtract.raw = 1;
    vram[512] = 0x801F;
    assert(sprite_pixel(&raw_subtract, &sprite, 0, 0, 0x7FFF) == 0x7FE0);
    vram[512] = 0x001F; /* same RGB, but STP-clear texel overwrites */
    assert(sprite_pixel(&raw_subtract, &sprite, 0, 0, 0x7FFF) == 0x001F);
    Gp0Mode wrapped = {.depth = 1, .tx = 960, .ty = 511, .clut_x = 1008, .clut_y = 510};
    vram[0] = 0x1111;
    vram[510 * 1024 + 1] = 0x83E0;
    assert(sprite_texel(&wrapped, 128, 1) == 0x83E0); /* texture and CLUT wrap */
    gp0_state.twin_mask_x = 8; gp0_state.twin_off_x = 8;
    vram[511 * 1024 + 964] = 0x1111;
    assert(sprite_texel(&wrapped, 0, 0) == 0x83E0);
    gp0_state.twin_mask_x = gp0_state.twin_off_x = 0;
    TexEntry *composed = get_composite(16, 16);
    assert(composed && uploads == 1);
    gx_frame();
    memcpy(convbuf16, expected, sizeof expected);
    assert(get_composite(16, 16) == composed && uploads == 1); /* cache reuses identical results */
    shadow_start();
    Gp0Vtx obstruct[3] = {{32, 32, 248, 16, 16}, {40, 32, 248, 16, 16}, {32, 40, 248, 16, 16}};
    gx_poly(&plain, obstruct, 3);
#ifdef PORT_GX_EXPERIMENTAL
    assert(shadow_rect_known(&sprite, 16, 16));
    assert(sprite_shadow[34 * SHADOW_W + 34] == (31 | (2 << 5) | (2 << 10)));
    for (int j=0; j<16; j++) memset(vram+j*1024+512,0,16*sizeof(u16));
    int count = uploads;
    gx_rect_now(&subtract, &sprite, 16, 16);
    assert(uploads == count + 1 && shadow_rect_known(&sprite, 16, 16));
    assert(captured_pixel(2*16+2) == (0x8000 | 31 | (2 << 5) | (2 << 10)));
    assert(captured_pixel(15*16+15) == 0x8000); /* uncovered background stays black */

#else
    assert(!shadow_rect_known(&sprite, 16, 16));
#endif

    reset();
    draw_w = 320; draw_h = 224;
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Vtx base = {.x = 0, .y = 0, .r = 56, .g = 40, .b = 24};
    plain = (Gp0Mode){0};
    gx_rect_now(&plain, &base, 16, 16);
    subtract = (Gp0Mode){.textured = 1, .semi = 1, .abr = 2, .depth = 0, .tx = 512,
                         .ty = 256, .clut_x = 16, .clut_y = 255};
    vram[255 * 1024 + 16] = 0xFFFF; /* subtraction stronger than all three background channels */
    base.r = base.g = base.b = 64;
    gx_rect_now(&subtract, &base, 16, 16);
    for (int i = 0; i < 256; i++) assert(captured_pixel(i) == 0x8000);
    assert(sprite_shadow[0] == 0); /* exact black, without a scene-specific brightness rule */
#if defined(PORT_GX_EXPERIMENTAL) || defined(PORT_GX_MERGE)
    reset();
    draw_w = 320; draw_h = 224;
    gp0_state.area_x1 = 319; gp0_state.area_y1 = 223;
    Gp0Mode tiles = {.textured=1,.raw=1,.depth=2,.tx=512,.ty=256};
    Gp0Vtx tile = {.x=0,.y=0,.r=128,.g=128,.b=128};
    for (int y=0;y<16;y++) for (int x=0;x<32;x++) vram[(256+y)*1024+512+x]=0x801F;
    gx_rect(&tiles,&tile,16,16);
    tile.x=tile.u=16; gx_rect(&tiles,&tile,16,16);
    assert(rect_run.active && rect_run.w==32 && polys==0);
    gx_frame();
    assert(psx_gx_stats_polys==1 && emitted==4 && max_x==32);
    reset();
    draw_w=320; draw_h=224; gp0_state.area_x1=319; gp0_state.area_y1=223;
    tile.x=tile.u=0; gx_rect(&tiles,&tile,16,16);
    tile.x=16; tile.u=32; gx_rect(&tiles,&tile,16,16);
    assert(polys==1); /* discontiguous UVs cannot merge */
    gx_frame(); assert(psx_gx_stats_polys==2);
    reset();
    draw_w=320; draw_h=224; gp0_state.area_x1=319; gp0_state.area_y1=223;
    tile.x=tile.u=0; gx_rect(&tiles,&tile,16,16);
    draw_x=320; gp0_state.area_x0=320; gp0_state.area_x1=639;
    tile.x=320; gx_rect(&tiles,&tile,16,16);
    assert(polys==1 && max_x==16); /* queued tile uses its original framebuffer */
    gx_frame(); assert(psx_gx_stats_polys==2);
    reset();
    draw_w=320; draw_h=224; gp0_state.area_x1=319; gp0_state.area_y1=223;
    tile.x=tile.u=0;
    vram[256*1024+512]=0x801F;
    gx_rect(&tiles,&tile,16,16);
    assert(rect_run.active && polys==0);
    psx_vram_touch(512,256,1,1);
    assert(!rect_run.active && polys==1 && ((u16 *)captured)[0]==0x801F);
    vram[256*1024+512]=0x83E0;
    gx_rect(&tiles,&tile,16,16);
    gx_frame();
    assert(((u16 *)captured)[0]==0x83E0);
    reset();
#endif
#ifdef PORT_GX_EXPERIMENTAL
    for (int i=0;i<256;i++) convbuf16[i]=0x8000|(i%16);
    assert(get_composite(16,16) && captured_type==GL_RGB16);
    for (int i=0;i<256;i++) assert(captured_pixel(i)==convbuf16[i]);
    convbuf16[0]=0;
    for (int i=1;i<256;i++) convbuf16[i]=0x8000|(i%15);
    assert(get_composite(16,16) && captured_type==GL_RGB16);
    for (int i=0;i<256;i++) assert(captured_pixel(i)==convbuf16[i]);
    for (int i=0;i<256;i++) convbuf16[i]=0x8000|(i%17);
    assert(get_composite(16,16) && captured_type==GL_RGB256);
    for (int i=0;i<256;i++) assert(captured_pixel(i)==convbuf16[i]);
    for (int i=0;i<512;i++) convbuf16[i]=0x8000|i;
    assert(get_composite(32,16) && captured_type==GL_RGBA);
    for (int i=0;i<512;i++) assert(captured_pixel(i)==convbuf16[i]);
    reset();
    reject_palette=1;
    for(int i=0;i<256;i++) convbuf16[i]=0x8000|(i%64);
    assert(get_composite(16,16) && captured_type==GL_RGBA && deletes==1);
    for(int i=0;i<256;i++) assert(captured_pixel(i)==convbuf16[i]);
#else
    reset();
    for (int i = 0; i < 256; i++) convbuf16[i] = i ? 0x8000 | (i % 16) : 0;
    assert(get_composite(16, 16) && captured_type == GL_RGBA);
    assert(palette_uploads == 0);
    for (int i = 0; i < 256; i++) assert(captured_pixel(i) == convbuf16[i]);
#endif
    reset();
    Gp0Vtx narrow[2] = {{.u = 121, .v = 117}, {.u = 248, .v = 244}};
    int bound_u, bound_v, bound_w, bound_h;
    texture_bounds(narrow, 2, 0, 0, &bound_u, &bound_v, &bound_w, &bound_h, 1);
    assert(bound_u == 121 && bound_v == 117 && bound_w == 128 && bound_h == 128);
    narrow[0].u = 240; narrow[1].u = 255;
    narrow[0].v = 245; narrow[1].v = 255;
    texture_bounds(narrow, 2, 0, 0, &bound_u, &bound_v, &bound_w, &bound_h, 2);
    assert(bound_u == 240 && bound_v == 240 && bound_w == 16 && bound_h == 16);

    /* Check sparse generation scanning against every sampled source pixel. */
    unsigned int rng=0x9ff;
    for (int y=0;y<64;y++) for (int x=0;x<16;x++) block_gen[y][x]=1+y*16+x;
    for (int y=0;y<512;y++) for (int x=0;x<16;x++) palette_row_gen[y][x]=2000+y*16+x;
    for (int trial=0;trial<80;trial++) {
        rng=rng*1664525u+1013904223u;
        Gp0Mode mode={.depth=trial%3,.tx=(int)(rng%16)*64,.ty=(trial&1)*256,
                      .clut_x=(int)((rng>>8)%64)*16,.clut_y=(int)((rng>>16)%512)};
        gp0_state.twin_mask_x=((rng>>3)&31)*8; gp0_state.twin_mask_y=((rng>>8)&31)*8;
        gp0_state.twin_off_x=((rng>>13)&31)*8; gp0_state.twin_off_y=((rng>>18)&31)*8;
        int w=16<<(trial%5), h=16<<((trial/5)%5);
        int u0=(rng>>1)%(257-w), v0=(rng>>9)%(257-h);
        int shift=mode.depth==0?2:mode.depth==1?1:0;
        unsigned int expected_gen=0;
        for (int y=v0;y<v0+h;y++) for (int x=u0;x<u0+w;x++) {
            int tx=(x&~gp0_state.twin_mask_x)|(gp0_state.twin_off_x&gp0_state.twin_mask_x);
            int ty=(y&~gp0_state.twin_mask_y)|(gp0_state.twin_off_y&gp0_state.twin_mask_y);
            unsigned int gen=block_gen[((mode.ty+ty)&511)/8][((mode.tx+(tx>>shift))&1023)/64];
            if(gen>expected_gen) expected_gen=gen;
        }
        if(mode.depth<2) for(int x=0;x<(mode.depth?256:16);x++) {
            unsigned int gen=palette_row_gen[mode.clut_y][((mode.clut_x+x)&1023)/64];
            if(gen>expected_gen) expected_gen=gen;
        }
        assert(source_gen(&mode,u0,v0,w,h)==expected_gen);
    }
    reset();
    puts("GX texture/cache/compositing regression checks passed");
}

/* libgpu: drawing state, a 1024x512 VRAM shadow, packet builders, and the ordering-table walker.
 *
 * VRAM: the game uploads textures and CLUTs (LoadImage), copies within VRAM (MoveImage, DR_MOVE packets), reads
 * back (StoreImage) and clears (ClearImage, isbg). All of that is kept exactly in the shadow, which the renderer
 * converts textures from (M2). Packet builders produce the GP0 words psx-spx documents, since game code may
 * build or patch packets by hand too.
 *
 * DrawOTag walks the ordering table in the PS1's order. OT links are 24-bit (P_TAG.addr): psx_ram lives in the
 * one 16 MB window both platforms share (DSi main RAM 0x02000000-0x02FFFFFF; the PC maps psx_ram there), so a
 * link rebuilds as (psx_ram & 0xFF000000) | addr24. The walker applies VRAM fills and copies to the shadow and
 * hands every packet to the renderer (psx_render_packet, weak: none yet).
 */
#include "psyq_port.h"

#define VRAM_W 1024
#define VRAM_H 512
#define OT_END 0xFFFFFF

u16 psx_vram[VRAM_W * VRAM_H];

static DRAWENV draw_env;
static DISPENV disp_env;
static void (*drawsync_cb)(void);
static int drawsync_owed;
static int graph_debug;

/* per-frame statistics (debug overlay, perf.md) */
unsigned int psx_gpu_packets, psx_gpu_words;

/* The renderer's entry (M2): called for each packet in draw order. */
__attribute__((weak)) void psx_render_packet(const u32 *words, unsigned int len) {
}
__attribute__((weak)) void psx_render_begin(const DRAWENV *env) {
}
__attribute__((weak)) void psx_render_end(void) {
}

static inline u32 *link_ptr(u32 addr24) {
    return (u32 *)(((u32)psx_ram & 0xFF000000) | addr24);
}

/* ---- VRAM ----------------------------------------------------------------------------------------------- */

static inline u16 *vram_at(int x, int y) {
    return &psx_vram[(y & (VRAM_H - 1)) * VRAM_W + (x & (VRAM_W - 1))];
}

static void vram_fill(int x, int y, int w, int h, u16 c) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            *vram_at(x + i, y + j) = c;
        }
    }
}

static void vram_copy(int sx, int sy, int dx, int dy, int w, int h) {
    /* row buffer: overlapping copies behave as the GPU's line-by-line copy */
    static u16 row[VRAM_W];
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            row[i] = *vram_at(sx + i, sy + j);
        }
        for (int i = 0; i < w; i++) {
            *vram_at(dx + i, dy + j) = row[i];
        }
    }
}

static u16 rgb15(int r, int g, int b) {
    return (u16)(((r >> 3) & 0x1F) | (((g >> 3) & 0x1F) << 5) | (((b >> 3) & 0x1F) << 10));
}

int LoadImage(RECT *r, u32 *p) {
    const u16 *src = (const u16 *)p;
    for (int j = 0; j < r->h; j++) {
        for (int i = 0; i < r->w; i++) {
            *vram_at(r->x + i, r->y + j) = *src++;
        }
    }
    return 0;
}

int StoreImage(RECT *r, u32 *p) {
    u16 *dst = (u16 *)p;
    for (int j = 0; j < r->h; j++) {
        for (int i = 0; i < r->w; i++) {
            *dst++ = *vram_at(r->x + i, r->y + j);
        }
    }
    return 0;
}

int MoveImage(RECT *r, int x, int y) {
    vram_copy(r->x, r->y, x, y, r->w, r->h);
    return 0;
}

void ClearImage(RECT *r, s32 cr, s32 cg, s32 cb) {
    vram_fill(r->x, r->y, r->w, r->h, rgb15(cr, cg, cb));
}

u16 GetTPage(int tp, int abr, int x, int y) {
    return (u16)(((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

u16 GetClut(int x, int y) {
    return (u16)((y << 6) | ((x >> 4) & 0x3F));
}

u16 LoadTPage(u32 *pix, s32 tp, s32 abr, s32 x, s32 y, s32 w, s32 h) {
    RECT r = {(s16)x, (s16)y, (s16)(tp == 0 ? w / 4 : tp == 1 ? w / 2 : w), (s16)h};
    LoadImage(&r, pix);
    return GetTPage(tp, abr, x, y);
}

u16 LoadClut(u32 *clut, s32 x, s32 y) {
    RECT r = {(s16)x, (s16)y, 256, 1};
    LoadImage(&r, clut);
    return GetClut(x, y);
}

/* ---- environments --------------------------------------------------------------------------------------- */

int ResetGraph(int mode) {
    return 0;
}

int SetGraphDebug(int level) {
    int old = graph_debug;
    graph_debug = level;
    return old;
}

void SetDispMask(int mask) {
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h) {
    env->clip.x = (s16)x, env->clip.y = (s16)y, env->clip.w = (s16)w, env->clip.h = (s16)h;
    env->ofs[0] = (s16)x, env->ofs[1] = (s16)y;
    env->tw.x = env->tw.y = env->tw.w = env->tw.h = 0;
    env->tpage = GetTPage(0, 0, 640, 0);
    env->dtd = 1;
    env->dfe = 1;
    env->isbg = 0;
    env->r0 = env->g0 = env->b0 = 0;
    return env;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h) {
    env->disp.x = (s16)x, env->disp.y = (s16)y, env->disp.w = (s16)w, env->disp.h = (s16)h;
    env->screen.x = env->screen.y = env->screen.w = env->screen.h = 0;
    env->isinter = 0;
    env->isrgb24 = 0;
    env->pad0 = env->pad1 = 0;
    return env;
}

static u32 area_tl(const RECT *r) {
    return 0xE3000000 | ((r->y & 0x3FF) << 10) | (r->x & 0x3FF);
}

static u32 area_br(const RECT *r) {
    return 0xE4000000 | (((r->y + r->h - 1) & 0x3FF) << 10) | ((r->x + r->w - 1) & 0x3FF);
}

static u32 offset_word(int x, int y) {
    return 0xE5000000 | ((y & 0x7FF) << 11) | (x & 0x7FF);
}

static inline void set_len(void *p, unsigned int len) /* via P_TAG: psx_ram is accessed through any type */ {
    ((P_TAG *)p)->len = len;
}

void SetDrawEnv(DR_ENV *dr, DRAWENV *env) {
    unsigned int n = 0;
    dr->code[n++] = area_tl(&env->clip);
    dr->code[n++] = area_br(&env->clip);
    dr->code[n++] = offset_word(env->ofs[0], env->ofs[1]);
    dr->code[n++] = get_mode(env->dfe, env->dtd, env->tpage);
    dr->code[n++] = get_tw(&env->tw);
    dr->code[n++] = 0xE6000000;
    if (env->isbg) {
        dr->code[n++] = 0x02000000 | (env->b0 << 16) | (env->g0 << 8) | env->r0;
        dr->code[n++] = ((u32)env->clip.y << 16) | (u16)env->clip.x;
        dr->code[n++] = ((u32)env->clip.h << 16) | (u16)env->clip.w;
    }
    set_len(dr, n);
}

static void draw_packet(u32 *p);

DRAWENV *PutDrawEnv(DRAWENV *env) {
    SetDrawEnv(&env->dr_env, env);
    ((P_TAG *)&env->dr_env)->addr = OT_END;
    draw_packet((u32 *)&env->dr_env);
    __builtin_memcpy(&draw_env, env, sizeof draw_env);
    psx_render_begin(env);
    return env;
}

DISPENV *PutDispEnv(DISPENV *env) {
    __builtin_memcpy(&disp_env, env, sizeof disp_env);
    return env;
}

void GetDrawEnv2(DR_ENV *p) {
    SetDrawEnv(p, &draw_env);
}

/* ---- packet builders ------------------------------------------------------------------------------------ */

void SetDrawArea(DR_AREA *p, RECT *r) {
    ((u32 *)p)[1] = area_tl(r);
    ((u32 *)p)[2] = area_br(r);
    set_len(p, 2);
}

void SetDrawOffset(DR_OFFSET *p, s16 *ofs) {
    ((u32 *)p)[1] = offset_word(ofs[0], ofs[1]);
    ((u32 *)p)[2] = 0;
    set_len(p, 2);
}

void SetTexWindow(void *p, RECT *r) {
    ((u32 *)p)[1] = get_tw(r);
    ((u32 *)p)[2] = 0;
    set_len(p, 2);
}

void SetDrawMove(void *p, RECT *r, int x, int y) {
    u32 *w = p;
    set_len(p, (r->w && r->h) ? 5 : 0);
    w[1] = 0x01000000;
    w[2] = 0x80000000;
    w[3] = ((u32)r->y << 16) | (u16)r->x;
    w[4] = ((u32)y << 16) | (u16)x;
    w[5] = ((u32)r->h << 16) | (u16)r->w;
}

void SetDrawStp(DR_STP *p, int pbw) {
    ((u32 *)p)[1] = 0xE6000000 | (pbw & 3);
    ((u32 *)p)[2] = 0;
    set_len(p, 2);
}

void SetDrawTPage(void *p, int dfe, int dtd, int tpage) {
    ((u32 *)p)[1] = get_mode(dfe, dtd, tpage);
    set_len(p, 1);
}

static inline void init_prim(void *p, unsigned int len, u8 code) {
    set_len(p, len);
    ((P_TAG *)p)->code = code;
}

void SetPolyF3(POLY_F3 *p) { init_prim(p, 4, 0x20); }
void SetPolyF4(POLY_F4 *p) { init_prim(p, 5, 0x28); }
void SetPolyG3(POLY_G3 *p) { init_prim(p, 6, 0x30); }
void SetPolyG4(POLY_G4 *p) { init_prim(p, 8, 0x38); }
void SetPolyFT4(POLY_FT4 *p) { init_prim(p, 9, 0x2C); }
void SetSprt(SPRT *p) { init_prim(p, 4, 0x64); }
void SetSprt16(SPRT_16 *p) { init_prim(p, 3, 0x7C); }
void SetTile(TILE *p) { init_prim(p, 3, 0x60); }
void SetLineF2(LINE_F2 *p) { init_prim(p, 3, 0x40); }
void SetLineG2(LINE_G2 *p) { init_prim(p, 4, 0x50); }

void SetSemiTrans(void *p, int abe) {
    if (abe) {
        ((P_TAG *)p)->code |= 2;
    } else {
        ((P_TAG *)p)->code &= ~2;
    }
}

void AddPrim(void *ot, void *p) {
    ((P_TAG *)p)->addr = ((P_TAG *)ot)->addr;
    ((P_TAG *)ot)->addr = (u32)p & 0xFFFFFF;
}

int MargePrim(void *p0, void *p1) {
    unsigned int n = ((P_TAG *)p0)->len + ((P_TAG *)p1)->len + 1;
    if (n > 16) {
        return -1;
    }
    set_len(p0, n);
    *(u32 *)p1 = 0;
    return 0;
}

u32 *ClearOTag(u32 *ot, int n) {
    for (int i = 0; i < n - 1; i++) {
        ot[i] = (u32)&ot[i + 1] & 0xFFFFFF;
    }
    ot[n - 1] = OT_END;
    return ot;
}

u32 *ClearOTagR(u32 *ot, int n) {
    ot[0] = OT_END;
    for (int i = 1; i < n; i++) {
        ot[i] = (u32)&ot[i - 1] & 0xFFFFFF;
    }
    return ot;
}

/* ---- drawing -------------------------------------------------------------------------------------------- */

/* One packet: VRAM-affecting commands go to the shadow; everything goes to the renderer. */
static void draw_packet(u32 *p) {
    unsigned int len = ((P_TAG *)p)->len;
    u32 *w = p + 1;
    psx_gpu_packets++;
    psx_gpu_words += len;
    for (unsigned int i = 0; i < len;) {
        u32 cmd = w[i] >> 24;
        if (cmd == 0x02 && i + 2 < len) { /* fill rectangle */
            u32 c = w[i];
            vram_fill(w[i + 1] & 0x3F0, (w[i + 1] >> 16) & 0x1FF, ((w[i + 2] & 0x3FF) + 15) & ~15,
                      (w[i + 2] >> 16) & 0x1FF, rgb15(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF));
            i += 3;
        } else if (cmd == 0x80 && i + 3 < len) { /* VRAM to VRAM copy */
            vram_copy(w[i + 1] & 0x3FF, (w[i + 1] >> 16) & 0x1FF, w[i + 2] & 0x3FF, (w[i + 2] >> 16) & 0x1FF,
                      w[i + 3] & 0x3FF, (w[i + 3] >> 16) & 0x1FF);
            i += 4;
        } else {
            i++;
        }
    }
    if (len) {
        psx_render_packet(w, len);
    }
}

void DrawPrim(void *p) {
    draw_packet(p);
}

void DrawOTag(u32 *p) {
    unsigned int guard = 0;
    while (((u32)p & 0xFFFFFF) != OT_END) {
        u32 addr = *p & 0xFFFFFF;
        if (((P_TAG *)p)->len) {
            draw_packet(p);
        }
        if (addr == OT_END) {
            break;
        }
        p = link_ptr(addr);
        if ((unsigned char *)p < psx_ram || (unsigned char *)p >= psx_ram + PSX_RAM_SIZE) {
            port_fatal("DrawOTag: link 0x%06X outside psx_ram", addr);
        }
        if (++guard > 1000000) {
            port_fatal("DrawOTag: ordering table loops");
        }
    }
    psx_render_end();
    if (drawsync_cb) {
        drawsync_owed = 1;
    }
}

void DrawOTagEnv(u32 *p, DRAWENV *env) {
    PutDrawEnv(env);
    DrawOTag(p);
}

int DrawSyncCallback(void (*func)(void)) {
    void (*old)(void) = drawsync_cb;
    drawsync_cb = func;
    return (int)(u32)old;
}

/* Deliver an owed DrawSync callback (the GPU "finished"): at DrawSync and once per frame. */
void psx_drawsync_pending(void) {
    if (drawsync_owed && drawsync_cb) {
        drawsync_owed = 0;
        drawsync_cb();
    }
}

int DrawSync(int mode) {
    if (mode == 0) {
        psx_drawsync_pending();
    }
    return 0;
}

s32 IsIdleGPU(s32 max) {
    return 0;
}

s32 BreakDraw(void) {
    return 0;
}

void ContinueDraw(u32 *insaddr, s32 contaddr) {
}

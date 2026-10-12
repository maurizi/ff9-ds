/* A software PS1 GPU: rasterises the packets the ordering-table walker hands over into the VRAM shadow, following
 * psx-spx ("GPU Render Polygon/Line/Rectangle Commands", "GPU Rendering Attributes").
 *
 * It is the reference renderer: on the PC build it shows what the PS1 would display (frame dumps), which the DS
 * renderer (3D hardware, scaled) is checked against. It models drawing area, offset, texture pages (4/8/15 bit, CLUTs),
 * texture window, the four semi-transparency modes, raw vs modulated textures, gouraud shading, mask bits.
 * Dithering and exact sub-pixel edge rules are not modelled.
 *
 * Enabled with psx_softgpu_enable(1) (PC: --render).
 */
#include "psyq_port.h"

#define VRAM_W 1024
#define VRAM_H 512

extern u16 *psx_vram;

static int enabled;

/* drawing environment (GP0 E1-E6) */
static int area_x0, area_y0, area_x1 = VRAM_W - 1, area_y1 = VRAM_H - 1;
static int ofs_x, ofs_y;
static u32 texpage; /* E1 bits */
static u32 twin_mask_x, twin_mask_y, twin_off_x, twin_off_y;
static int set_mask, check_mask;

void psx_softgpu_enable(int on) {
    enabled = on;
}

static inline u16 *px(int x, int y) {
    return &psx_vram[(y & (VRAM_H - 1)) * VRAM_W + (x & (VRAM_W - 1))];
}

/* ---- pixel pipeline ------------------------------------------------------------------------------------ */

typedef struct {
    int textured, raw, semi, gouraud;
    int abr;      /* blend mode */
    int depth;    /* 0 4-bit, 1 8-bit, 2 15-bit */
    int tx, ty;   /* texture page base */
    int clut_x, clut_y;
} Mode;

static inline u16 blend(u16 back, u16 front, int abr) {
    int br = back & 31, bg = (back >> 5) & 31, bb = (back >> 10) & 31;
    int fr = front & 31, fg = (front >> 5) & 31, fb = (front >> 10) & 31;
    int r, g, b;
    switch (abr) {
    case 0: r = (br + fr) >> 1; g = (bg + fg) >> 1; b = (bb + fb) >> 1; break;
    case 1: r = br + fr; g = bg + fg; b = bb + fb; break;
    case 2: r = br - fr; g = bg - fg; b = bb - fb; break;
    default: r = br + (fr >> 2); g = bg + (fg >> 2); b = bb + (fb >> 2); break;
    }
    r = r < 0 ? 0 : r > 31 ? 31 : r;
    g = g < 0 ? 0 : g > 31 ? 31 : g;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (u16)(r | g << 5 | b << 10);
}

static inline u16 texel(const Mode *m, int u, int v) {
    u = (u & ~twin_mask_x) | (twin_off_x & twin_mask_x);
    v = (v & ~twin_mask_y) | (twin_off_y & twin_mask_y);
    u &= 0xFF;
    v &= 0xFF;
    switch (m->depth) {
    case 0: {
        u16 w = *px(m->tx + (u >> 2), m->ty + v);
        int idx = (w >> ((u & 3) * 4)) & 0xF;
        return *px(m->clut_x + idx, m->clut_y);
    }
    case 1: {
        u16 w = *px(m->tx + (u >> 1), m->ty + v);
        int idx = (w >> ((u & 1) * 8)) & 0xFF;
        return *px(m->clut_x + idx, m->clut_y);
    }
    default:
        return *px(m->tx + u, m->ty + v);
    }
}

/* Plot one pixel: colour (r, g, b 0..255), texture coordinate (u, v) if textured. */
static inline void plot(const Mode *m, int x, int y, int r, int g, int b, int u, int v) {
    if (x < area_x0 || x > area_x1 || y < area_y0 || y > area_y1) {
        return;
    }
    u16 *d = px(x, y);
    if (check_mask && (*d & 0x8000)) {
        return;
    }
    u16 c;
    int semi = m->semi;
    if (m->textured) {
        u16 t = texel(m, u, v);
        if (t == 0) {
            return; /* fully transparent */
        }
        semi = semi && (t & 0x8000);
        if (m->raw) {
            c = t;
        } else {
            int tr = (t & 31) * r >> 7, tg = ((t >> 5) & 31) * g >> 7, tb = ((t >> 10) & 31) * b >> 7;
            c = (u16)((tr > 31 ? 31 : tr) | (tg > 31 ? 31 : tg) << 5 | (tb > 31 ? 31 : tb) << 10 | (t & 0x8000));
        }
    } else {
        c = (u16)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
    }
    if (semi) {
        c = (u16)(blend(*d, c, m->abr) | (c & 0x8000));
    }
    *d = c | (set_mask ? 0x8000 : 0);
}

/* ---- primitives ---------------------------------------------------------------------------------------- */

typedef struct {
    int x, y, r, g, b, u, v;
} Vtx;

static inline s32 edge(const Vtx *a, const Vtx *b, int x, int y) {
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static void triangle(const Mode *m, Vtx a, Vtx b, Vtx c) {
    s32 area = edge(&a, &b, c.x, c.y);
    if (area == 0) {
        return;
    }
    if (area < 0) { /* make it counter-clockwise */
        Vtx t = b;
        b = c;
        c = t;
        area = -area;
    }
    /* the GPU skips polygons larger than 1023x511 */
    int minx = a.x, maxx = a.x, miny = a.y, maxy = a.y;
    const Vtx *vs[3] = {&a, &b, &c};
    for (int i = 1; i < 3; i++) {
        if (vs[i]->x < minx) minx = vs[i]->x;
        if (vs[i]->x > maxx) maxx = vs[i]->x;
        if (vs[i]->y < miny) miny = vs[i]->y;
        if (vs[i]->y > maxy) maxy = vs[i]->y;
    }
    if (maxx - minx >= 1024 || maxy - miny >= 512) {
        return;
    }
    if (minx < area_x0) minx = area_x0;
    if (miny < area_y0) miny = area_y0;
    if (maxx > area_x1 + 1) maxx = area_x1 + 1;
    if (maxy > area_y1 + 1) maxy = area_y1 + 1;
    for (int y = miny; y < maxy; y++) {
        for (int x = minx; x < maxx; x++) {
            s32 w0 = edge(&b, &c, x, y), w1 = edge(&c, &a, x, y), w2 = edge(&a, &b, x, y);
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            /* top-left rule: drop pixels exactly on right/bottom edges */
            if ((w0 == 0 && !(c.y < b.y || (c.y == b.y && c.x < b.x))) ||
                (w1 == 0 && !(a.y < c.y || (a.y == c.y && a.x < c.x))) ||
                (w2 == 0 && !(b.y < a.y || (b.y == a.y && b.x < a.x)))) {
                continue;
            }
            int r = a.r, g = a.g, bl = a.b;
            if (m->gouraud) {
                r = (w0 * a.r + w1 * b.r + w2 * c.r) / area;
                g = (w0 * a.g + w1 * b.g + w2 * c.g) / area;
                bl = (w0 * a.b + w1 * b.b + w2 * c.b) / area;
            }
            int u = 0, v = 0;
            if (m->textured) {
                u = (w0 * a.u + w1 * b.u + w2 * c.u) / area;
                v = (w0 * a.v + w1 * b.v + w2 * c.v) / area;
            }
            plot(m, x, y, r, g, bl, u, v);
        }
    }
}

static void line(const Mode *m, Vtx a, Vtx b) {
    int dx = b.x - a.x, dy = b.y - a.y;
    int n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (n >= 1024) {
        return;
    }
    for (int i = 0; i <= n; i++) {
        int x = n ? a.x + dx * i / n : a.x, y = n ? a.y + dy * i / n : a.y;
        int r = a.r, g = a.g, bl = a.b;
        if (m->gouraud && n) {
            r = a.r + (b.r - a.r) * i / n;
            g = a.g + (b.g - a.g) * i / n;
            bl = a.b + (b.b - a.b) * i / n;
        }
        plot(m, x, y, r, g, bl, 0, 0);
    }
}

static void rect(const Mode *m, Vtx a, int w, int h) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            plot(m, a.x + i, a.y + j, a.r, a.g, a.b, a.u + i, a.v + j);
        }
    }
}

/* ---- packet decoding ----------------------------------------------------------------------------------- */

static inline int sext11(int v) {
    return (v << 21) >> 21;
}

static Vtx vtx(u32 xy) {
    Vtx v = {0};
    v.x = sext11((int)(xy & 0x7FF)) + ofs_x;
    v.y = sext11((int)((xy >> 16) & 0x7FF)) + ofs_y;
    return v;
}

static void set_color(Vtx *v, u32 c) {
    v->r = c & 0xFF;
    v->g = (c >> 8) & 0xFF;
    v->b = (c >> 16) & 0xFF;
}

static void apply_texpage(Mode *m, u32 tp) {
    m->tx = (tp & 15) * 64;
    m->ty = ((tp >> 4) & 1) * 256;
    m->abr = (tp >> 5) & 3;
    m->depth = (tp >> 7) & 3;
    if (m->depth == 3) {
        m->depth = 2;
    }
}

static void mode_from_texpage(Mode *m) {
    apply_texpage(m, texpage);
}

/* Render one GP0 command starting at w[0]; returns the number of words it used. */
static unsigned int command(const u32 *w, unsigned int avail) {
    u32 cmd = w[0] >> 24;
    Mode m = {0};
    mode_from_texpage(&m);

    if (cmd >= 0x20 && cmd < 0x40) { /* polygon */
        m.gouraud = (cmd >> 4) & 1;
        int quad = (cmd >> 3) & 1;
        m.textured = (cmd >> 2) & 1;
        m.semi = (cmd >> 1) & 1;
        m.raw = cmd & 1;
        int nv = quad ? 4 : 3;
        unsigned int need = 1 + nv * (1 + m.textured) + (m.gouraud ? nv - 1 : 0);
        if (need > avail) {
            return avail;
        }
        Vtx v[4];
        unsigned int k = 0;
        for (int i = 0; i < nv; i++) {
            u32 col = (i == 0 || !m.gouraud) ? w[0] : w[k++];
            if (i == 0) {
                k = 1;
            }
            v[i] = vtx(w[k++]);
            set_color(&v[i], col);
            if (m.textured) {
                u32 uv = w[k++];
                v[i].u = uv & 0xFF;
                v[i].v = (uv >> 8) & 0xFF;
                if (i == 0) {
                    m.clut_x = ((uv >> 16) & 0x3F) * 16;
                    m.clut_y = (uv >> 22) & 0x1FF;
                } else if (i == 1) {
                    u32 tp = uv >> 16;
                    texpage = (texpage & ~0x9FFu) | (tp & 0x9FF);
                    apply_texpage(&m, tp);
                }
            }
        }
        triangle(&m, v[0], v[1], v[2]);
        if (quad) {
            triangle(&m, v[1], v[2], v[3]);
        }
        return need;
    }
    if (cmd >= 0x40 && cmd < 0x60) { /* line, polyline */
        m.gouraud = (cmd >> 4) & 1;
        m.semi = (cmd >> 1) & 1;
        int poly = (cmd >> 3) & 1;
        unsigned int k = 1;
        Vtx prev = vtx(w[k++]);
        set_color(&prev, w[0]);
        for (;;) {
            if (k >= avail) {
                return avail;
            }
            u32 col = w[0];
            if (m.gouraud) {
                col = w[k++];
                if (k >= avail) {
                    return avail;
                }
            }
            if (poly && (w[k - (m.gouraud ? 1 : 0)] & 0xF000F000) == 0x50005000) {
                return k;
            }
            Vtx next = vtx(w[k++]);
            set_color(&next, col);
            line(&m, prev, next);
            prev = next;
            if (!poly) {
                return k;
            }
        }
    }
    if (cmd >= 0x60 && cmd < 0x80) { /* rectangle */
        m.textured = (cmd >> 2) & 1;
        m.semi = (cmd >> 1) & 1;
        m.raw = cmd & 1;
        int size = (cmd >> 3) & 3;
        unsigned int need = 2 + m.textured + (size == 0);
        if (need > avail) {
            return avail;
        }
        Vtx v = vtx(w[1]);
        set_color(&v, w[0]);
        unsigned int k = 2;
        if (m.textured) {
            u32 uv = w[k++];
            v.u = uv & 0xFF;
            v.v = (uv >> 8) & 0xFF;
            m.clut_x = ((uv >> 16) & 0x3F) * 16;
            m.clut_y = (uv >> 22) & 0x1FF;
        }
        int wd = 1, ht = 1;
        if (size == 0) {
            wd = w[k] & 0x3FF;
            ht = (w[k] >> 16) & 0x1FF;
        } else if (size == 2) {
            wd = ht = 8;
        } else if (size == 3) {
            wd = ht = 16;
        }
        rect(&m, v, wd, ht);
        return need;
    }
    switch (cmd) {
    case 0xE1:
        texpage = w[0] & 0xFFFFFF;
        break;
    case 0xE2:
        twin_mask_x = (w[0] & 31) * 8;
        twin_mask_y = ((w[0] >> 5) & 31) * 8;
        twin_off_x = ((w[0] >> 10) & 31) * 8;
        twin_off_y = ((w[0] >> 15) & 31) * 8;
        break;
    case 0xE3:
        area_x0 = w[0] & 0x3FF;
        area_y0 = (w[0] >> 10) & 0x1FF;
        break;
    case 0xE4:
        area_x1 = w[0] & 0x3FF;
        area_y1 = (w[0] >> 10) & 0x1FF;
        break;
    case 0xE5:
        ofs_x = sext11((int)(w[0] & 0x7FF));
        ofs_y = sext11((int)((w[0] >> 11) & 0x7FF));
        break;
    case 0xE6:
        set_mask = w[0] & 1;
        check_mask = (w[0] >> 1) & 1;
        break;
    case 0x02: /* fill: done by gpu.c on the shadow */
        return 3;
    case 0x80: /* VRAM copy: done by gpu.c */
        return 4;
    default:
        break;
    }
    return 1;
}

/* the walker's hook (gpu.c): one packet's words */
void psx_render_packet(const u32 *words, unsigned int len) {
    if (!enabled || !psx_vram) {
        return;
    }
    for (unsigned int i = 0; i < len;) {
        i += command(words + i, len - i);
    }
}

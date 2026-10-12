/* The software PS1 GPU backend (gp0.h): rasterises decoded primitives into the VRAM shadow.
 *
 * It is the reference renderer: on the PC build it shows what the PS1 would display (frame dumps), and the DS 3D
 * renderer is checked against it. It models the drawing area, texture pages (4/8/15 bit, CLUTs), texture window,
 * the four semi-transparency modes, raw vs modulated textures, gouraud shading and mask bits. Dithering and exact
 * sub-pixel edge rules are not modelled. Edge functions and interpolants step per pixel (no division in the loop).
 */
#include "gp0.h"
#include "psyq_port.h"

#define VRAM_W 1024
#define VRAM_H 512

extern u16 *psx_vram;

static inline u16 *px(int x, int y) {
    return &psx_vram[(y & (VRAM_H - 1)) * VRAM_W + (x & (VRAM_W - 1))];
}

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

static inline u16 texel(const Gp0Mode *m, int u, int v) {
    const Gp0State *s = &gp0_state;
    u = (int)(((unsigned)u & ~s->twin_mask_x) | (s->twin_off_x & s->twin_mask_x)) & 0xFF;
    v = (int)(((unsigned)v & ~s->twin_mask_y) | (s->twin_off_y & s->twin_mask_y)) & 0xFF;
    switch (m->depth) {
    case 0: {
        u16 w = *px(m->tx + (u >> 2), m->ty + v);
        return *px(m->clut_x + ((w >> ((u & 3) * 4)) & 0xF), m->clut_y);
    }
    case 1: {
        u16 w = *px(m->tx + (u >> 1), m->ty + v);
        return *px(m->clut_x + ((w >> ((u & 1) * 8)) & 0xFF), m->clut_y);
    }
    default:
        return *px(m->tx + u, m->ty + v);
    }
}

static inline void plot(const Gp0Mode *m, int x, int y, int r, int g, int b, int u, int v) {
    u16 *d = px(x, y);
    if (gp0_state.check_mask && (*d & 0x8000)) {
        return;
    }
    u16 c;
    int semi = m->semi;
    if (m->textured) {
        u16 t = texel(m, u, v);
        if (t == 0) {
            return;
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
    *d = c | (gp0_state.set_mask ? 0x8000 : 0);
}

/* ---- triangle: edge functions and interpolants stepped in 16.16 fixed point --------------------------- */

static void triangle(const Gp0Mode *m, const Gp0Vtx *a, const Gp0Vtx *b, const Gp0Vtx *c) {
    int area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        const Gp0Vtx *t = b;
        b = c;
        c = t;
        area = -area;
    }
    int minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
    const Gp0Vtx *vs[3] = {a, b, c};
    for (int i = 1; i < 3; i++) {
        if (vs[i]->x < minx) minx = vs[i]->x;
        if (vs[i]->x > maxx) maxx = vs[i]->x;
        if (vs[i]->y < miny) miny = vs[i]->y;
        if (vs[i]->y > maxy) maxy = vs[i]->y;
    }
    if (maxx - minx >= 1024 || maxy - miny >= 512) {
        return; /* the GPU skips polygons this large */
    }
    if (minx < gp0_state.area_x0) minx = gp0_state.area_x0;
    if (miny < gp0_state.area_y0) miny = gp0_state.area_y0;
    if (maxx > gp0_state.area_x1 + 1) maxx = gp0_state.area_x1 + 1;
    if (maxy > gp0_state.area_y1 + 1) maxy = gp0_state.area_y1 + 1;
    if (minx >= maxx || miny >= maxy) {
        return;
    }
    /* edge function e_i(x, y) = A_i x + B_i y + C_i, for the edge opposite vertex i */
    int A0 = b->y - c->y, B0 = c->x - b->x, A1 = c->y - a->y, B1 = a->x - c->x, A2 = a->y - b->y, B2 = b->x - a->x;
    int w0row = A0 * (minx - b->x) + B0 * (miny - b->y);
    int w1row = A1 * (minx - c->x) + B1 * (miny - c->y);
    int w2row = A2 * (minx - a->x) + B2 * (miny - a->y);
    /* top-left rule: on a right/bottom edge the pixel belongs to the neighbour (bias the edge by -1) */
    int bias0 = (A0 > 0 || (A0 == 0 && B0 < 0)) ? 0 : -1;
    int bias1 = (A1 > 0 || (A1 == 0 && B1 < 0)) ? 0 : -1;
    int bias2 = (A2 > 0 || (A2 == 0 && B2 < 0)) ? 0 : -1;
    /* attribute = (w0 * a + w1 * b + w2 * c) / area: precompute 16.16 per-step deltas */
    long long inv = ((long long)1 << 32) / area; /* 0.32 */
#define GRAD(fa, fb, fc) \
    (int)((((long long)(fa) * A0 + (long long)(fb) * A1 + (long long)(fc) * A2) * inv) >> 16), \
    (int)((((long long)(fa) * B0 + (long long)(fb) * B1 + (long long)(fc) * B2) * inv) >> 16)
    int drdx, drdy, dgdx, dgdy, dbdx, dbdy, dudx, dudy, dvdx, dvdy;
    {
        int t[2] = {GRAD(a->r, b->r, c->r)}; drdx = t[0]; drdy = t[1];
    }
    {
        int t[2] = {GRAD(a->g, b->g, c->g)}; dgdx = t[0]; dgdy = t[1];
    }
    {
        int t[2] = {GRAD(a->b, b->b, c->b)}; dbdx = t[0]; dbdy = t[1];
    }
    {
        int t[2] = {GRAD(a->u, b->u, c->u)}; dudx = t[0]; dudy = t[1];
    }
    {
        int t[2] = {GRAD(a->v, b->v, c->v)}; dvdx = t[0]; dvdy = t[1];
    }
#undef GRAD
    /* attribute values at (minx, miny), 16.16 */
#define AT(fa, fb, fc) \
    (int)((((long long)(fa) * w0row + (long long)(fb) * w1row + (long long)(fc) * w2row) * inv) >> 16)
    int rrow = AT(a->r, b->r, c->r), grow = AT(a->g, b->g, c->g), brow = AT(a->b, b->b, c->b);
    int urow = AT(a->u, b->u, c->u), vrow = AT(a->v, b->v, c->v);
#undef AT
    for (int y = miny; y < maxy; y++) {
        int w0 = w0row, w1 = w1row, w2 = w2row;
        int r = rrow, g = grow, bl = brow, u = urow, v = vrow;
        for (int x = minx; x < maxx; x++) {
            if ((w0 + bias0) >= 0 && (w1 + bias1) >= 0 && (w2 + bias2) >= 0) {
                if (m->gouraud) {
                    plot(m, x, y, r >> 16, g >> 16, bl >> 16, u >> 16, v >> 16);
                } else {
                    plot(m, x, y, a->r, a->g, a->b, u >> 16, v >> 16);
                }
            }
            w0 += A0;
            w1 += A1;
            w2 += A2;
            r += drdx, g += dgdx, bl += dbdx, u += dudx, v += dvdx;
        }
        w0row += B0;
        w1row += B1;
        w2row += B2;
        rrow += drdy, grow += dgdy, brow += dbdy, urow += dudy, vrow += dvdy;
    }
}

static void sg_poly(const Gp0Mode *m, const Gp0Vtx *v, int n) {
    if (!psx_vram) {
        return;
    }
    triangle(m, &v[0], &v[1], &v[2]);
    if (n == 4) {
        triangle(m, &v[1], &v[2], &v[3]);
    }
}

static void sg_line(const Gp0Mode *m, const Gp0Vtx *a, const Gp0Vtx *b) {
    if (!psx_vram) {
        return;
    }
    int dx = b->x - a->x, dy = b->y - a->y;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int n = adx > ady ? adx : ady;
    if (n >= 1024) {
        return;
    }
    for (int i = 0; i <= n; i++) {
        int x = n ? a->x + dx * i / n : a->x, y = n ? a->y + dy * i / n : a->y;
        if (x < gp0_state.area_x0 || x > gp0_state.area_x1 || y < gp0_state.area_y0 || y > gp0_state.area_y1) {
            continue;
        }
        int r = a->r, g = a->g, bl = a->b;
        if (m->gouraud && n) {
            r += (b->r - a->r) * i / n;
            g += (b->g - a->g) * i / n;
            bl += (b->b - a->b) * i / n;
        }
        plot(m, x, y, r, g, bl, 0, 0);
    }
}

static void sg_rect(const Gp0Mode *m, const Gp0Vtx *v, int w, int h) {
    if (!psx_vram) {
        return;
    }
    int x0 = v->x, y0 = v->y, x1 = v->x + w, y1 = v->y + h;
    int u0 = v->u - x0, v0 = v->v - y0;
    if (x0 < gp0_state.area_x0) x0 = gp0_state.area_x0;
    if (y0 < gp0_state.area_y0) y0 = gp0_state.area_y0;
    if (x1 > gp0_state.area_x1 + 1) x1 = gp0_state.area_x1 + 1;
    if (y1 > gp0_state.area_y1 + 1) y1 = gp0_state.area_y1 + 1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            plot(m, x, y, v->r, v->g, v->b, u0 + x, v0 + y);
        }
    }
}

const Gp0Backend psx_softgpu = {sg_poly, sg_line, sg_rect, 0, 0};

/* PC --render / DS debug: rasterise into the VRAM shadow */
void psx_softgpu_enable(int on) {
    gp0_backend = on ? &psx_softgpu : 0;
}

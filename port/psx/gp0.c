/* PS1 GPU command decoding (gp0.h), following psx-spx "GPU Render Polygon/Line/Rectangle Commands" and
 * "GPU Rendering Attributes". */
#include "gp0.h"

Gp0State gp0_state = {0, 0, 1023, 511, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const Gp0Backend *gp0_backend;

static inline int sext11(int v) {
    return (v << 21) >> 21;
}

static Gp0Vtx vtx(unsigned int xy, unsigned int col) {
    Gp0Vtx v = {0};
    v.x = sext11((int)(xy & 0x7FF)) + gp0_state.ofs_x;
    v.y = sext11((int)((xy >> 16) & 0x7FF)) + gp0_state.ofs_y;
    v.r = col & 0xFF;
    v.g = (col >> 8) & 0xFF;
    v.b = (col >> 16) & 0xFF;
    return v;
}

static void apply_texpage(Gp0Mode *m, unsigned int tp) {
    m->tx = (tp & 15) * 64;
    m->ty = ((tp >> 4) & 1) * 256;
    m->abr = (tp >> 5) & 3;
    m->depth = (tp >> 7) & 3;
    if (m->depth == 3) {
        m->depth = 2;
    }
}

static void set_clut(Gp0Mode *m, unsigned int uv) {
    m->clut_x = ((uv >> 16) & 0x3F) * 16;
    m->clut_y = (uv >> 22) & 0x1FF;
}

/* One GP0 command at w[0]; returns the words it used. */
static unsigned int command(const unsigned int *w, unsigned int avail) {
    unsigned int cmd = w[0] >> 24;
    Gp0Mode m = {0};
    apply_texpage(&m, gp0_state.texpage);

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
        Gp0Vtx v[4];
        unsigned int k = 1;
        for (int i = 0; i < nv; i++) {
            unsigned int col = (i == 0 || !m.gouraud) ? w[0] : w[k++];
            v[i] = vtx(w[k++], col);
            if (m.textured) {
                unsigned int uv = w[k++];
                v[i].u = uv & 0xFF;
                v[i].v = (uv >> 8) & 0xFF;
                if (i == 0) {
                    set_clut(&m, uv);
                } else if (i == 1) {
                    unsigned int tp = uv >> 16;
                    gp0_state.texpage = (gp0_state.texpage & ~0x9FFu) | (tp & 0x9FF);
                    apply_texpage(&m, tp);
                }
            }
        }
        if (gp0_backend && gp0_backend->poly) {
            gp0_backend->poly(&m, v, nv);
        }
        return need;
    }
    if (cmd >= 0x40 && cmd < 0x60) { /* line, polyline (terminated by 0x5xxx5xxx) */
        m.gouraud = (cmd >> 4) & 1;
        m.semi = (cmd >> 1) & 1;
        int poly = (cmd >> 3) & 1;
        unsigned int k = 1;
        Gp0Vtx prev = vtx(w[k++], w[0]);
        for (;;) {
            if (k >= avail) {
                return avail;
            }
            if (poly && (w[k] & 0xF000F000) == 0x50005000) {
                return k + 1;
            }
            unsigned int col = w[0];
            if (m.gouraud) {
                col = w[k++];
                if (k >= avail) {
                    return avail;
                }
            }
            Gp0Vtx next = vtx(w[k++], col);
            if (gp0_backend && gp0_backend->line) {
                gp0_backend->line(&m, &prev, &next);
            }
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
        Gp0Vtx v = vtx(w[1], w[0]);
        unsigned int k = 2;
        if (m.textured) {
            unsigned int uv = w[k++];
            v.u = uv & 0xFF;
            v.v = (uv >> 8) & 0xFF;
            set_clut(&m, uv);
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
        if (gp0_backend && gp0_backend->rect) {
            gp0_backend->rect(&m, &v, wd, ht);
        }
        return need;
    }
    switch (cmd) {
    case 0xE1:
        gp0_state.texpage = w[0] & 0xFFFFFF;
        break;
    case 0xE2:
        gp0_state.twin_mask_x = (w[0] & 31) * 8;
        gp0_state.twin_mask_y = ((w[0] >> 5) & 31) * 8;
        gp0_state.twin_off_x = ((w[0] >> 10) & 31) * 8;
        gp0_state.twin_off_y = ((w[0] >> 15) & 31) * 8;
        break;
    case 0xE3:
        gp0_state.area_x0 = w[0] & 0x3FF;
        gp0_state.area_y0 = (w[0] >> 10) & 0x1FF;
        break;
    case 0xE4:
        gp0_state.area_x1 = w[0] & 0x3FF;
        gp0_state.area_y1 = (w[0] >> 10) & 0x1FF;
        break;
    case 0xE5:
        gp0_state.ofs_x = sext11((int)(w[0] & 0x7FF));
        gp0_state.ofs_y = sext11((int)((w[0] >> 11) & 0x7FF));
        break;
    case 0xE6:
        gp0_state.set_mask = w[0] & 1;
        gp0_state.check_mask = (w[0] >> 1) & 1;
        break;
    case 0x02:
        if (avail >= 3 && gp0_backend && gp0_backend->fill) {
            gp0_backend->fill(w[1] & 0x3F0, (w[1] >> 16) & 0x1FF, ((w[2] & 0x3FF) + 15) & ~15, (w[2] >> 16) & 0x1FF,
                              w[0] & 0xFFFFFF);
        }
        return 3;
    case 0x80:
        return 4;
    default:
        break;
    }
    return 1;
}

void gp0_run(const unsigned int *w, unsigned int len) {
    for (unsigned int i = 0; i < len;) {
        i += command(w + i, len - i);
    }
}

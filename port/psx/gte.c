/* Software GTE (PS1 COP2). See gte.h.
 *
 * Register-level model following psx-spx: data registers d[0..31] and control registers c[0..31] keep their raw
 * 32-bit contents, and each command decodes the fields it needs. MAC1-3 are computed in 64 bits and checked for
 * the 44-bit overflow, and the perspective division uses the hardware's UNR (Newton-Raphson) table, so results
 * model the PS1 commands FF9 uses (RTPS/RTPT/NCLIP/AVSZ3/AVSZ4/MVMVA/OP/GPF/GPL/DPCS).
 * FLAG is maintained. The one simplification: overflow is checked on each final 44-bit sum rather than after
 * every partial addition, which only matters for FLAG on pathological inputs.
 */
#include <stdint.h>
#include "gte.h"

typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint16_t u16;
typedef uint32_t u32;

static struct {
    u32 d[32];
    u32 c[32];
} g;

/* ---- register views ------------------------------------------------------------------------------------- */

#define FLAG g.c[31]
#define V_X(n) ((i16)g.d[(n) * 2])
#define V_Y(n) ((i16)(g.d[(n) * 2] >> 16))
#define V_Z(n) ((i16)g.d[(n) * 2 + 1])
#define IR0 ((i16)g.d[8])
#define IR1 ((i16)g.d[9])
#define IR2 ((i16)g.d[10])
#define IR3 ((i16)g.d[11])
#define SX(n) ((i16)g.d[12 + (n)])
#define SY(n) ((i16)(g.d[12 + (n)] >> 16))
#define SZ(n) ((u16)g.d[16 + (n)])
#define MAC(n) ((i32)g.d[24 + (n)])
#define RGBC_R ((u32)(g.d[6] & 0xFF))
#define RGBC_G ((u32)((g.d[6] >> 8) & 0xFF))
#define RGBC_B ((u32)((g.d[6] >> 16) & 0xFF))
#define CODE ((u32)(g.d[6] >> 24))

/* a 3x3 matrix of s16 stored in five control registers starting at `base` (RT 0, LLM 8, LCM 16) */
static inline i32 mtx(int base, int row, int col) {
    int i = row * 3 + col;
    u32 w = g.c[base + (i >> 1)];
    return (i & 1) ? (i16)(w >> 16) : (i16)w;
}

#define TRX ((i32)g.c[5])
#define TRY ((i32)g.c[6])
#define TRZ ((i32)g.c[7])
#define RBK ((i32)g.c[13])
#define GBK ((i32)g.c[14])
#define BBK ((i32)g.c[15])
#define RFC ((i32)g.c[21])
#define GFC ((i32)g.c[22])
#define BFC ((i32)g.c[23])
#define OFX ((i32)g.c[24])
#define OFY ((i32)g.c[25])
#define H ((u16)g.c[26])
#define DQA ((i16)g.c[27])
#define DQB ((i32)g.c[28])
#define ZSF3 ((i16)g.c[29])
#define ZSF4 ((i16)g.c[30])

/* ---- saturation and flags ------------------------------------------------------------------------------- */

/* MAC1-3: 44-bit overflow check, then the shifted value truncated to 32 bits. */
static inline i32 set_mac(int n, i64 v, int shift) {
    if (v >= ((i64)1 << 43)) FLAG |= 1u << (31 - n);      /* bits 30/29/28 */
    else if (v < -((i64)1 << 43)) FLAG |= 1u << (28 - n); /* bits 27/26/25 */
    g.d[24 + n] = (u32)(i32)(v >> shift);
    return (i32)(v >> shift);
}

/* MAC0: 32-bit overflow check. */
static inline i64 set_mac0(i64 v) {
    if (v > 0x7FFFFFFFLL) FLAG |= 1u << 16;
    else if (v < -0x80000000LL) FLAG |= 1u << 15;
    g.d[24] = (u32)(i32)v;
    return v;
}

/* IR1-3 */
static inline void set_ir(int n, i32 v, int lm) {
    i32 lo = lm ? 0 : -0x8000;
    if (v > 0x7FFF) { v = 0x7FFF; FLAG |= 1u << (25 - n); } /* bits 24/23/22 */
    else if (v < lo) { v = lo; FLAG |= 1u << (25 - n); }
    g.d[8 + n] = (u32)(i32)v;
}

static inline void set_ir0(i32 v) {
    if (v > 0x1000) { v = 0x1000; FLAG |= 1u << 12; }
    else if (v < 0) { v = 0; FLAG |= 1u << 12; }
    g.d[8] = (u32)v;
}

static inline u32 lim_color(int n, i32 v) {
    if (v > 0xFF) { FLAG |= 1u << (22 - n); return 0xFF; } /* bits 21/20/19 */
    if (v < 0) { FLAG |= 1u << (22 - n); return 0; }
    return (u32)v;
}

static inline u32 lim_sz(i64 v) {
    if (v > 0xFFFF) { FLAG |= 1u << 18; return 0xFFFF; }
    if (v < 0) { FLAG |= 1u << 18; return 0; }
    return (u32)v;
}

static inline i32 lim_sxy(int bit, i64 v) {
    if (v > 0x3FF) { FLAG |= 1u << bit; return 0x3FF; }
    if (v < -0x400) { FLAG |= 1u << bit; return -0x400; }
    return (i32)v;
}

static inline void push_sz(u32 z) {
    g.d[16] = g.d[17];
    g.d[17] = g.d[18];
    g.d[18] = g.d[19];
    g.d[19] = z;
}

static inline void push_sxy(i32 x, i32 y) {
    g.d[12] = g.d[13];
    g.d[13] = g.d[14];
    g.d[14] = ((u32)y << 16) | ((u32)x & 0xFFFF);
}

static inline void push_color(void) {
    u32 r = lim_color(1, MAC(1) >> 4);
    u32 gr = lim_color(2, MAC(2) >> 4);
    u32 b = lim_color(3, MAC(3) >> 4);
    g.d[20] = g.d[21];
    g.d[21] = g.d[22];
    g.d[22] = (CODE << 24) | (b << 16) | (gr << 8) | r;
}

static inline void finish_flag(void) {
    if (FLAG & 0x7F87E000) FLAG |= 0x80000000;
}

/* ---- UNR division (H / SZ3) ----------------------------------------------------------------------------- */

static u16 unr_table[0x101];

static void init_unr(void) {
    for (int i = 0; i < 0x101; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unr_table[i] = v > 0 ? (u16)v : 0;
    }
}

static u32 gte_divide(u32 h, u32 sz3) {
    if (unr_table[0] == 0) init_unr();
    if (h >= sz3 * 2) {
        FLAG |= 1u << 17;
        return 0x1FFFF;
    }
    int z = __builtin_clz(sz3) - 16; /* sz3 > 0 here since h < sz3*2 */
    u32 n = h << z;
    u32 d = sz3 << z;
    u32 u = unr_table[(d - 0x7FC0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    uint64_t q = ((uint64_t)n * d + 0x8000) >> 16;
    return q > 0x1FFFF ? 0x1FFFF : (u32)q;
}

/* ---- register access ------------------------------------------------------------------------------------ */

void gte_mtc2(int reg, gte_u32 v) {
    switch (reg) {
    case 15: /* SXYP: push */
        g.d[12] = g.d[13];
        g.d[13] = g.d[14];
        g.d[14] = v;
        return;
    case 28: /* IRGB: expand to IR1-3 */
        g.d[9] = (v & 0x1F) << 7;
        g.d[10] = ((v >> 5) & 0x1F) << 7;
        g.d[11] = ((v >> 10) & 0x1F) << 7;
        return;
    case 29: /* ORGB: read-only */
    case 31: /* LZCR: read-only */
        return;
    case 30: { /* LZCS: count leading sign bits into LZCR */
        u32 x = (v & 0x80000000) ? ~v : v;
        g.d[30] = v;
        g.d[31] = x ? (u32)__builtin_clz(x) : 32;
        return;
    }
    default:
        g.d[reg] = v;
    }
}

gte_u32 gte_mfc2(int reg) {
    switch (reg) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        return (u32)(i32)(i16)g.d[reg];
    case 7: case 16: case 17: case 18: case 19:
        return g.d[reg] & 0xFFFF;
    case 15:
        return g.d[14];
    case 28: case 29: {
        i32 r = IR1 >> 7, gg = IR2 >> 7, b = IR3 >> 7;
        r = r < 0 ? 0 : r > 0x1F ? 0x1F : r;
        gg = gg < 0 ? 0 : gg > 0x1F ? 0x1F : gg;
        b = b < 0 ? 0 : b > 0x1F ? 0x1F : b;
        return (u32)(r | (gg << 5) | (b << 10));
    }
    default:
        return g.d[reg];
    }
}

void gte_ctc2(int reg, gte_u32 v) {
    if (reg == 31) {
        FLAG = v & 0x7FFFF000;
        finish_flag();
        return;
    }
    g.c[reg] = v;
}

gte_u32 gte_cfc2(int reg) {
    switch (reg) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        return (u32)(i32)(i16)g.c[reg]; /* H (26) also reads sign-extended on hardware */
    default:
        return g.c[reg];
    }
}

/* ---- commands ------------------------------------------------------------------------------------------- */

static void rtp(int v, int sf, int lm, int last) {
    int shift = sf * 12;
    i64 x = V_X(v), y = V_Y(v), z = V_Z(v);
    i64 m1 = ((i64)TRX * 4096) + mtx(0, 0, 0) * x + mtx(0, 0, 1) * y + mtx(0, 0, 2) * z;
    i64 m2 = ((i64)TRY * 4096) + mtx(0, 1, 0) * x + mtx(0, 1, 1) * y + mtx(0, 1, 2) * z;
    i64 m3 = ((i64)TRZ * 4096) + mtx(0, 2, 0) * x + mtx(0, 2, 1) * y + mtx(0, 2, 2) * z;
    set_ir(1, set_mac(1, m1, shift), lm);
    set_ir(2, set_mac(2, m2, shift), lm);
    {
        /* IR3 saturates on MAC3, but its flag follows the unshifted value >> 12 (psx-spx "RTPS/RTPT IR3"). */
        i32 mac3 = set_mac(3, m3, shift);
        u32 saved = FLAG;
        set_ir(3, mac3, lm);
        FLAG = saved;
        i64 z12 = m3 >> 12;
        if (z12 > 0x7FFF || z12 < -0x8000) FLAG |= 1u << 22;
        push_sz(lim_sz(m3 >> 12));
    }
    u32 div = gte_divide(H, SZ(3));
    i64 sx = set_mac0((i64)IR1 * div + OFX) >> 16;
    i64 sy = set_mac0((i64)IR2 * div + OFY) >> 16;
    push_sxy(lim_sxy(14, sx), lim_sxy(13, sy));
    if (last) {
        i64 m0 = set_mac0((i64)DQA * div + DQB);
        set_ir0((i32)(m0 >> 12));
    }
}

/* [MAC1..3] = (T << 12 + M * V) >> shift, IR = MAC (MVMVA and the lighting steps) */
static void mat_mul(int mbase, i64 vx, i64 vy, i64 vz, i64 tx, i64 ty, i64 tz, int sf, int lm) {
    int shift = sf * 12;
    set_ir(1, set_mac(1, (tx * 4096) + mtx(mbase, 0, 0) * vx + mtx(mbase, 0, 1) * vy + mtx(mbase, 0, 2) * vz, shift), lm);
    set_ir(2, set_mac(2, (ty * 4096) + mtx(mbase, 1, 0) * vx + mtx(mbase, 1, 1) * vy + mtx(mbase, 1, 2) * vz, shift), lm);
    set_ir(3, set_mac(3, (tz * 4096) + mtx(mbase, 2, 0) * vx + mtx(mbase, 2, 1) * vy + mtx(mbase, 2, 2) * vz, shift), lm);
}

/* Interpolate MAC (in .12 colour units) toward the far colour by IR0, then push the colour FIFO. */
static void interpolate(i64 m1, i64 m2, i64 m3, int sf, int lm) {
    int shift = sf * 12;
    set_ir(1, set_mac(1, ((i64)RFC * 4096) - m1, shift), 0);
    set_ir(2, set_mac(2, ((i64)GFC * 4096) - m2, shift), 0);
    set_ir(3, set_mac(3, ((i64)BFC * 4096) - m3, shift), 0);
    set_ir(1, set_mac(1, (i64)IR1 * IR0 + m1, shift), lm);
    set_ir(2, set_mac(2, (i64)IR2 * IR0 + m2, shift), lm);
    set_ir(3, set_mac(3, (i64)IR3 * IR0 + m3, shift), lm);
    push_color();
}

/* Colour times IR (<< 4), as the second half of NCCS/NCDS/CC/CDP/DCPL. */
static void color_ir(u32 r, u32 gr, u32 b, i64 *m1, i64 *m2, i64 *m3) {
    *m1 = (i64)r * IR1 * 16;
    *m2 = (i64)gr * IR2 * 16;
    *m3 = (i64)b * IR3 * 16;
}

static void light(int v, int sf, int lm, int kind) {
    int shift = sf * 12;
    i64 m1, m2, m3;
    mat_mul(8, V_X(v), V_Y(v), V_Z(v), 0, 0, 0, sf, lm);  /* LLM * V */
    mat_mul(16, IR1, IR2, IR3, RBK, GBK, BBK, sf, lm);    /* BK + LCM * IR */
    if (kind == 0) { /* NCS/NCT */
        push_color();
        return;
    }
    color_ir(RGBC_R, RGBC_G, RGBC_B, &m1, &m2, &m3);
    if (kind == 1) { /* NCCS/NCCT */
        set_ir(1, set_mac(1, m1, shift), lm);
        set_ir(2, set_mac(2, m2, shift), lm);
        set_ir(3, set_mac(3, m3, shift), lm);
        push_color();
    } else { /* NCDS/NCDT */
        interpolate(m1, m2, m3, sf, lm);
    }
}

void gte_cmd(gte_u32 op) {
    int sf = (op >> 19) & 1;
    int shift = sf * 12;
    int lm = (op >> 10) & 1;
    i64 m1, m2, m3;

    FLAG = 0;
    switch (op & 0x3F) {
    case 0x01: /* RTPS */
        rtp(0, sf, lm, 1);
        break;
    case 0x30: /* RTPT */
        rtp(0, sf, lm, 0);
        rtp(1, sf, lm, 0);
        rtp(2, sf, lm, 1);
        break;
    case 0x06: /* NCLIP */
        set_mac0((i64)SX(0) * SY(1) + (i64)SX(1) * SY(2) + (i64)SX(2) * SY(0) - (i64)SX(0) * SY(2) -
                 (i64)SX(1) * SY(0) - (i64)SX(2) * SY(1));
        break;
    case 0x2D: { /* AVSZ3 */
        i64 m0 = set_mac0((i64)ZSF3 * (SZ(1) + SZ(2) + SZ(3)));
        g.d[7] = lim_sz(m0 >> 12);
        break;
    }
    case 0x2E: { /* AVSZ4 */
        i64 m0 = set_mac0((i64)ZSF4 * (SZ(0) + SZ(1) + SZ(2) + SZ(3)));
        g.d[7] = lim_sz(m0 >> 12);
        break;
    }
    case 0x12: { /* MVMVA */
        int mx = (op >> 17) & 3, v = (op >> 15) & 3, cv = (op >> 13) & 3;
        i64 vx, vy, vz, tx = 0, ty = 0, tz = 0;
        if (v == 3) { vx = IR1; vy = IR2; vz = IR3; }
        else { vx = V_X(v); vy = V_Y(v); vz = V_Z(v); }
        if (cv == 0) { tx = TRX; ty = TRY; tz = TRZ; }
        else if (cv == 1) { tx = RBK; ty = GBK; tz = BBK; }
        else if (cv == 2) { tx = RFC; ty = GFC; tz = BFC; } /* hardware bug with FC not modelled */
        if (mx == 3) {
            /* "garbage" matrix: [-R<<4, R<<4, IR0], [RT13 x3], [RT22 x3] */
            i64 r = (i64)RGBC_R << 4;
            set_ir(1, set_mac(1, (tx * 4096) - r * vx + r * vy + IR0 * vz, shift), lm);
            set_ir(2, set_mac(2, (ty * 4096) + mtx(0, 0, 2) * (vx + vy + vz), shift), lm);
            set_ir(3, set_mac(3, (tz * 4096) + mtx(0, 1, 1) * (vx + vy + vz), shift), lm);
        } else {
            mat_mul(mx * 8, vx, vy, vz, tx, ty, tz, sf, lm);
        }
        break;
    }
    case 0x0C: { /* OP: cross product with the RT diagonal */
        i64 d1 = mtx(0, 0, 0), d2 = mtx(0, 1, 1), d3 = mtx(0, 2, 2);
        /* All three products read the input IR vector before any result is
         * written back. Camera basis construction depends on this simultaneity. */
        i64 x = IR1, y = IR2, z = IR3;
        m1 = z * d2 - y * d3;
        m2 = x * d3 - z * d1;
        m3 = y * d1 - x * d2;
        set_ir(1, set_mac(1, m1, shift), lm);
        set_ir(2, set_mac(2, m2, shift), lm);
        set_ir(3, set_mac(3, m3, shift), lm);
        break;
    }
    case 0x3D: /* GPF */
        set_ir(1, set_mac(1, (i64)IR0 * IR1, shift), lm);
        set_ir(2, set_mac(2, (i64)IR0 * IR2, shift), lm);
        set_ir(3, set_mac(3, (i64)IR0 * IR3, shift), lm);
        push_color();
        break;
    case 0x3E: /* GPL */
        set_ir(1, set_mac(1, ((i64)MAC(1) * ((i64)1 << shift)) + (i64)IR0 * IR1, shift), lm);
        set_ir(2, set_mac(2, ((i64)MAC(2) * ((i64)1 << shift)) + (i64)IR0 * IR2, shift), lm);
        set_ir(3, set_mac(3, ((i64)MAC(3) * ((i64)1 << shift)) + (i64)IR0 * IR3, shift), lm);
        push_color();
        break;
    case 0x10: /* DPCS */
        interpolate((i64)RGBC_R << 16, (i64)RGBC_G << 16, (i64)RGBC_B << 16, sf, lm);
        break;
    case 0x2A: /* DPCT: DPCS on RGB0 three times */
        for (int i = 0; i < 3; i++) {
            u32 c = g.d[20];
            interpolate((i64)(c & 0xFF) << 16, (i64)((c >> 8) & 0xFF) << 16, (i64)((c >> 16) & 0xFF) << 16, sf, lm);
        }
        break;
    case 0x11: /* INTPL */
        interpolate((i64)IR1 * 4096, (i64)IR2 * 4096, (i64)IR3 * 4096, sf, lm);
        break;
    case 0x29: /* DCPL */
        color_ir(RGBC_R, RGBC_G, RGBC_B, &m1, &m2, &m3);
        interpolate(m1, m2, m3, sf, lm);
        break;
    case 0x28: /* SQR */
        set_ir(1, set_mac(1, (i64)IR1 * IR1, shift), lm);
        set_ir(2, set_mac(2, (i64)IR2 * IR2, shift), lm);
        set_ir(3, set_mac(3, (i64)IR3 * IR3, shift), lm);
        break;
    case 0x1E: light(0, sf, lm, 0); break; /* NCS */
    case 0x20: light(0, sf, lm, 0); light(1, sf, lm, 0); light(2, sf, lm, 0); break; /* NCT */
    case 0x1B: light(0, sf, lm, 1); break; /* NCCS */
    case 0x3F: light(0, sf, lm, 1); light(1, sf, lm, 1); light(2, sf, lm, 1); break; /* NCCT */
    case 0x13: light(0, sf, lm, 2); break; /* NCDS */
    case 0x16: light(0, sf, lm, 2); light(1, sf, lm, 2); light(2, sf, lm, 2); break; /* NCDT */
    case 0x1C: /* CC */
        mat_mul(16, IR1, IR2, IR3, RBK, GBK, BBK, sf, lm);
        color_ir(RGBC_R, RGBC_G, RGBC_B, &m1, &m2, &m3);
        set_ir(1, set_mac(1, m1, shift), lm);
        set_ir(2, set_mac(2, m2, shift), lm);
        set_ir(3, set_mac(3, m3, shift), lm);
        push_color();
        break;
    case 0x14: /* CDP */
        mat_mul(16, IR1, IR2, IR3, RBK, GBK, BBK, sf, lm);
        color_ir(RGBC_R, RGBC_G, RGBC_B, &m1, &m2, &m3);
        interpolate(m1, m2, m3, sf, lm);
        break;
    default:
        break;
    }
    finish_flag();
}

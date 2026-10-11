/* libgte on the software GTE (gte.c).
 *
 * Each function issues the GTE operations libgte does, so both its results and its side effects on GTE state
 * (RT/TR overwritten by ApplyMatrix, the SXY/SZ FIFOs, IR0, FLAG) match what the game sees after the call.
 * Command words are the ones libgte and the decomp's macros use: RTPS 0x0180001, RTPT 0x0280030, NCLIP 0x1400006,
 * AVSZ4 0x168002E, MVMVA rt*v0 0x0486012 / rt*v0+tr 0x0480012 / rt*ir 0x049E012, OP sf=0 0x170000C.
 */
#include "gte.h"
#include "psyq_port.h"

#define RTPS 0x0180001
#define RTPT 0x0280030
#define NCLIP 0x1400006
#define AVSZ4 0x168002E
#define MVMVA_RT_V0 0x0486012
#define MVMVA_RT_V0_TR 0x0480012
#define MVMVA_RT_IR 0x049E012
#define OP_SF0 0x170000C

static void load_matrix(int base, const MATRIX *m) {
    const u32 *w = (const u32 *)m->m;
    gte_ctc2(base + 0, w[0]);
    gte_ctc2(base + 1, w[1]);
    gte_ctc2(base + 2, w[2]);
    gte_ctc2(base + 3, w[3]);
    gte_ctc2(base + 4, (u16)m->m[2][2]);
}

static void load_v0(const SVECTOR *v) {
    gte_mtc2(0, ((u32)(u16)v->vy << 16) | (u16)v->vx);
    gte_mtc2(1, (u16)v->vz);
}

void InitGeom(void) {
    gte_ctc2(29, 0x155); /* ZSF3 */
    gte_ctc2(30, 0x100); /* ZSF4 */
    gte_ctc2(26, 1000);  /* H */
    gte_ctc2(27, 0xFFFFEF9E); /* DQA */
    gte_ctc2(28, 0x1400000);  /* DQB */
    gte_ctc2(24, 0);
    gte_ctc2(25, 0);
}

void SetGeomOffset(long ofx, long ofy) {
    gte_ctc2(24, (u32)ofx << 16);
    gte_ctc2(25, (u32)ofy << 16);
}

void ReadGeomOffset(long *ofx, long *ofy) {
    *ofx = (s32)gte_cfc2(24) >> 16;
    *ofy = (s32)gte_cfc2(25) >> 16;
}

/* SetGeomScreen / ReadGeomScreen (decomp placeholders): ctc2/cfc2 H. */
void func_800639D4(s32 h) {
    gte_ctc2(26, (u32)h);
}

s32 func_800645C4(void) {
    return (s32)gte_cfc2(26);
}

void SetRotMatrix(MATRIX *m) {
    load_matrix(0, m);
}

void SetLightMatrix(MATRIX *m) {
    load_matrix(8, m);
}

void SetColorMatrix(MATRIX *m) {
    load_matrix(16, m);
}

void SetTransMatrix(MATRIX *m) {
    gte_ctc2(5, (u32)m->t[0]);
    gte_ctc2(6, (u32)m->t[1]);
    gte_ctc2(7, (u32)m->t[2]);
}

void SetBackColor(s32 r, s32 g, s32 b) {
    gte_ctc2(13, (u32)r << 4);
    gte_ctc2(14, (u32)g << 4);
    gte_ctc2(15, (u32)b << 4);
}

void SetFarColor(s32 r, s32 g, s32 b) {
    gte_ctc2(21, (u32)r << 4);
    gte_ctc2(22, (u32)g << 4);
    gte_ctc2(23, (u32)b << 4);
}

/* Depth cueing from near/far distances a, b at screen distance h (libgte SetFogNearFar). */
void SetFogNearFar(s32 a, s32 b, s32 h) {
    if (b - a < 100) {
        return;
    }
    s32 dqa = (-a * b / (b - a) << 8) / h;
    if (dqa > 0x7FFF) dqa = 0x7FFF;
    if (dqa < -0x8000) dqa = -0x8000;
    s32 dqb = ((b << 12) / (b - a)) << 12;
    gte_ctc2(27, (u32)dqa);
    gte_ctc2(28, (u32)dqb);
}

/* ---- transforms ----------------------------------------------------------------------------------------- */

long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag) {
    load_v0(v0);
    gte_cmd(RTPS);
    *sxy = (long)gte_mfc2(14);
    *p = (long)gte_mfc2(8);
    *flag = (long)gte_cfc2(31);
    return (long)(gte_mfc2(19) >> 2);
}

long RotTransPers4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, long *sxy0, long *sxy1, long *sxy2,
                   long *sxy3, long *p, long *flag) {
    load_v0(v0);
    gte_mtc2(2, ((u32)(u16)v1->vy << 16) | (u16)v1->vx);
    gte_mtc2(3, (u16)v1->vz);
    gte_mtc2(4, ((u32)(u16)v2->vy << 16) | (u16)v2->vx);
    gte_mtc2(5, (u16)v2->vz);
    gte_cmd(RTPT);
    *sxy0 = (long)gte_mfc2(12);
    *sxy1 = (long)gte_mfc2(13);
    *sxy2 = (long)gte_mfc2(14);
    load_v0(v3);
    gte_cmd(RTPS);
    *sxy3 = (long)gte_mfc2(14);
    *p = (long)gte_mfc2(8);
    *flag = (long)gte_cfc2(31);
    gte_cmd(AVSZ4);
    return (long)gte_mfc2(7);
}

void RotTrans(SVECTOR *v0, VECTOR *v1, long *flag) {
    load_v0(v0);
    gte_cmd(MVMVA_RT_V0_TR);
    v1->vx = (s32)gte_mfc2(25);
    v1->vy = (s32)gte_mfc2(26);
    v1->vz = (s32)gte_mfc2(27);
    *flag = (long)gte_cfc2(31);
}

VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1) {
    load_matrix(0, m);
    load_v0(v0);
    gte_cmd(MVMVA_RT_V0);
    v1->vx = (s32)gte_mfc2(25);
    v1->vy = (s32)gte_mfc2(26);
    v1->vz = (s32)gte_mfc2(27);
    return v1;
}

/* m * v with 32-bit v (libgte splits v into 15-bit halves; this is the same sum without the split). */
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1) {
    long long x = v0->vx, y = v0->vy, z = v0->vz;
    s32 r[3];
    for (int i = 0; i < 3; i++) {
        r[i] = (s32)((m->m[i][0] * x + m->m[i][1] * y + m->m[i][2] * z) >> 12);
    }
    v1->vx = r[0], v1->vy = r[1], v1->vz = r[2];
    return v1;
}

/* dst = RT * columns of src, column by column through IR (as the decomp's gte_MulMatrix0 macro does) */
static void rt_times(const MATRIX *src, MATRIX *dst) {
    MATRIX tmp;
    for (int c = 0; c < 3; c++) {
        gte_mtc2(9, (u16)src->m[0][c]);
        gte_mtc2(10, (u16)src->m[1][c]);
        gte_mtc2(11, (u16)src->m[2][c]);
        gte_cmd(MVMVA_RT_IR);
        tmp.m[0][c] = (s16)gte_mfc2(9);
        tmp.m[1][c] = (s16)gte_mfc2(10);
        tmp.m[2][c] = (s16)gte_mfc2(11);
    }
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            dst->m[r][c] = tmp.m[r][c];
        }
    }
}

/* m1 = m0 * m1 */
MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1) {
    load_matrix(0, m0);
    rt_times(m1, m1);
    return m1;
}

/* m0 = RT * m0 (RT is the current rotation matrix) */
MATRIX *MulRotMatrix(MATRIX *m0) {
    rt_times(m0, m0);
    return m0;
}

/* m2 = m0 * m1, with translation m2.t = m0 * m1.t + m0.t */
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2) {
    MATRIX r;
    load_matrix(0, m0);
    rt_times(m1, &r);
    long long t[3];
    for (int i = 0; i < 3; i++) {
        t[i] = ((m0->m[i][0] * (long long)m1->t[0] + m0->m[i][1] * (long long)m1->t[1] +
                 m0->m[i][2] * (long long)m1->t[2]) >> 12) + m0->t[i];
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m2->m[i][j] = r.m[i][j];
        }
        m2->t[i] = (s32)t[i];
    }
    return m2;
}

MATRIX *TransMatrix(MATRIX *m, VECTOR *v) {
    m->t[0] = v->vx;
    m->t[1] = v->vy;
    m->t[2] = v->vz;
    return m;
}

/* Scale column j by v[j] (.12). */
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v) {
    const s32 s[3] = {v->vx, v->vy, v->vz};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            m->m[i][j] = (s16)((m->m[i][j] * s[j]) >> 12);
        }
    }
    return m;
}

#define MATRIX_STACK 20
static u32 mstack[MATRIX_STACK][8];
static int msp;

void PushMatrix(void) {
    if (msp >= MATRIX_STACK) {
        port_fatal("PushMatrix: stack overflow");
    }
    for (int i = 0; i < 8; i++) {
        mstack[msp][i] = gte_cfc2(i);
    }
    msp++;
}

void PopMatrix(void) {
    if (msp <= 0) {
        port_fatal("PopMatrix: stack underflow");
    }
    msp--;
    for (int i = 0; i < 8; i++) {
        gte_ctc2(i, mstack[msp][i]);
    }
}

/* ---- vector math ---------------------------------------------------------------------------------------- */

long SquareRoot0(long a) {
    unsigned long v = (unsigned long)a, r = 0, bit = 1UL << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (long)r;
}

static void normalize(const VECTOR *v0, s32 out[3], long long *len2) {
    long long x = v0->vx, y = v0->vy, z = v0->vz;
    *len2 = x * x + y * y + z * z;
    long long len = 0;
    if (*len2) {
        /* integer sqrt of a 64-bit value */
        unsigned long long v = (unsigned long long)*len2, r = 0, bit = 1ULL << 62;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
            bit >>= 2;
        }
        len = (long long)r;
    }
    for (int i = 0; i < 3; i++) {
        long long c = i == 0 ? x : i == 1 ? y : z;
        out[i] = len ? (s32)((c << 12) / len) : 0;
    }
}

int VectorNormal(VECTOR *v0, VECTOR *v1) {
    s32 n[3];
    long long len2;
    normalize(v0, n, &len2);
    v1->vx = n[0], v1->vy = n[1], v1->vz = n[2];
    return (int)len2;
}

int VectorNormalS(VECTOR *v0, SVECTOR *v1) {
    s32 n[3];
    long long len2;
    normalize(v0, n, &len2);
    v1->vx = (s16)n[0], v1->vy = (s16)n[1], v1->vz = (s16)n[2];
    return (int)len2;
}

/* v2 = v0 x v1 (OP with RT's diagonal = v0, IR = v1, sf = 0) */
void OuterProduct0(VECTOR *v0, VECTOR *v1, VECTOR *v2) {
    gte_ctc2(0, (u16)v0->vx);
    gte_ctc2(2, (u16)v0->vy);
    gte_ctc2(4, (u16)v0->vz);
    gte_mtc2(9, (u32)v1->vx);
    gte_mtc2(10, (u32)v1->vy);
    gte_mtc2(11, (u32)v1->vz);
    gte_cmd(OP_SF0);
    v2->vx = (s32)gte_mfc2(25);
    v2->vy = (s32)gte_mfc2(26);
    v2->vz = (s32)gte_mfc2(27);
}

long NormalClip(long sxy0, long sxy1, long sxy2) {
    gte_mtc2(12, (u32)sxy0);
    gte_mtc2(13, (u32)sxy1);
    gte_mtc2(14, (u32)sxy2);
    gte_cmd(NCLIP);
    return (long)(s32)gte_mfc2(24);
}

/* v2 = (v0 * p0 + v1 * p1) >> 12 per colour channel, rounded as libgte's GPF (sf = 1) then GPL (sf = 1) does:
 * the first product is truncated to .0 before the second is added. */
void LoadAverageCol(u8 *v0, u8 *v1, s32 p0, s32 p1, u8 *v2) {
    for (int i = 0; i < 3; i++) {
        s32 c = (((v0[i] * p0) >> 12 << 12) + v1[i] * p1) >> 12;
        v2[i] = (u8)(c < 0 ? 0 : c > 255 ? 255 : c);
    }
}

void LoadAverageShort12(SVECTOR *v0, SVECTOR *v1, s32 p0, s32 p1, SVECTOR *v2) {
    v2->vx = (s16)((v0->vx * p0 + v1->vx * p1) >> 12);
    v2->vy = (s16)((v0->vy * p0 + v1->vy * p1) >> 12);
    v2->vz = (s16)((v0->vz * p0 + v1->vz * p1) >> 12);
}

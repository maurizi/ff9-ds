/* The MBG / FMV player (decomp ovl_64: MBG_init, play, per-frame decode, stop), replaced for the DS.
 *
 * The PS1 player streams STR sectors and decodes them with the MDEC (libpress). The port plays transcoded movies
 * instead (FastVideoDS, M4). Until then a movie is HEADLESS_FRAMES long. As on the PS1, where the player's
 * interrupt callbacks advance it, the frame number (D_80071E34) moves on its own, at 15 fps from the frame
 * interrupt (psx_movie_tick); the game's per-frame call reports whether a new frame is ready. The game waits for
 * frame > 0 before it watches for the end.
 * Config: config/overrides.txt.
 */
#define M2CTX
#include "ovl_64.h"

#define HEADLESS_FRAMES 2

s32 o64_func_8019AAF8(void);

static s32 movie_end;
static s32 shown; /* last frame number reported by the per-frame call */
static int tick;

/* from psx_frame(): one 60 Hz frame */
void psx_movie_tick(void) {
    if (D_80071E32 >= 2 && D_80071E34 < movie_end && ++tick % 4 == 0) {
        D_80071E34++;
    }
}

/* MBG_init(mode, buffers) */
s32 o64_func_801999B4(s32 mode, u8 **buf) {
    D_80071E32 = 1;
    return 1;
}

/* start movie `movie` (0..4), entry `idx` */
s32 o64_func_80199B28(u32 movie, s32 idx) {
    if (movie >= 5 || idx < 0 || idx >= D_8019DC5C[movie].unk0) {
        return 0;
    }
    D_8019DCF8 = &D_8019DC5C[movie].unk4[idx];
    D_80071E30 = D_8019DCF8->unk5;
    D_80071E32 = 2;
    D_80071E34 = 0;
    movie_end = HEADLESS_FRAMES;
    shown = 0;
    tick = 0;
    return 1;
}

/* per frame: returns non-zero when a new frame was drawn */
s32 o64_func_80199F30(s32 arg0) {
    if (D_80071E32 < 2) {
        return 0;
    }
    if (shown == D_80071E34) {
        if (shown >= movie_end) {
            o64_func_8019AAF8(); /* the stream ended: the player stops itself (and runs the end callback) */
        }
        return 0;
    }
    shown = D_80071E34;
    return 1;
}

/* the movie's last frame number */
s32 o64_func_8019AD0C(void) {
    return D_80071E32 < 2 ? -1 : movie_end;
}

/* stop */
s32 o64_func_8019AAF8(void) {
    D_80071E32--;
    if (D_8019DC94.unk60 != NULL) {
        D_8019DC94.unk60();
    }
    return 0;
}

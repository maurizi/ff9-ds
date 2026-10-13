/* Native stack packets and arena packets must both survive 24-bit OT links. */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/mman.h>
#undef NULL
#include "psyq_port.h"
#include "gp0.h"

unsigned char psx_ram[PSX_RAM_SIZE] __attribute__((aligned(32)));
static unsigned int decoded[64], count;
Gp0State gp0_state;
void gp0_run(const unsigned int *words, unsigned int len) {
    assert(count + len <= 64);
    for (unsigned int i = 0; i < len; i++) decoded[count++] = words[i];
}
void port_log(const char *fmt, ...) { (void)fmt; }
void port_fatal(const char *fmt, ...) { (void)fmt; abort(); }
void psx_vram_touch(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void port_draw_ot_buffer(u32 *, const void *, unsigned int);

static void packet(u32 *p, u32 command) {
    setlen(p, 1);
    p[1] = command;
}
int main(void) {
    u32 native[5] = {0};
    u32 *arena = (u32 *)psx_ram;
    /* The battle-init layout: OT follows two stack packets in one native buffer. */
    packet(native, 0xE1000011);
    packet(native + 2, 0xE2000022);
    ClearOTag(native + 4, 1);
    AddPrim(native + 4, native);
    addPrim(native + 4, native + 2);
    port_draw_ot_buffer(native + 4, native, sizeof native);
    assert(count == 2 && decoded[0] == 0xE2000022 && decoded[1] == 0xE1000011);
    /* Arena lists retain their original interpretation. */
    count = 0;
    packet(arena, 0xE1000033);
    ClearOTagR(arena + 2, 3);
    AddPrim(arena + 3, arena);
    DrawOTag(arena + 4);
    assert(count == 1 && decoded[0] == 0xE1000033);
    /* A native head can link to arena packets too. */
    count = 0;
    ClearOTag(native + 4, 1);
    AddPrim(native + 4, arena);
    AddPrim(native + 4, native);
    port_draw_ot_buffer(native + 4, native, sizeof native);
    assert(count == 2 && decoded[0] == 0xE1000011 && decoded[1] == 0xE1000033);
    /* Force the stack-window ambiguity regardless of host ASLR. */
    u32 address = 0x16000000 | ((u32)psx_ram & 0xFFFFFF);
    void *page = (void *)(address & ~4095u);
    assert(mmap(page, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == page);
    u32 *alias = (u32 *)address;
    packet(alias, 0xE1000044);
    ClearOTag(alias + 2, 1); AddPrim(alias + 2, alias);
    count = 0;
    port_draw_ot_buffer(alias + 2, alias, 12);
    assert(count == 1 && decoded[0] == 0xE1000044);
    assert(munmap(page,8192)==0);
    /* Idle GPU still executes the UI insertion; optional continuation follows. */
    count = 0;
    ContinueDraw(arena + 4, BreakDraw());
    assert(count == 1 && decoded[0] == 0xE1000033);
    count = 0;
    ContinueDraw(arena + 4, (s32)(arena + 4));
    assert(count == 2 && decoded[0] == 0xE1000033 && decoded[1] == 0xE1000033);
    /* Snapshot actual GP0 state, not the last high-level DRAWENV or its clear. */
    DR_ENV snapshot = {0};
    gp0_state = (Gp0State){.area_x0=12,.area_y0=256,.area_x1=319,.area_y1=479,
        .ofs_x=-7,.ofs_y=256,.texpage=0x62A,.twin_mask_x=24,.twin_mask_y=56,
        .twin_off_x=40,.twin_off_y=72,.set_mask=1,.check_mask=1};
    GetDrawEnv2(&snapshot);
    assert(((P_TAG *)&snapshot)->len == 6);
    assert(snapshot.code[0] == (0xE3000000u | (256<<10) | 12));
    assert(snapshot.code[1] == (0xE4000000u | (479<<10) | 319));
    assert(snapshot.code[2] == (0xE5000000u | (256<<11) | ((unsigned)-7 & 0x7FF)));
    assert(snapshot.code[3] == 0xE100062Au);
    assert(snapshot.code[4] == (0xE2000000u | 3 | (7<<5) | (5<<10) | (9<<15)));
    assert(snapshot.code[5] == 0xE6000003u);
    /* Full-screen TIM loop follows an OT bucket's stored link on the CPU.
     * The sprite and texture-page packets splice into that linked bucket,
     * retaining reverse insertion order and the preceding empty OT entries. */
    u32 *tim_ot = arena + 32;
    u32 *sprite = arena + 64, *tpage = arena + 66;
    ClearOTagR(tim_ot, 12);
    assert(PSX_OT_PTR(tim_ot[8]) == tim_ot + 7);
    packet(sprite, 0x64000055);
    packet(tpage, 0xE1000066);
    addPrim(PSX_OT_PTR(tim_ot[8]), sprite);
    addPrim(PSX_OT_PTR(tim_ot[8]), tpage);
    count = 0;
    DrawOTag(tim_ot + 11);
    assert(count == 2 && decoded[0] == 0xE1000066 && decoded[1] == 0x64000055);
    return 0;
}

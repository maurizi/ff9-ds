/* Memory card (libcard / BIOS _card_*): M1 has no card, so every slot reads as empty. The file-backed 128 KB
 * card (ff9ds/cards/cardN.mcr, emulator-compatible) is M4. */
#include "psyq_port.h"

void InitCARD(long val) {
}

long StartCARD(void) {
    return 1;
}

long StopCARD(void) {
    return 1;
}

long _card_format(long chan) {
    return 0;
}

long _card_clear(long chan) {
    return 0;
}

long _card_load(long chan) {
    return 0;
}

/* ---- BIOS card services, as "no card inserted" until M4 ---------------------------------------------------
 * A card request completes by raising a card event the game then polls with TestEvent: SwCARD (0xF4000001)
 * with EvSpTIMOUT (0x0100) means no card answered. */
#define SW_CARD 0xF4000001
#define HW_CARD 0xF0000011
#define EV_SP_TIMOUT 0x0100

void DeliverEvent(unsigned long desc, long spec);

static void card_timeout(void) {
    DeliverEvent(SW_CARD, EV_SP_TIMOUT);
    DeliverEvent(HW_CARD, EV_SP_TIMOUT);
}

/* A(70h) _bu_init (decomp placeholder func_80016BA0) */
void func_80016BA0(void) {
}

/* A(ABh) _card_info (func_80065218) */
long func_80065218(long chan) {
    card_timeout();
    return 1;
}

/* B(5Dh) _card_wait (func_80065228) */
long func_80065228(long chan) {
    return 1;
}

/* B(32h) open, B(33h) lseek, B(34h) read, B(35h) write, B(36h) close (func_80065E60 ... func_80065EA0),
 * B(42h) firstfile, B(43h) nextfile (func_80065EB0): the card file system. No card: every call fails. */
long func_80065E60(const char *name, long mode) {
    return -1;
}

s32 func_80065E70(s32 fd, s32 offset, s32 whence) {
    return -1;
}

s32 func_80065E80(s32 fd, s32 buf, s32 n) {
    return -1;
}

s32 func_80065E90(s32 fd, s32 buf, s32 n) {
    return -1;
}

s32 func_80065EA0(s32 fd) {
    return -1;
}

s32 func_80065EB0(s32 dir) {
    return 0;
}

void *firstfile(char *name, void *dir) {
    return 0;
}

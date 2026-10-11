/* libpad: the buffers PadInitDirect registered are refreshed once per frame, in the format libpad writes:
 * [0] 0x00 = data valid (0xFF = no pad), [1] (type << 4) | half-words (0x41 = digital), [2..3] buttons, active
 * low. Only port 1 has a pad. */
#include "psyq_port.h"

static u8 *pad_buf[2];

__attribute__((weak)) unsigned int plat_pad_buttons(int port) {
    return 0;
}

void PadInitDirect(u8 *pad1, u8 *pad2) {
    pad_buf[0] = pad1;
    pad_buf[1] = pad2;
}

int PadStartCom(void) {
    return 1;
}

void psx_pad_update(void) {
    for (int port = 0; port < 2; port++) {
        u8 *b = pad_buf[port];
        if (!b) {
            continue;
        }
        if (port == 0) {
            unsigned int keys = ~plat_pad_buttons(0) & 0xFFFF;
            b[0] = 0x00;
            b[1] = 0x41;
            b[2] = (u8)keys;
            b[3] = (u8)(keys >> 8);
        } else {
            b[0] = 0xFF;
        }
    }
}

/* PadGetState: PadStateStable (6) for port 1 (0x00), PadStateDiscon (0) otherwise. */
int PadGetState(int port) {
    return port == 0x00 ? 6 : 0;
}

/* PadInfoMode: InfoModeCurID (1) -> 4 (digital pad); InfoModeCurExID (2) -> 0; InfoModeIdTable (4) -> 0 entries */
int PadInfoMode(int port, int term, int offs) {
    if (port != 0x00) {
        return 0;
    }
    return term == 1 ? 4 : 0;
}

int PadSetMainMode(int port, int offs, int lock) {
    return 0;
}

void PadSetAct(int port, unsigned char *data, int len) {
}

int PadSetActAlign(int port, unsigned char *data) {
    return 1;
}

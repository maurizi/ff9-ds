/* Scripted pad input for automated runs: "BUTTON@FRAME[+FRAMES]" entries (PC: --press; DS: PORT_AUTOPRESS at build
 * time). The platforms add these presses to the real pad state. */
#include <stdio.h>
#include <string.h>

#include "input_script.h"

static const struct {
    const char *name;
    unsigned int bit;
} buttons[] = {
    {"SELECT", 0x0001}, {"START", 0x0008}, {"UP", 0x0010}, {"RIGHT", 0x0020}, {"DOWN", 0x0040},
    {"LEFT", 0x0080}, {"L2", 0x0100}, {"R2", 0x0200}, {"L1", 0x0400}, {"R1", 0x0800},
    {"TRIANGLE", 0x1000}, {"CIRCLE", 0x2000}, {"CROSS", 0x4000}, {"SQUARE", 0x8000},
};

#define MAX_PRESSES 4096
static struct {
    unsigned int bit;
    long from, to;
} presses[MAX_PRESSES];
static int n_presses;

int input_script_add(const char *arg) {
    char name[16];
    long at = 0, dur = 6;
    if (sscanf(arg, "%15[A-Z0-9]@%ld+%ld", name, &at, &dur) < 2 || n_presses == MAX_PRESSES) {
        return 0;
    }
    for (unsigned int i = 0; i < sizeof buttons / sizeof buttons[0]; i++) {
        if (!strcmp(name, buttons[i].name)) {
            presses[n_presses].bit = buttons[i].bit;
            presses[n_presses].from = at;
            presses[n_presses].to = at + dur;
            n_presses++;
            return 1;
        }
    }
    return 0;
}

int input_script_add_list(const char *list) {
    char item[64];
    while (*list) {
        size_t n = strcspn(list, ", ");
        if (n && n < sizeof item) {
            memcpy(item, list, n);
            item[n] = 0;
            if (!input_script_add(item)) {
                return 0;
            }
        }
        list += n;
        list += strspn(list, ", ");
    }
    return 1;
}

unsigned int input_script_buttons(long frame) {
    unsigned int b = 0;
    for (int i = 0; i < n_presses; i++) {
        if (frame >= presses[i].from && frame < presses[i].to) {
            b |= presses[i].bit;
        }
    }
    return b;
}

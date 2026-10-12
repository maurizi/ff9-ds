#ifndef DS_DISPLAY_H
#define DS_DISPLAY_H

/* Top screen (port/ds/display.c): set up after the VRAM shadow exists; present once per frame. */
void display_init(void);
void display_present(void);

#endif

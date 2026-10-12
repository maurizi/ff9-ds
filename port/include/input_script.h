#ifndef INPUT_SCRIPT_H
#define INPUT_SCRIPT_H

/* "BUTTON@FRAME[+FRAMES]": add one scripted press; 0 if malformed. */
int input_script_add(const char *arg);
/* A comma- or space-separated list of them. */
int input_script_add_list(const char *list);
/* PS1 buttons (active high) the script holds at `frame`. */
unsigned int input_script_buttons(long frame);

#endif

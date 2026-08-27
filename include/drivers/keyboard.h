#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <kernel/types.h>
void kbd_init(void);
char kbd_getchar(void);
int  kbd_haschar(void);
void kbd_irq_handler(void);
#endif

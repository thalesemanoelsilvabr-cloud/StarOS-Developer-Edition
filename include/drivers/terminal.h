#ifndef TERMINAL_H
#define TERMINAL_H
#include <kernel/types.h>
void term_init(void);
void term_write(const char* s);
void term_write_char(char c);
void term_clear(void);
void term_set_color(u8 fg, u8 bg);
#endif

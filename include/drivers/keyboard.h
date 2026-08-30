#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <kernel/types.h>

/* Layouts disponíveis */
#define KBD_LAYOUT_ENUS  0
#define KBD_LAYOUT_PTBR  1
#define KBD_LAYOUT_ES    2
#define KBD_LAYOUT_DE    3
#define KBD_LAYOUT_FR    4
#define KBD_LAYOUT_IT    5
#define KBD_LAYOUT_RU    6
#define KBD_LAYOUT_JA    7
#define KBD_LAYOUT_ZH    8
#define KBD_LAYOUT_AR    9
#define KBD_LAYOUT_MAX   10

/* Tecla especial Super (Win/Cmd) */
#define KEY_SUPER   0x01   /* SOH — não imprimível */

/* Nomes dos layouts */
static const char* const kbd_layout_names[KBD_LAYOUT_MAX] = {
    "English (US)", "Português (Brasil)", "Español",
    "Deutsch", "Français", "Italiano",
    "Русский", "日本語", "中文", "العربية"
};

void kbd_init(void);
char kbd_getchar(void);
int  kbd_haschar(void);
void kbd_flush(void);
void kbd_irq_handler(void);
u8   kbd_modifiers_get(void);
void kbd_set_layout(int layout);
int  kbd_get_layout(void);
#endif

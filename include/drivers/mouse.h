#ifndef MOUSE_H
#define MOUSE_H
#include <kernel/types.h>
typedef struct { int x,y,btn,scroll; } mouse_event_t;
typedef void (*mouse_event_cb_t)(mouse_event_t*);
void mouse_init(void);
void mouse_irq_handler(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_get_btn(void);
int  mouse_get_scroll(void);
void mouse_set_speed(int s);
void mouse_set_screen(int w, int h);
void mouse_set_callback(mouse_event_cb_t cb);
void mouse_set_pos(int x, int y);
#endif

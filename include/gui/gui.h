#ifndef GUI_H
#define GUI_H
#include <kernel/types.h>

#define GUI_KEY    1
#define GUI_MOUSE  2
#define GUI_SCROLL 3
#define GUI_CLOSE  4

typedef struct {
    int  type;
    char key;
    int  mx, my, mb;
    int  sdy;        /* scroll delta Y */
} gui_event_t;

void gui_mb2_init(void* mb2);
void gui_start(void);
void gui_run(void);
int  gui_poll_event(gui_event_t* e);
void gui_dispatch_event(gui_event_t* e, void* win);
void gui_mouse_update(void);

#endif

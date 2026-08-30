#ifndef TASKBAR_H
#define TASKBAR_H
#include <kernel/types.h>
void taskbar_init(u32 w, u32 h);
void taskbar_draw(void);
void taskbar_update_panel(void);
int  taskbar_click(int x, int y);
#endif

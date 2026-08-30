#ifndef WINDOW_H
#define WINDOW_H
#include <kernel/types.h>
typedef struct window window_t;
window_t* window_create(const char* title,int x,int y,int w,int h);
void      window_destroy(window_t* w);
void      window_render(window_t* w);
void      window_set_bg(window_t* w,u32 color);
#endif

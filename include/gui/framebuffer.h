#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include <kernel/types.h>
void fb_init(u32* buf,u32 w,u32 h,u32 pitch);
void fb_clear(u32 color);
void fb_rect(int x,int y,int w,int h,u32 color);
void fb_draw_text(int x,int y,const char* s,u32 color);
void fb_draw_char(int x,int y,char c,u32 color);
u32  fb_width(void);
u32  fb_height(void);
#endif

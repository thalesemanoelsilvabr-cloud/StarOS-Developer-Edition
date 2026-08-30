#ifndef WIDGET_H
#define WIDGET_H
#include <kernel/types.h>
#include <gui/window.h>
typedef struct widget widget_t;
typedef void (*wgt_cb_t)(widget_t*);
widget_t* wgt_button(window_t* w,int x,int y,int bw,int bh,const char* lbl,wgt_cb_t cb);
widget_t* wgt_label(window_t* w,int x,int y,const char* text);
widget_t* wgt_input(window_t* w,int x,int y,int iw,int ih);
widget_t* wgt_progressbar(window_t* w,int x,int y,int pw,int ph);
widget_t* wgt_listbox(window_t* w,int x,int y,int lw,int lh);
void wgt_label_set(widget_t* w,const char* text);
void wgt_label_set_color(widget_t* w,u32 color);
void wgt_input_set(widget_t* w,const char* text);
const char* wgt_input_get(widget_t* w);
void wgt_progressbar_set(widget_t* w,int pct);
void wgt_progressbar_set_color(widget_t* w,u32 fill,u32 bg);
void wgt_listbox_add(widget_t* w,const char* item);
int  wgt_listbox_selected(widget_t* w);
void wgt_set_colors(widget_t* w,u32* colors);
widget_t* wgt_separator(window_t* w,int x,int y,int len);
#endif
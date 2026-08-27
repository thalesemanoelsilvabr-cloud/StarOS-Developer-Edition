/* window.c — janelas simples */
#include <kernel/types.h>
#include <gui/window.h>
#include <gui/framebuffer.h>
#include <mm/kmalloc.h>

/* Paleta StarOS */
#define C_TITLEBAR  0x2D1B69
#define C_TITLE_TXT 0xEDE9FE
#define C_WIN_BG    0x1A0A3A
#define C_BORDER    0x7C3AED
#define C_CLOSE     0xF04060

#define MAX_WINS 8
#define TITLE_H  22

struct window {
    int x,y,w,h;
    u32 bg;
    char title[64];
    u8   used;
};

static window_t pool[MAX_WINS];

window_t* window_create(const char* title,int x,int y,int w,int h){
    for(int i=0;i<MAX_WINS;i++){
        if(!pool[i].used){
            window_t* wn=&pool[i];
            wn->x=x; wn->y=y; wn->w=w; wn->h=h;
            wn->bg=C_WIN_BG; wn->used=1;
            int ti=0;
            while(title[ti]&&ti<63){ wn->title[ti]=title[ti]; ti++; }
            wn->title[ti]=0;
            window_render(wn);
            return wn;
        }
    }
    return (window_t*)0;
}

void window_set_bg(window_t* w,u32 c){ if(w) w->bg=c; }

void window_render(window_t* w){
    if(!w) return;
    /* sombra */
    fb_rect(w->x+3,w->y+3,w->w,w->h,0x000000);
    /* borda */
    fb_rect(w->x-1,w->y-1,w->w+2,w->h+2,C_BORDER);
    /* titlebar */
    fb_rect(w->x,w->y,w->w,TITLE_H,C_TITLEBAR);
    /* titulo */
    fb_draw_text(w->x+8,w->y+4,w->title,C_TITLE_TXT);
    /* botao fechar */
    fb_rect(w->x+w->w-18,w->y+3,16,16,C_CLOSE);
    fb_draw_char(w->x+w->w-14,w->y+4,'x',0xFFFFFF);
    /* corpo */
    fb_rect(w->x,w->y+TITLE_H,w->w,w->h-TITLE_H,w->bg);
}

void window_destroy(window_t* w){ if(w) w->used=0; }
void window_move(window_t* w,int x,int y){ if(w){w->x=x;w->y=y;} }

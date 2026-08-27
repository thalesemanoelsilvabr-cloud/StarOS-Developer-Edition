/* widget.c — botoes, labels, inputs, progressbars, listbox */
#include <kernel/types.h>
#include <gui/gui.h>
#include <gui/widget.h>
#include <gui/window.h>
#include <gui/framebuffer.h>
#include <mm/kmalloc.h>

#define C_PANEL   0x2D1B69
#define C_ACCENT  0x7C3AED
#define C_WHITE   0xEDE9FE
#define C_INPUT   0x120830

#define TEXT_MAX   256
#define LIST_ITEMS  32
#define MAX_WGTS    64

typedef enum { WGT_BTN, WGT_LABEL, WGT_INPUT, WGT_PROG, WGT_LIST, WGT_SEP } wgt_type_t;

struct widget {
    wgt_type_t type;
    window_t*  win;
    int  x, y, w, h;
    char text[TEXT_MAX];
    u32  fg, bg, accent, fg2;
    int  value;
    wgt_cb_t cb;
    u8   used;
    char items[LIST_ITEMS][TEXT_MAX];
    int  item_count;
};

static widget_t pool[MAX_WGTS];

static widget_t* alloc_wgt(window_t* wn, wgt_type_t t, int x, int y, int w, int h){
    for(int i = 0; i < MAX_WGTS; i++){
        if(!pool[i].used){
            widget_t* wg = &pool[i];
            wg->type   = t;
            wg->win    = wn;
            wg->x = x; wg->y = y; wg->w = w; wg->h = h;
            wg->fg     = C_WHITE;
            wg->bg     = C_PANEL;
            wg->accent = C_ACCENT;
            wg->fg2    = C_WHITE;
            wg->value  = 0;
            wg->cb     = (void*)0;
            wg->used   = 1;
            wg->text[0]     = 0;
            wg->item_count  = 0;
            return wg;
        }
    }
    return (widget_t*)0;
}

/* acessa campos x/y da janela via ponteiro */
static int win_x(window_t* wn){ return wn ? ((int*)wn)[0] : 0; }
static int win_y(window_t* wn){ return wn ? ((int*)wn)[1] + 22 : 22; } /* +titlebar */

static void copy_str(char* dst, const char* src, int max){
    int i = 0;
    while(src[i] && i < max - 1){ dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int slen(const char* s){ int n=0; while(s[n]) n++; return n; }

/* ── Render de cada widget ────────────────────────────────── */
static void render_widget(widget_t* wg){
    int ax = win_x(wg->win) + wg->x;
    int ay = win_y(wg->win) + wg->y;

    switch(wg->type){

    case WGT_BTN:
        fb_rect(ax, ay, wg->w, wg->h, wg->bg);
        fb_rect(ax, ay, wg->w, 1, 0x9D80F8);
        fb_rect(ax, ay + wg->h - 1, wg->w, 1, 0x2D1B69);
        {
            int tx = ax + (wg->w - slen(wg->text) * 8) / 2;
            int ty = ay + (wg->h - 14) / 2;
            fb_draw_text(tx, ty, wg->text, wg->fg);
        }
        break;

    case WGT_LABEL:
        fb_draw_text(ax, ay, wg->text, wg->fg);
        break;

    case WGT_INPUT:
        fb_rect(ax, ay, wg->w, wg->h, C_INPUT);
        fb_rect(ax, ay, wg->w, 1, wg->accent);
        fb_rect(ax, ay + wg->h - 1, wg->w, 1, wg->accent);
        fb_rect(ax, ay, 1, wg->h, wg->accent);
        fb_rect(ax + wg->w - 1, ay, 1, wg->h, wg->accent);
        fb_draw_text(ax + 4, ay + (wg->h - 14) / 2, wg->text, C_WHITE);
        /* cursor piscante */
        {
            int cl = slen(wg->text) * 8;
            fb_rect(ax + 4 + cl, ay + 4, 2, wg->h - 8, C_WHITE);
        }
        break;

    case WGT_PROG:
        fb_rect(ax, ay, wg->w, wg->h, wg->bg);
        fb_rect(ax, ay, wg->w, 1, wg->accent);
        fb_rect(ax, ay + wg->h - 1, wg->w, 1, wg->accent);
        fb_rect(ax, ay, 1, wg->h, wg->accent);
        fb_rect(ax + wg->w - 1, ay, 1, wg->h, wg->accent);
        {
            int fill = (wg->w - 2) * wg->value / 100;
            if(fill > 0) fb_rect(ax + 1, ay + 1, fill, wg->h - 2, wg->accent);
            /* texto percentual */
            char pstr[8];
            int pv = wg->value, pi = 6;
            pstr[pi] = 0; pstr[--pi] = '%';
            if(pv == 0){ pstr[--pi] = '0'; }
            else { while(pv > 0){ pstr[--pi] = '0' + pv % 10; pv /= 10; } }
            fb_draw_text(ax + wg->w/2 - 12, ay + (wg->h - 14)/2, pstr + pi, C_WHITE);
        }
        break;

    case WGT_LIST:
        fb_rect(ax, ay, wg->w, wg->h, C_INPUT);
        fb_rect(ax, ay, wg->w, 1, wg->accent);
        fb_rect(ax, ay, 1, wg->h, wg->accent);
        fb_rect(ax + wg->w - 1, ay, 1, wg->h, wg->accent);
        fb_rect(ax, ay + wg->h - 1, wg->w, 1, wg->accent);
        for(int j = 0; j < wg->item_count; j++){
            int iy = ay + 2 + j * 15;
            if(iy + 14 > ay + wg->h) break;
            u32 ic = (j == wg->value) ? wg->accent : C_INPUT;
            fb_rect(ax + 1, iy, wg->w - 2, 14, ic);
            fb_draw_text(ax + 4, iy, wg->items[j], C_WHITE);
        }
        break;

    case WGT_SEP:
        fb_rect(ax, ay, wg->w, 1, wg->accent);
        break;
    }
}

static void render_all_for_win(window_t* wn){
    for(int i = 0; i < MAX_WGTS; i++)
        if(pool[i].used && pool[i].win == wn)
            render_widget(&pool[i]);
}

/* ── API pública ─────────────────────────────────────────── */
widget_t* wgt_button(window_t* wn,int x,int y,int bw,int bh,const char* lbl,wgt_cb_t cb){
    widget_t* wg = alloc_wgt(wn, WGT_BTN, x, y, bw, bh);
    if(!wg) return (widget_t*)0;
    copy_str(wg->text, lbl, TEXT_MAX);
    wg->cb = cb;
    return wg;
}
widget_t* wgt_label(window_t* wn,int x,int y,const char* txt){
    widget_t* wg = alloc_wgt(wn, WGT_LABEL, x, y, 0, 14);
    if(!wg) return (widget_t*)0;
    copy_str(wg->text, txt, TEXT_MAX);
    return wg;
}
widget_t* wgt_input(window_t* wn,int x,int y,int iw,int ih){
    return alloc_wgt(wn, WGT_INPUT, x, y, iw, ih);
}
widget_t* wgt_progressbar(window_t* wn,int x,int y,int pw,int ph){
    return alloc_wgt(wn, WGT_PROG, x, y, pw, ph);
}
widget_t* wgt_listbox(window_t* wn,int x,int y,int lw,int lh){
    return alloc_wgt(wn, WGT_LIST, x, y, lw, lh);
}
widget_t* wgt_separator(window_t* wn,int x,int y,int len){
    return alloc_wgt(wn, WGT_SEP, x, y, len, 1);
}
widget_t* wgt_checkbox(window_t* wn,int x,int y,const char* lbl){
    return wgt_button(wn, x, y, 16, 16, lbl, (void*)0);
}

void wgt_label_set(widget_t* wg, const char* t){
    if(wg) copy_str(wg->text, t, TEXT_MAX);
}
void wgt_label_set_color(widget_t* wg, u32 c){ if(wg) wg->fg = c; }
void wgt_input_set(widget_t* wg, const char* t){
    if(wg) copy_str(wg->text, t, TEXT_MAX);
}
const char* wgt_input_get(widget_t* wg){ return wg ? wg->text : (const char*)0; }
void wgt_progressbar_set(widget_t* wg, int pct){
    if(!wg) return;
    if(pct < 0) pct = 0;
    if(pct > 100) pct = 100;
    wg->value = pct;
}
void wgt_progressbar_set_color(widget_t* wg, u32 fill, u32 bg){
    if(!wg) return;
    wg->accent = fill;
    wg->bg     = bg;
}
void wgt_listbox_add(widget_t* wg, const char* item){
    if(!wg || wg->item_count >= LIST_ITEMS) return;
    copy_str(wg->items[wg->item_count], item, TEXT_MAX);
    wg->item_count++;
}
void wgt_listbox_clear(widget_t* wg){ if(wg){ wg->item_count = 0; wg->value = 0; } }
int  wgt_listbox_selected(widget_t* wg){ return wg ? wg->value : -1; }
void wgt_set_colors(widget_t* wg, u32* c){
    if(!wg || !c) return;
    wg->bg = c[0]; wg->fg = c[1]; wg->accent = c[2]; wg->fg2 = c[3];
}

/* chamado pelo gui.c */
void gui_dispatch_event_widgets(gui_event_t* e, void* win_v){
    window_t* wn = (window_t*)win_v;
    if(!e) return;

    if(e->type == GUI_KEY){
        for(int i = 0; i < MAX_WGTS; i++){
            widget_t* wg = &pool[i];
            if(!wg->used || wg->win != wn || wg->type != WGT_INPUT) continue;
            int n = slen(wg->text);
            char k = e->key;
            if(k == '\b'){ if(n > 0) wg->text[n-1] = 0; }
            else if(k >= 32 && k < 127 && n < TEXT_MAX - 1){
                wg->text[n] = k; wg->text[n+1] = 0;
            }
            break;
        }
    }

    if(e->type == GUI_MOUSE && e->mb == 1){
        for(int i = 0; i < MAX_WGTS; i++){
            widget_t* wg = &pool[i];
            if(!wg->used || wg->win != wn) continue;
            int ax = win_x(wg->win) + wg->x;
            int ay = win_y(wg->win) + wg->y;
            if(e->mx >= ax && e->mx < ax + wg->w &&
               e->my >= ay && e->my < ay + wg->h){
                if(wg->type == WGT_BTN && wg->cb) wg->cb(wg);
                if(wg->type == WGT_LIST){
                    int idx = (e->my - ay) / 15;
                    if(idx >= 0 && idx < wg->item_count) wg->value = idx;
                }
            }
        }
    }

    render_all_for_win(wn);
}

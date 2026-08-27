/* gui.c — compositor e loop principal */
#include <kernel/types.h>
#include <gui/gui.h>
#include <gui/framebuffer.h>

/* Multiboot2 tags */
typedef struct {
    u32 type;
    u32 size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    u32 type;
    u32 size;
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u8  bpp;
    u8  fb_type;
} __attribute__((packed)) mb2_fb_tag_t;

static u32* fb_mem  = (u32*)0;
static u32  fb_w    = 800;
static u32  fb_h    = 600;
static u32  fb_pitch= 3200;

/* fila de eventos */
#define EVT_BUF 32
static gui_event_t evt_buf[EVT_BUF];
static int evt_head = 0, evt_tail = 0;

extern int  mouse_get_x(void);
extern int  mouse_get_y(void);
extern int  mouse_get_btn(void);
extern int  kbd_haschar(void);
extern char kbd_getchar(void);
extern u32  volatile timer_ticks;

void gui_mb2_init(void* mb2){
    u8* p = (u8*)mb2 + 8;
    u32 total = *(u32*)mb2;
    while((u32)(p - (u8*)mb2) < total){
        mb2_tag_t* t = (mb2_tag_t*)p;
        if(t->type == 8){
            mb2_fb_tag_t* fb = (mb2_fb_tag_t*)t;
            fb_mem   = (u32*)(u32)fb->addr;
            fb_w     = fb->width;
            fb_h     = fb->height;
            fb_pitch = fb->pitch;
            fb_init(fb_mem, fb_w, fb_h, fb_pitch);
            break;
        }
        if(t->type == 0) break;
        p += ((t->size + 7) & ~7u);
    }
}

int gui_poll_event(gui_event_t* e){
    if(kbd_haschar()){
        e->type = GUI_KEY;
        e->key  = kbd_getchar();
        e->mx = 0; e->my = 0; e->mb = 0; e->sdy = 0;
        return 1;
    }
    if(evt_head != evt_tail){
        *e = evt_buf[evt_tail];
        evt_tail = (evt_tail + 1) % EVT_BUF;
        return 1;
    }
    return 0;
}

void gui_mouse_update(void){
    int btn = mouse_get_btn();
    int next = (evt_head + 1) % EVT_BUF;
    if(next != evt_tail){
        evt_buf[evt_head].type = GUI_MOUSE;
        evt_buf[evt_head].mx   = mouse_get_x();
        evt_buf[evt_head].my   = mouse_get_y();
        evt_buf[evt_head].mb   = btn;
        evt_buf[evt_head].key  = 0;
        evt_buf[evt_head].sdy  = 0;
        evt_head = next;
    }
}

/* ── Desktop ────────────────────────────────────────────── */
#define C_DESKTOP 0x04020F
#define C_TASKBAR 0x1A0A3A
#define C_BORDER  0x7C3AED
#define C_LOGO    0x9D80F8
#define C_TEXT    0xEDE9FE

static void draw_desktop(void){
    fb_clear(C_DESKTOP);
    /* estrelas de fundo */
    for(u32 i = 0; i < 80; i++){
        u32 sx = (i * 97 + 31) % fb_w;
        u32 sy = (i * 53 + 17) % (fb_h - 40);
        fb_rect((int)sx, (int)sy, 1, 1, 0x6B5FA0);
    }
    /* taskbar */
    fb_rect(0, (int)(fb_h - 32), (int)fb_w, 32, C_TASKBAR);
    fb_rect(0, (int)(fb_h - 33), (int)fb_w,  2, C_BORDER);
    /* logo */
    fb_draw_text(8, (int)(fb_h - 24), "StarOS", C_LOGO);
    fb_rect(70, (int)(fb_h - 28), 1, 24, C_BORDER);
    /* atalhos */
    fb_draw_text( 78, (int)(fb_h - 24), "[Shell]",   C_TEXT);
    fb_draw_text(142, (int)(fb_h - 24), "[Browser]", C_TEXT);
    fb_draw_text(222, (int)(fb_h - 24), "[PKG]",     C_TEXT);
    fb_draw_text(270, (int)(fb_h - 24), "[Editor]",  C_TEXT);
    /* relogio */
    u32 s = timer_ticks / 100, m = s / 60, h = m / 60;
    s %= 60; m %= 60; h %= 24;
    char clk[9];
    clk[0]='0'+h/10; clk[1]='0'+h%10; clk[2]=':';
    clk[3]='0'+m/10; clk[4]='0'+m%10; clk[5]=':';
    clk[6]='0'+s/10; clk[7]='0'+s%10; clk[8]=0;
    fb_draw_text((int)(fb_w - 72), (int)(fb_h - 24), clk, C_TEXT);
    /* cursor */
    int mx = mouse_get_x(), my = mouse_get_y();
    fb_rect(mx,   my,   2, 10, 0xFFFFFF);
    fb_rect(mx,   my,   8,  2, 0xFFFFFF);
    fb_rect(mx+1, my+2, 1,  4, 0x888888);
}

extern void shell_run(void);
extern void browser_main(void);
extern void deb_installer_app_main(void);
extern void net_poll(void);

void gui_start(void){
    if(fb_mem) draw_desktop();
}

void gui_run(void){
    if(!fb_mem){ shell_run(); return; }
    draw_desktop();
    u32 last_draw = 0;
    for(;;){
        if(timer_ticks - last_draw >= 50){
            draw_desktop();
            last_draw = timer_ticks;
        }
        gui_event_t e;
        if(gui_poll_event(&e)){
            if(e.type == GUI_MOUSE && e.mb == 1){
                if(e.my >= (int)(fb_h - 32)){
                    if(e.mx >=  78 && e.mx < 138) shell_run();
                    if(e.mx >= 142 && e.mx < 218) browser_main();
                    if(e.mx >= 222 && e.mx < 268) deb_installer_app_main();
                }
            }
        }
        net_poll();
        __asm__ volatile("hlt");
    }
}

void gui_dispatch_event(gui_event_t* e, void* win){
    extern void gui_dispatch_event_widgets(gui_event_t*, void*);
    gui_dispatch_event_widgets(e, win);
}

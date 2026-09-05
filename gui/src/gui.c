/* gui.c — compositor principal com taskbar inteligente */
#include <kernel/types.h>
#include <gui/gui.h>
#include <gui/framebuffer.h>

extern void kprintf(const char*, ...);

struct mb2_tag { u32 type; u32 size; };
struct mb2_fb_tag {
    u32 type; u32 size;
    u64 addr; u32 pitch; u32 width; u32 height; u8 bpp; u8 fb_type;
} __attribute__((packed));

static u32* fb_mem   = (u32*)0;
static u32  fb_w     = 800;
static u32  fb_h     = 600;
static u32  fb_pitch = 3200;

#define EVT_BUF 64
static gui_event_t evt_buf[EVT_BUF];
static int evt_head=0, evt_tail=0;

extern int  mouse_get_x(void);
extern int  mouse_get_y(void);
extern int  mouse_get_btn(void);
extern int  mouse_get_scroll(void);
extern int  kbd_haschar(void);
extern char kbd_getchar(void);
extern u32  volatile timer_ticks;

void gui_mb2_init(void* mb2){
    u8* p=(u8*)mb2+8;
    u32 total=*(u32*)mb2;
    while((u32)(p-(u8*)mb2)<total){
        struct mb2_tag* t=(struct mb2_tag*)p;
        if(t->type==8){
            struct mb2_fb_tag* fb=(struct mb2_fb_tag*)p;
            fb_mem=(u32*)(u32)fb->addr;
            fb_w=fb->width; fb_h=fb->height; fb_pitch=fb->pitch;
            fb_init(fb_mem,fb_w,fb_h,fb_pitch);
            /* informa mouse do tamanho da tela */
            extern void mouse_set_screen(int,int);
            mouse_set_screen((int)fb_w,(int)fb_h);
            break;
        }
        if(t->type==0) break;
        p+=((t->size+7)&~7u);
    }
}

int gui_has_fb(void){ return fb_mem != (u32*)0; }

int gui_poll_event(gui_event_t* e){
    /* scroll do mouse */
    extern int mouse_get_scroll(void);
    int sc=mouse_get_scroll();
    if(sc){
        e->type=GUI_SCROLL; e->sdy=sc;
        e->mx=mouse_get_x(); e->my=mouse_get_y();
        e->mb=0; e->key=0;
        return 1;
    }
    if(kbd_haschar()){
        e->type=GUI_KEY; e->key=kbd_getchar();
        e->mx=0; e->my=0; e->mb=0; e->sdy=0;
        return 1;
    }
    if(evt_head!=evt_tail){
        *e=evt_buf[evt_tail];
        evt_tail=(evt_tail+1)%EVT_BUF;
        return 1;
    }
    return 0;
}

void gui_mouse_update(void){
    static int last_btn=0;
    int btn=mouse_get_btn();
    if(btn!=last_btn){
        int next=(evt_head+1)%EVT_BUF;
        if(next!=evt_tail){
            evt_buf[evt_head].type=GUI_MOUSE;
            evt_buf[evt_head].mx=mouse_get_x();
            evt_buf[evt_head].my=mouse_get_y();
            evt_buf[evt_head].mb=btn;
            evt_buf[evt_head].key=0;
            evt_buf[evt_head].sdy=0;
            evt_head=next;
        }
        last_btn=btn;
    }
}

/* ── Desktop ────────────────────────────────────────────── */
#define C_DESKTOP 0x04020F
#define C_LOGO    0x9D80F8
#define C_TEXT    0xEDE9FE
#define C_GRAY    0x6B5FA0

static void draw_stars(void){
    for(u32 i=0;i<100;i++){
        int sx=(int)((i*97u+31u)%fb_w);
        int sy=(int)((i*53u+17u)%(fb_h-40u));
        u32 c=(i%3==0)?0x9D80F8:(i%3==1)?0x6B5FA0:0x3D1B69;
        fb_rect(sx,sy,1+(i%2),1+(i%2),c);
    }
}

extern void taskbar_init(u32 w,u32 h);
extern void taskbar_draw(void);
extern void taskbar_update_panel(void);
extern int  taskbar_click(int x,int y);

static void draw_desktop(void){
    fb_clear(C_DESKTOP);
    draw_stars();
    /* ícones do desktop */
    int iw=64, ih=64;
    struct { int x,y; const char* name; } icons[]={
        {30,50,"Terminal"},
        {30,150,"Browser"},
        {30,250,"Arquivos"},
        {30,350,"Config"},
        {0}
    };
    for(int i=0;icons[i].name;i++){
        fb_rect(icons[i].x,icons[i].y,iw,ih,0x2D1B69);
        fb_rect(icons[i].x,icons[i].y,iw,1,0x7C3AED);
        fb_rect(icons[i].x,icons[i].y,1,ih,0x7C3AED);
        fb_rect(icons[i].x+iw-1,icons[i].y,1,ih,0x7C3AED);
        fb_rect(icons[i].x,icons[i].y+ih-1,iw,1,0x7C3AED);
        fb_draw_text(icons[i].x+4,icons[i].y+30,icons[i].name,C_TEXT);
    }
    taskbar_draw();
    /* cursor */
    int mx=mouse_get_x(), my=mouse_get_y();
    fb_rect(mx,my,2,12,0xFFFFFF);
    fb_rect(mx,my,8,2,0xFFFFFF);
    fb_rect(mx+1,my+2,1,6,0xAAAAAA);
}

extern void shell_run(void);
extern void browser_main(void);
extern void deb_installer_app_main(void);
extern void settings_main(void);
extern void net_poll(void);

void gui_start(void){
    if(!fb_mem) return;
    taskbar_init(fb_w,fb_h);
    draw_desktop();
}

void gui_run(void){
    if(!fb_mem){ shell_run(); return; }
    taskbar_init(fb_w,fb_h);
    draw_desktop();

    u32 last_draw=0, last_panel=0;
    for(;;){
        /* redesenha a cada 500ms */
        if(timer_ticks-last_draw>=50){
            draw_desktop();
            last_draw=timer_ticks;
        }
        /* atualiza painel de status a cada 1s */
        if(timer_ticks-last_panel>=100){
            taskbar_update_panel();
            last_panel=timer_ticks;
        }
        gui_event_t e;
        if(gui_poll_event(&e)){
            if(e.type==GUI_MOUSE&&e.mb==1){
                /* verifica clique na taskbar */
                if(!taskbar_click(e.mx,e.my)){
                    /* ícones do desktop */
                    if(e.mx>=30&&e.mx<94){
                        if(e.my>=50&&e.my<114)  shell_run();
                        if(e.my>=150&&e.my<214) browser_main();
                        if(e.my>=250&&e.my<314) deb_installer_app_main();
                        if(e.my>=350&&e.my<414) settings_main();
                    }
                }
            }
            /* tecla Super → mostra/oculta launcher */
            extern u8 kbd_modifiers_get(void);
            if(e.type==GUI_KEY && e.key==0x01){ /* KEY_SUPER */
                /* TODO: abrir launcher */
                kprintf("[gui] Super key\n");
            }
        }
        net_poll();
        __asm__ volatile("hlt");
    }
}

void gui_dispatch_event(gui_event_t* e,void* win){
    extern void gui_dispatch_event_widgets(gui_event_t*,void*);
    gui_dispatch_event_widgets(e,win);
}
/* settings.c — Configurações do sistema */
#include <kernel/types.h>
#include <gui/framebuffer.h>
#include <gui/window.h>
#include <gui/widget.h>
#include <gui/gui.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/wifi.h>

extern void kprintf(const char*,...);
extern void ksnprintf(char*,u32,const char*,...);

#define C_WIN   0x1A0A3A
#define C_PANEL 0x2D1B69
#define C_ACCENT 0x7C3AED
#define C_WHITE 0xEDE9FE
#define C_GRAY  0x6B5FA0

static window_t* win = (void*)0;

/* Aba ativa */
static int active_tab = 0;
#define TAB_LANG   0
#define TAB_KBD    1
#define TAB_MOUSE  2
#define TAB_WIFI   3
#define TAB_SYSTEM 4

/* Widgets */
static widget_t* wl_layout_list = (void*)0;
static widget_t* wl_speed       = (void*)0;
static widget_t* wl_wifi_list   = (void*)0;
static widget_t* wl_status      = (void*)0;

/* Callbacks */
static void cb_tab(widget_t* w);
static void cb_set_lang(widget_t* w);
static void cb_set_speed(widget_t* w);
static void cb_wifi_conn(widget_t* w);
static void cb_wifi_discon(widget_t* w);

static widget_t* tabs[5];

static void draw_tabs(void){
    const char* names[]={"Idioma","Teclado","Mouse","WiFi","Sistema"};
    for(int i=0;i<5;i++){
        u32 bc=(i==active_tab)?C_ACCENT:C_PANEL;
        u32 tc=C_WHITE;
        fb_rect(10+i*130,10,125,28,bc);
        fb_draw_text(14+i*130,18,names[i],tc);
    }
}

static void show_tab_lang(void){
    wgt_label(win,10,50,"Idioma do sistema:");
    wl_layout_list=wgt_listbox(win,10,70,620,300);
    extern const char* const kbd_layout_names[];
    for(int i=0;i<10;i++)
        wgt_listbox_add(wl_layout_list, kbd_layout_names[i]);
    /* marca atual */
    widget_t* apply=wgt_button(win,490,390,140,28,"Aplicar",cb_set_lang);
    u32 bc[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
    wgt_set_colors(apply,bc);
}

static void show_tab_kbd(void){
    wgt_label(win,10,50,"Layout do teclado:");
    wl_layout_list=wgt_listbox(win,10,70,620,240);
    for(int i=0;i<10;i++)
        wgt_listbox_add(wl_layout_list, kbd_layout_names[i]);
    wgt_label(win,10,320,"Atual:");
    char cur[64];
    ksnprintf(cur,63,"Layout: %s",kbd_layout_names[kbd_get_layout()]);
    wgt_label(win,60,320,cur);
    widget_t* apply=wgt_button(win,490,390,140,28,"Aplicar",cb_set_lang);
    u32 bc[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
    wgt_set_colors(apply,bc);
}

static void show_tab_mouse(void){
    wgt_label(win,10,50,"Velocidade do mouse:");
    wl_speed=wgt_progressbar(win,10,70,400,24);
    wgt_progressbar_set(wl_speed,50);
    wgt_progressbar_set_color(wl_speed,C_ACCENT,C_PANEL);
    widget_t* slow=wgt_button(win,10,104,80,24,"Lento",cb_set_speed);
    widget_t* fast=wgt_button(win,100,104,80,24,"Rapido",cb_set_speed);
    (void)slow;(void)fast;
    wgt_label(win,10,140,"Botões:");
    wgt_label(win,10,158,"[Esquerdo] [Direito] [Meio]");
    wgt_label(win,10,180,"Toque duplo: duplo clique");
}

static void show_tab_wifi(void){
    wgt_label(win,10,50,"Redes WiFi disponíveis:");
    wl_wifi_list=wgt_listbox(win,10,70,620,200);
    wifi_net_t nets[8];
    int n=wifi_scan(nets,8);
    for(int i=0;i<n;i++){
        char entry[64];
        ksnprintf(entry,63,"%s  [%d dBm] %s",
                  nets[i].ssid, nets[i].signal,
                  nets[i].encrypted?"[*]":"");
        wgt_listbox_add(wl_wifi_list,entry);
    }
    if(!n) wgt_listbox_add(wl_wifi_list,"(Nenhuma rede encontrada)");
    widget_t* wconn=wgt_button(win,10,280,140,28,"Conectar",cb_wifi_conn);
    widget_t* wdisc=wgt_button(win,160,280,140,28,"Desconectar",cb_wifi_discon);
    u32 bc[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
    wgt_set_colors(wconn,bc);
    wgt_set_colors(wdisc,bc);
    wl_status=wgt_label(win,10,320,
        wifi_connected()?"WiFi: Conectado":"WiFi: Desconectado");
    u32 sc=wifi_connected()?0x40E0A0:C_GRAY;
    wgt_label_set_color(wl_status,sc);
}

static void show_tab_system(void){
    wgt_label(win,10, 50,"Sistema:");
    wgt_label(win,10, 70,"StarOS Beta Edition  v0.4");
    wgt_label(win,10, 90,"Kernel: x86 32-bit bare-metal");
    wgt_label(win,10,110,"GUI: VESA 800x600x32bpp");
    wgt_label(win,10,130,"Rede: NE2000 TCP/IP stack");
    wgt_separator(win,10,150,620);
    wgt_label(win,10,160,"Memória:");
    extern u32 kmem_used(void);
    char ms[32]; ksnprintf(ms,31,"Heap usado: %u KB",kmem_used()/1024);
    wgt_label(win,10,178,ms);
}

static void cb_tab(widget_t* w){
    for(int i=0;i<5;i++) if(tabs[i]==w) active_tab=i;
    window_render(win);
    draw_tabs();
    switch(active_tab){
        case TAB_LANG:   show_tab_lang();   break;
        case TAB_KBD:    show_tab_kbd();    break;
        case TAB_MOUSE:  show_tab_mouse();  break;
        case TAB_WIFI:   show_tab_wifi();   break;
        case TAB_SYSTEM: show_tab_system(); break;
    }
    window_render(win);
}

static void cb_set_lang(widget_t* w){ (void)w;
    if(!wl_layout_list) return;
    int idx=wgt_listbox_selected(wl_layout_list);
    if(idx>=0){ kbd_set_layout(idx); kprintf("[settings] Layout: %d\n",idx); }
}

static void cb_set_speed(widget_t* w){ (void)w;
    mouse_set_speed(3);
}

static void cb_wifi_conn(widget_t* w){ (void)w;
    wifi_connect("StarOS-Net","");
    if(wl_status){
        wgt_label_set(wl_status,"WiFi: Conectado");
        wgt_label_set_color(wl_status,0x40E0A0);
    }
}
static void cb_wifi_discon(widget_t* w){ (void)w;
    wifi_disconnect();
    if(wl_status){
        wgt_label_set(wl_status,"WiFi: Desconectado");
        wgt_label_set_color(wl_status,C_GRAY);
    }
}

void settings_main(void){
    win=window_create("Configurações do Sistema",30,30,660,450);
    window_set_bg(win,C_WIN);

    /* abas */
    const char* tnames[]={"Idioma","Teclado","Mouse","WiFi","Sistema"};
    for(int i=0;i<5;i++){
        tabs[i]=wgt_button(win,i*130,0,125,28,tnames[i],cb_tab);
        u32 bc[]={C_PANEL,C_WHITE,C_ACCENT,C_WHITE};
        wgt_set_colors(tabs[i],bc);
    }

    draw_tabs();
    show_tab_lang();
    window_render(win);

    for(;;){
        gui_event_t e;
        extern int gui_poll_event(gui_event_t*);
        if(gui_poll_event(&e)){
            extern void gui_dispatch_event(gui_event_t*,void*);
            gui_dispatch_event(&e,win);
        }
        __asm__ volatile("hlt");
    }
}

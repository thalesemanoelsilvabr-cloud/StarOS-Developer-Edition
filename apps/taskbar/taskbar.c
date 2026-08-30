/* taskbar.c — Barra de tarefas com WiFi + Bateria + Relógio */
#include <kernel/types.h>
#include <gui/framebuffer.h>
#include <gui/widget.h>
#include <gui/window.h>
#include <drivers/acpi.h>
#include <drivers/wifi.h>

extern void kprintf(const char*,...);
extern void ksnprintf(char*,u32,const char*,...);
extern u32  volatile timer_ticks;
extern int  mouse_get_x(void);
extern int  mouse_get_y(void);
extern int  mouse_get_btn(void);

/* Cores */
#define C_TASKBAR 0x1A0A3A
#define C_BORDER  0x7C3AED
#define C_LOGO    0x9D80F8
#define C_TEXT    0xEDE9FE
#define C_GRAY    0x6B5FA0
#define C_GREEN   0x40E0A0
#define C_YELLOW  0xFFD700
#define C_RED     0xF04060
#define C_ORANGE  0xFF8C00

static u32 tb_w = 800;
static u32 tb_h = 600;

/* Painel de status (WiFi+Bat+Relógio) — popup ao clicar */
static int panel_open = 0;
static window_t* status_panel = (void*)0;
static widget_t* wl_time_big = (void*)0;
static widget_t* wl_date     = (void*)0;
static widget_t* wl_bat_bar  = (void*)0;
static widget_t* wl_bat_lbl  = (void*)0;
static widget_t* wl_wifi_lbl = (void*)0;
static widget_t* wl_wifi_btn = (void*)0;

static void cb_wifi_toggle(widget_t* w){
    (void)w;
    if(wifi_connected()) wifi_disconnect();
    else wifi_connect("StarOS-Net","");
}

static void open_status_panel(void){
    if(panel_open) return;
    int px = (int)tb_w - 240;
    int py = (int)tb_h - 230;
    status_panel = window_create("Status", px, py, 230, 200);
    window_set_bg(status_panel, 0x1A0A3A);

    wl_time_big = wgt_label(status_panel, 10,  10, "00:00:00");
    wgt_label_set_color(wl_time_big, C_LOGO);

    wl_date = wgt_label(status_panel, 10, 30, "01/01/2025");
    wgt_label_set_color(wl_date, C_GRAY);

    wgt_separator(status_panel, 10, 50, 210);

    wgt_label(status_panel, 10, 58, "Bateria:");
    wl_bat_bar = wgt_progressbar(status_panel, 10, 74, 200, 16);
    wgt_progressbar_set_color(wl_bat_bar, C_GREEN, 0x2D1B69);
    wl_bat_lbl = wgt_label(status_panel, 10, 94, "---");
    wgt_label_set_color(wl_bat_lbl, C_GRAY);

    wgt_separator(status_panel, 10, 112, 210);

    wgt_label(status_panel, 10, 120, "WiFi:");
    wl_wifi_lbl = wgt_label(status_panel, 50, 120, "---");
    wgt_label_set_color(wl_wifi_lbl, C_GRAY);
    wl_wifi_btn = wgt_button(status_panel, 10, 136, 200, 22,
                             "Conectar / Desconectar", cb_wifi_toggle);
    u32 bc[]={C_BORDER,C_TEXT,C_LOGO,C_TEXT};
    wgt_set_colors(wl_wifi_btn, bc);

    window_render(status_panel);
    panel_open = 1;
}

static void close_status_panel(void){
    if(!panel_open) return;
    window_destroy(status_panel);
    status_panel = (void*)0;
    panel_open = 0;
}

/* Atualiza o painel com dados reais */
void taskbar_update_panel(void){
    if(!panel_open) return;

    /* Relógio */
    rtc_time_t t;
    rtc_read(&t);
    char tstr[16], dstr[16];
    ksnprintf(tstr,15,"%02u:%02u:%02u",t.hour,t.minute,t.second);
    ksnprintf(dstr,15,"%02u/%02u/%04u",t.day,t.month,t.year);
    wgt_label_set(wl_time_big, tstr);
    wgt_label_set(wl_date,     dstr);

    /* Bateria */
    battery_info_t bat;
    if(battery_read(&bat)==0 && bat.present){
        wgt_progressbar_set(wl_bat_bar, bat.percent);
        u32 bc = bat.percent>50?C_GREEN:bat.percent>20?C_YELLOW:C_RED;
        if(bat.charging) bc = C_ORANGE;
        wgt_progressbar_set_color(wl_bat_bar, bc, 0x2D1B69);
        char bs[48];
        ksnprintf(bs,47,"%s %u%% %s",
                  battery_icon(bat.percent,bat.charging),
                  bat.percent,
                  bat.charging?"(carregando)":"");
        wgt_label_set(wl_bat_lbl, bs);
    } else {
        wgt_label_set(wl_bat_lbl, "Sem bateria (desktop)");
    }

    /* WiFi */
    char ws[48];
    ksnprintf(ws,47,"%s %s",wifi_icon(),
              wifi_connected()?wifi_ssid():"desconectado");
    wgt_label_set(wl_wifi_lbl, ws);
    u32 wc = wifi_connected()?C_GREEN:C_GRAY;
    wgt_label_set_color(wl_wifi_lbl, wc);

    window_render(status_panel);
}

/* Desenha a taskbar no framebuffer */
void taskbar_draw(void){
    int y = (int)(tb_h - 32);
    fb_rect(0, y, (int)tb_w, 32, C_TASKBAR);
    fb_rect(0, y-1, (int)tb_w, 2, C_BORDER);

    /* Logo */
    fb_draw_text(8, y+9, "StarOS", C_LOGO);
    fb_rect(68, y+3, 1, 26, C_BORDER);

    /* Atalhos */
    fb_draw_text( 76, y+9, "[Shell]",   C_TEXT);
    fb_draw_text(140, y+9, "[Browser]", C_TEXT);
    fb_draw_text(220, y+9, "[PKG]",     C_TEXT);
    fb_draw_text(266, y+9, "[Editor]",  C_TEXT);

    /* ── Área de status (direita) ───────────────────────── */
    /* Relógio rápido */
    rtc_time_t t; rtc_read(&t);
    char clk[10];
    ksnprintf(clk,9,"%02u:%02u:%02u",t.hour,t.minute,t.second);
    fb_draw_text((int)tb_w - 74, y+9, clk, C_TEXT);
    fb_rect((int)tb_w-80, y+3, 1, 26, C_BORDER);

    /* Bateria */
    battery_info_t bat; int bat_ok=0;
    if(battery_read(&bat)==0 && bat.present){
        bat_ok=1;
        u32 bc=bat.percent>50?C_GREEN:bat.percent>20?C_YELLOW:C_RED;
        if(bat.charging) bc=C_ORANGE;
        /* mini barra de bateria */
        fb_rect((int)tb_w-138, y+8, 50, 14, 0x2D1B69);
        fb_rect((int)tb_w-138, y+8, bat.percent/2, 14, bc);
        fb_rect((int)tb_w-138, y+8, 50, 1, C_BORDER);
        fb_rect((int)tb_w-138, y+21, 50, 1, C_BORDER);
        fb_rect((int)tb_w-138, y+8, 1, 14, C_BORDER);
        fb_rect((int)tb_w-89,  y+8, 1, 14, C_BORDER);
        /* ponta da bateria */
        fb_rect((int)tb_w-88, y+11, 3, 8, C_BORDER);
    }
    if(!bat_ok){
        fb_draw_text((int)tb_w-138, y+9, "[AC]", C_GRAY);
    }
    fb_rect((int)tb_w-144, y+3, 1, 26, C_BORDER);

    /* WiFi */
    const char* wicon = wifi_icon();
    u32 wc = wifi_connected() ? C_GREEN : C_GRAY;
    fb_draw_text((int)tb_w-196, y+9, wicon, wc);
    fb_rect((int)tb_w-202, y+3, 1, 26, C_BORDER);
}

/* Processa clique na taskbar */
int taskbar_click(int x, int y_click){
    int ty = (int)(tb_h - 32);
    if(y_click < ty) return 0;

    /* Clique na área de status (WiFi+Bat+Relógio) */
    if(x > (int)tb_w - 202){
        if(panel_open) close_status_panel();
        else           open_status_panel();
        return 1;
    }
    return 0;
}

void taskbar_init(u32 w, u32 h){
    tb_w = w;
    tb_h = h;
}
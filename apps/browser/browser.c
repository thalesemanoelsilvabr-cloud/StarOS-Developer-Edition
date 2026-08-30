/* ============================================================
 *  StarOS — StarBrowser  (apps/browser/browser.c)
 *  Navegador web com GUI usando o sistema gráfico do StarOS
 *
 *  Paleta oficial StarOS:
 *    GUI_BLACK   #04020F  fundo desktop
 *    GUI_DARK_BG #1A0A3A  fundo janela
 *    GUI_PANEL   #2D1B69  painéis / barras
 *    GUI_ACCENT  #7C3AED  destaque / foco
 *    GUI_WHITE   #EDE9FE  texto principal
 * ============================================================ */
#include <gui/gui.h>
#include <gui/window.h>
#include <gui/widget.h>
#include <gui/framebuffer.h>
#include <net/http.h>
#include <net/net.h>
#include <kernel/types.h>
#include <mm/kmalloc.h>

/* declaração antecipada */
static void int_to_dec(int n, char *buf);


extern void kprintf(const char*,...);
extern void kstrcpy(char*,const char*);
extern int  kstrlen(const char*);
extern void kstrncpy(char*,const char*,u32);
extern void kmemset(void*,u8,u32);
extern int  kstrcmp(const char*,const char*);
extern int  kstrncmp(const char*,const char*,u32);

/* ── Cores da UI ─────────────────────────────────────────────── */
#define C_BG       0x04020F
#define C_WIN_BG   0x1A0A3A
#define C_PANEL    0x2D1B69
#define C_ACCENT   0x7C3AED
#define C_WHITE    0xEDE9FE
#define C_GRAY     0x6B5FA0
#define C_RED      0xF04060
#define C_GREEN    0x40E0A0
#define C_LINK     0x9D80F8

/* ── Layout ───────────────────────────────────────────────────── */
#define WIN_W      780
#define WIN_H      560
#define TOOLBAR_H  36
#define STATUSBAR_H 20
#define CONTENT_Y  (TOOLBAR_H + 4)
#define CONTENT_H  (WIN_H - TOOLBAR_H - STATUSBAR_H - 8)
#define CONTENT_X  4
#define CONTENT_W  (WIN_W - 8)

/* ── Estado do browser ───────────────────────────────────────── */
#define HISTORY_MAX  16
#define TABS_MAX     4

typedef struct {
    char url[HTTP_URL_MAX];
    char title[256];
    char content[HTTP_BODY_MAX];
    char links[64][HTTP_URL_MAX];
    int  link_count;
    int  scroll;          /* linha de scroll */
    int  loaded;
} tab_t;

static tab_t   tabs[TABS_MAX];
static int     tab_count   = 1;
static int     active_tab  = 0;
static char    history[HISTORY_MAX][HTTP_URL_MAX];
static int     hist_pos    = -1;
static int     hist_count  = 0;
static char    status_msg[128];
static int     loading = 0;

/* ── Janela e widgets ────────────────────────────────────────── */
static window_t *win;
static widget_t *w_url_bar;    /* campo de URL              */
static widget_t *w_go_btn;     /* botão Ir                  */
static widget_t *w_back_btn;
static widget_t *w_fwd_btn;
static widget_t *w_reload_btn;
static widget_t *w_new_tab;    /* + aba                     */
static widget_t *w_tabs[TABS_MAX];

/* ── Protótipos ──────────────────────────────────────────────── */
static void browser_navigate(const char *url);
static void browser_render_content(void);
static void cb_go(widget_t *w);
static void cb_back(widget_t *w);
static void cb_fwd(widget_t *w);
static void cb_reload(widget_t *w);
static void cb_new_tab(widget_t *w);
static void cb_tab(widget_t *w);

/* ── Helpers de desenho ──────────────────────────────────────── */
static void fb_fill_rect(int x,int y,int w,int h,u32 color) {
    fb_rect(x,y,w,h,color);
}
static void fb_text_color(int x,int y,const char *s,u32 color,u32 bg){
    (void)bg;
    fb_draw_text(x,y,s,color);
}

/* ── Renderiza a toolbar ─────────────────────────────────────── */
static void render_toolbar(void) {
    fb_fill_rect(0,0,WIN_W,TOOLBAR_H,C_PANEL);
    /* separador inferior */
    fb_fill_rect(0,TOOLBAR_H-1,WIN_W,1,C_ACCENT);
}

/* ── Renderiza as abas ───────────────────────────────────────── */
static void render_tabs(void) {
    int tab_w = (WIN_W - 30) / (tab_count < 1 ? 1 : tab_count);
    for (int i=0;i<tab_count;i++) {
        int tx = i * tab_w;
        u32 tc = (i==active_tab) ? C_ACCENT : C_PANEL;
        u32 fg = C_WHITE;
        fb_fill_rect(tx, WIN_H-STATUSBAR_H-22, tab_w-2, 20, tc);
        const char *t = tabs[i].title[0] ? tabs[i].title : tabs[i].url;
        char disp[20]; kstrncpy(disp,t,19); disp[19]=0;
        fb_text_color(tx+4, WIN_H-STATUSBAR_H-18, disp, fg, tc);
    }
}

/* ── Renderiza a barra de status ─────────────────────────────── */
static void render_status(void) {
    fb_fill_rect(0, WIN_H-STATUSBAR_H, WIN_W, STATUSBAR_H, C_PANEL);
    u32 sc = loading ? C_ACCENT : C_GRAY;
    fb_text_color(4, WIN_H-STATUSBAR_H+3, status_msg, sc, C_PANEL);
    /* indicador de loading */
    if (loading) {
        static int spin=0; spin=(spin+1)%4;
        const char *sp[4]={"-","\\","|","/"};
        fb_text_color(WIN_W-20, WIN_H-STATUSBAR_H+3, sp[spin], C_ACCENT, C_PANEL);
    }
}

/* ── Renderiza o conteúdo da aba ativa ───────────────────────── */
static void browser_render_content(void) {
    tab_t *t = &tabs[active_tab];
    fb_fill_rect(CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H, C_WIN_BG);
    if (!t->loaded) {
        fb_text_color(CONTENT_X+20, CONTENT_Y+20, "Nenhuma página carregada.",
                      C_GRAY, C_WIN_BG);
        fb_text_color(CONTENT_X+20, CONTENT_Y+38,
                      "Digite uma URL na barra acima e pressione [Ir].",
                      C_GRAY, C_WIN_BG);
        return;
    }
    /* renderiza texto linha por linha com scroll */
    const char *p = t->content;
    int y = CONTENT_Y + 4;
    int line = 0;
    int chars_per_line = CONTENT_W / 8;  /* font 8px */
    char linebuf[256];
    while (*p && y < CONTENT_Y + CONTENT_H - 14) {
        /* extrai linha */
        int n=0;
        while(*p && *p!='\n' && n < chars_per_line-1)
            linebuf[n++] = *p++;
        linebuf[n]=0;
        if(*p=='\n') p++;
        if(line >= t->scroll) {
            /* coloriza links com '[N]' */
            u32 color = C_WHITE;
            /* se a linha contiver URL detectada → azul */
            if(kstrncmp(linebuf,"http",4)==0) color=C_LINK;
            fb_text_color(CONTENT_X+4, y, linebuf, color, C_WIN_BG);
            y += 14;
        }
        line++;
    }
    /* lista de links numerados no rodapé da área */
    if (t->link_count > 0) {
        int lstart = CONTENT_Y + CONTENT_H - (t->link_count * 12) - 4;
        if (lstart > y) {
            fb_fill_rect(CONTENT_X, lstart-2, CONTENT_W, 1, C_ACCENT);
            fb_text_color(CONTENT_X+4, lstart, "Links:", C_ACCENT, C_WIN_BG);
            for (int i=0;i<t->link_count&&i<12;i++) {
                char line2[160];
                char num[4]; num[0]='['; num[1]='0'+i; num[2]=']'; num[3]=' ';
                kstrncpy(line2,num,4); line2[4]=0;
                kstrncpy(line2+4, t->links[i], 150);
                fb_text_color(CONTENT_X+4, lstart+12+i*12, line2,
                              C_LINK, C_WIN_BG);
            }
        }
    }
}

/* ── Navega para uma URL ─────────────────────────────────────── */
static void browser_navigate(const char *url) {
    tab_t *t = &tabs[active_tab];
    kstrncpy(t->url, url, HTTP_URL_MAX-1);
    t->loaded = 0;
    t->scroll = 0;
    t->link_count = 0;
    loading = 1;
    kstrncpy(status_msg, "Carregando...", sizeof(status_msg)-1);

    /* redesenha estado de loading */
    render_toolbar();
    render_status();
    browser_render_content();

    /* faz requisição */
    http_response_t resp;
    int max_redirects = 5;
    char cur_url[HTTP_URL_MAX];
    kstrncpy(cur_url, url, HTTP_URL_MAX-1);

    while (max_redirects-- > 0) {
        kmemset(&resp,0,sizeof(resp));
        int r = http_get(cur_url, &resp);
        if (r < 0) {
            kstrncpy(status_msg,"Erro: falha na conexão.",sizeof(status_msg)-1);
            loading=0; browser_render_content(); render_status(); return;
        }
        if (resp.status == 301 || resp.status == 302 || resp.status == 307) {
            if(resp.redirect_url[0]) {
                kstrncpy(cur_url, resp.redirect_url, HTTP_URL_MAX-1);
                http_response_free(&resp);
                continue;
            }
        }
        break;
    }

    if (resp.status != 200) {
        char msg[64];
        kstrncpy(msg,"Erro HTTP: ",12);
        char code[8]; int_to_dec(resp.status, code);
        kstrcpy(msg+11, code);
        kstrncpy(status_msg,msg,sizeof(status_msg)-1);
        loading=0;
        if(resp.body) http_response_free(&resp);
        browser_render_content(); render_status(); return;
    }

    /* parse HTML */
    html_doc_t *doc = (html_doc_t*)kmalloc(sizeof(html_doc_t));
    if (!doc) { http_response_free(&resp); return; }
    html_parse(resp.body, resp.body_len, doc);
    http_response_free(&resp);

    kstrncpy(t->content, doc->text, HTTP_BODY_MAX-1);
    kstrncpy(t->title,   doc->title[0]?doc->title:cur_url, 255);
    t->link_count = doc->link_count < 64 ? doc->link_count : 64;
    for(int i=0;i<t->link_count;i++)
        kstrncpy(t->links[i], doc->links[i], HTTP_URL_MAX-1);
    kfree(doc);

    t->loaded=1;
    /* histórico */
    if(hist_pos<HISTORY_MAX-1){ kstrncpy(history[++hist_pos],cur_url,HTTP_URL_MAX-1); hist_count=hist_pos+1; }
    kstrncpy(status_msg,cur_url,sizeof(status_msg)-1);
    loading=0;
    render_toolbar();
    render_tabs();
    browser_render_content();
    render_status();
}

/* ── Callbacks de widget ─────────────────────────────────────── */
static void int_to_dec(int n, char *buf){
    if(n<=0){buf[0]='0';buf[1]=0;return;}
    char tmp[12];int i=0;
    while(n>0){tmp[i++]='0'+n%10;n/=10;}
    int j=0;while(i-->0)buf[j++]=tmp[i];buf[j]=0;
}

static void cb_go(widget_t *w) {
    (void)w;
    const char *url = wgt_input_get(w_url_bar);
    if(url && url[0]) browser_navigate(url);
}

static void cb_back(widget_t *w) {
    (void)w;
    if(hist_pos > 0) {
        hist_pos--;
        kstrncpy(tabs[active_tab].url, history[hist_pos], HTTP_URL_MAX-1);
        wgt_input_set(w_url_bar, history[hist_pos]);
        browser_navigate(history[hist_pos]);
    }
}

static void cb_fwd(widget_t *w) {
    (void)w;
    if(hist_pos < hist_count-1) {
        hist_pos++;
        wgt_input_set(w_url_bar, history[hist_pos]);
        browser_navigate(history[hist_pos]);
    }
}

static void cb_reload(widget_t *w) {
    (void)w;
    if(tabs[active_tab].url[0])
        browser_navigate(tabs[active_tab].url);
}

static void cb_new_tab(widget_t *w) {
    (void)w;
    if(tab_count >= TABS_MAX) return;
    kmemset(&tabs[tab_count],0,sizeof(tab_t));
    active_tab = tab_count++;
    wgt_input_set(w_url_bar,"");
    kstrncpy(status_msg,"Nova aba",sizeof(status_msg)-1);
    render_toolbar();
    render_tabs();
    browser_render_content();
    render_status();
}

static void cb_tab(widget_t *w) {
    for(int i=0;i<tab_count;i++){
        if(w_tabs[i]==w){ active_tab=i; break; }
    }
    if(tabs[active_tab].url[0])
        wgt_input_set(w_url_bar, tabs[active_tab].url);
    render_toolbar();
    render_tabs();
    browser_render_content();
    render_status();
}

/* ── Ponto de entrada do browser ─────────────────────────────── */
void browser_main(void) {
    net_init();
    kmemset(tabs,0,sizeof(tabs));
    kstrncpy(status_msg,"Pronto.",sizeof(status_msg)-1);

    /* cria janela */
    win = window_create("StarBrowser", 10, 10, WIN_W, WIN_H);
    window_set_bg(win, C_WIN_BG);

    /* toolbar: botões de navegação */
    w_back_btn   = wgt_button(win,  4, 6, 28, 24, "◀", cb_back);
    w_fwd_btn    = wgt_button(win, 35, 6, 28, 24, "▶", cb_fwd);
    w_reload_btn = wgt_button(win, 66, 6, 28, 24, "↺", cb_reload);

    /* campo URL largo */
    w_url_bar = wgt_input(win, 98, 6, WIN_W-170, 24);
    wgt_input_set(w_url_bar, "http://");

    /* botão Ir */
    w_go_btn  = wgt_button(win, WIN_W-68, 6, 40, 24, "Ir", cb_go);

    /* botão nova aba */
    w_new_tab = wgt_button(win, WIN_W-26, 6, 22, 24, "+", cb_new_tab);

    /* abas de páginas — na base da área de conteúdo */
    int tab_w_px = (WIN_W-30) / TABS_MAX;
    for(int i=0;i<TABS_MAX;i++){
        w_tabs[i] = wgt_button(win, i*tab_w_px,
                               WIN_H-STATUSBAR_H-22, tab_w_px-2, 20,
                               "  Aba  ", cb_tab);
    }

    /* define cores dos botões */
    u32 btn_colors[] = {C_PANEL, C_WHITE, C_ACCENT, C_WHITE};
    wgt_set_colors(w_go_btn,     btn_colors);
    wgt_set_colors(w_back_btn,   btn_colors);
    wgt_set_colors(w_fwd_btn,    btn_colors);
    wgt_set_colors(w_reload_btn, btn_colors);
    wgt_set_colors(w_new_tab,    btn_colors);

    /* página inicial */
    render_toolbar();
    render_tabs();
    browser_render_content();
    render_status();

    kprintf("[browser] StarBrowser iniciado\n");

    /* loop principal */
    for (;;) {
        net_poll();
        gui_event_t evt;
        if (gui_poll_event(&evt)) {
            if (evt.type == GUI_KEY && evt.key == '\n')
                cb_go(w_url_bar);
            if (evt.type == GUI_SCROLL) {
                tab_t *t = &tabs[active_tab];
                if (evt.sdy < 0 && t->scroll > 0) t->scroll--;
                if (evt.sdy > 0) t->scroll++;
                browser_render_content();
            }
            gui_dispatch_event(&evt, win);
            render_status();
        }
    }
}
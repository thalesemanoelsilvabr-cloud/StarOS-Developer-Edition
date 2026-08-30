/* installer.c — Instalador do StarOS com suporte a múltiplos idiomas */
#include <kernel/types.h>
#include <gui/framebuffer.h>
#include <gui/window.h>
#include <gui/widget.h>
#include <gui/gui.h>
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>

extern void kprintf(const char*,...);
extern void ksnprintf(char*,u32,const char*,...);
extern void kstrncpy(char*,const char*,u32);
extern int  kstrcmp(const char*,const char*);
extern void kbd_set_layout(int);
extern u32  volatile timer_ticks;

/* ── Cores ────────────────────────────────────────────────── */
#define C_BG      0x04020F
#define C_WIN     0x1A0A3A
#define C_PANEL   0x2D1B69
#define C_ACCENT  0x7C3AED
#define C_WHITE   0xEDE9FE
#define C_GRAY    0x6B5FA0
#define C_GREEN   0x40E0A0
#define C_RED     0xF04060
#define C_LOGO    0x9D80F8

/* ══════════════════════════════════════════════════════════
 * STRINGS DE LOCALIZAÇÃO
 * ════════════════════════════════════════════════════════*/
#define LANG_MAX 20

typedef struct {
    const char* code;       /* "pt_BR", "en_US" ... */
    const char* name;       /* nome nativo */
    int         kbd_layout; /* KBD_LAYOUT_* */
    /* strings da UI */
    const char* s_welcome;
    const char* s_select_lang;
    const char* s_next;
    const char* s_back;
    const char* s_install;
    const char* s_disk;
    const char* s_user;
    const char* s_pass;
    const char* s_hostname;
    const char* s_timezone;
    const char* s_finish;
    const char* s_installing;
    const char* s_done;
    const char* s_reboot;
    const char* s_step_lang;
    const char* s_step_disk;
    const char* s_step_user;
    const char* s_step_install;
} lang_t;

static const lang_t langs[LANG_MAX] = {
    {
        "pt_BR","Português (Brasil)",1,
        "Bem-vindo ao instalador do StarOS!",
        "Selecione o idioma:",
        "Próximo","Voltar","Instalar",
        "Disco de instalação:","Usuário:","Senha:","Nome do PC:","Fuso horário:",
        "Instalação concluída!","Instalando...",
        "StarOS instalado com sucesso!","Reiniciar",
        "Idioma","Disco","Usuário","Instalar"
    },
    {
        "en_US","English (United States)",0,
        "Welcome to the StarOS installer!",
        "Select language:",
        "Next","Back","Install",
        "Installation disk:","Username:","Password:","Hostname:","Timezone:",
        "Installation complete!","Installing...",
        "StarOS installed successfully!","Reboot",
        "Language","Disk","User","Install"
    },
    {
        "es_ES","Español",2,
        "¡Bienvenido al instalador de StarOS!",
        "Seleccione el idioma:",
        "Siguiente","Atrás","Instalar",
        "Disco de instalación:","Usuario:","Contraseña:","Nombre del PC:","Zona horaria:",
        "¡Instalación completa!","Instalando...",
        "¡StarOS instalado con éxito!","Reiniciar",
        "Idioma","Disco","Usuario","Instalar"
    },
    {
        "de_DE","Deutsch",3,
        "Willkommen beim StarOS-Installationsprogramm!",
        "Sprache wählen:",
        "Weiter","Zurück","Installieren",
        "Installationsdisk:","Benutzername:","Passwort:","Rechnername:","Zeitzone:",
        "Installation abgeschlossen!","Installiere...",
        "StarOS erfolgreich installiert!","Neustart",
        "Sprache","Disk","Benutzer","Installieren"
    },
    {
        "fr_FR","Français",4,
        "Bienvenue dans l'installateur StarOS!",
        "Choisir la langue:",
        "Suivant","Retour","Installer",
        "Disque d'installation:","Utilisateur:","Mot de passe:","Nom d'hôte:","Fuseau horaire:",
        "Installation terminée!","Installation...",
        "StarOS installé avec succès!","Redémarrer",
        "Langue","Disque","Utilisateur","Installer"
    },
    {
        "it_IT","Italiano",5,
        "Benvenuto nell'installer di StarOS!",
        "Seleziona la lingua:",
        "Avanti","Indietro","Installa",
        "Disco d'installazione:","Utente:","Password:","Nome PC:","Fuso orario:",
        "Installazione completata!","Installazione...",
        "StarOS installato con successo!","Riavvia",
        "Lingua","Disco","Utente","Installa"
    },
    {
        "ru_RU","Русский",6,
        "Dobro pozhalovat v ustanovschik StarOS!",
        "Vyberite yazyk:",
        "Dalee","Nazad","Ustanovit",
        "Disk:","Polzovatel:","Parol:","Imya PC:","Chasovoy poyas:",
        "Ustanovka zavershena!","Ustanavlivayu...",
        "StarOS uspeshno ustanovlen!","Perezagruzit",
        "Yazyk","Disk","Polzovatel","Ustanovit"
    },
    {
        "ja_JP","日本語 (romaji)",7,
        "StarOS installer e yokoso!",
        "Gengo wo sentaku:",
        "Tsugi","Modoru","Insutoru",
        "Disuku:","Yuza:","Pasuwado:","Hosuto:","Taimu zon:",
        "Insutoru kanryo!","Insutoru chuu...",
        "StarOS wo insutoru shimashita!","Saiki dou",
        "Gengo","Disuku","Yuza","Insutoru"
    },
    {
        "zh_CN","中文 (pinyin)",8,
        "Huanying shi yong StarOS anzhuang chengxu!",
        "Xuanze yuyan:",
        "Xia yi bu","Fanhui","Anzhuang",
        "Anzhuang cipan:","Yonghuming:","Mima:","Jisuanji ming:","Shi qu:",
        "Anzhuang wancheng!","Zhengzai anzhuang...",
        "StarOS anzhuang chenggong!","Chongqi",
        "Yuyan","Cipan","Yonghu","Anzhuang"
    },
    {
        "ar_AR","العربية (transliterated)",9,
        "Marhaba fi mutsabbat StarOS!",
        "Ikhtiyar al-lugha:",
        "Tali","Sabiq","Tathbit",
        "Qurs:","Mustakhdim:","Kalima:","Ism:","Mintaqa:",
        "Itkmal al-tathbit!","Jari al-tathbit...",
        "Tathbit StarOS bi-najah!","Iada al-tashghil",
        "Lugha","Qurs","Mustakhdim","Tathbit"
    },
    {
        "nl_NL","Nederlands",0,
        "Welkom bij het StarOS installatieprogramma!",
        "Selecteer taal:",
        "Volgende","Terug","Installeren",
        "Installatiedisk:","Gebruiker:","Wachtwoord:","Computernaam:","Tijdzone:",
        "Installatie voltooid!","Installeren...",
        "StarOS succesvol geinstalleerd!","Herstarten",
        "Taal","Disk","Gebruiker","Installeren"
    },
    {
        "pl_PL","Polski",0,
        "Witaj w instalatorze StarOS!",
        "Wybierz jezyk:",
        "Dalej","Wstecz","Zainstaluj",
        "Dysk:","Uzytkownik:","Haslo:","Nazwa PC:","Strefa czasowa:",
        "Instalacja zakonczona!","Instalowanie...",
        "StarOS zainstalowany pomyslnie!","Uruchom ponownie",
        "Jezyk","Dysk","Uzytkownik","Zainstaluj"
    },
    {
        "tr_TR","Türkçe",0,
        "StarOS yukleyicisine hosgeldiniz!",
        "Dil secin:",
        "Ileri","Geri","Yukle",
        "Disk:","Kullanici:","Sifre:","PC adi:","Saat dilimi:",
        "Yukleme tamamlandi!","Yukleniyor...",
        "StarOS basariyla yuklendi!","Yeniden baslat",
        "Dil","Disk","Kullanici","Yukle"
    },
    {
        "ko_KR","한국어 (romaji)",0,
        "StarOS seolichelo osin geol hwangyeonghamnida!",
        "Eoneoeleul seontaeg:",
        "Daeum","Dwilo","Seolichada",
        "Disukeu:","Sayongja:","Biseubeodeu:","Ho-ost:","Sigandae:",
        "Seolich wanryo!","Seolich jung...",
        "StarOS ga seolich doeeosseumnida!","Dasi sijag",
        "Eoneo","Disukeu","Sayongja","Seolichada"
    },
    {0}
};

static int lang_count = 14;

/* ══════════════════════════════════════════════════════════
 * ETAPAS DO INSTALADOR
 * ════════════════════════════════════════════════════════*/
typedef enum {
    STEP_LANG=0,
    STEP_DISK,
    STEP_USER,
    STEP_INSTALL,
    STEP_DONE,
    STEP_MAX
} install_step_t;

static install_step_t step = STEP_LANG;
static int  sel_lang  = 0;   /* idioma selecionado */
static char sel_disk[32]    = "/dev/sda";
static char cfg_user[64]    = "user";
static char cfg_pass[64]    = "";
static char cfg_host[64]    = "staros-pc";
static char cfg_tz[32]      = "America/Sao_Paulo";
static int  install_pct     = 0;

static window_t* inst_win   = (void*)0;
static window_t* prog_win   = (void*)0;

/* Widgets principais */
static widget_t* wl_title   = (void*)0;
static widget_t* wl_sub     = (void*)0;
static widget_t* wl_lang_list=(void*)0;
static widget_t* wl_disk    = (void*)0;
static widget_t* wi_user    = (void*)0;
static widget_t* wi_pass    = (void*)0;
static widget_t* wi_host    = (void*)0;
static widget_t* wi_tz      = (void*)0;
static widget_t* wb_next    = (void*)0;
static widget_t* wb_back    = (void*)0;
static widget_t* wb_install = (void*)0;
static widget_t* wl_prog_lbl= (void*)0;
static widget_t* wb_prog    = (void*)0;

static const lang_t* L(void){ return &langs[sel_lang]; }

/* ── Funções auxiliares ───────────────────────────────────── */
static void draw_steps(void){
    /* barra de etapas no topo */
    fb_rect(0,0,800,40,0x1A0A3A);
    fb_rect(0,39,800,2,0x7C3AED);
    const char* names[4] = {
        L()->s_step_lang, L()->s_step_disk,
        L()->s_step_user, L()->s_step_install
    };
    for(int i=0;i<4;i++){
        int x = 20+i*200;
        u32 c = (i==(int)step)?0x7C3AED:0x6B5FA0;
        if(i<(int)step) c=0x40E0A0;
        fb_rect(x,10,180,20,c);
        fb_draw_text(x+4,14,names[i],0xEDE9FE);
    }
}

static void draw_logo(void){
    fb_clear(C_BG);
    /* estrelas */
    for(int i=0;i<60;i++){
        int sx=(i*97+31)%800, sy=(i*53+17)%560;
        fb_rect(sx,sy,1,1,0x6B5FA0);
    }
    /* logo grande */
    fb_draw_text(300,200,"S t a r O S",C_LOGO);
    fb_draw_text(280,220,"Beta Edition",C_GRAY);
}

/* ── Callbacks ────────────────────────────────────────────── */
static void install_do(void);
static void rebuild_step(void);

static void cb_next(widget_t* w){ (void)w;
    if(step<STEP_INSTALL){ step++; rebuild_step(); }
    else install_do();
}
static void cb_back(widget_t* w){ (void)w;
    if(step>STEP_LANG){ step--; rebuild_step(); }
}
static void cb_lang_sel(widget_t* w){
    int idx=wgt_listbox_selected(w);
    if(idx>=0&&idx<lang_count){
        sel_lang=idx;
        kbd_set_layout(langs[idx].kbd_layout);
        rebuild_step();
    }
}

/* ── Simulação de instalação ─────────────────────────────── */
static const char* install_stages[]={
    "Particionando disco...",
    "Formatando partições...",
    "Copiando arquivos do sistema...",
    "Instalando bootloader (GRUB)...",
    "Configurando usuário...",
    "Configurando rede...",
    "Configurando idioma e teclado...",
    "Finalizando instalação...",
    (void*)0
};

static void install_do(void){
    step = STEP_INSTALL;

    /* janela de progresso */
    prog_win = window_create(L()->s_installing, 200,180,400,180);
    window_set_bg(prog_win,C_WIN);
    wl_prog_lbl = wgt_label(prog_win, 10,10, install_stages[0]);
    wgt_label_set_color(wl_prog_lbl,C_WHITE);
    wb_prog = wgt_progressbar(prog_win,10,40,370,24);
    wgt_progressbar_set_color(wb_prog,C_ACCENT,C_PANEL);
    window_render(prog_win);

    for(int s=0;install_stages[s];s++){
        wgt_label_set(wl_prog_lbl, install_stages[s]);
        int pct=(s+1)*100/8;
        wgt_progressbar_set(wb_prog,pct);
        window_render(prog_win);
        /* simula tempo (500ms por etapa) */
        u32 dl=timer_ticks+50;
        while(timer_ticks<dl) __asm__ volatile("hlt");
    }

    /* Escreve arquivos de configuração */
    vfs_file_t* f;

    f=vfs_open("/etc/locale.conf",VFS_O_WRITE|VFS_O_CREATE);
    if(f){
        char buf[64];
        ksnprintf(buf,63,"LANG=%s\n",langs[sel_lang].code);
        vfs_write(f,(u8*)buf,(u32)__builtin_strlen(buf));
        ksnprintf(buf,63,"KEYMAP=%d\n",langs[sel_lang].kbd_layout);
        vfs_write(f,(u8*)buf,(u32)__builtin_strlen(buf));
        vfs_close(f);
    }
    f=vfs_open("/etc/hostname",VFS_O_WRITE|VFS_O_CREATE);
    if(f){
        vfs_write(f,(u8*)cfg_host,(u32)__builtin_strlen(cfg_host));
        vfs_write(f,(u8*)"\n",1);
        vfs_close(f);
    }
    f=vfs_open("/etc/timezone",VFS_O_WRITE|VFS_O_CREATE);
    if(f){
        vfs_write(f,(u8*)cfg_tz,(u32)__builtin_strlen(cfg_tz));
        vfs_write(f,(u8*)"\n",1);
        vfs_close(f);
    }
    f=vfs_open("/etc/passwd",VFS_O_WRITE|VFS_O_CREATE);
    if(f){
        char buf[128];
        ksnprintf(buf,127,"%s:x:1000:1000::/home/%s:/bin/sh\n",
                  cfg_user,cfg_user);
        vfs_write(f,(u8*)buf,(u32)__builtin_strlen(buf));
        vfs_close(f);
    }

    step = STEP_DONE;
    rebuild_step();
}

/* ── Constrói a tela de cada etapa ───────────────────────── */
static void rebuild_step(void){
    /* redesenha fundo */
    draw_logo();
    draw_steps();

    if(!inst_win){
        inst_win=window_create("StarOS Installer",50,50,700,480);
        window_set_bg(inst_win,C_WIN);
    }

    /* limpa widgets antigos redesenhando a janela */
    window_render(inst_win);

    switch(step){
    case STEP_LANG:{
        wl_title=wgt_label(inst_win,20,10,L()->s_welcome);
        wgt_label_set_color(wl_title,C_LOGO);
        wgt_label(inst_win,20,30,L()->s_select_lang);
        wl_lang_list=wgt_listbox(inst_win,20,50,640,320);
        for(int i=0;i<lang_count;i++)
            wgt_listbox_add(wl_lang_list,langs[i].name);
        wb_next=wgt_button(inst_win,540,390,140,28,L()->s_next,cb_next);
        u32 bc[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
        wgt_set_colors(wb_next,bc);
        break;
    }
    case STEP_DISK:{
        wgt_label(inst_win,20,10,"Disco de destino:");
        wgt_label_set_color(wgt_label(inst_win,20,10,""),C_LOGO);
        wl_disk=wgt_listbox(inst_win,20,50,640,200);
        wgt_listbox_add(wl_disk,"/dev/sda  (ATA)");
        wgt_listbox_add(wl_disk,"/dev/nvme0 (NVMe)");
        wgt_listbox_add(wl_disk,"/dev/sdb  (USB)");
        wgt_label(inst_win,20,270,"AVISO: todos os dados serão apagados!");
        wgt_label_set_color(wgt_label(inst_win,20,270,""),C_RED);
        wb_back=wgt_button(inst_win,20,390,140,28,L()->s_back,cb_back);
        wb_next=wgt_button(inst_win,540,390,140,28,L()->s_next,cb_next);
        u32 bc[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
        wgt_set_colors(wb_next,bc);
        break;
    }
    case STEP_USER:{
        wgt_label(inst_win,20,10,L()->s_user);
        wi_user=wgt_input(inst_win,20,30,400,26);
        wgt_input_set(wi_user,cfg_user);
        wgt_label(inst_win,20,65,L()->s_pass);
        wi_pass=wgt_input(inst_win,20,83,400,26);
        wgt_label(inst_win,20,118,L()->s_hostname);
        wi_host=wgt_input(inst_win,20,136,400,26);
        wgt_input_set(wi_host,cfg_host);
        wgt_label(inst_win,20,172,L()->s_timezone);
        wi_tz=wgt_input(inst_win,20,190,400,26);
        wgt_input_set(wi_tz,cfg_tz);
        wb_back=wgt_button(inst_win,20,390,140,28,L()->s_back,cb_back);
        wb_next=wgt_button(inst_win,540,390,140,28,L()->s_install,cb_next);
        u32 bc[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
        wgt_set_colors(wb_next,bc);
        break;
    }
    case STEP_DONE:{
        fb_clear(C_BG);
        fb_draw_text(200,200,L()->s_done,C_GREEN);
        fb_draw_text(200,240,"Reinicie para usar o StarOS.",C_WHITE);
        wgt_button(inst_win,300,350,200,36,L()->s_reboot,cb_back);
        extern void acpi_reboot(void);
        /* reboot ao clicar */
        break;
    }
    default: break;
    }
    window_render(inst_win);
}

/* ── Ponto de entrada ─────────────────────────────────────── */
void installer_main(void){
    step     = STEP_LANG;
    sel_lang = 0;

    draw_logo();
    draw_steps();
    rebuild_step();

    kprintf("[installer] Iniciado\n");

    for(;;){
        gui_event_t e;
        extern int gui_poll_event(gui_event_t*);
        if(gui_poll_event(&e)){
            extern void gui_dispatch_event(gui_event_t*,void*);
            if(inst_win) gui_dispatch_event(&e,inst_win);
            if(prog_win) gui_dispatch_event(&e,prog_win);
            /* atualiza seleção de idioma */
            if(step==STEP_LANG&&e.type==1&&wl_lang_list){
                cb_lang_sel(wl_lang_list);
            }
        }
        __asm__ volatile("hlt");
    }
}

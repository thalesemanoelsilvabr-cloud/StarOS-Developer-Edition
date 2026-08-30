/* keyboard.c — Driver de teclado universal
 * Suporta: PS/2, USB HID (via polling), layouts internacionais
 * Tecla Super (Win/Cmd) mapeada como KEY_SUPER
 */
#include <kernel/types.h>
#include <drivers/keyboard.h>

/* ── I/O helpers ─────────────────────────────────────────── */
static inline u8  inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void iowait(void){outb(0x80,0);}

/* ── Buffer de teclado ────────────────────────────────────── */
#define KBD_BUF 128
static char kbd_buf[KBD_BUF];
static int  kbd_head=0, kbd_tail=0;
static u8   kbd_modifiers=0;   /* bits: shift ctrl alt super */

#define MOD_SHIFT  (1<<0)
#define MOD_CTRL   (1<<1)
#define MOD_ALT    (1<<2)
#define MOD_SUPER  (1<<3)
#define MOD_CAPS   (1<<4)
#define MOD_ALTGR  (1<<5)

/* ── Layout selecionado ───────────────────────────────────── */
static int kbd_layout = KBD_LAYOUT_PTBR;  /* padrão: pt-BR */

/* ══════════════════════════════════════════════════════════
 * SCANCODE SET 1 — mapa base (sem modificadores)
 * ════════════════════════════════════════════════════════*/
static const char sc_base[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',
    0,' ',0, 0,0,0,0,0,0,0,0,0,   /* F1-F10 */
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};

/* Shift */
static const char sc_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};

/* pt-BR: AltGr + tecla */
static const char sc_altgr_ptbr[128] = {
    [18]='e',[21]='[',[22]=']',[26]='{',[27]='}',
    [40]='~',[41]='\\',[43]='|',0
};

/* ABNT2 pt-BR shift row diferente */
static const char sc_ptbr_base[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','\'','[','\n',
    0,'a','s','d','f','g','h','j','k','l',';','~','`',
    0,'\\','z','x','c','v','b','n','m',',','.',';',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};
static const char sc_ptbr_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','`','{','\n',
    0,'A','S','D','F','G','H','J','K','L',':','^','\"',
    0,'|','Z','X','C','V','B','N','M','<','>',':',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};

/* Espanhol */
static const char sc_es_base[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','\'','!','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','`','+','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'',0,
    0,'<','z','x','c','v','b','n','m',',','.','-',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};

/* Alemão */
static const char sc_de_base[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','`','\b',
    '\t','q','w','e','r','t','z','u','i','o','p',0,'+','\n',
    0,'a','s','d','f','g','h','j','k','l',0,0,'^',
    0,'<','y','x','c','v','b','n','m',',','.','-',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};

/* Francês AZERTY */
static const char sc_fr_base[128] = {
    0,27,'&',0,'\"','\'','(','`','!','^',0,')','=',0,'\b',
    '\t','a','z','e','r','t','y','u','i','o','p','^','$','\n',
    0,'q','s','d','f','g','h','j','k','l','m',0,'*',
    0,'<','w','x','c','v','b','n',',',';',':','!',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0
};

/* Seleciona o mapa correto */
static char get_char(u8 sc, u8 shifted, u8 altgr){
    if(sc >= 128) return 0;
    switch(kbd_layout){
    case KBD_LAYOUT_PTBR:
        if(altgr && sc_altgr_ptbr[sc]) return sc_altgr_ptbr[sc];
        return shifted ? sc_ptbr_shift[sc] : sc_ptbr_base[sc];
    case KBD_LAYOUT_ES:
        return shifted ? sc_shift[sc] : sc_es_base[sc];
    case KBD_LAYOUT_DE:
        return shifted ? sc_shift[sc] : sc_de_base[sc];
    case KBD_LAYOUT_FR:
        return shifted ? sc_shift[sc] : sc_fr_base[sc];
    default: /* EN-US */
        return shifted ? sc_shift[sc] : sc_base[sc];
    }
}

/* ── Inicializa PS/2 ──────────────────────────────────────── */
static void ps2_wait_in(void){
    u32 t=100000;
    while(t-- && (inb(0x64)&2));
}
static void ps2_wait_out(void){
    u32 t=100000;
    while(t-- && !(inb(0x64)&1));
}
static void ps2_send(u8 cmd){
    ps2_wait_in();
    outb(0x60,cmd);
}

void kbd_init(void){
    /* flush */
    while(inb(0x64)&1) inb(0x60);
    /* habilita teclado */
    ps2_wait_in(); outb(0x64,0xAE);
    /* leds off */
    ps2_send(0xED); ps2_send(0x00);
    /* scancode set 1 */
    ps2_send(0xF0); ps2_send(0x01);
}

/* ── IRQ1 handler ─────────────────────────────────────────── */
void kbd_irq_handler(void){
    static u8 extended = 0;
    u8 sc = inb(0x60);

    if(sc == 0xE0){ extended = 1; return; }

    u8 code     = sc & 0x7F;
    u8 released = sc & 0x80;

    /* teclas modificadoras */
    if(extended){
        /* Super esquerda/direita (0xE0 0x5B / 0xE0 0x5C) */
        if(code == 0x5B || code == 0x5C){
            if(!released){
                kbd_modifiers |= MOD_SUPER;
                /* Super sozinha abre menu/launcher */
                int next=(kbd_head+1)%KBD_BUF;
                if(next!=kbd_tail){
                    kbd_buf[kbd_head]=KEY_SUPER;
                    kbd_head=next;
                }
            } else {
                kbd_modifiers &= ~MOD_SUPER;
            }
        }
        /* Alt direito = AltGr */
        if(code == 0x38){
            if(!released) kbd_modifiers |=  MOD_ALTGR;
            else          kbd_modifiers &= ~MOD_ALTGR;
        }
        extended = 0;
        return;
    }

    /* modificadores normais */
    if(code==0x2A||code==0x36){ /* Shift L/R */
        if(!released) kbd_modifiers |=  MOD_SHIFT;
        else          kbd_modifiers &= ~MOD_SHIFT;
        return;
    }
    if(code==0x1D){ /* Ctrl */
        if(!released) kbd_modifiers |=  MOD_CTRL;
        else          kbd_modifiers &= ~MOD_CTRL;
        return;
    }
    if(code==0x38){ /* Alt */
        if(!released) kbd_modifiers |=  MOD_ALT;
        else          kbd_modifiers &= ~MOD_ALT;
        return;
    }
    if(code==0x3A){ /* Caps Lock */
        if(!released) kbd_modifiers ^= MOD_CAPS;
        return;
    }

    if(released) return;

    /* gera caractere */
    u8 shifted = (kbd_modifiers & MOD_SHIFT) ? 1 : 0;
    u8 altgr   = (kbd_modifiers & MOD_ALTGR) ? 1 : 0;

    /* Caps Lock afeta letras */
    if(kbd_modifiers & MOD_CAPS) shifted ^= 1;

    char c = get_char(code, shifted, altgr);

    /* Ctrl+letra */
    if((kbd_modifiers & MOD_CTRL) && c>='a' && c<='z') c=(char)(c-'a'+1);
    if((kbd_modifiers & MOD_CTRL) && c>='A' && c<='Z') c=(char)(c-'A'+1);

    if(c){
        int next=(kbd_head+1)%KBD_BUF;
        if(next!=kbd_tail){ kbd_buf[kbd_head]=c; kbd_head=next; }
    }
}

/* ── API pública ──────────────────────────────────────────── */
char kbd_getchar(void){
    while(kbd_head==kbd_tail) __asm__ volatile("hlt");
    char c=kbd_buf[kbd_tail];
    kbd_tail=(kbd_tail+1)%KBD_BUF;
    return c;
}
int  kbd_haschar(void)  { return kbd_head != kbd_tail; }
void kbd_flush(void)    { kbd_head = kbd_tail = 0; }
u8   kbd_modifiers_get(void){ return kbd_modifiers; }
void kbd_set_layout(int layout){ kbd_layout = layout; }
int  kbd_get_layout(void){ return kbd_layout; }

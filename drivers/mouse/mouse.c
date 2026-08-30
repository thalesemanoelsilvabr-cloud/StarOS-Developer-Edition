/* mouse.c — Driver de mouse universal (PS/2 + USB polling) */
#include <kernel/types.h>
#include <drivers/mouse.h>

static inline u8   inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void iowait(void){outb(0x80,0);}

/* ── Estado ───────────────────────────────────────────────── */
static int  ms_x = 400, ms_y = 300;
static int  ms_btn = 0;
static int  ms_scroll = 0;
static u8   ms_cycle = 0;
static u8   ms_bytes[4];
static int  ms_has_wheel = 0;
static int  ms_speed = 2;       /* multiplicador de velocidade */
static int  ms_screen_w = 800;
static int  ms_screen_h = 600;

/* ── Callback de evento ───────────────────────────────────── */
static mouse_event_cb_t ms_callback = (void*)0;

/* ── PS/2 helpers ─────────────────────────────────────────── */
static void ps2_wait_in(void){u32 t=100000;while(t--&&(inb(0x64)&2));}
static void ps2_wait_out(void){u32 t=100000;while(t--&&!(inb(0x64)&1));}

static void mouse_send(u8 cmd){
    ps2_wait_in(); outb(0x64,0xD4);
    ps2_wait_in(); outb(0x60,cmd);
    ps2_wait_out(); inb(0x60); /* ACK */
}

void mouse_init(void){
    /* habilita porta auxiliar */
    ps2_wait_in(); outb(0x64,0xA8);
    /* habilita IRQ12 no byte de comando */
    ps2_wait_in(); outb(0x64,0x20);
    ps2_wait_out();
    u8 status = (inb(0x60) | 2) & ~0x20;
    ps2_wait_in(); outb(0x64,0x60);
    ps2_wait_in(); outb(0x60,status);
    /* detecta scroll wheel (z-axis) */
    mouse_send(0xF3); mouse_send(200);
    mouse_send(0xF3); mouse_send(100);
    mouse_send(0xF3); mouse_send(80);
    mouse_send(0xF2); /* pede device ID */
    ps2_wait_out(); u8 id = inb(0x60);
    ms_has_wheel = (id == 3 || id == 4);
    /* taxa de amostragem */
    mouse_send(0xF3); mouse_send(100);
    /* resolução */
    mouse_send(0xE8); mouse_send(3); /* 8 counts/mm */
    /* inicia streaming */
    mouse_send(0xF4);
}

/* ── IRQ12 handler ────────────────────────────────────────── */
void mouse_irq_handler(void){
    u8 b = inb(0x60);
    u8 max_bytes = ms_has_wheel ? 4 : 3;

    ms_bytes[ms_cycle++] = b;
    if(ms_cycle < max_bytes) return;
    ms_cycle = 0;

    /* valida byte de flags */
    if(!(ms_bytes[0] & 0x08)) return;

    /* botões */
    ms_btn = ms_bytes[0] & 0x07;

    /* movimento com overflow check */
    int dx = (int)(s8)ms_bytes[1];
    int dy = -(int)(s8)ms_bytes[2];
    if(ms_bytes[0] & 0x40) dx = 0; /* overflow X */
    if(ms_bytes[0] & 0x80) dy = 0; /* overflow Y */

    /* velocidade adaptativa */
    int speed = ms_speed;
    if(dx > 8 || dx < -8 || dy > 8 || dy < -8) speed *= 2;

    ms_x += dx * speed;
    ms_y += dy * speed;

    /* limites da tela */
    if(ms_x < 0)          ms_x = 0;
    if(ms_y < 0)          ms_y = 0;
    if(ms_x >= ms_screen_w) ms_x = ms_screen_w - 1;
    if(ms_y >= ms_screen_h) ms_y = ms_screen_h - 1;

    /* scroll wheel */
    ms_scroll = 0;
    if(ms_has_wheel && ms_bytes[3]){
        ms_scroll = (ms_bytes[3] & 0x08) ?
                    (int)(ms_bytes[3] | 0xF0) : /* negativo */
                    (int)(ms_bytes[3] & 0x07);  /* positivo */
    }

    /* dispara callback */
    if(ms_callback){
        mouse_event_t e;
        e.x=ms_x; e.y=ms_y;
        e.btn=ms_btn; e.scroll=ms_scroll;
        ms_callback(&e);
    }
}

/* ── API ──────────────────────────────────────────────────── */
int  mouse_get_x(void)    { return ms_x; }
int  mouse_get_y(void)    { return ms_y; }
int  mouse_get_btn(void)  { return ms_btn; }
int  mouse_get_scroll(void){ return ms_scroll; }
void mouse_set_speed(int s){ ms_speed = (s<1)?1:(s>8)?8:s; }
void mouse_set_screen(int w,int h){ ms_screen_w=w; ms_screen_h=h; }
void mouse_set_callback(mouse_event_cb_t cb){ ms_callback=cb; }
void mouse_set_pos(int x,int y){ ms_x=x; ms_y=y; }

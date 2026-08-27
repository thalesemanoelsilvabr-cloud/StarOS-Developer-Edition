/* mouse.c — PS/2 mouse IRQ12 */
#include <kernel/types.h>
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline u8   inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void iowait(void){outb(0x80,0);}

static int mouse_x=400, mouse_y=300;
static int mouse_btn=0;
static u8  mouse_cycle=0;
static u8  mouse_bytes[3];

int  mouse_get_x(void){ return mouse_x; }
int  mouse_get_y(void){ return mouse_y; }
int  mouse_get_btn(void){ return mouse_btn; }

static void mouse_send(u8 cmd){
    outb(0x64,0xD4); iowait();
    outb(0x60,cmd);
    while(inb(0x60)!=0xFA);
}
void mouse_init(void){
    outb(0x64,0xA8); iowait();
    outb(0x64,0x20); iowait();
    u8 status = inb(0x60) | 2;
    outb(0x64,0x60); iowait();
    outb(0x60,status); iowait();
    mouse_send(0xF6);
    mouse_send(0xF4);
}
void mouse_irq_handler(void){
    u8 b = inb(0x60);
    switch(mouse_cycle){
        case 0: mouse_bytes[0]=b; mouse_cycle=1; break;
        case 1: mouse_bytes[1]=b; mouse_cycle=2; break;
        case 2:
            mouse_bytes[2]=b; mouse_cycle=0;
            mouse_btn = mouse_bytes[0] & 7;
            int dx = (int)(s8)mouse_bytes[1];
            int dy = -(int)(s8)mouse_bytes[2];
            mouse_x += dx; mouse_y += dy;
            if(mouse_x<0) mouse_x=0;
            if(mouse_y<0) mouse_y=0;
            if(mouse_x>799) mouse_x=799;
            if(mouse_y>599) mouse_y=599;
            break;
    }
}

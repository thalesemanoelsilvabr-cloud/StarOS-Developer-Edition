/* pic.c */
#include <kernel/types.h>
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void iowait(void){outb(0x80,0);}
void pic_init(void){
    outb(0x20,0x11);iowait(); outb(0xA0,0x11);iowait();
    outb(0x21,0x20);iowait(); outb(0xA1,0x28);iowait();
    outb(0x21,0x04);iowait(); outb(0xA1,0x02);iowait();
    outb(0x21,0x01);iowait(); outb(0xA1,0x01);iowait();
    outb(0x21,0xF9); /* IRQ0+IRQ1 habilitados */
    outb(0xA1,0xEF); /* IRQ12 habilitado no slave */
}
void pic_eoi(u8 irq){ if(irq>=8) outb(0xA0,0x20); outb(0x20,0x20); }

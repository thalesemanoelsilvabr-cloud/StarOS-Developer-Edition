/* timer.c — PIT 100 Hz */
#include <kernel/types.h>
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
void timer_init(void){
    /* PIT canal 0, 100 Hz */
    u16 div=1193180/100;
    outb(0x43,0x36);
    outb(0x40,(u8)(div&0xFF));
    outb(0x40,(u8)(div>>8));
}

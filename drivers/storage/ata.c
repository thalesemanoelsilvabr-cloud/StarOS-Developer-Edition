/* ata.c — ATA PIO simples (inicializacao apenas) */
#include <kernel/types.h>
extern void kprintf(const char*,...);
static inline u8  inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
void ata_init(void){
    outb(0x3F6,0); /* reset */
    /* detecta disco primario */
    outb(0x1F6,0xA0);
    for(int i=0;i<15;i++) inb(0x1F7);
    u8 status=inb(0x1F7);
    if(status==0xFF) kprintf("[ata] Nenhum disco\n");
    else kprintf("[ata] Disco primario detectado (status=%02X)\n",status);
}

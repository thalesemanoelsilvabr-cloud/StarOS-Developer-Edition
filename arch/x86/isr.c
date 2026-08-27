/* isr.c */
#include <kernel/types.h>
#include <kernel/kernel.h>
u32 volatile timer_ticks = 0;
void kbd_irq_handler(void);
void mouse_irq_handler(void);
void ne2k_irq_handler(void);
void pic_eoi(u8 irq);
typedef struct PACKED {
    u32 ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,int_no,err,eip,cs,eflags,useresp,ss;
} regs_t;
void isr_handler(regs_t* r){
    kprintf("[FAULT] #%u EIP=%08X\n",r->int_no,r->eip);
    kpanic("CPU exception");
}
void irq_handler(regs_t* r){
    u8 irq=(u8)(r->int_no-32);
    if(irq==0)  timer_ticks++;
    else if(irq==1)  kbd_irq_handler();
    else if(irq==10) ne2k_irq_handler();
    else if(irq==12) mouse_irq_handler();
    pic_eoi(irq);
}

/* idt.c */
#include <kernel/types.h>

typedef struct {
    u16 b0;
    u16 sel;
    u8  z;
    u8  fl;
    u16 b1;
} __attribute__((packed)) idt_e;

typedef struct {
    u16 lim;
    u32 base;
} __attribute__((packed)) idt_ptr;

static idt_e   idt[256];
static idt_ptr idtp;

extern void isr0(void),  isr1(void),  isr2(void),  isr3(void);
extern void isr6(void),  isr8(void),  isr13(void), isr14(void);
extern void irq0(void),  irq1(void),  irq10(void), irq12(void);
extern void idt_flush(u32);

static void set(u8 n, u32 b, u16 s, u8 f){
    idt[n].b0  = (u16)(b & 0xFFFF);
    idt[n].b1  = (u16)(b >> 16);
    idt[n].sel = s;
    idt[n].z   = 0;
    idt[n].fl  = f;
}

void idt_init(void){
    idtp.lim  = (u16)(sizeof(idt) - 1);
    idtp.base = (u32)&idt;

    for(int i = 0; i < 256; i++) set((u8)i, 0, 0, 0);

    /* exceções CPU */
    set(0,  (u32)isr0,  0x08, 0x8E);
    set(1,  (u32)isr1,  0x08, 0x8E);
    set(2,  (u32)isr2,  0x08, 0x8E);
    set(3,  (u32)isr3,  0x08, 0x8E);
    set(6,  (u32)isr6,  0x08, 0x8E);
    set(8,  (u32)isr8,  0x08, 0x8E);
    set(13, (u32)isr13, 0x08, 0x8E);
    set(14, (u32)isr14, 0x08, 0x8E);

    /* IRQs (remapeados para 0x20+) */
    set(32, (u32)irq0,  0x08, 0x8E);  /* timer   */
    set(33, (u32)irq1,  0x08, 0x8E);  /* teclado */
    set(42, (u32)irq10, 0x08, 0x8E);  /* NIC     */
    set(44, (u32)irq12, 0x08, 0x8E);  /* mouse   */

    idt_flush((u32)&idtp);
}

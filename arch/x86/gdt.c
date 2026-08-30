/* gdt.c */
#include <kernel/types.h>

typedef struct {
    u16 lim0;
    u16 base0;
    u8  base1;
    u8  acc;
    u8  gran;
    u8  base2;
} __attribute__((packed)) gdt_e;

typedef struct {
    u16 lim;
    u32 base;
} __attribute__((packed)) gdt_ptr;

static gdt_e   gdt[5];
static gdt_ptr gdtp;

static void set(int i, u32 b, u32 l, u8 a, u8 g){
    gdt[i].base0 = (u16)(b & 0xFFFF);
    gdt[i].base1 = (u8)((b >> 16) & 0xFF);
    gdt[i].base2 = (u8)(b >> 24);
    gdt[i].lim0  = (u16)(l & 0xFFFF);
    gdt[i].gran  = (u8)(((l >> 16) & 0xF) | (g & 0xF0));
    gdt[i].acc   = a;
}

extern void gdt_flush(u32);

void gdt_init(void){
    gdtp.lim  = (u16)(sizeof(gdt) - 1);
    gdtp.base = (u32)&gdt;
    set(0, 0, 0,       0,    0   );   /* null */
    set(1, 0, 0xFFFFF, 0x9A, 0xCF);  /* kernel code */
    set(2, 0, 0xFFFFF, 0x92, 0xCF);  /* kernel data */
    set(3, 0, 0xFFFFF, 0xFA, 0xCF);  /* user code   */
    set(4, 0, 0xFFFFF, 0xF2, 0xCF);  /* user data   */
    gdt_flush((u32)&gdtp);
}

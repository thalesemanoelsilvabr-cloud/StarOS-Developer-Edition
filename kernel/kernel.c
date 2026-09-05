/* kernel.c — kernel_main */
#include <kernel/types.h>
#include <kernel/kernel.h>
#include <kernel/macro.h>
#include <drivers/terminal.h>
#include <drivers/keyboard.h>
#include <mm/kmalloc.h>
#include <fs/vfs.h>

extern void gdt_init(void);
extern void idt_init(void);
extern void pic_init(void);
extern void timer_init(void);
extern void mouse_init(void);
extern void ata_init(void);
extern void ne2k_init(void);
extern void net_init(void);
extern void pkg_init(void);
extern void gui_mb2_init(void* mb2);
extern int  gui_has_fb(void);
extern void gui_start(void);
extern void gui_run(void);
extern void sched_init(void);
extern void shell_run(void);

#define MB1_MAGIC  0x2BADB002u
#define MB2_MAGIC  0x36D76289u
#define HEAP_BASE  0x200000u
#define HEAP_BYTES (8u*1024u*1024u)

void kernel_main(u32 magic, void* mb_info){
    /* 1. GDT + IDT + PIC */
    gdt_init();
    idt_init();
    pic_init();

    /* 2. Terminal VGA */
    term_init();
    term_set_color(0x0F, 0x00);
    term_write("  StarOS Beta Edition\n");
    term_write("================================\n");

    /* 3. Framebuffer: a estrutura multiboot fica dentro da area do heap,
       entao ela precisa ser lida antes de qualquer alocacao */
    int has_fb = 0;
    u32 heap_base = HEAP_BASE;
    if(magic == MB2_MAGIC && mb_info){
        gui_mb2_init(mb_info);
        has_fb = gui_has_fb();
        u32 mbi_end = (u32)mb_info + *(u32*)mb_info;
        if(mbi_end > heap_base && (u32)mb_info < heap_base + HEAP_BYTES)
            heap_base = mbi_end;
        term_write(has_fb ? "[OK] Framebuffer VESA (Multiboot2)\n"
                          : "[OK] Multiboot2 sem framebuffer - modo texto\n");
    } else if(magic == MB1_MAGIC && mb_info){
        /* Multiboot1: mbi[0]=flags, bit 12 = framebuffer
           mbi[22]=addr mbi[23]=pitch mbi[24]=width mbi[25]=height */
        u32* mbi = (u32*)mb_info;
        if(mbi[0] & (1u<<12)){
            extern void fb_init(u32*,u32,u32,u32);
            fb_init((u32*)(mbi[22]), mbi[24], mbi[25], mbi[23]);
            has_fb = 1;
            term_write("[OK] Framebuffer VESA (Multiboot1)\n");
        } else {
            term_write("[OK] Multiboot1 sem framebuffer - modo texto\n");
        }
        u32 mbi_end = (u32)mb_info + 128;
        if(mbi_end > heap_base && (u32)mb_info < heap_base + HEAP_BYTES)
            heap_base = mbi_end;
    } else {
        term_write("[AVISO] Bootloader desconhecido\n");
    }

    /* 4. Memoria */
    kmem_init_at(heap_base, HEAP_BYTES);
    term_write("[OK] Heap 8MB\n");

    /* 5. VFS */
    vfs_init();
    term_write("[OK] VFS\n");

    /* 6. Dispositivos */
    kbd_init();
    mouse_init();
    ata_init();
    timer_init();
    term_write("[OK] Teclado, Mouse, ATA, Timer\n");

    /* 7. Interrupcoes: precisam estar ativas antes da rede, que espera
       por timer_ticks enquanto faz DHCP */
    __asm__ volatile("sti");
    term_write("[OK] Interrupcoes ativas\n");

    /* 8. Rede */
    ne2k_init();
    net_init();
    pkg_init();
    term_write("[OK] Rede + PKG\n");

    /* 9. Scheduler */
    sched_init();
    term_write("[OK] Scheduler\n");

    term_write("================================\n");
    term_write("StarOS pronto!\n\n");

    /* 10. Inicia GUI ou shell */
    if(has_fb){
        gui_start();
        gui_run();
    } else {
        shell_run();
    }

    kpanic("kernel_main retornou");
}

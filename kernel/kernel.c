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
extern void gui_start(void);
extern void gui_run(void);
extern void sched_init(void);
extern void shell_run(void);

#define MB1_MAGIC 0x2BADB002u
#define MB2_MAGIC 0x36D76289u

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

    /* 3. Memoria */
    kmem_init();
    term_write("[OK] Heap 8MB\n");

    /* 4. VFS */
    vfs_init();
    term_write("[OK] VFS\n");

    /* 5. Dispositivos */
    kbd_init();
    mouse_init();
    ata_init();
    timer_init();
    term_write("[OK] Teclado, Mouse, ATA, Timer\n");

    /* 6. Rede */
    ne2k_init();
    net_init();
    pkg_init();
    term_write("[OK] Rede + PKG\n");

    /* 7. Scheduler */
    sched_init();
    term_write("[OK] Scheduler\n");

    /* 8. GUI (so se tiver framebuffer) */
    int has_fb = 0;
    if(magic == MB2_MAGIC){
        gui_mb2_init(mb_info);
        has_fb = 1;
        term_write("[OK] Framebuffer VESA (Multiboot2)\n");
    } else if(magic == MB1_MAGIC){
        /* Multiboot1: tenta pegar framebuffer da estrutura */
        /* offset 88 = framebuffer_addr quando flags bit 12 set */
        u32* mbi = (u32*)mb_info;
        if(mbi && (mbi[0] & (1<<12))){
            /* mbi[22]=addr mbi[23]=pitch mbi[24]=width mbi[25]=height mbi[26]=bpp */
            extern void fb_init(u32*,u32,u32,u32);
            fb_init((u32*)(mbi[22]), mbi[24], mbi[25], mbi[23]);
            has_fb = 1;
            term_write("[OK] Framebuffer VESA (Multiboot1)\n");
        } else {
            term_write("[OK] Multiboot1 sem framebuffer — modo texto\n");
        }
    } else {
        term_write("[AVISO] Bootloader desconhecido\n");
    }

    /* 9. Habilita interrupcoes */
    __asm__ volatile("sti");
    term_write("[OK] Interrupcoes ativas\n");
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

/* kernel.c — kernel_main: ponto de entrada principal */
#include <kernel/types.h>
#include <kernel/kernel.h>
#include <kernel/macro.h>
#include <drivers/terminal.h>
#include <drivers/keyboard.h>
#include <mm/kmalloc.h>
#include <fs/vfs.h>

/* forward */
void gdt_init(void);
void idt_init(void);
void pic_init(void);
void timer_init(void);
void mouse_init(void);
void ata_init(void);
void ne2k_init(void);
void net_init(void);
void pkg_init(void);
void gui_mb2_init(void* mb2);
void gui_start(void);
void gui_run(void);
void sched_init(void);

#define MB2_MAGIC_EXPECTED 0x36D76289u

void kernel_main(void* mb2, u32 magic){
    /* 1. GDT + IDT + PIC */
    gdt_init();
    idt_init();
    pic_init();

    /* 2. Terminal VGA */
    term_init();
    term_set_color(0x0F,0x00);
    term_write("StarOS Beta Edition\n");
    term_write("Inicializando...\n");

    /* 3. Memoria */
    kmem_init();
    term_write("[OK] Heap: 8MB\n");

    /* 4. VFS */
    vfs_init();
    term_write("[OK] VFS (ramfs + devfs)\n");

    /* 5. Dispositivos basicos */
    kbd_init();
    mouse_init();
    ata_init();
    timer_init();
    term_write("[OK] Teclado + Mouse + ATA + Timer\n");

    /* 6. Rede */
    ne2k_init();
    net_init();
    pkg_init();
    term_write("[OK] Rede TCP/IP + PKG\n");

    /* 7. Scheduler */
    sched_init();
    term_write("[OK] Scheduler\n");

    /* 8. GUI */
    if(magic == MB2_MAGIC_EXPECTED){
        gui_mb2_init(mb2);
        term_write("[OK] Framebuffer VESA\n");
    } else {
        term_write("[AVISO] Sem Multiboot2 — modo texto\n");
    }

    /* 9. Habilita interrupcoes */
    __asm__ volatile("sti");

    term_write("[OK] StarOS pronto!\n");

    /* 10. GUI loop (ou shell em modo texto) */
    if(magic == MB2_MAGIC_EXPECTED){
        gui_start();
        gui_run();
    } else {
        /* shell simples no terminal */
        extern void shell_run(void);
        shell_run();
    }

    kpanic("kernel_main retornou");
}

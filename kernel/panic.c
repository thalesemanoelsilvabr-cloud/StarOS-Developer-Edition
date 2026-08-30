/* panic.c */
#include <kernel/types.h>
#include <kernel/macro.h>
#include <drivers/terminal.h>

extern void kprintf(const char*, ...);

void kpanic(const char* msg) __attribute__((noreturn));

void kpanic(const char* msg){
    term_set_color(0x0C, 0x00);
    kprintf("\n*** KERNEL PANIC ***\n");
    kprintf("  %s\n", msg);
    kprintf("Sistema parado. Reinicie.\n");
    __asm__ volatile("cli");
    for(;;) __asm__ volatile("hlt");
}

void khalt(void){
    __asm__ volatile("cli; hlt");
}

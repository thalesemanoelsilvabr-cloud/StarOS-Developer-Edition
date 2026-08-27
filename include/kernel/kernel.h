#ifndef KERNEL_H
#define KERNEL_H
#include <kernel/types.h>
void kernel_main(void* mb2, u32 magic);
void kpanic(const char* msg);
void kprintf(const char* fmt, ...);
void khalt(void);
extern u32 volatile timer_ticks;
#endif

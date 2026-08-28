#ifndef KERNEL_H
#define KERNEL_H
#include <kernel/types.h>
void kernel_main(u32 magic, void* mb_info);
void kpanic(const char* msg);
void kprintf(const char* fmt, ...);
void khalt(void);
extern u32 volatile timer_ticks;
#endif
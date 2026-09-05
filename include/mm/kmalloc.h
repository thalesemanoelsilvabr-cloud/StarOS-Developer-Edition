#ifndef KMALLOC_H
#define KMALLOC_H
#include <kernel/types.h>
void  kmem_init(void);
void  kmem_init_at(u32 base, u32 size);
void* kmalloc(u32 size);
void* kcalloc(u32 n, u32 size);
void  kfree(void* ptr);
void* krealloc(void* ptr, u32 new_size);
u32   kmem_used(void);
#endif

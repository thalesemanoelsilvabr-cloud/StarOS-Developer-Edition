/* devfs.c — /dev/* */
#include <kernel/types.h>
extern void kprintf(const char*,...);
void devfs_init(void){ kprintf("[devfs] /dev iniciado\n"); }

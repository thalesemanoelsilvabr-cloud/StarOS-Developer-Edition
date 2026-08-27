/* syscall.c — int 0x80 handler */
#include <kernel/types.h>
extern void kprintf(const char*,...);
typedef struct PACKED { u32 eax,ebx,ecx,edx,esi,edi; } regs_t;
void syscall_handler(regs_t* r){
    switch(r->eax){
        case 0: /* exit */ break;
        case 1: /* write */ kprintf("%s",(char*)r->ebx); break;
        default: kprintf("[syscall] #%u desconhecido\n",r->eax); break;
    }
}

/* init.c — PID 1 */
#include <kernel/types.h>
extern void kprintf(const char*,...);
extern void shell_run(void);
void init_main(void){
    kprintf("[init] PID 1 iniciado\n");
    shell_run();
}

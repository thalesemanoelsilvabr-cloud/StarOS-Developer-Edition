/* sched.c — Round-robin simples */
#include <kernel/types.h>
#define MAX_TASKS 16
typedef struct { u32 esp; u8 used; char name[16]; } task_t;
static task_t tasks[MAX_TASKS];
static int    cur_task=0, ntasks=0;
void sched_init(void){ ntasks=0; }
int  sched_create(void(*fn)(void), const char* name, u32* stack, u32 ssz){
    if(ntasks>=MAX_TASKS) return -1;
    task_t* t=&tasks[ntasks];
    t->esp=(u32)(stack+ssz/4)-5*4;
    u32* sp=(u32*)t->esp;
    sp[4]=(u32)fn; sp[3]=0x202; /* EFLAGS */
    t->used=1;
    for(int i=0;name[i]&&i<15;i++) t->name[i]=name[i];
    return ntasks++;
}
void sched_tick(void){ if(ntasks>1) cur_task=(cur_task+1)%ntasks; }

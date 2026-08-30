/* pipe.c — FIFO ring buffer */
#include <kernel/types.h>
#include <mm/kmalloc.h>
#define PIPE_SZ 4096
typedef struct { u8 buf[PIPE_SZ]; u32 head,tail; } pipe_t;
pipe_t* pipe_create(void){ return (pipe_t*)kmalloc(sizeof(pipe_t)); }
void    pipe_destroy(pipe_t* p){ kfree(p); }
int     pipe_write(pipe_t* p,const u8* d,u32 n){
    u32 w=0;
    while(w<n){ u32 next=(p->head+1)%PIPE_SZ; if(next==p->tail) break; p->buf[p->head]=d[w++]; p->head=next; }
    return (int)w;
}
int     pipe_read(pipe_t* p,u8* d,u32 n){
    u32 r=0;
    while(r<n&&p->tail!=p->head){ d[r++]=p->buf[p->tail]; p->tail=(p->tail+1)%PIPE_SZ; }
    return (int)r;
}

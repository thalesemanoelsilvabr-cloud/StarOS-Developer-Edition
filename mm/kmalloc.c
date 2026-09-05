/* kmalloc.c — heap simples com lista encadeada */
#include <kernel/types.h>
#include <mm/kmalloc.h>

#define HEAP_START  0x200000u   /* 2 MB */
#define HEAP_SIZE   (8*1024*1024u)  /* 8 MB */
#define MAGIC_FREE  0xFEE1DEAD
#define MAGIC_USED  0xC0FFEE00

typedef struct blk {
    u32 magic;
    u32 size;        /* tamanho util (sem header) */
    struct blk* next;
    struct blk* prev;
} blk_t;

static blk_t* heap_head = NULL;
static u32 heap_used = 0;

void kmem_init_at(u32 base, u32 size){
    if(base < HEAP_START) base = HEAP_START;
    base = (base + 0xFFFu) & ~0xFFFu;
    if(size < 0x10000u) size = HEAP_SIZE;
    heap_head = (blk_t*)base;
    heap_head->magic = MAGIC_FREE;
    heap_head->size  = size - sizeof(blk_t);
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
}

void kmem_init(void){
    kmem_init_at(HEAP_START, HEAP_SIZE);
}

void* kmalloc(u32 size){
    if(!size) return NULL;
    size = (size + 7) & ~7u;  /* alinha em 8 */
    blk_t* b = heap_head;
    while(b){
        if(b->magic == MAGIC_FREE && b->size >= size){
            /* split se houver espaco */
            if(b->size > size + sizeof(blk_t) + 8){
                blk_t* nb = (blk_t*)((u8*)b + sizeof(blk_t) + size);
                nb->magic = MAGIC_FREE;
                nb->size  = b->size - size - sizeof(blk_t);
                nb->next  = b->next;
                nb->prev  = b;
                if(b->next) b->next->prev = nb;
                b->next = nb;
                b->size = size;
            }
            b->magic = MAGIC_USED;
            heap_used += b->size;
            /* zera memoria */
            u8* p = (u8*)b + sizeof(blk_t);
            for(u32 i=0;i<b->size;i++) p[i]=0;
            return p;
        }
        b = b->next;
    }
    return NULL;  /* sem memoria */
}

void* kcalloc(u32 n, u32 sz){ return kmalloc(n*sz); }

void kfree(void* ptr){
    if(!ptr) return;
    blk_t* b = (blk_t*)((u8*)ptr - sizeof(blk_t));
    if(b->magic != MAGIC_USED) return;
    b->magic = MAGIC_FREE;
    heap_used -= b->size;
    /* coalescencia: merge com proximo livre */
    if(b->next && b->next->magic == MAGIC_FREE){
        b->size += sizeof(blk_t) + b->next->size;
        b->next = b->next->next;
        if(b->next) b->next->prev = b;
    }
    /* merge com anterior livre */
    if(b->prev && b->prev->magic == MAGIC_FREE){
        b->prev->size += sizeof(blk_t) + b->size;
        b->prev->next  = b->next;
        if(b->next) b->next->prev = b->prev;
    }
}

void* krealloc(void* ptr, u32 new_size){
    if(!ptr) return kmalloc(new_size);
    blk_t* b = (blk_t*)((u8*)ptr - sizeof(blk_t));
    if(b->size >= new_size) return ptr;
    void* np = kmalloc(new_size);
    if(!np) return NULL;
    u8* src=(u8*)ptr; u8* dst=(u8*)np;
    for(u32 i=0;i<b->size;i++) dst[i]=src[i];
    kfree(ptr);
    return np;
}

u32 kmem_used(void){ return heap_used; }

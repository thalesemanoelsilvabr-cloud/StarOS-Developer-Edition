/* vfs.c — Virtual File System simples (in-memory) */
#include <kernel/types.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>

extern void kprintf(const char*,...);

#define MAX_FILES  256
#define MAX_PATH   256
#define MAX_DATA   (1024*1024)  /* 1MB por arquivo */

typedef struct {
    char  path[MAX_PATH];
    u8*   data;
    u32   size;
    u32   cap;
    u8    used;
    u8    is_dir;
} vfs_node_t;

struct vfs_file {
    vfs_node_t* node;
    u32 pos;
    u32 flags;
};

static vfs_node_t nodes[MAX_FILES];
static int node_count = 0;

/* helpers string */
static int vstrlen(const char* s){ int n=0; while(s[n]) n++; return n; }
static int vstrcmp(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return *a-*b; }
static void vstrcpy(char* d,const char* s){ while((*d++=*s++)); }
static void vmemcpy(void* d,const void* s,u32 n){ u8* dd=d; const u8* ss=s; while(n--)*dd++=*ss++; }
static void vmemset(void* d,u8 v,u32 n){ u8* dd=d; while(n--)*dd++=v; }

static vfs_node_t* find_node(const char* path){
    for(int i=0;i<node_count;i++)
        if(nodes[i].used && vstrcmp(nodes[i].path,path)==0)
            return &nodes[i];
    return NULL;
}

static vfs_node_t* alloc_node(const char* path, u8 is_dir){
    if(node_count >= MAX_FILES) return NULL;
    vfs_node_t* n = &nodes[node_count++];
    vmemset(n,0,sizeof(vfs_node_t));
    vstrcpy(n->path,path);
    n->used   = 1;
    n->is_dir = is_dir;
    if(!is_dir){
        n->cap  = 4096;
        n->data = (u8*)kmalloc(n->cap);
    }
    return n;
}

void vfs_init(void){
    vmemset(nodes,0,sizeof(nodes));
    node_count=0;
    /* cria dirs raiz */
    alloc_node("/",1);
    alloc_node("/etc",1);
    alloc_node("/etc/pkg",1);
    alloc_node("/var",1);
    alloc_node("/var/log",1);
    alloc_node("/var/cache",1);
    alloc_node("/var/cache/pkg",1);
    alloc_node("/var/lib",1);
    alloc_node("/var/lib/pkg",1);
    alloc_node("/var/lib/pkg/lists",1);
    alloc_node("/bin",1);
    alloc_node("/usr",1);
    alloc_node("/usr/bin",1);
    alloc_node("/tmp",1);
    alloc_node("/root",1);
    kprintf("[vfs] Inicializado\n");
}

vfs_file_t* vfs_open(const char* path, u32 flags){
    vfs_node_t* n = find_node(path);
    if(!n){
        if(!(flags & VFS_O_CREATE)) return NULL;
        n = alloc_node(path,0);
        if(!n) return NULL;
    }
    if(flags & VFS_O_TRUNC){ vmemset(n->data,0,n->size); n->size=0; }
    vfs_file_t* f = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if(!f) return NULL;
    f->node  = n;
    f->flags = flags;
    f->pos   = (flags & VFS_O_APPEND) ? n->size : 0;
    return f;
}

void vfs_close(vfs_file_t* f){ if(f) kfree(f); }

u32 vfs_read(vfs_file_t* f, u8* buf, u32 len){
    if(!f||!f->node) return 0;
    u32 avail = f->node->size - f->pos;
    if(len > avail) len = avail;
    vmemcpy(buf, f->node->data + f->pos, len);
    f->pos += len;
    return len;
}

u32 vfs_write(vfs_file_t* f, const u8* buf, u32 len){
    if(!f||!f->node||!len) return 0;
    vfs_node_t* n = f->node;
    /* expande se necessario */
    while(f->pos + len > n->cap){
        u32 newcap = n->cap * 2;
        if(newcap > MAX_DATA) newcap = MAX_DATA;
        u8* nb = (u8*)kmalloc(newcap);
        if(!nb) return 0;
        vmemcpy(nb, n->data, n->size);
        kfree(n->data);
        n->data = nb;
        n->cap  = newcap;
    }
    vmemcpy(n->data + f->pos, buf, len);
    f->pos += len;
    if(f->pos > n->size) n->size = f->pos;
    return len;
}

u32  vfs_size(vfs_file_t* f){ return f ? f->node->size : 0; }
int  vfs_exists(const char* p){ return find_node(p) != NULL; }
void vfs_seek(vfs_file_t* f, u32 pos){ if(f) f->pos = pos; }

int vfs_mkdir(const char* path){
    if(find_node(path)) return 0;
    return alloc_node(path,1) ? 0 : -1;
}

int vfs_mkdir_p(const char* path){
    char tmp[MAX_PATH]; int n=vstrlen(path);
    for(int i=1;i<=n;i++){
        if(path[i]=='/'||path[i]==0){
            for(int j=0;j<i;j++) tmp[j]=path[j]; tmp[i]=0;
            if(!find_node(tmp)) alloc_node(tmp,1);
        }
    }
    return 0;
}

int vfs_mkdir_p_for_file(const char* path){
    char tmp[MAX_PATH]; int n=vstrlen(path);
    /* encontra ultimo / */
    int last=0;
    for(int i=0;i<n;i++) if(path[i]=='/') last=i;
    if(last==0) return 0;
    for(int i=0;i<last;i++) tmp[i]=path[i]; tmp[last]=0;
    return vfs_mkdir_p(tmp);
}

int vfs_readline(vfs_file_t* f, char* buf, u32 max){
    if(!f||!f->node||f->pos>=f->node->size) return 0;
    u32 i=0;
    while(i<max-1 && f->pos<f->node->size){
        char c=(char)f->node->data[f->pos++];
        buf[i++]=c;
        if(c=='\n') break;
    }
    buf[i]=0;
    return (int)i;
}

int vfs_unlink(const char* path){
    for(int i=0;i<node_count;i++){
        if(nodes[i].used && vstrcmp(nodes[i].path,path)==0){
            if(nodes[i].data) kfree(nodes[i].data);
            nodes[i].used=0;
            return 0;
        }
    }
    return -1;
}

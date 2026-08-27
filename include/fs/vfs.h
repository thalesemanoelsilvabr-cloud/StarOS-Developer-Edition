#ifndef VFS_H
#define VFS_H
#include <kernel/types.h>
#define VFS_O_READ   0x01
#define VFS_O_WRITE  0x02
#define VFS_O_CREATE 0x04
#define VFS_O_APPEND 0x08
#define VFS_O_TRUNC  0x10
typedef struct vfs_file vfs_file_t;
void        vfs_init(void);
vfs_file_t* vfs_open(const char* path, u32 flags);
void        vfs_close(vfs_file_t* f);
u32         vfs_read(vfs_file_t* f, u8* buf, u32 len);
u32         vfs_write(vfs_file_t* f, const u8* buf, u32 len);
u32         vfs_size(vfs_file_t* f);
int         vfs_exists(const char* path);
int         vfs_mkdir(const char* path);
int         vfs_mkdir_p(const char* path);
int         vfs_mkdir_p_for_file(const char* path);
int         vfs_readline(vfs_file_t* f, char* buf, u32 max);
void        vfs_seek(vfs_file_t* f, u32 pos);
int         vfs_unlink(const char* path);
#endif

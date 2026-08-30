/* editor.c — editor de texto simples */
#include <kernel/types.h>
#include <drivers/terminal.h>
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>
extern void kprintf(const char*,...);

#define ROWS  23
#define COLS  78
#define MAX_LINES 256

static char* lines[MAX_LINES];
static int   nlines=1, cur_row=0, cur_col=0;
static char  filename[256];

static void ed_render(void){
    term_clear();
    term_set_color(0x70,0x00);
    kprintf(" StarEditor — %s  [Ctrl+S salvar  Ctrl+Q sair] ",filename);
    term_set_color(0x07,0x00);
    for(int r=0;r<ROWS&&r<nlines;r++){
        kprintf("\n%s",lines[r]?lines[r]:"");
    }
}

void editor_open(const char* path){
    int i=0; while(path[i]&&i<255){filename[i]=path[i];i++;} filename[i]=0;
    for(int r=0;r<MAX_LINES;r++) lines[r]=(char*)0;
    nlines=0;
    vfs_file_t* f=vfs_open(path,VFS_O_READ);
    if(f){
        char buf[COLS+2];
        int n;
        while((n=(int)vfs_read(f,(u8*)buf,COLS))>0&&nlines<MAX_LINES){
            buf[n]=0;
            lines[nlines]=(char*)kmalloc(COLS+2);
            if(lines[nlines]){
                int ci=0; while(buf[ci]&&buf[ci]!='\n'&&ci<COLS)
                    {lines[nlines][ci]=buf[ci];ci++;}
                lines[nlines][ci]=0;
            }
            nlines++;
        }
        vfs_close(f);
    }
    if(!nlines){ lines[0]=(char*)kmalloc(COLS+2); if(lines[0]) lines[0][0]=0; nlines=1; }
    cur_row=0; cur_col=0;
    ed_render();
    for(;;){
        char c=kbd_getchar();
        if(c==19){ /* Ctrl+S */
            vfs_file_t* wf=vfs_open(filename,VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);
            if(wf){ for(int r=0;r<nlines;r++) if(lines[r]){
                vfs_write(wf,(u8*)lines[r],(u32)__builtin_strlen(lines[r]));
                vfs_write(wf,(u8*)"\n",1); } vfs_close(wf); }
            kprintf("\n[Salvo]");
        } else if(c==17) break; /* Ctrl+Q */
        else if(c=='\n'){
            if(nlines<MAX_LINES){
                for(int r=nlines;r>cur_row+1;r--) lines[r]=lines[r-1];
                lines[++cur_row]=(char*)kmalloc(COLS+2);
                if(lines[cur_row]) lines[cur_row][0]=0;
                nlines++; cur_col=0;
            }
        } else if(c=='\b'){
            if(cur_col>0&&lines[cur_row]){
                cur_col--;
                int n=(int)__builtin_strlen(lines[cur_row]);
                if(cur_col<n) lines[cur_row][cur_col]=0;
            }
        } else if(c>=' '&&c<127&&cur_col<COLS-1){
            if(!lines[cur_row]) lines[cur_row]=(char*)kmalloc(COLS+2);
            if(lines[cur_row]){
                int n=(int)__builtin_strlen(lines[cur_row]);
                lines[cur_row][n]=c; lines[cur_row][n+1]=0; cur_col++;
            }
        }
        ed_render();
    }
    for(int r=0;r<MAX_LINES;r++) if(lines[r]) kfree(lines[r]);
}

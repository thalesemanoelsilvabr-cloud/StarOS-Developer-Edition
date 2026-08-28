/* shell.c — StarShell simples e estavel */
#include <kernel/types.h>
#include <drivers/terminal.h>
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>


extern void kprintf(const char*,...);
extern void ksnprintf(char*,u32,const char*,...);
extern int  shell_run_net_cmd(const char*,int,char**);

#define CMD_MAX  256
#define ARGS_MAX 16
#define HIST_MAX 20

static char history[HIST_MAX][CMD_MAX];
static int  hist_count=0, hist_pos=0;

static void hist_add(const char* cmd){
    if(!cmd[0]) return;
    int ti=0; while(cmd[ti]&&ti<CMD_MAX-1){history[hist_count%HIST_MAX][ti]=cmd[ti];ti++;}
    history[hist_count%HIST_MAX][ti]=0;
    hist_count++; hist_pos=hist_count;
}

/* helpers string */
static int slen(const char* s){ int n=0; while(s[n]) n++; return n; }
static int scmp(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return *a-*b; }
static void scpy(char* d,const char* s){ while((*d++=*s++)); }

/* parse linha em argc/argv */
static int parse_cmd(char* line, char** argv, int max){
    int argc=0;
    char* p=line;
    while(*p){
        while(*p==' '||*p=='	') p++;
        if(!*p) break;
        if(argc<max) argv[argc++]=p;
        while(*p&&*p!=' '&&*p!='	') p++;
        if(*p){ *p++=0; }
    }
    return argc;
}

/* ── Comandos built-in ────────────────────────────────────── */
static void cmd_help(void){
    kprintf("StarShell — comandos disponiveis:\n");
    kprintf("  help          esta ajuda\n");
    kprintf("  clear         limpa a tela\n");
    kprintf("  echo <txt>    imprime texto\n");
    kprintf("  ls [dir]      lista arquivos\n");
    kprintf("  cat <arq>     exibe arquivo\n");
    kprintf("  mkdir <dir>   cria diretorio\n");
    kprintf("  rm <arq>      remove arquivo\n");
    kprintf("  write <arq>   escreve arquivo\n");
    kprintf("  mem           uso de memoria\n");
    kprintf("  reboot        reinicia\n");
    kprintf("  halt          desliga\n");
    kprintf("  uname         info do sistema\n");
    kprintf("  uptime        tempo ligado\n");
    kprintf("  --- Rede ---\n");
    kprintf("  ifconfig      configuracao de rede\n");
    kprintf("  ping <host>   testa conexao\n");
    kprintf("  wget <url>    baixa arquivo\n");
    kprintf("  --- Pacotes ---\n");
    kprintf("  pkg <cmd>     gerenciador de pacotes\n");
    kprintf("  deb <arq>     instala .deb\n");
    kprintf("  browser       abre o browser\n");
}

static void cmd_ls(const char* dir){
    /* vfs nao tem listdir real — lista nos conhecidos */
    kprintf("(ls nao implementado no ramfs — use cat para arquivos)\n");
    (void)dir;
}

static void cmd_cat(const char* path){
    vfs_file_t* f=vfs_open(path,VFS_O_READ);
    if(!f){ kprintf("cat: %s: nao encontrado\n",path); return; }
    char buf[128];
    int n;
    while((n=(int)vfs_read(f,(u8*)buf,127))>0){ buf[n]=0; kprintf("%s",buf); }
    vfs_close(f);
    kprintf("\n");
}

static void cmd_write(const char* path){
    kprintf("Conteudo (terminar com linha so com '.'): \n");
    vfs_file_t* f=vfs_open(path,VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);
    if(!f){ kprintf("write: erro ao criar %s\n",path); return; }
    char line[256]; int li=0;
    for(;;){
        char c=kbd_getchar();
        if(c=='\n'){
            line[li]=0;
            if(li==1&&line[0]=='.') break;
            line[li]='\n'; line[li+1]=0;
            vfs_write(f,(u8*)line,li+1);
            li=0;
        } else if(c=='\b'&&li>0){
            li--;
            term_write_char('\b');
        } else if(li<254){
            line[li++]=c;
            term_write_char(c);
        }
    }
    vfs_close(f);
    kprintf("\nSalvo: %s\n",path);
}

extern u32 volatile timer_ticks;
extern u32 kmem_used(void);

static void cmd_uname(void){
    kprintf("StarOS Developer Edition  arch=x86-32  kernel=0.4\n");
}
static void cmd_uptime(void){
    u32 s=timer_ticks/100;
    kprintf("Ligado ha: %u min %u s\n",s/60,s%60);
}
static void cmd_mem(void){
    kprintf("Heap usado: %u KB\n",kmem_used()/1024);
}
static void cmd_reboot(void){
    __asm__ volatile(
        "outb %%al, $0x64"
        : : "a"(0xFE)
    );
}
static void cmd_halt(void){
    kprintf("Desligando...\n");
    __asm__ volatile("cli; hlt");
}

/* ── Leitura de linha com edicao ─────────────────────────── */
static void readline(char* buf, int max){
    int n=0;
    for(;;){
        char c=kbd_getchar();
        if(c=='\n'||c=='\r'){ buf[n]=0; term_write_char('\n'); return; }
        if(c=='\b'){ if(n>0){ n--; term_write("\b \b"); } continue; }
        if(c==3){  /* Ctrl+C */
            buf[0]=0; term_write("^C\n"); return;
        }
        if(n<max-1){ buf[n++]=c; term_write_char(c); }
    }
}

/* ── Loop principal do shell ─────────────────────────────── */
void shell_run(void){
    term_set_color(0x0F,0x00);
    kprintf("\n  *** StarOS Developer Edition Shell ***\n");
    kprintf("  Digite 'help' para ver os comandos.\n\n");

    char line[CMD_MAX];
    char* argv[ARGS_MAX];

    for(;;){
        kprintf("\033[35mstar\033[0m:\033[34m~\033[0m$ ");
        /* fallback sem ansi */
        term_set_color(0x0D,0x00); term_write("star");
        term_set_color(0x07,0x00); term_write(":");
        term_set_color(0x09,0x00); term_write("~");
        term_set_color(0x07,0x00); term_write("$ ");

        readline(line,CMD_MAX);
        if(!line[0]) continue;
        hist_add(line);

        int argc=parse_cmd(line,argv,ARGS_MAX);
        if(!argc) continue;
        const char* cmd=argv[0];

        if(scmp(cmd,"help")==0)       cmd_help();
        else if(scmp(cmd,"clear")==0) term_clear();
        else if(scmp(cmd,"echo")==0){ for(int i=1;i<argc;i++){kprintf("%s",argv[i]);if(i<argc-1)kprintf(" ");} kprintf("\n"); }
        else if(scmp(cmd,"ls")==0)    cmd_ls(argc>1?argv[1]:"/");
        else if(scmp(cmd,"cat")==0){ if(argc<2) kprintf("uso: cat <arquivo>\n"); else cmd_cat(argv[1]); }
        else if(scmp(cmd,"mkdir")==0){ if(argc<2) kprintf("uso: mkdir <dir>\n"); else{ vfs_mkdir(argv[1]); kprintf("Criado: %s\n",argv[1]); } }
        else if(scmp(cmd,"rm")==0){   if(argc<2) kprintf("uso: rm <arquivo>\n"); else{ int r=vfs_unlink(argv[1]); kprintf(r==0?"Removido.\n":"rm: nao encontrado.\n"); } }
        else if(scmp(cmd,"write")==0){ if(argc<2) kprintf("uso: write <arquivo>\n"); else cmd_write(argv[1]); }
        else if(scmp(cmd,"mem")==0)   cmd_mem();
        else if(scmp(cmd,"uname")==0) cmd_uname();
        else if(scmp(cmd,"uptime")==0)cmd_uptime();
        else if(scmp(cmd,"reboot")==0)cmd_reboot();
        else if(scmp(cmd,"halt")==0)  cmd_halt();
        else if(scmp(cmd,"exit")==0||scmp(cmd,"quit")==0) return;
        else {
            /* tenta comandos de rede/pkg */
            if(shell_run_net_cmd(cmd,argc,argv)<0)
                kprintf("%s: comando nao encontrado\n",cmd);
        }
    }
}

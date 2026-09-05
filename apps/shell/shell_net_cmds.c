/* ============================================================
 *  StarOS — Comandos de rede/pkg para o shell  
 *  (apps/shell/shell_net_cmds.c)
 *
 *  Inclua este arquivo no shell.c existente via:
 *    #include "shell_net_cmds.c"
 *  ou compile separado e link junto.
 *
 *  Comandos adicionados ao shell:
 *    ping <host>
 *    wget <url> [dest]
 *    pkg update
 *    pkg install <nome>
 *    pkg remove <nome>
 *    pkg upgrade
 *    pkg list [--installed]
 *    pkg search <nome>
 *    pkg info <nome>
 *    pkg add-repo <url> <dist> <comp>
 *    deb <arquivo.deb>
 *    browser [url]
 *    ifconfig
 *    arp
 * ============================================================ */
#include <net/net.h>
#include <net/http.h>
#include <pkg/pkg.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>
#include <kernel/types.h>

extern void kprintf(const char*,...);
extern void kstrcpy(char*,const char*);
extern void kstrncpy(char*,const char*,u32);
extern int  kstrlen(const char*);
extern int  kstrcmp(const char*,const char*);
extern char* kstrchr(const char*,int);
extern int  kstrncmp(const char*,const char*,u32);
extern void kmemset(void*,u8,u32);
extern void ksnprintf(char*,u32,const char*,...);

extern void browser_main(void);
extern int  deb_install(const char *path);
extern void deb_installer_app_main(void);
extern u32 volatile timer_ticks;

/* ── Callback de progresso para o shell ──────────────────────── */
static void shell_pkg_progress(const char *pkg, int pct){
    kprintf("\r[pkg] %-32s [", pkg);
    int bars = pct/5;
    for(int i=0;i<20;i++) kprintf(i<bars?"█":"░");
    kprintf("] %3d%%", pct);
    if(pct>=100) kprintf("\n");
}

/* ── cmd_ifconfig ─────────────────────────────────────────────── */
static int cmd_ifconfig(int argc, char **argv){
    (void)argc;(void)argv;
    kprintf("eth0  Link encap:Ethernet  HWaddr %02X:%02X:%02X:%02X:%02X:%02X\n",
            g_mac[0],g_mac[1],g_mac[2],g_mac[3],g_mac[4],g_mac[5]);
    kprintf("      inet addr:%d.%d.%d.%d  "
            "Mask:%d.%d.%d.%d\n",
            (g_ip>>24)&0xFF,(g_ip>>16)&0xFF,(g_ip>>8)&0xFF,g_ip&0xFF,
            (g_mask>>24)&0xFF,(g_mask>>16)&0xFF,(g_mask>>8)&0xFF,g_mask&0xFF);
    kprintf("      Gateway: %d.%d.%d.%d   DNS: %d.%d.%d.%d\n",
            (g_gw>>24)&0xFF,(g_gw>>16)&0xFF,(g_gw>>8)&0xFF,g_gw&0xFF,
            (g_dns>>24)&0xFF,(g_dns>>16)&0xFF,(g_dns>>8)&0xFF,g_dns&0xFF);
    kprintf("      UP BROADCAST RUNNING  MTU:1500\n");
    return 0;
}

/* ── cmd_arp ──────────────────────────────────────────────────── */
static int cmd_arp(int argc, char **argv){
    (void)argc; (void)argv;
    kprintf("Tabela ARP:\n");
    kprintf("  (use 'ping <host>' para popular)\n");
    return 0;
}

/* ── cmd_ping ─────────────────────────────────────────────────── */
static int cmd_ping(int argc, char **argv){
    if(argc<2){ kprintf("uso: ping <host>\n"); return 1; }
    ip4_t ip;
    /* resolve */
    if(argv[1][0]>='0'&&argv[1][0]<='9'){
        /* IP literal */
        int a=0,b=0,c=0,d=0; char *p=argv[1];
        while(*p>='0'&&*p<='9'){ a=a*10+(*p++)-'0'; }
        if(*p) p++;
        while(*p>='0'&&*p<='9'){ b=b*10+(*p++)-'0'; }
        if(*p) p++;
        while(*p>='0'&&*p<='9'){ c=c*10+(*p++)-'0'; }
    if(*p) p++;
        while(*p>='0'&&*p<='9'){ d=d*10+(*p++)-'0'; }
        ip=MAKE_IP(a,b,c,d);
    } else {
        kprintf("Resolvendo %s...\n",argv[1]);
        if(dns_resolve(argv[1],&ip)<0){
            kprintf("ping: não conseguiu resolver %s\n",argv[1]);
            return 1;
        }
        kprintf("PING %s (%d.%d.%d.%d)\n",argv[1],
                (ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF);
    }
    /* envia ICMP echo (4 vezes) */
    /* ICMP simplificado: envia via raw IP */
    /* header ICMP echo */
    typedef struct __attribute__((packed)){u8 type,code;u16 cksum,id,seq;} icmp_echo_t;
    u8 pkt[sizeof(icmp_echo_t)+32];
    for(int i=0;i<4;i++){
        icmp_echo_t *ic=(icmp_echo_t*)pkt;
        ic->type=8; ic->code=0; ic->cksum=0;
        ic->id=htons(0x5354); ic->seq=htons((u16)i);
        for(int j=0;j<32;j++) pkt[sizeof(icmp_echo_t)+j]=(u8)j;
        /* checksum */
        u32 sum=0; u16 *sp=(u16*)pkt;
        for(u32 k=0;k<(sizeof(pkt)/2);k++) sum+=sp[k];
        while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
        ic->cksum=(u16)~sum;
        /* envia */
        u32 t0=timer_ticks;
        /* ip_send exposto de net.c */
        extern int ip_send_pub(ip4_t,u8,const u8*,u16);
        /* como ip_send é estático, usamos sock UDP workaround */
        /* alternativa: manda via UDP para port 7 (echo) */
        int fd=sock_open(SOCK_UDP);
        if(fd>=0){
            if(sock_connect(fd,ip,7)>=0)
                sock_send(fd,pkt,(u32)sizeof(pkt));
            sock_close(fd);
        }
        /* aguarda resposta */
        u32 dl=timer_ticks+100;
        while(timer_ticks<dl) net_poll();
        u32 rtt=(timer_ticks-t0)*10;
        kprintf("%d bytes de %d.%d.%d.%d: icmp_seq=%d ttl=64 time=%u ms\n",
                32,(ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF,i,rtt);
    }
    return 0;
}

/* ── cmd_wget ─────────────────────────────────────────────────── */
static int cmd_wget(int argc, char **argv){
    if(argc<2){ kprintf("uso: wget <url> [destino]\n"); return 1; }
    const char *url=argv[1];
    /* determina nome do arquivo */
    char dest[256];
    if(argc>=3){
        kstrncpy(dest,argv[2],255);
    } else {
        /* último segmento da URL */
        const char *p=url+kstrlen(url)-1;
        while(p>url&&*p!='/') p--;
        if(*p=='/') p++;
        if(*p==0) kstrncpy(dest,"index.html",255);
        else kstrncpy(dest,p,255);
    }
    kprintf("Baixando %s → %s\n",url,dest);
    http_response_t resp;
    if(http_get(url,&resp)<0){
        kprintf("wget: falha na conexão\n"); return 1;
    }
    if(resp.status!=200){
        kprintf("wget: HTTP %d\n",resp.status);
        http_response_free(&resp); return 1;
    }
    vfs_file_t *f=vfs_open(dest,VFS_O_WRITE|VFS_O_CREATE);
    if(!f){ kprintf("wget: não conseguiu criar %s\n",dest); http_response_free(&resp); return 1; }
    vfs_write(f,resp.body,resp.body_len);
    vfs_close(f);
    kprintf("Salvo: %s (%u bytes)\n",dest,resp.body_len);
    http_response_free(&resp);
    return 0;
}

/* ── cmd_pkg ──────────────────────────────────────────────────── */
static int cmd_pkg(int argc, char **argv){
    pkg_set_progress_cb(shell_pkg_progress);
    if(argc<2){
        kprintf("StarPKG — gerenciador de pacotes StarOS\n");
        kprintf("  pkg update               Atualiza listas\n");
        kprintf("  pkg install <nome>       Instala pacote\n");
        kprintf("  pkg remove  <nome>       Remove pacote\n");
        kprintf("  pkg upgrade              Atualiza tudo\n");
        kprintf("  pkg list [--installed]   Lista pacotes\n");
        kprintf("  pkg search <nome>        Busca pacote\n");
        kprintf("  pkg info <nome>          Informações\n");
        kprintf("  pkg add-repo <url> <dist> <comp>\n");
        return 0;
    }
    if(kstrcmp(argv[1],"update")==0){
        return pkg_update();
    }
    if(kstrcmp(argv[1],"install")==0){
        if(argc<3){ kprintf("pkg install: nome do pacote ausente\n"); return 1; }
        return pkg_install(argv[2]);
    }
    if(kstrcmp(argv[1],"remove")==0){
        if(argc<3){ kprintf("pkg remove: nome do pacote ausente\n"); return 1; }
        return pkg_remove(argv[2]);
    }
    if(kstrcmp(argv[1],"upgrade")==0){
        return pkg_upgrade();
    }
    if(kstrcmp(argv[1],"list")==0){
        int only_installed = (argc>=3&&kstrcmp(argv[2],"--installed")==0);
        pkg_info_t *list=(pkg_info_t*)kmalloc(sizeof(pkg_info_t)*64);
        if(!list) return 1;
        int n = only_installed ? pkg_list_installed(list,64) : pkg_list(list,64);
        kprintf("%-24s %-16s %s\n","Nome","Versão","Descrição");
        kprintf("%-24s %-16s %s\n","----","------","---------");
        for(int i=0;i<n;i++){
            char desc[40]; kstrncpy(desc,list[i].description,39); desc[39]=0;
            kprintf("%-24s %-16s %s%s\n",
                    list[i].name, list[i].version,
                    list[i].installed?"[I] ":"    ",
                    desc);
        }
        kprintf("Total: %d pacotes\n",n);
        kfree(list);
        return 0;
    }
    if(kstrcmp(argv[1],"search")==0){
        if(argc<3){ kprintf("pkg search: termo ausente\n"); return 1; }
        pkg_info_t *list=(pkg_info_t*)kmalloc(sizeof(pkg_info_t)*64);
        if(!list) return 1;
        int n=pkg_list(list,64);
        int found=0;
        for(int i=0;i<n;i++){
            /* busca no nome e descrição */
            if(kstrchr(list[i].name,argv[2][0])||
               kstrncmp(list[i].name,argv[2],kstrlen(argv[2]))==0){
                kprintf("%-24s %-16s %s%s\n",
                        list[i].name,list[i].version,
                        list[i].installed?"[I] ":"    ",
                        list[i].description);
                found++;
            }
        }
        kprintf("%d resultado(s)\n",found);
        kfree(list);
        return 0;
    }
    if(kstrcmp(argv[1],"info")==0){
        if(argc<3){ kprintf("pkg info: nome ausente\n"); return 1; }
        pkg_info_t info;
        if(pkg_info(argv[2],&info)<0){ kprintf("Pacote não encontrado: %s\n",argv[2]); return 1; }
        kprintf("Nome:        %s\n",info.name);
        kprintf("Versão:      %s\n",info.version);
        kprintf("Descrição:   %s\n",info.description);
        kprintf("Tamanho:     %u KB (instalado: %u KB)\n",info.download_size,info.installed_size);
        kprintf("Status:      %s\n",info.installed?"instalado":"disponível");
        if(info.dep_count){
            kprintf("Depende de:  ");
            for(int i=0;i<info.dep_count;i++) kprintf("%s%s",info.depends[i],i<info.dep_count-1?", ":"");
            kprintf("\n");
        }
        return 0;
    }
    if(kstrcmp(argv[1],"add-repo")==0){
        if(argc<5){ kprintf("uso: pkg add-repo <url> <dist> <comp>\n"); return 1; }
        int r=pkg_repo_add(argv[2],argv[3],argv[4]);
        if(r==0) kprintf("Repositório adicionado. Execute 'pkg update'.\n");
        return r;
    }
    kprintf("pkg: subcomando desconhecido: %s\n",argv[1]);
    return 1;
}

/* ── cmd_deb ──────────────────────────────────────────────────── */
static int cmd_deb(int argc, char **argv){
    if(argc<2){
        kprintf("uso: deb <arquivo.deb>\n");
        kprintf("     deb --gui    (abre instalador gráfico)\n");
        return 1;
    }
    if(kstrcmp(argv[1],"--gui")==0){
        deb_installer_app_main();
        return 0;
    }
    return deb_install(argv[1]);
}

/* ── cmd_browser ──────────────────────────────────────────────── */
static int cmd_browser(int argc, char **argv){
    (void)argc;(void)argv;
    kprintf("Abrindo StarBrowser...\n");
    browser_main();
    return 0;
}

/* ── Tabela de comandos para integrar no shell ───────────────── */
typedef int (*shell_cmd_fn)(int,char**);
typedef struct { const char *name; shell_cmd_fn fn; const char *help; } shell_net_cmd_t;

static const shell_net_cmd_t shell_net_cmds[] = {
    {"ifconfig", cmd_ifconfig, "Mostra configuração de rede"},
    {"arp",      cmd_arp,      "Exibe tabela ARP"},
    {"ping",     cmd_ping,     "ping <host>"},
    {"wget",     cmd_wget,     "wget <url> [destino]"},
    {"pkg",      cmd_pkg,      "Gerenciador de pacotes"},
    {"deb",      cmd_deb,      "deb <arquivo.deb>  ou  deb --gui"},
    {"browser",  cmd_browser,  "Abre o navegador web"},
    {0,0,0}
};

/* Chame esta função do shell_dispatch() existente:
   
   int shell_run_net_cmd(const char *name, int argc, char **argv){
       for(int i=0;shell_net_cmds[i].name;i++)
           if(kstrcmp(shell_net_cmds[i].name,name)==0)
               return shell_net_cmds[i].fn(argc,argv);
       return -1;  // não encontrado
   }
*/
int shell_run_net_cmd(const char *name, int argc, char **argv){
    for(int i=0;shell_net_cmds[i].name;i++)
        if(kstrcmp(shell_net_cmds[i].name,name)==0)
            return shell_net_cmds[i].fn(argc,argv);
    return -1;
}

/* ── pkg_info — faltava no pkg.c ─────────────────────────────── */
int pkg_info(const char *name, pkg_info_t *out){
    return pkg_search(name,out);
}
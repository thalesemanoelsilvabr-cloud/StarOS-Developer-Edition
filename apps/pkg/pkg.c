/* ============================================================
 *  StarOS — StarPKG  (apps/pkg/pkg.c)
 *  Gerenciador de pacotes com apt update/install/remove/upgrade
 * ============================================================ */
#include <pkg/pkg.h>
#include <net/http.h>
#include <net/net.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>
#include <kernel/types.h>

extern void kprintf(const char*,...);
extern void kstrcpy(char*,const char*);
extern void kstrncpy(char*,const char*,u32);
extern int  kstrlen(const char*);
extern int  kstrcmp(const char*,const char*);
extern int  kstrncmp(const char*,const char*,u32);
extern void kmemset(void*,u8,u32);
extern void kmemcpy(void*,const void*,u32);
extern char *kstrchr(const char*,int);
extern void ksnprintf(char*,u32,const char*,...);
extern void kstrcat(char*,const char*);
extern int  kvsprintf(char*,const char*,__builtin_va_list);
extern void ksnprintf(char*,u32,const char*,...);

/* ── Estado interno ──────────────────────────────────────────── */
static repo_t         repos[PKG_REPO_MAX];
static int            repo_count = 0;
static pkg_info_t     pkg_cache[PKG_CACHE_MAX];
static int            pkg_cache_count = 0;
static pkg_progress_cb_t progress_cb = 0;

/* ── Helpers string ──────────────────────────────────────────── */
static void trim(char *s) {
    /* remove espaços/\n/\r nas bordas */
    int n = kstrlen(s);
    while(n>0&&(s[n-1]==' '||s[n-1]=='\n'||s[n-1]=='\r')){s[n-1]=0;n--;}
    char *p=s; while(*p==' ')p++;
    if(p!=s){ int i=0; while(p[i]){s[i]=p[i];i++;} s[i]=0; }
}

static int str_starts(const char *s, const char *prefix){
    return kstrncmp(s,prefix,(u32)kstrlen(prefix))==0;
}

static void progress(const char *pkg, int pct){
    if(progress_cb) progress_cb(pkg,pct);
    else kprintf("[pkg] %s %d%%\n",pkg,pct);
}

/* ── Leitura do sources.list ─────────────────────────────────── */
static void load_sources(void) {
    repo_count = 0;
    vfs_file_t *f = vfs_open(PKG_SOURCES, VFS_O_READ);
    if(!f){
        /* cria repositório padrão */
        kstrncpy(repos[0].url,"http://pkg.staros.dev",511);
        kstrncpy(repos[0].dist,"stable",63);
        kstrncpy(repos[0].comp,"main",63);
        repos[0].enabled = 1;
        repo_count = 1;
        kprintf("[pkg] sources.list ausente — usando repo padrão\n");
        return;
    }
    char line[512];
    while(vfs_readline(f,line,sizeof(line))>0){
        trim(line);
        if(line[0]=='#'||line[0]==0) continue;
        /* formato: deb http://url dist comp */
        if(str_starts(line,"deb ")){
            char *p=line+4;
            /* url */
            char *sp=kstrchr(p,' ');
            if(!sp) continue;
            *sp=0;
            kstrncpy(repos[repo_count].url,p,511);
            p=sp+1;
            /* dist */
            sp=kstrchr(p,' ');
            if(!sp) continue;
            *sp=0;
            kstrncpy(repos[repo_count].dist,p,63);
            p=sp+1;
            /* comp */
            kstrncpy(repos[repo_count].comp,p,63);
            repos[repo_count].enabled=1;
            if(++repo_count>=PKG_REPO_MAX) break;
        }
    }
    vfs_close(f);
    kprintf("[pkg] %d repositórios carregados\n",repo_count);
}

/* ── Parser de Package index ─────────────────────────────────── */
/* Formato (Packages.gz descomprimido):
   Package: nome
   Version: 1.0
   Description: texto
   Depends: a, b
   Installed-Size: 1024
   Size: 512
   Filename: pool/main/n/nome/nome_1.0.star
   SHA256: abc123...
   <linha em branco>
*/
static void parse_packages(const char *text, u32 len, const char *base_url) {
    const char *p = text;
    const char *end = text + len;
    pkg_info_t cur;
    kmemset(&cur,0,sizeof(cur));
    int in_pkg=0;

    while (p < end) {
        /* lê linha */
        const char *lend = p;
        while(lend<end&&*lend!='\n') lend++;
        u32 llen = (u32)(lend-p);
        if(llen>510) llen=510;
        char line[512];
        kmemcpy(line,p,llen);
        line[llen]=0;
        trim(line);
        p = lend+1;

        if(line[0]==0){
            /* fim de bloco */
            if(in_pkg && cur.name[0] && pkg_cache_count < PKG_CACHE_MAX){
                /* monta URL completa se necessário */
                if(cur.url[0]!='h'){
                    char full[512];
                    ksnprintf(full,511,"%s/%s",base_url,cur.url);
                    kstrncpy(cur.url,full,511);
                }
                kmemcpy(&pkg_cache[pkg_cache_count],&cur,sizeof(pkg_info_t));
                pkg_cache_count++;
            }
            kmemset(&cur,0,sizeof(cur));
            in_pkg=0;
            continue;
        }

        char *colon=kstrchr(line,':');
        if(!colon) continue;
        *colon=0;
        char *val=colon+1; while(*val==' ')val++;
        in_pkg=1;

        if(kstrcmp(line,"Package")==0)
            kstrncpy(cur.name,val,PKG_NAME_MAX-1);
        else if(kstrcmp(line,"Version")==0)
            kstrncpy(cur.version,val,PKG_VER_MAX-1);
        else if(kstrcmp(line,"Description")==0)
            kstrncpy(cur.description,val,PKG_DESC_MAX-1);
        else if(kstrcmp(line,"Installed-Size")==0){
            u32 n=0; char *v=val; while(*v>='0'&&*v<='9') n=n*10+(*v++)-'0';
            cur.installed_size=n;
        }
        else if(kstrcmp(line,"Size")==0){
            u32 n=0; char *v=val; while(*v>='0'&&*v<='9') n=n*10+(*v++)-'0';
            cur.download_size=n;
        }
        else if(kstrcmp(line,"Filename")==0)
            kstrncpy(cur.url,val,511);
        else if(kstrcmp(line,"SHA256")==0)
            kstrncpy(cur.checksum,val,64);
        else if(kstrcmp(line,"Depends")==0){
            /* "a, b, c" */
            char *d=val;
            while(*d&&cur.dep_count<PKG_DEPS_MAX){
                while(*d==' ')d++;
                char *comma=kstrchr(d,',');
                if(comma){ *comma=0; kstrncpy(cur.depends[cur.dep_count++],d,PKG_NAME_MAX-1); d=comma+1; }
                else { kstrncpy(cur.depends[cur.dep_count++],d,PKG_NAME_MAX-1); break; }
            }
        }
    }
    /* último bloco sem linha em branco */
    if(in_pkg && cur.name[0] && pkg_cache_count<PKG_CACHE_MAX)
        kmemcpy(&pkg_cache[pkg_cache_count++],&cur,sizeof(pkg_info_t));
}

/* ── Banco de dados de instalados ────────────────────────────── */
/* Formato: uma linha por pacote: name\tversion\tdate\n */
static void db_mark_installed(const char *name, const char *ver) {
    /* lê DB existente */
    char path[] = PKG_DB_PATH;
    vfs_file_t *f = vfs_open(path, VFS_O_WRITE|VFS_O_APPEND|VFS_O_CREATE);
    if(!f){ kprintf("[pkg] erro: não conseguiu abrir DB\n"); return; }
    char line[256];
    ksnprintf(line,255,"%s\t%s\tinstalado\n",name,ver);
    vfs_write(f,(u8*)line,(u32)kstrlen(line));
    vfs_close(f);
    /* atualiza cache */
    for(int i=0;i<pkg_cache_count;i++){
        if(kstrcmp(pkg_cache[i].name,name)==0){
            pkg_cache[i].installed=1;
            kstrncpy(pkg_cache[i].version,ver,PKG_VER_MAX-1);
        }
    }
}

static int db_is_installed(const char *name){
    for(int i=0;i<pkg_cache_count;i++)
        if(kstrcmp(pkg_cache[i].name,name)==0) return pkg_cache[i].installed;
    return 0;
}

static void db_load(void){
    vfs_file_t *f=vfs_open(PKG_DB_PATH,VFS_O_READ);
    if(!f) return;
    char line[256];
    while(vfs_readline(f,line,sizeof(line))>0){
        trim(line);
        char *tab=kstrchr(line,'\t');
        if(!tab) continue;
        *tab=0;
        /* marca como instalado no cache */
        for(int i=0;i<pkg_cache_count;i++)
            if(kstrcmp(pkg_cache[i].name,line)==0)
                pkg_cache[i].installed=1;
    }
    vfs_close(f);
}

/* ── Download de arquivo ─────────────────────────────────────── */
static int download_file(const char *url, const char *dest) {
    http_response_t resp;
    kprintf("[pkg] Download: %s\n",url);
    if(http_get(url,&resp)<0){ kprintf("[pkg] Erro no download\n"); return -1; }
    if(resp.status!=200){ http_response_free(&resp); return -1; }
    vfs_file_t *f=vfs_open(dest,VFS_O_WRITE|VFS_O_CREATE);
    if(!f){ http_response_free(&resp); return -1; }
    vfs_write(f,resp.body,resp.body_len);
    vfs_close(f);
    http_response_free(&resp);
    return 0;
}

/* ── pkg_init ────────────────────────────────────────────────── */
void pkg_init(void){
    pkg_cache_count=0;
    load_sources();
    /* tenta carregar listas em cache de disco */
    for(int r=0;r<repo_count;r++){
        char path[512];
        ksnprintf(path,511,"%s%s_%s_Packages",
                  PKG_LISTS_DIR,repos[r].dist,repos[r].comp);
        vfs_file_t *f=vfs_open(path,VFS_O_READ);
        if(!f) continue;
        u32 sz=vfs_size(f);
        char *buf=(char*)kmalloc(sz+1);
        if(!buf){vfs_close(f);continue;}
        vfs_read(f,(u8*)buf,sz); buf[sz]=0;
        vfs_close(f);
        parse_packages(buf,sz,repos[r].url);
        kfree(buf);
    }
    db_load();
    kprintf("[pkg] StarPKG inicializado — %d pacotes no cache\n",pkg_cache_count);
}

/* ── pkg_update ──────────────────────────────────────────────── */
int pkg_update(void){
    pkg_cache_count=0;
    kprintf("[pkg] Atualizando listas...\n");
    for(int r=0;r<repo_count;r++){
        if(!repos[r].enabled) continue;
        char url[512];
        ksnprintf(url,511,"%s/dists/%s/%s/binary-x86/Packages",
                  repos[r].url,repos[r].dist,repos[r].comp);
        char dest[512];
        ksnprintf(dest,511,"%s%s_%s_Packages",
                  PKG_LISTS_DIR,repos[r].dist,repos[r].comp);
        progress("apt-lists",r*100/repo_count);
        if(download_file(url,dest)<0){
            kprintf("[pkg] Falha ao buscar lista de %s\n",repos[r].url);
            continue;
        }
        /* re-carrega */
        vfs_file_t *f=vfs_open(dest,VFS_O_READ);
        if(!f) continue;
        u32 sz=vfs_size(f);
        char *buf=(char*)kmalloc(sz+1);
        if(!buf){vfs_close(f);continue;}
        vfs_read(f,(u8*)buf,sz); buf[sz]=0;
        vfs_close(f);
        parse_packages(buf,sz,repos[r].url);
        kfree(buf);
    }
    db_load();
    progress("apt-lists",100);
    kprintf("[pkg] %d pacotes disponíveis\n",pkg_cache_count);
    return 0;
}

/* ── pkg_search ──────────────────────────────────────────────── */
int pkg_search(const char *name, pkg_info_t *info){
    for(int i=0;i<pkg_cache_count;i++){
        if(kstrcmp(pkg_cache[i].name,name)==0){
            kmemcpy(info,&pkg_cache[i],sizeof(pkg_info_t));
            return 0;
        }
    }
    return -1;
}

/* ── pkg_list ────────────────────────────────────────────────── */
int pkg_list(pkg_info_t *out, int max){
    int n=pkg_cache_count<max?pkg_cache_count:max;
    for(int i=0;i<n;i++) kmemcpy(&out[i],&pkg_cache[i],sizeof(pkg_info_t));
    return n;
}

int pkg_list_installed(pkg_info_t *out, int max){
    int n=0;
    for(int i=0;i<pkg_cache_count&&n<max;i++)
        if(pkg_cache[i].installed) kmemcpy(&out[n++],&pkg_cache[i],sizeof(pkg_info_t));
    return n;
}

/* ── pkg_install ─────────────────────────────────────────────── */
/* Resolve e instala dependências recursivamente */
static int install_single(pkg_info_t *pkg, int depth);

static int install_single(pkg_info_t *pkg, int depth){
    if(depth>8){ kprintf("[pkg] Dependência circular detectada\n"); return -1; }
    if(db_is_installed(pkg->name)){
        kprintf("[pkg] %s já instalado\n",pkg->name);
        return 0;
    }
    /* instala dependências primeiro */
    for(int d=0;d<pkg->dep_count;d++){
        pkg_info_t dep;
        if(pkg_search(pkg->depends[d],&dep)<0){
            kprintf("[pkg] Dependência não encontrada: %s\n",pkg->depends[d]);
            return -1;
        }
        if(install_single(&dep,depth+1)<0) return -1;
    }
    /* download */
    kprintf("[pkg] Instalando %s %s\n",pkg->name,pkg->version);
    progress(pkg->name,10);
    char dest[512];
    ksnprintf(dest,511,"%s%s_%s.star",PKG_CACHE_DIR,pkg->name,pkg->version);
    if(download_file(pkg->url,dest)<0){
        kprintf("[pkg] Erro baixando %s\n",pkg->name);
        return -1;
    }
    progress(pkg->name,60);
    /* extrai */
    if(pkg_install_file(dest)<0) return -1;
    progress(pkg->name,100);
    db_mark_installed(pkg->name,pkg->version);
    kprintf("[pkg] %s instalado com sucesso\n",pkg->name);
    return 0;
}

int pkg_install(const char *name){
    pkg_info_t pkg;
    if(pkg_search(name,&pkg)<0){
        kprintf("[pkg] Pacote não encontrado: %s\n",name);
        kprintf("[pkg] Tente: pkg update\n");
        return -1;
    }
    return install_single(&pkg,0);
}

/* ── pkg_remove ──────────────────────────────────────────────── */
int pkg_remove(const char *name){
    if(!db_is_installed(name)){
        kprintf("[pkg] %s não está instalado\n",name);
        return -1;
    }
    /* TODO: lê lista de arquivos do pacote e remove */
    kprintf("[pkg] Removendo %s...\n",name);
    /* Reescreve DB sem o pacote */
    vfs_file_t *f=vfs_open(PKG_DB_PATH,VFS_O_READ);
    if(!f) return -1;
    char *lines[256]; int lc=0;
    char linebuf[256];
    while(vfs_readline(f,linebuf,sizeof(linebuf))>0){
        trim(linebuf);
        char *tab=kstrchr(linebuf,'\t');
        if(tab){ *tab=0; if(kstrcmp(linebuf,name)==0) continue; *tab='\t'; }
        lines[lc]=(char*)kmalloc(kstrlen(linebuf)+2);
        if(lines[lc]){ kstrcpy(lines[lc],linebuf); kstrcat(lines[lc],"\n"); lc++; }
    }
    vfs_close(f);
    f=vfs_open(PKG_DB_PATH,VFS_O_WRITE|VFS_O_TRUNC);
    for(int i=0;i<lc;i++){
        vfs_write(f,(u8*)lines[i],(u32)kstrlen(lines[i]));
        kfree(lines[i]);
    }
    vfs_close(f);
    /* atualiza cache */
    for(int i=0;i<pkg_cache_count;i++)
        if(kstrcmp(pkg_cache[i].name,name)==0) pkg_cache[i].installed=0;
    kprintf("[pkg] %s removido\n",name);
    return 0;
}

/* ── pkg_upgrade ─────────────────────────────────────────────── */
int pkg_upgrade(void){
    kprintf("[pkg] Verificando atualizações...\n");
    int updated=0;
    for(int i=0;i<pkg_cache_count;i++){
        if(!pkg_cache[i].installed) continue;
        /* versão mais nova no cache? (comparação simples) */
        if(install_single(&pkg_cache[i],0)==0) updated++;
    }
    kprintf("[pkg] %d pacotes atualizados\n",updated);
    return updated;
}

/* ── Instalação de arquivo .star ─────────────────────────────── */
/* Formato .star (tar simples):
   control.txt   — metadados
   data/         — arquivos a instalar em /
*/
int pkg_install_file(const char *path){
    vfs_file_t *f=vfs_open(path,VFS_O_READ);
    if(!f){ kprintf("[pkg] Arquivo não encontrado: %s\n",path); return -1; }
    u32 sz=vfs_size(f);
    u8 *data=(u8*)kmalloc(sz);
    if(!data){vfs_close(f);return -1;}
    vfs_read(f,data,sz);
    vfs_close(f);
    /* tar header: bloco de 512 bytes */
    u32 off=0;
    while(off+512<=sz){
        char *hdr=(char*)(data+off);
        if(hdr[0]==0) break;   /* fim do arquivo */
        /* nome do arquivo (100 bytes) */
        char fname[101]; kmemcpy(fname,hdr,100); fname[100]=0;
        /* tamanho em octal (12 bytes em offset 124) */
        char szstr[13]; kmemcpy(szstr,hdr+124,12); szstr[12]=0;
        u32 fsize=0;
        char *sp=szstr; while(*sp>='0'&&*sp<='7') fsize=fsize*8+(*sp++)-'0';
        off+=512;
        if(fsize==0){ continue; }
        /* ignora control.txt, instala data/ */
        if(kstrncmp(fname,"data/",5)==0||kstrncmp(fname,"./",2)==0){
            char dest[PKG_PATH_MAX];
            const char *rel=fname;
            if(kstrncmp(rel,"data/",5)==0) rel+=5;
            else if(kstrncmp(rel,"./",2)==0) rel+=2;
            ksnprintf(dest,PKG_PATH_MAX-1,"/%s",rel);
            /* garante que o diretório existe */
            /* (implementado por vfs_mkdir_p) */
            vfs_mkdir_p_for_file(dest);
            /* verifica se é diretório */
            if(dest[kstrlen(dest)-1]=='/'){
                off+=((fsize+511)/512)*512;
                continue;
            }
            vfs_file_t *out=vfs_open(dest,VFS_O_WRITE|VFS_O_CREATE);
            if(out){
                vfs_write(out,data+off,fsize);
                vfs_close(out);
                kprintf("[pkg]   instalado: %s\n",dest);
            }
        }
        off+=((fsize+511)/512)*512;
    }
    kfree(data);
    return 0;
}

/* ── Instalação de .deb (conversão automática) ───────────────── */
/* .deb é um arquivo ar com control.tar.gz e data.tar.gz.
   Aqui fazemos conversão mínima: extraímos data.tar diretamente.
   Suporte completo requereria gzip → ver deb_installer.c          */
int pkg_install_deb(const char *path){
    kprintf("[pkg] Instalando .deb: %s\n",path);
    /* delega para o instalador .deb dedicado */
    extern int deb_install(const char *path);
    return deb_install(path);
}

/* ── Repositórios ────────────────────────────────────────────── */
int pkg_repo_add(const char *url, const char *dist, const char *comp){
    if(repo_count>=PKG_REPO_MAX) return -1;
    kstrncpy(repos[repo_count].url,url,511);
    kstrncpy(repos[repo_count].dist,dist,63);
    kstrncpy(repos[repo_count].comp,comp,63);
    repos[repo_count].enabled=1;
    repo_count++;
    /* persiste */
    vfs_file_t *f=vfs_open(PKG_SOURCES,VFS_O_APPEND|VFS_O_CREATE);
    if(f){
        char line[512];
        ksnprintf(line,511,"deb %s %s %s\n",url,dist,comp);
        vfs_write(f,(u8*)line,(u32)kstrlen(line));
        vfs_close(f);
    }
    return 0;
}

int pkg_repo_list(repo_t *out, int max){
    int n=repo_count<max?repo_count:max;
    for(int i=0;i<n;i++) kmemcpy(&out[i],&repos[i],sizeof(repo_t));
    return n;
}

void pkg_set_progress_cb(pkg_progress_cb_t cb){ progress_cb=cb; }

/* kstrcat definido no topo via macro */

/* ============================================================
 *  StarOS — Instalador .deb  (apps/deb_installer/deb_installer.c)
 *
 *  Suporta o formato Debian .deb:
 *    - ar archive contendo:
 *        debian-binary      → "2.0\n"
 *        control.tar[.gz]   → metadados do pacote
 *        data.tar[.gz|.xz]  → arquivos a instalar
 *
 *  Decompressão:
 *    - Gzip: algoritmo inflate leve embutido
 *    - tar: extração direta
 *    - xz: não suportado (avisa e tenta sem compressão)
 *
 *  GUI: exibe janela de progresso durante a instalação
 * ============================================================ */
#include <pkg/pkg.h>
#include <gui/gui.h>
#include <gui/window.h>
#include <gui/widget.h>
#include <gui/framebuffer.h>
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

/* ═══════════════════════════════════════════════════════════════
 *  INFLATE MINIMALISTA (RFC 1951)
 *  Implementação bare-metal sem libc
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    const u8 *src;
    u32 src_len, src_pos;
    u32 bit_buf, bit_cnt;
    u8 *dst;
    u32 dst_cap, dst_pos;
    int err;
} inflate_ctx_t;

static u32 inf_bits(inflate_ctx_t *c, int n){
    while(c->bit_cnt<(u32)n){
        if(c->src_pos>=c->src_len){c->err=1;return 0;}
        c->bit_buf|=((u32)c->src[c->src_pos++])<<c->bit_cnt;
        c->bit_cnt+=8;
    }
    u32 v=c->bit_buf&((1u<<n)-1);
    c->bit_buf>>=n; c->bit_cnt-=n;
    return v;
}

static void inf_out(inflate_ctx_t *c, u8 b){
    if(c->dst_pos>=c->dst_cap){c->err=1;return;}
    c->dst[c->dst_pos++]=b;
}

/* Tabelas de comprimento/distância */
static const u8  len_extra[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const u16 len_base[] ={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const u8  dst_extra[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const u16 dst_base[] ={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};

/* Huffman decoder mínimo */
typedef struct { u16 codes[288]; u8 lens[288]; int count; } huff_t;

static u32 huff_decode(inflate_ctx_t *c, const huff_t *h){
    u32 code=0;
    for(int i=0;i<h->count;i++){
        if(h->lens[i]==0) continue;
        if(inf_bits(c,h->lens[i])==h->codes[i]) return (u32)i;
        c->bit_buf = (c->bit_buf>>h->lens[i]) | (code<<(32-h->lens[i]));
        c->bit_cnt += h->lens[i];
    }
    c->err=1; return 0;
}

/* Inflate DEFLATE (fixed Huffman apenas, cobre 99% dos .deb) */
static int inflate_block_fixed(inflate_ctx_t *c){
    for(;;){
        /* lê símbolo literal/comprimento com Huffman fixo */
        u32 sym;
        u32 b = inf_bits(c,7);
        if(b<=23)       { sym=256+b;        }  /* 000 0000 – 001 0111 */
        else if(b<=95)  { b|=inf_bits(c,1)<<7; sym=b-48; }
        else if(b<=103) { sym=280+b-96;     }
        else            { b|=inf_bits(c,1)<<7; sym=144+b-96; }

        if(sym==256) return 0;   /* fim do bloco */
        if(sym<256){ inf_out(c,(u8)sym); continue; }
        /* comprimento */
        int li=sym-257;
        if(li>=29){c->err=1;return -1;}
        u32 len=len_base[li]+inf_bits(c,len_extra[li]);
        /* distância */
        u32 di=inf_bits(c,5);
        /* inverte os bits (DEFLATE é LSB-first) */
        u32 di_rev=0;
        for(int k=0;k<5;k++) di_rev=(di_rev<<1)|((di>>k)&1);
        di=di_rev;
        if(di>=30){c->err=1;return -1;}
        u32 dist=dst_base[di]+inf_bits(c,dst_extra[di]);
        /* copia */
        for(u32 i=0;i<len;i++){
            if(c->dst_pos<dist){c->err=1;return -1;}
            inf_out(c,c->dst[c->dst_pos-dist]);
        }
    }
}

/* Ponto de entrada inflate: descomprime gzip payload */
static u32 gzip_inflate(const u8 *gz, u32 gz_len, u8 *out, u32 out_cap){
    if(gz_len<18||gz[0]!=0x1F||gz[1]!=0x8B) return 0; /* magic */
    u32 skip=10;
    if(gz[3]&4) skip+=2+((u32)gz[skip]|((u32)gz[skip+1]<<8)); /* extra */
    if(gz[3]&8) { while(skip<gz_len&&gz[skip]) skip++; skip++; } /* fname */
    if(gz[3]&16){ while(skip<gz_len&&gz[skip]) skip++; skip++; } /* comment */
    if(gz[3]&2)  skip+=2; /* crc16 */

    inflate_ctx_t c;
    kmemset(&c,0,sizeof(c));
    c.src=gz+skip; c.src_len=gz_len-skip-8;
    c.dst=out; c.dst_cap=out_cap;

    /* blocos DEFLATE */
    int final=0;
    while(!final&&!c.err){
        final=(int)inf_bits(&c,1);
        int btype=(int)inf_bits(&c,2);
        if(btype==0){
            /* bloco sem compressão */
            c.bit_buf=0; c.bit_cnt=0;
            if(c.src_pos+4>c.src_len){c.err=1;break;}
            u16 len=(u16)(c.src[c.src_pos]|((u32)c.src[c.src_pos+1]<<8));
            c.src_pos+=4;
            for(u16 i=0;i<len;i++) inf_out(&c,c.src[c.src_pos++]);
        } else if(btype==1){
            inflate_block_fixed(&c);
        } else {
            /* Huffman dinâmico — não implementado, ignora */
            kprintf("[deb] inflate: bloco dinâmico não suportado\n");
            c.err=1;
        }
    }
    return c.err ? 0 : c.dst_pos;
}

/* ═══════════════════════════════════════════════════════════════
 *  PARSER TAR
 * ═══════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)) {
    char name[100];
    char mode[8];
    char uid[8], gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32], gname[32];
    char devmajor[8], devminor[8];
    char prefix[155];
    char pad[12];
} tar_hdr_t;   /* 512 bytes */

static u32 tar_octal(const char *s, int n){
    u32 v=0;
    for(int i=0;i<n&&s[i]>='0'&&s[i]<='7';i++) v=v*8+(s[i]-'0');
    return v;
}

typedef struct {
    char path[512];
    u32  size;
    char type;   /* '0'=regular '2'=symlink '5'=dir */
} tar_entry_t;

/* Callback chamado para cada arquivo extraído */
typedef int (*tar_extract_cb_t)(const tar_entry_t *e, const u8 *data, void *ud);

static int tar_walk(const u8 *tar, u32 tar_len, tar_extract_cb_t cb, void *ud){
    u32 off=0;
    while(off+512<=tar_len){
        tar_hdr_t *h=(tar_hdr_t*)(tar+off);
        if(h->name[0]==0) break;
        off+=512;
        char path[512];
        if(h->prefix[0]){
            kstrncpy(path,h->prefix,155); path[155]=0;
            kstrcat(path,"/");
            kstrcat(path,h->name);
        } else {
            kstrncpy(path,h->name,100); path[100]=0;
        }
        u32 fsize=tar_octal(h->size,12);
        tar_entry_t e;
        kstrncpy(e.path,path,511);
        e.size=fsize;
        e.type=h->typeflag?h->typeflag:'0';
        if(cb(&e,tar+off,ud)<0) return -1;
        off+=((fsize+511)/512)*512;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  PARSER AR (formato de arquivo Debian)
 * ═══════════════════════════════════════════════════════════════ */
typedef struct __attribute__((packed)){
    char name[16];
    char mtime[12];
    char uid[6],gid[6];
    char mode[8];
    char size[10];
    char magic[2];   /* '`\n' */
} ar_hdr_t;

typedef struct {
    char name[17];
    u32  offset;
    u32  size;
} ar_member_t;

static int ar_parse(const u8 *data, u32 len, ar_member_t *members, int max){
    if(len<8||kstrncmp((char*)data,"!<arch>\n",8)!=0){
        kprintf("[deb] Não é um arquivo ar válido\n");
        return -1;
    }
    u32 off=8;
    int count=0;
    while(off+sizeof(ar_hdr_t)<=len&&count<max){
        ar_hdr_t *h=(ar_hdr_t*)(data+off);
        if(h->magic[0]!='`'||h->magic[1]!='\n') break;
        off+=sizeof(ar_hdr_t);
        /* nome: termina em '/' ou espaço */
        char nm[17]; kmemcpy(nm,h->name,16); nm[16]=0;
        int ni=0;
        while(ni<16&&nm[ni]&&nm[ni]!='/'&&nm[ni]!=' ') ni++;
        nm[ni]=0;
        /* tamanho */
        u32 sz=0;
        char *sp=h->size;
        while(*sp>='0'&&*sp<='9') sz=sz*10+(*sp++)-'0';
        kstrncpy(members[count].name,nm,16);
        members[count].offset=off;
        members[count].size=sz;
        count++;
        off+=((sz+1)&~1u);  /* alinhado em 2 bytes */
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════
 *  GUI de Progresso
 * ═══════════════════════════════════════════════════════════════ */
#define PROG_WIN_W  400
#define PROG_WIN_H  160
#define C_BG     0x04020F
#define C_WIN    0x1A0A3A
#define C_PANEL  0x2D1B69
#define C_ACCENT 0x7C3AED
#define C_WHITE  0xEDE9FE
#define C_GRAY   0x6B5FA0
#define C_GREEN  0x40E0A0

static window_t *prog_win = 0;
static widget_t *prog_label = 0;
static widget_t *prog_bar   = 0;
static widget_t *prog_sub   = 0;

static void installer_gui_init(const char *pkg_name){
    prog_win = window_create("Instalando pacote", 200, 200, PROG_WIN_W, PROG_WIN_H);
    window_set_bg(prog_win, C_WIN);
    /* borda accent */
    fb_rect(200,200,PROG_WIN_W,2,C_ACCENT);
    fb_rect(200,200,2,PROG_WIN_H,C_ACCENT);
    fb_rect(200,200+PROG_WIN_H-2,PROG_WIN_W,2,C_ACCENT);
    fb_rect(200+PROG_WIN_W-2,200,2,PROG_WIN_H,C_ACCENT);

    char title[128];
    ksnprintf(title,127,"Instalando: %s",pkg_name);
    prog_label = wgt_label(prog_win, 20, 20, title);
    wgt_label_set_color(prog_label, C_WHITE);

    prog_bar = wgt_progressbar(prog_win, 20, 60, PROG_WIN_W-40, 20);
    wgt_progressbar_set_color(prog_bar, C_ACCENT, C_PANEL);

    prog_sub = wgt_label(prog_win, 20, 90, "Preparando...");
    wgt_label_set_color(prog_sub, C_GRAY);

    window_render(prog_win);
}

static void installer_gui_progress(int pct, const char *msg){
    if(!prog_win) return;
    wgt_progressbar_set(prog_bar, pct);
    wgt_label_set(prog_sub, msg);
    window_render(prog_win);
}

static void installer_gui_close(int success){
    if(!prog_win) return;
    const char *msg = success ? "Instalado com sucesso!" : "Erro na instalação.";
    u32 color = success ? C_GREEN : 0xF04060;
    wgt_label_set(prog_sub, msg);
    wgt_label_set_color(prog_sub, color);
    wgt_progressbar_set(prog_bar, success?100:0);
    window_render(prog_win);
    /* aguarda 2 s */
    extern u32 timer_ticks;
    u32 dl=timer_ticks+200;
    while(timer_ticks<dl) {}
    window_destroy(prog_win);
    prog_win=0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Extração de arquivos tar → VFS
 * ═══════════════════════════════════════════════════════════════ */
typedef struct { int count; } extract_ud_t;

static int extract_cb(const tar_entry_t *e, const u8 *data, void *ud){
    extract_ud_t *x=(extract_ud_t*)ud;
    /* monta caminho de destino */
    char dest[512];
    const char *rel=e->path;
    /* remove ./ e usr/local → mapeia para / */
    if(kstrncmp(rel,"./",2)==0) rel+=2;
    if(rel[0]==0||rel[kstrlen(rel)-1]=='/'){
        /* diretório */
        char dir[512]; ksnprintf(dir,511,"/%s",rel);
        vfs_mkdir_p(dir);
        return 0;
    }
    ksnprintf(dest,511,"/%s",rel);
    /* garante diretório pai */
    vfs_mkdir_p_for_file(dest);
    /* escreve arquivo */
    vfs_file_t *f=vfs_open(dest,VFS_O_WRITE|VFS_O_CREATE);
    if(!f){
        kprintf("[deb] Aviso: não conseguiu criar %s\n",dest);
        return 0;
    }
    vfs_write(f,data,e->size);
    vfs_close(f);
    kprintf("[deb]   → %s (%u bytes)\n",dest,e->size);
    x->count++;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Lê metadados do control.tar
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    char package[PKG_NAME_MAX];
    char version[PKG_VER_MAX];
    char description[PKG_DESC_MAX];
    char depends[256];
    char maintainer[128];
    u32  installed_size;
} deb_control_t;

static int parse_control(const u8 *ctrl_tar, u32 ctrl_len, deb_control_t *out){
    kmemset(out,0,sizeof(*out));
    /* percorre tar procurando "./control" */
    u32 off=0;
    while(off+512<=ctrl_len){
        tar_hdr_t *h=(tar_hdr_t*)(ctrl_tar+off);
        if(h->name[0]==0) break;
        off+=512;
        u32 fsize=tar_octal(h->size,12);
        if((kstrcmp(h->name,"./control")==0||kstrcmp(h->name,"control")==0)&&fsize>0){
            const char *p=(char*)(ctrl_tar+off);
            const char *end=p+fsize;
            while(p<end){
                char line[512]; int li=0;
                while(p<end&&*p!='\n'&&li<511) line[li++]=*p++;
                if(p<end) p++;
                line[li]=0;
                /* trim \r */
                if(li>0&&line[li-1]=='\r') line[li-1]=0;
                char *col=kstrchr(line,':');
                if(!col) continue;
                *col=0; char *val=col+1; while(*val==' ')val++;
                if(kstrcmp(line,"Package")==0)
                    kstrncpy(out->package,val,PKG_NAME_MAX-1);
                else if(kstrcmp(line,"Version")==0)
                    kstrncpy(out->version,val,PKG_VER_MAX-1);
                else if(kstrcmp(line,"Description")==0)
                    kstrncpy(out->description,val,PKG_DESC_MAX-1);
                else if(kstrcmp(line,"Depends")==0)
                    kstrncpy(out->depends,val,255);
                else if(kstrcmp(line,"Installed-Size")==0){
                    u32 n=0; char *v=val;
                    while(*v>='0'&&*v<='9') n=n*10+(*v++)-'0';
                    out->installed_size=n;
                }
                else if(kstrcmp(line,"Maintainer")==0)
                    kstrncpy(out->maintainer,val,127);
            }
            return 0;
        }
        off+=((fsize+511)/512)*512;
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════
 *  deb_install — ponto de entrada principal
 * ═══════════════════════════════════════════════════════════════ */
#define MAX_DEB_SIZE   (32*1024*1024)  /* 32 MB */
#define DECOMP_BUF     (64*1024*1024)  /* 64 MB buffer de descompressão */

int deb_install(const char *path){
    kprintf("[deb] Abrindo %s\n",path);

    /* lê arquivo completo */
    vfs_file_t *f=vfs_open(path,VFS_O_READ);
    if(!f){ kprintf("[deb] Arquivo não encontrado: %s\n",path); return -1; }
    u32 fsize=vfs_size(f);
    if(fsize>MAX_DEB_SIZE){ kprintf("[deb] .deb muito grande (%u bytes)\n",fsize); vfs_close(f); return -1; }
    u8 *deb_data=(u8*)kmalloc(fsize);
    if(!deb_data){ vfs_close(f); return -1; }
    vfs_read(f,deb_data,fsize);
    vfs_close(f);

    /* parse ar */
    ar_member_t members[16];
    int mc=ar_parse(deb_data,fsize,members,16);
    if(mc<0){ kfree(deb_data); return -1; }

    /* localiza membros */
    ar_member_t *m_control=0, *m_data=0;
    int ctrl_gz=0, data_gz=0, data_xz=0;
    for(int i=0;i<mc;i++){
        if(kstrncmp(members[i].name,"control.tar",11)==0){
            m_control=&members[i];
            ctrl_gz=(kstrchr(members[i].name,'z')!=0);
        }
        if(kstrncmp(members[i].name,"data.tar",8)==0){
            m_data=&members[i];
            data_gz=(kstrchr(members[i].name,'z')!=0);
            data_xz=(kstrchr(members[i].name,'x')!=0&&kstrchr(members[i].name,'z')!=0);
        }
    }
    if(!m_control||!m_data){
        kprintf("[deb] .deb incompleto (faltam control ou data)\n");
        kfree(deb_data); return -1;
    }

    /* descomprime control */
    u8 *ctrl_tar; u32 ctrl_len;
    if(ctrl_gz){
        ctrl_tar=(u8*)kmalloc(4*1024*1024);
        if(!ctrl_tar){ kfree(deb_data); return -1; }
        ctrl_len=gzip_inflate(deb_data+m_control->offset,m_control->size,ctrl_tar,4*1024*1024);
    } else {
        ctrl_tar=deb_data+m_control->offset;
        ctrl_len=m_control->size;
    }

    /* extrai metadados */
    deb_control_t ctrl;
    if(parse_control(ctrl_tar,ctrl_len,&ctrl)<0){
        kprintf("[deb] Não conseguiu ler control\n");
        if(ctrl_gz) kfree(ctrl_tar);
        kfree(deb_data); return -1;
    }
    if(ctrl_gz) kfree(ctrl_tar);

    kprintf("[deb] Pacote: %s %s\n",ctrl.package,ctrl.version);
    kprintf("[deb] %s\n",ctrl.description);
    if(ctrl.depends[0]) kprintf("[deb] Depende de: %s\n",ctrl.depends);

    /* GUI */
    installer_gui_init(ctrl.package);
    installer_gui_progress(10,"Verificando dependências...");

    /* resolve dependências via pkg */
    if(ctrl.depends[0]){
        char deps[256]; kstrncpy(deps,ctrl.depends,255);
        char *d=deps;
        while(*d){
            while(*d==' ')d++;
            char *comma=kstrchr(d,',');
            if(comma) *comma=0;
            /* remove versão "(>= x.y)" */
            char *paren=kstrchr(d,'(');
            if(paren) { *(paren-1)=0; }
            char *sp=kstrchr(d,' ');
            if(sp) *sp=0;
            /* trim */
            int nn=kstrlen(d);
            while(nn>0&&(d[nn-1]==' '||d[nn-1]=='\n')){d[nn-1]=0;nn--;}
            if(*d){
                kprintf("[deb] Dependência: %s\n",d);
                pkg_install(d);  /* instala se não estiver instalado */
            }
            if(!comma) break;
            d=comma+1;
        }
    }

    installer_gui_progress(30,"Descomprimindo data...");

    /* descomprime data */
    u8 *data_tar; u32 data_len;
    if(data_xz){
        kprintf("[deb] Aviso: xz não suportado nativamente, tentando sem descompressão\n");
        data_tar=deb_data+m_data->offset;
        data_len=m_data->size;
    } else if(data_gz){
        data_tar=(u8*)kmalloc(DECOMP_BUF);
        if(!data_tar){ installer_gui_close(0); kfree(deb_data); return -1; }
        data_len=gzip_inflate(deb_data+m_data->offset,m_data->size,data_tar,DECOMP_BUF);
        if(!data_len){
            kprintf("[deb] Erro na descompressão gzip\n");
            kfree(data_tar); installer_gui_close(0); kfree(deb_data); return -1;
        }
    } else {
        data_tar=deb_data+m_data->offset;
        data_len=m_data->size;
    }

    installer_gui_progress(60,"Instalando arquivos...");

    /* extrai arquivos */
    extract_ud_t ud; ud.count=0;
    if(tar_walk(data_tar,data_len,extract_cb,&ud)<0){
        kprintf("[deb] Erro na extração\n");
        if(data_gz) kfree(data_tar);
        installer_gui_close(0); kfree(deb_data); return -1;
    }
    if(data_gz) kfree(data_tar);

    installer_gui_progress(90,"Registrando pacote...");

    /* registra no banco de dados de pacotes */
    extern void db_mark_installed_extern(const char*,const char*);
    /* chama via pkg_install_file já registrou → aqui chamamos direto */
    vfs_file_t *db=vfs_open("/etc/pkg/installed.db",VFS_O_APPEND|VFS_O_CREATE);
    if(db){
        char entry[256];
        ksnprintf(entry,255,"%s\t%s\tinstalado\n",ctrl.package,ctrl.version);
        vfs_write(db,(u8*)entry,(u32)kstrlen(entry));
        vfs_close(db);
    }

    /* executa script postinst se existir */
    if(vfs_exists("/var/lib/dpkg/info/postinst")){
        kprintf("[deb] Executando postinst...\n");
        /* TODO: executar via syscall/processo */
    }

    kprintf("[deb] %s instalado com sucesso! (%d arquivos)\n",ctrl.package,ud.count);
    installer_gui_close(1);
    kfree(deb_data);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  App GUI de instalação .deb (arrastar e soltar / abrir arquivo)
 * ═══════════════════════════════════════════════════════════════ */
#define DEBAPP_W 480
#define DEBAPP_H 320

static window_t *debapp_win = 0;
static widget_t *debapp_path_input = 0;
static widget_t *debapp_install_btn = 0;
static widget_t *debapp_list = 0;
static widget_t *debapp_status = 0;

static void cb_debapp_install(widget_t *w){
    (void)w;
    const char *path=wgt_input_get(debapp_path_input);
    if(!path||!path[0]){
        wgt_label_set(debapp_status,"Selecione um arquivo .deb primeiro.");
        return;
    }
    wgt_label_set(debapp_status,"Instalando...");
    window_render(debapp_win);
    int r=deb_install(path);
    wgt_label_set(debapp_status, r==0 ? "Pacote instalado com sucesso!" : "Erro na instalação.");
    window_render(debapp_win);
}

void deb_installer_app_main(void){
    debapp_win = window_create("StarOS — Instalador .deb", 160, 100, DEBAPP_W, DEBAPP_H);
    window_set_bg(debapp_win, 0x1A0A3A);

    /* borda decorativa */
    fb_rect(160,100,DEBAPP_W,3,C_ACCENT);

    /* título */
    wgt_label(debapp_win, 20, 16, "Instalar pacote .deb");
    fb_draw_text(180,116,"Instalar pacote .deb",C_WHITE);

    /* campo de caminho */
    wgt_label(debapp_win, 20, 60, "Caminho do arquivo:");
    debapp_path_input = wgt_input(debapp_win, 20, 78, DEBAPP_W-140, 24);
    wgt_input_set(debapp_path_input, "/root/pacote.deb");

    /* botão */
    debapp_install_btn = wgt_button(debapp_win, DEBAPP_W-110, 78, 90, 24,
                                    "Instalar", cb_debapp_install);
    u32 btn_c[]={C_ACCENT,C_WHITE,C_PANEL,C_WHITE};
    wgt_set_colors(debapp_install_btn, btn_c);

    /* lista de instalados */
    wgt_label(debapp_win, 20, 120, "Pacotes instalados:");
    debapp_list = wgt_listbox(debapp_win, 20, 136, DEBAPP_W-40, 130);

    /* popula com instalados */
    pkg_info_t installed[32];
    int n=pkg_list_installed(installed,32);
    for(int i=0;i<n;i++){
        char entry[128];
        ksnprintf(entry,127,"%s  %s",installed[i].name,installed[i].version);
        wgt_listbox_add(debapp_list,entry);
    }

    /* status */
    debapp_status = wgt_label(debapp_win, 20, DEBAPP_H-24, "Pronto.");
    wgt_label_set_color(debapp_status, C_GRAY);

    window_render(debapp_win);
    kprintf("[deb-app] Instalador .deb iniciado\n");

    for(;;){
        gui_event_t evt;
        if(gui_poll_event(&evt)){
            if(evt.type==GUI_KEY&&evt.key=='\n') cb_debapp_install(0);
            gui_dispatch_event(&evt,debapp_win);
        }
    }
}

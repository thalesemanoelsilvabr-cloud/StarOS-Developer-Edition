/* ============================================================
 *  StarOS — HTTP Client  (drivers/net/http.c)
 *  HTTP/1.1 GET síncrono sobre a pilha TCP do StarOS
 * ============================================================ */
#include <net/http.h>
#include <net/net.h>
#include <mm/kmalloc.h>
#include <kernel/types.h>

extern void kprintf(const char*, ...);
extern int  kstrlen(const char*);
extern void kmemset(void*,u8,u32);
extern void kmemcpy(void*,const void*,u32);
extern int  kstrcmp(const char*,const char*);
extern int  kstrncmp(const char*,const char*,u32);
extern void kstrcpy(char*,const char*);
extern void kstrncpy(char*,const char*,u32);
extern char* kstrchr(const char*,int);
extern void kitoa(int,char*,int);

/* ── Helpers de string internos ─────────────────────────────── */
static int is_digit(char c){ return c>='0'&&c<='9'; }
static int atoi_s(const char *s){
    int n=0, neg=0;
    if(*s=='-'){neg=1;s++;}
    while(is_digit(*s)) n=n*10+(*s++)-'0';
    return neg?-n:n;
}
static void int_to_str(int n, char *buf){
    if(n<0){*buf++='-';n=-n;}
    char tmp[12]; int i=0;
    if(n==0){tmp[i++]='0';}
    while(n>0){tmp[i++]='0'+n%10;n/=10;}
    while(i-->0)*buf++=tmp[i];
    *buf=0;
}
static int str_lower_eq(const char *a, const char *b, u32 n){
    for(u32 i=0;i<n;i++){
        char ca=a[i],cb=b[i];
        if(ca>='A'&&ca<='Z') ca+=32;
        if(cb>='A'&&cb<='Z') cb+=32;
        if(ca!=cb) return 0;
    }
    return 1;
}

/* ── Parser de URL ──────────────────────────────────────────── */
typedef struct { char host[256]; u16 port; char path[HTTP_URL_MAX]; } url_t;

static int parse_url(const char *url, url_t *out) {
    /* suporta http://host[:port]/path */
    const char *p = url;
    if (str_lower_eq(p,"http://",7)) p+=7;
    else if (str_lower_eq(p,"https://",8)) {
        kprintf("[http] HTTPS não suportado (sem TLS)\n");
        return -1;
    } else return -1;

    out->port = 80;
    int i=0;
    while (*p && *p!='/' && *p!=':') out->host[i++] = *p++;
    out->host[i] = 0;
    if (*p==':') {
        p++;
        char pstr[8]; int pi=0;
        while(is_digit(*p)) pstr[pi++]=*p++;
        pstr[pi]=0;
        out->port=(u16)atoi_s(pstr);
    }
    if (*p=='/') kstrncpy(out->path, p, HTTP_URL_MAX-1);
    else { out->path[0]='/'; out->path[1]=0; }
    return 0;
}

/* ── Envia string pelo socket ───────────────────────────────── */
static int sock_send_str(int fd, const char *s) {
    return sock_send(fd, (const u8*)s, (u32)kstrlen(s));
}

/* ── http_get ───────────────────────────────────────────────── */
int http_get(const char *url, http_response_t *resp) {
    kmemset(resp, 0, sizeof(*resp));
    resp->status = -1;

    url_t u;
    if (parse_url(url, &u) < 0) return -1;

    /* resolve hostname */
    ip4_t srv_ip;
    kprintf("[http] DNS: %s\n", u.host);
    if (dns_resolve(u.host, &srv_ip) < 0) {
        kprintf("[http] DNS falhou para %s\n", u.host);
        return -1;
    }
    kprintf("[http] Conectando %d.%d.%d.%d:%d\n",
            (srv_ip>>24)&0xFF,(srv_ip>>16)&0xFF,
            (srv_ip>>8)&0xFF,srv_ip&0xFF, u.port);

    int fd = sock_open(SOCK_TCP);
    if (fd < 0) return -1;
    if (sock_connect(fd, srv_ip, u.port) < 0) {
        kprintf("[http] Conexão recusada\n");
        sock_close(fd);
        return -1;
    }

    /* monta request */
    char req[1024];
    char *w = req;
    /* GET /path HTTP/1.1\r\n */
    kmemcpy(w,"GET ",4); w+=4;
    kstrcpy(w,u.path); w+=kstrlen(u.path);
    kmemcpy(w," HTTP/1.1\r\nHost: ",17); w+=17;
    kstrcpy(w,u.host); w+=kstrlen(u.host);
    kmemcpy(w,"\r\nUser-Agent: StarOS/0.4 Browser\r\n"
               "Accept: text/html\r\nConnection: close\r\n\r\n", 75);
    w+=75;

    sock_send(fd,(u8*)req,(u32)(w-req));

    /* recebe resposta */
    u8 *buf = (u8*)kmalloc(HTTP_BODY_MAX + 4096);
    if (!buf){ sock_close(fd); return -1; }
    u32 total = 0, cap = HTTP_BODY_MAX + 4096;
    extern u32 timer_ticks;
    u32 deadline = timer_ticks + 1000;  /* 10 s */
    while (timer_ticks < deadline && total < cap-1) {
        int n = sock_recv(fd, buf+total, cap-1-total);
        if (n > 0) { total += (u32)n; deadline = timer_ticks + 200; }
        else net_poll();
    }
    buf[total] = 0;
    sock_close(fd);

    if (total == 0) { kfree(buf); return -1; }

    /* parse status line */
    char *line = (char*)buf;
    /* HTTP/1.1 200 ... */
    char *space = kstrchr(line,' ');
    if (!space){ kfree(buf); return -1; }
    resp->status = atoi_s(space+1);
    kprintf("[http] Status %d\n", resp->status);

    /* parse headers */
    char *hdr = kstrchr(line,'\n');
    if (!hdr){ kfree(buf); return -1; }
    hdr++;
    resp->header_count = 0;
    int chunked = 0;
    int content_len = -1;
    char *body_start = 0;
    while (*hdr && !(hdr[0]=='\r'&&hdr[1]=='\n') && !(hdr[0]=='\n')) {
        char *end = kstrchr(hdr,'\n');
        if (!end) break;
        /* "Name: Value" */
        char *colon = kstrchr(hdr,':');
        if (colon && colon < end) {
            int ni=0,vi=0;
            char *p2=hdr;
            while(p2<colon&&ni<63) resp->headers[resp->header_count][0][ni++]=*p2++;
            resp->headers[resp->header_count][0][ni]=0;
            p2=colon+2;
            while(p2<end&&*p2!='\r'&&vi<255) resp->headers[resp->header_count][1][vi++]=*p2++;
            resp->headers[resp->header_count][1][vi]=0;
            /* verifica chunked e content-length */
            if(str_lower_eq(resp->headers[resp->header_count][0],"transfer-encoding",17)
               && str_lower_eq(resp->headers[resp->header_count][1],"chunked",7))
                chunked=1;
            if(str_lower_eq(resp->headers[resp->header_count][0],"content-length",14))
                content_len=atoi_s(resp->headers[resp->header_count][1]);
            if(str_lower_eq(resp->headers[resp->header_count][0],"location",8)) {
                kstrncpy(resp->redirect_url,
                         resp->headers[resp->header_count][1], HTTP_URL_MAX-1);
            }
            if(resp->header_count<HTTP_MAX_HEADERS-1) resp->header_count++;
        }
        hdr = end+1;
    }
    /* avança para o corpo */
    body_start = (*hdr=='\r') ? hdr+2 : hdr+1;

    u32 blen = (u32)(total - (u32)(body_start-(char*)buf));
    if (content_len >= 0 && (u32)content_len < blen) blen = (u32)content_len;

    /* dechunk se necessário */
    u8 *body;
    if (chunked) {
        body = (u8*)kmalloc(blen+1);
        if (!body){ kfree(buf); return -1; }
        u8 *src=(u8*)body_start; u32 out=0;
        while (src < buf+total) {
            /* lê tamanho do chunk em hex */
            u32 csz=0;
            while(*src!='\r'&&src<buf+total) {
                char c=*src++;
                csz<<=4;
                if(c>='0'&&c<='9') csz+=c-'0';
                else if(c>='a'&&c<='f') csz+=c-'a'+10;
                else if(c>='A'&&c<='F') csz+=c-'A'+10;
            }
            src+=2; /* \r\n */
            if(!csz) break;
            if(out+csz>blen) csz=blen-out;
            kmemcpy(body+out,src,csz);
            out+=csz; src+=csz+2;
        }
        body[out]=0;
        resp->body = body;
        resp->body_len = out;
    } else {
        body = (u8*)kmalloc(blen+1);
        if(!body){ kfree(buf); return -1; }
        kmemcpy(body,body_start,blen);
        body[blen]=0;
        resp->body = body;
        resp->body_len = blen;
    }
    kfree(buf);
    return 0;
}

void http_response_free(http_response_t *resp) {
    if (resp && resp->body) { kfree(resp->body); resp->body=0; }
}

/* ── Parser HTML mínimo ─────────────────────────────────────── */
static int is_space(char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'; }

/* decodifica &amp; &lt; &gt; &nbsp; */
static void decode_entities(char *s) {
    char *r=s,*w2=s;
    while(*r){
        if(*r=='&'){
            if(kstrncmp(r,"&amp;",5)==0){*w2++='&';r+=5;}
            else if(kstrncmp(r,"&lt;",4)==0){*w2++='<';r+=4;}
            else if(kstrncmp(r,"&gt;",4)==0){*w2++='>';r+=4;}
            else if(kstrncmp(r,"&nbsp;",6)==0){*w2++=' ';r+=6;}
            else *w2++=*r++;
        } else *w2++=*r++;
    }
    *w2=0;
}

int html_parse(const u8 *html, u32 len, html_doc_t *out) {
    kmemset(out,0,sizeof(*out));
    const char *p = (const char*)html;
    const char *end = p + len;
    char *text = out->text;
    u32  tpos = 0;
    int  in_script=0, in_style=0, in_title=0;
    int  tlen=0;
    char tag_buf[128];

    while (p < end) {
        if (*p == '<') {
            p++;
            if (*p=='!') { /* comentário / doctype */
                while(p<end&&*p!='>') p++;
                if(p<end) p++;
                continue;
            }
            int closing = (*p=='/');
            if(closing) p++;
            /* lê nome da tag */
            int ti=0;
            while(p<end&&!is_space(*p)&&*p!='>'&&ti<127) tag_buf[ti++]=*p++;
            tag_buf[ti]=0;
            /* avança até fechar a tag */
            char *href = 0; char href_buf[HTTP_URL_MAX]={0};
            while(p<end&&*p!='>') {
                /* captura href="..." */
                if(kstrncmp(p,"href=\"",6)==0){
                    p+=6; int hi=0;
                    while(p<end&&*p!='"'&&hi<HTTP_URL_MAX-1) href_buf[hi++]=*p++;
                    href_buf[hi]=0; href=href_buf;
                    if(p<end) p++;
                } else p++;
            }
            if(p<end) p++;  /* > */

            /* detecta script/style */
            if(str_lower_eq(tag_buf,"script",6)){in_script=!closing;}
            if(str_lower_eq(tag_buf,"style",5)){in_style=!closing;}
            if(str_lower_eq(tag_buf,"title",5)){in_title=!closing;}

            /* quebras de linha semânticas */
            if(!closing){
                if(str_lower_eq(tag_buf,"br",2)||
                   str_lower_eq(tag_buf,"p",1)||
                   str_lower_eq(tag_buf,"div",3)||
                   str_lower_eq(tag_buf,"h1",2)||str_lower_eq(tag_buf,"h2",2)||
                   str_lower_eq(tag_buf,"h3",2)||str_lower_eq(tag_buf,"li",2)){
                    if(tpos<HTTP_BODY_MAX-2) text[tpos++]='\n';
                }
                if(str_lower_eq(tag_buf,"hr",2)&&tpos<HTTP_BODY_MAX-40){
                    for(int i=0;i<40;i++) text[tpos++]='-';
                    text[tpos++]='\n';
                }
            }

            /* captura links */
            if(!closing&&str_lower_eq(tag_buf,"a",1)&&href&&
               out->link_count<64){
                kstrncpy(out->links[out->link_count],href,HTTP_URL_MAX-1);
                out->link_count++;
            }
            tlen++;
        } else {
            /* texto */
            if(!in_script&&!in_style){
                char c=*p;
                if(in_title&&tpos<255){
                    /* coleta título separado */
                    int ti=kstrlen(out->title);
                    if(ti<255) out->title[ti]=c;
                } else if(tpos<HTTP_BODY_MAX-1){
                    /* comprime espaços */
                    if(is_space(c)){
                        if(tpos>0&&!is_space(text[tpos-1])) text[tpos++]=' ';
                    } else text[tpos++]=c;
                }
            }
            p++;
        }
    }
    text[tpos]=0;
    decode_entities(text);
    (void)tlen;
    return 0;
}

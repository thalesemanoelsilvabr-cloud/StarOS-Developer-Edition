/* net.c — Pilha TCP/IP real para StarOS
 * Ethernet/ARP/IP/ICMP/TCP/UDP/DNS/DHCP
 */
#include <net/net.h>
#include <mm/kmalloc.h>
/* structs e enums definidos em net.h */
#include <kernel/types.h>

extern void kprintf(const char*,...);
extern void ksnprintf(char*,u32,const char*,...);
extern void kstrncpy(char*,const char*,u32);
extern void kstrlen(const char*);
extern void kmemset(void*,u8,u32);
extern void kmemcpy(void*,const void*,u32);
extern int  kstrcmp(const char*,const char*);
extern u32  volatile timer_ticks;

/* endian helpers em net.h */

/* ── Config de rede (ajustada via DHCP) ─────────────────── */
extern u8 g_mac[6]; /* definido em ne2000.c */
ip4_t g_ip     = 0;               /* preenchido pelo DHCP */
ip4_t g_gw     = 0;
ip4_t g_mask   = 0;
ip4_t g_dns    = 0;
static int    dhcp_ok = 0;

/* ── Buffers de frame ───────────────────────────────────── */
#define FRAME_MAX 1536
static u8 tx_frame[FRAME_MAX];
static u8 rx_frame[FRAME_MAX];

/* ── ARP cache ──────────────────────────────────────────── */
#define ARP_CACHE 16
typedef struct { ip4_t ip; u8 mac[6]; u32 ts; u8 valid; } arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE];

/* ── Checksum ───────────────────────────────────────────── */
static u16 ip_cksum(const void* data, u32 len){
    const u16* p=(const u16*)data;
    u32 sum=0;
    while(len>1){ sum+=*p++; len-=2; }
    if(len) sum+=*(u8*)p;
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (u16)~sum;
}

static u16 transport_cksum(ip4_t src,ip4_t dst,u8 proto,
                            const void* data,u16 len){
    u32 sum=0;
    sum+=(src>>16)&0xFFFF; sum+=src&0xFFFF;
    sum+=(dst>>16)&0xFFFF; sum+=dst&0xFFFF;
    sum+=htons((u16)proto);
    sum+=htons(len);
    const u16* p=(const u16*)data;
    u32 l=len;
    while(l>1){ sum+=*p++; l-=2; }
    if(l) sum+=*(u8*)p;
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (u16)~sum;
}

/* ── Ethernet build ─────────────────────────────────────── */
static u16 ip_id=1;
/* structs definidas em net.h */

/* ETH frame local (sem flex array) */
typedef struct __attribute__((packed)){
    u8  dst[6]; u8 src[6]; u16 etype;
} local_eth_t;
static u8* eth_begin(const u8 dst_mac[6], u16 etype){
    local_eth_t* e=(local_eth_t*)tx_frame;
    for(int i=0;i<6;i++) e->dst[i]=dst_mac[i];
    for(int i=0;i<6;i++) e->src[i]=g_mac[i];
    e->etype=htons(etype);
    return tx_frame+sizeof(local_eth_t);
}

extern void ne2k_send(const u8*,u16);
extern u16  ne2k_recv(u8*,u16);

static void eth_send(u16 total){ ne2k_send(tx_frame,total); }

/* ── ARP ────────────────────────────────────────────────── */
static void arp_cache_put(ip4_t ip, const u8 mac[6]){
    int oldest=0;
    for(int i=0;i<ARP_CACHE;i++){
        if(!arp_cache[i].valid||arp_cache[i].ip==ip){oldest=i;break;}
        if(arp_cache[i].ts<arp_cache[oldest].ts) oldest=i;
    }
    arp_cache[oldest].ip=ip;
    for(int i=0;i<6;i++) arp_cache[oldest].mac[i]=mac[i];
    arp_cache[oldest].ts=timer_ticks;
    arp_cache[oldest].valid=1;
}

static void arp_send_request(ip4_t target){
    u8 bcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    u8* p=eth_begin(bcast,0x0806);
    arp_pkt_t* a=(arp_pkt_t*)p;
    a->htype=htons(1); a->ptype=htons(0x0800);
    a->hlen=6; a->plen=4; a->oper=htons(1);
    for(int i=0;i<6;i++) a->sha[i]=g_mac[i];
    a->spa=htonl(g_ip);
    for(int i=0;i<6;i++) a->tha[i]=0;
    a->tpa=htonl(target);
    eth_send((u16)(sizeof(local_eth_t)+sizeof(arp_pkt_t)));
}

void arp_request(ip4_t ip){ arp_send_request(ip); }

int arp_resolve(ip4_t ip, u8 out_mac[6]){
    if(ip==0xFFFFFFFF){for(int i=0;i<6;i++) out_mac[i]=0xFF;return 0;}
    ip4_t target=((ip&g_mask)==(g_ip&g_mask))?ip:g_gw;
    for(int i=0;i<ARP_CACHE;i++){
        if(arp_cache[i].valid&&arp_cache[i].ip==target){
            for(int j=0;j<6;j++) out_mac[j]=arp_cache[i].mac[j];
            return 0;
        }
    }
    arp_send_request(target);
    u32 dl=timer_ticks+50;
    while(timer_ticks<dl){
        net_poll();
        for(int i=0;i<ARP_CACHE;i++){
            if(arp_cache[i].valid&&arp_cache[i].ip==target){
                for(int j=0;j<6;j++) out_mac[j]=arp_cache[i].mac[j];
                return 0;
            }
        }
    }
    return -1;
}

/* ── Sockets ────────────────────────────────────────────── */
#define MAX_SOCKS   8
#define RXBUF       8192
#define TXBUF       4096

/* tcp_state_t definida em net.h */

typedef struct {
    u8  type;           /* SOCK_TCP/SOCK_UDP/SOCK_NONE */
    ip4_t remote_ip;
    u16 remote_port, local_port;
    tcp_state_t state;
    u32 tx_seq, rx_seq;
    u8  connected;
    u8  rx_buf[RXBUF];
    u32 rx_head, rx_tail;
} sock_t;

static sock_t socks[MAX_SOCKS];

static u32 rb_avail(u32 h,u32 t,u32 cap){return h>=t?h-t:cap-t+h;}
static void rb_push(u8* buf,u32* h,u32 cap,const u8* d,u32 n){
    for(u32 i=0;i<n;i++){buf[*h]=d[i];*h=(*h+1)%cap;}
}
static u32 rb_pop(u8* buf,u32* t,u32 cap,u8* out,u32 max){
    u32 n=0;
    while(n<max && *t!=(u32)-1){
        /* calcula avail inline */
        out[n++]=buf[*t];
        *t=(*t+1)%cap;
    }
    return n;
}

/* ── IP send ────────────────────────────────────────────── */
static int ip_send(ip4_t dst_ip,u8 proto,const u8* payload,u16 plen){
    u8 dst_mac[6];
    if(arp_resolve(dst_ip,dst_mac)<0) return -1;
    u8* p=eth_begin(dst_mac,0x0800);
    ip_hdr_t* ih=(ip_hdr_t*)p;
    ih->ver_ihl=0x45; ih->tos=0;
    ih->total_len=htons((u16)(20+plen));
    ih->id=htons(ip_id++);
    ih->flags_off=htons(0x4000);
    ih->ttl=64; ih->proto=proto;
    ih->checksum=0;
    ih->src=htonl(g_ip);
    ih->dst=htonl(dst_ip);
    u8* pay=p+sizeof(ip_hdr_t);
    for(u16 i=0;i<plen;i++) pay[i]=payload[i];
    ih->checksum=ip_cksum(ih,20);
    eth_send((u16)(sizeof(local_eth_t)+20+plen));
    return 0;
}

/* ── TCP ────────────────────────────────────────────────── */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

static void tcp_send_flags(sock_t* s,u8 flags,const u8* data,u16 dlen){
    u8 seg[sizeof(tcp_hdr_t)+dlen];
    tcp_hdr_t* th=(tcp_hdr_t*)seg;
    th->src_port=htons(s->local_port);
    th->dst_port=htons(s->remote_port);
    th->seq=htonl(s->tx_seq);
    th->ack_num=(flags&TCP_ACK)?htonl(s->rx_seq):0;
    th->data_off=0x50; th->flags=flags;
    th->window=htons(8192);
    th->checksum=0; th->urgent=0;
    for(u16 i=0;i<dlen;i++) seg[sizeof(tcp_hdr_t)+i]=data[i];
    th->checksum=transport_cksum(g_ip,s->remote_ip,6,seg,(u16)(sizeof(tcp_hdr_t)+dlen));
    ip_send(s->remote_ip,6,seg,(u16)(sizeof(tcp_hdr_t)+dlen));
    if(flags&(TCP_SYN|TCP_FIN)) s->tx_seq++;
    s->tx_seq+=dlen;
}

static void tcp_handle(ip_hdr_t* ih,tcp_hdr_t* th,u8* data,u16 dlen){
    u16 dp=ntohs(th->dst_port);
    ip4_t src=ntohl(ih->src);
    for(int i=0;i<MAX_SOCKS;i++){
        sock_t* s=&socks[i];
        if(s->type!=SOCK_TCP||s->local_port!=dp) continue;
        if(s->remote_ip&&s->remote_ip!=src) continue;
        u8 fl=th->flags;
        if((fl&TCP_SYN)&&(fl&TCP_ACK)&&s->state==TCP_SYN_SENT){
            s->rx_seq=ntohl(th->seq)+1;
            s->tx_seq=ntohl(th->ack_num);
            s->state=TCP_ESTABLISHED; s->connected=1;
            s->remote_port=ntohs(th->src_port);
            tcp_send_flags(s,TCP_ACK,0,0);
        } else if(fl&TCP_ACK){
            s->tx_seq=ntohl(th->ack_num);
        }
        if(dlen>0&&s->state==TCP_ESTABLISHED){
            s->rx_seq=ntohl(th->seq)+dlen;
            u32 free=RXBUF-rb_avail(s->rx_head,s->rx_tail,RXBUF)-1;
            if(dlen<=free) rb_push(s->rx_buf,&s->rx_head,RXBUF,data,dlen);
            tcp_send_flags(s,TCP_ACK,0,0);
        }
        if(fl&TCP_FIN){
            s->rx_seq++;
            tcp_send_flags(s,TCP_ACK,0,0);
            s->state=TCP_CLOSE_WAIT;
        }
        if(fl&TCP_RST){ s->state=TCP_CLOSED; s->connected=0; }
        return;
    }
}

/* ── UDP ────────────────────────────────────────────────── */
static void udp_handle(ip_hdr_t* ih,udp_hdr_t* uh,u8* data,u16 dlen){
    u16 dp=ntohs(uh->dst_port);
    for(int i=0;i<MAX_SOCKS;i++){
        sock_t* s=&socks[i];
        if(s->type!=SOCK_UDP||s->local_port!=dp) continue;
        u32 free=RXBUF-rb_avail(s->rx_head,s->rx_tail,RXBUF)-1;
        if(dlen<=free) rb_push(s->rx_buf,&s->rx_head,RXBUF,data,dlen);
        return;
    }
    (void)ih;
}

/* ── DHCP ────────────────────────────────────────────────── */
#define DHCP_PORT_CLIENT 68
#define DHCP_PORT_SERVER 67
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

typedef struct __attribute__((packed)){
    u8  op,htype,hlen,hops;
    u32 xid;
    u16 secs,flags;
    u32 ciaddr,yiaddr,siaddr,giaddr;
    u8  chaddr[16];
    u8  sname[64];
    u8  file[128];
    u32 magic;
    u8  options[308];
} dhcp_pkt_t;

static u32 dhcp_xid=0x12345678;

static void dhcp_send_discover(void){
    u8 seg[sizeof(udp_hdr_t)+sizeof(dhcp_pkt_t)];
    udp_hdr_t* uh=(udp_hdr_t*)seg;
    dhcp_pkt_t* d=(dhcp_pkt_t*)(seg+sizeof(udp_hdr_t));
    kmemset(d,0,sizeof(dhcp_pkt_t));
    d->op=1; d->htype=1; d->hlen=6; d->hops=0;
    d->xid=htonl(dhcp_xid);
    d->flags=htons(0x8000);  /* broadcast */
    for(int i=0;i<6;i++) d->chaddr[i]=g_mac[i];
    d->magic=htonl(0x63825363);
    /* options: DISCOVER + param request */
    d->options[0]=53; d->options[1]=1; d->options[2]=DHCP_DISCOVER;
    d->options[3]=55; d->options[4]=4;
    d->options[5]=1;  /* subnet */
    d->options[6]=3;  /* router */
    d->options[7]=6;  /* DNS */
    d->options[8]=15; /* domain */
    d->options[9]=255;/* end */
    u16 plen=(u16)(sizeof(udp_hdr_t)+sizeof(dhcp_pkt_t));
    uh->src_port=htons(DHCP_PORT_CLIENT);
    uh->dst_port=htons(DHCP_PORT_SERVER);
    uh->length=htons(plen);
    uh->checksum=0;
    ip_send(0xFFFFFFFF,17,seg,plen);
    kprintf("[dhcp] Discover enviado\n");
}

static void dhcp_handle_offer(dhcp_pkt_t* d){
    ip4_t offered=ntohl(d->yiaddr);
    /* envia REQUEST */
    u8 seg[sizeof(udp_hdr_t)+sizeof(dhcp_pkt_t)];
    udp_hdr_t* uh=(udp_hdr_t*)seg;
    dhcp_pkt_t* req=(dhcp_pkt_t*)(seg+sizeof(udp_hdr_t));
    kmemset(req,0,sizeof(dhcp_pkt_t));
    req->op=1; req->htype=1; req->hlen=6;
    req->xid=htonl(dhcp_xid);
    req->flags=htons(0x8000);
    for(int i=0;i<6;i++) req->chaddr[i]=g_mac[i];
    req->magic=htonl(0x63825363);
    int oi=0;
    req->options[oi++]=53;req->options[oi++]=1;req->options[oi++]=DHCP_REQUEST;
    req->options[oi++]=50;req->options[oi++]=4;
    req->options[oi++]=(u8)(offered>>24);
    req->options[oi++]=(u8)(offered>>16);
    req->options[oi++]=(u8)(offered>>8);
    req->options[oi++]=(u8)(offered);
    req->options[oi++]=255;
    u16 plen=(u16)(sizeof(udp_hdr_t)+sizeof(dhcp_pkt_t));
    uh->src_port=htons(DHCP_PORT_CLIENT);
    uh->dst_port=htons(DHCP_PORT_SERVER);
    uh->length=htons(plen);
    uh->checksum=0;
    ip_send(0xFFFFFFFF,17,seg,plen);
}

static void dhcp_handle_ack(dhcp_pkt_t* d){
    g_ip=ntohl(d->yiaddr);
    /* parse options para máscara, gateway, DNS */
    u8* opt=d->options;
    while(*opt!=255&&opt<d->options+308){
        u8 code=*opt++; u8 len=*opt++;
        switch(code){
        case 1: /* subnet */
            if(len==4) g_mask=ntohl(*(u32*)opt);
            break;
        case 3: /* router */
            if(len>=4) g_gw=ntohl(*(u32*)opt);
            break;
        case 6: /* DNS */
            if(len>=4) g_dns=ntohl(*(u32*)opt);
            break;
        }
        opt+=len;
    }
    dhcp_ok=1;
    kprintf("[dhcp] IP=%u.%u.%u.%u GW=%u.%u.%u.%u\n",
            (g_ip>>24)&0xFF,(g_ip>>16)&0xFF,(g_ip>>8)&0xFF,g_ip&0xFF,
            (g_gw>>24)&0xFF,(g_gw>>16)&0xFF,(g_gw>>8)&0xFF,g_gw&0xFF);
}

/* ── ARP/IP handler ─────────────────────────────────────── */
static void arp_handle(arp_pkt_t* a){
    ip4_t spa=ntohl(a->spa);
    arp_cache_put(spa,a->sha);
    if(ntohs(a->oper)==1&&ntohl(a->tpa)==g_ip){
        u8* p=eth_begin(a->sha,0x0806);
        arp_pkt_t* r=(arp_pkt_t*)p;
        r->htype=htons(1);r->ptype=htons(0x0800);r->hlen=6;r->plen=4;r->oper=htons(2);
        for(int i=0;i<6;i++) r->sha[i]=g_mac[i];
        r->spa=htonl(g_ip);
        for(int i=0;i<6;i++) r->tha[i]=a->sha[i];
        r->tpa=a->spa;
        eth_send((u16)(sizeof(local_eth_t)+sizeof(arp_pkt_t)));
    }
}

static void ip_handle(ip_hdr_t* ih, u16 total){
    u16 ihl=(u16)((ih->ver_ihl&0xF)*4);
    u16 plen=(u16)(ntohs(ih->total_len)-ihl);
    u8* proto_data=(u8*)ih+ihl;
    if(ih->proto==6&&plen>=(u16)sizeof(tcp_hdr_t)){
        tcp_hdr_t* th=(tcp_hdr_t*)proto_data;
        u16 thdr=(u16)((th->data_off>>4)*4);
        tcp_handle(ih,th,proto_data+thdr,(u16)(plen-thdr));
    } else if(ih->proto==17&&plen>=(u16)sizeof(udp_hdr_t)){
        udp_hdr_t* uh=(udp_hdr_t*)proto_data;
        u8* udata=proto_data+sizeof(udp_hdr_t);
        u16 ulen=(u16)(ntohs(uh->length)-sizeof(udp_hdr_t));
        /* DHCP? */
        if(ntohs(uh->dst_port)==DHCP_PORT_CLIENT&&ulen>=(u16)sizeof(dhcp_pkt_t)){
            dhcp_pkt_t* d=(dhcp_pkt_t*)udata;
            if(ntohl(d->magic)==0x63825363&&d->xid==htonl(dhcp_xid)){
                u8 msg_type=0;
                u8* opt=d->options;
                while(*opt!=255&&opt<d->options+308){
                    u8 c=*opt++;u8 l=*opt++;
                    if(c==53&&l==1) msg_type=*opt;
                    opt+=l;
                }
                if(msg_type==DHCP_OFFER) dhcp_handle_offer(d);
                else if(msg_type==DHCP_ACK) dhcp_handle_ack(d);
            }
        } else {
            udp_handle(ih,uh,udata,ulen);
        }
    }
    (void)total;
}

/* ── net_poll ───────────────────────────────────────────── */
void net_poll(void){
    u16 len;
    while((len=ne2k_recv(rx_frame,FRAME_MAX))>0){
        if(len<14) continue;
        local_eth_t* e=(local_eth_t*)rx_frame;
        u16 etype=ntohs(e->etype);
        if(etype==0x0806&&len>=(u16)(sizeof(local_eth_t)+sizeof(arp_pkt_t)))
            arp_handle((arp_pkt_t*)(rx_frame+sizeof(local_eth_t)));
        else if(etype==0x0800&&len>sizeof(local_eth_t))
            ip_handle((ip_hdr_t*)(rx_frame+sizeof(local_eth_t)),len);
    }
}

/* ── net_init (com DHCP) ────────────────────────────────── */
void net_init(void){
    kmemset(socks,0,sizeof(socks));
    for(int i=0;i<MAX_SOCKS;i++) socks[i].type=SOCK_NONE;
    kmemset(arp_cache,0,sizeof(arp_cache));

    kprintf("[net] Iniciando DHCP...\n");
    /* tenta DHCP por ate 5 segundos */
    dhcp_send_discover();
    u32 dl=timer_ticks+500;
    while(timer_ticks<dl&&!dhcp_ok) net_poll();

    if(!dhcp_ok){
        /* fallback estatico QEMU */
        g_ip  = MAKE_IP(10,0,2,15);
        g_gw  = MAKE_IP(10,0,2,2);
        g_mask= MAKE_IP(255,255,255,0);
        g_dns = MAKE_IP(10,0,2,3);
        kprintf("[net] DHCP falhou — IP estatico 10.0.2.15\n");
    }
    kprintf("[net] IP=%u.%u.%u.%u  GW=%u.%u.%u.%u  DNS=%u.%u.%u.%u\n",
            (g_ip>>24)&0xFF,(g_ip>>16)&0xFF,(g_ip>>8)&0xFF,g_ip&0xFF,
            (g_gw>>24)&0xFF,(g_gw>>16)&0xFF,(g_gw>>8)&0xFF,g_gw&0xFF,
            (g_dns>>24)&0xFF,(g_dns>>16)&0xFF,(g_dns>>8)&0xFF,g_dns&0xFF);
}

/* ── API de sockets ─────────────────────────────────────── */
int sock_open(u8 type){
    for(int i=0;i<MAX_SOCKS;i++){
        if(socks[i].type==SOCK_NONE){
            socks[i].type=type;
            socks[i].state=TCP_CLOSED;
            socks[i].connected=0;
            socks[i].rx_head=socks[i].rx_tail=0;
            socks[i].local_port=(u16)(32768+(u16)(timer_ticks&0x7FFF));
            return i;
        }
    }
    return -1;
}

int sock_connect(int fd,ip4_t ip,u16 port){
    if(fd<0||fd>=MAX_SOCKS||socks[fd].type==SOCK_NONE) return -1;
    sock_t* s=&socks[fd];
    s->remote_ip=ip; s->remote_port=port;
    s->tx_seq=timer_ticks*13337;
    if(s->type==SOCK_TCP){
        s->state=TCP_SYN_SENT;
        tcp_send_flags(s,TCP_SYN,0,0);
        u32 dl=timer_ticks+300;
        while(timer_ticks<dl&&!s->connected) net_poll();
        if(!s->connected){ s->state=TCP_CLOSED; return -1; }
        return 0;
    }
    s->connected=1;
    return 0;
}

int sock_send(int fd,const u8* data,u32 len){
    if(fd<0||fd>=MAX_SOCKS||!socks[fd].connected) return -1;
    sock_t* s=&socks[fd];
    if(s->type==SOCK_TCP){
        u32 sent=0;
        while(sent<len){
            u32 chunk=len-sent;
            if(chunk>1460) chunk=1460;
            tcp_send_flags(s,TCP_PSH|TCP_ACK,data+sent,(u16)chunk);
            sent+=chunk;
            u32 dl=timer_ticks+10;
            while(timer_ticks<dl) net_poll();
        }
        return (int)len;
    }
    /* UDP */
    u8 seg[sizeof(udp_hdr_t)+len];
    udp_hdr_t* uh=(udp_hdr_t*)seg;
    uh->src_port=htons(s->local_port);
    uh->dst_port=htons(s->remote_port);
    uh->length=htons((u16)(sizeof(udp_hdr_t)+len));
    uh->checksum=0;
    for(u32 i=0;i<len;i++) seg[sizeof(udp_hdr_t)+i]=data[i];
    ip_send(s->remote_ip,17,seg,(u16)(sizeof(udp_hdr_t)+len));
    return (int)len;
}

int sock_recv(int fd,u8* buf,u32 max){
    if(fd<0||fd>=MAX_SOCKS) return -1;
    net_poll();
    sock_t* s=&socks[fd];
    u32 avail=rb_avail(s->rx_head,s->rx_tail,RXBUF);
    if(!avail) return 0;
    if(avail>max) avail=max;
    for(u32 i=0;i<avail;i++){
        buf[i]=s->rx_buf[s->rx_tail];
        s->rx_tail=(s->rx_tail+1)%RXBUF;
    }
    return (int)avail;
}

int sock_ready(int fd){
    if(fd<0||fd>=MAX_SOCKS) return 0;
    net_poll();
    return (int)rb_avail(socks[fd].rx_head,socks[fd].rx_tail,RXBUF);
}

int sock_close(int fd){
    if(fd<0||fd>=MAX_SOCKS) return -1;
    sock_t* s=&socks[fd];
    if(s->type==SOCK_TCP&&s->state==TCP_ESTABLISHED)
        tcp_send_flags(s,TCP_FIN|TCP_ACK,0,0);
    s->type=SOCK_NONE; s->connected=0;
    return 0;
}

/* ── DNS ────────────────────────────────────────────────── */
typedef struct __attribute__((packed)){u16 id,fl,qd,an,ns,ar;} dns_hdr_t;

int dns_resolve(const char* host, ip4_t* out){
    int fd=sock_open(SOCK_UDP);
    if(fd<0) return -1;
    if(sock_connect(fd,g_dns,53)<0){sock_close(fd);return -1;}

    u8 pkt[512]; u32 pos=sizeof(dns_hdr_t);
    dns_hdr_t* h=(dns_hdr_t*)pkt;
    h->id=htons(0xBEEF);h->fl=htons(0x0100);
    h->qd=htons(1);h->an=h->ns=h->ar=0;
    const char* p=host;
    while(*p){
        const char* dot=p;
        while(*dot&&*dot!='.') dot++;
        u8 ll=(u8)(dot-p);
        pkt[pos++]=ll;
        while(p<dot) pkt[pos++]=(u8)*p++;
        if(*p=='.') p++;
    }
    pkt[pos++]=0;
    pkt[pos++]=0;pkt[pos++]=1;  /* A */
    pkt[pos++]=0;pkt[pos++]=1;  /* IN */

    sock_send(fd,pkt,pos);
    u8 resp[512];
    u32 dl=timer_ticks+300;
    int rlen=0;
    while(timer_ticks<dl){
        rlen=sock_recv(fd,resp,sizeof(resp));
        if(rlen>0) break;
    }
    sock_close(fd);
    if(rlen<=0) return -1;

    dns_hdr_t* rh=(dns_hdr_t*)resp;
    if(ntohs(rh->an)==0) return -1;

    u32 off=sizeof(dns_hdr_t);
    /* pula questão */
    while(off<(u32)rlen&&resp[off]) off+=resp[off]+1;
    off+=5;
    /* primeira resposta */
    if(off+12>(u32)rlen) return -1;
    /* verifica se é ponteiro (0xC0) */
    if((resp[off]&0xC0)==0xC0) off+=2; else while(resp[off]) off+=resp[off]+1,off++;
    off+=4; /* type+class */
    off+=4; /* ttl */
    u16 rdlen=(u16)((resp[off]<<8)|resp[off+1]); off+=2;
    if(rdlen!=4||off+4>(u32)rlen) return -1;
    *out=MAKE_IP(resp[off],resp[off+1],resp[off+2],resp[off+3]);
    return 0;
}

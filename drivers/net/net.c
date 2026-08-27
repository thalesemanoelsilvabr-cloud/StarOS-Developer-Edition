/* ============================================================
 *  StarOS — Network Stack  (drivers/net/net.c)
 *  Pilha Ethernet/ARP/IP/TCP/UDP/DNS para x86 32-bit bare-metal
 *
 *  Depende de:
 *    drivers/net/ne2000.c   – enviar/receber frames raw
 *    kernel/kprintf.c       – kprintf()
 *    mm/kmalloc.c           – kmalloc()/kfree()
 *    arch/x86/isr.c         – timer_ticks (u32 volatile)
 * ============================================================ */
#include <net/net.h>
#include <drivers/terminal.h>   /* term_write para debug leve */
#include <kernel/types.h>
#include <mm/kmalloc.h>

/* ── Dependências externas (fornecidas pelo kernel) ─────────── */
extern void     ne2k_send(const u8 *frame, u16 len);
extern u16      ne2k_recv(u8 *buf, u16 max);  /* 0 = sem pkt  */
extern u32      timer_ticks;                  /* ~100 Hz       */
void kprintf(const char *fmt, ...);

/* ── Configuração da interface (padrão QEMU e2000) ─────────── */
mac_t  g_mac   = {0x52,0x54,0x00,0x12,0x34,0x56};
ip4_t  g_ip    = MAKE_IP(10,0,2,15);
ip4_t  g_gw    = MAKE_IP(10,0,2,2);
ip4_t  g_mask  = MAKE_IP(255,255,255,0);
ip4_t  g_dns   = MAKE_IP(8,8,8,8);

/* ── ARP cache ─────────────────────────────────────────────── */
#define ARP_CACHE_SIZE 16
#define ARP_TIMEOUT    3000   /* ~30 s a 100 Hz */
typedef struct { ip4_t ip; mac_t mac; u32 ts; u8 valid; } arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];

/* ── Socket table ──────────────────────────────────────────── */
static socket_t socks[MAX_SOCKETS];

/* ── Checksum IP/TCP/UDP ────────────────────────────────────── */
static u16 ip_checksum(const void *data, u32 len) {
    const u16 *p = (const u16*)data;
    u32 sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(u8*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

static u16 tcp_udp_checksum(ip4_t src, ip4_t dst, u8 proto,
                             const void *data, u16 len) {
    /* pseudo-header */
    u32 sum = 0;
    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += htons((u16)proto);
    sum += htons(len);
    const u16 *p = (const u16*)data;
    u32 l = len;
    while (l > 1) { sum += *p++; l -= 2; }
    if (l) sum += *(u8*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

/* ── Helpers de buffer frame ───────────────────────────────── */
#define FRAME_BUF 1536
static u8 tx_frame[FRAME_BUF];
static u8 rx_frame[FRAME_BUF];

static u8* eth_build(const mac_t dst, u16 ethertype) {
    eth_frame_t *e = (eth_frame_t*)tx_frame;
    for (int i=0;i<6;i++) e->dst[i] = dst[i];
    for (int i=0;i<6;i++) e->src[i] = g_mac[i];
    e->type = htons(ethertype);
    return e->payload;
}

/* ── ARP ───────────────────────────────────────────────────── */
static void arp_cache_put(ip4_t ip, const mac_t mac) {
    /* procura slot vazio ou LRU */
    int oldest = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid || arp_cache[i].ip == ip) { oldest = i; break; }
        if (arp_cache[i].ts < arp_cache[oldest].ts) oldest = i;
    }
    arp_cache[oldest].ip = ip;
    for(int i=0;i<6;i++) arp_cache[oldest].mac[i] = mac[i];
    arp_cache[oldest].ts = timer_ticks;
    arp_cache[oldest].valid = 1;
}

void arp_request(ip4_t ip) {
    mac_t bcast = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    u8 *p = eth_build(bcast, ETH_TYPE_ARP);
    arp_pkt_t *a = (arp_pkt_t*)p;
    a->htype = htons(ARP_HW_ETH);
    a->ptype = htons(ETH_TYPE_IP);
    a->hlen = 6; a->plen = 4;
    a->oper = htons(ARP_OP_REQ);
    for(int i=0;i<6;i++) a->sha[i] = g_mac[i];
    a->spa = htonl(g_ip);
    for(int i=0;i<6;i++) a->tha[i] = 0;
    a->tpa = htonl(ip);
    ne2k_send(tx_frame, sizeof(eth_frame_t) + sizeof(arp_pkt_t));
}

int arp_resolve(ip4_t ip, mac_t out) {
    /* local ou broadcast? */
    if (ip == IP_BROADCAST) {
        for(int i=0;i<6;i++) out[i]=0xFF;
        return 0;
    }
    /* fora da subnet → usa gateway */
    ip4_t target = ((ip & g_mask) == (g_ip & g_mask)) ? ip : g_gw;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == target) {
            for(int j=0;j<6;j++) out[j]=arp_cache[i].mac[j];
            return 0;
        }
    }
    /* envia ARP request e aguarda até 500 ms */
    arp_request(target);
    u32 deadline = timer_ticks + 50;
    while (timer_ticks < deadline) {
        net_poll();
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (arp_cache[i].valid && arp_cache[i].ip == target) {
                for(int j=0;j<6;j++) out[j]=arp_cache[i].mac[j];
                return 0;
            }
        }
    }
    return -1;  /* timeout */
}

static void arp_handle(arp_pkt_t *a) {
    ip4_t spa = ntohl(a->spa);
    arp_cache_put(spa, a->sha);
    if (ntohs(a->oper) == ARP_OP_REQ && ntohl(a->tpa) == g_ip) {
        /* responde */
        u8 *p = eth_build(a->sha, ETH_TYPE_ARP);
        arp_pkt_t *r = (arp_pkt_t*)p;
        r->htype = htons(ARP_HW_ETH);
        r->ptype = htons(ETH_TYPE_IP);
        r->hlen = 6; r->plen = 4;
        r->oper = htons(ARP_OP_REPL);
        for(int i=0;i<6;i++) r->sha[i]=g_mac[i];
        r->spa = htonl(g_ip);
        for(int i=0;i<6;i++) r->tha[i]=a->sha[i];
        r->tpa = a->spa;
        ne2k_send(tx_frame, sizeof(eth_frame_t)+sizeof(arp_pkt_t));
    }
}

/* ── IP send ───────────────────────────────────────────────── */
static u16 ip_id_counter = 1;

static int ip_send(ip4_t dst, u8 proto, const u8 *payload, u16 plen) {
    mac_t dst_mac;
    if (arp_resolve(dst, dst_mac) < 0) return -1;
    u8 *p = eth_build(dst_mac, ETH_TYPE_IP);
    ip_hdr_t *ih = (ip_hdr_t*)p;
    ih->ver_ihl  = 0x45;
    ih->tos      = 0;
    ih->total_len= htons(20 + plen);
    ih->id       = htons(ip_id_counter++);
    ih->flags_off= htons(0x4000); /* DF */
    ih->ttl      = 64;
    ih->proto    = proto;
    ih->checksum = 0;
    ih->src      = htonl(g_ip);
    ih->dst      = htonl(dst);
    /* copia payload após header */
    u8 *pay = p + sizeof(ip_hdr_t);
    for (u32 i=0;i<plen;i++) pay[i]=payload[i];
    ih->checksum = ip_checksum(ih, sizeof(ip_hdr_t));
    ne2k_send(tx_frame, sizeof(eth_frame_t)+20+plen);
    return 0;
}

/* ── Ring buffer helpers ────────────────────────────────────── */
static u32 rb_free(u32 h, u32 t, u32 cap) {
    return (h >= t) ? (cap - h + t - 1) : (t - h - 1);
}
static u32 rb_used(u32 h, u32 t, u32 cap) {
    return (h >= t) ? (h - t) : (cap - t + h);
}
static void rb_push(u8 *buf, u32 *h, u32 cap, const u8 *data, u32 len) {
    for (u32 i=0;i<len;i++) { buf[*h]=data[i]; *h=(*h+1)%cap; }
}
static u32 rb_pop(u8 *buf, u32 *t, u32 cap, u8 *out, u32 max) {
    u32 n = rb_used(*t==0?cap:*t, *t, cap); /* simplificado */
    (void)n;
    u32 cnt=0;
    while(cnt<max && buf[*t]!=0) { /* sentinela não confiável */
        out[cnt++]=buf[*t]; buf[*t]=0; *t=(*t+1)%cap;
    }
    return cnt;
}

/* ── TCP ───────────────────────────────────────────────────── */
static void tcp_send_flags(socket_t *s, u8 flags, const u8 *data, u16 dlen) {
    u8 seg[sizeof(tcp_hdr_t)+dlen];
    tcp_hdr_t *th = (tcp_hdr_t*)seg;
    th->src_port = htons(s->local_port);
    th->dst_port = htons(s->remote_port);
    th->seq      = htonl(s->tx_seq);
    th->ack_num  = (flags & TCP_ACK) ? htonl(s->rx_seq) : 0;
    th->data_off = 0x50;   /* 5×4 = 20 bytes, sem opções */
    th->flags    = flags;
    th->window   = htons(4096);
    th->checksum = 0;
    th->urgent   = 0;
    for(u32 i=0;i<dlen;i++) seg[sizeof(tcp_hdr_t)+i]=data[i];
    th->checksum = tcp_udp_checksum(g_ip, s->remote_ip, IP_PROTO_TCP,
                                    seg, sizeof(tcp_hdr_t)+dlen);
    ip_send(s->remote_ip, IP_PROTO_TCP, seg, sizeof(tcp_hdr_t)+dlen);
    if (flags & (TCP_SYN|TCP_FIN)) s->tx_seq++;
    s->tx_seq += dlen;
}

static void tcp_handle(ip_hdr_t *ih, tcp_hdr_t *th, u8 *data, u16 dlen) {
    u16 dport = ntohs(th->dst_port);
    u16 sport = ntohs(th->src_port);
    ip4_t src = ntohl(ih->src);
    for (int i=0;i<MAX_SOCKETS;i++) {
        socket_t *s = &socks[i];
        if (s->type!=SOCK_TCP) continue;
        if (s->local_port!=dport) continue;
        if (s->remote_ip && s->remote_ip!=src) continue;
        /* FSM simplificada */
        if ((th->flags & TCP_SYN) && (th->flags & TCP_ACK)
             && s->tcp_state == TCP_SYN_SENT) {
            s->rx_seq = ntohl(th->seq)+1;
            s->tx_seq = ntohl(th->ack_num);
            s->tcp_state = TCP_ESTABLISHED;
            s->connected = 1;
            s->remote_port = sport;
            tcp_send_flags(s, TCP_ACK, 0, 0);
            return;
        }
        if ((th->flags & TCP_ACK) && s->tcp_state == TCP_ESTABLISHED) {
            s->tx_seq = ntohl(th->ack_num);
        }
        if (dlen > 0 && s->tcp_state == TCP_ESTABLISHED) {
            s->rx_seq = ntohl(th->seq) + dlen;
            u32 free = rb_free(s->rx_head, s->rx_tail, SOCK_RXBUF);
            if (dlen <= free) {
                rb_push(s->rx_buf, &s->rx_head, SOCK_RXBUF, data, dlen);
            }
            tcp_send_flags(s, TCP_ACK, 0, 0);
        }
        if (th->flags & TCP_FIN) {
            s->rx_seq++;
            tcp_send_flags(s, TCP_ACK, 0, 0);
            s->tcp_state = TCP_CLOSE_WAIT;
        }
        return;
    }
}

/* ── UDP ───────────────────────────────────────────────────── */
static void udp_handle(ip_hdr_t *ih, udp_hdr_t *uh, u8 *data, u16 dlen) {
    u16 dport = ntohs(uh->dst_port);
    ip4_t src = ntohl(ih->src);
    for (int i=0;i<MAX_SOCKETS;i++) {
        socket_t *s = &socks[i];
        if (s->type!=SOCK_UDP) continue;
        if (s->local_port!=dport) continue;
        (void)src;
        u32 free = rb_free(s->rx_head, s->rx_tail, SOCK_RXBUF);
        if (dlen <= free)
            rb_push(s->rx_buf, &s->rx_head, SOCK_RXBUF, data, dlen);
        return;
    }
}

/* ── Processamento de frame recebido ────────────────────────── */
static void net_process_frame(u8 *frame, u16 len) {
    if (len < (u16)sizeof(eth_frame_t)) return;
    eth_frame_t *e = (eth_frame_t*)frame;
    u16 etype = ntohs(e->type);
    if (etype == ETH_TYPE_ARP) {
        if (len >= sizeof(eth_frame_t)+sizeof(arp_pkt_t))
            arp_handle((arp_pkt_t*)e->payload);
        return;
    }
    if (etype != ETH_TYPE_IP) return;
    ip_hdr_t *ih = (ip_hdr_t*)e->payload;
    if ((ih->ver_ihl >> 4) != 4) return;
    u16 ihl = (ih->ver_ihl & 0xF) * 4;
    u16 total = ntohs(ih->total_len);
    u8 *proto_data = (u8*)ih + ihl;
    u16 proto_len  = total - ihl;
    if (ih->proto == IP_PROTO_TCP && proto_len >= sizeof(tcp_hdr_t)) {
        tcp_hdr_t *th = (tcp_hdr_t*)proto_data;
        u16 thdr = (th->data_off >> 4) * 4;
        tcp_handle(ih, th, proto_data+thdr, proto_len-thdr);
    } else if (ih->proto == IP_PROTO_UDP && proto_len >= sizeof(udp_hdr_t)) {
        udp_hdr_t *uh = (udp_hdr_t*)proto_data;
        udp_handle(ih, uh, proto_data+sizeof(udp_hdr_t),
                   ntohs(uh->length)-sizeof(udp_hdr_t));
    }
}

/* ── API pública ────────────────────────────────────────────── */
void net_init(void) {
    for (int i=0;i<MAX_SOCKETS;i++) socks[i].type = SOCK_NONE;
    for (int i=0;i<ARP_CACHE_SIZE;i++) arp_cache[i].valid = 0;
    kprintf("[net] Pilha inicializada  IP=%d.%d.%d.%d\n",
            (g_ip>>24)&0xFF,(g_ip>>16)&0xFF,(g_ip>>8)&0xFF,g_ip&0xFF);
}

void net_poll(void) {
    u16 len;
    while ((len = ne2k_recv(rx_frame, FRAME_BUF)) > 0)
        net_process_frame(rx_frame, len);
}

int sock_open(u8 type) {
    for (int i=0;i<MAX_SOCKETS;i++) {
        if (socks[i].type == SOCK_NONE) {
            socks[i].type = type;
            socks[i].tcp_state = TCP_CLOSED;
            socks[i].connected = 0;
            socks[i].rx_head = socks[i].rx_tail = 0;
            socks[i].tx_head = socks[i].tx_tail = 0;
            socks[i].local_port = (u16)(32768 + i*100 + (timer_ticks & 0xFF));
            return i;
        }
    }
    return -1;
}

int sock_connect(int fd, ip4_t ip, u16 port) {
    if (fd<0||fd>=MAX_SOCKETS||socks[fd].type==SOCK_NONE) return -1;
    socket_t *s = &socks[fd];
    s->remote_ip   = ip;
    s->remote_port = port;
    s->tx_seq      = timer_ticks * 13337;
    if (s->type == SOCK_TCP) {
        s->tcp_state = TCP_SYN_SENT;
        tcp_send_flags(s, TCP_SYN, 0, 0);
        /* aguarda SYNACK até 3 s */
        u32 dl = timer_ticks + 300;
        while (timer_ticks < dl) {
            net_poll();
            if (s->connected) return 0;
        }
        s->tcp_state = TCP_CLOSED;
        return -1;
    }
    s->connected = 1;
    return 0;
}

int sock_send(int fd, const u8 *data, u32 len) {
    if (fd<0||fd>=MAX_SOCKETS||!socks[fd].connected) return -1;
    socket_t *s = &socks[fd];
    if (s->type == SOCK_TCP) {
        /* fragmenta em chunks de 1460 */
        u32 sent = 0;
        while (sent < len) {
            u32 chunk = len - sent;
            if (chunk > 1460) chunk = 1460;
            tcp_send_flags(s, TCP_PSH|TCP_ACK, data+sent, (u16)chunk);
            sent += chunk;
            /* polling para ACKs */
            u32 dl = timer_ticks + 20;
            while (timer_ticks < dl) net_poll();
        }
        return (int)len;
    } else { /* UDP */
        u8 seg[sizeof(udp_hdr_t)+len];
        udp_hdr_t *uh = (udp_hdr_t*)seg;
        uh->src_port = htons(s->local_port);
        uh->dst_port = htons(s->remote_port);
        uh->length   = htons(sizeof(udp_hdr_t)+len);
        uh->checksum = 0;
        for(u32 i=0;i<len;i++) seg[sizeof(udp_hdr_t)+i]=data[i];
        ip_send(s->remote_ip, IP_PROTO_UDP, seg, sizeof(udp_hdr_t)+len);
        return (int)len;
    }
}

int sock_recv(int fd, u8 *buf, u32 max_len) {
    if (fd<0||fd>=MAX_SOCKETS) return -1;
    socket_t *s = &socks[fd];
    net_poll();
    u32 avail = rb_used(s->rx_head, s->rx_tail, SOCK_RXBUF);
    if (!avail) return 0;
    if (avail > max_len) avail = max_len;
    for (u32 i=0;i<avail;i++) {
        buf[i] = s->rx_buf[s->rx_tail];
        s->rx_tail = (s->rx_tail+1) % SOCK_RXBUF;
    }
    return (int)avail;
}

int sock_ready(int fd) {
    if (fd<0||fd>=MAX_SOCKETS) return 0;
    net_poll();
    return (int)rb_used(socks[fd].rx_head, socks[fd].rx_tail, SOCK_RXBUF);
}

int sock_close(int fd) {
    if (fd<0||fd>=MAX_SOCKETS) return -1;
    socket_t *s = &socks[fd];
    if (s->type==SOCK_TCP && s->tcp_state==TCP_ESTABLISHED)
        tcp_send_flags(s, TCP_FIN|TCP_ACK, 0, 0);
    s->type = SOCK_NONE;
    s->connected = 0;
    return 0;
}

/* ── DNS simples (UDP/53) ───────────────────────────────────── */
#define DNS_PORT 53
typedef struct __attribute__((packed)) {
    u16 id,flags,qdcount,ancount,nscount,arcount;
} dns_hdr_t;

int dns_resolve(const char *hostname, ip4_t *out) {
    int fd = sock_open(SOCK_UDP);
    if (fd<0) return -1;
    if (sock_connect(fd, g_dns, DNS_PORT)<0) { sock_close(fd); return -1; }

    /* monta query */
    u8 pkt[512];
    u32 pos = sizeof(dns_hdr_t);
    dns_hdr_t *h = (dns_hdr_t*)pkt;
    h->id = htons(0xBEEF);
    h->flags = htons(0x0100);  /* RD=1 */
    h->qdcount = htons(1);
    h->ancount = h->nscount = h->arcount = 0;

    /* codifica hostname em labels */
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot!='.') dot++;
        u8 llen = (u8)(dot-p);
        pkt[pos++] = llen;
        while (p<dot) pkt[pos++]=(u8)*p++;
        if (*p=='.') p++;
    }
    pkt[pos++]=0;     /* root */
    pkt[pos++]=0; pkt[pos++]=1;  /* QTYPE A */
    pkt[pos++]=0; pkt[pos++]=1;  /* QCLASS IN */

    sock_send(fd, pkt, pos);

    /* aguarda resposta */
    u8 resp[512];
    u32 dl = timer_ticks + 300;
    int rlen = 0;
    while (timer_ticks < dl) {
        rlen = sock_recv(fd, resp, sizeof(resp));
        if (rlen > 0) break;
    }
    sock_close(fd);
    if (rlen <= 0) return -1;

    dns_hdr_t *rh = (dns_hdr_t*)resp;
    if (ntohs(rh->ancount) == 0) return -1;

    /* pula a seção de pergunta */
    u32 off = sizeof(dns_hdr_t);
    while (off < (u32)rlen && resp[off]) {
        off += resp[off] + 1;
    }
    off += 5; /* root + type + class */

    /* primeira resposta */
    if (off + 12 > (u32)rlen) return -1;
    off += 10; /* name ptr(2)+type(2)+class(2)+ttl(4) */
    u16 rdlen = (u16)((resp[off]<<8)|resp[off+1]); off+=2;
    if (rdlen != 4 || off+4 > (u32)rlen) return -1;
    *out = MAKE_IP(resp[off], resp[off+1], resp[off+2], resp[off+3]);
    return 0;
}

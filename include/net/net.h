/* ============================================================
 *  StarOS — Network Stack  (include/net/net.h)
 *  Camada de rede leve: Ethernet/ARP/IP/TCP/UDP/HTTP
 *  Alvo: x86 32-bit bare-metal, sem libc
 * ============================================================ */
#ifndef STAROS_NET_H
#define STAROS_NET_H

#include <kernel/types.h>

/* ── Tipos básicos ─────────────────────────────────────────── */
typedef u8  mac_t[6];
typedef u32 ip4_t;           /* big-endian em memória         */

/* ── Helpers endian ────────────────────────────────────────── */
static inline u16 htons(u16 v){ return (u16)((v>>8)|(v<<8)); }
static inline u16 ntohs(u16 v){ return htons(v); }
static inline u32 htonl(u32 v){
    return ((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|((v>>24)&0xFF);
}
static inline u32 ntohl(u32 v){ return htonl(v); }

/* ── IP helpers ────────────────────────────────────────────── */
#define MAKE_IP(a,b,c,d) ((ip4_t)((a)<<24|(b)<<16|(c)<<8|(d)))
#define IP_BROADCAST     0xFFFFFFFFu

/* ── Ethernet ──────────────────────────────────────────────── */
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_MTU       1500

typedef struct __attribute__((packed)) {
    mac_t  dst;
    mac_t  src;
    u16    type;
    u8     payload[];
} eth_frame_t;

/* ── ARP ───────────────────────────────────────────────────── */
#define ARP_HW_ETH  1
#define ARP_OP_REQ  1
#define ARP_OP_REPL 2

typedef struct __attribute__((packed)) {
    u16 htype, ptype;
    u8  hlen, plen;
    u16 oper;
    mac_t  sha; ip4_t spa;
    mac_t  tha; ip4_t tpa;
} arp_pkt_t;

/* ── IP ────────────────────────────────────────────────────── */
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct __attribute__((packed)) {
    u8  ver_ihl, tos;
    u16 total_len, id, flags_off;
    u8  ttl, proto;
    u16 checksum;
    ip4_t src, dst;
} ip_hdr_t;

/* ── TCP ───────────────────────────────────────────────────── */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef struct __attribute__((packed)) {
    u16 src_port, dst_port;
    u32 seq, ack_num;
    u8  data_off, flags;
    u16 window, checksum, urgent;
} tcp_hdr_t;

/* ── UDP ───────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    u16 src_port, dst_port, length, checksum;
} udp_hdr_t;

/* ── Sockets simples ───────────────────────────────────────── */
#define SOCK_TCP   0
#define SOCK_UDP   1
#define SOCK_NONE  0xFF

#define MAX_SOCKETS  8
#define SOCK_RXBUF   4096
#define SOCK_TXBUF   4096

typedef enum {
    TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED,
    TCP_FIN_WAIT, TCP_CLOSE_WAIT, TCP_LAST_ACK
} tcp_state_t;

typedef struct {
    u8         type;          /* SOCK_TCP / SOCK_UDP / SOCK_NONE */
    ip4_t      remote_ip;
    u16        remote_port;
    u16        local_port;
    tcp_state_t tcp_state;
    u32        tx_seq, rx_seq;
    /* ring buffers */
    u8         rx_buf[SOCK_RXBUF];
    u32        rx_head, rx_tail;
    u8         tx_buf[SOCK_TXBUF];
    u32        tx_head, tx_tail;
    u8         connected;
} socket_t;

/* ── API de rede ───────────────────────────────────────────── */
void   net_init(void);           /* inicializa toda a pilha      */
void   net_poll(void);           /* processa pacotes pendentes   */

/* ARP */
void   arp_request(ip4_t ip);
int    arp_resolve(ip4_t ip, mac_t out_mac);  /* 0 = ok         */

/* Sockets */
int    sock_open(u8 type);
int    sock_connect(int fd, ip4_t ip, u16 port);
int    sock_send(int fd, const u8 *data, u32 len);
int    sock_recv(int fd, u8 *buf, u32 max_len);
int    sock_close(int fd);
int    sock_ready(int fd);       /* bytes disponíveis para ler   */

/* DNS simples */
int    dns_resolve(const char *hostname, ip4_t *out);

/* Configuração */
extern mac_t  g_mac;
extern ip4_t  g_ip, g_gw, g_mask, g_dns;

#endif /* STAROS_NET_H */

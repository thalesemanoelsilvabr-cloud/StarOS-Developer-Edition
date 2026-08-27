/* ne2000.c — Driver NE2000/RTL8029 PCI */
#include <kernel/types.h>

extern void kprintf(const char*,...);
static inline u8  inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline u16 inw(u16 p){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outw(u16 p,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}

/* IO base padrao QEMU ne2k_pci */
#define NE2K_BASE  0x300
#define NE2K_DATA  (NE2K_BASE+0x10)
#define NE2K_RESET (NE2K_BASE+0x1F)

/* Registradores NE2000 */
#define NE_CMD   0x00
#define NE_PSTART 0x01
#define NE_PSTOP  0x02
#define NE_BNRY  0x03
#define NE_TPSR  0x04
#define NE_TBCR0 0x05
#define NE_TBCR1 0x06
#define NE_ISR   0x07
#define NE_RSAR0 0x08
#define NE_RSAR1 0x09
#define NE_RBCR0 0x0A
#define NE_RBCR1 0x0B
#define NE_RCR   0x0C
#define NE_TCR   0x0D
#define NE_DCR   0x0E
#define NE_IMR   0x0F

/* Page1 */
#define NE_PAR0  0x01  /* MAC bytes */
#define NE_CURR  0x07
#define NE_MAR0  0x08

#define NE_TX_BUF  0x40
#define NE_RX_START 0x46
#define NE_RX_STOP  0x80
#define ETH_FRAME_MAX 1536

static u8  ne2k_mac[6];
static u8  ne2k_next_page = 0;
static int ne2k_ready = 0;

/* PCI detect simples: varre bus 0 */
static u32 pci_read(u8 bus,u8 dev,u8 fn,u8 off){
    u32 addr=0x80000000|(u32)bus<<16|(u32)dev<<11|(u32)fn<<8|(off&0xFC);
    outb(0xCF8,(u8)(addr));      outb(0xCF9,(u8)(addr>>8));
    outb(0xCFA,(u8)(addr>>16));  outb(0xCFB,(u8)(addr>>24));
    /* usa I/O 32-bit */
    __asm__ volatile("outl %0,%1"::"a"(addr),"Nd"((u16)0xCF8));
    u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"((u16)0xCFC));
    return v;
}

static u16 ne2k_base = NE2K_BASE;

static void ne2k_write_mac(void){
    /* Page 1 */
    outb(ne2k_base+NE_CMD,0x61);
    for(int i=0;i<6;i++) outb(ne2k_base+NE_PAR0+i, ne2k_mac[i]);
    /* zera MAR (multicast) */
    for(int i=0;i<8;i++) outb(ne2k_base+NE_MAR0+i,0xFF);
    /* volta para page 0 */
    outb(ne2k_base+NE_CMD,0x21);
}

static void ne2k_read_mac(void){
    /* le MAC da PROM via DMA remoto */
    outb(ne2k_base+NE_CMD,0x21);      /* stop, page0 */
    outb(ne2k_base+NE_RBCR0,12);
    outb(ne2k_base+NE_RBCR1,0);
    outb(ne2k_base+NE_RSAR0,0);
    outb(ne2k_base+NE_RSAR1,0);
    outb(ne2k_base+NE_CMD,0x0A);      /* remote read DMA */
    for(int i=0;i<6;i++){
        ne2k_mac[i]=inb(NE2K_DATA);
        inb(NE2K_DATA);  /* prom retorna byte duplicado */
    }
}

void ne2k_init(void){
    /* reset */
    u8 r=inb(NE2K_RESET);
    outb(NE2K_RESET,r);
    /* aguarda */
    for(int i=0;i<1000;i++) inb(ne2k_base+NE_ISR);
    /* configuracao basica */
    outb(ne2k_base+NE_CMD,0x21);          /* stop + page0 */
    outb(ne2k_base+NE_DCR,0x49);          /* word, fifo 8 */
    outb(ne2k_base+NE_RBCR0,0);
    outb(ne2k_base+NE_RBCR1,0);
    outb(ne2k_base+NE_RCR,0x04);          /* recebe broadcast */
    outb(ne2k_base+NE_TCR,0x02);          /* modo loopback */
    outb(ne2k_base+NE_PSTART,NE_RX_START);
    outb(ne2k_base+NE_PSTOP,NE_RX_STOP);
    outb(ne2k_base+NE_BNRY,NE_RX_START);
    outb(ne2k_base+NE_TPSR,NE_TX_BUF);
    outb(ne2k_base+NE_ISR,0xFF);           /* limpa flags */
    outb(ne2k_base+NE_IMR,0x1F);           /* habilita interrupcoes */
    ne2k_read_mac();
    ne2k_write_mac();
    /* page1: seta CURR */
    outb(ne2k_base+NE_CMD,0x61);
    outb(ne2k_base+NE_CURR,NE_RX_START+1);
    ne2k_next_page = NE_RX_START+1;
    /* volta page0, inicia */
    outb(ne2k_base+NE_CMD,0x22);
    outb(ne2k_base+NE_TCR,0x00);           /* normal */
    ne2k_ready=1;
    kprintf("[ne2k] MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
            ne2k_mac[0],ne2k_mac[1],ne2k_mac[2],
            ne2k_mac[3],ne2k_mac[4],ne2k_mac[5]);
    /* exporta MAC para a pilha de rede */
    extern u8 g_mac[6];
    for(int i=0;i<6;i++) g_mac[i]=ne2k_mac[i];
}

void ne2k_send(const u8* frame, u16 len){
    if(!ne2k_ready) return;
    if(len<60) len=60;  /* padding minimo */
    /* DMA write para pagina de TX */
    outb(ne2k_base+NE_CMD,0x21);
    outb(ne2k_base+NE_RBCR0,(u8)(len&0xFF));
    outb(ne2k_base+NE_RBCR1,(u8)(len>>8));
    outb(ne2k_base+NE_RSAR0,0);
    outb(ne2k_base+NE_RSAR1,NE_TX_BUF);
    outb(ne2k_base+NE_CMD,0x12);  /* remote write DMA */
    for(u16 i=0;i<len;i+=2){
        u16 w=(u16)frame[i]|((i+1<len)?((u16)frame[i+1]<<8):(u16)0u);
        outw(NE2K_DATA,w);
    }
    /* dispara TX */
    outb(ne2k_base+NE_TBCR0,(u8)(len&0xFF));
    outb(ne2k_base+NE_TBCR1,(u8)(len>>8));
    outb(ne2k_base+NE_CMD,0x26);  /* start + TX */
}

u16 ne2k_recv(u8* buf, u16 max){
    if(!ne2k_ready) return 0;
    /* le CURR (page1) */
    outb(ne2k_base+NE_CMD,0x62);
    u8 curr=inb(ne2k_base+NE_CURR);
    outb(ne2k_base+NE_CMD,0x22);
    u8 bnry=(u8)(inb(ne2k_base+NE_BNRY)+1);
    if(bnry>=NE_RX_STOP) bnry=NE_RX_START;
    if(bnry==curr) return 0;
    /* le header do pacote (4 bytes) */
    u16 hdr_addr=(u16)bnry<<8;
    outb(ne2k_base+NE_RBCR0,4);
    outb(ne2k_base+NE_RBCR1,0);
    outb(ne2k_base+NE_RSAR0,(u8)(hdr_addr&0xFF));
    outb(ne2k_base+NE_RSAR1,(u8)(hdr_addr>>8));
    outb(ne2k_base+NE_CMD,0x0A);
    u8 status=inb(NE2K_DATA); (void)status;
    u8 nextpg=inb(NE2K_DATA);
    u16 plen=(u16)inw(NE2K_DATA);
    if(plen<4||plen>ETH_FRAME_MAX){ outb(ne2k_base+NE_BNRY,nextpg-1); return 0; }
    u16 datalen=plen-4;
    if(datalen>max) datalen=(u16)max;
    /* le dados */
    outb(ne2k_base+NE_RBCR0,(u8)(datalen&0xFF));
    outb(ne2k_base+NE_RBCR1,(u8)(datalen>>8));
    u16 daddr=(u16)(hdr_addr+4);
    outb(ne2k_base+NE_RSAR0,(u8)(daddr&0xFF));
    outb(ne2k_base+NE_RSAR1,(u8)(daddr>>8));
    outb(ne2k_base+NE_CMD,0x0A);
    for(u16 i=0;i<datalen;i+=2){
        u16 w=inw(NE2K_DATA);
        buf[i]=(u8)(w&0xFF);
        if(i+1<datalen) buf[i+1]=(u8)(w>>8);
    }
    outb(ne2k_base+NE_BNRY,nextpg-1);
    return datalen;
}

void ne2k_irq_handler(void){
    if(!ne2k_ready) return;
    u8 isr=inb(ne2k_base+NE_ISR);
    outb(ne2k_base+NE_ISR,isr);  /* limpa flags */
}

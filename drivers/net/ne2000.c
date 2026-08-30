/* ne2000.c — Driver NE2000/RTL8029 PCI real para QEMU
 *
 * O QEMU expõe ne2k_pci com:
 *   Vendor 0x10EC  Device 0x8029  (Realtek RTL8029)
 *   Class  0x0200  (Ethernet)
 *   BAR0   = IO base (tipicamente 0xC100 no QEMU)
 *
 * Este driver:
 *  1. Varre o barramento PCI para encontrar o dispositivo
 *  2. Lê o IO base do BAR0
 *  3. Inicializa o chip NE2000
 *  4. Expõe ne2k_send() e ne2k_recv()
 */
#include <kernel/types.h>

extern void kprintf(const char*,...);

/* ── I/O helpers ─────────────────────────────────────────── */
static inline void outb(u16 p,u8 v) {__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void outw(u16 p,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline u8   inb(u16 p){u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16  inw(u16 p){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u32  inl(u16 p){u32 v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outl(u16 p,u32 v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}

/* ── PCI ─────────────────────────────────────────────────── */
static u32 pci_read(u8 bus,u8 dev,u8 fn,u8 off){
    u32 addr = 0x80000000u
              |((u32)bus<<16)|((u32)dev<<11)
              |((u32)fn<<8) |(off&0xFC);
    outl(0xCF8,addr);
    return inl(0xCFC);
}
static void pci_write(u8 bus,u8 dev,u8 fn,u8 off,u32 v){
    u32 addr = 0x80000000u
              |((u32)bus<<16)|((u32)dev<<11)
              |((u32)fn<<8) |(off&0xFC);
    outl(0xCF8,addr);
    outl(0xCFC,v);
}

/* ── Registradores NE2000 (offset do IO base) ─────────────── */
/* Page 0 */
#define NE_CMD    0x00  /* command */
#define NE_PSTART 0x01  /* page start */
#define NE_PSTOP  0x02  /* page stop */
#define NE_BNRY   0x03  /* boundary */
#define NE_TPSR   0x04  /* tx page start */
#define NE_TBCR0  0x05  /* tx byte count 0 */
#define NE_TBCR1  0x06  /* tx byte count 1 */
#define NE_ISR    0x07  /* interrupt status */
#define NE_RSAR0  0x08  /* remote DMA start addr 0 */
#define NE_RSAR1  0x09  /* remote DMA start addr 1 */
#define NE_RBCR0  0x0A  /* remote byte count 0 */
#define NE_RBCR1  0x0B  /* remote byte count 1 */
#define NE_RCR    0x0C  /* rx config */
#define NE_TCR    0x0D  /* tx config */
#define NE_DCR    0x0E  /* data config */
#define NE_IMR    0x0F  /* interrupt mask */
#define NE_DATA   0x10  /* data port (NE2000 específico) */
#define NE_RESET  0x1F  /* reset port */
/* Page 1 */
#define NE_PAR0   0x01  /* physical address 0-5 */
#define NE_CURR   0x07  /* current page */
#define NE_MAR0   0x08  /* multicast address */

/* Páginas de memória do chip (em blocos de 256 bytes) */
#define NE_TX_START  0x40   /* TX buffer: páginas 0x40-0x45 */
#define NE_RX_START  0x46   /* RX buffer: páginas 0x46-0x7F */
#define NE_RX_STOP   0x80

/* ── Estado ───────────────────────────────────────────────── */
static u16 ne_base  = 0;       /* IO base detectado via PCI */
static u8  ne_mac[6]= {0};
static u8  ne_next  = NE_RX_START + 1;
static int ne_ready = 0;

/* exporta MAC para a pilha de rede */
u8 g_mac[6];

/* ── Wrappers com IO base ────────────────────────────────── */
static inline void NE_OUT(u8 reg,u8 val){ outb((u16)(ne_base+reg),val); }
static inline u8   NE_IN (u8 reg)       { return inb((u16)(ne_base+reg)); }
static inline u16  NE_INW(void)         { return inw((u16)(ne_base+NE_DATA)); }
static inline void NE_OUTW(u16 v)       { outw((u16)(ne_base+NE_DATA),v); }

/* ── DMA remoto: lê N bytes do chip para buf ─────────────── */
static void ne_read_mem(u16 src, u8* buf, u16 len){
    /* arredonda para par */
    u16 rlen = (len+1)&~1;
    NE_OUT(NE_CMD,  0x22);        /* page0 + start */
    NE_OUT(NE_RBCR0,(u8)(rlen&0xFF));
    NE_OUT(NE_RBCR1,(u8)(rlen>>8));
    NE_OUT(NE_RSAR0,(u8)(src&0xFF));
    NE_OUT(NE_RSAR1,(u8)(src>>8));
    NE_OUT(NE_CMD,  0x0A);        /* remote read DMA */
    for(u16 i=0;i<len;i+=2){
        u16 w = NE_INW();
        buf[i]   = (u8)(w&0xFF);
        if(i+1<len) buf[i+1]=(u8)(w>>8);
    }
    /* aguarda DMA terminar */
    u32 t=10000;
    while(t-- && !(NE_IN(NE_ISR)&0x40));
    NE_OUT(NE_ISR,0x40);
}

/* ── DMA remoto: escreve N bytes do buf para o chip ─────── */
static void ne_write_mem(u16 dst, const u8* buf, u16 len){
    u16 wlen=(len+1)&~1;
    NE_OUT(NE_CMD,  0x22);
    NE_OUT(NE_RBCR0,(u8)(wlen&0xFF));
    NE_OUT(NE_RBCR1,(u8)(wlen>>8));
    NE_OUT(NE_RSAR0,(u8)(dst&0xFF));
    NE_OUT(NE_RSAR1,(u8)(dst>>8));
    NE_OUT(NE_CMD,  0x12);        /* remote write DMA */
    for(u16 i=0;i<len;i+=2){
        u16 w=(u16)buf[i];
        if(i+1<len) w|=(u16)buf[i+1]<<8;
        NE_OUTW(w);
    }
    u32 t=10000;
    while(t-- && !(NE_IN(NE_ISR)&0x40));
    NE_OUT(NE_ISR,0x40);
}

/* ── Lê MAC da PROM ─────────────────────────────────────── */
static void ne_read_mac(void){
    u8 prom[32];
    /* PROM fica nos primeiros 32 bytes da memória do chip */
    NE_OUT(NE_CMD,  0x21);
    NE_OUT(NE_DCR,  0x49);  /* word, FIFO 8 */
    NE_OUT(NE_RBCR0,32);
    NE_OUT(NE_RBCR1,0);
    NE_OUT(NE_RSAR0,0);
    NE_OUT(NE_RSAR1,0);
    NE_OUT(NE_CMD,  0x0A);  /* remote read */
    for(int i=0;i<32;i++) prom[i]=(u8)NE_INW();
    /* MAC está nos bytes pares da PROM */
    for(int i=0;i<6;i++) ne_mac[i]=prom[i*2];
}

/* ── Detecta NE2000 via PCI ─────────────────────────────── */
static int pci_find_ne2000(void){
    for(u8 bus=0;bus<8;bus++){
        for(u8 dev=0;dev<32;dev++){
            u32 id  = pci_read(bus,dev,0,0x00);
            if(id==0xFFFFFFFF) continue;
            u32 cls = pci_read(bus,dev,0,0x08)>>8;
            /* RTL8029: 0x10EC:0x8029  ou  NE2000 genérico: 0x????:0x0029
               Classe 0x0200 = Ethernet */
            u16 vendor=(u16)(id&0xFFFF);
            u16 device=(u16)(id>>16);
            u8  baseclass=(u8)(cls>>16);
            if(baseclass==0x02 && (
                (vendor==0x10EC && device==0x8029) ||  /* RTL8029 */
                (vendor==0x1050 && device==0x0940) ||  /* Winbond */
                (vendor==0x11F6 && device==0x1401) ||  /* Compex */
                (vendor==0x8E2E && device==0x3000) ||  /* KTI */
                (vendor==0x4A14 && device==0x5000) ||  /* NetVin */
                device==0x8029 || device==0x0029
            )){
                /* habilita IO + bus master */
                u32 cmd = pci_read(bus,dev,0,0x04);
                pci_write(bus,dev,0,0x04,cmd|0x05);
                /* lê BAR0 (IO base) */
                u32 bar0 = pci_read(bus,dev,0,0x10) & 0xFFFC;
                if(bar0 > 0x100){
                    ne_base=(u16)bar0;
                    kprintf("[ne2k] PCI %02X:%02X ID=%04X:%04X BAR0=%04X\n",
                            bus,dev,vendor,device,ne_base);
                    return 1;
                }
            }
        }
    }
    /* fallback: IO 0x300 (ISA legacy) */
    ne_base=0x300;
    kprintf("[ne2k] Nenhum PCI — tentando ISA 0x300\n");
    return 0;
}

/* ── Inicialização ──────────────────────────────────────── */
void ne2k_init(void){
    pci_find_ne2000();

    /* reset */
    u8 r=inb((u16)(ne_base+NE_RESET));
    outb((u16)(ne_base+NE_RESET),r);
    /* aguarda reset */
    u32 t=100000; while(t--);
    while(!(NE_IN(NE_ISR)&0x80));
    NE_OUT(NE_ISR,0xFF);

    /* configura chip */
    NE_OUT(NE_CMD,  0x21);         /* stop, page0 */
    NE_OUT(NE_DCR,  0x49);         /* word, FIFO 8, DMA */
    NE_OUT(NE_RBCR0,0x00);
    NE_OUT(NE_RBCR1,0x00);
    NE_OUT(NE_RCR,  0x20);         /* monitor mode temporário */
    NE_OUT(NE_TCR,  0x02);         /* modo loopback interno */
    NE_OUT(NE_TPSR, NE_TX_START);
    NE_OUT(NE_PSTART,NE_RX_START);
    NE_OUT(NE_PSTOP, NE_RX_STOP);
    NE_OUT(NE_BNRY,  NE_RX_START);
    NE_OUT(NE_IMR,  0x00);         /* sem interrupções por ora */
    NE_OUT(NE_ISR,  0xFF);

    /* lê MAC */
    ne_read_mac();

    /* page 1: grava PAR e CURR */
    NE_OUT(NE_CMD,0x61);
    for(int i=0;i<6;i++) NE_OUT((u8)(NE_PAR0+i),ne_mac[i]);
    for(int i=0;i<8;i++) NE_OUT((u8)(NE_MAR0+i),0xFF);
    NE_OUT(NE_CURR,NE_RX_START+1);
    ne_next=NE_RX_START+1;

    /* page 0: modo normal */
    NE_OUT(NE_CMD,  0x22);         /* start */
    NE_OUT(NE_TCR,  0x00);         /* modo normal */
    NE_OUT(NE_RCR,  0x0C);         /* aceita broadcast + multicast */
    NE_OUT(NE_IMR,  0x1F);         /* habilita IRQs */

    /* copia MAC para a pilha de rede */
    for(int i=0;i<6;i++) g_mac[i]=ne_mac[i];
    ne_ready=1;

    kprintf("[ne2k] MAC=%02X:%02X:%02X:%02X:%02X:%02X  pronto\n",
            ne_mac[0],ne_mac[1],ne_mac[2],
            ne_mac[3],ne_mac[4],ne_mac[5]);
}

/* ── Envio de frame ─────────────────────────────────────── */
void ne2k_send(const u8* frame, u16 len){
    if(!ne_ready||!len) return;
    if(len<60) len=60;

    /* aguarda TX livre */
    u32 t=100000;
    while(t-- && (NE_IN(NE_CMD)&0x04));

    ne_write_mem((u16)NE_TX_START<<8, frame, len);

    NE_OUT(NE_CMD,  0x22);
    NE_OUT(NE_TPSR, NE_TX_START);
    NE_OUT(NE_TBCR0,(u8)(len&0xFF));
    NE_OUT(NE_TBCR1,(u8)(len>>8));
    NE_OUT(NE_CMD,  0x26);         /* start TX */
}

/* ── Recepção de frame ──────────────────────────────────── */
u16 ne2k_recv(u8* buf, u16 max){
    if(!ne_ready) return 0;

    /* lê página current (page1) */
    NE_OUT(NE_CMD,0x62);
    u8 curr=NE_IN(NE_CURR);
    NE_OUT(NE_CMD,0x22);

    u8 bnry=(u8)(NE_IN(NE_BNRY)+1);
    if(bnry>=NE_RX_STOP) bnry=NE_RX_START;
    if(bnry==curr) return 0;

    /* lê header do pacote (4 bytes) */
    u8 hdr[4];
    ne_read_mem((u16)bnry<<8, hdr, 4);

    u8  status  = hdr[0];
    u8  next_pg = hdr[1];
    u16 pkt_len = (u16)hdr[2]|(u16)hdr[3]<<8;

    /* valida */
    if(!(status&0x01)||pkt_len<4||pkt_len>1536||
       next_pg<NE_RX_START||next_pg>=NE_RX_STOP){
        NE_OUT(NE_BNRY,(u8)(curr==NE_RX_START?NE_RX_STOP-1:curr-1));
        return 0;
    }

    u16 data_len=(u16)(pkt_len-4);
    if(data_len>max) data_len=max;

    /* lê dados — pode envolver wrap-around */
    u16 data_off=(u16)((bnry<<8)+4);
    u16 end_off =(u16)(data_off+data_len);

    if(end_off<=(u16)(NE_RX_STOP<<8)){
        ne_read_mem(data_off,buf,data_len);
    } else {
        /* wrap */
        u16 first=(u16)((NE_RX_STOP<<8)-data_off);
        ne_read_mem(data_off,buf,first);
        ne_read_mem((u16)(NE_RX_START<<8),buf+first,(u16)(data_len-first));
    }

    /* avança BNRY */
    u8 new_bnry=(u8)(next_pg-1);
    if(new_bnry<NE_RX_START) new_bnry=NE_RX_STOP-1;
    NE_OUT(NE_BNRY,new_bnry);

    return data_len;
}

/* ── IRQ handler ────────────────────────────────────────── */
void ne2k_irq_handler(void){
    if(!ne_ready) return;
    u8 isr=NE_IN(NE_ISR);
    NE_OUT(NE_ISR,isr);   /* limpa flags */
}

/* wifi.c — WiFi stub (detecção PCI + rede via ne2k fallback) */
#include <kernel/types.h>
#include <drivers/wifi.h>

extern void kprintf(const char*,...);
static inline u32 pci_read32(u8 bus,u8 dev,u8 fn,u8 off){
    u32 addr=0x80000000|(u32)bus<<16|(u32)dev<<11|(u32)fn<<8|(off&0xFC);
    __asm__ volatile("outl %0,%1"::"a"(addr),"Nd"((u16)0xCF8));
    u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"((u16)0xCFC));
    return v;
}

static int  wifi_found    = 0;
static int  wifi_conn     = 0;
static char wifi_cur_ssid[33] = {0};

/* IDs PCI de chipsets WiFi comuns */
static const u32 wifi_ids[] = {
    0x10268086, /* Intel WiFi */
    0x422B8086, /* Intel Centrino */
    0x008814E4, /* Broadcom BCM4311 */
    0x431114E4, /* Broadcom BCM4312 */
    0x002B168C, /* Atheros AR9285 */
    0x002C168C, /* Atheros AR9287 */
    0x7260168C, /* Qualcomm AR7260 */
    0xB72210EC, /* Realtek RTL8723 */
    0xC82110EC, /* Realtek RTL8821 */
    0
};

void wifi_init(void){
    /* varre barramento PCI procurando WiFi (class 0x0280 = Network/Other) */
    for(int bus=0;bus<4&&!wifi_found;bus++){
        for(int dev=0;dev<32&&!wifi_found;dev++){
            u32 id  = pci_read32((u8)bus,(u8)dev,0,0x00);
            u32 cls = pci_read32((u8)bus,(u8)dev,0,0x08)>>8;
            if(id==0xFFFFFFFF) continue;
            if((cls&0xFFFF00)==0x028000){
                wifi_found=1;
                kprintf("[wifi] Chipset detectado: %08X\n",id);
            }
            /* checa IDs conhecidos */
            for(int i=0;wifi_ids[i];i++){
                if(id==wifi_ids[i]){ wifi_found=1; break; }
            }
        }
    }
    if(!wifi_found) kprintf("[wifi] Nenhum adaptador WiFi encontrado\n");
}

int wifi_present(void)   { return wifi_found; }
int wifi_connected(void) { return wifi_conn; }
const char* wifi_ssid(void){ return wifi_cur_ssid[0]?wifi_cur_ssid:"--"; }

const char* wifi_icon(void){
    if(!wifi_found)  return "[NO WIFI]";
    if(!wifi_conn)   return "[~--]";
    return "[~~~]";
}

int wifi_scan(wifi_net_t* nets, int max){
    if(!wifi_found) return 0;
    /* stub: retorna rede simulada em QEMU */
    if(max<1) return 0;
    extern void kstrncpy(char*,const char*,u32);
    kstrncpy(nets[0].ssid,"StarOS-Net",33);
    nets[0].signal=-45;
    nets[0].encrypted=0;
    nets[0].connected=wifi_conn;
    return 1;
}

int wifi_connect(const char* ssid, const char* pass){
    (void)pass;
    if(!wifi_found) return -1;
    extern void kstrncpy(char*,const char*,u32);
    kstrncpy(wifi_cur_ssid,ssid,32);
    wifi_conn=1;
    kprintf("[wifi] Conectado a: %s\n",ssid);
    return 0;
}

void wifi_disconnect(void){
    wifi_conn=0;
    wifi_cur_ssid[0]=0;
}

/* acpi.c — ACPI/RTC/Bateria */
#include <kernel/types.h>
#include <drivers/acpi.h>

extern void kprintf(const char*,...);

static inline u8  inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void outw(u16 p,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}

/* ── RTC (Real Time Clock via BIOS CMOS) ──────────────────── */
static u8 cmos_read(u8 reg){
    outb(0x70, reg | 0x80);  /* 0x80 = NMI disable */
    u8 v = inb(0x71);
    outb(0x70, 0);
    return v;
}

static u8 bcd_to_bin(u8 v){ return (v>>4)*10 + (v&0xF); }

static int rtc_updating(void){
    outb(0x70,0x0A);
    return inb(0x71)&0x80;
}

void rtc_read(rtc_time_t* t){
    /* aguarda RTC estável */
    while(rtc_updating());

    u8 regB = cmos_read(0x0B);
    u8 is_bcd = !(regB & 0x04);
    u8 is_12h = !(regB & 0x02);

    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    u8 yr     = cmos_read(0x09);
    u8 cent   = cmos_read(0x32);  /* century register */

    if(is_bcd){
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        t->hour   = bcd_to_bin(t->hour & 0x7F);
        t->day    = bcd_to_bin(t->day);
        t->month  = bcd_to_bin(t->month);
        yr        = bcd_to_bin(yr);
        cent      = bcd_to_bin(cent);
    }

    if(is_12h && (t->hour & 0x80)){
        t->hour = ((t->hour & 0x7F) + 12) % 24;
    }

    t->year = (cent ? (u16)(cent*100+yr) : (u16)(2000+yr));
}

/* ── Bateria via ACPI EC (Embedded Controller) ────────────── */
/* Porta EC padrão: 0x62 (dados) / 0x66 (comando) */
#define EC_DATA  0x62
#define EC_CMD   0x66
#define EC_IBF   (1<<1)  /* input buffer full */
#define EC_OBF   (1<<0)  /* output buffer full */

static void ec_wait_ibf(void){u32 t=10000;while(t--&&(inb(EC_CMD)&EC_IBF));}
static void ec_wait_obf(void){u32 t=10000;while(t--&&!(inb(EC_CMD)&EC_OBF));}

static u8 ec_read(u8 reg){
    ec_wait_ibf(); outb(EC_CMD,0x80);  /* Read EC */
    ec_wait_ibf(); outb(EC_DATA,reg);
    ec_wait_obf(); return inb(EC_DATA);
}

/* Registradores EC comuns (variam por fabricante — fallback seguro) */
#define EC_BAT_PERCENT  0x2C
#define EC_BAT_STATUS   0x2B   /* bit0=present bit1=charging */
#define EC_BAT_VOLT_L   0x2E
#define EC_BAT_VOLT_H   0x2F

static int acpi_ok = 0;

void acpi_init(void){
    /* Testa se EC responde */
    u8 test = ec_read(EC_BAT_PERCENT);
    acpi_ok = (test <= 100);
    kprintf("[acpi] RTC OK  Bateria: %s\n", acpi_ok?"detectada":"nao detectada");
}

int battery_read(battery_info_t* b){
    b->present = 0;
    if(!acpi_ok) return -1;

    u8 status  = ec_read(EC_BAT_STATUS);
    u8 pct     = ec_read(EC_BAT_PERCENT);
    u8 vl      = ec_read(EC_BAT_VOLT_L);
    u8 vh      = ec_read(EC_BAT_VOLT_H);

    if(pct > 100) return -1;  /* EC não respondeu */

    b->present    = 1;
    b->charging   = (status >> 1) & 1;
    b->percent    = pct;
    b->voltage_mv = (u32)((vh<<8)|vl) * 10;
    b->remain_min = b->charging ? 0 : (u32)(pct * 2); /* estimativa simples */
    return 0;
}

const char* battery_icon(int pct, int charging){
    if(charging) return "[~]";
    if(pct > 80) return "[|||]";
    if(pct > 60) return "[|| ]";
    if(pct > 40) return "[|  ]";
    if(pct > 15) return "[!  ]";
    return "[X  ]";
}

/* ── Power ────────────────────────────────────────────────── */
void acpi_power_off(void){
    /* ACPI S5 via porta 0x604 (QEMU) */
    outw(0x604,0x2000);   /* qemu >= 2.0 */
    outw(0xB004,0x2000);  /* bochs/older qemu */
    outw(0x4004,0x3400);  /* virtualbox */
    /* fallback HLT */
    __asm__ volatile("cli; hlt");
}

void acpi_reboot(void){
    /* teclado controller reset */
    outb(0x64,0xFE);
    __asm__ volatile("cli; hlt");
}

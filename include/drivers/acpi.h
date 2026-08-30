#ifndef ACPI_H
#define ACPI_H
#include <kernel/types.h>

typedef struct {
    u8  present;        /* 1 = bateria presente */
    u8  charging;       /* 1 = carregando */
    u8  percent;        /* 0-100 */
    u32 voltage_mv;     /* mV */
    u32 remain_min;     /* minutos restantes */
} battery_info_t;

typedef struct {
    u8 second, minute, hour;
    u8 day, month;
    u16 year;
} rtc_time_t;

void          acpi_init(void);
void          rtc_read(rtc_time_t* t);
int           battery_read(battery_info_t* b);
void          acpi_power_off(void);
void          acpi_reboot(void);
const char*   battery_icon(int pct, int charging);
#endif

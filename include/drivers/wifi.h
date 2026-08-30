#ifndef WIFI_H
#define WIFI_H
#include <kernel/types.h>
#define WIFI_SSID_MAX 33
#define WIFI_NETS_MAX 8
typedef struct {
    char ssid[WIFI_SSID_MAX];
    int  signal;      /* -100 a 0 dBm */
    u8   encrypted;
    u8   connected;
} wifi_net_t;
void        wifi_init(void);
int         wifi_present(void);
int         wifi_connected(void);
int         wifi_scan(wifi_net_t* nets, int max);
int         wifi_connect(const char* ssid, const char* pass);
void        wifi_disconnect(void);
const char* wifi_icon(void);
const char* wifi_ssid(void);
#endif

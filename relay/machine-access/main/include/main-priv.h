#pragma once

// Allow overriding configuration from Kconfig.
#define ISTR(x) STR(x)
#define STR(x) #x

#ifdef WIFI_SSID
#define CFG_WIFI_SSID ISTR(WIFI_SSID)
#else
#define CFG_WIFI_SSID CONFIG_MAIN_WIFI_SSID_DEFAULT
#endif

#ifdef WIFI_PASSWORD
#define CFG_WIFI_PASSWORD ISTR(WIFI_PASSWORD)
#else
#define CFG_WIFI_PASSWORD CONFIG_MAIN_WIFI_PASSWORD_DEFAULT
#endif

#ifdef MQTT_HOST
#define CFG_MQTT_HOST ISTR(MQTT_HOST)
#else
#define CFG_MQTT_HOST CONFIG_MAIN_MQTT_HOST_DEFAULT
#endif

#ifdef MQTT_PORT
#define CFG_MQTT_PORT MQTT_PORT
#else
#define CFG_MQTT_PORT CONFIG_MAIN_MQTT_PORT_DEFAULT
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t user_rf_cal_sector_set();

void user_init();

#ifdef __cplusplus
}
#endif

int fs_init();

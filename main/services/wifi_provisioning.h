#ifndef COPET_WIFI_PROVISIONING_H
#define COPET_WIFI_PROVISIONING_H

#include "esp_err.h"

/*
 * SoftAP Wi-Fi setup. When no credentials are configured, the device brings up
 * an open access point "CoPet-Setup" and serves a small page at 192.168.4.1
 * where the user enters an SSID/password. On submit they are saved to NVS and
 * the device reboots to join as a station -- no reflashing needed.
 *
 * Assumes esp_netif_init(), the default event loop and esp_wifi_init() are
 * already done (wifi_service handles that shared bring-up).
 */
esp_err_t wifi_provisioning_start(void);

#endif

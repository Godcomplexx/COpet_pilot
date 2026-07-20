#ifndef COPET_WIFI_CREDENTIALS_H
#define COPET_WIFI_CREDENTIALS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A small, priority-ordered list of known Wi-Fi networks. This module is pure
 * (no ESP-IDF) so the "which known network is in range?" decision can be
 * verified by host tests. wifi_service builds the list from Kconfig and, when
 * more than one network is configured, scans the air and connects to the
 * highest-priority known SSID that is present — so the device connects at
 * several locations without reflashing.
 */

enum {
    WIFI_MAX_NETWORKS = 3,
    WIFI_SSID_CAPACITY = 33,     /* 32-byte SSID + null */
    WIFI_PASSWORD_CAPACITY = 65, /* up to 63-char WPA2 key + null */
};

typedef struct {
    char ssid[WIFI_SSID_CAPACITY];
    char password[WIFI_PASSWORD_CAPACITY];
} wifi_credential_t;

typedef struct {
    wifi_credential_t items[WIFI_MAX_NETWORKS];
    uint8_t count;
} wifi_credential_list_t;

/* Empty the list. */
void wifi_credentials_clear(wifi_credential_list_t *list);

/*
 * Append a network. Ignored (returns false) when ssid is NULL/empty, the list
 * is full, or the SSID is already present. password may be NULL/empty (open
 * network). Earlier entries have higher priority.
 */
bool wifi_credentials_add(wifi_credential_list_t *list,
                          const char *ssid, const char *password);

/*
 * Return the index of the highest-priority known network whose SSID appears in
 * scanned_ssids, or -1 when none of the known networks are in range.
 */
int wifi_credentials_match(const wifi_credential_list_t *list,
                           const char *const *scanned_ssids,
                           size_t scanned_count);

#endif

#include "services/wifi_credentials.h"

#include <stdio.h>
#include <string.h>

void wifi_credentials_clear(wifi_credential_list_t *list)
{
    if (list == NULL) { return; }
    list->count = 0;
}

static bool contains_ssid(const wifi_credential_list_t *list, const char *ssid)
{
    for (uint8_t index = 0; index < list->count; ++index) {
        if (strcmp(list->items[index].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

bool wifi_credentials_add(wifi_credential_list_t *list,
                          const char *ssid, const char *password)
{
    if (list == NULL || ssid == NULL || ssid[0] == '\0') { return false; }
    if (list->count >= WIFI_MAX_NETWORKS) { return false; }
    if (strlen(ssid) >= WIFI_SSID_CAPACITY) { return false; }
    if (password != NULL && strlen(password) >= WIFI_PASSWORD_CAPACITY) {
        return false;
    }
    if (contains_ssid(list, ssid)) { return false; }

    wifi_credential_t *slot = &list->items[list->count];
    snprintf(slot->ssid, sizeof(slot->ssid), "%s", ssid);
    snprintf(slot->password, sizeof(slot->password), "%s",
             password != NULL ? password : "");
    ++list->count;
    return true;
}

int wifi_credentials_match(const wifi_credential_list_t *list,
                           const char *const *scanned_ssids,
                           size_t scanned_count)
{
    if (list == NULL || scanned_ssids == NULL) { return -1; }

    for (uint8_t known = 0; known < list->count; ++known) {
        const char *ssid = list->items[known].ssid;
        for (size_t seen = 0; seen < scanned_count; ++seen) {
            if (scanned_ssids[seen] != NULL &&
                strcmp(ssid, scanned_ssids[seen]) == 0) {
                return (int)known;
            }
        }
    }
    return -1;
}

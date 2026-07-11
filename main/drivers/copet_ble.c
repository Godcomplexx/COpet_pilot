#include "drivers/copet_ble.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "copet_ble";
static uint8_t s_own_address_type;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile copet_ble_status_t s_status = COPET_BLE_OFF;
static volatile bool s_advertising_requested;
static volatile bool s_host_synced;
static QueueHandle_t s_message_queue;

enum {
    COPET_BLE_MESSAGE_MAX = 64,
};

typedef struct {
    char text[COPET_BLE_MESSAGE_MAX + 1];
} ble_message_t;

static const ble_uuid16_t s_bridge_service_uuid =
    BLE_UUID16_INIT(0xFFF0);
static const ble_uuid16_t s_message_characteristic_uuid =
    BLE_UUID16_INIT(0xFFF1);

static int gap_event(struct ble_gap_event *event, void *argument);
static int message_access(uint16_t connection_handle,
                          uint16_t attribute_handle,
                          struct ble_gatt_access_ctxt *context,
                          void *argument);

static const struct ble_gatt_svc_def s_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_bridge_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_message_characteristic_uuid.u,
                .access_cb = message_access,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {0},
        },
    },
    {0},
};

static int message_access(uint16_t connection_handle,
                          uint16_t attribute_handle,
                          struct ble_gatt_access_ctxt *context,
                          void *argument)
{
    (void)connection_handle;
    (void)attribute_handle;
    (void)argument;

    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > COPET_BLE_MESSAGE_MAX) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    ble_message_t message = {0};
    uint16_t copied = 0;
    const int result = ble_hs_mbuf_to_flat(
        context->om, message.text, COPET_BLE_MESSAGE_MAX, &copied);
    if (result != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    message.text[copied] = '\0';

    for (uint16_t index = 0; index < copied; ++index) {
        if (message.text[index] >= 'a' && message.text[index] <= 'z') {
            message.text[index] =
                (char)(message.text[index] - 'a' + 'A');
        } else if ((uint8_t)message.text[index] < 0x20U ||
                   (uint8_t)message.text[index] > 0x7EU) {
            message.text[index] = ' ';
        }
    }

    (void)xQueueOverwrite(s_message_queue, &message);
    ESP_LOGI(TAG, "RX FFF1: %s", message.text);
    return 0;
}

static esp_err_t start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t *)&s_bridge_service_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) {
        ESP_LOGE(TAG, "Advertising data failed: rc=%d", result);
        s_status = COPET_BLE_ERROR;
        return ESP_FAIL;
    }

    const struct ble_gap_adv_params parameters = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    result = ble_gap_adv_start(
        s_own_address_type, NULL, BLE_HS_FOREVER,
        &parameters, gap_event, NULL);
    if (result != 0 && result != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Advertising start failed: rc=%d", result);
        s_status = COPET_BLE_ERROR;
        return ESP_FAIL;
    }

    s_status = COPET_BLE_ADVERTISING;
    ESP_LOGI(TAG, "Advertising as CoPet Pilot");
    return ESP_OK;
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connection_handle = event->connect.conn_handle;
            s_status = COPET_BLE_CONNECTED;
            ESP_LOGI(TAG, "Phone connected, handle=%u",
                     s_connection_handle);
        } else if (s_advertising_requested) {
            (void)start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Phone disconnected, reason=%d",
                 event->disconnect.reason);
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_advertising_requested) {
            (void)start_advertising();
        } else {
            s_status = COPET_BLE_OFF;
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_advertising_requested) {
            (void)start_advertising();
        }
        return 0;

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: reason=%d", reason);
    s_host_synced = false;
    s_status = COPET_BLE_ERROR;
}

static void on_sync(void)
{
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) {
        result = ble_hs_id_infer_auto(0, &s_own_address_type);
    }
    if (result != 0) {
        ESP_LOGE(TAG, "BLE address setup failed: rc=%d", result);
        s_status = COPET_BLE_ERROR;
        return;
    }

    s_host_synced = true;
    ESP_LOGI(TAG, "NimBLE host synchronized");
    if (s_advertising_requested) {
        (void)start_advertising();
    } else {
        s_status = COPET_BLE_OFF;
    }
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t copet_ble_init(void)
{
    s_message_queue = xQueueCreate(1, sizeof(ble_message_t));
    ESP_RETURN_ON_FALSE(s_message_queue != NULL, ESP_ERR_NO_MEM, TAG,
                        "BLE message queue allocation failed");

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        result = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(result, TAG, "NVS initialization failed");

    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG,
                        "NimBLE initialization failed");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int gatt_result = ble_gatts_count_cfg(s_gatt_services);
    ESP_RETURN_ON_FALSE(gatt_result == 0, ESP_FAIL, TAG,
                        "GATT count failed: rc=%d", gatt_result);
    gatt_result = ble_gatts_add_svcs(s_gatt_services);
    ESP_RETURN_ON_FALSE(gatt_result == 0, ESP_FAIL, TAG,
                        "GATT registration failed: rc=%d", gatt_result);

    const int name_result = ble_svc_gap_device_name_set("CoPet Pilot");
    ESP_RETURN_ON_FALSE(name_result == 0, ESP_FAIL, TAG,
                        "BLE name setup failed: rc=%d", name_result);

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "Phone Bridge initialized; advertising is off");
    return ESP_OK;
}

esp_err_t copet_ble_start(void)
{
    s_advertising_requested = true;
    if (!s_host_synced) {
        s_status = COPET_BLE_STARTING;
        return ESP_OK;
    }
    if (s_connection_handle != BLE_HS_CONN_HANDLE_NONE) {
        s_status = COPET_BLE_CONNECTED;
        return ESP_OK;
    }
    return start_advertising();
}

void copet_ble_stop(void)
{
    s_advertising_requested = false;
    if (ble_gap_adv_active()) {
        (void)ble_gap_adv_stop();
    }
    if (s_connection_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(s_connection_handle,
                                BLE_ERR_REM_USER_CONN_TERM);
    } else {
        s_status = COPET_BLE_OFF;
    }
    ESP_LOGI(TAG, "Phone Bridge stopped");
}

copet_ble_status_t copet_ble_get_status(void)
{
    return s_status;
}

bool copet_ble_take_message(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0 || s_message_queue == NULL) {
        return false;
    }

    ble_message_t message;
    if (xQueueReceive(s_message_queue, &message, 0) != pdTRUE) {
        return false;
    }

    const size_t copy_length =
        strnlen(message.text, sizeof(message.text));
    const size_t output_length =
        copy_length < capacity - 1 ? copy_length : capacity - 1;
    memcpy(buffer, message.text, output_length);
    buffer[output_length] = '\0';
    return true;
}

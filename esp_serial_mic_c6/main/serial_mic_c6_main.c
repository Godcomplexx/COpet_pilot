/*
 * USB audio bridge for Seeed XIAO ESP32-C6 with an analog microphone module.
 *
 * Wiring:
 *   mic AOUT -> D0 / A0 / GPIO0 / ADC1_CH0
 *   mic DOUT -> not used for audio
 *   mic GND  -> GND
 *   mic VCC  -> 3V3
 *   button   -> D1 / GPIO1 to GND, internal pull-up enabled
 *
 * Wireless mode:
 *   Put your router Wi-Fi name/password in WIFI_STA_SSID / WIFI_STA_PASS below.
 *   ESP joins that router and streams TCP on <router-assigned-ip>:3333.
 *   Fallback AP stays available as "ESP32C6_MIC" / "12345678", TCP 192.168.4.1:3333.
 *
 * Frame protocol, ESP -> PC:
 *   audio: 0xA5 0x5A 0x01 lenLo lenHi <PCM int16 LE ...>
 *   event: 0xA5 0x5A 0x02 code        (1 = start, 0 = stop)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_adc/adc_continuous.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"

#include "oled_eyes.h"

#define MIC_ADC_GPIO       0
#define MIC_ADC_UNIT       ADC_UNIT_1
#define MIC_ADC_CHANNEL    ADC_CHANNEL_0
#define TALK_BTN_PIN       1

#define SAMPLE_RATE        16000
#define ADC_OSR            4            // передискретизация: семплим 4x, усредняем -> меньше шума, анти-алиас
#define OUT_SAMPLES        256
#define ADC_READ_SAMPLES   (OUT_SAMPLES * ADC_OSR)
#define ADC_FRAME_BYTES    (ADC_READ_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES)
#define MIC_GAIN           40.0f        // выше усиление (сигнал слабый); подстрой при клиппинге

// Home router Wi-Fi. Leave SSID empty to use only fallback AP mode.
#define WIFI_STA_SSID      ""
#define WIFI_STA_PASS      ""
#define WIFI_STA_MAX_RETRY 8

#define WIFI_AP_SSID       "ESP32C6_MIC"
#define WIFI_AP_PASS       "12345678"
#define WIFI_AP_CHANNEL    6
#define WIFI_TCP_PORT      3333
#define WIFI_DISCOVERY_PORT 3334
#define WIFI_DISCOVERY_MS  2000
#define WIFI_DISCOVERY_REQ "ESP32C6_MIC?"

#define TAG "serial_mic"

#define MAGIC0 0xA5
#define MAGIC1 0x5A
#define TYPE_AUDIO 0x01
#define TYPE_EVENT 0x02

#if CONFIG_IDF_TARGET_ESP32S2
#define ADC_OUTPUT_FORMAT ADC_DIGI_OUTPUT_FORMAT_TYPE1
#else
#define ADC_OUTPUT_FORMAT ADC_DIGI_OUTPUT_FORMAT_TYPE2
#endif

static adc_continuous_handle_t adc_handle;
static volatile int tcp_client_fd = -1;
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count = 0;
static volatile bool wifi_sta_connected = false;
static char wifi_sta_ip[16] = "";
static volatile uint32_t face_generation = 0;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void out_bytes(const void *data, size_t len)
{
    int fd = tcp_client_fd;
    if (fd < 0) {
        return;
    }

    const uint8_t *p = (const uint8_t *)data;
    size_t sent_total = 0;
    while (sent_total < len) {
        int sent = send(fd, p + sent_total, len - sent_total, 0);
        if (sent <= 0) {
            close(fd);
            if (tcp_client_fd == fd) {
                tcp_client_fd = -1;
            }
            return;
        }
        sent_total += (size_t)sent;
    }
}

static void send_event(uint8_t code)
{
    uint8_t hdr[4] = {MAGIC0, MAGIC1, TYPE_EVENT, code};
    out_bytes(hdr, sizeof(hdr));
}

static void send_audio(const int16_t *data, int samples)
{
    uint16_t len = (uint16_t)(samples * sizeof(int16_t));
    uint8_t hdr[5] = {
        MAGIC0, MAGIC1, TYPE_AUDIO, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)
    };
    out_bytes(hdr, sizeof(hdr));
    out_bytes(data, len);
}

static int16_t clamp_i16(float value)
{
    if (value > 32767.0f) {
        return 32767;
    }
    if (value < -32768.0f) {
        return -32768;
    }
    return (int16_t)value;
}

static void init_button(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << TALK_BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static void init_adc(void)
{
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = ADC_FRAME_BYTES * 4,
        .conv_frame_size = ADC_FRAME_BYTES,
        .flags.flush_pool = 1,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &adc_handle));

    adc_digi_pattern_config_t pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = MIC_ADC_CHANNEL,
        .unit = MIC_ADC_UNIT,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    adc_continuous_config_t config = {
        .pattern_num = 1,
        .adc_pattern = &pattern,
        .sample_freq_hz = SAMPLE_RATE * ADC_OSR,   // 64 кГц, усредняем до 16 кГц
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_OUTPUT_FORMAT,
    };
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &config));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

static void processing_face_timeout_task(void *arg)
{
    uint32_t generation = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(9000));
    if (generation == face_generation) {
        oled_eyes_set_state(FACE_IDLE);
    }
    vTaskDelete(NULL);
}

static void wifi_tcp_server_task(void *arg)
{
    (void)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0) {
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WIFI_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, 1) != 0) {
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int fd = accept(listen_fd, (struct sockaddr *)&source_addr, &addr_len);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        int old_fd = tcp_client_fd;
        tcp_client_fd = fd;
        if (old_fd >= 0) {
            close(old_fd);
        }
    }
}

static void wifi_discovery_task(void *arg)
{
    (void)arg;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WIFI_DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr));

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 200000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(WIFI_DISCOVERY_PORT),
        .sin_addr.s_addr = inet_addr("255.255.255.255"),
    };

    while (1) {
        char msg[96];
        const bool sta = wifi_sta_connected && wifi_sta_ip[0] != '\0';
        const char *ip = sta ? wifi_sta_ip : "192.168.4.1";
        const char *mode = sta ? "sta" : "ap";

        int len = snprintf(
            msg, sizeof(msg), "ESP32C6_MIC;ip=%s;tcp=%d;mode=%s",
            ip, WIFI_TCP_PORT, mode
        );
        if (len > 0) {
            sendto(fd, msg, (size_t)len, 0, (struct sockaddr *)&dest, sizeof(dest));
        }

        int waited_ms = 0;
        while (waited_ms < WIFI_DISCOVERY_MS) {
            char req[64];
            struct sockaddr_in src;
            socklen_t src_len = sizeof(src);
            int got = recvfrom(fd, req, sizeof(req) - 1, 0, (struct sockaddr *)&src, &src_len);
            if (got > 0) {
                req[got] = '\0';
                if (strncmp(req, WIFI_DISCOVERY_REQ, strlen(WIFI_DISCOVERY_REQ)) == 0 && len > 0) {
                    sendto(fd, msg, (size_t)len, 0, (struct sockaddr *)&src, src_len);
                }
            }
            waited_ms += 200;
        }
    }
}

static void wifi_event_handler(
    void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data
)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_sta_connected = false;
        wifi_sta_ip[0] = '\0';

        if (wifi_retry_count < WIFI_STA_MAX_RETRY) {
            wifi_retry_count++;
            esp_wifi_connect();
        } else if (wifi_event_group != NULL) {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(wifi_sta_ip, sizeof(wifi_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        wifi_sta_connected = true;

        if (wifi_event_group != NULL) {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void init_wifi(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_ap();
    if (strlen(WIFI_STA_SSID) > 0) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL
    ));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(WIFI_AP_PASS) == 0) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    if (strlen(WIFI_STA_SSID) > 0) {
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, WIFI_STA_SSID, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char *)sta_cfg.sta.password, WIFI_STA_PASS, sizeof(sta_cfg.sta.password) - 1);
        sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_cfg.sta.failure_retry_cnt = WIFI_STA_MAX_RETRY;
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(wifi_tcp_server_task, "wifi_tcp_server", 4096, NULL, 5, NULL);
    xTaskCreate(wifi_discovery_task, "wifi_discovery", 3072, NULL, 4, NULL);
}

void app_main(void)
{
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    setvbuf(stdout, NULL, _IONBF, 0);

    init_wifi();
    init_button();
    init_adc();
    oled_eyes_start();   // OLED-глаза (SDA=22/SCL=23); если дисплея нет — тихо пропустит

    static uint8_t raw[ADC_FRAME_BYTES];
    static adc_continuous_data_t parsed[ADC_READ_SAMPLES];
    static int16_t pcm[OUT_SAMPLES];

    bool recording = false;
    vTaskDelay(pdMS_TO_TICKS(250));
    int last_button = gpio_get_level(TALK_BTN_PIN);
    ESP_LOGW(TAG, "Talk button initial level=%d (1=released, 0=pressed)", last_button);
    float dc = 2048.0f;

    while (1) {
        int button = gpio_get_level(TALK_BTN_PIN);
        if (button != last_button) {
            vTaskDelay(pdMS_TO_TICKS(20));
            button = gpio_get_level(TALK_BTN_PIN);
            if (button != last_button) {
                last_button = button;
                if (button == 0) {
                    recording = !recording;
                    send_event(recording ? 1 : 0);
                    uint32_t generation = ++face_generation;
                    if (recording) {
                        oled_eyes_set_state(FACE_LISTENING);
                    } else {
                        oled_eyes_set_state(FACE_PROCESSING);
                        xTaskCreate(
                            processing_face_timeout_task,
                            "face_processing_timeout",
                            2048,
                            (void *)(uintptr_t)generation,
                            1,
                            NULL
                        );
                    }
                }
            }
        }

        uint32_t raw_len = 0;
        esp_err_t err = adc_continuous_read(
            adc_handle, raw, sizeof(raw), &raw_len, ADC_MAX_DELAY
        );
        if (err != ESP_OK || raw_len == 0) {
            continue;
        }

        uint32_t parsed_count = 0;
        err = adc_continuous_parse_data(
            adc_handle, raw, raw_len, parsed, &parsed_count
        );
        if (err != ESP_OK || parsed_count == 0) {
            continue;
        }

        int out_count = 0;
        float acc = 0.0f;
        int acc_n = 0;
        for (uint32_t i = 0; i < parsed_count && out_count < OUT_SAMPLES; i++) {
            if (!parsed[i].valid ||
                parsed[i].unit != MIC_ADC_UNIT ||
                parsed[i].channel != MIC_ADC_CHANNEL) {
                continue;
            }

            acc += (float)parsed[i].raw_data;
            if (++acc_n < ADC_OSR) {
                continue;
            }
            float sample = acc / (float)ADC_OSR;   // усреднение OSR отсчётов
            acc = 0.0f;
            acc_n = 0;

            dc = 0.9995f * dc + 0.0005f * sample;  // убрать постоянную составляющую
            pcm[out_count++] = clamp_i16((sample - dc) * MIC_GAIN);
        }

        if (out_count > 0) {
            send_audio(pcm, out_count);
        }
    }
}

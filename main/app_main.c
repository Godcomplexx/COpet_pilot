#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "core/copet_behavior.h"
#include "core/copet_modes.h"
#include "drivers/copet_ble.h"
#include "drivers/copet_audio.h"
#include "drivers/mpu6050.h"
#include "drivers/touch_button.h"
#include "modes/desk_mode.h"
#include "modes/focus_mode.h"
#include "modes/menu_mode.h"
#include "modes/settings_mode.h"
#include "services/weather_service.h"
#include "services/wifi_service.h"
#include "ui/boot_ui.h"
#include "ui/desk_ui.h"
#include "ui/diag_ui.h"
#include "ui/focus_ui.h"
#include "ui/menu_ui.h"
#include "ui/settings_ui.h"
#include "ui/ui_canvas.h"

/*
 * CoPet integration layer for the ESP32-WROOM-32 DevKit shown in the photos.
 *
 * app_main owns only the hardware bring-up (display, I2C/SHT31, encoder) and
 * the main loop that wires inputs to mode modules and mode state to renderers.
 * The mode logic lives in modes/, the drawing in ui/. See docs 01 and 03.
 *
 * ST7789: SCK=18, MOSI=5, DC=16, RST=17, CS is not present.
 * SHT3x:  SDA=21, SCL=22.
 * Encoder: A=32, B=33, COM=GND.
 */

static const char *TAG = "copet_test";

enum {
    LCD_WIDTH = 240,
    LCD_HEIGHT = 240,
    LCD_PIN_SCLK = 18,
    LCD_PIN_MOSI = 5,
    LCD_PIN_DC = 16,
    LCD_PIN_RST = 17,
    I2C_PIN_SDA = 21,
    I2C_PIN_SCL = 22,
    ENCODER_PIN_A = 32,
    ENCODER_PIN_B = 33,
    ENCODER_DEBOUNCE_US = 12000,
    LCD_TRANSFER_ROWS = 16,
    DESK_FRAME_INTERVAL_US = 125000,
    MENU_TIMEOUT_US = 10000000,
    BOOT_STAGE_HOLD_MS = 80,
    BOOT_FINAL_HOLD_MS = 250,
};

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)
#define LCD_X_GAP 0
#define LCD_Y_GAP 0

#define SHT31_ADDR_PRIMARY 0x44
#define SHT31_ADDR_SECONDARY 0x45

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static SemaphoreHandle_t s_lcd_done;
typedef uint8_t screen_color_t;

static screen_color_t *s_framebuffer;
static uint16_t *s_lcd_transfer_buffer;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sht31_primary;
static i2c_master_dev_handle_t s_sht31_secondary;

static volatile int32_t s_encoder_position;
static volatile uint8_t s_encoder_ab;

static uint16_t rgb332_to_panel_rgb565(screen_color_t color)
{
    const uint16_t red_3 = (color >> 5) & 0x07U;
    const uint16_t green_3 = (color >> 2) & 0x07U;
    const uint16_t blue_2 = color & 0x03U;
    const uint16_t red_5 = (red_3 << 2) | (red_3 >> 1);
    const uint16_t green_6 = (green_3 << 3) | green_3;
    const uint16_t blue_5 =
        (blue_2 << 3) | (blue_2 << 1) | (blue_2 >> 1);
    const uint16_t color_565 =
        (red_5 << 11) | (green_6 << 5) | blue_5;

    /* The SPI panel consumes RGB565 most-significant byte first. */
    return (uint16_t)((color_565 << 8) | (color_565 >> 8));
}

static bool lcd_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *event_data,
                              void *user_context)
{
    (void)panel_io;
    (void)event_data;
    (void)user_context;

    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_lcd_done, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t lcd_apply_gmt130_init(void)
{
    static const uint8_t madctl[] = {0x00};
    static const uint8_t pixel_format[] = {0x55};
    static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t gate_control[] = {0x35};
    static const uint8_t vcom[] = {0x19};
    static const uint8_t lcm_control[] = {0x2C};
    static const uint8_t vdv_vrh_enable[] = {0x01};
    static const uint8_t vrh[] = {0x12};
    static const uint8_t vdv[] = {0x20};
    static const uint8_t frame_rate[] = {0x0F};
    static const uint8_t power_control[] = {0xA4, 0xA1};
    static const uint8_t positive_gamma[] = {
        0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
        0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
    };
    static const uint8_t negative_gamma[] = {
        0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
        0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
    };

#define GMT130_COMMAND(command, data)                                      \
    ESP_RETURN_ON_ERROR(                                                   \
        esp_lcd_panel_io_tx_param(s_panel_io, command, data, sizeof(data)), \
        TAG, "GMT130 command 0x%02X failed", command)

    GMT130_COMMAND(0x36, madctl);
    GMT130_COMMAND(0x3A, pixel_format);
    GMT130_COMMAND(0xB2, porch);
    GMT130_COMMAND(0xB7, gate_control);
    GMT130_COMMAND(0xBB, vcom);
    GMT130_COMMAND(0xC0, lcm_control);
    GMT130_COMMAND(0xC2, vdv_vrh_enable);
    GMT130_COMMAND(0xC3, vrh);
    GMT130_COMMAND(0xC4, vdv);
    GMT130_COMMAND(0xC6, frame_rate);
    GMT130_COMMAND(0xD0, power_control);
    GMT130_COMMAND(0xE0, positive_gamma);
    GMT130_COMMAND(0xE1, negative_gamma);
#undef GMT130_COMMAND

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(s_panel_io, 0x21, NULL, 0),
        TAG, "GMT130 inversion command failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(s_panel_io, 0x11, NULL, 0),
        TAG, "GMT130 sleep-out command failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(s_panel_io, 0x29, NULL, 0),
        TAG, "GMT130 display-on command failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

static esp_err_t lcd_init(void)
{
    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
        TAG, "SPI bus initialization failed");

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = -1,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,
        .trans_queue_depth = 1,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                 &io_config, &s_panel_io),
        TAG, "LCD panel IO initialization failed");

    s_lcd_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lcd_done != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD semaphore allocation failed");

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lcd_transfer_done,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_register_event_callbacks(
            s_panel_io, &callbacks, NULL),
        TAG, "LCD callback registration failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel),
        TAG, "ST7789 initialization failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "LCD reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(lcd_apply_gmt130_init(),
                        TAG, "GMT130-specific initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, LCD_Y_GAP),
                        TAG, "LCD gap setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),
                        TAG, "LCD enable failed");

    s_framebuffer = heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(screen_color_t),
        MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD framebuffer allocation failed");

    s_lcd_transfer_buffer = heap_caps_malloc(
        LCD_WIDTH * LCD_TRANSFER_ROWS * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_lcd_transfer_buffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD transfer buffer allocation failed");

    ESP_LOGI(TAG, "LCD buffers: framebuffer=%u bytes, DMA stripe=%u bytes",
             LCD_WIDTH * LCD_HEIGHT * (unsigned)sizeof(screen_color_t),
             LCD_WIDTH * LCD_TRANSFER_ROWS *
                 (unsigned)sizeof(uint16_t));
    return ESP_OK;
}

static esp_err_t lcd_refresh(void)
{
    for (int y = 0; y < LCD_HEIGHT; y += LCD_TRANSFER_ROWS) {
        const int rows =
            y + LCD_TRANSFER_ROWS <= LCD_HEIGHT
                ? LCD_TRANSFER_ROWS
                : LCD_HEIGHT - y;
        const int pixel_count = LCD_WIDTH * rows;
        const int source_offset = y * LCD_WIDTH;

        for (int pixel = 0; pixel < pixel_count; ++pixel) {
            s_lcd_transfer_buffer[pixel] =
                rgb332_to_panel_rgb565(
                    s_framebuffer[source_offset + pixel]);
        }

        /* Remove a stale notification before starting the next stripe. */
        (void)xSemaphoreTake(s_lcd_done, 0);
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(
                s_panel, 0, y, LCD_WIDTH, y + rows,
                s_lcd_transfer_buffer),
            TAG, "LCD stripe transfer failed");
        ESP_RETURN_ON_FALSE(
            xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(1000)) == pdTRUE,
            ESP_ERR_TIMEOUT, TAG, "LCD stripe transfer timeout");
    }
    return ESP_OK;
}

static void show_boot_progress(uint8_t progress_percent, const char *status,
                               uint32_t hold_ms)
{
    boot_ui_render(s_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                   progress_percent, status);
    ESP_ERROR_CHECK(lcd_refresh());
    if (hold_ms > 0U) {
        vTaskDelay(pdMS_TO_TICKS(hold_ms));
    }
}

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus),
                        TAG, "I2C bus initialization failed");

    const i2c_device_config_t primary_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_ADDR_PRIMARY,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_i2c_bus, &primary_config,
                                  &s_sht31_primary),
        TAG, "SHT3x primary address setup failed");

    const i2c_device_config_t secondary_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_ADDR_SECONDARY,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &secondary_config,
                                     &s_sht31_secondary);
}

static uint8_t sht31_crc(const uint8_t *data)
{
    uint8_t crc = 0xFF;
    for (int byte = 0; byte < 2; ++byte) {
        crc ^= data[byte];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t sht31_read_at(i2c_master_dev_handle_t device,
                               uint8_t address,
                               float *temperature,
                               float *humidity)
{
    const uint8_t measure_high_repeatability[] = {0x24, 0x00};
    uint8_t response[6];

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit(device, measure_high_repeatability,
                            sizeof(measure_high_repeatability), 100),
        TAG, "SHT3x command failed at address 0x%02X", address);

    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(
        i2c_master_receive(device, response, sizeof(response), 100),
        TAG, "SHT3x read failed at address 0x%02X", address);

    ESP_RETURN_ON_FALSE(
        sht31_crc(&response[0]) == response[2] &&
            sht31_crc(&response[3]) == response[5],
        ESP_ERR_INVALID_CRC, TAG, "SHT3x CRC check failed");

    const uint16_t raw_temperature =
        ((uint16_t)response[0] << 8) | response[1];
    const uint16_t raw_humidity =
        ((uint16_t)response[3] << 8) | response[4];

    *temperature = -45.0f + 175.0f * raw_temperature / 65535.0f;
    *humidity = 100.0f * raw_humidity / 65535.0f;
    return ESP_OK;
}

static esp_err_t sht31_read(float *temperature,
                            float *humidity,
                            uint8_t *detected_address)
{
    if (*detected_address != 0) {
        return sht31_read_at(
            *detected_address == SHT31_ADDR_PRIMARY
                ? s_sht31_primary
                : s_sht31_secondary,
            *detected_address, temperature, humidity);
    }

    esp_err_t error = sht31_read_at(
        s_sht31_primary, SHT31_ADDR_PRIMARY, temperature, humidity);
    if (error == ESP_OK) {
        *detected_address = SHT31_ADDR_PRIMARY;
        return ESP_OK;
    }

    error = sht31_read_at(
        s_sht31_secondary, SHT31_ADDR_SECONDARY, temperature, humidity);
    if (error == ESP_OK) {
        *detected_address = SHT31_ADDR_SECONDARY;
    }
    return error;
}

static void encoder_task(void *argument)
{
    (void)argument;

    /*
     * The tested three-contact mouse wheel has a three-state cycle:
     * 10 -> 11 -> 01 -> 10. The reverse direction uses the opposite order.
     * State 00 is not a detent and is ignored.
     */
    static const int8_t transition_table[16] = {
        0, 0, 0, 0,
        0, 0, 1, -1,
        0, -1, 0, 1,
        0, 1, -1, 0,
    };

    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << ENCODER_PIN_A) | (1ULL << ENCODER_PIN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    uint8_t raw_state =
        ((uint8_t)gpio_get_level(ENCODER_PIN_A) << 1) |
        (uint8_t)gpio_get_level(ENCODER_PIN_B);
    uint8_t stable_state = raw_state;
    int64_t raw_changed_us = esp_timer_get_time();
    s_encoder_ab = stable_state;

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        const uint8_t sampled_state =
            ((uint8_t)gpio_get_level(ENCODER_PIN_A) << 1) |
            (uint8_t)gpio_get_level(ENCODER_PIN_B);

        if (sampled_state != raw_state) {
            raw_state = sampled_state;
            raw_changed_us = now_us;
        }

        if (raw_state != stable_state && raw_state != 0 &&
            now_us - raw_changed_us >= ENCODER_DEBOUNCE_US) {
            const int8_t logical_step =
                transition_table[(stable_state << 2) | raw_state];
            if (logical_step != 0) {
                s_encoder_position += logical_step;
                ESP_LOGI(TAG, "ENC STEP %s, count=%ld, AB=%u%u",
                         logical_step > 0 ? "RIGHT" : "LEFT",
                         (long)s_encoder_position,
                         (raw_state >> 1) & 1U, raw_state & 1U);
            } else {
                ESP_LOGW(TAG, "ENC unexpected transition %u%u -> %u%u",
                         (stable_state >> 1) & 1U, stable_state & 1U,
                         (raw_state >> 1) & 1U, raw_state & 1U);
            }
            stable_state = raw_state;
            s_encoder_ab = stable_state;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void play_audio_event(bool audio_available,
                             copet_audio_event_t event)
{
    if (!audio_available) {
        return;
    }
    const esp_err_t result = copet_audio_play_event(event);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Audio event %d skipped: %s", event,
                 esp_err_to_name(result));
    }
}

static void post_behavior(copet_behavior_t *behavior,
                          copet_behavior_event_type_t type,
                          int32_t value,
                          uint32_t now_ms)
{
    const copet_behavior_event_t event = {
        .type = type,
        .value = value,
    };
    copet_behavior_post(behavior, &event, now_ms);
}

static copet_behavior_focus_state_t behavior_focus_state(
    copet_mode_t mode, const focus_mode_t *focus)
{
    if (mode != COPET_MODE_FOCUS || focus == NULL) {
        return COPET_BEHAVIOR_FOCUS_OFF;
    }
    if (focus->break_phase) {
        if (focus->state == FOCUS_TIMER_RUNNING) {
            return COPET_BEHAVIOR_FOCUS_RUNNING_BREAK;
        }
        return focus->state == FOCUS_TIMER_PAUSED
            ? COPET_BEHAVIOR_FOCUS_PAUSED_BREAK
            : COPET_BEHAVIOR_FOCUS_READY_BREAK;
    }
    if (focus->state == FOCUS_TIMER_RUNNING) {
        return COPET_BEHAVIOR_FOCUS_RUNNING_WORK;
    }
    return focus->state == FOCUS_TIMER_PAUSED
        ? COPET_BEHAVIOR_FOCUS_PAUSED_WORK
        : COPET_BEHAVIOR_FOCUS_READY_WORK;
}

static bool wifi_status_is_connecting(wifi_service_status_t status)
{
    return status == WIFI_SERVICE_STARTING ||
           status == WIFI_SERVICE_CONNECTING ||
           status == WIFI_SERVICE_RETRY_WAIT;
}

static void render_active_mode(copet_mode_t mode, const desk_mode_t *desk,
                               const menu_mode_t *menu,
                               const focus_mode_t *focus,
                               const settings_mode_t *settings,
                               const copet_behavior_view_t *behavior,
                               const char *network_status,
                               const weather_service_snapshot_t *weather,
                               desk_ui_environment_t environment,
                               copet_ble_status_t ble_status,
                               const char *ble_message)
{
    switch (mode) {
    case COPET_MODE_DESK:
        desk_ui_render(s_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                       desk_mode_get_view(desk), behavior, network_status,
                       weather, environment);
        break;
    case COPET_MODE_SETTINGS:
        settings_ui_render(s_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                           desk_mode_get_view(desk), settings, behavior,
                           network_status, weather);
        break;
    case COPET_MODE_FOCUS:
        focus_ui_render(s_framebuffer, LCD_WIDTH, LCD_HEIGHT, focus,
                        behavior);
        break;
    case COPET_MODE_PHONE_BRIDGE:
        diag_ui_render_phone_bridge(s_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                                    ble_status, ble_message);
        break;
    case COPET_MODE_MENU:
    default:
        menu_ui_render(s_framebuffer, LCD_WIDTH, LCD_HEIGHT, menu,
                       behavior, network_status);
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CoPet Desk + menu + hardware test");
    ESP_LOGI(TAG, "Board target: ESP32-WROOM-32");

    ESP_ERROR_CHECK(lcd_init());
    show_boot_progress(10, "DISPLAY READY", BOOT_STAGE_HOLD_MS);
    ESP_ERROR_CHECK(i2c_init());
    ESP_ERROR_CHECK(touch_button_init());
    show_boot_progress(35, "INPUT READY", BOOT_STAGE_HOLD_MS);
    uint8_t mpu6050_address = 0;
    const esp_err_t mpu6050_init_result =
        mpu6050_init(s_i2c_bus, &mpu6050_address);
    const bool mpu6050_available = mpu6050_init_result == ESP_OK;
    if (mpu6050_available) {
        ESP_LOGI(TAG, "MPU6050 motion reactions enabled at 0x%02X",
                 mpu6050_address);
    } else {
        ESP_LOGW(TAG, "MPU6050 not found; motion reactions are waiting for hardware");
    }
    show_boot_progress(55, "SENSORS CHECKED", BOOT_STAGE_HOLD_MS);
    const esp_err_t wifi_init_result = wifi_service_start();
    if (wifi_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi unavailable: %s",
                 esp_err_to_name(wifi_init_result));
    }
    const esp_err_t weather_init_result = weather_service_start();
    if (weather_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Outdoor weather unavailable: %s",
                 esp_err_to_name(weather_init_result));
    }
    show_boot_progress(75, "NETWORK STARTED", BOOT_STAGE_HOLD_MS);
#if CONFIG_COPET_DIAGNOSTIC_BLE_ENABLED
    const esp_err_t ble_init_result = copet_ble_init();
    const bool ble_available = ble_init_result == ESP_OK;
    if (!ble_available) {
        ESP_LOGE(TAG, "Diagnostic BLE unavailable: %s",
                 esp_err_to_name(ble_init_result));
    }
#else
    const bool ble_available = false;
    ESP_LOGI(TAG, "Diagnostic BLE disabled; RAM reserved for network services");
#endif
    const esp_err_t audio_init_result = copet_audio_init();
    const bool audio_available = audio_init_result == ESP_OK;
    if (!audio_available) {
        ESP_LOGE(TAG, "Product audio unavailable: %s",
                 esp_err_to_name(audio_init_result));
    }
    show_boot_progress(90, "SERVICES CHECKED", BOOT_STAGE_HOLD_MS);

    BaseType_t task_created =
        xTaskCreate(encoder_task, "encoder", 2048, NULL, 5, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    bool sensor_ok = false;
    float temperature = 0.0f;
    float humidity = 0.0f;
    uint8_t sht31_address = 0;
    int64_t next_sensor_read_us = 0;
    int32_t displayed_encoder_position = INT32_MIN;
    uint8_t displayed_encoder_ab = UINT8_MAX;
    copet_mode_t mode = COPET_MODE_DESK;
    bool force_redraw = true;
    desk_mode_t desk;
    menu_mode_t menu;
    focus_mode_t focus;
    settings_mode_t settings;
    copet_behavior_t behavior;
    copet_ble_status_t displayed_ble_status = COPET_BLE_OFF;
    char ble_message[65] = "NO MESSAGE";
    int64_t next_desk_frame_us = 0;
    int64_t menu_last_activity_us = 0;
    int64_t next_mpu6050_read_us = 0;
    desk_motion_event_t displayed_motion_event = DESK_MOTION_NONE;
    int displayed_expression = -1;
    int displayed_vibe = -1;
    int displayed_behavior = -1;
    copet_behavior_focus_state_t published_focus_state =
        COPET_BEHAVIOR_FOCUS_OFF;
    wifi_service_status_t displayed_wifi_status = WIFI_SERVICE_OFF;
    weather_service_snapshot_t weather = {0};
    uint32_t displayed_weather_revision = UINT32_MAX;
    desk_ui_environment_t desk_environment = DESK_UI_ENVIRONMENT_ROOM;

    const uint32_t app_started_ms =
        (uint32_t)(esp_timer_get_time() / 1000);
    desk_mode_init(&desk, app_started_ms);
    copet_behavior_init(&behavior, app_started_ms, app_started_ms);
    menu_mode_init(&menu);
    focus_mode_init(&focus);
    settings_mode_init(&settings, true);
    if (audio_available) {
        copet_audio_set_enabled(settings.sound_enabled);
    }

    show_boot_progress(100, "DESK MODE", BOOT_FINAL_HOLD_MS);
    post_behavior(&behavior, COPET_BEHAVIOR_EVENT_BOOT_COMPLETED, 0,
                  (uint32_t)(esp_timer_get_time() / 1000));
    ESP_LOGI(TAG, "Mode: DESK");

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        const int64_t now_ms = now_us / 1000;
        bool redraw = false;

        const touch_button_event_t touch_event =
            touch_button_poll(now_ms);
        if (touch_event == TOUCH_BUTTON_EVENT_SHORT) {
            ESP_LOGI(TAG, "TOUCH_SHORT");
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
            if (mode == COPET_MODE_DESK) {
                post_behavior(&behavior, COPET_BEHAVIOR_EVENT_TOUCH_SHORT,
                              0, (uint32_t)now_ms);
            } else if (mode == COPET_MODE_MENU ||
                       mode == COPET_MODE_FOCUS ||
                       mode == COPET_MODE_SETTINGS) {
                post_behavior(&behavior, COPET_BEHAVIOR_EVENT_CONFIRM,
                              0, (uint32_t)now_ms);
            } else {
                post_behavior(&behavior,
                              COPET_BEHAVIOR_EVENT_USER_ACTIVITY,
                              0, (uint32_t)now_ms);
            }
            if (mode == COPET_MODE_DESK) {
                desk_environment =
                    desk_environment == DESK_UI_ENVIRONMENT_ROOM
                        ? DESK_UI_ENVIRONMENT_OUTDOOR
                        : DESK_UI_ENVIRONMENT_ROOM;
                desk_mode_on_touch(&desk, (uint32_t)now_ms);
                play_audio_event(audio_available,
                                 COPET_AUDIO_VIEW_CHANGE);
                ESP_LOGI(TAG, "Desk touch reaction: %s; climate view: %s",
                         desk_mode_touch_reaction_label(
                             desk_mode_get_view(&desk)->touch_reaction),
                         desk_environment == DESK_UI_ENVIRONMENT_ROOM
                             ? "ROOM"
                             : "OUT");
                redraw = true;
            } else if (mode == COPET_MODE_MENU) {
                const menu_item_t *item = menu_mode_selected(&menu);
                play_audio_event(audio_available,
                                 COPET_AUDIO_MENU_CONFIRM);
                mode = item->mode;
                ESP_LOGI(TAG, "Mode opened: %s", item->label);
                if (mode == COPET_MODE_PHONE_BRIDGE && ble_available) {
                    const esp_err_t result = copet_ble_start();
                    if (result != ESP_OK) {
                        ESP_LOGE(TAG, "BLE advertising failed: %s",
                                 esp_err_to_name(result));
                    }
                }
            } else if (mode == COPET_MODE_FOCUS) {
                const focus_toggle_result_t toggle_result =
                    focus_mode_toggle(&focus, now_us);
                if (toggle_result == FOCUS_TOGGLE_STARTED ||
                    toggle_result == FOCUS_TOGGLE_RESUMED) {
                    play_audio_event(audio_available,
                                     COPET_AUDIO_FOCUS_START);
                } else {
                    play_audio_event(audio_available,
                                     COPET_AUDIO_FOCUS_PAUSE);
                }
                if (toggle_result == FOCUS_TOGGLE_PAUSED) {
                    ESP_LOGI(TAG, "Focus timer paused at %lu seconds",
                             (unsigned long)focus.remaining_seconds);
                } else {
                    ESP_LOGI(TAG, "Focus timer %s: %s",
                             toggle_result == FOCUS_TOGGLE_RESUMED
                                 ? "resumed"
                                 : "started",
                             focus.break_phase ? "BREAK" : "WORK");
                }
            } else if (mode == COPET_MODE_SETTINGS) {
                const bool enabled = settings_mode_toggle_sound(&settings);
                if (audio_available) {
                    copet_audio_set_enabled(enabled);
                }
                ESP_LOGI(TAG, "Sound setting: %s",
                         settings_mode_sound_label(&settings));
            }
            force_redraw = true;
        } else if (touch_event == TOUCH_BUTTON_EVENT_LONG) {
            ESP_LOGI(TAG, "TOUCH_LONG");
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
            post_behavior(&behavior, COPET_BEHAVIOR_EVENT_USER_ACTIVITY,
                          0, (uint32_t)now_ms);
            if (mode != COPET_MODE_DESK) {
                if (mode == COPET_MODE_PHONE_BRIDGE && ble_available) {
                    copet_ble_stop();
                }
                if (mode == COPET_MODE_FOCUS) {
                    focus_mode_pause(&focus);
                }
                mode = COPET_MODE_DESK;
                next_desk_frame_us = 0;
                ESP_LOGI(TAG, "Mode: DESK");
            }
            force_redraw = true;
        }

        if (now_us >= next_sensor_read_us) {
            const esp_err_t result =
                sht31_read(&temperature, &humidity, &sht31_address);
            sensor_ok = result == ESP_OK;
            if (sensor_ok) {
                ESP_LOGI(TAG,
                         "SHT3x 0x%02X: temperature=%.1f C, humidity=%.1f %%",
                         sht31_address, (double)temperature, (double)humidity);
            } else {
                ESP_LOGW(TAG,
                         "SHT3x not responding at 0x44/0x45: %s",
                         esp_err_to_name(result));
                sht31_address = 0;
            }
            desk_mode_set_environment(&desk, sensor_ok,
                                      temperature, humidity);
            next_sensor_read_us = now_us + 2000000;
            redraw = true;
        }

        if (mpu6050_available && now_us >= next_mpu6050_read_us) {
            mpu6050_sample_t sample;
            const esp_err_t motion_result = mpu6050_read(&sample);
            if (motion_result == ESP_OK) {
                const desk_motion_event_t motion_event =
                    desk_mode_set_motion_sample(
                        &desk, true,
                        sample.accel_x_g, sample.accel_y_g, sample.accel_z_g,
                        sample.gyro_x_dps, sample.gyro_y_dps,
                        sample.gyro_z_dps, (uint32_t)now_ms);
                if (motion_event != displayed_motion_event) {
                    ESP_LOGI(TAG,
                             "Motion reaction: %s; "
                             "accel=(%.2f, %.2f, %.2f) g; "
                             "gyro=(%.1f, %.1f, %.1f) dps",
                             desk_mode_motion_label(motion_event),
                             (double)sample.accel_x_g,
                             (double)sample.accel_y_g,
                             (double)sample.accel_z_g,
                             (double)sample.gyro_x_dps,
                             (double)sample.gyro_y_dps,
                             (double)sample.gyro_z_dps);
                    displayed_motion_event = motion_event;
                    if (motion_event == DESK_MOTION_FALLING) {
                        post_behavior(
                            &behavior,
                            COPET_BEHAVIOR_EVENT_MOTION_FALLING,
                            0, (uint32_t)now_ms);
                    } else if (motion_event == DESK_MOTION_SHAKEN) {
                        post_behavior(
                            &behavior,
                            COPET_BEHAVIOR_EVENT_MOTION_SHAKEN_STRONG,
                            0, (uint32_t)now_ms);
                    } else if (motion_event == DESK_MOTION_TILTED) {
                        post_behavior(
                            &behavior,
                            sample.accel_x_g < 0.0f
                                ? COPET_BEHAVIOR_EVENT_MOTION_TILT_LEFT
                                : COPET_BEHAVIOR_EVENT_MOTION_TILT_RIGHT,
                            0, (uint32_t)now_ms);
                    }
                }
                if (mode == COPET_MODE_DESK) { redraw = true; }
            } else {
                desk_mode_set_motion_sample(&desk, false,
                                            0, 0, 0, 0, 0, 0,
                                            (uint32_t)now_ms);
            }
            next_mpu6050_read_us = now_us + 100000;
        }

        if (mode == COPET_MODE_FOCUS) {
            const bool was_break = focus.break_phase;
            const focus_timer_state_t previous_state = focus.state;
            if (focus_mode_tick(&focus, now_us)) {
                if (previous_state == FOCUS_TIMER_RUNNING &&
                    focus.state == FOCUS_TIMER_READY) {
                    if (focus.break_phase && !was_break) {
                        ESP_LOGI(TAG,
                                 "Work complete; break ready, sessions=%lu",
                                 (unsigned long)focus.sessions);
                    } else if (!focus.break_phase && was_break) {
                        ESP_LOGI(TAG, "Break complete; work ready");
                    }
                    play_audio_event(audio_available,
                                     COPET_AUDIO_FOCUS_COMPLETE);
                }
                redraw = true;
            }
        }

        const copet_ble_status_t ble_status =
            ble_available ? copet_ble_get_status() : COPET_BLE_ERROR;
        if (ble_status != displayed_ble_status) {
            displayed_ble_status = ble_status;
            if (mode == COPET_MODE_PHONE_BRIDGE) {
                redraw = true;
            }
        }
        if (copet_ble_take_message(ble_message, sizeof(ble_message))) {
            ESP_LOGI(TAG, "Phone message displayed: %s", ble_message);
            if (mode == COPET_MODE_PHONE_BRIDGE) {
                redraw = true;
            }
        }

        desk_mode_update(&desk, (uint32_t)now_ms);
        const desk_mode_view_t *desk_view = desk_mode_get_view(&desk);
        if ((int)desk_view->expression != displayed_expression ||
            (int)desk_view->vibe != displayed_vibe) {
            displayed_expression = desk_view->expression;
            displayed_vibe = desk_view->vibe;
            ESP_LOGI(TAG, "Desk emotion: %s, effect: %s, idle=%lu s",
                     desk_mode_expression_label(desk_view->expression),
                     desk_mode_vibe_label(desk_view->vibe),
                     (unsigned long)desk_view->inactivity_seconds);
        }
        const bool animate_menu_behavior =
            mode == COPET_MODE_MENU &&
            displayed_behavior > COPET_BEHAVIOR_NEUTRAL;
        if ((mode == COPET_MODE_DESK || mode == COPET_MODE_FOCUS ||
             mode == COPET_MODE_SETTINGS || animate_menu_behavior) &&
            now_us >= next_desk_frame_us) {
            next_desk_frame_us = now_us + DESK_FRAME_INTERVAL_US;
            redraw = true;
        }

        const wifi_service_status_t wifi_status =
            wifi_service_get_status();
        if (wifi_status != displayed_wifi_status) {
            displayed_wifi_status = wifi_status;
            post_behavior(&behavior, COPET_BEHAVIOR_EVENT_WIFI_CHANGED,
                          wifi_status_is_connecting(wifi_status) ? 1 : 0,
                          (uint32_t)now_ms);
            ESP_LOGI(TAG, "Wi-Fi UI status: %s",
                     wifi_service_status_label(wifi_status));
            if (mode == COPET_MODE_DESK || mode == COPET_MODE_MENU ||
                mode == COPET_MODE_SETTINGS) {
                redraw = true;
            }
        }

        weather_service_get_snapshot(&weather);
        if (weather.revision != displayed_weather_revision) {
            displayed_weather_revision = weather.revision;
            ESP_LOGI(TAG, "Weather UI status: %s",
                     weather_service_status_label(weather.status));
            if (mode == COPET_MODE_DESK || mode == COPET_MODE_SETTINGS) {
                redraw = true;
            }
        }

        const int32_t encoder_position = s_encoder_position;
        const uint8_t encoder_ab = s_encoder_ab;
        if (encoder_position != displayed_encoder_position ||
            encoder_ab != displayed_encoder_ab) {
            const bool real_step =
                encoder_position != displayed_encoder_position &&
                displayed_encoder_position != INT32_MIN;
            if (real_step) {
                desk_mode_on_activity(&desk, (uint32_t)now_ms);
                const int32_t behavior_steps =
                    encoder_position - displayed_encoder_position;
                post_behavior(
                    &behavior,
                    behavior_steps < 0
                        ? COPET_BEHAVIOR_EVENT_ENCODER_LEFT
                        : COPET_BEHAVIOR_EVENT_ENCODER_RIGHT,
                    behavior_steps, (uint32_t)now_ms);
            }
            if (real_step && mode == COPET_MODE_DESK) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                menu_mode_scroll(&menu, logical_steps);
                mode = COPET_MODE_MENU;
                menu_last_activity_us = now_us;
                play_audio_event(audio_available, COPET_AUDIO_MENU_MOVE);
                ESP_LOGI(TAG, "Mode: MENU, selected: %s",
                         menu_mode_selected(&menu)->label);
                force_redraw = true;
            } else if (real_step && mode == COPET_MODE_MENU) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                menu_mode_scroll(&menu, logical_steps);
                play_audio_event(audio_available, COPET_AUDIO_MENU_MOVE);
                ESP_LOGI(TAG, "Menu selected: %s",
                         menu_mode_selected(&menu)->label);
                menu_last_activity_us = now_us;
                force_redraw = true;
            }
            displayed_encoder_position = encoder_position;
            displayed_encoder_ab = encoder_ab;
        }

        if (mode == COPET_MODE_MENU && menu_last_activity_us > 0 &&
            now_us - menu_last_activity_us >= MENU_TIMEOUT_US) {
            mode = COPET_MODE_DESK;
            next_desk_frame_us = 0;
            ESP_LOGI(TAG, "Menu timeout; Mode: DESK");
            force_redraw = true;
        }

        const copet_behavior_focus_state_t current_focus_state =
            behavior_focus_state(mode, &focus);
        if (current_focus_state != published_focus_state) {
            published_focus_state = current_focus_state;
            post_behavior(&behavior, COPET_BEHAVIOR_EVENT_FOCUS_CHANGED,
                          current_focus_state, (uint32_t)now_ms);
        }
        const copet_behavior_context_t behavior_context = {
            .desk_active = mode == COPET_MODE_DESK,
        };
        copet_behavior_update(&behavior, &behavior_context,
                              (uint32_t)now_ms);
        const copet_behavior_view_t *behavior_view =
            copet_behavior_get_view(&behavior);
        if ((int)behavior_view->id != displayed_behavior) {
            const char *old_label = displayed_behavior < 0
                ? "START"
                : copet_behavior_label(
                      (copet_behavior_id_t)displayed_behavior);
            ESP_LOGI(TAG, "behavior: %s -> %s source=%s priority=P%u",
                     old_label, copet_behavior_label(behavior_view->id),
                     copet_behavior_source_label(behavior_view->source),
                     (unsigned)behavior_view->priority);
            displayed_behavior = behavior_view->id;
            redraw = true;
        }

        if (redraw || force_redraw) {
            render_active_mode(mode, &desk, &menu, &focus, &settings,
                               behavior_view,
                               wifi_service_status_label(wifi_status),
                               &weather, desk_environment, ble_status,
                               ble_message);
            ESP_ERROR_CHECK(lcd_refresh());
            force_redraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

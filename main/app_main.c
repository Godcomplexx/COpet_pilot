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

#include "drivers/audio_loopback.h"
#include "drivers/copet_ble.h"
#include "drivers/mpu6050.h"
#include "drivers/touch_button.h"
#include "modes/desk_mode.h"
#include "ui/desk_ui.h"

/*
 * CoPet hardware test for the ESP32-WROOM-32 DevKit shown in the photos.
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
    ANIMATION_FRAME_COUNT = 7,
    ANIMATION_FRAME_BYTES = LCD_WIDTH * LCD_HEIGHT / 4,
    ANIMATION_FRAME_INTERVAL_US = 200000,
    DESK_FRAME_INTERVAL_US = 125000,
    MENU_TIMEOUT_US = 10000000,
};

typedef enum {
    COPET_MODE_BOOT,
    COPET_MODE_MENU,
    COPET_MODE_DESK,
    COPET_MODE_FOCUS,
    COPET_MODE_PHONE_BRIDGE,
    COPET_MODE_MINI_TV,
    COPET_MODE_OUTDOOR,
    COPET_MODE_SETTINGS,
    COPET_MODE_SLEEP,
    COPET_MODE_AUDIO_TEST,
    COPET_MODE_ANIMATION,
    COPET_MODE_CREDITS,
} copet_mode_t;

typedef enum {
    FOCUS_TIMER_READY,
    FOCUS_TIMER_RUNNING,
    FOCUS_TIMER_PAUSED,
} focus_timer_state_t;

typedef struct {
    const char *label;
    copet_mode_t mode;
} menu_item_t;

static const menu_item_t MENU_ITEMS[] = {
    {"FOCUS MODE", COPET_MODE_FOCUS},
    {"PHONE BRIDGE", COPET_MODE_PHONE_BRIDGE},
    {"AUDIO LOOPBACK", COPET_MODE_AUDIO_TEST},
    {"ANIMATION", COPET_MODE_ANIMATION},
    {"INPUT TEST", COPET_MODE_SETTINGS},
    {"CREDITS", COPET_MODE_CREDITS},
};

enum {
    FOCUS_WORK_SECONDS = 25 * 60,
    FOCUS_BREAK_SECONDS = 5 * 60,
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

extern const uint8_t s_animation_data_start[]
    asm("_binary_copet_animation_2bpp_bin_start");
extern const uint8_t s_animation_data_end[]
    asm("_binary_copet_animation_2bpp_bin_end");

static volatile int32_t s_encoder_position;
static volatile uint8_t s_encoder_ab;

typedef struct {
    char character;
    uint8_t rows[5];
} glyph_t;

/* Compact 3x5 font. Each row uses the low three bits. */
static const glyph_t FONT[] = {
    {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}},
    {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {3, 4, 5, 5, 3}}, {'H', {5, 5, 7, 5, 5}},
    {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 2}},
    {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 7, 5, 5}}, {'N', {5, 7, 7, 7, 5}},
    {'O', {2, 5, 5, 5, 2}}, {'P', {6, 5, 6, 4, 4}},
    {'Q', {2, 5, 5, 3, 1}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {3, 4, 2, 1, 6}}, {'T', {7, 2, 2, 2, 2}},
    {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}},
    {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}},
    {'2', {6, 1, 7, 4, 7}}, {'3', {6, 1, 3, 1, 6}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 6, 1, 6}},
    {'6', {3, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 6}},
    {'-', {0, 0, 7, 0, 0}}, {'.', {0, 0, 0, 0, 2}},
    {':', {0, 2, 0, 2, 0}}, {'>', {4, 2, 1, 2, 4}},
    {'/', {1, 1, 2, 4, 4}},
    {'(', {1, 2, 2, 2, 1}}, {')', {4, 2, 2, 2, 4}},
    {' ', {0, 0, 0, 0, 0}},
};

static screen_color_t rgb332(uint8_t red, uint8_t green, uint8_t blue)
{
    return (screen_color_t)((red & 0xE0U) |
                            ((green & 0xE0U) >> 3) |
                            (blue >> 6));
}

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

static void fill_rect(int x, int y, int width, int height,
                      screen_color_t color)
{
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > LCD_WIDTH) {
        width = LCD_WIDTH - x;
    }
    if (y + height > LCD_HEIGHT) {
        height = LCD_HEIGHT - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int row = y; row < y + height; ++row) {
        screen_color_t *destination =
            &s_framebuffer[row * LCD_WIDTH + x];
        for (int column = 0; column < width; ++column) {
            destination[column] = color;
        }
    }
}

static const uint8_t *find_glyph(char character)
{
    for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); ++i) {
        if (FONT[i].character == character) {
            return FONT[i].rows;
        }
    }
    return FONT[sizeof(FONT) / sizeof(FONT[0]) - 1].rows;
}

static void draw_text(int x, int y, const char *text, int scale,
                      screen_color_t color)
{
    while (*text != '\0') {
        const uint8_t *rows = find_glyph(*text++);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((rows[row] & (1U << (2 - column))) != 0) {
                    fill_rect(x + column * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
        x += 4 * scale;
    }
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

static void lcd_color_test(void)
{
    const struct {
        const char *name;
        screen_color_t color;
    } colors[] = {
        {"RED", rgb332(255, 0, 0)},
        {"GREEN", rgb332(0, 255, 0)},
        {"BLUE", rgb332(0, 0, 255)},
        {"WHITE", rgb332(255, 255, 255)},
    };

    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        ESP_LOGI(TAG, "LCD color test: %s", colors[i].name);
        fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, colors[i].color);
        ESP_ERROR_CHECK(lcd_refresh());
        vTaskDelay(pdMS_TO_TICKS(1000));
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

static void render_menu(size_t selected_item)
{
    const screen_color_t background = rgb332(8, 14, 24);
    const screen_color_t header = rgb332(20, 90, 150);
    const screen_color_t white = rgb332(245, 248, 250);
    const screen_color_t cyan = rgb332(70, 210, 230);
    const screen_color_t muted = rgb332(110, 130, 145);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(16, 8, "COPET MENU", 4, white);

    for (size_t index = 0;
         index < sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
         ++index) {
        const int y = 44 + (int)index * 28;
        if (index == selected_item) {
            fill_rect(8, y - 5, 224, 25, rgb332(18, 70, 105));
            draw_text(16, y, ">", 2, cyan);
        }
        draw_text(36, y, MENU_ITEMS[index].label, 2,
                  index == selected_item ? white : muted);
    }

    draw_text(12, 218, "TURN AND TOUCH", 2, cyan);
}

static void render_desk(const desk_mode_t *desk)
{
    desk_ui_render(s_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                   desk_mode_get_view(desk));
}

static void render_input_test(int32_t encoder_position,
                              uint8_t encoder_ab,
                              const char *touch_status)
{
    const screen_color_t background = rgb332(8, 14, 24);
    const screen_color_t header = rgb332(20, 90, 150);
    const screen_color_t white = rgb332(245, 248, 250);
    const screen_color_t cyan = rgb332(70, 210, 230);
    const screen_color_t green = rgb332(60, 210, 120);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(16, 8, "INPUT TEST", 4, white);

    char line[20];
    snprintf(line, sizeof(line), "ENC %ld", (long)encoder_position);
    draw_text(12, 64, line, 3, white);
    snprintf(line, sizeof(line), "AB %u%u",
             (encoder_ab >> 1) & 1U, encoder_ab & 1U);
    draw_text(12, 98, line, 3, cyan);
    draw_text(12, 140, touch_status, 3, green);
    draw_text(12, 202, "HOLD TOUCH BACK", 2, cyan);
}

static const char *focus_status(bool break_phase,
                                focus_timer_state_t state)
{
    if (state == FOCUS_TIMER_RUNNING) {
        return break_phase ? "BREAK RUN" : "WORK RUN";
    }
    if (state == FOCUS_TIMER_PAUSED) {
        return break_phase ? "BREAK PAUSE" : "WORK PAUSE";
    }
    return break_phase ? "BREAK READY" : "WORK READY";
}

static void render_focus(uint32_t remaining_seconds,
                         bool break_phase,
                         focus_timer_state_t state,
                         uint32_t sessions)
{
    const screen_color_t background = rgb332(8, 14, 24);
    const screen_color_t header = rgb332(20, 90, 150);
    const screen_color_t white = rgb332(245, 248, 250);
    const screen_color_t cyan = rgb332(70, 210, 230);
    const screen_color_t green = rgb332(60, 210, 120);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(16, 8, "FOCUS MODE", 4, white);
    draw_text(12, 50, focus_status(break_phase, state), 3, green);

    char line[20];
    const uint32_t minutes = remaining_seconds / 60U;
    const uint32_t seconds = remaining_seconds % 60U;
    snprintf(line, sizeof(line), "%02lu:%02lu",
             (unsigned long)minutes, (unsigned long)seconds);
    draw_text(40, 88, line, 8, white);

    snprintf(line, sizeof(line), "SESSIONS %lu",
             (unsigned long)sessions);
    draw_text(12, 148, line, 2, cyan);
    draw_text(12, 178, "TOUCH START PAUSE", 2, white);
    draw_text(12, 208, "HOLD BACK", 2, cyan);
}

static const char *ble_status_text(copet_ble_status_t status)
{
    switch (status) {
    case COPET_BLE_STARTING:
        return "BLE STARTING";
    case COPET_BLE_ADVERTISING:
        return "ADVERTISING";
    case COPET_BLE_CONNECTED:
        return "CONNECTED";
    case COPET_BLE_ERROR:
        return "BLE ERROR";
    case COPET_BLE_OFF:
    default:
        return "BLE OFF";
    }
}

static void draw_message_lines(const char *message,
                               int x,
                               int y,
                               screen_color_t color)
{
    enum {
        CHARACTERS_PER_LINE = 27,
        LINE_COUNT = 2,
    };

    size_t offset = 0;
    for (int row = 0; row < LINE_COUNT && message[offset] != '\0'; ++row) {
        char line[CHARACTERS_PER_LINE + 1];
        size_t length = 0;
        while (length < CHARACTERS_PER_LINE &&
               message[offset + length] != '\0') {
            line[length] = message[offset + length];
            ++length;
        }
        line[length] = '\0';
        draw_text(x, y + row * 24, line, 2, color);
        offset += length;
    }
}

static void render_phone_bridge(copet_ble_status_t status,
                                const char *received_message)
{
    const screen_color_t background = rgb332(8, 14, 24);
    const screen_color_t header = rgb332(20, 90, 150);
    const screen_color_t white = rgb332(245, 248, 250);
    const screen_color_t cyan = rgb332(70, 210, 230);
    const screen_color_t green = rgb332(60, 210, 120);
    const screen_color_t red = rgb332(235, 70, 70);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(8, 8, "PHONE BRIDGE", 4, white);

    draw_text(12, 52, ble_status_text(status), 3,
              status == COPET_BLE_ERROR ? red : green);
    draw_text(12, 86, "RX FFF1", 2, cyan);
    draw_message_lines(received_message, 12, 112, white);
    draw_text(12, 166, "SERVICE FFF0", 2, cyan);
    draw_text(12, 188, "WRITE TEXT TO FFF1", 2, white);
    draw_text(12, 208, "HOLD BACK", 2, cyan);
}

static const char *audio_status_text(audio_loopback_status_t status)
{
    switch (status) {
    case AUDIO_LOOPBACK_STARTING:
        return "STARTING";
    case AUDIO_LOOPBACK_OUTPUT_TEST:
        return "OUTPUT TEST";
    case AUDIO_LOOPBACK_RUNNING:
        return "LOOPBACK ON";
    case AUDIO_LOOPBACK_ERROR:
        return "AUDIO ERROR";
    case AUDIO_LOOPBACK_OFF:
    default:
        return "AUDIO OFF";
    }
}

static void render_audio_loopback(audio_loopback_status_t status,
                                  uint8_t level)
{
    const screen_color_t background = rgb332(8, 14, 24);
    const screen_color_t header = rgb332(20, 90, 150);
    const screen_color_t white = rgb332(245, 248, 250);
    const screen_color_t cyan = rgb332(70, 210, 230);
    const screen_color_t green = rgb332(60, 210, 120);
    const screen_color_t red = rgb332(235, 70, 70);
    const screen_color_t muted = rgb332(45, 60, 72);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(16, 8, "AUDIO LOOP", 4, white);
    draw_text(12, 54, audio_status_text(status), 3,
              status == AUDIO_LOOPBACK_ERROR ? red : green);
    draw_text(12, 92, "SPEAK TO MIC", 3, cyan);

    char line[20];
    snprintf(line, sizeof(line), "LEVEL %u", level);
    draw_text(12, 128, line, 3, white);
    fill_rect(12, 164, 216, 18, muted);
    fill_rect(12, 164, (216 * level) / 100, 18, green);
    draw_text(12, 208, "HOLD BACK", 2, cyan);
}

static void render_animation(size_t frame_index)
{
    const screen_color_t shades[] = {
        rgb332(0, 0, 0),
        rgb332(95, 115, 130),
        rgb332(190, 220, 235),
        rgb332(255, 255, 255),
    };
    const uint8_t *frame =
        s_animation_data_start + frame_index * ANIMATION_FRAME_BYTES;

    for (size_t packed_index = 0;
         packed_index < ANIMATION_FRAME_BYTES;
         ++packed_index) {
        const uint8_t packed = frame[packed_index];
        const size_t pixel_index = packed_index * 4;
        s_framebuffer[pixel_index] = shades[(packed >> 6) & 0x03U];
        s_framebuffer[pixel_index + 1] = shades[(packed >> 4) & 0x03U];
        s_framebuffer[pixel_index + 2] = shades[(packed >> 2) & 0x03U];
        s_framebuffer[pixel_index + 3] = shades[packed & 0x03U];
    }
}

static void render_credits(void)
{
    const screen_color_t background = rgb332(8, 14, 24);
    const screen_color_t header = rgb332(20, 90, 150);
    const screen_color_t white = rgb332(245, 248, 250);
    const screen_color_t cyan = rgb332(70, 210, 230);
    const screen_color_t muted = rgb332(110, 130, 145);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(16, 8, "CREDITS", 4, white);
    draw_text(12, 50, "ORIGINALLY CREATED BY", 2, muted);
    draw_text(12, 70, "HAMZA YESILMEN", 3, white);
    draw_text(12, 102, "(HAMZAYSLMN)", 3, cyan);
    draw_text(12, 136, "SOURCE GITHUB.COM/", 2, muted);
    draw_text(12, 154, "HAMZAYSLMN", 2, white);
    draw_text(12, 178, "GITHUB.COM/SPONSORS/", 2, muted);
    draw_text(12, 196, "HAMZAYSLMN", 2, white);
    draw_text(12, 220, "HOLD BACK", 2, cyan);
}

static void render_mode(copet_mode_t mode,
                        size_t selected_item,
                        const desk_mode_t *desk,
                        int32_t encoder_position,
                        uint8_t encoder_ab,
                        const char *touch_status,
                        uint32_t focus_remaining_seconds,
                        bool focus_break_phase,
                        focus_timer_state_t focus_state,
                        uint32_t focus_sessions,
                        copet_ble_status_t ble_status,
                        const char *ble_message,
                        audio_loopback_status_t audio_status,
                        uint8_t audio_level,
                        size_t animation_frame)
{
    switch (mode) {
    case COPET_MODE_DESK:
        render_desk(desk);
        break;
    case COPET_MODE_SETTINGS:
        render_input_test(encoder_position, encoder_ab, touch_status);
        break;
    case COPET_MODE_FOCUS:
        render_focus(focus_remaining_seconds, focus_break_phase,
                     focus_state, focus_sessions);
        break;
    case COPET_MODE_PHONE_BRIDGE:
        render_phone_bridge(ble_status, ble_message);
        break;
    case COPET_MODE_AUDIO_TEST:
        render_audio_loopback(audio_status, audio_level);
        break;
    case COPET_MODE_ANIMATION:
        render_animation(animation_frame);
        break;
    case COPET_MODE_CREDITS:
        render_credits();
        break;
    case COPET_MODE_MENU:
    default:
        render_menu(selected_item);
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CoPet Desk + menu + hardware test");
    ESP_LOGI(TAG, "Board target: ESP32-WROOM-32");

    ESP_ERROR_CHECK(lcd_init());
    lcd_color_test();
    ESP_ERROR_CHECK(i2c_init());
    ESP_ERROR_CHECK(touch_button_init());
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
    const esp_err_t ble_init_result = copet_ble_init();
    const bool ble_available = ble_init_result == ESP_OK;
    if (!ble_available) {
        ESP_LOGE(TAG, "Phone Bridge unavailable: %s",
                 esp_err_to_name(ble_init_result));
    }
    const esp_err_t audio_init_result = audio_loopback_init();
    const bool audio_available = audio_init_result == ESP_OK;
    if (!audio_available) {
        ESP_LOGE(TAG, "Audio loopback unavailable: %s",
                 esp_err_to_name(audio_init_result));
    }

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
    const char *touch_status = "TOUCH READY";
    copet_mode_t mode = COPET_MODE_DESK;
    size_t selected_item = 0;
    bool force_redraw = true;
    desk_mode_t desk;
    uint32_t focus_remaining_seconds = FOCUS_WORK_SECONDS;
    uint32_t focus_sessions = 0;
    bool focus_break_phase = false;
    focus_timer_state_t focus_state = FOCUS_TIMER_READY;
    int64_t focus_last_tick_us = 0;
    copet_ble_status_t displayed_ble_status = COPET_BLE_OFF;
    char ble_message[65] = "NO MESSAGE";
    int64_t next_audio_ui_us = 0;
    size_t animation_frame = 0;
    int64_t next_animation_frame_us = 0;
    int64_t next_desk_frame_us = 0;
    int64_t menu_last_activity_us = 0;
    int64_t next_mpu6050_read_us = 0;
    desk_motion_event_t displayed_motion_event = DESK_MOTION_NONE;
    int displayed_expression = -1;
    int displayed_vibe = -1;

    desk_mode_init(&desk, (uint32_t)(esp_timer_get_time() / 1000));

    ESP_ERROR_CHECK((size_t)(s_animation_data_end - s_animation_data_start) ==
                    ANIMATION_FRAME_COUNT * ANIMATION_FRAME_BYTES
                        ? ESP_OK
                        : ESP_ERR_INVALID_SIZE);

    ESP_LOGI(TAG, "Mode: DESK");

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        const int64_t now_ms = now_us / 1000;
        bool redraw = false;

        const touch_button_event_t touch_event =
            touch_button_poll(now_ms);
        if (touch_event == TOUCH_BUTTON_EVENT_SHORT) {
            ESP_LOGI(TAG, "TOUCH_SHORT");
            touch_status = "TOUCH SHORT";
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
            if (mode == COPET_MODE_DESK) {
                desk_mode_on_touch(&desk, (uint32_t)now_ms);
                ESP_LOGI(TAG, "Desk touch reaction: %s",
                         desk_mode_touch_reaction_label(
                             desk_mode_get_view(&desk)->touch_reaction));
            } else if (mode == COPET_MODE_MENU) {
                mode = MENU_ITEMS[selected_item].mode;
                ESP_LOGI(TAG, "Mode opened: %s",
                         MENU_ITEMS[selected_item].label);
                if (mode == COPET_MODE_PHONE_BRIDGE && ble_available) {
                    const esp_err_t result = copet_ble_start();
                    if (result != ESP_OK) {
                        ESP_LOGE(TAG, "BLE advertising failed: %s",
                                 esp_err_to_name(result));
                    }
                } else if (mode == COPET_MODE_AUDIO_TEST &&
                           audio_available) {
                    audio_loopback_start();
                } else if (mode == COPET_MODE_ANIMATION) {
                    animation_frame = 0;
                    next_animation_frame_us =
                        now_us + ANIMATION_FRAME_INTERVAL_US;
                }
            } else if (mode == COPET_MODE_FOCUS) {
                if (focus_state == FOCUS_TIMER_RUNNING) {
                    focus_state = FOCUS_TIMER_PAUSED;
                    ESP_LOGI(TAG, "Focus timer paused at %lu seconds",
                             (unsigned long)focus_remaining_seconds);
                } else {
                    focus_state = FOCUS_TIMER_RUNNING;
                    focus_last_tick_us = now_us;
                    ESP_LOGI(TAG, "Focus timer started: %s",
                             focus_break_phase ? "BREAK" : "WORK");
                }
            }
            force_redraw = true;
        } else if (touch_event == TOUCH_BUTTON_EVENT_LONG) {
            ESP_LOGI(TAG, "TOUCH_LONG");
            touch_status = "TOUCH LONG";
            desk_mode_on_activity(&desk, (uint32_t)now_ms);
            if (mode != COPET_MODE_DESK) {
                if (mode == COPET_MODE_PHONE_BRIDGE && ble_available) {
                    copet_ble_stop();
                }
                if (mode == COPET_MODE_AUDIO_TEST && audio_available) {
                    audio_loopback_stop();
                }
                if (mode == COPET_MODE_FOCUS &&
                    focus_state == FOCUS_TIMER_RUNNING) {
                    focus_state = FOCUS_TIMER_PAUSED;
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
                    ESP_LOGI(TAG, "Motion reaction event: %d", motion_event);
                    displayed_motion_event = motion_event;
                }
                if (mode == COPET_MODE_DESK) { redraw = true; }
            } else {
                desk_mode_set_motion_sample(&desk, false,
                                            0, 0, 0, 0, 0, 0,
                                            (uint32_t)now_ms);
            }
            next_mpu6050_read_us = now_us + 100000;
        }

        if (mode == COPET_MODE_FOCUS &&
            focus_state == FOCUS_TIMER_RUNNING) {
            const uint32_t elapsed_seconds =
                (uint32_t)((now_us - focus_last_tick_us) / 1000000);
            if (elapsed_seconds > 0) {
                focus_last_tick_us +=
                    (int64_t)elapsed_seconds * 1000000;
                if (elapsed_seconds >= focus_remaining_seconds) {
                    if (!focus_break_phase) {
                        ++focus_sessions;
                        focus_break_phase = true;
                        focus_remaining_seconds = FOCUS_BREAK_SECONDS;
                        ESP_LOGI(TAG,
                                 "Work complete; break ready, sessions=%lu",
                                 (unsigned long)focus_sessions);
                    } else {
                        focus_break_phase = false;
                        focus_remaining_seconds = FOCUS_WORK_SECONDS;
                        ESP_LOGI(TAG, "Break complete; work ready");
                    }
                    focus_state = FOCUS_TIMER_READY;
                } else {
                    focus_remaining_seconds -= elapsed_seconds;
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

        const audio_loopback_status_t audio_status =
            audio_available ? audio_loopback_get_status()
                            : AUDIO_LOOPBACK_ERROR;
        const uint8_t audio_level =
            audio_available ? audio_loopback_get_level() : 0;
        if (mode == COPET_MODE_AUDIO_TEST &&
            now_us >= next_audio_ui_us) {
            next_audio_ui_us = now_us + 200000;
            redraw = true;
        }

        if (mode == COPET_MODE_ANIMATION &&
            now_us >= next_animation_frame_us) {
            animation_frame =
                (animation_frame + 1) % ANIMATION_FRAME_COUNT;
            next_animation_frame_us =
                now_us + ANIMATION_FRAME_INTERVAL_US;
            redraw = true;
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
        if (mode == COPET_MODE_DESK && now_us >= next_desk_frame_us) {
            next_desk_frame_us = now_us + DESK_FRAME_INTERVAL_US;
            redraw = true;
        }

        const int32_t encoder_position = s_encoder_position;
        const uint8_t encoder_ab = s_encoder_ab;
        if (encoder_position != displayed_encoder_position ||
            encoder_ab != displayed_encoder_ab) {
            if (encoder_position != displayed_encoder_position &&
                displayed_encoder_position != INT32_MIN) {
                desk_mode_on_activity(&desk, (uint32_t)now_ms);
            }
            if (encoder_position != displayed_encoder_position &&
                displayed_encoder_position != INT32_MIN &&
                mode == COPET_MODE_DESK) {
                mode = COPET_MODE_MENU;
                menu_last_activity_us = now_us;
                ESP_LOGI(TAG, "Mode: MENU");
                force_redraw = true;
            } else if (encoder_position != displayed_encoder_position &&
                       displayed_encoder_position != INT32_MIN &&
                       mode == COPET_MODE_MENU) {
                const int32_t logical_steps =
                    encoder_position - displayed_encoder_position;
                const size_t item_count =
                    sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
                const int32_t wrapped =
                    ((int32_t)selected_item + logical_steps) %
                    (int32_t)item_count;
                selected_item =
                    (size_t)(wrapped < 0 ? wrapped + (int32_t)item_count
                                        : wrapped);
                ESP_LOGI(TAG, "Menu selected: %s",
                         MENU_ITEMS[selected_item].label);
                menu_last_activity_us = now_us;
                force_redraw = true;
            }
            displayed_encoder_position = encoder_position;
            displayed_encoder_ab = encoder_ab;
            if (mode == COPET_MODE_SETTINGS) {
                redraw = true;
            }
        }

        if (mode == COPET_MODE_MENU && menu_last_activity_us > 0 &&
            now_us - menu_last_activity_us >= MENU_TIMEOUT_US) {
            mode = COPET_MODE_DESK;
            next_desk_frame_us = 0;
            ESP_LOGI(TAG, "Menu timeout; Mode: DESK");
            force_redraw = true;
        }

        if (redraw || force_redraw) {
            render_mode(mode, selected_item, &desk,
                        encoder_position, encoder_ab, touch_status,
                        focus_remaining_seconds, focus_break_phase,
                        focus_state, focus_sessions, ble_status, ble_message,
                        audio_status, audio_level, animation_frame);
            ESP_ERROR_CHECK(lcd_refresh());
            force_redraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

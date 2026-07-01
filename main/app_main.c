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

/*
 * CoPet hardware test for the ESP32-WROOM-32 DevKit shown in the photos.
 *
 * ST7789: SCK=18, MOSI=23, DC=16, RST=17, CS is not present.
 * SHT3x:  SDA=21, SCL=22.
 * Encoder: A=32, B=33, COM=GND.
 */

static const char *TAG = "copet_test";

enum {
    LCD_WIDTH = 240,
    LCD_HEIGHT = 240,
    LCD_PIN_SCLK = 18,
    LCD_PIN_MOSI = 23,
    LCD_PIN_DC = 16,
    LCD_PIN_RST = 17,
    I2C_PIN_SDA = 21,
    I2C_PIN_SCL = 22,
    ENCODER_PIN_A = 32,
    ENCODER_PIN_B = 33,
};

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)
#define LCD_X_GAP 0
#define LCD_Y_GAP 80

#define SHT31_ADDR_PRIMARY 0x44
#define SHT31_ADDR_SECONDARY 0x45

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_lcd_done;
static uint16_t *s_framebuffer;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sht31_primary;
static i2c_master_dev_handle_t s_sht31_secondary;

static volatile int32_t s_encoder_position;
static volatile int8_t s_encoder_direction;
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
    {':', {0, 2, 0, 2, 0}}, {' ', {0, 0, 0, 0, 0}},
};

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint16_t color =
        ((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3);

    /* The SPI panel consumes RGB565 most-significant byte first. */
    return (uint16_t)((color << 8) | (color >> 8));
}

static void fill_rect(int x, int y, int width, int height, uint16_t color)
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
        uint16_t *destination = &s_framebuffer[row * LCD_WIDTH + x];
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

static void draw_text(int x, int y, const char *text, int scale, uint16_t color)
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
        .spi_mode = 2,
        .trans_queue_depth = 1,
    };
    esp_lcd_panel_io_handle_t panel_io = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                 &io_config, &panel_io),
        TAG, "LCD panel IO initialization failed");

    s_lcd_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lcd_done != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD semaphore allocation failed");

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lcd_transfer_done,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_register_event_callbacks(panel_io, &callbacks, NULL),
        TAG, "LCD callback registration failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(panel_io, &panel_config, &s_panel),
        TAG, "ST7789 initialization failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "LCD reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true),
                        TAG, "LCD inversion setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, LCD_Y_GAP),
                        TAG, "LCD gap setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),
                        TAG, "LCD enable failed");

    s_framebuffer = heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD framebuffer allocation failed");

    return ESP_OK;
}

static esp_err_t lcd_refresh(void)
{
    /* Remove a stale notification before starting the next transfer. */
    (void)xSemaphoreTake(s_lcd_done, 0);

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT,
                                  s_framebuffer),
        TAG, "LCD transfer failed");

    ESP_RETURN_ON_FALSE(
        xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(1000)) == pdTRUE,
        ESP_ERR_TIMEOUT, TAG, "LCD transfer timeout");
    return ESP_OK;
}

static void lcd_color_test(void)
{
    const struct {
        const char *name;
        uint16_t color;
    } colors[] = {
        {"RED", rgb565(255, 0, 0)},
        {"GREEN", rgb565(0, 255, 0)},
        {"BLUE", rgb565(0, 0, 255)},
        {"WHITE", rgb565(255, 255, 255)},
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
     * Valid transitions for the Gray-code sequence:
     * 00 -> 01 -> 11 -> 10 -> 00, or the reverse sequence.
     * Contact bounce normally adds opposite transitions which cancel out.
     */
    static const int8_t transition_table[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
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

    uint8_t previous =
        ((uint8_t)gpio_get_level(ENCODER_PIN_A) << 1) |
        (uint8_t)gpio_get_level(ENCODER_PIN_B);
    s_encoder_ab = previous;

    while (true) {
        const uint8_t current =
            ((uint8_t)gpio_get_level(ENCODER_PIN_A) << 1) |
            (uint8_t)gpio_get_level(ENCODER_PIN_B);

        if (current != previous) {
            const int8_t movement =
                transition_table[(previous << 2) | current];
            s_encoder_ab = current;

            if (movement != 0) {
                s_encoder_position += movement;
                s_encoder_direction = movement;
                ESP_LOGI(TAG, "ENC %s, count=%ld, AB=%u%u",
                         movement > 0 ? "RIGHT" : "LEFT",
                         (long)s_encoder_position,
                         (current >> 1) & 1U, current & 1U);
            }
            previous = current;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void render_screen(bool sensor_ok,
                          float temperature,
                          float humidity,
                          int32_t encoder_position,
                          int8_t encoder_direction,
                          uint8_t encoder_ab)
{
    const uint16_t background = rgb565(8, 14, 24);
    const uint16_t header = rgb565(20, 90, 150);
    const uint16_t white = rgb565(245, 248, 250);
    const uint16_t cyan = rgb565(70, 210, 230);
    const uint16_t green = rgb565(60, 210, 120);
    const uint16_t red = rgb565(235, 70, 70);
    const uint16_t muted = rgb565(110, 130, 145);

    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, background);
    fill_rect(0, 0, LCD_WIDTH, 36, header);
    draw_text(16, 8, "COPET TEST", 4, white);

    char line[20];
    if (sensor_ok) {
        draw_text(12, 52, "SHT OK", 3, green);

        snprintf(line, sizeof(line), "TEMP %.1F C", (double)temperature);
        draw_text(12, 78, line, 3, white);

        snprintf(line, sizeof(line), "HUM %.1F", (double)humidity);
        draw_text(12, 104, line, 3, cyan);
    } else {
        draw_text(12, 52, "SHT ERROR", 3, red);
        draw_text(12, 82, "CHECK 21 22", 3, white);
    }

    fill_rect(10, 134, 220, 2, muted);

    snprintf(line, sizeof(line), "ENC %ld", (long)encoder_position);
    draw_text(12, 150, line, 3, white);

    snprintf(line, sizeof(line), "AB %u%u",
             (encoder_ab >> 1) & 1U, encoder_ab & 1U);
    draw_text(12, 176, line, 3, cyan);

    const char *direction = "ROLL WHEEL";
    if (encoder_direction > 0) {
        direction = "RIGHT";
    } else if (encoder_direction < 0) {
        direction = "LEFT";
    }
    draw_text(12, 208, direction, 3,
              encoder_direction == 0 ? muted : green);
}

void app_main(void)
{
    ESP_LOGI(TAG, "CoPet display + SHT3x + encoder test");
    ESP_LOGI(TAG, "Board target: ESP32-WROOM-32");

    ESP_ERROR_CHECK(lcd_init());
    lcd_color_test();
    ESP_ERROR_CHECK(i2c_init());

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

    while (true) {
        const int64_t now_us = esp_timer_get_time();
        bool redraw = false;

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
            next_sensor_read_us = now_us + 2000000;
            redraw = true;
        }

        const int32_t encoder_position = s_encoder_position;
        const uint8_t encoder_ab = s_encoder_ab;
        if (encoder_position != displayed_encoder_position ||
            encoder_ab != displayed_encoder_ab) {
            displayed_encoder_position = encoder_position;
            displayed_encoder_ab = encoder_ab;
            redraw = true;
        }

        if (redraw) {
            render_screen(sensor_ok, temperature, humidity,
                          encoder_position, s_encoder_direction, encoder_ab);
            ESP_ERROR_CHECK(lcd_refresh());
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

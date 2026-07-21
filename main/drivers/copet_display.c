#include "drivers/copet_display.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum {
    LCD_PIN_SCLK = 18,
    LCD_PIN_MOSI = 5,
    LCD_PIN_DC = 16,
    LCD_PIN_RST = 17,
    LCD_TRANSFER_ROWS = 16,
};

#define LCD_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)
#define LCD_X_GAP 0
#define LCD_Y_GAP 0

static const char *TAG = "copet_display";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static SemaphoreHandle_t s_lcd_done;
static uint8_t *s_framebuffer;
static uint16_t *s_lcd_transfer_buffer;

static uint16_t rgb332_to_panel_rgb565(uint8_t color)
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

esp_err_t copet_display_init(void)
{
    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz =
            COPET_DISPLAY_WIDTH * COPET_DISPLAY_HEIGHT * sizeof(uint16_t),
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
        COPET_DISPLAY_WIDTH * COPET_DISPLAY_HEIGHT * sizeof(uint8_t),
        MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD framebuffer allocation failed");

    s_lcd_transfer_buffer = heap_caps_malloc(
        COPET_DISPLAY_WIDTH * LCD_TRANSFER_ROWS * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_lcd_transfer_buffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD transfer buffer allocation failed");

    ESP_LOGI(TAG, "LCD buffers: framebuffer=%u bytes, DMA stripe=%u bytes",
             COPET_DISPLAY_WIDTH * COPET_DISPLAY_HEIGHT * (unsigned)sizeof(uint8_t),
             COPET_DISPLAY_WIDTH * LCD_TRANSFER_ROWS *
                 (unsigned)sizeof(uint16_t));
    return ESP_OK;
}

uint8_t *copet_display_framebuffer(void)
{
    return s_framebuffer;
}

esp_err_t copet_display_refresh(void)
{
    for (int y = 0; y < COPET_DISPLAY_HEIGHT; y += LCD_TRANSFER_ROWS) {
        const int rows =
            y + LCD_TRANSFER_ROWS <= COPET_DISPLAY_HEIGHT
                ? LCD_TRANSFER_ROWS
                : COPET_DISPLAY_HEIGHT - y;
        const int pixel_count = COPET_DISPLAY_WIDTH * rows;
        const int source_offset = y * COPET_DISPLAY_WIDTH;

        for (int pixel = 0; pixel < pixel_count; ++pixel) {
            s_lcd_transfer_buffer[pixel] =
                rgb332_to_panel_rgb565(
                    s_framebuffer[source_offset + pixel]);
        }

        /* Remove a stale notification before starting the next stripe. */
        (void)xSemaphoreTake(s_lcd_done, 0);
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(
                s_panel, 0, y, COPET_DISPLAY_WIDTH, y + rows,
                s_lcd_transfer_buffer),
            TAG, "LCD stripe transfer failed");
        ESP_RETURN_ON_FALSE(
            xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(1000)) == pdTRUE,
            ESP_ERR_TIMEOUT, TAG, "LCD stripe transfer timeout");
    }
    return ESP_OK;
}

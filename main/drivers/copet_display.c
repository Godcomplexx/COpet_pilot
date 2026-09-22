#include "drivers/copet_display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

enum {
    LCD_PIN_SCLK = 18,
    LCD_PIN_MOSI = 5,
    LCD_PIN_DC = 16,
    LCD_PIN_RST = 17,
};

#define LCD_HOST SPI2_HOST
/* Conservative bring-up clock for the current long flying-wire assembly. */
#define LCD_PIXEL_CLOCK_HZ (1 * 1000 * 1000)
#define LCD_STRIPE_BYTES (COPET_PANEL_WIDTH * COPET_PANEL_STRIPE_ROWS * 2)

static const char *TAG = "copet_display";
static esp_lcd_panel_io_handle_t s_panel_io;
static SemaphoreHandle_t s_lcd_done;
static uint8_t *s_framebuffer;
static uint8_t *s_lcd_transfer_buffer;

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

static esp_err_t lcd_command(int command, const void *data, size_t length)
{
    return esp_lcd_panel_io_tx_param(s_panel_io, command, data, length);
}

static esp_err_t lcd_init_st7735(void)
{
    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << LCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "Reset GPIO setup failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_RST, 1), TAG, "Reset high failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_RST, 0), TAG, "Reset low failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_RST, 1), TAG, "Reset release failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(lcd_command(0x01, NULL, 0), TAG, "Software reset failed");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(lcd_command(0x11, NULL, 0), TAG, "Sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ST7735R/S register settings based on Adafruit's initR tables.
     * See THIRD_PARTY_LICENSES/Adafruit-ST7735-Library-NOTICE.txt.
     * This is deliberately separate from the old ST7789/GMT130 sequence.
     */
    static const struct {
        uint8_t command;
        uint8_t length;
        uint8_t data[16];
    } init[] = {
        {0xB1, 3, {0x01, 0x2C, 0x2D}},
        {0xB2, 3, {0x01, 0x2C, 0x2D}},
        {0xB3, 6, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}},
        {0xB4, 1, {0x07}},
        {0xC0, 3, {0xA2, 0x02, 0x84}},
        {0xC1, 1, {0xC5}},
        {0xC2, 2, {0x0A, 0x00}},
        {0xC3, 2, {0x8A, 0x2A}},
        {0xC4, 2, {0x8A, 0xEE}},
        {0xC5, 1, {0x0E}},
        {0x3A, 1, {0x05}}, /* RGB565, unlike ST7789's 0x55. */
        {0xE0, 16, {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}},
        {0xE1, 16, {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        ESP_RETURN_ON_ERROR(lcd_command(init[i].command, init[i].data,
                                        init[i].length), TAG,
                            "ST7735 command 0x%02X failed", init[i].command);
    }

    uint8_t madctl = 0xC0; /* Portrait, MX | MY. */
#ifdef CONFIG_COPET_LCD_FLIP
    madctl = 0;
#endif
#ifdef CONFIG_COPET_LCD_BGR
    madctl |= 0x08;
#endif
    ESP_RETURN_ON_ERROR(lcd_command(0x36, &madctl, 1), TAG, "MADCTL failed");
#ifdef CONFIG_COPET_LCD_INVERT
    const int inversion_command = 0x21;
#else
    const int inversion_command = 0x20;
#endif
    ESP_RETURN_ON_ERROR(lcd_command(inversion_command, NULL, 0), TAG,
                        "Inversion setup failed");
    ESP_RETURN_ON_ERROR(lcd_command(0x13, NULL, 0), TAG, "Normal mode failed");
    vTaskDelay(pdMS_TO_TICKS(10));
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
        .max_transfer_sz = LCD_STRIPE_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus initialization failed");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = -1, /* Physical CS must be tied to GND. Dedicated bus. */
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
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
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(
                            s_panel_io, &callbacks, NULL),
                        TAG, "LCD callback registration failed");
    ESP_RETURN_ON_ERROR(lcd_init_st7735(), TAG, "ST7735 initialization failed");

    s_framebuffer = heap_caps_calloc(COPET_DISPLAY_WIDTH * COPET_DISPLAY_HEIGHT,
                                     sizeof(uint8_t), MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD framebuffer allocation failed");
    s_lcd_transfer_buffer = heap_caps_malloc(LCD_STRIPE_BYTES,
                                             MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_lcd_transfer_buffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "LCD transfer buffer allocation failed");
    /* Clear all GRAM before enabling the panel, avoiding random boot pixels. */
    ESP_RETURN_ON_ERROR(copet_display_refresh(), TAG, "Initial clear failed");
    ESP_RETURN_ON_ERROR(lcd_command(0x29, NULL, 0), TAG, "Display on failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "ST7735: UI %dx%d -> panel %dx%d; offset %d,%d; SPI mode 0, %d Hz",
             COPET_DISPLAY_WIDTH, COPET_DISPLAY_HEIGHT,
             COPET_PANEL_WIDTH, COPET_PANEL_HEIGHT,
             CONFIG_COPET_LCD_X_OFFSET, CONFIG_COPET_LCD_Y_OFFSET,
             LCD_PIXEL_CLOCK_HZ);
    return ESP_OK;
}

uint8_t *copet_display_framebuffer(void)
{
    return s_framebuffer;
}

#ifdef CONFIG_COPET_LCD_DIAGNOSTIC
esp_err_t copet_display_diagnostic(void)
{
    /* A full native RGB565 frame, independent of the UI converter and stripe
     * windows. Keep this DMA buffer alive even if a transfer times out.
     * This function is called once in a diagnostic-only boot.
     */
    const size_t bytes = COPET_PANEL_WIDTH * COPET_PANEL_HEIGHT * 2;
    uint8_t *pixels = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_NO_MEM, TAG, "Test allocation failed");
    const uint16_t primaries[] = {0xF800, 0x07E0, 0x001F};
    const char *names[] = {"RED", "GREEN", "BLUE", "GRID"};
    for (int pattern = 0; pattern < 4; ++pattern) {
        for (int y = 0; y < COPET_PANEL_HEIGHT; ++y) {
            for (int x = 0; x < COPET_PANEL_WIDTH; ++x) {
                uint16_t color;
                if (pattern < 3) {
                    color = primaries[pattern];
                } else {
                    color = y < 80 ? primaries[x * 3 / COPET_PANEL_WIDTH] :
                        ((x % 16 == 0 || y % 16 == 0) ? 0xFFFF : 0);
                    if (x < 2 || x >= COPET_PANEL_WIDTH - 2 ||
                        y < 2 || y >= COPET_PANEL_HEIGHT - 2) color = 0xFFFF;
                    if (x >= 4 && x < 12 && y >= 4 && y < 12) color = 0xFFE0;
                    if (x >= 116 && x < 124 && y >= 148 && y < 156) color = 0xF81F;
                }
                const size_t offset = (y * COPET_PANEL_WIDTH + x) * 2;
                pixels[offset] = color >> 8;
                pixels[offset + 1] = color & 0xFF;
            }
        }
        const unsigned x0 = CONFIG_COPET_LCD_X_OFFSET;
        const unsigned x1 = x0 + COPET_PANEL_WIDTH - 1;
        const unsigned y0 = CONFIG_COPET_LCD_Y_OFFSET;
        const unsigned y1 = y0 + COPET_PANEL_HEIGHT - 1;
        const uint8_t columns[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
        const uint8_t rows[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
        ESP_RETURN_ON_ERROR(lcd_command(0x2A, columns, sizeof(columns)), TAG,
                            "Test column window failed");
        ESP_RETURN_ON_ERROR(lcd_command(0x2B, rows, sizeof(rows)), TAG,
                            "Test row window failed");
        (void)xSemaphoreTake(s_lcd_done, 0);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_panel_io, 0x2C, pixels, bytes),
                            TAG, "Test pixels failed");
        ESP_RETURN_ON_FALSE(xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(2000)) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "Test transfer timed out");
        ESP_LOGI(TAG, "LCD TEST %s: native RGB565, one full-frame window", names[pattern]);
        if (pattern < 3) vTaskDelay(pdMS_TO_TICKS(3000));
    }
    heap_caps_free(pixels);
    return ESP_OK;
}
#endif

esp_err_t copet_display_refresh(void)
{
    ESP_RETURN_ON_FALSE(s_framebuffer && s_lcd_transfer_buffer && s_panel_io &&
                        s_lcd_done, ESP_ERR_INVALID_STATE, TAG,
                        "Display not initialized");
    for (int y = 0; y < COPET_PANEL_HEIGHT; y += COPET_PANEL_STRIPE_ROWS) {
        const int rows = y + COPET_PANEL_STRIPE_ROWS <= COPET_PANEL_HEIGHT
            ? COPET_PANEL_STRIPE_ROWS : COPET_PANEL_HEIGHT - y;
        copet_display_convert_stripe(s_framebuffer, y, rows, s_lcd_transfer_buffer);
        const unsigned x_start = CONFIG_COPET_LCD_X_OFFSET;
        const unsigned x_end = x_start + COPET_PANEL_WIDTH - 1;
        const unsigned y_start = y + CONFIG_COPET_LCD_Y_OFFSET;
        const unsigned y_end = y_start + rows - 1;
        const uint8_t columns[] = {x_start >> 8, x_start & 0xFF,
                                    x_end >> 8, x_end & 0xFF};
        const uint8_t lines[] = {y_start >> 8, y_start & 0xFF,
                                  y_end >> 8, y_end & 0xFF};
        ESP_RETURN_ON_ERROR(lcd_command(0x2A, columns, sizeof(columns)), TAG,
                            "Column address failed");
        ESP_RETURN_ON_ERROR(lcd_command(0x2B, lines, sizeof(lines)), TAG,
                            "Row address failed");
        (void)xSemaphoreTake(s_lcd_done, 0);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_panel_io, 0x2C,
                            s_lcd_transfer_buffer, COPET_PANEL_WIDTH * rows * 2),
                            TAG, "LCD stripe transfer failed");
        ESP_RETURN_ON_FALSE(xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(1000)) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "LCD stripe transfer timeout");
    }
    return ESP_OK;
}

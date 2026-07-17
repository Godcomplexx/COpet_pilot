#include "ui/diag_ui.h"
#include "ui/ui_canvas.h"

#include <stddef.h>
#include <stdio.h>

static const char *ble_status_text(copet_ble_status_t status)
{
    switch (status) {
    case COPET_BLE_STARTING: return "BLE STARTING";
    case COPET_BLE_ADVERTISING: return "ADVERTISING";
    case COPET_BLE_CONNECTED: return "CONNECTED";
    case COPET_BLE_ERROR: return "BLE ERROR";
    case COPET_BLE_OFF:
    default: return "BLE OFF";
    }
}

static void draw_message_lines(ui_canvas_t *canvas, const char *message,
                               int x, int y, ui_color_t color)
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
        ui_draw_text(canvas, x, y + row * 24, line, 2, color);
        offset += length;
    }
}

void diag_ui_render_phone_bridge(uint8_t *framebuffer, int width, int height,
                                 copet_ble_status_t status,
                                 const char *received_message)
{
    if (framebuffer == NULL || width <= 0 || height <= 0) {
        return;
    }

    ui_canvas_t canvas = {framebuffer, width, height};
    const ui_color_t background = ui_rgb332(8, 14, 24);
    const ui_color_t header = ui_rgb332(20, 90, 150);
    const ui_color_t white = ui_rgb332(245, 248, 250);
    const ui_color_t cyan = ui_rgb332(70, 210, 230);
    const ui_color_t green = ui_rgb332(60, 210, 120);
    const ui_color_t red = ui_rgb332(235, 70, 70);

    ui_fill_rect(&canvas, 0, 0, width, height, background);
    ui_fill_rect(&canvas, 0, 0, width, 36, header);
    ui_draw_text(&canvas, 8, 8, "PHONE BRIDGE", 4, white);

    ui_draw_text(&canvas, 12, 52, ble_status_text(status), 3,
                 status == COPET_BLE_ERROR ? red : green);
    ui_draw_text(&canvas, 12, 86, "RX FFF1", 2, cyan);
    draw_message_lines(&canvas,
                       received_message != NULL ? received_message : "",
                       12, 112, white);
    ui_draw_text(&canvas, 12, 166, "SERVICE FFF0", 2, cyan);
    ui_draw_text(&canvas, 12, 188, "WRITE TEXT TO FFF1", 2, white);
    ui_draw_text(&canvas, 12, 208, "HOLD BACK", 2, cyan);
}

static const char *audio_status_text(audio_loopback_status_t status)
{
    switch (status) {
    case AUDIO_LOOPBACK_STARTING: return "STARTING";
    case AUDIO_LOOPBACK_OUTPUT_TEST: return "OUTPUT TEST";
    case AUDIO_LOOPBACK_RUNNING: return "LOOPBACK ON";
    case AUDIO_LOOPBACK_ERROR: return "AUDIO ERROR";
    case AUDIO_LOOPBACK_OFF:
    default: return "AUDIO OFF";
    }
}

void diag_ui_render_audio(uint8_t *framebuffer, int width, int height,
                          audio_loopback_status_t status, uint8_t level)
{
    if (framebuffer == NULL || width <= 0 || height <= 0) {
        return;
    }

    ui_canvas_t canvas = {framebuffer, width, height};
    const ui_color_t background = ui_rgb332(8, 14, 24);
    const ui_color_t header = ui_rgb332(20, 90, 150);
    const ui_color_t white = ui_rgb332(245, 248, 250);
    const ui_color_t cyan = ui_rgb332(70, 210, 230);
    const ui_color_t green = ui_rgb332(60, 210, 120);
    const ui_color_t red = ui_rgb332(235, 70, 70);
    const ui_color_t muted = ui_rgb332(45, 60, 72);

    ui_fill_rect(&canvas, 0, 0, width, height, background);
    ui_fill_rect(&canvas, 0, 0, width, 36, header);
    ui_draw_text(&canvas, 16, 8, "AUDIO LOOP", 4, white);
    ui_draw_text(&canvas, 12, 54, audio_status_text(status), 3,
                 status == AUDIO_LOOPBACK_ERROR ? red : green);
    ui_draw_text(&canvas, 12, 92, "SPEAK TO MIC", 3, cyan);

    char line[20];
    snprintf(line, sizeof(line), "LEVEL %u", level);
    ui_draw_text(&canvas, 12, 128, line, 3, white);
    ui_fill_rect(&canvas, 12, 164, 216, 18, muted);
    ui_fill_rect(&canvas, 12, 164, (216 * level) / 100, 18, green);
    ui_draw_text(&canvas, 12, 208, "HOLD BACK", 2, cyan);
}

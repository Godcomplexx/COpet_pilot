#include "drivers/copet_audio.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum {
    AUDIO_PIN_BCLK = 26,
    AUDIO_PIN_LRC = 25,
    AUDIO_PIN_DOUT = 27,
    AUDIO_SAMPLE_RATE = 16000,
    AUDIO_BUFFER_FRAMES = 128,
    AUDIO_OUTPUT_GAIN_PERCENT = 25,
};

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    const char *name;
} audio_clip_t;

extern const uint8_t menu_confirm_start[]
    asm("_binary_menu_confirm_pcm_start");
extern const uint8_t menu_confirm_end[]
    asm("_binary_menu_confirm_pcm_end");
extern const uint8_t menu_move_start[]
    asm("_binary_menu_move_pcm_start");
extern const uint8_t menu_move_end[]
    asm("_binary_menu_move_pcm_end");
extern const uint8_t focus_start_start[]
    asm("_binary_focus_start_pcm_start");
extern const uint8_t focus_start_end[]
    asm("_binary_focus_start_pcm_end");
extern const uint8_t focus_complete_start[]
    asm("_binary_focus_complete_pcm_start");
extern const uint8_t focus_complete_end[]
    asm("_binary_focus_complete_pcm_end");

static const char *TAG = "copet_audio";
static i2s_chan_handle_t s_tx_channel;
static QueueHandle_t s_event_queue;
static volatile bool s_enabled = true;

static bool get_clip(copet_audio_event_t event, audio_clip_t *clip)
{
    if (clip == NULL) {
        return false;
    }
    switch (event) {
    case COPET_AUDIO_MENU_MOVE:
        *clip = (audio_clip_t){menu_move_start, menu_move_end, "MENU MOVE"};
        return true;
    case COPET_AUDIO_MENU_CONFIRM:
        *clip = (audio_clip_t){menu_confirm_start, menu_confirm_end,
                               "MENU CONFIRM"};
        return true;
    case COPET_AUDIO_VIEW_CHANGE:
        *clip = (audio_clip_t){menu_move_start, menu_move_end,
                               "VIEW CHANGE"};
        return true;
    case COPET_AUDIO_FOCUS_START:
        *clip = (audio_clip_t){focus_start_start, focus_start_end,
                               "FOCUS START"};
        return true;
    case COPET_AUDIO_FOCUS_PAUSE:
        *clip = (audio_clip_t){menu_confirm_start, menu_confirm_end,
                               "FOCUS PAUSE"};
        return true;
    case COPET_AUDIO_FOCUS_COMPLETE:
        *clip = (audio_clip_t){focus_complete_start, focus_complete_end,
                               "FOCUS COMPLETE"};
        return true;
    default:
        return false;
    }
}

static int16_t read_scaled_sample(const uint8_t *bytes)
{
    const int16_t sample = (int16_t)((uint16_t)bytes[0] |
                                     ((uint16_t)bytes[1] << 8));
    return (int16_t)(((int32_t)sample * AUDIO_OUTPUT_GAIN_PERCENT) / 100);
}

static void write_silence(void)
{
    int16_t silence[AUDIO_BUFFER_FRAMES * 2] = {0};
    size_t bytes_written = 0;
    (void)i2s_channel_write(s_tx_channel, silence, sizeof(silence),
                            &bytes_written, portMAX_DELAY);
}

static void audio_task(void *argument)
{
    (void)argument;

    copet_audio_event_t event;
    int16_t stereo_samples[AUDIO_BUFFER_FRAMES * 2];

    while (true) {
        xQueueReceive(s_event_queue, &event, portMAX_DELAY);
        if (!s_enabled) {
            continue;
        }

        audio_clip_t clip;
        if (!get_clip(event, &clip) || clip.end <= clip.start) {
            ESP_LOGE(TAG, "Invalid embedded audio event: %d", event);
            continue;
        }

        const esp_err_t enable_result = i2s_channel_enable(s_tx_channel);
        if (enable_result != ESP_OK) {
            ESP_LOGE(TAG, "I2S start failed for %s: %s", clip.name,
                     esp_err_to_name(enable_result));
            continue;
        }

        const size_t byte_count = (size_t)(clip.end - clip.start);
        const size_t mono_samples = byte_count / sizeof(int16_t);
        size_t sample_offset = 0;
        ESP_LOGI(TAG, "Playing %s: %u ms", clip.name,
                 (unsigned)((mono_samples * 1000U) / AUDIO_SAMPLE_RATE));

        while (sample_offset < mono_samples && s_enabled) {
            const size_t frames =
                mono_samples - sample_offset < AUDIO_BUFFER_FRAMES
                    ? mono_samples - sample_offset
                    : AUDIO_BUFFER_FRAMES;
            for (size_t frame = 0; frame < frames; ++frame) {
                const uint8_t *source =
                    clip.start + (sample_offset + frame) * sizeof(int16_t);
                const int16_t sample = read_scaled_sample(source);
                stereo_samples[frame * 2] = sample;
                stereo_samples[frame * 2 + 1] = sample;
            }

            size_t bytes_written = 0;
            const size_t bytes_to_write =
                frames * 2U * sizeof(stereo_samples[0]);
            const esp_err_t result = i2s_channel_write(
                s_tx_channel, stereo_samples, bytes_to_write,
                &bytes_written, portMAX_DELAY);
            if (result != ESP_OK || bytes_written != bytes_to_write) {
                ESP_LOGE(TAG, "PCM write failed: %s",
                         esp_err_to_name(result));
                break;
            }
            sample_offset += frames;
        }

        write_silence();
        /* One DMA buffer is 8 ms at 16 kHz; let silence reach the amp. */
        vTaskDelay(pdMS_TO_TICKS(12));
        const esp_err_t disable_result = i2s_channel_disable(s_tx_channel);
        if (disable_result != ESP_OK) {
            ESP_LOGE(TAG, "I2S stop failed after %s: %s", clip.name,
                     esp_err_to_name(disable_result));
        }
        ESP_LOGI(TAG, "%s %s", clip.name,
                 sample_offset == mono_samples ? "finished" : "stopped");
    }
}

esp_err_t copet_audio_init(void)
{
    const i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&channel_config, &s_tx_channel, NULL),
        TAG, "I2S channel allocation failed");

    const i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_PIN_BCLK,
            .ws = AUDIO_PIN_LRC,
            .dout = AUDIO_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(s_tx_channel, &standard_config),
        TAG, "I2S standard mode initialization failed");

    s_event_queue = xQueueCreate(8, sizeof(copet_audio_event_t));
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_NO_MEM, TAG,
                        "Audio event queue allocation failed");

    const BaseType_t task_created =
        xTaskCreate(audio_task, "copet_audio", 3072, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "Audio task creation failed");

    ESP_LOGI(TAG,
             "PCM audio ready: 16 kHz mono clips, BCLK=%d LRC=%d DIN=%d",
             AUDIO_PIN_BCLK, AUDIO_PIN_LRC, AUDIO_PIN_DOUT);
    return ESP_OK;
}

void copet_audio_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled && s_event_queue != NULL) {
        xQueueReset(s_event_queue);
    }
    ESP_LOGI(TAG, "Sound %s", enabled ? "enabled" : "disabled");
}

bool copet_audio_is_enabled(void)
{
    return s_enabled;
}

esp_err_t copet_audio_play_event(copet_audio_event_t event)
{
    audio_clip_t clip;
    ESP_RETURN_ON_FALSE(get_clip(event, &clip), ESP_ERR_INVALID_ARG, TAG,
                        "Unknown audio event");
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Audio is not initialized");
    if (!s_enabled) {
        return ESP_OK;
    }
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "Queued %s", clip.name);
    return ESP_OK;
}

#include "drivers/copet_audio.h"

#include <stdint.h>
#include <string.h>

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
    AUDIO_BUFFER_FRAMES = 128,
    AUDIO_AMPLITUDE_16 = 28000,
    AUDIO_AMPLITUDE_32 = 0x60000000,
};

typedef enum {
    AUDIO_PROFILE_16K_16BIT,
    AUDIO_PROFILE_44K1_16BIT,
    AUDIO_PROFILE_48K_32BIT,
} audio_profile_t;

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    audio_profile_t profile;
} tone_command_t;

static const char *TAG = "copet_audio";
static i2s_chan_handle_t s_tx_channel;
static QueueHandle_t s_tone_queue;

static uint32_t profile_sample_rate(audio_profile_t profile)
{
    switch (profile) {
    case AUDIO_PROFILE_16K_16BIT:
        return 16000;
    case AUDIO_PROFILE_44K1_16BIT:
        return 44100;
    case AUDIO_PROFILE_48K_32BIT:
        return 48000;
    default:
        return 48000;
    }
}

static i2s_data_bit_width_t profile_bit_width(audio_profile_t profile)
{
    return profile == AUDIO_PROFILE_48K_32BIT
               ? I2S_DATA_BIT_WIDTH_32BIT
               : I2S_DATA_BIT_WIDTH_16BIT;
}

static const char *profile_name(audio_profile_t profile)
{
    switch (profile) {
    case AUDIO_PROFILE_16K_16BIT:
        return "16 kHz / 16-bit Philips";
    case AUDIO_PROFILE_44K1_16BIT:
        return "44.1 kHz / 16-bit Philips";
    case AUDIO_PROFILE_48K_32BIT:
        return "48 kHz / 32-bit Philips";
    default:
        return "unknown";
    }
}

static esp_err_t audio_apply_profile(audio_profile_t profile)
{
    const i2s_std_clk_config_t clock_config =
        I2S_STD_CLK_DEFAULT_CONFIG(profile_sample_rate(profile));
    const i2s_std_slot_config_t slot_config =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            profile_bit_width(profile), I2S_SLOT_MODE_STEREO);

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_channel),
                        TAG, "I2S disable failed");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_clock(s_tx_channel, &clock_config),
        TAG, "I2S clock reconfiguration failed");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(s_tx_channel, &slot_config),
        TAG, "I2S slot reconfiguration failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel),
                        TAG, "I2S enable failed");
    return ESP_OK;
}

static void audio_task(void *argument)
{
    (void)argument;

    tone_command_t tone;
    /* Large enough for 128 stereo frames in either 16- or 32-bit mode. */
    int32_t sample_storage[AUDIO_BUFFER_FRAMES * 2];

    while (true) {
        xQueueReceive(s_tone_queue, &tone, portMAX_DELAY);

        const esp_err_t profile_result = audio_apply_profile(tone.profile);
        if (profile_result != ESP_OK) {
            ESP_LOGE(TAG, "Skipping tone: profile setup failed");
            continue;
        }

        const uint32_t sample_rate = profile_sample_rate(tone.profile);
        const i2s_data_bit_width_t bit_width =
            profile_bit_width(tone.profile);
        const uint32_t total_frames =
            (sample_rate * tone.duration_ms) / 1000U;
        const uint32_t phase_step =
            (uint32_t)(((uint64_t)tone.frequency_hz << 32) /
                       sample_rate);
        uint32_t phase = 0;
        uint32_t frames_remaining = total_frames;

        ESP_LOGI(TAG, "TEST %s: %u Hz for %u ms",
                 profile_name(tone.profile), tone.frequency_hz,
                 tone.duration_ms);

        while (frames_remaining > 0) {
            const uint32_t frame_count =
                frames_remaining < AUDIO_BUFFER_FRAMES
                    ? frames_remaining
                    : AUDIO_BUFFER_FRAMES;

            if (bit_width == I2S_DATA_BIT_WIDTH_16BIT) {
                int16_t *samples = (int16_t *)sample_storage;
                for (uint32_t frame = 0; frame < frame_count; ++frame) {
                    const int16_t sample =
                        (phase & 0x80000000U) != 0
                            ? AUDIO_AMPLITUDE_16
                            : -AUDIO_AMPLITUDE_16;
                    samples[frame * 2] = sample;
                    samples[frame * 2 + 1] = sample;
                    phase += phase_step;
                }
            } else {
                for (uint32_t frame = 0; frame < frame_count; ++frame) {
                    const int32_t sample =
                        (phase & 0x80000000U) != 0
                            ? AUDIO_AMPLITUDE_32
                            : -AUDIO_AMPLITUDE_32;
                    sample_storage[frame * 2] = sample;
                    sample_storage[frame * 2 + 1] = sample;
                    phase += phase_step;
                }
            }

            size_t bytes_written = 0;
            const size_t bytes_to_write =
                frame_count * 2 *
                (bit_width == I2S_DATA_BIT_WIDTH_16BIT
                     ? sizeof(int16_t)
                     : sizeof(int32_t));
            const esp_err_t result =
                i2s_channel_write(s_tx_channel, sample_storage,
                                  bytes_to_write,
                                  &bytes_written, portMAX_DELAY);
            if (result != ESP_OK || bytes_written != bytes_to_write) {
                ESP_LOGE(TAG, "I2S write failed: %s",
                         esp_err_to_name(result));
                break;
            }
            frames_remaining -= frame_count;
        }

        memset(sample_storage, 0, sizeof(sample_storage));
        size_t bytes_written = 0;
        const size_t silence_bytes =
            AUDIO_BUFFER_FRAMES * 2 *
            (bit_width == I2S_DATA_BIT_WIDTH_16BIT
                 ? sizeof(int16_t)
                 : sizeof(int32_t));
        (void)i2s_channel_write(s_tx_channel, sample_storage, silence_bytes,
                                &bytes_written, portMAX_DELAY);
        ESP_LOGI(TAG, "TEST finished");
        vTaskDelay(pdMS_TO_TICKS(700));
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
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
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
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel),
                        TAG, "I2S channel enable failed");

    s_tone_queue = xQueueCreate(8, sizeof(tone_command_t));
    ESP_RETURN_ON_FALSE(s_tone_queue != NULL, ESP_ERR_NO_MEM, TAG,
                        "Tone queue allocation failed");

    const BaseType_t task_created =
        xTaskCreate(audio_task, "copet_audio", 3072, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "Audio task creation failed");

    ESP_LOGI(TAG,
             "MAX98357A diagnostic ready: BCLK=%d LRC=%d DIN=%d",
             AUDIO_PIN_BCLK, AUDIO_PIN_LRC, AUDIO_PIN_DOUT);
    return ESP_OK;
}

esp_err_t copet_audio_play_tone(uint16_t frequency_hz,
                                uint16_t duration_ms)
{
    ESP_RETURN_ON_FALSE(s_tone_queue != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Audio is not initialized");
    ESP_RETURN_ON_FALSE(
        frequency_hz >= 50 && frequency_hz <= 4000 &&
            duration_ms > 0 && duration_ms <= 10000,
        ESP_ERR_INVALID_ARG, TAG, "Invalid tone parameters");

    const tone_command_t command = {
        .frequency_hz = frequency_hz,
        .duration_ms = duration_ms,
        .profile = AUDIO_PROFILE_48K_32BIT,
    };
    return xQueueSend(s_tone_queue, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t copet_audio_run_diagnostic(void)
{
    ESP_RETURN_ON_FALSE(s_tone_queue != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Audio is not initialized");

    static const tone_command_t tests[] = {
        {
            .frequency_hz = 500,
            .duration_ms = 3000,
            .profile = AUDIO_PROFILE_16K_16BIT,
        },
        {
            .frequency_hz = 1000,
            .duration_ms = 3000,
            .profile = AUDIO_PROFILE_44K1_16BIT,
        },
        {
            .frequency_hz = 1500,
            .duration_ms = 3000,
            .profile = AUDIO_PROFILE_48K_32BIT,
        },
    };

    for (size_t index = 0; index < sizeof(tests) / sizeof(tests[0]);
         ++index) {
        ESP_RETURN_ON_FALSE(
            xQueueSend(s_tone_queue, &tests[index], 0) == pdTRUE,
            ESP_ERR_TIMEOUT, TAG, "Diagnostic queue is full");
    }
    return ESP_OK;
}

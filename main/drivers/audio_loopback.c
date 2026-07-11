#include "drivers/audio_loopback.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    AUDIO_SAMPLE_RATE = 16000,
    AUDIO_PIN_BCLK = 26,
    AUDIO_PIN_WS = 25,
    AUDIO_PIN_SPEAKER_DATA = 27,
    AUDIO_PIN_MIC_DATA = 34,
    AUDIO_FRAME_COUNT = 128,
    AUDIO_SOFTWARE_GAIN = 32,
    AUDIO_PHILIPS_TEST_FREQUENCY = 1000,
    AUDIO_LEFT_JUSTIFIED_TEST_FREQUENCY = 1500,
    AUDIO_TEST_DURATION_MS = 3000,
    AUDIO_TEST_AMPLITUDE = 0x60000000,
};

static const char *TAG = "audio_loopback";
static i2s_chan_handle_t s_tx_channel;
static i2s_chan_handle_t s_rx_channel;
static volatile bool s_requested;
static volatile audio_loopback_status_t s_status = AUDIO_LOOPBACK_OFF;
static volatile uint8_t s_level;

static esp_err_t configure_rx_input(int input_pin)
{
    const i2s_std_gpio_config_t gpio_config = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = AUDIO_PIN_BCLK,
        .ws = AUDIO_PIN_WS,
        .dout = I2S_GPIO_UNUSED,
        .din = input_pin,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };
    return i2s_channel_reconfig_std_gpio(s_rx_channel, &gpio_config);
}

static int32_t amplify_and_clip(int32_t sample)
{
    const int64_t amplified = (int64_t)sample * AUDIO_SOFTWARE_GAIN;
    if (amplified > INT32_MAX) {
        return INT32_MAX;
    }
    if (amplified < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)amplified;
}

static esp_err_t enable_channels(void)
{
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_channel), TAG,
                        "RX enable failed");
    const esp_err_t result = i2s_channel_enable(s_tx_channel);
    if (result != ESP_OK) {
        (void)i2s_channel_disable(s_rx_channel);
        ESP_LOGE(TAG, "TX enable failed: %s", esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG, "Loopback running: mic D34 -> speaker D27");
    return ESP_OK;
}

static void disable_channels(void)
{
    (void)i2s_channel_disable(s_tx_channel);
    (void)i2s_channel_disable(s_rx_channel);
    s_level = 0;
    ESP_LOGI(TAG, "Loopback stopped");
}

static esp_err_t play_output_test_tone(int32_t *samples,
                                       int32_t *probe_samples,
                                       const char *format_name,
                                       uint32_t frequency_hz,
                                       bool *din_test_passed)
{
    const uint32_t total_frames =
        (AUDIO_SAMPLE_RATE * AUDIO_TEST_DURATION_MS) / 1000;
    uint32_t frames_remaining = total_frames;
    uint32_t phase = 0;
    uint32_t probe_peak = 0;
    size_t probe_sample_count = 0;
    bool probe_saw_positive = false;
    bool probe_saw_negative = false;
    const uint32_t phase_step =
        (uint32_t)(((uint64_t)frequency_hz << 32) /
                   AUDIO_SAMPLE_RATE);

    *din_test_passed = false;
    ESP_LOGI(TAG, "OUTPUT TEST %s: %u Hz for %d ms",
             format_name, (unsigned)frequency_hz,
             AUDIO_TEST_DURATION_MS);
    while (frames_remaining > 0 && s_requested) {
        const uint32_t frames =
            frames_remaining < AUDIO_FRAME_COUNT
                ? frames_remaining
                : AUDIO_FRAME_COUNT;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const int32_t sample =
                (phase & 0x80000000U) != 0
                    ? AUDIO_TEST_AMPLITUDE
                    : -AUDIO_TEST_AMPLITUDE;
            samples[frame * 2] = sample;
            samples[frame * 2 + 1] = sample;
            phase += phase_step;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write =
            frames * 2 * sizeof(int32_t);
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_channel, samples, bytes_to_write,
                              &bytes_written, pdMS_TO_TICKS(100)),
            TAG, "Output test write failed");
        ESP_RETURN_ON_FALSE(bytes_written == bytes_to_write, ESP_FAIL, TAG,
                            "Output test short write");

        size_t probe_bytes_read = 0;
        const esp_err_t probe_result =
            i2s_channel_read(s_rx_channel, probe_samples, bytes_to_write,
                             &probe_bytes_read, pdMS_TO_TICKS(100));
        ESP_RETURN_ON_ERROR(probe_result, TAG, "DIN probe read failed");

        const size_t samples_read =
            probe_bytes_read / sizeof(probe_samples[0]);
        probe_sample_count += samples_read;
        for (size_t index = 0; index < samples_read; ++index) {
            const int32_t sample = probe_samples[index];
            const uint32_t magnitude =
                sample == INT32_MIN
                    ? (uint32_t)INT32_MAX
                    : (uint32_t)(sample < 0 ? -sample : sample);
            if (magnitude > probe_peak) {
                probe_peak = magnitude;
            }
            if (sample > 0x10000000) {
                probe_saw_positive = true;
            } else if (sample < -0x10000000) {
                probe_saw_negative = true;
            }
        }
        frames_remaining -= frames;
    }

    *din_test_passed =
        probe_sample_count > 0 &&
        probe_peak > 0x10000000 &&
        probe_saw_positive &&
        probe_saw_negative;
    if (*din_test_passed) {
        ESP_LOGI(TAG, "DIN SELF-TEST PASS: GPIO%d samples=%u peak=0x%08x",
                 AUDIO_PIN_SPEAKER_DATA, (unsigned)probe_sample_count,
                 (unsigned)probe_peak);
    } else {
        ESP_LOGE(TAG,
                 "DIN SELF-TEST FAIL: GPIO%d samples=%u peak=0x%08x "
                 "positive=%d negative=%d",
                 AUDIO_PIN_SPEAKER_DATA, (unsigned)probe_sample_count,
                 (unsigned)probe_peak, probe_saw_positive,
                 probe_saw_negative);
    }
    ESP_LOGI(TAG, "OUTPUT TEST %s finished", format_name);
    return ESP_OK;
}

static esp_err_t switch_audio_format(bool philips)
{
    const i2s_std_slot_config_t philips_config =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    const i2s_std_slot_config_t left_justified_config =
        I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    const i2s_std_slot_config_t *slot_config =
        philips ? &philips_config : &left_justified_config;

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_channel), TAG,
                        "TX pause failed");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_rx_channel), TAG,
                        "RX pause failed");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(s_tx_channel, slot_config),
        TAG, "TX format switch failed");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(s_rx_channel, slot_config),
        TAG, "RX format switch failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_channel), TAG,
                        "RX restart failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel), TAG,
                        "TX restart failed");
    ESP_LOGI(TAG, "I2S format switched to %s",
             philips ? "PHILIPS (MAX98357A)"
                     : "LEFT-JUSTIFIED (MAX98357B)");
    return ESP_OK;
}

static esp_err_t switch_rx_to_microphone(void)
{
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_channel), TAG,
                        "TX pause failed");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_rx_channel), TAG,
                        "RX pause failed");
    ESP_RETURN_ON_ERROR(configure_rx_input(AUDIO_PIN_MIC_DATA), TAG,
                        "Microphone input selection failed");
    const i2s_std_slot_config_t philips_config =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(s_tx_channel, &philips_config),
        TAG, "TX Philips restore failed");
    ESP_RETURN_ON_ERROR(
        i2s_channel_reconfig_std_slot(s_rx_channel, &philips_config),
        TAG, "RX Philips restore failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_channel), TAG,
                        "RX restart failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel), TAG,
                        "TX restart failed");
    ESP_LOGI(TAG, "RX input switched to microphone GPIO%d",
             AUDIO_PIN_MIC_DATA);
    return ESP_OK;
}

static void audio_task(void *argument)
{
    (void)argument;

    int32_t rx_samples[AUDIO_FRAME_COUNT * 2];
    int32_t tx_samples[AUDIO_FRAME_COUNT * 2];
    bool channels_enabled = false;

    while (true) {
        if (s_requested && !channels_enabled) {
            s_status = AUDIO_LOOPBACK_STARTING;
            const esp_err_t probe_select_result =
                configure_rx_input(AUDIO_PIN_SPEAKER_DATA);
            if (probe_select_result == ESP_OK &&
                enable_channels() == ESP_OK) {
                channels_enabled = true;
                s_status = AUDIO_LOOPBACK_OUTPUT_TEST;
                bool philips_din_passed = false;
                bool left_justified_din_passed = false;
                if (play_output_test_tone(
                        tx_samples, rx_samples,
                        "PHILIPS/MAX98357A",
                        AUDIO_PHILIPS_TEST_FREQUENCY,
                        &philips_din_passed) == ESP_OK &&
                    s_requested &&
                    switch_audio_format(false) == ESP_OK &&
                    play_output_test_tone(
                        tx_samples, rx_samples,
                        "LEFT-JUSTIFIED/MAX98357B",
                        AUDIO_LEFT_JUSTIFIED_TEST_FREQUENCY,
                        &left_justified_din_passed) == ESP_OK &&
                    s_requested &&
                    switch_rx_to_microphone() == ESP_OK) {
                    s_status = philips_din_passed &&
                                       left_justified_din_passed
                                   ? AUDIO_LOOPBACK_RUNNING
                                   : AUDIO_LOOPBACK_ERROR;
                } else if (s_requested) {
                    s_status = AUDIO_LOOPBACK_ERROR;
                    s_requested = false;
                }
            } else {
                if (probe_select_result != ESP_OK) {
                    ESP_LOGE(TAG, "DIN probe selection failed: %s",
                             esp_err_to_name(probe_select_result));
                }
                s_requested = false;
                s_status = AUDIO_LOOPBACK_ERROR;
            }
        } else if (!s_requested && channels_enabled) {
            disable_channels();
            channels_enabled = false;
            s_status = AUDIO_LOOPBACK_OFF;
        }

        if (!channels_enabled) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t bytes_read = 0;
        const esp_err_t read_result =
            i2s_channel_read(s_rx_channel, rx_samples, sizeof(rx_samples),
                             &bytes_read, pdMS_TO_TICKS(100));
        if (read_result != ESP_OK) {
            if (read_result != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "I2S read failed: %s",
                         esp_err_to_name(read_result));
                s_status = AUDIO_LOOPBACK_ERROR;
                s_requested = false;
            }
            continue;
        }

        const size_t frames = bytes_read / (2 * sizeof(int32_t));
        uint32_t peak = 0;
        for (size_t frame = 0; frame < frames; ++frame) {
            /* L/R=GND makes INMP441 drive the left I2S slot. */
            const int32_t microphone = rx_samples[frame * 2];
            const uint32_t magnitude =
                microphone == INT32_MIN
                    ? (uint32_t)INT32_MAX
                    : (uint32_t)(microphone < 0 ? -microphone : microphone);
            if (magnitude > peak) {
                peak = magnitude;
            }

            const int32_t output = amplify_and_clip(microphone);
            tx_samples[frame * 2] = output;
            tx_samples[frame * 2 + 1] = output;
        }

        uint32_t level = peak >> 22;
        if (level > 100) {
            level = 100;
        }
        s_level = (uint8_t)level;

        size_t bytes_written = 0;
        const size_t bytes_to_write =
            frames * 2 * sizeof(int32_t);
        const esp_err_t write_result =
            i2s_channel_write(s_tx_channel, tx_samples, bytes_to_write,
                              &bytes_written, pdMS_TO_TICKS(100));
        if (write_result != ESP_OK || bytes_written != bytes_to_write) {
            ESP_LOGE(TAG, "I2S write failed: %s",
                     esp_err_to_name(write_result));
            s_status = AUDIO_LOOPBACK_ERROR;
            s_requested = false;
        }
    }
}

esp_err_t audio_loopback_init(void)
{
    /*
     * Keep speaker TX and microphone RX on different ESP32 controllers.
     * I2S0 is the clock master. I2S1 receives the same BCLK/WS pins as a
     * slave, so a stalled microphone channel cannot affect speaker TX.
     */
    i2s_chan_config_t tx_channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    tx_channel_config.dma_desc_num = 4;
    tx_channel_config.dma_frame_num = AUDIO_FRAME_COUNT;

    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&tx_channel_config, &s_tx_channel, NULL),
        TAG, "I2S speaker TX allocation failed");

    i2s_chan_config_t rx_channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
    rx_channel_config.dma_desc_num = 4;
    rx_channel_config.dma_frame_num = AUDIO_FRAME_COUNT;

    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&rx_channel_config, NULL, &s_rx_channel),
        TAG, "I2S microphone RX allocation failed");

    const i2s_std_config_t tx_standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_PIN_BCLK,
            .ws = AUDIO_PIN_WS,
            .dout = AUDIO_PIN_SPEAKER_DATA,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    const i2s_std_config_t rx_standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_PIN_BCLK,
            .ws = AUDIO_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_PIN_SPEAKER_DATA,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(s_tx_channel, &tx_standard_config),
        TAG, "I2S TX configuration failed");
    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(s_rx_channel, &rx_standard_config),
        TAG, "I2S RX configuration failed");

    const BaseType_t task_created =
        xTaskCreate(audio_task, "audio_loopback", 4096, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "Audio task creation failed");

    ESP_LOGI(TAG,
             "Ready: TX=I2S0 master RX=I2S1 slave "
             "BCLK=%d WS=%d MIC_SD=%d AMP_DIN=%d",
             AUDIO_PIN_BCLK, AUDIO_PIN_WS,
             AUDIO_PIN_MIC_DATA, AUDIO_PIN_SPEAKER_DATA);
    return ESP_OK;
}

void audio_loopback_start(void)
{
    s_requested = true;
}

void audio_loopback_stop(void)
{
    s_requested = false;
}

audio_loopback_status_t audio_loopback_get_status(void)
{
    return s_status;
}

uint8_t audio_loopback_get_level(void)
{
    return s_level;
}

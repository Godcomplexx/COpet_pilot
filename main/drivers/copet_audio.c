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
    AUDIO_PIN_MIC_DIN = 34,
    AUDIO_SAMPLE_RATE = 16000,
    AUDIO_BUFFER_FRAMES = 128,
    AUDIO_OUTPUT_GAIN_PERCENT = 25,
    /* INMP441 left-justifies 24 valid bits in a 32-bit slot. Shifting the raw
     * word right by this keeps the upper part of that 24-bit range: >>16 threw
     * away the lower 8 bits where quieter, at-a-distance music lives, so it
     * read near zero. >>11 keeps ~13 bits, enough for room-level music. */
    AUDIO_MIC_SHIFT = 11,
    /* Divisor mapping mean |sample| to a 0..255 loudness after AUDIO_MIC_SHIFT.
     * Tune so normal listening volume sits near mid-scale and the envelope can
     * still swing for the beat detector. */
    AUDIO_MIC_LEVEL_DIVISOR = 40,
    /* DC/drift removal: subtract a slow EMA of the signal instead of a
     * differentiator. y = x - dc; dc += (x - dc) >> AUDIO_DC_SHIFT. Larger
     * shift = slower estimate = lower cutoff. 7 ≈ 128-sample TC ≈ 20 Hz, so it
     * strips the mic's offset/drift while passing the audio band at full gain
     * (the old x-x_prev high-pass attenuated mid frequencies ~10x, which made
     * loud music read as a low amp with no zero-crossings). */
    AUDIO_DC_SHIFT = 7,
    /* Amplitude below which a sample counts as "near zero" for ZCR. After the
     * high-pass the quiet-room signal centers near zero, so a small band
     * rejects residual noise while letting real audio cross. */
    AUDIO_ZCR_HYSTERESIS = 150,
    /* Crossings/sample is tiny; scale it up into the 0..255 ZCR range. At
     * 16 kHz a 1 kHz tone gives ~2000 crossings/s = 0.125/sample -> ~64. */
    AUDIO_ZCR_SCALE = 512,
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
extern const uint8_t angry_grunt_start[]
    asm("_binary_angry_grunt_pcm_start");
extern const uint8_t angry_grunt_end[]
    asm("_binary_angry_grunt_pcm_end");

static const char *TAG = "copet_audio";
static i2s_chan_handle_t s_tx_channel;
static i2s_chan_handle_t s_rx_channel;
static QueueHandle_t s_event_queue;
static volatile bool s_enabled = true;
static volatile bool s_mic_active;
static volatile uint8_t s_mic_level;
static volatile uint8_t s_mic_zcr;
/* When the mic is active, TX is enabled once at init and left on so the shared
 * clock keeps running for the mic; audio_task must then not enable/disable it
 * per clip. */
static bool s_tx_always_on;
/* Persistent slow DC/drift estimate for the mic high-pass (see mic_task). */
static int32_t s_dc_est;

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
    case COPET_AUDIO_ANGRY:
        *clip = (audio_clip_t){angry_grunt_start, angry_grunt_end,
                               "ANGRY GRUNT"};
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
    /* 32-bit stereo bus: silence is just zeroed 32-bit words. */
    int32_t silence[AUDIO_BUFFER_FRAMES * 2] = {0};
    size_t bytes_written = 0;
    (void)i2s_channel_write(s_tx_channel, silence, sizeof(silence),
                            &bytes_written, portMAX_DELAY);
}

static void audio_task(void *argument)
{
    (void)argument;

    copet_audio_event_t event;
    /* 32-bit stereo bus: each 16-bit PCM sample is left-justified into the
     * upper half of a 32-bit word so the MAX98357A reads it correctly. */
    int32_t stereo_samples[AUDIO_BUFFER_FRAMES * 2];

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

        if (!s_tx_always_on) {
            const esp_err_t enable_result = i2s_channel_enable(s_tx_channel);
            if (enable_result != ESP_OK) {
                ESP_LOGE(TAG, "I2S start failed for %s: %s", clip.name,
                         esp_err_to_name(enable_result));
                continue;
            }
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
                const int32_t wide = (int32_t)sample << 16;
                stereo_samples[frame * 2] = wide;
                stereo_samples[frame * 2 + 1] = wide;
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
        if (!s_tx_always_on) {
            const esp_err_t disable_result = i2s_channel_disable(s_tx_channel);
            if (disable_result != ESP_OK) {
                ESP_LOGE(TAG, "I2S stop failed after %s: %s", clip.name,
                         esp_err_to_name(disable_result));
            }
        }
        ESP_LOGI(TAG, "%s %s", clip.name,
                 sample_offset == mono_samples ? "finished" : "stopped");
    }
}

/*
 * Background loudness meter for the INMP441 on its own I2S controller.
 *
 * The INMP441 is a 24-bit mic that left-justifies its 24 data bits in a
 * 32-bit slot. We therefore read 32-bit words and take the top 16 bits as the
 * effective sample (sample >> 16), which is where the real audio lives; the
 * lower bits are mic self-noise. Reading it as 16-bit before -- on the shared
 * 16-bit TX bus -- captured those noise bits instead, so silence and music
 * looked identical. This dedicated 32-bit path fixes that.
 */
static void mic_task(void *argument)
{
    (void)argument;
    int32_t samples[AUDIO_BUFFER_FRAMES * 2];
#if CONFIG_COPET_MUSIC_DEBUG
    TickType_t last_diag = 0;
#endif
    while (true) {
        size_t bytes_read = 0;
        const esp_err_t result = i2s_channel_read(
            s_rx_channel, samples, sizeof(samples), &bytes_read,
            pdMS_TO_TICKS(200));
        if (result != ESP_OK || bytes_read == 0) {
#if CONFIG_COPET_MUSIC_DEBUG
            if (xTaskGetTickCount() - last_diag >= pdMS_TO_TICKS(1000)) {
                last_diag = xTaskGetTickCount();
                ESP_LOGW(TAG, "mic read: err=%s bytes=%u",
                         esp_err_to_name(result), (unsigned)bytes_read);
            }
#endif
            continue;
        }
        const size_t count = bytes_read / sizeof(samples[0]);

        /* The INMP441 drives only one I2S slot; the other is silent. Which slot
         * carries the audio depends on the module's L/R strap wiring, so do not
         * assume "left". Measure both slots this window and use the louder one.
         * (Reading the wrong slot was why loud music barely moved the level.) */
        uint64_t energy_left = 0;
        uint64_t energy_right = 0;
        for (size_t i = 0; i + 1 < count; i += 2) {
            const int32_t l = samples[i] >> AUDIO_MIC_SHIFT;
            const int32_t r = samples[i + 1] >> AUDIO_MIC_SHIFT;
            energy_left += (uint32_t)(l < 0 ? -l : l);
            energy_right += (uint32_t)(r < 0 ? -r : r);
        }
        const bool use_left = energy_left >= energy_right;
        const size_t start = use_left ? 0 : 1;

        uint64_t accumulator = 0;
        uint32_t crossings = 0;
        /* A one-pole DC-blocking high-pass filter runs per sample on the active
         * slot: y[n] = x[n] - x[n-1] + R*y[n-1] (R in Q15). It removes the
         * mic's slow drift/DC so the audio-band oscillation crosses zero and
         * ZCR measures tonality instead of degenerating into a loudness gate. */
        uint32_t used = 0;
        int previous_sign = 0;
        for (size_t i = start; i < count; i += 2) {
            const int32_t x = samples[i] >> AUDIO_MIC_SHIFT;
            /* AC audio = sample minus the slow DC/drift estimate. */
            const int32_t y = x - s_dc_est;
            s_dc_est += (x - s_dc_est) >> AUDIO_DC_SHIFT;

            accumulator += (uint32_t)(y < 0 ? -y : y);
            ++used;
            int sign = previous_sign;
            if (y > AUDIO_ZCR_HYSTERESIS) {
                sign = 1;
            } else if (y < -AUDIO_ZCR_HYSTERESIS) {
                sign = -1;
            }
            if (previous_sign != 0 && sign != 0 && sign != previous_sign) {
                ++crossings;
            }
            if (sign != 0) { previous_sign = sign; }
        }
        const uint32_t mean = used > 0 ? (uint32_t)(accumulator / used) : 0;
        const uint32_t raw_left = used > 0 ? (uint32_t)(energy_left / used) : 0;
        const uint32_t raw_right = used > 0 ? (uint32_t)(energy_right / used) : 0;
        /* Loudness comes from the RAW active-slot envelope, which tracks music
         * strongly (~30x quiet->loud). The high-pass output (`mean`) is kept
         * only for the ZCR/diagnostics; on this mic the audio sits mostly below
         * the high-pass corner, so it made a poor loudness signal. */
        const uint32_t active_raw = use_left ? raw_left : raw_right;
        uint32_t level = active_raw / AUDIO_MIC_LEVEL_DIVISOR;
        if (level > 255U) { level = 255U; }
        s_mic_level = (uint8_t)level;

        /* Normalize crossings to 0..255 over the left-slot samples actually
         * used, scaled so a full window of the highest musically meaningful
         * rate maps near the top of the range. */
        uint32_t zcr = used > 0
            ? (crossings * AUDIO_ZCR_SCALE) / used
            : 0;
        if (zcr > 255U) { zcr = 255U; }
        s_mic_zcr = (uint8_t)zcr;

#if CONFIG_COPET_MUSIC_DEBUG
        if (xTaskGetTickCount() - last_diag >= pdMS_TO_TICKS(1000)) {
            last_diag = xTaskGetTickCount();
            ESP_LOGI(TAG, "mic: L=%u R=%u slot=%c amp=%u cross=%u level=%u zcr=%u",
                     (unsigned)raw_left, (unsigned)raw_right,
                     use_left ? 'L' : 'R', (unsigned)mean,
                     (unsigned)crossings, (unsigned)level, (unsigned)zcr);
        }
#endif
    }
}

esp_err_t copet_audio_init(void)
{
#if CONFIG_COPET_MIC_ENABLED
    const bool want_mic = true;
#else
    const bool want_mic = false;
#endif

    /* TX (amp) and RX (INMP441) share one controller and one set of pins
     * (BCLK/WS on 26/25), so they must use the same slot width. The INMP441 is
     * a 24-bit mic that only yields real audio in a 32-bit slot, so the WHOLE
     * bus runs at 32 bits: TX left-justifies each 16-bit PCM sample into the
     * upper half of a 32-bit word (see audio_task), and RX reads 32-bit words
     * and takes the top 16 bits. Running the bus at 16 bits -- the old setup --
     * fed the mic only its noise bits, so silence and music looked identical. */
    const i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&channel_config, &s_tx_channel,
                        want_mic ? &s_rx_channel : NULL),
        TAG, "I2S channel allocation failed");

    const i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_PIN_BCLK,
            .ws = AUDIO_PIN_LRC,
            .dout = AUDIO_PIN_DOUT,
            .din = want_mic ? AUDIO_PIN_MIC_DIN : I2S_GPIO_UNUSED,
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

    if (want_mic && s_rx_channel != NULL) {
        /* Mic is best-effort: any failure here leaves sound (TX) working. TX is
         * enabled once and kept on so the shared clock always drives the mic
         * (INMP441 is a slave); audio_task then must not toggle TX per clip. */
        esp_err_t mic = i2s_channel_init_std_mode(s_rx_channel,
                                                  &standard_config);
        if (mic == ESP_OK) {
            mic = i2s_channel_enable(s_tx_channel);
        }
        if (mic == ESP_OK) {
            s_tx_always_on = true;
            write_silence();
            mic = i2s_channel_enable(s_rx_channel);
        }
        if (mic == ESP_OK &&
            xTaskCreate(mic_task, "copet_mic", 3072, NULL, 3, NULL) == pdPASS) {
            s_mic_active = true;
            ESP_LOGI(TAG, "Mic capture on DIN=%d (32-bit bus) for listening",
                     AUDIO_PIN_MIC_DIN);
        } else {
            ESP_LOGW(TAG, "Mic capture unavailable (%s); sound still works",
                     esp_err_to_name(mic));
        }
    }

    ESP_LOGI(TAG,
             "PCM audio ready: 16 kHz mono clips, BCLK=%d LRC=%d DOUT=%d",
             AUDIO_PIN_BCLK, AUDIO_PIN_LRC, AUDIO_PIN_DOUT);
    return ESP_OK;
}

uint8_t copet_audio_get_mic_level(void)
{
    return s_mic_active ? s_mic_level : 0U;
}

uint8_t copet_audio_get_mic_zcr(void)
{
    return s_mic_active ? s_mic_zcr : 0U;
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

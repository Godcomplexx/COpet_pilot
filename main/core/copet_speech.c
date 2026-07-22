#include "core/copet_speech.h"

static int add(speech_word_t *out, int max, int n, speech_word_t word)
{
    if (n >= 0 && n < max) { out[n] = word; }
    return n + 1;
}

static int add_number(speech_word_t *out, int max, int n, int value)
{
    if (value < 0) { value = 0; }
    if (value > 59) { value = 59; }
    if (value < 20) {
        return add(out, max, n, (speech_word_t)(SPEECH_ZERO + value));
    }
    n = add(out, max, n, (speech_word_t)(SPEECH_TWENTY + value / 10 - 2));
    if (value % 10 != 0) {
        n = add(out, max, n, (speech_word_t)(SPEECH_ZERO + value % 10));
    }
    return n;
}

static speech_word_t weather_code_word(int code)
{
    if (code == 0) { return SPEECH_CLEAR; }
    if (code >= 1 && code <= 3) { return SPEECH_CLOUDY; }
    if (code == 45 || code == 48) { return SPEECH_FOG; }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return SPEECH_RAIN;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return SPEECH_SNOW;
    }
    if (code >= 95) { return SPEECH_STORM; }
    return SPEECH_CLOUDY;
}

int copet_speech_number(int value, speech_word_t *out, int max)
{
    return add_number(out, max, 0, value);
}

int copet_speech_time(int hour, int minute, speech_word_t *out, int max)
{
    int n = add_number(out, max, 0, hour);
    if (minute == 0) {
        n = add(out, max, n, SPEECH_OCLOCK);
    } else if (minute < 10) {
        n = add(out, max, n, SPEECH_OH);
        n = add(out, max, n, (speech_word_t)(SPEECH_ZERO + minute));
    } else {
        n = add_number(out, max, n, minute);
    }
    return n;
}

int copet_speech_weather(int temp_c, int weather_code, speech_word_t *out,
                         int max)
{
    int n = 0;
    if (temp_c < 0) {
        n = add(out, max, n, SPEECH_MINUS);
        temp_c = -temp_c;
    }
    n = add_number(out, max, n, temp_c);
    n = add(out, max, n, SPEECH_DEGREES);
    n = add(out, max, n, weather_code_word(weather_code));
    return n;
}

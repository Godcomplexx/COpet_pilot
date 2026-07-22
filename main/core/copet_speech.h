#ifndef COPET_SPEECH_H
#define COPET_SPEECH_H

/*
 * Concatenative speech vocabulary + sequencing. Pure logic: it turns a number,
 * a clock time or a weather reading into an ordered list of word ids. The audio
 * driver owns the matching embedded PCM clips and plays them back to back.
 *
 * The enum order is the contract with the clip table in copet_audio.c and with
 * tools/generate_speech.ps1 -- keep all three in sync. SPEECH_ZERO..NINETEEN
 * must stay contiguous (indexed by digit).
 */

typedef enum {
    SPEECH_ZERO, SPEECH_ONE, SPEECH_TWO, SPEECH_THREE, SPEECH_FOUR,
    SPEECH_FIVE, SPEECH_SIX, SPEECH_SEVEN, SPEECH_EIGHT, SPEECH_NINE,
    SPEECH_TEN, SPEECH_ELEVEN, SPEECH_TWELVE, SPEECH_THIRTEEN, SPEECH_FOURTEEN,
    SPEECH_FIFTEEN, SPEECH_SIXTEEN, SPEECH_SEVENTEEN, SPEECH_EIGHTEEN,
    SPEECH_NINETEEN,
    SPEECH_TWENTY, SPEECH_THIRTY, SPEECH_FORTY, SPEECH_FIFTY,
    SPEECH_DEGREES, SPEECH_MINUS, SPEECH_OH, SPEECH_OCLOCK,
    SPEECH_CLEAR, SPEECH_CLOUDY, SPEECH_FOG, SPEECH_RAIN, SPEECH_SNOW,
    SPEECH_STORM,
    SPEECH_WORD_COUNT,
} speech_word_t;

enum { SPEECH_MAX_WORDS = 8 };

/*
 * Each function fills `out` (up to `max` words) and returns the total number of
 * words the phrase needs -- which may exceed `max`, so callers should pass a
 * SPEECH_MAX_WORDS-sized buffer.
 */

/* A whole number 0..59 ("forty seven"). */
int copet_speech_number(int value, speech_word_t *out, int max);

/* A 24-hour clock time ("fourteen thirty", "nine oh five", "ten o'clock"). */
int copet_speech_time(int hour, int minute, speech_word_t *out, int max);

/* Temperature (C) + Open-Meteo weather code ("eighteen degrees, clear sky"). */
int copet_speech_weather(int temp_c, int weather_code, speech_word_t *out,
                         int max);

#endif

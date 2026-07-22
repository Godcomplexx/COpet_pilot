#include "core/copet_speech.h"
#include "test_util.h"

static void test_numbers(void)
{
    speech_word_t w[SPEECH_MAX_WORDS];
    CHECK(copet_speech_number(5, w, SPEECH_MAX_WORDS) == 1);
    CHECK(w[0] == SPEECH_FIVE);
    CHECK(copet_speech_number(19, w, SPEECH_MAX_WORDS) == 1);
    CHECK(w[0] == SPEECH_NINETEEN);
    CHECK(copet_speech_number(20, w, SPEECH_MAX_WORDS) == 1);
    CHECK(w[0] == SPEECH_TWENTY);
    CHECK(copet_speech_number(47, w, SPEECH_MAX_WORDS) == 2);
    CHECK(w[0] == SPEECH_FORTY);
    CHECK(w[1] == SPEECH_SEVEN);
    CHECK(copet_speech_number(30, w, SPEECH_MAX_WORDS) == 1);
    CHECK(w[0] == SPEECH_THIRTY);
}

static void test_time(void)
{
    speech_word_t w[SPEECH_MAX_WORDS];
    CHECK(copet_speech_time(14, 30, w, SPEECH_MAX_WORDS) == 2);
    CHECK(w[0] == SPEECH_FOURTEEN);
    CHECK(w[1] == SPEECH_THIRTY);

    CHECK(copet_speech_time(14, 5, w, SPEECH_MAX_WORDS) == 3);
    CHECK(w[0] == SPEECH_FOURTEEN);
    CHECK(w[1] == SPEECH_OH);
    CHECK(w[2] == SPEECH_FIVE);

    CHECK(copet_speech_time(10, 0, w, SPEECH_MAX_WORDS) == 2);
    CHECK(w[0] == SPEECH_TEN);
    CHECK(w[1] == SPEECH_OCLOCK);

    CHECK(copet_speech_time(9, 45, w, SPEECH_MAX_WORDS) == 3);
    CHECK(w[0] == SPEECH_NINE);
    CHECK(w[1] == SPEECH_FORTY);
    CHECK(w[2] == SPEECH_FIVE);

    /* 23:15 -> twenty three fifteen */
    CHECK(copet_speech_time(23, 15, w, SPEECH_MAX_WORDS) == 3);
    CHECK(w[0] == SPEECH_TWENTY);
    CHECK(w[1] == SPEECH_THREE);
    CHECK(w[2] == SPEECH_FIFTEEN);
}

static void test_weather(void)
{
    speech_word_t w[SPEECH_MAX_WORDS];
    CHECK(copet_speech_weather(18, 0, w, SPEECH_MAX_WORDS) == 3);
    CHECK(w[0] == SPEECH_EIGHTEEN);
    CHECK(w[1] == SPEECH_DEGREES);
    CHECK(w[2] == SPEECH_CLEAR);

    CHECK(copet_speech_weather(-5, 71, w, SPEECH_MAX_WORDS) == 4);
    CHECK(w[0] == SPEECH_MINUS);
    CHECK(w[1] == SPEECH_FIVE);
    CHECK(w[2] == SPEECH_DEGREES);
    CHECK(w[3] == SPEECH_SNOW);

    CHECK(copet_speech_weather(23, 61, w, SPEECH_MAX_WORDS) == 4);
    CHECK(w[0] == SPEECH_TWENTY);
    CHECK(w[1] == SPEECH_THREE);
    CHECK(w[2] == SPEECH_DEGREES);
    CHECK(w[3] == SPEECH_RAIN);

    CHECK(copet_speech_weather(20, 95, w, SPEECH_MAX_WORDS) == 3);
    CHECK(w[2] == SPEECH_STORM);
}

int main(void)
{
    test_numbers();
    test_time();
    test_weather();
    TEST_REPORT("copet_speech");
}

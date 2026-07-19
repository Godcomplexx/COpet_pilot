#include "modes/settings_mode.h"
#include "test_util.h"

static void test_initial_value(void)
{
    settings_mode_t settings;
    settings_mode_init(&settings, true);
    CHECK(settings.sound_enabled == true);
    CHECK_STR(settings_mode_sound_label(&settings), "ON");

    settings_mode_init(&settings, false);
    CHECK(settings.sound_enabled == false);
    CHECK_STR(settings_mode_sound_label(&settings), "OFF");
}

static void test_toggle(void)
{
    settings_mode_t settings;
    settings_mode_init(&settings, true);

    CHECK(settings_mode_toggle_sound(&settings) == false);
    CHECK(settings.sound_enabled == false);
    CHECK_STR(settings_mode_sound_label(&settings), "OFF");

    CHECK(settings_mode_toggle_sound(&settings) == true);
    CHECK(settings.sound_enabled == true);
    CHECK_STR(settings_mode_sound_label(&settings), "ON");
}

int main(void)
{
    test_initial_value();
    test_toggle();
    TEST_REPORT("settings_mode");
}

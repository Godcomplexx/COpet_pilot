#include "modes/menu_mode.h"
#include "test_util.h"

static void test_layout(void)
{
    CHECK(menu_mode_count() == 3);
    CHECK(menu_mode_item(0) != NULL);
    CHECK_STR(menu_mode_item(0)->label, "FOCUS");
    CHECK(menu_mode_item(0)->mode == COPET_MODE_FOCUS);
    CHECK_STR(menu_mode_item(1)->label, "ASSISTANT");
    CHECK(menu_mode_item(1)->mode == COPET_MODE_ASSISTANT);
    CHECK_STR(menu_mode_item(2)->label, "SETTINGS");
    CHECK(menu_mode_item(2)->mode == COPET_MODE_SETTINGS);
    CHECK(menu_mode_item(3) == NULL); /* out of range */
}

static void test_scroll_wraps(void)
{
    menu_mode_t menu;
    menu_mode_init(&menu);
    CHECK(menu.selected == 0);

    menu_mode_scroll(&menu, 1);
    CHECK(menu.selected == 1);
    menu_mode_scroll(&menu, 2); /* 1 + 2 = 3 -> wraps to 0 */
    CHECK(menu.selected == 0);
    CHECK_STR(menu_mode_selected(&menu)->label, "FOCUS");

    /* Backward past the start wraps to the end. */
    menu_mode_scroll(&menu, -1);
    CHECK(menu.selected == 2);
    CHECK_STR(menu_mode_selected(&menu)->label, "SETTINGS");
}

static void test_scroll_multi_step(void)
{
    menu_mode_t menu;
    menu_mode_init(&menu);

    /* +7 over 3 items lands on index 1. */
    menu_mode_scroll(&menu, 7);
    CHECK(menu.selected == 1);

    menu_mode_init(&menu);
    /* -7 over 3 items lands on index 2. */
    menu_mode_scroll(&menu, -7);
    CHECK(menu.selected == 2);
}

int main(void)
{
    test_layout();
    test_scroll_wraps();
    test_scroll_multi_step();
    TEST_REPORT("menu_mode");
}

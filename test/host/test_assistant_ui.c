#include "ui/assistant_ui.h"
#include "test_util.h"

static void test_wrap_fits_one_line(void)
{
    char lines[ASSISTANT_UI_MAX_LINES][ASSISTANT_UI_LINE_MAX];
    const int n = assistant_ui_wrap("HELLO WORLD", 26, lines,
                                    ASSISTANT_UI_MAX_LINES);
    CHECK(n == 1);
    CHECK_STR(lines[0], "HELLO WORLD");
}

static void test_wrap_breaks_on_spaces(void)
{
    char lines[ASSISTANT_UI_MAX_LINES][ASSISTANT_UI_LINE_MAX];
    const int n = assistant_ui_wrap("HELLO WORLD", 8, lines,
                                    ASSISTANT_UI_MAX_LINES);
    CHECK(n == 2);
    CHECK_STR(lines[0], "HELLO");
    CHECK_STR(lines[1], "WORLD");
}

static void test_wrap_packs_words(void)
{
    char lines[ASSISTANT_UI_MAX_LINES][ASSISTANT_UI_LINE_MAX];
    const int n = assistant_ui_wrap("IT IS SUNNY TODAY", 10, lines,
                                    ASSISTANT_UI_MAX_LINES);
    CHECK(n == 3);
    CHECK_STR(lines[0], "IT IS");
    CHECK_STR(lines[1], "SUNNY");
    CHECK_STR(lines[2], "TODAY");
}

static void test_wrap_hard_splits_long_word(void)
{
    char lines[ASSISTANT_UI_MAX_LINES][ASSISTANT_UI_LINE_MAX];
    const int n = assistant_ui_wrap("ABCDEFGHIJ", 4, lines,
                                    ASSISTANT_UI_MAX_LINES);
    CHECK(n == 3);
    CHECK_STR(lines[0], "ABCD");
    CHECK_STR(lines[1], "EFGH");
    CHECK_STR(lines[2], "IJ");
}

static void test_wrap_respects_max_lines(void)
{
    char lines[2][ASSISTANT_UI_LINE_MAX];
    const int n = assistant_ui_wrap("A B C D E", 1, lines, 2);
    CHECK(n == 2);
    CHECK_STR(lines[0], "A");
    CHECK_STR(lines[1], "B");
}

int main(void)
{
    test_wrap_fits_one_line();
    test_wrap_breaks_on_spaces();
    test_wrap_packs_words();
    test_wrap_hard_splits_long_word();
    test_wrap_respects_max_lines();
    TEST_REPORT("assistant_ui");
}

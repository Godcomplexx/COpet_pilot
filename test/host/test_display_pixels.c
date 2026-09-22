#include "drivers/display_pixels.h"
#include "test_util.h"

static uint8_t frame[COPET_DISPLAY_WIDTH * COPET_DISPLAY_HEIGHT];
static uint8_t full[COPET_PANEL_WIDTH * COPET_PANEL_HEIGHT * 2];
static uint8_t striped[sizeof(full)];

static void test_solid_colors(void)
{
    const uint8_t colors[] = {0x00, 0xE0, 0x1C, 0x03, 0xFF};
    const uint8_t wire[][2] = {{0, 0}, {0xF8, 0}, {0x07, 0xE0},
                               {0, 0x1F}, {0xFF, 0xFF}};
    for (int c = 0; c < 5; ++c) {
        memset(frame, colors[c], sizeof(frame));
        memset(full, 0xA5, sizeof(full));
        copet_display_convert_stripe(frame, 0, COPET_PANEL_HEIGHT, full);
        int matches = 1;
        for (size_t i = 0; i < sizeof(full); i += 2) {
            const int y = (int)(i / (COPET_PANEL_WIDTH * 2));
            const int active = y >= 16 && y < 144;
            if (full[i] != (active ? wire[c][0] : 0) ||
                full[i + 1] != (active ? wire[c][1] : 0)) matches = 0;
        }
        CHECK(matches);
    }
}

static void test_edges_and_stripes(void)
{
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)(i * 31);
    frame[0] = 0xE0;
    frame[COPET_DISPLAY_WIDTH - 1] = 0x1C;
    frame[sizeof(frame) - COPET_DISPLAY_WIDTH] = 0x03;
    frame[sizeof(frame) - 1] = 0xFF;
    copet_display_convert_stripe(frame, 0, COPET_PANEL_HEIGHT, full);
    const size_t first_row = 16 * COPET_PANEL_WIDTH * 2;
    CHECK(full[first_row] == 0xF8 && full[first_row + 1] == 0);
    CHECK(full[first_row + 254] == 0x07 && full[first_row + 255] == 0xE0);
    const size_t last_row = 143 * COPET_PANEL_WIDTH * 2;
    CHECK(full[last_row] == 0 && full[last_row + 1] == 0x1F);
    CHECK(full[last_row + 254] == 0xFF && full[last_row + 255] == 0xFF);

    /* An uneven stripe size exercises the final partial stripe too. */
    for (int y = 0; y < COPET_PANEL_HEIGHT; y += 7) {
        const int rows = y + 7 <= COPET_PANEL_HEIGHT ? 7 : COPET_PANEL_HEIGHT - y;
        copet_display_convert_stripe(frame, y, rows,
                                      striped + y * COPET_PANEL_WIDTH * 2);
    }
    CHECK(memcmp(full, striped, sizeof(full)) == 0);

    uint8_t guarded[COPET_PANEL_WIDTH * COPET_PANEL_STRIPE_ROWS * 2 + 2];
    memset(guarded, 0xA5, sizeof(guarded));
    copet_display_convert_stripe(frame, 137, COPET_PANEL_STRIPE_ROWS, guarded + 1);
    CHECK(guarded[0] == 0xA5);
    CHECK(guarded[sizeof(guarded) - 1] == 0xA5);
    CHECK(memcmp(guarded + 1, full + 137 * COPET_PANEL_WIDTH * 2,
                  sizeof(guarded) - 2) == 0);
}

static void test_square_keeps_proportions(void)
{
    memset(frame, 0, sizeof(frame));
    for (int y = 60; y < 180; ++y) {
        for (int x = 60; x < 180; ++x) frame[y * COPET_DISPLAY_WIDTH + x] = 0xFF;
    }
    copet_display_convert_stripe(frame, 0, COPET_PANEL_HEIGHT, full);
    int matches = 1;
    for (int y = 0; y < 160; ++y) {
        for (int x = 0; x < 128; ++x) {
            /* Centered 120x120 source square becomes exactly 64x64. */
            const uint8_t expected = x >= 32 && x < 96 && y >= 48 && y < 112 ? 255 : 0;
            const int offset = (y * 128 + x) * 2;
            if (full[offset] != expected || full[offset + 1] != expected) matches = 0;
        }
    }
    CHECK(matches);
}

int main(void)
{
    test_solid_colors();
    test_edges_and_stripes();
    test_square_keeps_proportions();
    TEST_REPORT("display_pixels");
}

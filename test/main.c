#include "raylib.h"
void draw_info_box(Font font, Color accent_color)
{
    const int box_x = 30;
    const int box_y = 160;
    const int box_width = 240;
    const int box_height = 100;

    DrawRectangleLines(
        box_x,
        box_y,
        box_width,
        box_height,
        accent_color);

    DrawTextEx(
        font,
        "FLAME // 03",
        (Vector2){box_x + 12, box_y + 10},
        20,
        1,
        accent_color);
        const int node_x = 520;
const int node_y = 300;
const int node_size = 16;

const int line_start_x = box_x + box_width;
const int line_start_y = box_y + box_height / 2;
const int line_end_x = node_x - node_size / 2;

DrawLine(
    line_start_x,
    line_start_y,
    line_end_x,
    node_y,
    accent_color);

DrawRectangleLines(
    node_x - node_size / 2,
    node_y - node_size / 2,
    node_size,
    node_size,
    accent_color);

DrawTextEx(
    font,
    "TX03",
    (Vector2){node_x + 14, node_y - 8},
    16,
    1,
    accent_color);
}
void draw_header(Color text_color, Font title_font,
    Font subtitle_font)
{
    DrawTextEx(
        title_font,
        "FIREPLACE LAB",
        (Vector2){30, 25},
        64,
        1,
        text_color);

    DrawTextEx(
        subtitle_font,
        "UNCERTAINTIES",
        (Vector2){30, 85},
        38,
        1,
        text_color);
}
void draw_grid(int screenWidth, int screenHeight, Color minor_grid, Color major_grid)
{
    for (int x = 0; x < screenWidth; x += 10)
    {
        Color color;

        if (x % 20 == 0)
            {
                color = major_grid;
            }
        else
        {
            color = minor_grid;
        }
        DrawLine(x, 0, x, screenHeight, color);
        
    }
    for (int y = 0; y < screenHeight; y += 10)
    {
        Color color;

        if (y % 20 == 0)
            {
                color = major_grid;
            }
        else
        {
            color = minor_grid;
        }
        DrawLine(0, y, screenWidth, y, color);
    }
}

int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 800;
     Color backgroundColor = {226,217,217, 255};
    Color minor_grid = {231, 231, 226, 255};
    Color major_grid = {208, 216, 210, 255};
    Color text_color = {30, 30, 28, 255};
    Color accent_color = {90, 135, 110, 255};
    InitWindow(screenWidth, screenHeight, "Fireplace");
    Font ui_font1 = LoadFontEx(
    "test/fonts/GeistPixel-Regular-VariableFont_ELSH.ttf",
    64,
    NULL,
    0);
    Font ui_font2 = LoadFontEx(
    "test/fonts/BitcountPropDoubleInk-VariableFont_CRSV,ELSH,ELXP,SZP1,SZP2,XPN1,XPN2,YPN1,YPN2,slnt,wght.ttf",
    64,
    NULL,
    0);
    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(backgroundColor);
        draw_grid(screenWidth, screenHeight, minor_grid, major_grid);
        draw_header(text_color, ui_font1, ui_font2);
        draw_info_box(ui_font2, accent_color);
        EndDrawing();
    }
    UnloadFont(ui_font1);
    UnloadFont(ui_font2);
    CloseWindow();        // Close window and OpenGL context
    return 0;
}

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define TOP_WIDTH  400
#define BOT_WIDTH  320
#define SCREEN_HEIGHT 240
#define FB_STRIDE 240

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    const char *name;
} Color;

/* Start the app on green. */
static Color current_color = { 0, 255, 0, "Green" };

static const Color common_colors[] = {
    {255,   0,   0, "Red"},
    {  0, 255,   0, "Green"},
    {  0,   0, 255, "Blue"},
    {255, 255,   0, "Yellow"},
    {255,   0, 255, "Magenta"},
    {  0, 255, 255, "Cyan"},
    {255, 255, 255, "White"},
    {  0,   0,   0, "Black"},
    {128, 128, 128, "Gray"},
    {255, 128,   0, "Orange"},
    {128,   0, 255, "Purple"},
    {  0, 128, 255, "Sky"}
};

#define COMMON_COUNT (sizeof(common_colors) / sizeof(common_colors[0]))

static bool menu_open = false;
static int tab = 0; /* 0 = common, 1 = custom */
static u64 start_hold_start = 0;
static bool start_was_held = false;

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(
        ((u16)(r >> 3) << 11) |
        ((u16)(g >> 2) << 5) |
        (u16)(b >> 3)
    );
}

/*
 * Fill the complete physical framebuffer.
 *
 * 3DS framebuffers are stored rotated, with 240 pixels between columns.
 * The top screen is 400x240 and the bottom screen is 320x240.
 */
static void fill_screen(gfxScreen_t screen, u16 color)
{
    u16 width = 0;
    u16 height = 0;
    u16 *fb = (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);

    if (!fb)
        return;

    /* Use the dimensions supplied by libctru, but keep the known stride. */
    if (height != SCREEN_HEIGHT)
        return;

    for (u16 x = 0; x < width; ++x) {
        for (u16 y = 0; y < height; ++y) {
            fb[(u32)x * FB_STRIDE + (SCREEN_HEIGHT - 1 - y)] = color;
        }
    }
}

static void bottom_rect(int x, int y, int width, int height, u16 color)
{
    u16 fb_width = 0;
    u16 fb_height = 0;
    u16 *fb = (u16 *)gfxGetFramebuffer(
        GFX_BOTTOM, GFX_LEFT, &fb_width, &fb_height
    );

    if (!fb)
        return;

    /* Clip against the actual bottom-screen dimensions. */
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > (int)fb_width)
        width = (int)fb_width - x;
    if (y + height > (int)fb_height)
        height = (int)fb_height - y;

    if (width <= 0 || height <= 0)
        return;

    for (int px = x; px < x + width; ++px) {
        for (int py = y; py < y + height; ++py) {
            fb[(u32)px * FB_STRIDE + (SCREEN_HEIGHT - 1 - py)] = color;
        }
    }
}

static void bottom_outline(int x, int y, int width, int height, u16 color)
{
    bottom_rect(x, y, width, 2, color);
    bottom_rect(x, y + height - 2, width, 2, color);
    bottom_rect(x, y, 2, height, color);
    bottom_rect(x + width - 2, y, 2, height, color);
}

static void center_text(const char *text, int row, int width_chars)
{
    int len = (int)strlen(text);
    int col = ((width_chars - len) / 2) + 1;

    if (col < 1)
        col = 1;

    printf("\x1b[%d;%dH%s", row, col, text);
}

static void draw_menu(void)
{
    const u16 panel = rgb565(28, 28, 34);
    const u16 panel2 = rgb565(48, 48, 56);
    const u16 border = rgb565(150, 150, 160);
    const u16 text = rgb565(255, 255, 255);
    const u16 selected_tab = rgb565(65, 95, 165);
    const u16 close_color = rgb565(85, 45, 50);

    /*
     * consoleClear() clears the entire bottom framebuffer. Do it first,
     * then draw every pixel of the menu ourselves so no old frame remains.
     */
    consoleClear();

    /* Full-width menu background: no half-screen/old-color area. */
    bottom_rect(0, 0, BOT_WIDTH, SCREEN_HEIGHT, panel);

    /* Main panel with a small margin. */
    bottom_rect(4, 4, 312, 232, panel);
    bottom_outline(4, 4, 312, 232, border);

    /* Tabs. */
    bottom_rect(10, 10, 145, 28, tab == 0 ? selected_tab : panel2);
    bottom_rect(165, 10, 145, 28, tab == 1 ? selected_tab : panel2);
    bottom_outline(10, 10, 145, 28, border);
    bottom_outline(165, 10, 145, 28, border);

    /* Common-color grid. */
    if (tab == 0) {
        const int x0 = 12;
        const int y0 = 46;
        const int cell_w = 70;
        const int cell_h = 43;
        const int gap = 4;

        for (int i = 0; i < (int)COMMON_COUNT; ++i) {
            int column = i % 4;
            int row = i / 4;
            int x = x0 + column * (cell_w + gap);
            int y = y0 + row * (cell_h + gap);

            bottom_rect(
                x, y, cell_w, cell_h,
                rgb565(
                    common_colors[i].r,
                    common_colors[i].g,
                    common_colors[i].b
                )
            );
            bottom_outline(x, y, cell_w, cell_h, text);
        }
    } else {
        /* Custom-color page. */
        center_text("CUSTOM COLOR", 7, 40);
        center_text("Tap this tab to enter HEX", 9, 40);
        center_text("Format: #RRGGBB", 11, 40);
        center_text("Example: #00FF80", 13, 40);
    }

    /* Close button. */
    bottom_rect(10, 198, 300, 28, close_color);
    bottom_outline(10, 198, 300, 28, border);

    /* Console text is drawn after the framebuffer graphics. */
    printf("\x1b[2;3HCOMMON COLORS");
    printf("\x1b[2;23HCUSTOM COLOR");
    center_text("CLOSE  (B)", 26, 40);
}

static void draw_frame(void)
{
    u16 color = rgb565(current_color.r, current_color.g, current_color.b);

    /* The application itself is always the selected solid color. */
    fill_screen(GFX_TOP, color);
    fill_screen(GFX_BOTTOM, color);

    /* The menu is the only exception, and only while it is open. */
    if (menu_open)
        draw_menu();
}

static bool hex_digit(char c, u8 *value)
{
    if (c >= '0' && c <= '9') {
        *value = (u8)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *value = (u8)(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *value = (u8)(c - 'A' + 10);
        return true;
    }
    return false;
}

static bool parse_hex(const char *input, Color *out)
{
    size_t len = strlen(input);
    const char *p = input;

    if (len == 7 && input[0] == '#') {
        p = input + 1;
    } else if (len != 6) {
        return false;
    }

    if (strlen(p) != 6)
        return false;

    u8 d[6];
    for (int i = 0; i < 6; ++i) {
        if (!hex_digit(p[i], &d[i]))
            return false;
    }

    out->r = (u8)((d[0] << 4) | d[1]);
    out->g = (u8)((d[2] << 4) | d[3]);
    out->b = (u8)((d[4] << 4) | d[5]);
    out->name = "Custom";
    return true;
}

static void custom_color_keyboard(void)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, 7);
    swkbdSetHintText(&swkbd, "#RRGGBB");
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    char input[8] = {0};
    SwkbdButton button = swkbdInputText(&swkbd, input, sizeof(input));

    if (button != SWKBD_BUTTON_CONFIRM)
        return;

    Color parsed;
    if (parse_hex(input, &parsed))
        current_color = parsed;
}

static void open_menu(void)
{
    menu_open = true;
    tab = 0;
}

static void handle_touch(void)
{
    touchPosition touch;
    hidTouchRead(&touch);

    int x = touch.px;
    int y = touch.py;

    /* Tabs. */
    if (y >= 10 && y < 38) {
        if (x >= 10 && x < 155) {
            tab = 0;
            return;
        }

        if (x >= 165 && x < 310) {
            tab = 1;
            custom_color_keyboard();
            return;
        }
    }

    /* Common colors. */
    if (tab == 0 && x >= 12 && x < 304 && y >= 46 && y < 187) {
        const int cell_w = 70;
        const int cell_h = 43;
        const int gap = 4;

        int column = (x - 12) / (cell_w + gap);
        int row = (y - 46) / (cell_h + gap);
        int local_x = (x - 12) % (cell_w + gap);
        int local_y = (y - 46) % (cell_h + gap);

        /* Ignore the gaps between cells. */
        if (column >= 0 && column < 4 &&
            row >= 0 && row < 3 &&
            local_x < cell_w && local_y < cell_h) {
            int index = row * 4 + column;
            if (index < (int)COMMON_COUNT)
                current_color = common_colors[index];
        }
        return;
    }

    /* Close. */
    if (x >= 10 && x < 310 && y >= 198 && y < 226) {
        menu_open = false;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    while (aptMainLoop()) {
        hidScanInput();

        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();

        /* B closes the menu. B at the app root exits normally. */
        if (down & KEY_B) {
            if (menu_open)
                menu_open = false;
            else
                break;
        }

        if (menu_open && (down & KEY_TOUCH))
            handle_touch();

        /* START must be held for one full second. */
        if (held & KEY_START) {
            if (!start_was_held) {
                start_hold_start = osGetTime();
                start_was_held = true;
            } else if (!menu_open && osGetTime() - start_hold_start >= 1000) {
                open_menu();
            }
        } else {
            start_was_held = false;
        }

        draw_frame();

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define SCREEN_H 240
#define FB_STRIDE 240
#define TOP_W 400
#define BOT_W 320

#define MENU_X 4
#define MENU_Y 4
#define MENU_W 312
#define MENU_H 232

#define START_HOLD_MS 1000

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    const char *name;
} Color;

/* The app starts completely green. */
static Color current_color = {0, 255, 0, "Green"};

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

#define COMMON_COUNT ((int)(sizeof(common_colors) / sizeof(common_colors[0])))

static bool menu_open = false;
static int active_tab = 0; /* 0 = common, 1 = custom */
static bool start_was_held = false;
static u64 start_hold_time = 0;

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(
        ((u16)(r >> 3) << 11) |
        ((u16)(g >> 2) << 5) |
        (u16)(b >> 3)
    );
}

/*
 * Get the actual framebuffer and fill every pixel in it.
 *
 * libctru exposes the 3DS framebuffer rotated in memory. The correct
 * address for a logical (x,y) pixel is x * 240 + (239 - y).
 */
static void fill_screen(gfxScreen_t screen, u16 color)
{
    u16 width = 0;
    u16 height = 0;
    u16 *fb = (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);

    if (fb == NULL || height != SCREEN_H)
        return;

    for (u16 x = 0; x < width; ++x) {
        for (u16 y = 0; y < height; ++y) {
            fb[(u32)x * FB_STRIDE + (SCREEN_H - 1 - y)] = color;
        }
    }
}

static void fill_bottom_rect(int x, int y, int w, int h, u16 color)
{
    u16 width = 0;
    u16 height = 0;
    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);

    if (fb == NULL || height != SCREEN_H)
        return;

    /* Clip to the real bottom framebuffer. */
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)width)
        w = (int)width - x;
    if (y + h > (int)height)
        h = (int)height - y;

    if (w <= 0 || h <= 0)
        return;

    for (int px = x; px < x + w; ++px) {
        for (int py = y; py < y + h; ++py) {
            fb[(u32)px * FB_STRIDE + (SCREEN_H - 1 - py)] = color;
        }
    }
}

static void draw_bottom_outline(int x, int y, int w, int h, u16 color)
{
    fill_bottom_rect(x, y, w, 2, color);
    fill_bottom_rect(x, y + h - 2, w, 2, color);
    fill_bottom_rect(x, y, 2, h, color);
    fill_bottom_rect(x + w - 2, y, 2, h, color);
}

static void print_centered(const char *text, int row, int columns)
{
    int length = (int)strlen(text);
    int column = ((columns - length) / 2) + 1;

    if (column < 1)
        column = 1;

    printf("\x1b[%d;%dH%s", row, column, text);
}

static void draw_menu(void)
{
    const u16 background = rgb565(25, 25, 30);
    const u16 panel = rgb565(40, 40, 48);
    const u16 inactive_tab = rgb565(58, 58, 68);
    const u16 active_tab = rgb565(70, 100, 175);
    const u16 border = rgb565(170, 170, 180);
    const u16 white = rgb565(255, 255, 255);
    const u16 close_color = rgb565(100, 45, 50);

    /* Start from a known framebuffer state every frame. */
    consoleClear();
    fill_bottom_rect(0, 0, BOT_W, SCREEN_H, background);

    /* Main menu panel. */
    fill_bottom_rect(MENU_X, MENU_Y, MENU_W, MENU_H, panel);
    draw_bottom_outline(MENU_X, MENU_Y, MENU_W, MENU_H, border);

    /* Tabs. */
    fill_bottom_rect(10, 10, 145, 28,
        active_tab == 0 ? active_tab : inactive_tab);
    fill_bottom_rect(165, 10, 145, 28,
        active_tab == 1 ? active_tab : inactive_tab);
    draw_bottom_outline(10, 10, 145, 28, border);
    draw_bottom_outline(165, 10, 145, 28, border);

    if (active_tab == 0) {
        /* Four columns by three rows of large touch targets. */
        const int x0 = 12;
        const int y0 = 46;
        const int cell_w = 70;
        const int cell_h = 43;
        const int gap = 4;

        for (int i = 0; i < COMMON_COUNT; ++i) {
            const int col = i % 4;
            const int row = i / 4;
            const int x = x0 + col * (cell_w + gap);
            const int y = y0 + row * (cell_h + gap);

            fill_bottom_rect(
                x, y, cell_w, cell_h,
                rgb565(
                    common_colors[i].r,
                    common_colors[i].g,
                    common_colors[i].b
                )
            );
            draw_bottom_outline(x, y, cell_w, cell_h, white);
        }
    } else {
        print_centered("CUSTOM COLOR", 7, 40);
        print_centered("Tap the tab to enter a HEX color", 9, 40);
        print_centered("Format: #RRGGBB", 11, 40);
        print_centered("Example: #00FF80", 13, 40);
    }

    /* Close is always in the same place. */
    fill_bottom_rect(10, 198, 300, 28, close_color);
    draw_bottom_outline(10, 198, 300, 28, border);

    /* Text is drawn last so the graphics do not cover it. */
    printf("\x1b[2;3HCOMMON COLORS");
    printf("\x1b[2;23HCUSTOM COLOR");
    print_centered("CLOSE  (B)", 26, 40);
}

static void draw_frame(void)
{
    const u16 color = rgb565(current_color.r, current_color.g, current_color.b);

    /*
     * No tinting or overlay: both screens are literally filled with the
     * selected RGB color while the app is in its normal state.
     */
    fill_screen(GFX_TOP, color);
    fill_screen(GFX_BOTTOM, color);

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

static bool parse_hex_color(const char *input, Color *result)
{
    const char *digits = input;
    size_t length = strlen(input);

    if (length == 7 && input[0] == '#') {
        digits = input + 1;
    } else if (length != 6) {
        return false;
    }

    u8 value[6];

    for (int i = 0; i < 6; ++i) {
        if (!hex_digit(digits[i], &value[i]))
            return false;
    }

    result->r = (u8)((value[0] << 4) | value[1]);
    result->g = (u8)((value[2] << 4) | value[3]);
    result->b = (u8)((value[4] << 4) | value[5]);
    result->name = "Custom";

    return true;
}

static void enter_custom_color(void)
{
    SwkbdState keyboard;
    char input[8] = {0};

    swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, 7);
    swkbdSetHintText(&keyboard, "#RRGGBB");
    swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    if (swkbdInputText(&keyboard, input, sizeof(input)) != SWKBD_BUTTON_CONFIRM)
        return;

    Color new_color;

    if (parse_hex_color(input, &new_color))
        current_color = new_color;
}

static void open_menu(void)
{
    menu_open = true;
    active_tab = 0;
}

static void handle_touch(void)
{
    touchPosition touch;
    hidTouchRead(&touch);

    const int x = touch.px;
    const int y = touch.py;

    /* Common-colors tab. */
    if (x >= 10 && x < 155 && y >= 10 && y < 38) {
        active_tab = 0;
        return;
    }

    /* Custom-color tab. */
    if (x >= 165 && x < 310 && y >= 10 && y < 38) {
        active_tab = 1;
        enter_custom_color();
        return;
    }

    /* Common color grid. */
    if (active_tab == 0 && x >= 12 && x < 304 && y >= 46 && y < 187) {
        const int x0 = 12;
        const int y0 = 46;
        const int cell_w = 70;
        const int cell_h = 43;
        const int gap = 4;

        const int col = (x - x0) / (cell_w + gap);
        const int row = (y - y0) / (cell_h + gap);
        const int local_x = (x - x0) % (cell_w + gap);
        const int local_y = (y - y0) % (cell_h + gap);

        /* Touching the gap does nothing. */
        if (col >= 0 && col < 4 && row >= 0 && row < 3 &&
            local_x < cell_w && local_y < cell_h) {
            const int index = row * 4 + col;

            if (index < COMMON_COUNT)
                current_color = common_colors[index];
        }

        return;
    }

    /* Close button. */
    if (x >= 10 && x < 310 && y >= 198 && y < 226)
        menu_open = false;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    while (aptMainLoop()) {
        hidScanInput();

        const u32 down = hidKeysDown();
        const u32 held = hidKeysHeld();

        /* B closes the menu, or exits the app if no menu is open. */
        if (down & KEY_B) {
            if (menu_open)
                menu_open = false;
            else
                break;
        }

        if (menu_open && (down & KEY_TOUCH))
            handle_touch();

        /* Open the menu after START has been held continuously for 1 second. */
        if (held & KEY_START) {
            if (!start_was_held) {
                start_hold_time = osGetTime();
                start_was_held = true;
            } else if (!menu_open &&
                       osGetTime() - start_hold_time >= START_HOLD_MS) {
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

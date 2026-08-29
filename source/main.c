#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

typedef struct {
    u8 r, g, b;
    const char *name;
} Color;

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
    {  0, 128, 255, "Sky"},
};

#define COMMON_COUNT (sizeof(common_colors) / sizeof(common_colors[0]))

static Color current_color = { 0, 0, 0, "Custom" };
static bool menu_open = false;
static int tab = 0; /* 0 = common, 1 = custom */
static u64 start_pressed_at = 0;

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/*
 * 3DS framebuffers are stored rotated. The examples in devkitPro's
 * 3DS examples use x * 240 + (239 - y) addressing.
 */
static void fill_screen(gfxScreen_t screen, u16 color)
{
    u16 w, h;
    u16 *fb = (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);

    for (u16 y = 0; y < h; y++) {
        for (u16 x = 0; x < w; x++) {
            fb[x * 240 + (239 - y)] = color;
        }
    }
}

static void rect(gfxScreen_t screen, int x, int y, int w, int h, u16 color)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > BOT_W) w = BOT_W - x;
    if (y + h > BOT_H) h = BOT_H - y;
    if (w <= 0 || h <= 0) return;

    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            fb[px * 240 + (239 - py)] = color;
        }
    }
}

static void outline(gfxScreen_t screen, int x, int y, int w, int h, u16 color)
{
    rect(screen, x, y, w, 2, color);
    rect(screen, x, y + h - 2, w, 2, color);
    rect(screen, x, y, 2, h, color);
    rect(screen, x + w - 2, y, 2, h, color);
}

static void draw_centered_text(const char *text, int row, int width_chars)
{
    int len = (int)strlen(text);
    int col = (width_chars - len) / 2 + 1;
    if (col < 1) col = 1;
    printf("\x1b[%d;%dH%s", row, col, text);
}

static void draw_ui(void)
{
    u16 base = rgb565(current_color.r, current_color.g, current_color.b);

    /* The top screen is always the selected color. */
    fill_screen(GFX_TOP, base);

    /* Bottom starts as the selected color too. */
    fill_screen(GFX_BOTTOM, base);

    if (!menu_open)
        return;

    /*
     * Clear the console first, because consoleClear() writes to the
     * framebuffer. All graphical menu elements are drawn after it.
     */
    consoleClear();

    u16 panel = rgb565(32, 32, 38);
    u16 panel2 = rgb565(48, 48, 56);
    u16 white = rgb565(245, 245, 245);
    u16 border = rgb565(120, 120, 130);

    rect(GFX_BOTTOM, 6, 6, 308, 228, panel);

    /* Tabs. */
    rect(GFX_BOTTOM, 10, 10, 145, 26,
         tab == 0 ? rgb565(70, 90, 150) : panel2);
    rect(GFX_BOTTOM, 165, 10, 145, 26,
         tab == 1 ? rgb565(70, 90, 150) : panel2);
    outline(GFX_BOTTOM, 10, 10, 145, 26, border);
    outline(GFX_BOTTOM, 165, 10, 145, 26, border);

    /* Close button. */
    rect(GFX_BOTTOM, 10, 198, 300, 28, rgb565(70, 45, 50));
    outline(GFX_BOTTOM, 10, 198, 300, 28, border);

    /*
     * Common color grid: 4 columns x 3 rows.
     * The swatches are deliberately large so they are easy to tap.
     */
    if (tab == 0) {
        const int x0 = 14, y0 = 46, cw = 68, ch = 42, gap = 7;

        for (int i = 0; i < (int)COMMON_COUNT; i++) {
            int col = i % 4;
            int row = i / 4;
            int x = x0 + col * (cw + gap);
            int y = y0 + row * (ch + gap);

            rect(GFX_BOTTOM, x, y, cw, ch,
                 rgb565(common_colors[i].r, common_colors[i].g, common_colors[i].b));
            outline(GFX_BOTTOM, x, y, cw, ch, white);
        }
    }

    /* Text is rendered last so it stays visible over the menu. */
    printf("\x1b[2;3HCOMMON COLORS");
    printf("\x1b[2;23HCUSTOM COLOR");

    if (tab == 1) {
        draw_centered_text("Enter a HEX color", 7, 40);
        draw_centered_text("Example: #FF8800", 9, 40);
        draw_centered_text("A-Z / 0-9", 11, 40);
    }

    draw_centered_text("CLOSE  (B)", 26, 40);
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
    for (int i = 0; i < 6; i++) {
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
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 7);

    swkbdSetHintText(&swkbd, "#RRGGBB");
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOT_REQUIRED, 0, 0);

    char input[8] = {0};
    SwkbdButton button = swkbdInputText(&swkbd, input, sizeof(input));

    if (button != SWKBD_BUTTON_CONFIRM)
        return;

    Color parsed;
    if (parse_hex(input, &parsed)) {
        current_color = parsed;
    } else {
        /* Invalid input: leave the current color unchanged. */
        tab = 1;
    }
}

static void open_menu(void)
{
    menu_open = true;
    tab = 0;
}

static void handle_touch(u32 kDown)
{
    if (!(kDown & KEY_TOUCH))
        return;

    touchPosition touch;
    hidTouchRead(&touch);

    int x = touch.px;
    int y = touch.py;

    if (y >= 10 && y < 36) {
        if (x >= 10 && x < 155) {
            tab = 0;
        } else if (x >= 165 && x < 310) {
            tab = 1;
            custom_color_keyboard();
        }
        return;
    }

    if (tab == 0 && x >= 14 && x < 300 && y >= 46 && y < 181) {
        const int cw = 68, ch = 42, gap = 7;
        int col = (x - 14) / (cw + gap);
        int row = (y - 46) / (ch + gap);

        if (col >= 0 && col < 4 && row >= 0 && row < 3) {
            int localX = (x - 14) % (cw + gap);
            int localY = (y - 46) % (ch + gap);

            if (localX < cw && localY < ch) {
                int index = row * 4 + col;
                if (index < (int)COMMON_COUNT)
                    current_color = common_colors[index];
            }
        }
        return;
    }

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

    current_color.r = 0;
    current_color.g = 0;
    current_color.b = 0;
    current_color.name = "Black";

    bool was_start_held = false;

    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_B) {
            if (menu_open) {
                menu_open = false;
            } else {
                break;
            }
        }

        if (menu_open) {
            handle_touch(kDown);
        }

        /*
         * Hold START for about one second to open the menu.
         * The timer is based on the 3DS system tick counter.
         */
        if (kHeld & KEY_START) {
            if (!was_start_held) {
                start_pressed_at = svcGetSystemTick();
                was_start_held = true;
            } else if (!menu_open) {
                u64 elapsed = svcGetSystemTick() - start_pressed_at;
                /* System tick is 268 MHz on the 3DS. */
                if (elapsed >= 268000000ULL) {
                    open_menu();
                }
            }
        } else {
            was_start_held = false;
        }

        draw_ui();

        gfxFlushBuffers();
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    gfxExit();
    return 0;
}

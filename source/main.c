#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    const char *name;
} Color;

/* -------------------------------------------------------------------------- */
/* Common colors                                                              */
/* -------------------------------------------------------------------------- */

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

#define COMMON_COUNT \
    (sizeof(common_colors) / sizeof(common_colors[0]))

/* -------------------------------------------------------------------------- */
/* Global state                                                               */
/* -------------------------------------------------------------------------- */

static Color current_color = {
    0, 0, 0, "Black"
};

static bool menu_open = false;

/*
 * 0 = Common Colors
 * 1 = Custom Color
 */
static int tab = 0;

static u64 start_pressed_at = 0;

/* -------------------------------------------------------------------------- */
/* RGB -> RGB565                                                              */
/* -------------------------------------------------------------------------- */

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(
        ((u16)(r >> 3) << 11) |
        ((u16)(g >> 2) << 5)  |
        ((u16)(b >> 3))
    );
}

/* -------------------------------------------------------------------------- */
/* Fill an entire screen                                                      */
/* -------------------------------------------------------------------------- */

static void fill_screen(gfxScreen_t screen, u16 color)
{
    u16 width;
    u16 height;

    u16 *fb = (u16 *)gfxGetFramebuffer(
        screen,
        GFX_LEFT,
        &width,
        &height
    );

    if (!fb)
        return;

    /*
     * 3DS framebuffers are rotated in memory.
     *
     * The framebuffer is addressed as:
     *
     *     x * 240 + (239 - y)
     */
    for (u16 y = 0; y < height; y++) {
        for (u16 x = 0; x < width; x++) {
            fb[x * 240 + (239 - y)] = color;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Draw rectangle on bottom screen                                            */
/* -------------------------------------------------------------------------- */

static void rect(
    int x,
    int y,
    int w,
    int h,
    u16 color
)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(
        GFX_BOTTOM,
        GFX_LEFT,
        NULL,
        NULL
    );

    if (!fb)
        return;

    /* Clip to bottom screen. */

    if (x < 0) {
        w += x;
        x = 0;
    }

    if (y < 0) {
        h += y;
        y = 0;
    }

    if (x + w > BOT_W)
        w = BOT_W - x;

    if (y + h > BOT_H)
        h = BOT_H - y;

    if (w <= 0 || h <= 0)
        return;

    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            fb[px * 240 + (239 - py)] = color;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Rectangle outline                                                          */
/* -------------------------------------------------------------------------- */

static void outline(
    int x,
    int y,
    int w,
    int h,
    u16 color
)
{
    rect(x, y, w, 2, color);
    rect(x, y + h - 2, w, 2, color);
    rect(x, y, 2, h, color);
    rect(x + w - 2, y, 2, h, color);
}

/* -------------------------------------------------------------------------- */
/* Center text using the 3DS console                                          */
/* -------------------------------------------------------------------------- */

static void draw_centered_text(
    const char *text,
    int row,
    int width_chars
)
{
    int len = (int)strlen(text);
    int col = (width_chars - len) / 2 + 1;

    if (col < 1)
        col = 1;

    printf(
        "\x1b[%d;%dH%s",
        row,
        col,
        text
    );
}

/* -------------------------------------------------------------------------- */
/* Draw the application                                                       */
/* -------------------------------------------------------------------------- */

static void draw_ui(void)
{
    u16 selected_color = rgb565(
        current_color.r,
        current_color.g,
        current_color.b
    );

    /*
     * IMPORTANT:
     *
     * The actual application screens are ALWAYS the selected color.
     *
     * When the menu is closed, both screens are completely filled with it.
     */
    fill_screen(GFX_TOP, selected_color);
    fill_screen(GFX_BOTTOM, selected_color);

    if (!menu_open)
        return;

    /*
     * The menu is only an application UI.
     *
     * The top screen remains completely the selected color.
     * The bottom screen contains the menu while it is open.
     */

    consoleClear();

    u16 panel = rgb565(32, 32, 38);
    u16 panel2 = rgb565(48, 48, 56);
    u16 white = rgb565(245, 245, 245);
    u16 border = rgb565(120, 120, 130);

    u16 selected_tab = rgb565(70, 90, 150);
    u16 close_color = rgb565(70, 45, 50);

    /* Main menu panel. */
    rect(
        6,
        6,
        308,
        228,
        panel
    );

    /* ---------------------------------------------------------------------- */
    /* Tabs                                                                   */
    /* ---------------------------------------------------------------------- */

    rect(
        10,
        10,
        145,
        26,
        tab == 0 ? selected_tab : panel2
    );

    rect(
        165,
        10,
        145,
        26,
        tab == 1 ? selected_tab : panel2
    );

    outline(
        10,
        10,
        145,
        26,
        border
    );

    outline(
        165,
        10,
        145,
        26,
        border
    );

    /* ---------------------------------------------------------------------- */
    /* Close button                                                           */
    /* ---------------------------------------------------------------------- */

    rect(
        10,
        198,
        300,
        28,
        close_color
    );

    outline(
        10,
        198,
        300,
        28,
        border
    );

    /* ---------------------------------------------------------------------- */
    /* Common color grid                                                      */
    /* ---------------------------------------------------------------------- */

    if (tab == 0) {

        const int x0 = 14;
        const int y0 = 46;

        const int cw = 68;
        const int ch = 42;

        const int gap = 7;

        for (int i = 0; i < (int)COMMON_COUNT; i++) {

            int col = i % 4;
            int row = i / 4;

            int x = x0 + col * (cw + gap);
            int y = y0 + row * (ch + gap);

            u16 color = rgb565(
                common_colors[i].r,
                common_colors[i].g,
                common_colors[i].b
            );

            rect(
                x,
                y,
                cw,
                ch,
                color
            );

            outline(
                x,
                y,
                cw,
                ch,
                white
            );
        }
    }

    /* ---------------------------------------------------------------------- */
    /* Custom color tab                                                       */
    /* ---------------------------------------------------------------------- */

    if (tab == 1) {

        draw_centered_text(
            "ENTER A HEX COLOR",
            7,
            40
        );

        draw_centered_text(
            "Example: #FF8800",
            9,
            40
        );

        draw_centered_text(
            "Press the tab to open keyboard",
            11,
            40
        );
    }

    /* ---------------------------------------------------------------------- */
    /* Text                                                                    */
    /* ---------------------------------------------------------------------- */

    printf(
        "\x1b[2;3HCOMMON COLORS"
    );

    printf(
        "\x1b[2;23HCUSTOM COLOR"
    );

    draw_centered_text(
        "CLOSE  (B)",
        26,
        40
    );
}

/* -------------------------------------------------------------------------- */
/* HEX digit parser                                                           */
/* -------------------------------------------------------------------------- */

static bool hex_digit(
    char c,
    u8 *value
)
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

/* -------------------------------------------------------------------------- */
/* Parse #RRGGBB / RRGGBB                                                     */
/* -------------------------------------------------------------------------- */

static bool parse_hex(
    const char *input,
    Color *out
)
{
    size_t len = strlen(input);

    const char *p = input;

    /*
     * Accept:
     *
     * #RRGGBB
     * RRGGBB
     */

    if (len == 7 && input[0] == '#') {
        p = input + 1;
    }
    else if (len != 6) {
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

/* -------------------------------------------------------------------------- */
/* Custom color keyboard                                                      */
/* -------------------------------------------------------------------------- */

static void custom_color_keyboard(void)
{
    SwkbdState swkbd;

    /*
     * Western keyboard gives us a normal text keyboard without requiring
     * Japanese input capabilities on Japanese systems.
     */
    swkbdInit(
        &swkbd,
        SWKBD_TYPE_WESTERN,
        2,
        7
    );

    swkbdSetHintText(
        &swkbd,
        "#RRGGBB"
    );

    /*
     * This is the correct libctru constant.
     *
     * SWKBD_NOTEMPTY_NOT_REQUIRED does not exist.
     */
    swkbdSetValidation(
        &swkbd,
        SWKBD_NOTEMPTY_NOTBLANK,
        0,
        0
    );

    char input[8] = {0};

    SwkbdButton button = swkbdInputText(
        &swkbd,
        input,
        sizeof(input)
    );

    if (button != SWKBD_BUTTON_CONFIRM)
        return;

    Color parsed;

    if (parse_hex(input, &parsed)) {

        /*
         * Change the application's selected color.
         *
         * On the next frame draw_ui() fills BOTH screens with it.
         */
        current_color = parsed;

    }
    else {

        /*
         * Invalid HEX:
         *
         * Keep the previous color and leave the custom tab open.
         */
        tab = 1;
    }
}

/* -------------------------------------------------------------------------- */
/* Open menu                                                                  */
/* -------------------------------------------------------------------------- */

static void open_menu(void)
{
    menu_open = true;
    tab = 0;
}

/* -------------------------------------------------------------------------- */
/* Touch input                                                                */
/* -------------------------------------------------------------------------- */

static void handle_touch(u32 kDown)
{
    if (!(kDown & KEY_TOUCH))
        return;

    touchPosition touch;

    hidTouchRead(&touch);

    int x = touch.px;
    int y = touch.py;

    /* ---------------------------------------------------------------------- */
    /* Tabs                                                                   */
    /* ---------------------------------------------------------------------- */

    if (y >= 10 && y < 36) {

        /* Common Colors */
        if (x >= 10 && x < 155) {

            tab = 0;
            return;
        }

        /* Custom Color */
        if (x >= 165 && x < 310) {

            tab = 1;

            /*
             * Immediately open the 3DS system keyboard.
             */
            custom_color_keyboard();

            return;
        }
    }

    /* ---------------------------------------------------------------------- */
    /* Common color grid                                                       */
    /* ---------------------------------------------------------------------- */

    if (
        tab == 0 &&
        x >= 14 &&
        x < 300 &&
        y >= 46 &&
        y < 181
    ) {

        const int cw = 68;
        const int ch = 42;
        const int gap = 7;

        int col = (x - 14) / (cw + gap);
        int row = (y - 46) / (ch + gap);

        if (
            col >= 0 &&
            col < 4 &&
            row >= 0 &&
            row < 3
        ) {

            int localX =
                (x - 14) % (cw + gap);

            int localY =
                (y - 46) % (ch + gap);

            /*
             * Ignore the gap between swatches.
             */
            if (
                localX < cw &&
                localY < ch
            ) {

                int index =
                    row * 4 + col;

                if (index < (int)COMMON_COUNT) {

                    /*
                     * Selecting a color immediately changes
                     * the application's screen color.
                     */
                    current_color =
                        common_colors[index];
                }
            }
        }

        return;
    }

    /* ---------------------------------------------------------------------- */
    /* Close button                                                           */
    /* ---------------------------------------------------------------------- */

    if (
        x >= 10 &&
        x < 310 &&
        y >= 198 &&
        y < 226
    ) {

        menu_open = false;
    }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /*
     * Use RGB565 because the drawing routines below write u16 pixels.
     *
     * This also means the framebuffer format matches rgb565().
     */
    gfxInit(
        GSP_RGB565_OES,
        GSP_RGB565_OES,
        false
    );

    /*
     * The console is only used while the application's menu is open.
     * It is NOT used to color the screens.
     */
    consoleInit(
        GFX_BOTTOM,
        NULL
    );

    current_color.r = 0;
    current_color.g = 0;
    current_color.b = 0;
    current_color.name = "Black";

    bool was_start_held = false;

    while (aptMainLoop()) {

        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        /* ------------------------------------------------------------------ */
        /* B button                                                            */
        /* ------------------------------------------------------------------ */

        if (kDown & KEY_B) {

            if (menu_open) {

                /*
                 * B closes the application's menu.
                 *
                 * The color remains selected.
                 */
                menu_open = false;

            }
            else {

                /*
                 * B while the menu is closed exits the app.
                 */
                break;
            }
        }

        /* ------------------------------------------------------------------ */
        /* Touch                                                                 */
        /* ------------------------------------------------------------------ */

        if (menu_open) {
            handle_touch(kDown);
        }

        /* ------------------------------------------------------------------ */
        /* Hold START for one second                                            */
        /* ------------------------------------------------------------------ */

        if (kHeld & KEY_START) {

            if (!was_start_held) {

                start_pressed_at =
                    svcGetSystemTick();

                was_start_held = true;

            }
            else if (!menu_open) {

                u64 elapsed =
                    svcGetSystemTick() -
                    start_pressed_at;

                /*
                 * The 3DS system tick runs at 268 MHz.
                 *
                 * 268,000,000 ticks = approximately one second.
                 */
                if (elapsed >= 268000000ULL) {

                    open_menu();
                }
            }

        }
        else {

            was_start_held = false;
        }

        /* ------------------------------------------------------------------ */
        /* Draw application                                                    */
        /* ------------------------------------------------------------------ */

        draw_ui();

        /*
         * Flush the framebuffer and present it.
         */
        gfxFlushBuffers();

        gfxSwapBuffers();

        gspWaitForVBlank();
    }

    gfxExit();

    return 0;
}


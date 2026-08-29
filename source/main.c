#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TOP_WIDTH 400
#define BOTTOM_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_STRIDE 240

#define HOLD_TICKS_PER_SECOND 268000000ULL
#define HOLD_ONE_SECOND HOLD_TICKS_PER_SECOND

#define MENU_X 8
#define MENU_Y 8
#define MENU_W 304
#define MENU_H 224
#define CORNER 12

#define TAB_Y 18
#define TAB_H 30
#define TAB_W 142
#define TAB1_X 18
#define TAB2_X 160

#define CLOSE_X 18
#define CLOSE_Y 194
#define CLOSE_W 284
#define CLOSE_H 28

#define GRID_X 18
#define GRID_Y 58
#define CELL_W 64
#define CELL_H 36
#define CELL_GAP_X 8
#define CELL_GAP_Y 8

#define ADVANCED_TEXT_Y 174

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    const char *name;
} Color;

typedef enum {
    TARGET_BOTH = 0,
    TARGET_TOP,
    TARGET_BOTTOM
} ColorTarget;

static const Color common_colors[] = {
    {255,   0,   0, "RED"},
    {  0, 255,   0, "GREEN"},
    {  0,   0, 255, "BLUE"},
    {255, 255,   0, "YELLOW"},
    {255,   0, 255, "MAGENTA"},
    {  0, 255, 255, "CYAN"},
    {255, 255, 255, "WHITE"},
    {  0,   0,   0, "BLACK"},
    {128, 128, 128, "GRAY"},
    {255, 128,   0, "ORANGE"},
    {128,   0, 255, "PURPLE"},
    {  0, 128, 255, "SKY"}
};

#define COMMON_COUNT ((int)(sizeof(common_colors) / sizeof(common_colors[0])))

/* The app starts as solid green on both screens. */
static Color top_color = {0, 255, 0, "GREEN"};
static Color bottom_color = {0, 255, 0, "GREEN"};
static Color custom_color = {0, 255, 0, "CUSTOM"};

static bool menu_open = false;
static bool advanced_mode = false;
static int active_tab = 0;
static bool select_was_held = false;
static u64 select_hold_started = 0;
static bool start_was_held = false;
static u64 start_hold_started = 0;

/*
 * A compact 5x7 bitmap font.  It is rendered directly into the framebuffer,
 * so there is no console, terminal, or text overlay involved.
 *
 * Each byte contains one glyph row. Only ASCII 0x20..0x5A is needed here.
 */
typedef struct {
    char ch;
    u8 rows[7];
} Glyph;

#define G(ch,a,b,c,d,e,f,g) { ch, {a,b,c,d,e,f,g} }

static const Glyph font[] = {
    G(' ',0x00,0x00,0x00,0x00,0x00,0x00,0x00),
    G('!',0x04,0x04,0x04,0x04,0x04,0x00,0x04),
    G('#',0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00),
    G('(',0x02,0x04,0x08,0x08,0x08,0x04,0x02),
    G(')',0x08,0x04,0x02,0x02,0x02,0x04,0x08),
    G('+',0x00,0x04,0x04,0x1F,0x04,0x04,0x00),
    G('-',0x00,0x00,0x00,0x1F,0x00,0x00,0x00),
    G('.',0x00,0x00,0x00,0x00,0x00,0x06,0x06),
    G('/',0x01,0x02,0x04,0x08,0x10,0x00,0x00),
    G(':',0x00,0x06,0x06,0x00,0x06,0x06,0x00),
    G('#',0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00),
    G('0',0x0E,0x11,0x13,0x15,0x19,0x11,0x0E),
    G('1',0x04,0x0C,0x04,0x04,0x04,0x04,0x0E),
    G('2',0x0E,0x11,0x01,0x02,0x04,0x08,0x1F),
    G('3',0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E),
    G('4',0x02,0x06,0x0A,0x12,0x1F,0x02,0x02),
    G('5',0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E),
    G('6',0x06,0x08,0x10,0x1E,0x11,0x11,0x0E),
    G('7',0x1F,0x01,0x02,0x04,0x08,0x08,0x08),
    G('8',0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E),
    G('9',0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C),
    G('A',0x0E,0x11,0x11,0x1F,0x11,0x11,0x11),
    G('B',0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E),
    G('C',0x0E,0x11,0x10,0x10,0x10,0x11,0x0E),
    G('D',0x1E,0x11,0x11,0x11,0x11,0x11,0x1E),
    G('E',0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F),
    G('F',0x1F,0x10,0x10,0x1E,0x10,0x10,0x10),
    G('G',0x0E,0x11,0x10,0x17,0x11,0x11,0x0E),
    G('H',0x11,0x11,0x11,0x1F,0x11,0x11,0x11),
    G('I',0x0E,0x04,0x04,0x04,0x04,0x04,0x0E),
    G('J',0x01,0x01,0x01,0x01,0x11,0x11,0x0E),
    G('K',0x11,0x12,0x14,0x18,0x14,0x12,0x11),
    G('L',0x10,0x10,0x10,0x10,0x10,0x10,0x1F),
    G('M',0x11,0x1B,0x15,0x15,0x11,0x11,0x11),
    G('N',0x11,0x19,0x19,0x15,0x13,0x13,0x11),
    G('O',0x0E,0x11,0x11,0x11,0x11,0x11,0x0E),
    G('P',0x1E,0x11,0x11,0x1E,0x10,0x10,0x10),
    G('Q',0x0E,0x11,0x11,0x11,0x15,0x12,0x0D),
    G('R',0x1E,0x11,0x11,0x1E,0x14,0x12,0x11),
    G('S',0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E),
    G('T',0x1F,0x04,0x04,0x04,0x04,0x04,0x04),
    G('U',0x11,0x11,0x11,0x11,0x11,0x11,0x0E),
    G('V',0x11,0x11,0x11,0x11,0x11,0x0A,0x04),
    G('W',0x11,0x11,0x11,0x15,0x15,0x15,0x0A),
    G('X',0x11,0x11,0x0A,0x04,0x0A,0x11,0x11),
    G('Y',0x11,0x11,0x0A,0x04,0x04,0x04,0x04),
    G('Z',0x1F,0x01,0x02,0x04,0x08,0x10,0x1F)
};

#undef G

static const Glyph *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) {
        if (font[i].ch == c)
            return &font[i];
    }
    return &font[0];
}

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(((u16)(r >> 3) << 11) |
                 ((u16)(g >> 2) << 5) |
                 (u16)(b >> 3));
}

static u16 *framebuffer(gfxScreen_t screen, u16 *width, u16 *height)
{
    return (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, width, height);
}

static void put_pixel(u16 *fb, int x, int y, u16 color)
{
    if (!fb || x < 0 || x >= TOP_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    fb[(u32)x * FB_STRIDE + (SCREEN_HEIGHT - 1 - y)] = color;
}

static void fill_rect(gfxScreen_t screen, int x, int y, int w, int h, u16 color)
{
    u16 width = 0, height = 0;
    u16 *fb = framebuffer(screen, &width, &height);
    if (!fb)
        return;

    int max_width = (int)width;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > max_width) w = max_width - x;
    if (y + h > (int)height) h = (int)height - y;
    if (w <= 0 || h <= 0)
        return;

    for (int px = x; px < x + w; ++px) {
        u32 base = (u32)px * FB_STRIDE;
        for (int py = y; py < y + h; ++py)
            fb[base + (SCREEN_HEIGHT - 1 - py)] = color;
    }
}

static void fill_circle(gfxScreen_t screen, int cx, int cy, int radius, u16 color)
{
    u16 width = 0, height = 0;
    u16 *fb = framebuffer(screen, &width, &height);
    if (!fb)
        return;

    for (int y = -radius; y <= radius; ++y) {
        int dx = radius;
        while (dx * dx + y * y > radius * radius)
            --dx;
        for (int x = -dx; x <= dx; ++x)
            put_pixel(fb, cx + x, cy + y, color);
    }
}

static void rounded_rect(gfxScreen_t screen, int x, int y, int w, int h,
                         int radius, u16 color)
{
    if (radius < 1) {
        fill_rect(screen, x, y, w, h, color);
        return;
    }

    fill_rect(screen, x + radius, y, w - 2 * radius, h, color);
    fill_rect(screen, x, y + radius, w, h - 2 * radius, color);
    fill_circle(screen, x + radius, y + radius, radius, color);
    fill_circle(screen, x + w - radius - 1, y + radius, radius, color);
    fill_circle(screen, x + radius, y + h - radius - 1, radius, color);
    fill_circle(screen, x + w - radius - 1, y + h - radius - 1, radius, color);
}

static void rounded_outline(gfxScreen_t screen, int x, int y, int w, int h,
                            int radius, int thickness, u16 color)
{
    rounded_rect(screen, x, y, w, h, radius, color);
    if (w > thickness * 2 && h > thickness * 2)
        rounded_rect(screen, x + thickness, y + thickness,
                     w - thickness * 2, h - thickness * 2,
                     radius > thickness ? radius - thickness : 1,
                     rgb565(0, 0, 0));
}

/* Draw text directly into the current framebuffer. */
static void draw_text(gfxScreen_t screen, int x, int y, const char *text,
                      int scale, u16 color)
{
    u16 width = 0, height = 0;
    u16 *fb = framebuffer(screen, &width, &height);
    if (!fb || !text || scale < 1)
        return;

    int cursor = x;
    while (*text) {
        const Glyph *glyph = find_glyph(*text++);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (glyph->rows[gy] & (1u << (4 - gx))) {
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx)
                            put_pixel(fb, cursor + gx * scale + sx,
                                      y + gy * scale + sy, color);
                }
            }
        }
        cursor += 6 * scale;
    }
}

static int text_width(const char *text, int scale)
{
    int length = (int)strlen(text);
    return length * 6 * scale;
}

static void draw_text_centered(gfxScreen_t screen, int y, int center_x,
                               const char *text, int scale, u16 color)
{
    int width = text_width(text, scale);
    draw_text(screen, center_x - width / 2, y, text, scale, color);
}

static void fill_color_screen(gfxScreen_t screen, Color color)
{
    fill_rect(screen, 0, 0,
              screen == GFX_TOP ? TOP_WIDTH : BOTTOM_WIDTH,
              SCREEN_HEIGHT,
              rgb565(color.r, color.g, color.b));
}

static void apply_color(Color color, ColorTarget target)
{
    if (target == TARGET_BOTH || target == TARGET_TOP)
        top_color = color;
    if (target == TARGET_BOTH || target == TARGET_BOTTOM)
        bottom_color = color;
}

static bool hex_digit(char c, u8 *value)
{
    if (c >= '0' && c <= '9') { *value = (u8)(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { *value = (u8)(c - 'A' + 10); return true; }
    if (c >= 'a' && c <= 'f') { *value = (u8)(c - 'a' + 10); return true; }
    return false;
}

static bool parse_hex(const char *input, Color *out)
{
    size_t len = strlen(input);
    const char *p = input;

    if (len == 7 && input[0] == '#')
        p = input + 1;
    else if (len != 6)
        return false;

    for (int i = 0; i < 6; ++i) {
        u8 value;
        if (!hex_digit(p[i], &value))
            return false;
    }

    u8 d[6];
    for (int i = 0; i < 6; ++i)
        hex_digit(p[i], &d[i]);

    out->r = (u8)((d[0] << 4) | d[1]);
    out->g = (u8)((d[2] << 4) | d[3]);
    out->b = (u8)((d[4] << 4) | d[5]);
    out->name = "CUSTOM";
    return true;
}

static void custom_keyboard(void)
{
    SwkbdState state;
    char input[8] = {0};

    swkbdInit(&state, SWKBD_TYPE_WESTERN, 2, 7);
    swkbdSetHintText(&state, "#RRGGBB");
    swkbdSetValidation(&state, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    if (swkbdInputText(&state, input, sizeof(input)) != SWKBD_BUTTON_CONFIRM)
        return;

    Color parsed;
    if (parse_hex(input, &parsed)) {
        custom_color = parsed;
        active_tab = 1;
        apply_color(custom_color, advanced_mode ?
                    (hidKeysHeld() & KEY_L ? TARGET_BOTTOM :
                     (hidKeysHeld() & KEY_R ? TARGET_TOP : TARGET_BOTH)) :
                    TARGET_BOTH);
    }
}

static ColorTarget advanced_target(u32 held)
{
    if (!advanced_mode)
        return TARGET_BOTH;
    if ((held & KEY_L) && !(held & KEY_R))
        return TARGET_BOTTOM;
    if ((held & KEY_R) && !(held & KEY_L))
        return TARGET_TOP;
    return TARGET_BOTH;
}

static void draw_color_cell(int index)
{
    const int col = index % 4;
    const int row = index / 4;
    const int x = GRID_X + col * (CELL_W + CELL_GAP_X);
    const int y = GRID_Y + row * (CELL_H + CELL_GAP_Y);
    Color c = common_colors[index];

    rounded_rect(GFX_BOTTOM, x, y, CELL_W, CELL_H, 7,
                 rgb565(c.r, c.g, c.b));
}

static void draw_menu(u32 held)
{
    const u16 bg = rgb565(18, 20, 25);
    const u16 panel = rgb565(37, 40, 48);
    const u16 tab = rgb565(55, 59, 70);
    const u16 selected = rgb565(64, 103, 190);
    const u16 text = rgb565(245, 247, 250);
    const u16 muted = rgb565(170, 176, 188);
    const u16 close = rgb565(122, 48, 55);
    const u16 border = rgb565(100, 108, 125);

    fill_color_screen(GFX_BOTTOM, (Color){18, 20, 25, "MENU"});
    rounded_rect(GFX_BOTTOM, MENU_X, MENU_Y, MENU_W, MENU_H, CORNER, panel);

    rounded_rect(GFX_BOTTOM, TAB1_X, TAB_Y, TAB_W, TAB_H, 9,
                 active_tab == 0 ? selected : tab);
    rounded_rect(GFX_BOTTOM, TAB2_X, TAB_Y, TAB_W, TAB_H, 9,
                 active_tab == 1 ? selected : tab);

    draw_text_centered(GFX_BOTTOM, TAB_Y + 9, TAB1_X + TAB_W / 2,
                       "COMMON", 2, text);
    draw_text_centered(GFX_BOTTOM, TAB_Y + 9, TAB2_X + TAB_W / 2,
                       "CUSTOM", 2, text);

    if (active_tab == 0) {
        for (int i = 0; i < COMMON_COUNT; ++i)
            draw_color_cell(i);

        if (advanced_mode) {
            draw_text_centered(GFX_BOTTOM, ADVANCED_TEXT_Y, BOTTOM_WIDTH / 2,
                               "ADVANCED L=BOT R=TOP", 1, muted);
        } else {
            draw_text_centered(GFX_BOTTOM, ADVANCED_TEXT_Y, BOTTOM_WIDTH / 2,
                               "SELECT 1S: ADVANCED", 1, muted);
        }
    } else {
        rounded_rect(GFX_BOTTOM, 36, 70, 248, 58, 10, tab);
        draw_text_centered(GFX_BOTTOM, 82, BOTTOM_WIDTH / 2,
                           "ENTER HEX COLOR", 2, text);
        draw_text_centered(GFX_BOTTOM, 106, BOTTOM_WIDTH / 2,
                           "TAP TO OPEN KEYBOARD", 1, muted);

        if (advanced_mode)
            draw_text_centered(GFX_BOTTOM, 146, BOTTOM_WIDTH / 2,
                               "L=BOT R=TOP", 1, muted);
    }

    rounded_rect(GFX_BOTTOM, CLOSE_X, CLOSE_Y, CLOSE_W, CLOSE_H, 9, close);
    draw_text_centered(GFX_BOTTOM, CLOSE_Y + 8, BOTTOM_WIDTH / 2,
                       "CLOSE  B", 2, text);

    /* A subtle border around the close control makes the touch target clear. */
    (void)border;
    (void)held;
}

static void draw_frame(u32 held)
{
    /* Each normal screen is filled completely with its actual selected color. */
    fill_color_screen(GFX_TOP, top_color);
    fill_color_screen(GFX_BOTTOM, bottom_color);

    if (menu_open)
        draw_menu(held);
}

static void handle_touch(u32 held)
{
    touchPosition touch;
    hidTouchRead(&touch);
    int x = touch.px;
    int y = touch.py;

    if (x >= TAB1_X && x < TAB1_X + TAB_W &&
        y >= TAB_Y && y < TAB_Y + TAB_H) {
        active_tab = 0;
        return;
    }

    if (x >= TAB2_X && x < TAB2_X + TAB_W &&
        y >= TAB_Y && y < TAB_Y + TAB_H) {
        active_tab = 1;
        return;
    }

    if (active_tab == 0 && x >= GRID_X && y >= GRID_Y) {
        int col = (x - GRID_X) / (CELL_W + CELL_GAP_X);
        int row = (y - GRID_Y) / (CELL_H + CELL_GAP_Y);
        int local_x = (x - GRID_X) % (CELL_W + CELL_GAP_X);
        int local_y = (y - GRID_Y) % (CELL_H + CELL_GAP_Y);

        if (col >= 0 && col < 4 && row >= 0 && row < 3 &&
            local_x < CELL_W && local_y < CELL_H) {
            int index = row * 4 + col;
            if (index < COMMON_COUNT)
                apply_color(common_colors[index], advanced_target(held));
        }
        return;
    }

    if (active_tab == 1 && x >= 36 && x < 284 && y >= 70 && y < 128) {
        custom_keyboard();
        return;
    }

    if (x >= CLOSE_X && x < CLOSE_X + CLOSE_W &&
        y >= CLOSE_Y && y < CLOSE_Y + CLOSE_H) {
        menu_open = false;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    hidInit();

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();

        /* SELECT held for one second always exits the app. */
        if (held & KEY_SELECT) {
            if (!select_was_held) {
                select_hold_started = svcGetSystemTick();
                select_was_held = true;
            } else if (svcGetSystemTick() - select_hold_started >= HOLD_ONE_SECOND) {
                break;
            }
        } else {
            select_was_held = false;
        }

        /* START held for one second opens the menu from anywhere. */
        if (held & KEY_START) {
            if (!start_was_held) {
                start_hold_started = svcGetSystemTick();
                start_was_held = true;
            } else if (!menu_open &&
                       svcGetSystemTick() - start_hold_started >= HOLD_ONE_SECOND) {
                menu_open = true;
                active_tab = 0;
            }
        } else {
            start_was_held = false;
        }

        if (menu_open) {
            if (down & KEY_B) {
                menu_open = false;
            } else if (down & KEY_TOUCH) {
                handle_touch(held);
            }

            /* Hold SELECT for one second in the menu to toggle Advanced Mode. */
            if ((held & KEY_SELECT) && select_was_held &&
                svcGetSystemTick() - select_hold_started >= HOLD_ONE_SECOND) {
                /* Exit has priority when SELECT is held anywhere. */
                break;
            }
        }

        draw_frame(held);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    hidExit();
    gfxExit();
    return 0;
}

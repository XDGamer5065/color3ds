#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    u8 r, g, b;
} Color;

typedef enum {
    TARGET_BOTH,
    TARGET_TOP,
    TARGET_BOTTOM
} ColorTarget;

typedef struct {
    u8 *data;
    int width;
    int height;
} Frame;

#define SCREEN_H 240
#define TOP_W 400
#define BOTTOM_W 320
#define MENU_X 8
#define MENU_Y 8
#define MENU_W 304
#define MENU_H 224

static const Color colors[] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 0},
    {255, 0, 255},
    {0, 255, 255},
    {255, 255, 255},
    {0, 0, 0},
    {128, 128, 128},
    {255, 128, 0},
    {128, 0, 255},
    {0, 128, 255}
};

#define COLOR_COUNT ((int)(sizeof(colors) / sizeof(colors[0])))

static Color top_color = {0, 255, 0};
static Color bottom_color = {0, 255, 0};
static Color custom_color = {0, 255, 0};

static bool menu_open = false;
static bool advanced_mode = false;
static int tab = 0;

static bool start_holding = false;
static u64 start_tick = 0;
static bool select_holding = false;
static u64 select_tick = 0;

/*
 * libctru's default framebuffer format is BGR8, not RGB565.
 *
 * gfxInitDefault() calls gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false),
 * so every pixel occupies THREE bytes. The framebuffer is also stored
 * sideways: logical (x,y) is at (x * 240 + (239-y)) * 3.
 *
 * Treating this framebuffer as u16 was the reason for the colored lines,
 * corrupted halves, and broken menu seen on hardware.
 */
static Frame get_frame(gfxScreen_t screen)
{
    Frame f;
    u16 width = 0;
    u16 height = 0;

    f.data = gfxGetFramebuffer(screen, GFX_LEFT, &width, &height);
    f.width = (int)width;
    f.height = (int)height;
    return f;
}

static inline bool frame_valid(const Frame *f)
{
    return f != NULL && f->data != NULL && f->width > 0 && f->height > 0;
}

static inline u8 *pixel_ptr(const Frame *f, int x, int y)
{
    return f->data + ((x * SCREEN_H) + (SCREEN_H - 1 - y)) * 3;
}

static inline void put_pixel(const Frame *f, int x, int y, Color c)
{
    if (!frame_valid(f) || x < 0 || y < 0 || x >= f->width || y >= f->height)
        return;

    u8 *p = pixel_ptr(f, x, y);
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

static void clear_frame(const Frame *f, Color c)
{
    if (!frame_valid(f))
        return;

    for (int x = 0; x < f->width; x++) {
        for (int y = 0; y < f->height; y++) {
            u8 *p = pixel_ptr(f, x, y);
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
}

static void fill_rect(const Frame *f, int x, int y, int w, int h, Color c)
{
    if (!frame_valid(f) || w <= 0 || h <= 0)
        return;

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > f->width)
        w = f->width - x;
    if (y + h > f->height)
        h = f->height - y;
    if (w <= 0 || h <= 0)
        return;

    for (int px = x; px < x + w; px++) {
        for (int py = y; py < y + h; py++) {
            u8 *p = pixel_ptr(f, px, py);
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
}

static void fill_circle(const Frame *f, int cx, int cy, int radius, Color c)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius)
                put_pixel(f, cx + x, cy + y, c);
        }
    }
}

static void rounded_rect(const Frame *f, int x, int y, int w, int h, int radius, Color c)
{
    if (radius <= 0) {
        fill_rect(f, x, y, w, h, c);
        return;
    }

    if (radius * 2 > w)
        radius = w / 2;
    if (radius * 2 > h)
        radius = h / 2;

    fill_rect(f, x + radius, y, w - radius * 2, h, c);
    fill_rect(f, x, y + radius, w, h - radius * 2, c);

    fill_circle(f, x + radius, y + radius, radius, c);
    fill_circle(f, x + w - radius - 1, y + radius, radius, c);
    fill_circle(f, x + radius, y + h - radius - 1, radius, c);
    fill_circle(f, x + w - radius - 1, y + h - radius - 1, radius, c);
}

typedef struct {
    char character;
    u8 rows[7];
} Glyph;

#define GLYPH(c,a,b,d,e,f,g,h) { c, { a,b,d,e,f,g,h } }

static const Glyph font[] = {
    GLYPH(' ',0,0,0,0,0,0,0),
    GLYPH('#',10,31,10,10,31,10,0),
    GLYPH('-',0,0,0,31,0,0,0),
    GLYPH('.',0,0,0,0,0,6,6),
    GLYPH(':',0,6,6,0,6,6,0),
    GLYPH('0',14,17,19,21,25,17,14),
    GLYPH('1',4,12,4,4,4,4,14),
    GLYPH('2',14,17,1,2,4,8,31),
    GLYPH('3',30,1,1,14,1,1,30),
    GLYPH('4',2,6,10,18,31,2,2),
    GLYPH('5',31,16,16,30,1,1,30),
    GLYPH('6',6,8,16,30,17,17,14),
    GLYPH('7',31,1,2,4,8,8,8),
    GLYPH('8',14,17,17,14,17,17,14),
    GLYPH('9',14,17,17,15,1,2,12),
    GLYPH('A',14,17,17,31,17,17,17),
    GLYPH('B',30,17,17,30,17,17,30),
    GLYPH('C',14,17,16,16,16,17,14),
    GLYPH('D',30,17,17,17,17,17,30),
    GLYPH('E',31,16,16,30,16,16,31),
    GLYPH('F',31,16,16,30,16,16,16),
    GLYPH('G',14,17,16,23,17,17,14),
    GLYPH('H',17,17,17,31,17,17,17),
    GLYPH('I',14,4,4,4,4,4,14),
    GLYPH('J',1,1,1,1,17,17,14),
    GLYPH('K',17,18,20,24,20,18,17),
    GLYPH('L',16,16,16,16,16,16,31),
    GLYPH('M',17,27,21,21,17,17,17),
    GLYPH('N',17,25,21,21,19,19,17),
    GLYPH('O',14,17,17,17,17,17,14),
    GLYPH('P',30,17,17,30,16,16,16),
    GLYPH('Q',14,17,17,17,21,18,13),
    GLYPH('R',30,17,17,30,20,18,17),
    GLYPH('S',15,16,16,14,1,1,30),
    GLYPH('T',31,4,4,4,4,4,4),
    GLYPH('U',17,17,17,17,17,17,14),
    GLYPH('V',17,17,17,17,17,10,4),
    GLYPH('W',17,17,17,21,21,21,10),
    GLYPH('X',17,17,10,4,10,17,17),
    GLYPH('Y',17,17,10,4,4,4,4),
    GLYPH('Z',31,1,2,4,8,16,31)
};

#undef GLYPH

static const Glyph *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].character == c)
            return &font[i];
    }
    return &font[0];
}

static void draw_text(const Frame *f, int x, int y, const char *text, int scale, Color c)
{
    int cursor = x;

    if (!text || scale < 1)
        return;

    while (*text) {
        const Glyph *g = find_glyph(*text++);

        for (int gy = 0; gy < 7; gy++) {
            for (int gx = 0; gx < 5; gx++) {
                if (!(g->rows[gy] & (1u << (4 - gx))))
                    continue;

                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++)
                        put_pixel(f, cursor + gx * scale + sx,
                                  y + gy * scale + sy, c);
                }
            }
        }

        cursor += 6 * scale;
    }
}

static void centered_text(const Frame *f, int center, int y,
                          const char *text, int scale, Color c)
{
    int width = (int)strlen(text) * 6 * scale;
    draw_text(f, center - width / 2, y, text, scale, c);
}

static void apply_color(Color color, ColorTarget target)
{
    if (target == TARGET_BOTH || target == TARGET_TOP)
        top_color = color;
    if (target == TARGET_BOTH || target == TARGET_BOTTOM)
        bottom_color = color;
}

static ColorTarget get_target(u32 held)
{
    if (!advanced_mode)
        return TARGET_BOTH;

    if ((held & KEY_L) && !(held & KEY_R))
        return TARGET_BOTTOM;
    if ((held & KEY_R) && !(held & KEY_L))
        return TARGET_TOP;

    return TARGET_BOTH;
}

static bool hex_digit(char c, u8 *value)
{
    if (c >= '0' && c <= '9') {
        *value = (u8)(c - '0');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *value = (u8)(c - 'A' + 10);
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *value = (u8)(c - 'a' + 10);
        return true;
    }
    return false;
}

static bool parse_hex(const char *input, Color *out)
{
    const char *p = input;
    size_t length = strlen(input);
    u8 digits[6];

    if (length == 7 && input[0] == '#')
        p++;
    else if (length != 6)
        return false;

    for (int i = 0; i < 6; i++) {
        if (!hex_digit(p[i], &digits[i]))
            return false;
    }

    out->r = (u8)((digits[0] << 4) | digits[1]);
    out->g = (u8)((digits[2] << 4) | digits[3]);
    out->b = (u8)((digits[4] << 4) | digits[5]);
    return true;
}

static void open_keyboard(ColorTarget target)
{
    SwkbdState keyboard;
    char input[8] = {0};

    swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, 7);
    swkbdSetHintText(&keyboard, "#RRGGBB");
    swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    if (swkbdInputText(&keyboard, input, sizeof(input)) != SWKBD_BUTTON_CONFIRM)
        return;

    Color parsed;
    if (parse_hex(input, &parsed)) {
        custom_color = parsed;
        apply_color(custom_color, target);
    }
}

static void draw_menu(const Frame *f)
{
    const Color background = {12, 14, 19};
    const Color panel = {35, 39, 48};
    const Color inactive = {53, 58, 70};
    const Color selected = {58, 102, 190};
    const Color white = {245, 247, 250};
    const Color muted = {175, 182, 194};
    const Color close = {125, 48, 55};
    const Color advanced = {48, 125, 76};

    clear_frame(f, background);
    rounded_rect(f, MENU_X, MENU_Y, MENU_W, MENU_H, 12, panel);

    rounded_rect(f, 18, 18, 142, 30, 8, tab == 0 ? selected : inactive);
    rounded_rect(f, 160, 18, 142, 30, 8, tab == 1 ? selected : inactive);

    centered_text(f, 89, 27, "COMMON", 2, white);
    centered_text(f, 231, 27, "CUSTOM", 2, white);

    if (tab == 0) {
        for (int i = 0; i < COLOR_COUNT; i++) {
            int column = i % 4;
            int row = i / 4;
            int x = 18 + column * 72;
            int y = 58 + row * 41;
            rounded_rect(f, x, y, 64, 34, 7, colors[i]);
        }
    } else {
        const Color button = {53, 58, 70};
        rounded_rect(f, 34, 68, 252, 60, 10, button);
        centered_text(f, 160, 83, "ENTER HEX", 2, white);
        centered_text(f, 160, 107, "TAP TO TYPE", 1, muted);
    }

    rounded_rect(f, 18, 166, 284, 22, 8,
                 advanced_mode ? advanced : inactive);
    centered_text(f, 160, 173,
                  advanced_mode ? "L BOTTOM R TOP" : "ADVANCED OFF",
                  1, white);

    rounded_rect(f, 18, 194, 284, 28, 8, close);
    centered_text(f, 160, 203, "CLOSE MENU", 2, white);
}

static void render(void)
{
    Frame top = get_frame(GFX_TOP);
    Frame bottom = get_frame(GFX_BOTTOM);

    clear_frame(&top, top_color);
    clear_frame(&bottom, bottom_color);

    if (menu_open)
        draw_menu(&bottom);

    gfxFlushBuffers();
}

static void handle_touch(u32 keys_down, u32 keys_held)
{
    if (!(keys_down & KEY_TOUCH) || !menu_open)
        return;

    touchPosition touch;
    hidTouchRead(&touch);

    int x = (int)touch.px;
    int y = (int)touch.py;
    ColorTarget target = get_target(keys_held);

    if (y >= 18 && y < 48) {
        if (x >= 18 && x < 160) {
            tab = 0;
        } else if (x >= 160 && x < 302) {
            tab = 1;
        }
        return;
    }

    if (tab == 0 && x >= 18 && x < 306 && y >= 58 && y < 181) {
        int column = (x - 18) / 72;
        int row = (y - 58) / 41;
        int local_x = (x - 18) % 72;
        int local_y = (y - 58) % 41;

        if (column >= 0 && column < 4 && row >= 0 && row < 3 &&
            local_x < 64 && local_y < 34) {
            int index = row * 4 + column;
            if (index < COLOR_COUNT)
                apply_color(colors[index], target);
        }
        return;
    }

    if (tab == 1 && x >= 34 && x < 286 && y >= 68 && y < 128) {
        open_keyboard(target);
        return;
    }

    if (x >= 18 && x < 302 && y >= 166 && y < 188) {
        advanced_mode = !advanced_mode;
        return;
    }

    if (x >= 18 && x < 302 && y >= 194 && y < 222) {
        menu_open = false;
    }
}

static bool held_for_one_second(bool held, bool *was_held, u64 *start)
{
    if (held) {
        if (!*was_held) {
            *was_held = true;
            *start = svcGetSystemTick();
        } else if (svcGetSystemTick() - *start >= 268000000ULL) {
            return true;
        }
    } else {
        *was_held = false;
    }

    return false;
}

int main(void)
{
    gfxInitDefault();
    gfxSet3D(false);

    while (aptMainLoop()) {
        hidScanInput();

        u32 keys_down = hidKeysDown();
        u32 keys_held = hidKeysHeld();

        /* SELECT held for one second exits from anywhere in the app. */
        if (held_for_one_second(keys_held & KEY_SELECT,
                                &select_holding, &select_tick)) {
            break;
        }

        /* START held for one second opens the menu. */
        if (!menu_open && held_for_one_second(keys_held & KEY_START,
                                              &start_holding, &start_tick)) {
            menu_open = true;
            tab = 0;
            start_holding = false;
        }

        if (menu_open) {
            if (keys_down & KEY_B)
                menu_open = false;

            handle_touch(keys_down, keys_held);
        }

        render();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Screen Color Changer
 *
 * The application owns both framebuffers while it is running.  The top and
 * bottom screens are filled directly with RGB888 pixels; no terminal or
 * console rendering is used.
 */

typedef struct {
    u8 r, g, b;
} Color;

typedef enum {
    TARGET_BOTH,
    TARGET_TOP,
    TARGET_BOTTOM
} Target;

typedef struct {
    u8 *data;
    int width;
    int height;
} Frame;

enum {
    FB_STRIDE = 240,
    FB_BPP = 3,
    TOP_WIDTH = 400,
    BOTTOM_WIDTH = 320,
    SCREEN_HEIGHT = 240
};

#define HOLD_TICKS 268000000ULL
#define NO_BUTTON (-1)
#define BTN_TAB_COMMON 0
#define BTN_TAB_CUSTOM 1
#define BTN_CLOSE 2
#define BTN_APPLY 3
#define BTN_COLOR_BASE 10
#define BTN_SLIDER_R 30
#define BTN_SLIDER_G 31
#define BTN_SLIDER_B 32

static const Color common_colors[] = {
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

#define COMMON_COUNT ((int)(sizeof(common_colors) / sizeof(common_colors[0])))

static Color top_color = {0, 255, 0};
static Color bottom_color = {0, 255, 0};
static Color custom_color = {0, 255, 0};

static bool menu_open = false;
static int tab = 0;

static bool start_held = false;
static bool select_held = false;
static u64 start_tick = 0;
static u64 select_tick = 0;

static int pressed_button = NO_BUTTON;
static Target pressed_target = TARGET_BOTH;

static Frame frame_get(gfxScreen_t screen)
{
    Frame f;
    u16 pitch_width = 0;
    u16 pitch_height = 0;

    f.data = (u8 *)gfxGetFramebuffer(screen, GFX_LEFT, &pitch_width, &pitch_height);
    f.width = (screen == GFX_TOP) ? TOP_WIDTH : BOTTOM_WIDTH;
    f.height = SCREEN_HEIGHT;

    return f;
}

static inline bool frame_ok(const Frame *f)
{
    return f != NULL && f->data != NULL && f->width > 0 && f->height > 0;
}

/*
 * The 3DS framebuffer is column-major from the application's point of view.
 * Each logical pixel is RGB888 and columns are separated by 240 pixels.
 */
static inline u8 *pixel(const Frame *f, int x, int y)
{
    return f->data + ((x * FB_STRIDE) + (SCREEN_HEIGHT - 1 - y)) * FB_BPP;
}

static inline void put_pixel(const Frame *f, int x, int y, Color c)
{
    if (!frame_ok(f) || x < 0 || y < 0 || x >= f->width || y >= f->height)
        return;

    u8 *p = pixel(f, x, y);
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

static void clear_frame(const Frame *f, Color c)
{
    if (!frame_ok(f))
        return;

    for (int x = 0; x < f->width; x++) {
        u8 *base = f->data + x * FB_STRIDE * FB_BPP;

        for (int y = 0; y < f->height; y++) {
            u8 *p = base + (SCREEN_HEIGHT - 1 - y) * FB_BPP;
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
}

static void fill_rect(const Frame *f, int x, int y, int w, int h, Color c)
{
    if (!frame_ok(f) || w <= 0 || h <= 0)
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
        u8 *base = f->data + px * FB_STRIDE * FB_BPP;

        for (int py = y; py < y + h; py++) {
            u8 *p = base + (SCREEN_HEIGHT - 1 - py) * FB_BPP;
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
}

static void circle(const Frame *f, int cx, int cy, int r, Color c)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r)
                put_pixel(f, cx + x, cy + y, c);
        }
    }
}

static void rounded_rect(const Frame *f, int x, int y, int w, int h, int r, Color c)
{
    if (r <= 0) {
        fill_rect(f, x, y, w, h, c);
        return;
    }

    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;

    fill_rect(f, x + r, y, w - r * 2, h, c);
    fill_rect(f, x, y + r, w, h - r * 2, c);

    circle(f, x + r, y + r, r, c);
    circle(f, x + w - r - 1, y + r, r, c);
    circle(f, x + r, y + h - r - 1, r, c);
    circle(f, x + w - r - 1, y + h - r - 1, r, c);
}

/* Small built-in bitmap font. It is rendered directly into the framebuffer. */
typedef struct {
    char c;
    u8 rows[7];
} Glyph;

#define G(ch,a,b,c,d,e,f,g) {ch,{a,b,c,d,e,f,g}}

static const Glyph font[] = {
    G(' ', 0,0,0,0,0,0,0),
    G('#', 10,31,10,10,31,10,0),
    G('.', 0,0,0,0,0,6,6),
    G(':', 0,6,6,0,6,6,0),
    G('-', 0,0,0,31,0,0,0),
    G('0', 14,17,19,21,25,17,14),
    G('1', 4,12,4,4,4,4,14),
    G('2', 14,17,1,2,4,8,31),
    G('3', 30,1,1,14,1,1,30),
    G('4', 2,6,10,18,31,2,2),
    G('5', 31,16,16,30,1,1,30),
    G('6', 6,8,16,30,17,17,14),
    G('7', 31,1,2,4,8,8,8),
    G('8', 14,17,17,14,17,17,14),
    G('9', 14,17,17,15,1,2,12),
    G('A', 14,17,17,31,17,17,17),
    G('B', 30,17,17,30,17,17,30),
    G('C', 14,17,16,16,16,17,14),
    G('D', 30,17,17,17,17,17,30),
    G('E', 31,16,16,30,16,16,31),
    G('F', 31,16,16,30,16,16,16),
    G('G', 14,17,16,23,17,17,14),
    G('H', 17,17,17,31,17,17,17),
    G('I', 14,4,4,4,4,4,14),
    G('J', 1,1,1,1,17,17,14),
    G('K', 17,18,20,24,20,18,17),
    G('L', 16,16,16,16,16,16,31),
    G('M', 17,27,21,21,17,17,17),
    G('N', 17,25,21,21,19,19,17),
    G('O', 14,17,17,17,17,17,14),
    G('P', 30,17,17,30,16,16,16),
    G('Q', 14,17,17,17,21,18,13),
    G('R', 30,17,17,30,20,18,17),
    G('S', 15,16,16,14,1,1,30),
    G('T', 31,4,4,4,4,4,4),
    G('U', 17,17,17,17,17,17,14),
    G('V', 17,17,17,17,17,10,4),
    G('W', 17,17,17,21,21,21,10),
    G('X', 17,17,10,4,10,17,17),
    G('Y', 17,17,10,4,4,4,4),
    G('Z', 31,1,2,4,8,16,31)
};

#undef G

static const Glyph *glyph(char c)
{
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
        if (font[i].c == c)
            return &font[i];
    }

    return &font[0];
}

static void text(const Frame *f, int x, int y, const char *s, int scale, Color c)
{
    int cur = x;

    while (s != NULL && *s != '\0') {
        const Glyph *g = glyph(*s++);

        for (int gy = 0; gy < 7; gy++) {
            for (int gx = 0; gx < 5; gx++) {
                if (!(g->rows[gy] & (1u << (4 - gx))))
                    continue;

                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        put_pixel(f, cur + gx * scale + sx, y + gy * scale + sy, c);
                    }
                }
            }
        }

        cur += 6 * scale;
    }
}

static void centered(const Frame *f, int center, int y, const char *s, int scale, Color c)
{
    int width = (int)strlen(s) * 6 * scale;
    text(f, center - width / 2, y, s, scale, c);
}

static Target input_target(u32 held)
{
    if ((held & KEY_L) && !(held & KEY_R))
        return TARGET_TOP;
    if ((held & KEY_R) && !(held & KEY_L))
        return TARGET_BOTTOM;

    return TARGET_BOTH;
}

static void apply_color(Color c, Target target)
{
    if (target == TARGET_BOTH || target == TARGET_TOP)
        top_color = c;

    if (target == TARGET_BOTH || target == TARGET_BOTTOM)
        bottom_color = c;
}

static Color darken(Color c)
{
    c.r = (u8)((u16)c.r * 3 / 4);
    c.g = (u8)((u16)c.g * 3 / 4);
    c.b = (u8)((u16)c.b * 3 / 4);
    return c;
}

static int slider_value_from_x(int x)
{
    const int left = 64;
    const int right = 278;

    if (x <= left)
        return 0;
    if (x >= right)
        return 255;

    return ((x - left) * 255) / (right - left);
}

static int slider_x_from_value(int value)
{
    const int left = 64;
    const int right = 278;

    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;

    return left + (value * (right - left)) / 255;
}

static void draw_slider(const Frame *f, int y, char label, int value, Color fill, bool pressed)
{
    const Color track = {58, 63, 74};
    const Color white = {245, 247, 250};
    const Color knob = {235, 238, 242};
    const Color knob_pressed = {190, 194, 200};

    text(f, 26, y - 5, (char[]){label, '\0'}, 2, white);

    rounded_rect(f, 64, y, 214, 12, 6, track);
    rounded_rect(f, 64, y, slider_x_from_value(value) - 64, 12, 6, fill);

    int kx = slider_x_from_value(value);
    circle(f, kx, y + 6, pressed ? 8 : 7, pressed ? knob_pressed : knob);
}

static void draw_menu(const Frame *f)
{
    const Color panel = {35, 39, 48};
    const Color inactive = {53, 58, 70};
    const Color selected = {58, 102, 190};
    const Color white = {245, 247, 250};
    const Color close = {125, 48, 55};
    const Color apply = {48, 119, 72};

    /* Keep the area outside the menu the current bottom-screen color. */
    clear_frame(f, bottom_color);

    rounded_rect(f, 8, 8, 304, 224, 12, panel);

    Color common_bg = (tab == 0) ? selected : inactive;
    Color custom_bg = (tab == 1) ? selected : inactive;

    if (pressed_button == BTN_TAB_COMMON)
        common_bg = darken(common_bg);
    if (pressed_button == BTN_TAB_CUSTOM)
        custom_bg = darken(custom_bg);

    /* Tabs have a small gap between them. */
    rounded_rect(f, 18, 18, 132, 30, 8, common_bg);
    rounded_rect(f, 170, 18, 132, 30, 8, custom_bg);

    centered(f, 84, 27, "COMMON", 2, white);
    centered(f, 236, 27, "CUSTOM", 2, white);

    if (tab == 0) {
        for (int i = 0; i < COMMON_COUNT; i++) {
            int col = i % 4;
            int row = i / 4;
            Color c = common_colors[i];

            if (pressed_button == BTN_COLOR_BASE + i)
                c = darken(c);

            rounded_rect(f, 18 + col * 72, 58 + row * 41, 64, 34, 7, c);
        }
    } else {
        draw_slider(f, 62, 'R', custom_color.r, (Color){220, 65, 65}, pressed_button == BTN_SLIDER_R);
        draw_slider(f, 99, 'G', custom_color.g, (Color){65, 190, 95}, pressed_button == BTN_SLIDER_G);
        draw_slider(f, 136, 'B', custom_color.b, (Color){70, 120, 220}, pressed_button == BTN_SLIDER_B);

        Color apply_bg = apply;
        if (pressed_button == BTN_APPLY)
            apply_bg = darken(apply_bg);

        rounded_rect(f, 18, 165, 284, 25, 8, apply_bg);
        centered(f, 160, 173, "APPLY COLOR", 2, white);
    }

    Color close_bg = close;
    if (pressed_button == BTN_CLOSE)
        close_bg = darken(close_bg);

    rounded_rect(f, 18, 198, 284, 28, 8, close_bg);
    centered(f, 160, 207, "CLOSE MENU", 2, white);
}

static void render(void)
{
    Frame top = frame_get(GFX_TOP);
    Frame bottom = frame_get(GFX_BOTTOM);

    clear_frame(&top, top_color);
    clear_frame(&bottom, bottom_color);

    if (menu_open)
        draw_menu(&bottom);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

static bool held_one_second(u32 held, bool *was_held, u64 *start)
{
    if (held) {
        if (!*was_held) {
            *was_held = true;
            *start = svcGetSystemTick();
        } else if (svcGetSystemTick() - *start >= HOLD_TICKS) {
            return true;
        }
    } else {
        *was_held = false;
    }

    return false;
}

static int hit_test(int x, int y)
{
    if (y >= 18 && y < 48) {
        if (x >= 18 && x < 150)
            return BTN_TAB_COMMON;
        if (x >= 170 && x < 302)
            return BTN_TAB_CUSTOM;
        return NO_BUTTON;
    }

    if (tab == 0 && x >= 18 && x < 306 && y >= 58 && y < 181) {
        int col = (x - 18) / 72;
        int row = (y - 58) / 41;
        int local_x = (x - 18) % 72;
        int local_y = (y - 58) % 41;

        if (col >= 0 && col < 4 && row >= 0 && row < 3 &&
            local_x < 64 && local_y < 34) {
            int index = row * 4 + col;
            if (index < COMMON_COUNT)
                return BTN_COLOR_BASE + index;
        }
    }

    if (tab == 1) {
        if (x >= 52 && x <= 290 && y >= 55 && y < 81)
            return BTN_SLIDER_R;
        if (x >= 52 && x <= 290 && y >= 92 && y < 118)
            return BTN_SLIDER_G;
        if (x >= 52 && x <= 290 && y >= 129 && y < 155)
            return BTN_SLIDER_B;

        if (x >= 18 && x < 302 && y >= 165 && y < 190)
            return BTN_APPLY;
    }

    if (x >= 18 && x < 302 && y >= 198 && y < 226)
        return BTN_CLOSE;

    return NO_BUTTON;
}

static void update_slider_from_touch(int button, int x)
{
    int value = slider_value_from_x(x);

    if (button == BTN_SLIDER_R)
        custom_color.r = (u8)value;
    else if (button == BTN_SLIDER_G)
        custom_color.g = (u8)value;
    else if (button == BTN_SLIDER_B)
        custom_color.b = (u8)value;
}

static void activate_button(int button, Target target)
{
    if (button == BTN_TAB_COMMON) {
        tab = 0;
    } else if (button == BTN_TAB_CUSTOM) {
        tab = 1;
    } else if (button == BTN_CLOSE) {
        menu_open = false;
    } else if (button >= BTN_COLOR_BASE && button < BTN_COLOR_BASE + COMMON_COUNT) {
        apply_color(common_colors[button - BTN_COLOR_BASE], target);
    } else if (button == BTN_APPLY) {
        apply_color(custom_color, target);
    }
}

static void update_touch(u32 down, u32 held)
{
    bool touching = (held & KEY_TOUCH) != 0;
    touchPosition t;

    /* Touch down records the button and the L/R destination at that moment. */
    if (down & KEY_TOUCH) {
        hidTouchRead(&t);
        pressed_button = hit_test((int)t.px, (int)t.py);
        pressed_target = input_target(held);

        if (pressed_button == BTN_SLIDER_R ||
            pressed_button == BTN_SLIDER_G ||
            pressed_button == BTN_SLIDER_B) {
            update_slider_from_touch(pressed_button, (int)t.px);
        }

        return;
    }

    if (pressed_button == NO_BUTTON)
        return;

    if (touching) {
        hidTouchRead(&t);

        /* Sliders intentionally follow the stylus while it is held. */
        if (pressed_button == BTN_SLIDER_R ||
            pressed_button == BTN_SLIDER_G ||
            pressed_button == BTN_SLIDER_B) {
            update_slider_from_touch(pressed_button, (int)t.px);
            return;
        }

        /* Normal buttons cancel if the stylus leaves their button. */
        if (hit_test((int)t.px, (int)t.py) != pressed_button)
            pressed_button = NO_BUTTON;

        return;
    }

    /*
     * Release activates the button. We do not call hidTouchRead here because
     * the touch position after KEY_TOUCH goes up is not guaranteed to be the
     * last stylus position.
     */
    if (pressed_button == BTN_SLIDER_R ||
        pressed_button == BTN_SLIDER_G ||
        pressed_button == BTN_SLIDER_B) {
        pressed_button = NO_BUTTON;
        return;
    }

    int button = pressed_button;
    Target target = pressed_target;
    pressed_button = NO_BUTTON;
    activate_button(button, target);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    hidScanInput();

    while (aptMainLoop()) {
        hidScanInput();

        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();

        /* Hold SELECT for one second anywhere in the application to exit. */
        if (held_one_second(held & KEY_SELECT, &select_held, &select_tick))
            break;

        /* Hold START for one second to open the menu. */
        if (held & KEY_START) {
            if (!start_held) {
                start_held = true;
                start_tick = svcGetSystemTick();
            } else if (!menu_open && svcGetSystemTick() - start_tick >= HOLD_TICKS) {
                menu_open = true;
                tab = 0;
                pressed_button = NO_BUTTON;
            }
        } else {
            start_held = false;
        }

        if (menu_open)
            update_touch(down, held);
        else
            pressed_button = NO_BUTTON;

        render();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}

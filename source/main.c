#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define SCREEN_H 240
#define TOP_W 400
#define BOT_W 320
#define ONE_SECOND_MS 1000ULL

typedef struct { u8 r, g, b; const char *name; } Color;
typedef enum { TARGET_BOTH, TARGET_TOP, TARGET_BOTTOM } ColorTarget;

static const Color colors[] = {
    {255,0,0,"RED"}, {0,255,0,"GREEN"}, {0,0,255,"BLUE"},
    {255,255,0,"YELLOW"}, {255,0,255,"MAGENTA"}, {0,255,255,"CYAN"},
    {255,255,255,"WHITE"}, {0,0,0,"BLACK"}, {128,128,128,"GRAY"},
    {255,128,0,"ORANGE"}, {128,0,255,"PURPLE"}, {0,128,255,"SKY"}
};
#define COLOR_COUNT ((int)(sizeof(colors) / sizeof(colors[0])))

static Color top_color = {0,255,0,"GREEN"};
static Color bottom_color = {0,255,0,"GREEN"};
static Color custom_color = {0,255,0,"CUSTOM"};
static bool menu_open = false;
static bool advanced = false;
static int active_tab = 0;
static bool start_held = false;
static u64 start_time = 0;
static bool select_held = false;
static u64 select_time = 0;

/* Small bitmap font. It is drawn directly into the framebuffer, so there is
   no console/terminal rendering anywhere in the application. */
typedef struct { char c; u8 rows[7]; } Glyph;
#define G(c,a,b,d,e,f,g,h) {c,{a,b,d,e,f,g,h}}
static const Glyph font[] = {
    G(' ',0,0,0,0,0,0,0), G('#',10,31,10,10,31,10,0), G('-',0,0,0,31,0,0,0),
    G('.',0,0,0,0,0,6,6), G(':',0,6,6,0,6,6,0),
    G('0',14,17,19,21,25,17,14), G('1',4,12,4,4,4,4,14),
    G('2',14,17,1,2,4,8,31), G('3',30,1,1,14,1,1,30),
    G('4',2,6,10,18,31,2,2), G('5',31,16,16,30,1,1,30),
    G('6',6,8,16,30,17,17,14), G('7',31,1,2,4,8,8,8),
    G('8',14,17,17,14,17,17,14), G('9',14,17,17,15,1,2,12),
    G('A',14,17,17,31,17,17,17), G('B',30,17,17,30,17,17,30),
    G('C',14,17,16,16,16,17,14), G('D',30,17,17,17,17,17,30),
    G('E',31,16,16,30,16,16,31), G('F',31,16,16,30,16,16,16),
    G('G',14,17,16,23,17,17,14), G('H',17,17,17,31,17,17,17),
    G('I',14,4,4,4,4,4,14), G('J',1,1,1,1,17,17,14),
    G('K',17,18,20,24,20,18,17), G('L',16,16,16,16,16,16,31),
    G('M',17,27,21,21,17,17,17), G('N',17,25,21,21,19,19,17),
    G('O',14,17,17,17,17,17,14), G('P',30,17,17,30,16,16,16),
    G('Q',14,17,17,17,21,18,13), G('R',30,17,17,30,20,18,17),
    G('S',15,16,16,14,1,1,30), G('T',31,4,4,4,4,4,4),
    G('U',17,17,17,17,17,17,14), G('V',17,17,17,17,17,10,4),
    G('W',17,17,17,21,21,21,10), G('X',17,17,10,4,10,17,17),
    G('Y',17,17,10,4,4,4,4), G('Z',31,1,2,4,8,16,31)
};
#undef G

static const Glyph *glyph(char c)
{
    for (size_t i = 0; i < sizeof(font)/sizeof(font[0]); ++i)
        if (font[i].c == c) return &font[i];
    return &font[0];
}

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(((u16)(r >> 3) << 11) |
                 ((u16)(g >> 2) << 5) |
                 (u16)(b >> 3));
}

/* IMPORTANT: the 3DS framebuffer is rotated in memory. A pixel at logical
   (x,y) is stored at x * 240 + (239-y). The previous implementation used
   the TOP screen's 400-pixel width for BOTH screens, which could write past
   the bottom framebuffer. Every primitive below uses the actual framebuffer
   width returned by gfxGetFramebuffer(). */
static u16 *framebuffer(gfxScreen_t screen, u16 *width, u16 *height)
{
    return (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, width, height);
}

static void pixel(gfxScreen_t screen, int x, int y, u16 color)
{
    u16 width = 0, height = 0;
    u16 *fb = framebuffer(screen, &width, &height);
    if (!fb) return;
    if (x < 0 || y < 0 || x >= (int)width || y >= (int)height) return;
    fb[(u32)x * 240u + (239u - (u32)y)] = color;
}

static void fill(gfxScreen_t screen, u16 color)
{
    u16 width = 0, height = 0;
    u16 *fb = framebuffer(screen, &width, &height);
    if (!fb) return;

    for (u16 x = 0; x < width; ++x) {
        u32 base = (u32)x * 240u;
        for (u16 y = 0; y < height; ++y)
            fb[base + (239u - y)] = color;
    }
}

static void rect(gfxScreen_t screen, int x, int y, int w, int h, u16 color)
{
    u16 width = 0, height = 0;
    u16 *fb = framebuffer(screen, &width, &height);
    if (!fb || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)width) w = (int)width - x;
    if (y + h > (int)height) h = (int)height - y;
    if (w <= 0 || h <= 0) return;

    for (int px = x; px < x + w; ++px) {
        u32 base = (u32)px * 240u;
        for (int py = y; py < y + h; ++py)
            fb[base + (239u - (u32)py)] = color;
    }
}

static void circle(gfxScreen_t screen, int cx, int cy, int r, u16 color)
{
    if (r <= 0) return;
    for (int y = -r; y <= r; ++y) {
        int dx = r;
        while (dx * dx + y * y > r * r) --dx;
        for (int x = -dx; x <= dx; ++x)
            pixel(screen, cx + x, cy + y, color);
    }
}

static void rounded_rect(gfxScreen_t screen, int x, int y, int w, int h,
                         int r, u16 color)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    rect(screen, x + r, y, w - 2*r, h, color);
    rect(screen, x, y + r, w, h - 2*r, color);
    circle(screen, x + r, y + r, r, color);
    circle(screen, x + w - r - 1, y + r, r, color);
    circle(screen, x + r, y + h - r - 1, r, color);
    circle(screen, x + w - r - 1, y + h - r - 1, r, color);
}

static void text(gfxScreen_t screen, int x, int y, const char *s,
                 int scale, u16 color)
{
    if (!s || scale < 1) return;
    int cursor = x;
    while (*s) {
        const Glyph *g = glyph(*s++);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (!(g->rows[gy] & (1 << (4-gx)))) continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx)
                        pixel(screen, cursor + gx*scale + sx,
                              y + gy*scale + sy, color);
            }
        }
        cursor += 6 * scale;
    }
}

static void centered(gfxScreen_t screen, int y, int center,
                     const char *s, int scale, u16 color)
{
    int width = (int)strlen(s) * 6 * scale;
    text(screen, center - width/2, y, s, scale, color);
}

static void apply_color(Color c, ColorTarget target)
{
    if (target == TARGET_BOTH || target == TARGET_TOP)
        top_color = c;
    if (target == TARGET_BOTH || target == TARGET_BOTTOM)
        bottom_color = c;
}

static ColorTarget current_target(u32 held)
{
    if (!advanced) return TARGET_BOTH;
    if ((held & KEY_L) && !(held & KEY_R)) return TARGET_BOTTOM;
    if ((held & KEY_R) && !(held & KEY_L)) return TARGET_TOP;
    return TARGET_BOTH;
}

static bool hex_digit(char c, u8 *v)
{
    if (c >= '0' && c <= '9') { *v = (u8)(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { *v = (u8)(c - 'A' + 10); return true; }
    if (c >= 'a' && c <= 'f') { *v = (u8)(c - 'a' + 10); return true; }
    return false;
}

static bool parse_hex(const char *s, Color *out)
{
    size_t len = strlen(s);
    const char *p = s;
    if (len == 7 && s[0] == '#') p++;
    else if (len != 6) return false;

    u8 d[6];
    for (int i = 0; i < 6; ++i)
        if (!hex_digit(p[i], &d[i])) return false;

    out->r = (u8)((d[0] << 4) | d[1]);
    out->g = (u8)((d[2] << 4) | d[3]);
    out->b = (u8)((d[4] << 4) | d[5]);
    out->name = "CUSTOM";
    return true;
}

static void custom_keyboard(ColorTarget target)
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
        apply_color(custom_color, target);
    }
}

#define PANEL_X 8
#define PANEL_Y 8
#define PANEL_W 304
#define PANEL_H 224
#define TAB_Y 18
#define TAB_H 30
#define TAB_W 142
#define TAB1_X 18
#define TAB2_X 160
#define GRID_X 18
#define GRID_Y 58
#define CELL_W 64
#define CELL_H 36
#define GAP_X 8
#define GAP_Y 8
#define ADV_X 18
#define ADV_Y 166
#define ADV_W 284
#define ADV_H 22
#define CLOSE_X 18
#define CLOSE_Y 194
#define CLOSE_W 284
#define CLOSE_H 28

static void draw_menu(void)
{
    const u16 bg = rgb565(18,20,25);
    const u16 panel = rgb565(38,41,49);
    const u16 tab = rgb565(58,62,73);
    const u16 selected = rgb565(65,105,195);
    const u16 white = rgb565(245,247,250);
    const u16 muted = rgb565(175,181,192);
    const u16 close = rgb565(125,48,55);
    const u16 on = rgb565(55,135,82);

    fill(GFX_BOTTOM, bg);
    rounded_rect(GFX_BOTTOM, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 12, panel);
    rounded_rect(GFX_BOTTOM, TAB1_X, TAB_Y, TAB_W, TAB_H, 9,
                 active_tab == 0 ? selected : tab);
    rounded_rect(GFX_BOTTOM, TAB2_X, TAB_Y, TAB_W, TAB_H, 9,
                 active_tab == 1 ? selected : tab);
    centered(GFX_BOTTOM, TAB_Y + 9, TAB1_X + TAB_W/2, "COMMON", 2, white);
    centered(GFX_BOTTOM, TAB_Y + 9, TAB2_X + TAB_W/2, "CUSTOM", 2, white);

    if (active_tab == 0) {
        for (int i = 0; i < COLOR_COUNT; ++i) {
            int col = i % 4;
            int row = i / 4;
            int x = GRID_X + col * (CELL_W + GAP_X);
            int y = GRID_Y + row * (CELL_H + GAP_Y);
            rounded_rect(GFX_BOTTOM, x, y, CELL_W, CELL_H, 7,
                         rgb565(colors[i].r, colors[i].g, colors[i].b));
        }
        centered(GFX_BOTTOM, ADV_Y + 7, BOT_W/2,
                 advanced ? "L BOT  R TOP" : "ADVANCED OFF", 1, muted);
    } else {
        rounded_rect(GFX_BOTTOM, 36, 70, 248, 58, 10, tab);
        centered(GFX_BOTTOM, 83, BOT_W/2, "ENTER HEX", 2, white);
        centered(GFX_BOTTOM, 106, BOT_W/2, "TAP TO TYPE", 1, muted);
    }

    rounded_rect(GFX_BOTTOM, ADV_X, ADV_Y, ADV_W, ADV_H, 8,
                 advanced ? on : tab);
    centered(GFX_BOTTOM, ADV_Y + 7, BOT_W/2,
             advanced ? "ADVANCED ON" : "ADVANCED OFF", 1, white);

    rounded_rect(GFX_BOTTOM, CLOSE_X, CLOSE_Y, CLOSE_W, CLOSE_H, 9, close);
    centered(GFX_BOTTOM, CLOSE_Y + 8, BOT_W/2, "CLOSE  B", 2, white);
}

static void draw_frame(void)
{
    fill(GFX_TOP, rgb565(top_color.r, top_color.g, top_color.b));
    fill(GFX_BOTTOM, rgb565(bottom_color.r, bottom_color.g, bottom_color.b));
    if (menu_open) draw_menu();
}

static void handle_touch(u32 held)
{
    touchPosition t;
    hidTouchRead(&t);
    int x = t.px;
    int y = t.py;

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

    if (active_tab == 0 && x >= GRID_X && x < 300 &&
        y >= GRID_Y && y < GRID_Y + 3*(CELL_H + GAP_Y)) {
        int col = (x - GRID_X) / (CELL_W + GAP_X);
        int row = (y - GRID_Y) / (CELL_H + GAP_Y);
        int lx = (x - GRID_X) % (CELL_W + GAP_X);
        int ly = (y - GRID_Y) % (CELL_H + GAP_Y);
        if (col >= 0 && col < 4 && row >= 0 && row < 3 &&
            lx < CELL_W && ly < CELL_H) {
            int i = row * 4 + col;
            if (i < COLOR_COUNT)
                apply_color(colors[i], current_target(held));
        }
        return;
    }

    if (active_tab == 1 && x >= 36 && x < 284 && y >= 70 && y < 128) {
        custom_keyboard(current_target(held));
        return;
    }

    if (x >= ADV_X && x < ADV_X + ADV_W &&
        y >= ADV_Y && y < ADV_Y + ADV_H) {
        advanced = !advanced;
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
        u64 now = osGetTime();

        /* SELECT held for one second exits from anywhere in the app. */
        if (held & KEY_SELECT) {
            if (!select_held) {
                select_held = true;
                select_time = now;
            } else if (now - select_time >= ONE_SECOND_MS) {
                break;
            }
        } else {
            select_held = false;
        }

        /* START held for one second opens the menu. */
        if (held & KEY_START) {
            if (!start_held) {
                start_held = true;
                start_time = now;
            } else if (!menu_open && now - start_time >= ONE_SECOND_MS) {
                menu_open = true;
                active_tab = 0;
            }
        } else {
            start_held = false;
        }

        if (menu_open) {
            if (down & KEY_B) {
                menu_open = false;
            } else if (down & KEY_TOUCH) {
                handle_touch(held);
            }
        }

        draw_frame();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    hidExit();
    gfxExit();
    return 0;
}

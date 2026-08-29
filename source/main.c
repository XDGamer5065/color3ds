#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct { u8 r, g, b; } Color;
typedef enum { TARGET_BOTH, TARGET_TOP, TARGET_BOTTOM } Target;

typedef struct {
    u8 *data;
    int width;
    int height;
} Frame;

#define FB_STRIDE 240
#define FB_BPP 3
#define TOP_WIDTH 400
#define BOTTOM_WIDTH 320
#define SCREEN_HEIGHT 240
#define HOLD_TICKS 268000000ULL

static const Color common_colors[] = {
    {255,0,0}, {0,255,0}, {0,0,255}, {255,255,0},
    {255,0,255}, {0,255,255}, {255,255,255}, {0,0,0},
    {128,128,128}, {255,128,0}, {128,0,255}, {0,128,255}
};
#define COMMON_COUNT ((int)(sizeof(common_colors) / sizeof(common_colors[0])))

static Color top_color = {0,255,0};
static Color bottom_color = {0,255,0};
static Color custom_color = {0,255,0};

static bool menu_open = false;
static int tab = 0;
static bool start_held = false;
static bool select_held = false;
static u64 start_tick = 0;
static u64 select_tick = 0;

/*
 * 3DS framebuffers use 24-bit BGR pixels and are stored rotated.
 * The physical framebuffer dimensions reported by gfxGetFramebuffer()
 * are 240x400 for the top screen and 240x320 for the bottom screen.
 * For drawing, however, we use the logical dimensions of 400x240 and
 * 320x240. Logical (x,y) maps to x * 240 + (239-y).
 */
static Frame frame_get(gfxScreen_t screen)
{
    Frame f;
    u16 physical_w = 0, physical_h = 0;

    f.data = gfxGetFramebuffer(screen, GFX_LEFT, &physical_w, &physical_h);

    (void)physical_w;
    (void)physical_h;

    if (screen == GFX_TOP) {
        f.width = TOP_WIDTH;
        f.height = SCREEN_HEIGHT;
    } else {
        f.width = BOTTOM_WIDTH;
        f.height = SCREEN_HEIGHT;
    }

    return f;
}

static inline bool frame_ok(const Frame *f)
{
    return f && f->data && f->width > 0 && f->height > 0;
}

static inline u8 *frame_pixel(const Frame *f, int x, int y)
{
    return f->data + ((x * FB_STRIDE) + (SCREEN_HEIGHT - 1 - y)) * FB_BPP;
}

static inline void put_pixel(const Frame *f, int x, int y, Color c)
{
    if (!frame_ok(f) || x < 0 || y < 0 || x >= f->width || y >= f->height)
        return;

    u8 *p = frame_pixel(f, x, y);
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

static void clear_frame(const Frame *f, Color c)
{
    if (!frame_ok(f)) return;

    for (int x = 0; x < f->width; ++x) {
        u8 *p = f->data + x * FB_STRIDE * FB_BPP;
        for (int y = 0; y < f->height; ++y) {
            u8 *q = p + (SCREEN_HEIGHT - 1 - y) * FB_BPP;
            q[0] = c.b;
            q[1] = c.g;
            q[2] = c.r;
        }
    }
}

static void fill_rect(const Frame *f, int x, int y, int w, int h, Color c)
{
    if (!frame_ok(f) || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > f->width) w = f->width - x;
    if (y + h > f->height) h = f->height - y;
    if (w <= 0 || h <= 0) return;

    for (int px = x; px < x + w; ++px) {
        u8 *base = f->data + px * FB_STRIDE * FB_BPP;
        for (int py = y; py < y + h; ++py) {
            u8 *p = base + (SCREEN_HEIGHT - 1 - py) * FB_BPP;
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
}

static void circle(const Frame *f, int cx, int cy, int r, Color c)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x*x + y*y <= r*r)
                put_pixel(f, cx+x, cy+y, c);
}

static void rounded_rect(const Frame *f, int x, int y, int w, int h, int r, Color c)
{
    if (r <= 0) { fill_rect(f,x,y,w,h,c); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    fill_rect(f, x+r, y, w-2*r, h, c);
    fill_rect(f, x, y+r, w, h-2*r, c);
    circle(f, x+r, y+r, r, c);
    circle(f, x+w-r-1, y+r, r, c);
    circle(f, x+r, y+h-r-1, r, c);
    circle(f, x+w-r-1, y+h-r-1, r, c);
}

typedef struct { char c; u8 rows[7]; } Glyph;
#define G(c,a,b,d,e,f,g,h) {c,{a,b,d,e,f,g,h}}
static const Glyph font[] = {
    G(' ',0,0,0,0,0,0,0), G('#',10,31,10,10,31,10,0),
    G('.',0,0,0,0,0,6,6), G(':',0,6,6,0,6,6,0), G('-',0,0,0,31,0,0,0),
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
    for (size_t i=0; i<sizeof(font)/sizeof(font[0]); ++i)
        if (font[i].c == c) return &font[i];
    return &font[0];
}

static void text(const Frame *f, int x, int y, const char *s, int scale, Color c)
{
    int cursor = x;
    while (s && *s) {
        const Glyph *g = glyph(*s++);
        for (int gy=0; gy<7; ++gy) for (int gx=0; gx<5; ++gx) {
            if (!(g->rows[gy] & (1u << (4-gx)))) continue;
            for (int sy=0; sy<scale; ++sy)
                for (int sx=0; sx<scale; ++sx)
                    put_pixel(f,cursor+gx*scale+sx,y+gy*scale+sy,c);
        }
        cursor += 6*scale;
    }
}

static void centered(const Frame *f, int center, int y, const char *s, int scale, Color c)
{
    text(f, center - ((int)strlen(s)*6*scale)/2, y, s, scale, c);
}

static Target input_target(u32 held)
{
    /* No advanced mode: L = top, R = bottom, neither = both. */
    if ((held & KEY_L) && !(held & KEY_R)) return TARGET_TOP;
    if ((held & KEY_R) && !(held & KEY_L)) return TARGET_BOTTOM;
    return TARGET_BOTH;
}

static void apply_color(Color c, Target target)
{
    if (target == TARGET_BOTH || target == TARGET_TOP) top_color = c;
    if (target == TARGET_BOTH || target == TARGET_BOTTOM) bottom_color = c;
}

static bool hex_digit(char c, u8 *v)
{
    if (c >= '0' && c <= '9') { *v = c-'0'; return true; }
    if (c >= 'A' && c <= 'F') { *v = c-'A'+10; return true; }
    if (c >= 'a' && c <= 'f') { *v = c-'a'+10; return true; }
    return false;
}

static bool parse_hex(const char *s, Color *out)
{
    size_t len = strlen(s);
    const char *p = s;
    u8 d[6];
    if (len == 7 && s[0] == '#') p++;
    else if (len != 6) return false;
    for (int i=0; i<6; ++i) if (!hex_digit(p[i], &d[i])) return false;
    out->r=(u8)((d[0]<<4)|d[1]);
    out->g=(u8)((d[2]<<4)|d[3]);
    out->b=(u8)((d[4]<<4)|d[5]);
    return true;
}

static void keyboard(Target target)
{
    SwkbdState state;
    char input[8] = {0};
    swkbdInit(&state, SWKBD_TYPE_WESTERN, 2, 7);
    swkbdSetHintText(&state, "#RRGGBB");
    swkbdSetValidation(&state, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (swkbdInputText(&state,input,sizeof(input)) != SWKBD_BUTTON_CONFIRM) return;
    Color c;
    if (parse_hex(input,&c)) { custom_color=c; apply_color(c,target); }
}

static void draw_menu(const Frame *f)
{
    const Color panel={35,39,48}, inactive={53,58,70};
    const Color selected={58,102,190}, white={245,247,250}, close={125,48,55};

    /* Keep the area outside the menu the same color as the bottom screen. */
    clear_frame(f,bottom_color);
    rounded_rect(f,8,8,304,224,12,panel);
    rounded_rect(f,18,18,142,30,8,tab==0?selected:inactive);
    rounded_rect(f,160,18,142,30,8,tab==1?selected:inactive);
    centered(f,89,27,"COMMON",2,white);
    centered(f,231,27,"CUSTOM",2,white);

    if (tab == 0) {
        for (int i=0;i<COMMON_COUNT;++i) {
            int col=i%4, row=i/4;
            rounded_rect(f,18+col*72,58+row*41,64,34,7,common_colors[i]);
        }
    } else {
        const Color button={53,58,70};
        rounded_rect(f,34,70,252,60,10,button);
        centered(f,160,85,"ENTER HEX",2,white);
        centered(f,160,108,"TAP TO TYPE",1,white);
    }

    rounded_rect(f,18,194,284,28,8,close);
    centered(f,160,203,"CLOSE MENU",2,white);
}

static void render(void)
{
    Frame top=frame_get(GFX_TOP);
    Frame bottom=frame_get(GFX_BOTTOM);

    clear_frame(&top,top_color);
    clear_frame(&bottom,bottom_color);
    if (menu_open) draw_menu(&bottom);

    gfxFlushBuffers();
    gfxSwapBuffers();
}

static bool held_one_second(u32 held, bool *was_held, u64 *start)
{
    if (held) {
        if (!*was_held) {
            *was_held=true;
            *start=svcGetSystemTick();
        } else if (svcGetSystemTick()-*start >= HOLD_TICKS) {
            return true;
        }
    } else {
        *was_held=false;
    }
    return false;
}

static void touch_menu(u32 keys_down, u32 keys_held)
{
    if (!(keys_down & KEY_TOUCH)) return;

    touchPosition t;
    hidTouchRead(&t);
    int x=(int)t.px, y=(int)t.py;
    Target target=input_target(keys_held);

    if (y>=18 && y<48) {
        if (x>=18 && x<160) tab=0;
        else if (x>=160 && x<302) tab=1;
        return;
    }

    if (tab==0 && x>=18 && x<306 && y>=58 && y<181) {
        int col=(x-18)/72, row=(y-58)/41;
        int local_x=(x-18)%72, local_y=(y-58)%41;
        if (col>=0 && col<4 && row>=0 && row<3 && local_x<64 && local_y<34) {
            int index=row*4+col;
            if (index<COMMON_COUNT) apply_color(common_colors[index],target);
        }
        return;
    }

    if (tab==1 && x>=34 && x<286 && y>=70 && y<130) {
        keyboard(target);
        return;
    }

    if (x>=18 && x<302 && y>=194 && y<222) menu_open=false;
}

int main(void)
{
    gfxInitDefault();
    gfxSet3D(false);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down=hidKeysDown();
        u32 held=hidKeysHeld();

        /* SELECT held for one second exits from anywhere. */
        if (held_one_second(held & KEY_SELECT,&select_held,&select_tick)) break;

        /* START held for one second opens the menu. */
        if (!menu_open && held_one_second(held & KEY_START,&start_held,&start_tick)) {
            menu_open=true;
            tab=0;
            start_held=false;
        }

        if (menu_open) {
            if (down & KEY_B) menu_open=false;
            else touch_menu(down,held);
        }

        render();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}

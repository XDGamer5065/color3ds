#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define SCREEN_HEIGHT 240
#define TOP_WIDTH 400
#define BOTTOM_WIDTH 320
#define START_HOLD_MS 1000
#define SELECT_HOLD_MS 1000

typedef struct { u8 r, g, b; const char *name; } Color;
typedef enum { TARGET_BOTH, TARGET_TOP, TARGET_BOTTOM } ColorTarget;
typedef struct { u16 *pixels; int width; int height; } Frame;

static const Color common_colors[] = {
    {255,0,0,"RED"}, {0,255,0,"GREEN"}, {0,0,255,"BLUE"},
    {255,255,0,"YELLOW"}, {255,0,255,"MAGENTA"}, {0,255,255,"CYAN"},
    {255,255,255,"WHITE"}, {0,0,0,"BLACK"}, {128,128,128,"GRAY"},
    {255,128,0,"ORANGE"}, {128,0,255,"PURPLE"}, {0,128,255,"SKY"}
};
#define COMMON_COUNT ((int)(sizeof(common_colors) / sizeof(common_colors[0])))

static Color top_color = {0,255,0,"GREEN"};
static Color bottom_color = {0,255,0,"GREEN"};
static Color custom_color = {0,255,0,"CUSTOM"};
static bool menu_open = false;
static bool advanced_mode = false;
static int active_tab = 0;
static bool start_held = false;
static u64 start_time = 0;
static bool select_held = false;
static u64 select_time = 0;

typedef struct { char c; u8 rows[7]; } Glyph;
#define G(ch,a,b,c,d,e,f,g) {ch,{a,b,c,d,e,f,g}}
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
    for (size_t i=0; i<sizeof(font)/sizeof(font[0]); ++i)
        if (font[i].c == c) return &font[i];
    return &font[0];
}

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(((u16)(r >> 3) << 11) | ((u16)(g >> 2) << 5) | (u16)(b >> 3));
}

/*
 * Native 3DS framebuffer layout is rotated in memory. The logical pixel
 * (x,y) is stored at x * 240 + (239-y). The stride is 240 pixels on both
 * screens; only the logical width differs (400 top, 320 bottom).
 *
 * Each screen's framebuffer is acquired once per frame. Nothing here uses a
 * terminal/console and no top-screen dimensions are ever used for the bottom
 * screen.
 */
static Frame get_frame(gfxScreen_t screen)
{
    Frame f;
    u16 width=0, height=0;
    f.pixels=(u16 *)gfxGetFramebuffer(screen,GFX_LEFT,&width,&height);
    f.width=(int)width;
    f.height=(int)height;
    return f;
}

static inline bool valid_frame(const Frame *f)
{
    return f && f->pixels && f->width>0 && f->height>0;
}

static inline void pixel(const Frame *f,int x,int y,u16 color)
{
    if (!valid_frame(f) || x<0 || y<0 || x>=f->width || y>=f->height) return;
    f->pixels[x*SCREEN_HEIGHT+(SCREEN_HEIGHT-1-y)]=color;
}

static void clear_frame(const Frame *f,u16 color)
{
    if (!valid_frame(f)) return;
    for (int x=0;x<f->width;++x) {
        u32 base=(u32)x*SCREEN_HEIGHT;
        for (int y=0;y<f->height;++y)
            f->pixels[base+(SCREEN_HEIGHT-1-y)]=color;
    }
}

static void rect(const Frame *f,int x,int y,int w,int h,u16 color)
{
    if (!valid_frame(f)||w<=0||h<=0) return;
    if (x<0){w+=x;x=0;}
    if (y<0){h+=y;y=0;}
    if (x+w>f->width) w=f->width-x;
    if (y+h>f->height) h=f->height-y;
    if (w<=0||h<=0) return;

    for (int px=x;px<x+w;++px) {
        u32 base=(u32)px*SCREEN_HEIGHT;
        for (int py=y;py<y+h;++py)
            f->pixels[base+(SCREEN_HEIGHT-1-py)]=color;
    }
}

static void circle(const Frame *f,int cx,int cy,int r,u16 color)
{
    if (!valid_frame(f)||r<=0) return;
    for (int y=-r;y<=r;++y) {
        int dx=r;
        while (dx>0 && dx*dx+y*y>r*r) --dx;
        for (int x=-dx;x<=dx;++x) pixel(f,cx+x,cy+y,color);
    }
}

static void rounded_rect(const Frame *f,int x,int y,int w,int h,int r,u16 color)
{
    if (w<=0||h<=0) return;
    if (r<0) r=0;
    if (r*2>w) r=w/2;
    if (r*2>h) r=h/2;
    if (r==0){rect(f,x,y,w,h,color);return;}

    rect(f,x+r,y,w-2*r,h,color);
    rect(f,x,y+r,w,h-2*r,color);
    circle(f,x+r,y+r,r,color);
    circle(f,x+w-r-1,y+r,r,color);
    circle(f,x+r,y+h-r-1,r,color);
    circle(f,x+w-r-1,y+h-r-1,r,color);
}

static void text(const Frame *f,int x,int y,const char *s,int scale,u16 color)
{
    if (!valid_frame(f)||!s||scale<1) return;
    int cursor=x;
    while (*s) {
        const Glyph *g=glyph(*s++);
        for (int gy=0;gy<7;++gy) for (int gx=0;gx<5;++gx) {
            if (!(g->rows[gy]&(1u<<(4-gx)))) continue;
            for (int sy=0;sy<scale;++sy) for (int sx=0;sx<scale;++sx)
                pixel(f,cursor+gx*scale+sx,y+gy*scale+sy,color);
        }
        cursor+=6*scale;
    }
}

static void centered(const Frame *f,int cx,int y,const char *s,int scale,u16 color)
{
    if (!s) return;
    text(f,cx-((int)strlen(s)*6*scale)/2,y,s,scale,color);
}

static void apply_color(Color c,ColorTarget target)
{
    if (target==TARGET_BOTH||target==TARGET_TOP) top_color=c;
    if (target==TARGET_BOTH||target==TARGET_BOTTOM) bottom_color=c;
}

static ColorTarget target_from_keys(u32 held)
{
    if (!advanced_mode) return TARGET_BOTH;
    if ((held&KEY_L)&&!(held&KEY_R)) return TARGET_BOTTOM;
    if ((held&KEY_R)&&!(held&KEY_L)) return TARGET_TOP;
    return TARGET_BOTH;
}

static bool hex_digit(char c,u8 *v)
{
    if(c>='0'&&c<='9'){*v=(u8)(c-'0');return true;}
    if(c>='A'&&c<='F'){*v=(u8)(c-'A'+10);return true;}
    if(c>='a'&&c<='f'){*v=(u8)(c-'a'+10);return true;}
    return false;
}

static bool parse_hex(const char *s,Color *out)
{
    size_t len=strlen(s); const char *p=s;
    if(len==7&&s[0]=='#') ++p;
    else if(len!=6) return false;
    u8 d[6];
    for(int i=0;i<6;++i) if(!hex_digit(p[i],&d[i])) return false;
    out->r=(u8)((d[0]<<4)|d[1]);
    out->g=(u8)((d[2]<<4)|d[3]);
    out->b=(u8)((d[4]<<4)|d[5]);
    out->name="CUSTOM";
    return true;
}

static void custom_keyboard(ColorTarget target)
{
    SwkbdState keyboard; char input[8]={0};
    swkbdInit(&keyboard,SWKBD_TYPE_WESTERN,2,7);
    swkbdSetHintText(&keyboard,"#RRGGBB");
    swkbdSetValidation(&keyboard,SWKBD_NOTEMPTY_NOTBLANK,0,0);
    if(swkbdInputText(&keyboard,input,sizeof(input))!=SWKBD_BUTTON_CONFIRM) return;
    Color parsed;
    if(parse_hex(input,&parsed)){custom_color=parsed;apply_color(custom_color,target);}
}

#define PANEL_X 8
#define PANEL_Y 8
#define PANEL_W 304
#define PANEL_H 224
#define TAB_Y 18
#define TAB_H 30
#define TAB_W 142
#define COMMON_X 18
#define CUSTOM_X 160
#define GRID_X 18
#define GRID_Y 58
#define CELL_W 64
#define CELL_H 34
#define GAP_X 8
#define GAP_Y 7
#define ADV_X 18
#define ADV_Y 166
#define ADV_W 284
#define ADV_H 22
#define CLOSE_X 18
#define CLOSE_Y 194
#define CLOSE_W 284
#define CLOSE_H 28

static void draw_menu(const Frame *f)
{
    const u16 bg=rgb565(16,18,23), panel=rgb565(37,40,48);
    const u16 tab=rgb565(57,61,72), selected=rgb565(60,102,190);
    const u16 white=rgb565(245,247,250), muted=rgb565(180,185,195);
    const u16 close=rgb565(125,48,55), enabled=rgb565(48,125,76);

    clear_frame(f,bg);
    rounded_rect(f,PANEL_X,PANEL_Y,PANEL_W,PANEL_H,12,panel);
    rounded_rect(f,COMMON_X,TAB_Y,TAB_W,TAB_H,8,active_tab==0?selected:tab);
    rounded_rect(f,CUSTOM_X,TAB_Y,TAB_W,TAB_H,8,active_tab==1?selected:tab);
    centered(f,COMMON_X+TAB_W/2,TAB_Y+9,"COMMON",2,white);
    centered(f,CUSTOM_X+TAB_W/2,TAB_Y+9,"CUSTOM",2,white);

    if(active_tab==0) {
        for(int i=0;i<COMMON_COUNT;++i) {
            int col=i%4,row=i/4;
            int x=GRID_X+col*(CELL_W+GAP_X);
            int y=GRID_Y+row*(CELL_H+GAP_Y);
            rounded_rect(f,x,y,CELL_W,CELL_H,7,
                rgb565(common_colors[i].r,common_colors[i].g,common_colors[i].b));
        }
    } else {
        rounded_rect(f,34,68,252,60,10,tab);
        centered(f,BOTTOM_WIDTH/2,84,"ENTER HEX",2,white);
        centered(f,BOTTOM_WIDTH/2,108,"TAP TO TYPE",1,muted);
    }

    rounded_rect(f,ADV_X,ADV_Y,ADV_W,ADV_H,8,advanced_mode?enabled:tab);
    centered(f,BOTTOM_WIDTH/2,ADV_Y+7,
             advanced_mode?"L BOTTOM   R TOP":"ADVANCED OFF",1,
             advanced_mode?white:muted);
    rounded_rect(f,CLOSE_X,CLOSE_Y,CLOSE_W,CLOSE_H,8,close);
    centered(f,BOTTOM_WIDTH/2,CLOSE_Y+9,"CLOSE MENU",2,white);
}

static void render(void)
{
    Frame top=get_frame(GFX_TOP);
    Frame bottom=get_frame(GFX_BOTTOM);

    clear_frame(&top,rgb565(top_color.r,top_color.g,top_color.b));
    clear_frame(&bottom,rgb565(bottom_color.r,bottom_color.g,bottom_color.b));

    if(menu_open) draw_menu(&bottom);

    gfxFlushBuffers();
}

static void handle_touch(u32 kDown)
{
    if(!(kDown&KEY_TOUCH)||!menu_open) return;
    touchPosition t; hidTouchRead(&t);
    int x=(int)t.px,y=(int)t.py;

    if(y>=TAB_Y&&y<TAB_Y+TAB_H) {
        if(x>=COMMON_X&&x<COMMON_X+TAB_W) active_tab=0;
        else if(x>=CUSTOM_X&&x<CUSTOM_X+TAB_W) {
            active_tab=1;
            custom_keyboard(target_from_keys(hidKeysHeld()));
        }
        return;
    }

    if(active_tab==0&&x>=GRID_X&&x<GRID_X+4*CELL_W+3*GAP_X&&
       y>=GRID_Y&&y<GRID_Y+3*CELL_H+2*GAP_Y) {
        int col=(x-GRID_X)/(CELL_W+GAP_X);
        int row=(y-GRID_Y)/(CELL_H+GAP_Y);
        int lx=(x-GRID_X)%(CELL_W+GAP_X);
        int ly=(y-GRID_Y)%(CELL_H+GAP_Y);
        if(col>=0&&col<4&&row>=0&&row<3&&lx<CELL_W&&ly<CELL_H) {
            int index=row*4+col;
            if(index>=0&&index<COMMON_COUNT)
                apply_color(common_colors[index],target_from_keys(hidKeysHeld()));
        }
        return;
    }

    if(active_tab==1&&x>=34&&x<286&&y>=68&&y<128) {
        custom_keyboard(target_from_keys(hidKeysHeld()));
        return;
    }

    if(x>=ADV_X&&x<ADV_X+ADV_W&&y>=ADV_Y&&y<ADV_Y+ADV_H) {
        advanced_mode=!advanced_mode;
        return;
    }

    if(x>=CLOSE_X&&x<CLOSE_X+CLOSE_W&&y>=CLOSE_Y&&y<CLOSE_Y+CLOSE_H)
        menu_open=false;
}

int main(int argc,char **argv)
{
    (void)argc; (void)argv;
    gfxInitDefault();
    hidInit();

    while(aptMainLoop()) {
        hidScanInput();
        u32 kDown=hidKeysDown();
        u32 kHeld=hidKeysHeld();
        u64 now=osGetTime();

        if(kHeld&KEY_SELECT) {
            if(!select_held) {select_held=true;select_time=now;}
            else if(now-select_time>=SELECT_HOLD_MS) break;
        } else select_held=false;

        if(kHeld&KEY_START) {
            if(!start_held) {start_held=true;start_time=now;}
            else if(!menu_open&&now-start_time>=START_HOLD_MS) {
                menu_open=true; active_tab=0;
            }
        } else start_held=false;

        if(menu_open) handle_touch(kDown);
        if((kDown&KEY_B)&&menu_open) menu_open=false;

        render();
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    hidExit();
    gfxExit();
    return 0;
}

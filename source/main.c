#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define TOP_WIDTH 400
#define BOTTOM_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_STRIDE 240
#define ONE_SECOND_MS 1000ULL

#define MENU_X 8
#define MENU_Y 8
#define MENU_W 304
#define MENU_H 224
#define RADIUS 12
#define TAB_Y 18
#define TAB_H 30
#define TAB_W 142
#define TAB1_X 18
#define TAB2_X 160
#define GRID_X 18
#define GRID_Y 58
#define CELL_W 64
#define CELL_H 36
#define CELL_GAP_X 8
#define CELL_GAP_Y 8
#define ADV_X 18
#define ADV_Y 166
#define ADV_W 284
#define ADV_H 22
#define CLOSE_X 18
#define CLOSE_Y 194
#define CLOSE_W 284
#define CLOSE_H 28

typedef struct { u8 r, g, b; const char *name; } Color;
typedef enum { TARGET_BOTH, TARGET_TOP, TARGET_BOTTOM } ColorTarget;

static const Color common_colors[] = {
    {255,0,0,"RED"},{0,255,0,"GREEN"},{0,0,255,"BLUE"},
    {255,255,0,"YELLOW"},{255,0,255,"MAGENTA"},{0,255,255,"CYAN"},
    {255,255,255,"WHITE"},{0,0,0,"BLACK"},{128,128,128,"GRAY"},
    {255,128,0,"ORANGE"},{128,0,255,"PURPLE"},{0,128,255,"SKY"}
};
#define COMMON_COUNT ((int)(sizeof(common_colors)/sizeof(common_colors[0])))

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

typedef struct { char c; u8 p[7]; } Glyph;
#define GL(c,a,b,d,e,f,g,h) {c,{a,b,d,e,f,g,h}}
static const Glyph font[] = {
    GL(' ',0,0,0,0,0,0,0),GL('!',4,4,4,4,4,0,4),GL('#',10,31,10,10,31,10,0),
    GL('(',2,4,8,8,8,4,2),GL(')',8,4,2,2,2,4,8),GL('-',0,0,0,31,0,0,0),
    GL('.',0,0,0,0,0,6,6),GL('/',1,2,4,8,16,0,0),GL(':',0,6,6,0,6,6,0),
    GL('0',14,17,19,21,25,17,14),GL('1',4,12,4,4,4,4,14),GL('2',14,17,1,2,4,8,31),
    GL('3',30,1,1,14,1,1,30),GL('4',2,6,10,18,31,2,2),GL('5',31,16,16,30,1,1,30),
    GL('6',6,8,16,30,17,17,14),GL('7',31,1,2,4,8,8,8),GL('8',14,17,17,14,17,17,14),
    GL('9',14,17,17,15,1,2,12),GL('A',14,17,17,31,17,17,17),GL('B',30,17,17,30,17,17,30),
    GL('C',14,17,16,16,16,17,14),GL('D',30,17,17,17,17,17,30),GL('E',31,16,16,30,16,16,31),
    GL('F',31,16,16,30,16,16,16),GL('G',14,17,16,23,17,17,14),GL('H',17,17,17,31,17,17,17),
    GL('I',14,4,4,4,4,4,14),GL('J',1,1,1,1,17,17,14),GL('K',17,18,20,24,20,18,17),
    GL('L',16,16,16,16,16,16,31),GL('M',17,27,21,21,17,17,17),GL('N',17,25,21,21,19,19,17),
    GL('O',14,17,17,17,17,17,14),GL('P',30,17,17,30,16,16,16),GL('Q',14,17,17,17,21,18,13),
    GL('R',30,17,17,30,20,18,17),GL('S',15,16,16,14,1,1,30),GL('T',31,4,4,4,4,4,4),
    GL('U',17,17,17,17,17,17,14),GL('V',17,17,17,17,17,10,4),GL('W',17,17,17,21,21,21,10),
    GL('X',17,17,10,4,10,17,17),GL('Y',17,17,10,4,4,4,4),GL('Z',31,1,2,4,8,16,31)
};
#undef GL

static const Glyph *glyph_for(char c)
{
    for (size_t i=0;i<sizeof(font)/sizeof(font[0]);++i)
        if (font[i].c==c) return &font[i];
    return &font[0];
}

static u16 rgb565(u8 r,u8 g,u8 b)
{
    return (u16)(((u16)(r>>3)<<11)|((u16)(g>>2)<<5)|(u16)(b>>3));
}

static u16 *get_fb(gfxScreen_t screen,u16 *w,u16 *h)
{
    return (u16 *)gfxGetFramebuffer(screen,GFX_LEFT,w,h);
}

static void pixel(u16 *fb,int x,int y,u16 color)
{
    if (!fb || x<0 || y<0 || x>=TOP_WIDTH || y>=SCREEN_HEIGHT) return;
    fb[(u32)x*FB_STRIDE+(SCREEN_HEIGHT-1-y)]=color;
}

static void rect(gfxScreen_t screen,int x,int y,int w,int h,u16 color)
{
    u16 fw=0,fh=0; u16 *fb=get_fb(screen,&fw,&fh); if(!fb) return;
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>(int)fw) w=(int)fw-x; if(y+h>(int)fh) h=(int)fh-y;
    if(w<=0||h<=0) return;
    for(int px=x;px<x+w;++px){u32 base=(u32)px*FB_STRIDE;
        for(int py=y;py<y+h;++py) fb[base+SCREEN_HEIGHT-1-py]=color;}
}

static void circle(gfxScreen_t screen,int cx,int cy,int r,u16 color)
{
    u16 fw=0,fh=0; u16 *fb=get_fb(screen,&fw,&fh); if(!fb) return;
    for(int y=-r;y<=r;++y){int dx=r; while(dx*dx+y*y>r*r)--dx;
        for(int x=-dx;x<=dx;++x) pixel(fb,cx+x,cy+y,color);}
}

static void rounded_rect(gfxScreen_t screen,int x,int y,int w,int h,int r,u16 color)
{
    if(w<=0||h<=0) return;
    rect(screen,x+r,y,w-2*r,h,color); rect(screen,x,y+r,w,h-2*r,color);
    circle(screen,x+r,y+r,r,color); circle(screen,x+w-r-1,y+r,r,color);
    circle(screen,x+r,y+h-r-1,r,color); circle(screen,x+w-r-1,y+h-r-1,r,color);
}

static void text(gfxScreen_t screen,int x,int y,const char *s,int scale,u16 color)
{
    u16 fw=0,fh=0; u16 *fb=get_fb(screen,&fw,&fh); if(!fb||!s||scale<1) return;
    int cursor=x;
    while(*s){const Glyph *g=glyph_for(*s++);
        for(int gy=0;gy<7;++gy) for(int gx=0;gx<5;++gx) if(g->p[gy]&(1<<(4-gx)))
            for(int sy=0;sy<scale;++sy) for(int sx=0;sx<scale;++sx)
                pixel(fb,cursor+gx*scale+sx,y+gy*scale+sy,color);
        cursor+=6*scale;}
}

static void centered(gfxScreen_t screen,int y,int cx,const char *s,int scale,u16 color)
{
    text(screen,cx-((int)strlen(s)*6*scale)/2,y,s,scale,color);
}

static void fill_screen(gfxScreen_t screen,Color c)
{
    rect(screen,0,0,screen==GFX_TOP?TOP_WIDTH:BOTTOM_WIDTH,SCREEN_HEIGHT,rgb565(c.r,c.g,c.b));
}

static ColorTarget target_from_keys(u32 held)
{
    if(!advanced_mode) return TARGET_BOTH;
    if((held&KEY_L)&&!(held&KEY_R)) return TARGET_BOTTOM;
    if((held&KEY_R)&&!(held&KEY_L)) return TARGET_TOP;
    return TARGET_BOTH;
}

static void apply_color(Color c,ColorTarget target)
{
    if(target==TARGET_BOTH||target==TARGET_TOP) top_color=c;
    if(target==TARGET_BOTH||target==TARGET_BOTTOM) bottom_color=c;
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
    if(len==7&&s[0]=='#') ++p; else if(len!=6) return false;
    u8 d[6]; for(int i=0;i<6;++i) if(!hex_digit(p[i],&d[i])) return false;
    out->r=(u8)((d[0]<<4)|d[1]); out->g=(u8)((d[2]<<4)|d[3]);
    out->b=(u8)((d[4]<<4)|d[5]); out->name="CUSTOM"; return true;
}

static void custom_keyboard(ColorTarget target)
{
    SwkbdState state; char input[8]={0};
    swkbdInit(&state,SWKBD_TYPE_WESTERN,2,7);
    swkbdSetHintText(&state,"#RRGGBB");
    swkbdSetValidation(&state,SWKBD_NOTEMPTY_NOTBLANK,0,0);
    if(swkbdInputText(&state,input,sizeof(input))!=SWKBD_BUTTON_CONFIRM) return;
    Color parsed; if(parse_hex(input,&parsed)){custom_color=parsed;apply_color(custom_color,target);}
}

static void draw_menu(void)
{
    const u16 panel=rgb565(38,41,49), tab=rgb565(58,62,73), selected=rgb565(65,105,195);
    const u16 white=rgb565(245,247,250), muted=rgb565(175,181,192), close=rgb565(125,48,55);
    const u16 on=rgb565(55,135,82);

    fill_screen(GFX_BOTTOM,(Color){18,20,25,"MENU"});
    rounded_rect(GFX_BOTTOM,MENU_X,MENU_Y,MENU_W,MENU_H,RADIUS,panel);
    rounded_rect(GFX_BOTTOM,TAB1_X,TAB_Y,TAB_W,TAB_H,9,active_tab==0?selected:tab);
    rounded_rect(GFX_BOTTOM,TAB2_X,TAB_Y,TAB_W,TAB_H,9,active_tab==1?selected:tab);
    centered(GFX_BOTTOM,TAB_Y+9,TAB1_X+TAB_W/2,"COMMON",2,white);
    centered(GFX_BOTTOM,TAB_Y+9,TAB2_X+TAB_W/2,"CUSTOM",2,white);

    if(active_tab==0){
        for(int i=0;i<COMMON_COUNT;++i){int col=i%4,row=i/4;
            int x=GRID_X+col*(CELL_W+CELL_GAP_X),y=GRID_Y+row*(CELL_H+CELL_GAP_Y);
            Color c=common_colors[i]; rounded_rect(GFX_BOTTOM,x,y,CELL_W,CELL_H,7,rgb565(c.r,c.g,c.b));}
        centered(GFX_BOTTOM,ADV_Y+7,BOTTOM_WIDTH/2,advanced_mode?"L BOT  R TOP":"ADVANCED OFF",1,muted);
    }else{
        rounded_rect(GFX_BOTTOM,36,70,248,58,10,tab);
        centered(GFX_BOTTOM,83,BOTTOM_WIDTH/2,"ENTER HEX",2,white);
        centered(GFX_BOTTOM,106,BOTTOM_WIDTH/2,"TAP TO TYPE",1,muted);
    }

    rounded_rect(GFX_BOTTOM,ADV_X,ADV_Y,ADV_W,ADV_H,8,advanced_mode?on:tab);
    centered(GFX_BOTTOM,ADV_Y+7,BOTTOM_WIDTH/2,advanced_mode?"ADVANCED ON":"ADVANCED OFF",1,white);
    rounded_rect(GFX_BOTTOM,CLOSE_X,CLOSE_Y,CLOSE_W,CLOSE_H,9,close);
    centered(GFX_BOTTOM,CLOSE_Y+8,BOTTOM_WIDTH/2,"CLOSE  B",2,white);
}

static void draw_frame(void)
{
    fill_screen(GFX_TOP,top_color);
    fill_screen(GFX_BOTTOM,bottom_color);
    if(menu_open) draw_menu();
}

static void touch_menu(u32 held)
{
    touchPosition t; hidTouchRead(&t); int x=t.px,y=t.py;
    if(x>=TAB1_X&&x<TAB1_X+TAB_W&&y>=TAB_Y&&y<TAB_Y+TAB_H){active_tab=0;return;}
    if(x>=TAB2_X&&x<TAB2_X+TAB_W&&y>=TAB_Y&&y<TAB_Y+TAB_H){active_tab=1;return;}

    if(active_tab==0&&x>=GRID_X&&y>=GRID_Y&&y<158){
        int col=(x-GRID_X)/(CELL_W+CELL_GAP_X),row=(y-GRID_Y)/(CELL_H+CELL_GAP_Y);
        int lx=(x-GRID_X)%(CELL_W+CELL_GAP_X),ly=(y-GRID_Y)%(CELL_H+CELL_GAP_Y);
        if(col>=0&&col<4&&row>=0&&row<3&&lx<CELL_W&&ly<CELL_H){
            int i=row*4+col; if(i<COMMON_COUNT) apply_color(common_colors[i],target_from_keys(held));}
        return;
    }
    if(active_tab==1&&x>=36&&x<284&&y>=70&&y<128){custom_keyboard(target_from_keys(held));return;}
    if(x>=ADV_X&&x<ADV_X+ADV_W&&y>=ADV_Y&&y<ADV_Y+ADV_H){advanced_mode=!advanced_mode;return;}
    if(x>=CLOSE_X&&x<CLOSE_X+CLOSE_W&&y>=CLOSE_Y&&y<CLOSE_Y+CLOSE_H) menu_open=false;
}

int main(int argc,char **argv)
{
    (void)argc;(void)argv;
    gfxInitDefault();
    hidInit();

    while(aptMainLoop()){
        hidScanInput(); u32 down=hidKeysDown(),held=hidKeysHeld(); u64 now=osGetTime();

        /* SELECT always exits after one continuous second, even in the menu. */
        if(held&KEY_SELECT){
            if(!select_held){select_held=true;select_time=now;}
            else if(now-select_time>=ONE_SECOND_MS) break;
        }else select_held=false;

        /* START opens the menu after one continuous second. */
        if(held&KEY_START){
            if(!start_held){start_held=true;start_time=now;}
            else if(!menu_open&&now-start_time>=ONE_SECOND_MS){menu_open=true;active_tab=0;}
        }else start_held=false;

        if(menu_open){
            if(down&KEY_B) menu_open=false;
            else if(down&KEY_TOUCH) touch_menu(held);
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

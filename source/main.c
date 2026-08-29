#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct { u8 r,g,b; } Color;
typedef enum { TARGET_BOTH, TARGET_TOP, TARGET_BOTTOM } Target;
typedef struct { u8 *data; int width; int height; } Frame;

enum { FB_STRIDE=240, FB_BPP=3, TOP_WIDTH=400, BOTTOM_WIDTH=320, SCREEN_HEIGHT=240 };
#define HOLD_TICKS 268000000ULL
#define NO_BUTTON (-1)
#define BTN_TAB_COMMON 0
#define BTN_TAB_CUSTOM 1
#define BTN_CLOSE 2
#define BTN_APPLY 3
#define BTN_HEX 4
#define BTN_COLOR_BASE 10
#define BTN_SLIDER_R 30
#define BTN_SLIDER_G 31
#define BTN_SLIDER_B 32

static const Color common_colors[]={{255,0,0},{0,255,0},{0,0,255},{255,255,0},{255,0,255},{0,255,255},{255,255,255},{0,0,0},{128,128,128},{255,128,0},{128,0,255},{0,128,255}};
#define COMMON_COUNT ((int)(sizeof(common_colors)/sizeof(common_colors[0])))
static Color top_color={0,255,0},bottom_color={0,255,0},custom_color={0,255,0};
static bool menu_open=false,start_held=false,select_held=false;
static int tab=0,pressed_button=NO_BUTTON;
static Target pressed_target=TARGET_BOTH;
static u64 start_tick=0,select_tick=0;

static Frame frame_get(gfxScreen_t s){ Frame f; u16 w,h; f.data=(u8*)gfxGetFramebuffer(s,GFX_LEFT,&w,&h); f.width=(s==GFX_TOP)?TOP_WIDTH:BOTTOM_WIDTH; f.height=SCREEN_HEIGHT; return f; }
static inline bool ok(const Frame*f){return f&&f->data;}
static inline u8* pixel(const Frame*f,int x,int y){return f->data+((x*FB_STRIDE)+(239-y))*FB_BPP;}
static inline void put(const Frame*f,int x,int y,Color c){if(!ok(f)||x<0||y<0||x>=f->width||y>=f->height)return;u8*p=pixel(f,x,y);p[0]=c.b;p[1]=c.g;p[2]=c.r;}
static void clear_frame(const Frame*f,Color c){if(!ok(f))return;for(int x=0;x<f->width;x++)for(int y=0;y<240;y++){u8*p=pixel(f,x,y);p[0]=c.b;p[1]=c.g;p[2]=c.r;}}
static void rect(const Frame*f,int x,int y,int w,int h,Color c){if(!ok(f)||w<=0||h<=0)return;if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>f->width)w=f->width-x;if(y+h>240)h=240-y;if(w<=0||h<=0)return;for(int px=x;px<x+w;px++)for(int py=y;py<y+h;py++)put(f,px,py,c);}
static void circle(const Frame*f,int cx,int cy,int r,Color c){for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++)if(x*x+y*y<=r*r)put(f,cx+x,cy+y,c);}
static void roundrect(const Frame*f,int x,int y,int w,int h,int r,Color c){if(r<=0){rect(f,x,y,w,h,c);return;}if(r*2>w)r=w/2;if(r*2>h)r=h/2;rect(f,x+r,y,w-2*r,h,c);rect(f,x,y+r,w,h-2*r,c);circle(f,x+r,y+r,r,c);circle(f,x+w-r-1,y+r,r,c);circle(f,x+r,y+h-r-1,r,c);circle(f,x+w-r-1,y+h-r-1,r,c);}

typedef struct{char c;u8 rows[7];}Glyph;
#define G(c,a,b,d,e,f,g,h) {c,{a,b,d,e,f,g,h}}
static const Glyph font[]={G(' ',0,0,0,0,0,0,0),G('#',10,31,10,10,31,10,0),G('.',0,0,0,0,0,6,6),G(':',0,6,6,0,6,6,0),G('-',0,0,0,31,0,0,0),G('0',14,17,19,21,25,17,14),G('1',4,12,4,4,4,4,14),G('2',14,17,1,2,4,8,31),G('3',30,1,1,14,1,1,30),G('4',2,6,10,18,31,2,2),G('5',31,16,16,30,1,1,30),G('6',6,8,16,30,17,17,14),G('7',31,1,2,4,8,8,8),G('8',14,17,17,14,17,17,14),G('9',14,17,17,15,1,2,12),G('A',14,17,17,31,17,17,17),G('B',30,17,17,30,17,17,30),G('C',14,17,16,16,16,17,14),G('D',30,17,17,17,17,17,30),G('E',31,16,16,30,16,16,31),G('F',31,16,16,30,16,16,16),G('G',14,17,16,23,17,17,14),G('H',17,17,17,31,17,17,17),G('I',14,4,4,4,4,4,14),G('J',1,1,1,1,17,17,14),G('K',17,18,20,24,20,18,17),G('L',16,16,16,16,16,16,31),G('M',17,27,21,21,17,17,17),G('N',17,25,21,21,19,19,17),G('O',14,17,17,17,17,17,14),G('P',30,17,17,30,16,16,16),G('Q',14,17,17,17,21,18,13),G('R',30,17,17,30,20,18,17),G('S',15,16,16,14,1,1,30),G('T',31,4,4,4,4,4,4),G('U',17,17,17,17,17,17,14),G('V',17,17,17,17,17,10,4),G('W',17,17,17,21,21,21,10),G('X',17,17,10,4,10,17,17),G('Y',17,17,10,4,4,4,4),G('Z',31,1,2,4,8,16,31)};
#undef G
static const Glyph*glyph(char c){for(size_t i=0;i<sizeof(font)/sizeof(font[0]);i++)if(font[i].c==c)return&font[i];return&font[0];}
static void text(const Frame*f,int x,int y,const char*s,int scale,Color c){while(s&&*s){const Glyph*g=glyph(*s++);for(int gy=0;gy<7;gy++)for(int gx=0;gx<5;gx++)if(g->rows[gy]&(1u<<(4-gx)))for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++)put(f,x+gx*scale+sx,y+gy*scale+sy,c);x+=6*scale;}}
static void centered(const Frame*f,int cx,int y,const char*s,int scale,Color c){text(f,cx-(int)strlen(s)*3*scale,y,s,scale,c);}
static Target target(u32 h){if((h&KEY_L)&&!(h&KEY_R))return TARGET_TOP;if((h&KEY_R)&&!(h&KEY_L))return TARGET_BOTTOM;return TARGET_BOTH;}
static void apply_color(Color c,Target t){if(t==TARGET_BOTH||t==TARGET_TOP)top_color=c;if(t==TARGET_BOTH||t==TARGET_BOTTOM)bottom_color=c;}
static Color dark(Color c){c.r=(u8)((u16)c.r*3/4);c.g=(u8)((u16)c.g*3/4);c.b=(u8)((u16)c.b*3/4);return c;}
static bool hd(char c,u8*v){if(c>='0'&&c<='9'){*v=c-'0';return true;}if(c>='a'&&c<='f'){*v=c-'a'+10;return true;}if(c>='A'&&c<='F'){*v=c-'A'+10;return true;}return false;}
static bool parse_hex(const char*s,Color*out){size_t n=strlen(s);if(n==7&&s[0]=='#')s++;else if(n!=6)return false;u8 d[6];for(int i=0;i<6;i++)if(!hd(s[i],&d[i]))return false;out->r=(u8)((d[0]<<4)|d[1]);out->g=(u8)((d[2]<<4)|d[3]);out->b=(u8)((d[4]<<4)|d[5]);return true;}
static void keyboard(void){SwkbdState st;char in[8]={0};swkbdInit(&st,SWKBD_TYPE_WESTERN,2,7);swkbdSetHintText(&st,"#RRGGBB");swkbdSetValidation(&st,SWKBD_NOTEMPTY_NOTBLANK,0,0);if(swkbdInputText(&st,in,sizeof(in))==SWKBD_BUTTON_CONFIRM){Color c;if(parse_hex(in,&c))custom_color=c;}}
static int slider_x(int v){return 62+(v*216)/255;}
static int slider_v(int x){if(x<=62)return 0;if(x>=278)return 255;return ((x-62)*255)/216;}
static void slider(const Frame*f,int y,char lab,int v,Color fc,bool p){Color tr={58,63,74},wh={245,247,250};text(f,25,y-5,(char[]){lab,0},2,wh);roundrect(f,62,y,216,12,6,tr);roundrect(f,62,y,slider_x(v)-62,12,6,fc);circle(f,slider_x(v),y+6,p?9:7,p?dark(wh):wh);}
static void draw_menu(const Frame*f){Color panel={35,39,48},inactive={53,58,70},sel={58,102,190},wh={245,247,250},close={125,48,55},ap={48,119,72};clear_frame(f,bottom_color);roundrect(f,4,4,312,232,16,panel);Color a=tab==0?sel:inactive,b=tab==1?sel:inactive;if(pressed_button==BTN_TAB_COMMON)a=dark(a);if(pressed_button==BTN_TAB_CUSTOM)b=dark(b);roundrect(f,12,14,138,30,8,a);roundrect(f,170,14,138,30,8,b);centered(f,81,23,"COMMON",2,wh);centered(f,239,23,"CUSTOM",2,wh);
if(tab==0){for(int i=0;i<COMMON_COUNT;i++){int col=i%4,row=i/4;Color c=common_colors[i];if(pressed_button==BTN_COLOR_BASE+i)c=dark(c);roundrect(f,14+col*73,56+row*41,65,34,8,c);}}
else{slider(f,65,'R',custom_color.r,(Color){220,65,65},pressed_button==BTN_SLIDER_R);slider(f,101,'G',custom_color.g,(Color){65,190,85},pressed_button==BTN_SLIDER_G);slider(f,137,'B',custom_color.b,(Color){65,110,220},pressed_button==BTN_SLIDER_B);Color ab=ap;if(pressed_button==BTN_APPLY)ab=dark(ab);Color hb=inactive;if(pressed_button==BTN_HEX)hb=dark(hb);roundrect(f,18,166,137,28,8,ab);roundrect(f,165,166,137,28,8,hb);centered(f,86,175,"APPLY",2,wh);centered(f,234,175,"CUSTOM",2,wh);}
Color cb=close;if(pressed_button==BTN_CLOSE)cb=dark(cb);roundrect(f,12,204,296,24,8,cb);centered(f,160,211,"CLOSE MENU",2,wh);}
static int hit(int x,int y){if(y>=14&&y<44){if(x>=12&&x<150)return BTN_TAB_COMMON;if(x>=170&&x<308)return BTN_TAB_CUSTOM;}if(tab==0&&x>=14&&x<306&&y>=56&&y<179){int col=(x-14)/73,row=(y-56)/41,lx=(x-14)%73,ly=(y-56)%41;if(col<4&&row<3&&lx<65&&ly<34){int i=row*4+col;if(i<COMMON_COUNT)return BTN_COLOR_BASE+i;}}if(tab==1){if(y>=59&&y<89)return BTN_SLIDER_R;if(y>=95&&y<125)return BTN_SLIDER_G;if(y>=131&&y<161)return BTN_SLIDER_B;if(y>=166&&y<194){if(x<155)return BTN_APPLY;if(x>=165)return BTN_HEX;}}if(y>=204&&y<228&&x>=12&&x<308)return BTN_CLOSE;return NO_BUTTON;}
static void activate(int b,Target t){if(b==BTN_TAB_COMMON)tab=0;else if(b==BTN_TAB_CUSTOM)tab=1;else if(b==BTN_CLOSE)menu_open=false;else if(b==BTN_APPLY)apply_color(custom_color,t);else if(b==BTN_HEX)keyboard();else if(b>=BTN_COLOR_BASE&&b<BTN_COLOR_BASE+COMMON_COUNT)apply_color(common_colors[b-BTN_COLOR_BASE],t);}
static void touch_update(u32 down,u32 held){static int touch_button=NO_BUTTON;static bool touching=false;touchPosition p;if(down&KEY_TOUCH){hidTouchRead(&p);touch_button=hit(p.px,p.py);pressed_button=touch_button;pressed_target=target(held);touching=true;return;}if(!touching)return;if(held&KEY_TOUCH){hidTouchRead(&p);if(hit(p.px,p.py)!=touch_button){touch_button=NO_BUTTON;pressed_button=NO_BUTTON;}return;}if(touch_button!=NO_BUTTON)activate(touch_button,pressed_target);touch_button=NO_BUTTON;pressed_button=NO_BUTTON;touching=false;}
static void update_slider(u32 held){if(!(held&KEY_TOUCH)||pressed_button<BTN_SLIDER_R||pressed_button>BTN_SLIDER_B)return;touchPosition p;hidTouchRead(&p);if(p.px<0||p.px>319)return;int v=slider_v(p.px);if(pressed_button==BTN_SLIDER_R)custom_color.r=v;else if(pressed_button==BTN_SLIDER_G)custom_color.g=v;else custom_color.b=v;}
int main(void){gfxInitDefault();Color green={0,255,0};top_color=bottom_color=custom_color=green;while(aptMainLoop()){hidScanInput();u32 down=hidKeysDown(),held=hidKeysHeld();if(held&KEY_START){if(!start_held){start_held=true;start_tick=svcGetSystemTick();}else if(!menu_open&&svcGetSystemTick()-start_tick>=HOLD_TICKS){menu_open=true;tab=0;}}else start_held=false;if(held&KEY_SELECT){if(!select_held){select_held=true;select_tick=svcGetSystemTick();}else if(svcGetSystemTick()-select_tick>=HOLD_TICKS)break;}else select_held=false;if(menu_open){touch_update(down,held);update_slider(held);}else if(down&KEY_TOUCH){}Frame top=frame_get(GFX_TOP),bot=frame_get(GFX_BOTTOM);clear_frame(&top,top_color);clear_frame(&bot,bottom_color);if(menu_open)draw_menu(&bot);gfxFlushBuffers();gspWaitForVBlank();gfxSwapBuffers();}gfxExit();return 0;}

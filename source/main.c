#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
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
#define BTN_CREDITS_CLOSE 40
#define BTN_CREDITS_EXIT 41

static const Color common_colors[] = {
    {255,0,0},{0,255,0},{0,0,255},{255,255,0},
    {255,0,255},{0,255,255},{255,255,255},{0,0,0},
    {128,128,128},{255,128,0},{128,0,255},{0,128,255}
};
#define COMMON_COUNT ((int)(sizeof(common_colors)/sizeof(common_colors[0])))

static Color top_color={0,255,0}, bottom_color={0,255,0}, custom_color={0,255,0};
static bool menu_open=false, credits_open=false;
static bool start_held=false, select_held=false;
static bool app_exit_requested=false;
static int tab=0, pressed_button=NO_BUTTON;
static Target pressed_target=TARGET_BOTH;
static u64 start_tick=0, select_tick=0;

/* -------------------------------------------------------------------------
   Procedural UI audio
   -------------------------------------------------------------------------
   Sounds are generated entirely in RAM. No WAV/OGG/MP3 files are needed.
   The 3DS DSP plays the generated stereo PCM16 buffers directly.
*/
#define SOUND_RATE 32728.0f
#define TAP_SAMPLES 1700
#define COLOR_SAMPLES 7200
#define SOUND_CHANNEL_TAP 0
#define SOUND_CHANNEL_BOTH 1
#define SOUND_CHANNEL_TOP 2
#define SOUND_CHANNEL_BOTTOM 3

typedef struct {
    u32 *samples;
    size_t sample_count;
    ndspWaveBuf wave;
} Sound;

static Sound tap_sound;
static Sound both_sound;
static Sound top_sound;
static Sound bottom_sound;
static bool audio_ready=false;

static int16_t clamp_sample(float v) {
    if(v>32767.0f) return 32767;
    if(v<-32768.0f) return -32768;
    return (int16_t)v;
}

static void generate_tone(Sound *sound, size_t count, float start_freq, float end_freq,
                          float attack, float release, float volume) {
    sound->sample_count=count;
    sound->samples=(u32*)linearAlloc(count*sizeof(u32));
    if(!sound->samples) return;

    float phase=0.0f;
    for(size_t i=0;i<count;i++) {
        float t=(float)i/(float)count;
        float freq=start_freq+(end_freq-start_freq)*t;
        phase += (2.0f*(float)M_PI*freq)/SOUND_RATE;
        if(phase>2.0f*(float)M_PI) phase-=2.0f*(float)M_PI;

        float env=1.0f;
        float a=attack/(float)count;
        float r=release/(float)count;
        if(a>0.0f && t<a) env=t/a;
        if(r>0.0f && t>1.0f-r) env=(1.0f-t)/r;
        if(env<0.0f) env=0.0f;

        int16_t s=clamp_sample(sinf(phase)*32767.0f*volume*env);
        sound->samples[i]=((u32)(uint16_t)s<<16)|(uint16_t)s;
    }

    memset(&sound->wave,0,sizeof(sound->wave));
    sound->wave.data_vaddr=sound->samples;
    sound->wave.nsamples=count;
    DSP_FlushDataCache(sound->samples,count*sizeof(u32));
}

static void init_sound_channel(int channel) {
    float mix[12];
    memset(mix,0,sizeof(mix));
    mix[0]=1.0f;
    mix[1]=1.0f;
    ndspChnSetInterp(channel,NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel,SOUND_RATE);
    ndspChnSetFormat(channel,NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetMix(channel,mix);
}

static bool sound_busy(Sound *sound) {
    return sound->wave.status==NDSP_WBUF_PLAYING;
}

static void play_sound(Sound *sound,int channel) {
    if(!audio_ready || !sound->samples) return;
    ndspChnWaveBufClear(channel);
    sound->wave.status=NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(channel,&sound->wave);
}

static void init_audio(void) {
    if(R_FAILED(ndspInit())) return;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    generate_tone(&tap_sound,TAP_SAMPLES,760.0f,560.0f,70.0f,450.0f,0.18f);
    generate_tone(&both_sound,COLOR_SAMPLES,440.0f,660.0f,400.0f,2200.0f,0.16f);
    generate_tone(&top_sound,COLOR_SAMPLES,660.0f,880.0f,400.0f,2200.0f,0.16f);
    generate_tone(&bottom_sound,COLOR_SAMPLES,330.0f,440.0f,400.0f,2200.0f,0.16f);

    if(tap_sound.samples && both_sound.samples && top_sound.samples && bottom_sound.samples) {
        init_sound_channel(SOUND_CHANNEL_TAP);
        init_sound_channel(SOUND_CHANNEL_BOTH);
        init_sound_channel(SOUND_CHANNEL_TOP);
        init_sound_channel(SOUND_CHANNEL_BOTTOM);
        audio_ready=true;
    }
}

static void exit_audio(void) {
    if(!audio_ready && !tap_sound.samples && !both_sound.samples && !top_sound.samples && !bottom_sound.samples)
        return;
    ndspChnWaveBufClear(SOUND_CHANNEL_TAP);
    ndspChnWaveBufClear(SOUND_CHANNEL_BOTH);
    ndspChnWaveBufClear(SOUND_CHANNEL_TOP);
    ndspChnWaveBufClear(SOUND_CHANNEL_BOTTOM);
    if(tap_sound.samples) linearFree(tap_sound.samples);
    if(both_sound.samples) linearFree(both_sound.samples);
    if(top_sound.samples) linearFree(top_sound.samples);
    if(bottom_sound.samples) linearFree(bottom_sound.samples);
    ndspExit();
    audio_ready=false;
}

static void play_ui_tap(void) {
    play_sound(&tap_sound,SOUND_CHANNEL_TAP);
}

static void play_color_sound(Target t) {
    if(t==TARGET_TOP) play_sound(&top_sound,SOUND_CHANNEL_TOP);
    else if(t==TARGET_BOTTOM) play_sound(&bottom_sound,SOUND_CHANNEL_BOTTOM);
    else play_sound(&both_sound,SOUND_CHANNEL_BOTH);
}

static Frame frame_get(gfxScreen_t s) {
    Frame f;
    u16 w,h;
    f.data=(u8*)gfxGetFramebuffer(s,GFX_LEFT,&w,&h);
    f.width=(s==GFX_TOP)?TOP_WIDTH:BOTTOM_WIDTH;
    f.height=SCREEN_HEIGHT;
    return f;
}

static inline bool ok(const Frame *f) { return f && f->data; }

static inline u8 *pixel(const Frame *f,int x,int y) {
    return f->data+((x*FB_STRIDE)+(239-y))*FB_BPP;
}

static inline void put(const Frame *f,int x,int y,Color c) {
    if(!ok(f)||x<0||y<0||x>=f->width||y>=f->height) return;
    u8 *p=pixel(f,x,y);
    p[0]=c.b; p[1]=c.g; p[2]=c.r;
}

static void clear_frame(const Frame *f,Color c) {
    if(!ok(f)) return;
    for(int x=0;x<f->width;x++)
        for(int y=0;y<240;y++) {
            u8 *p=pixel(f,x,y);
            p[0]=c.b; p[1]=c.g; p[2]=c.r;
        }
}

static void rect(const Frame *f,int x,int y,int w,int h,Color c) {
    if(!ok(f)||w<=0||h<=0) return;
    if(x<0){w+=x;x=0;}
    if(y<0){h+=y;y=0;}
    if(x+w>f->width) w=f->width-x;
    if(y+h>240) h=240-y;
    if(w<=0||h<=0) return;
    for(int px=x;px<x+w;px++)
        for(int py=y;py<y+h;py++) put(f,px,py,c);
}

static void circle(const Frame *f,int cx,int cy,int r,Color c) {
    for(int y=-r;y<=r;y++)
        for(int x=-r;x<=r;x++)
            if(x*x+y*y<=r*r) put(f,cx+x,cy+y,c);
}

static void roundrect(const Frame *f,int x,int y,int w,int h,int r,Color c) {
    if(r<=0){rect(f,x,y,w,h,c);return;}
    if(r*2>w) r=w/2;
    if(r*2>h) r=h/2;
    rect(f,x+r,y,w-2*r,h,c);
    rect(f,x,y+r,w,h-2*r,c);
    circle(f,x+r,y+r,r,c);
    circle(f,x+w-r-1,y+r,r,c);
    circle(f,x+r,y+h-r-1,r,c);
    circle(f,x+w-r-1,y+h-r-1,r,c);
}

typedef struct { char c; u8 rows[7]; } Glyph;
#define G(c,a,b,d,e,f,g,h) {c,{a,b,d,e,f,g,h}}
static const Glyph font[] = {
    G(' ',0,0,0,0,0,0,0),G('#',10,31,10,10,31,10,0),
    G('.',0,0,0,0,0,6,6),G(':',0,6,6,0,6,6,0),G('-',0,0,0,31,0,0,0),
    G('0',14,17,19,21,25,17,14),G('1',4,12,4,4,4,4,14),G('2',14,17,1,2,4,8,31),
    G('3',30,1,1,14,1,1,30),G('4',2,6,10,18,31,2,2),G('5',31,16,16,30,1,1,30),
    G('6',6,8,16,30,17,17,14),G('7',31,1,2,4,8,8,8),G('8',14,17,17,14,17,17,14),
    G('9',14,17,17,15,1,2,12),G('A',14,17,17,31,17,17,17),G('B',30,17,17,30,17,17,30),
    G('C',14,17,16,16,16,17,14),G('D',30,17,17,17,17,17,30),G('E',31,16,16,30,16,16,31),
    G('F',31,16,16,30,16,16,16),G('G',14,17,16,23,17,17,14),G('H',17,17,17,31,17,17,17),
    G('I',14,4,4,4,4,4,14),G('J',1,1,1,1,17,17,14),G('K',17,18,20,24,20,18,17),
    G('L',16,16,16,16,16,16,31),G('M',17,27,21,21,17,17,17),G('N',17,25,21,21,19,19,17),
    G('O',14,17,17,17,17,17,14),G('P',30,17,17,30,16,16,16),G('Q',14,17,17,17,21,18,13),
    G('R',30,17,17,30,20,18,17),G('S',15,16,16,14,1,1,30),G('T',31,4,4,4,4,4,4),
    G('U',17,17,17,17,17,17,14),G('V',17,17,17,17,17,10,4),G('W',17,17,17,21,21,21,10),
    G('X',17,17,10,4,10,17,17),G('Y',17,17,10,4,4,4,4),G('Z',31,1,2,4,8,16,31)
};
#undef G

static const Glyph *glyph(char c) {
    for(size_t i=0;i<sizeof(font)/sizeof(font[0]);i++) if(font[i].c==c) return &font[i];
    return &font[0];
}

static void text(const Frame *f,int x,int y,const char *s,int scale,Color c) {
    while(s && *s) {
        const Glyph *g=glyph(*s++);
        for(int gy=0;gy<7;gy++) for(int gx=0;gx<5;gx++) if(g->rows[gy]&(1u<<(4-gx)))
            for(int sy=0;sy<scale;sy++) for(int sx=0;sx<scale;sx++) put(f,x+gx*scale+sx,y+gy*scale+sy,c);
        x+=6*scale;
    }
}

static void centered(const Frame *f,int cx,int y,const char *s,int scale,Color c) {
    text(f,cx-(int)strlen(s)*3*scale,y,s,scale,c);
}

static Target target(u32 h) {
    if((h&KEY_L)&&!(h&KEY_R)) return TARGET_TOP;
    if((h&KEY_R)&&!(h&KEY_L)) return TARGET_BOTTOM;
    return TARGET_BOTH;
}

static void apply_color(Color c,Target t) {
    if(t==TARGET_BOTH||t==TARGET_TOP) top_color=c;
    if(t==TARGET_BOTH||t==TARGET_BOTTOM) bottom_color=c;
}

static Color dark(Color c) {
    c.r=(u8)((u16)c.r*3/4); c.g=(u8)((u16)c.g*3/4); c.b=(u8)((u16)c.b*3/4); return c;
}

static bool hd(char c,u8 *v) {
    if(c>='0'&&c<='9'){*v=c-'0';return true;} if(c>='a'&&c<='f'){*v=c-'a'+10;return true;}
    if(c>='A'&&c<='F'){*v=c-'A'+10;return true;} return false;
}

static bool parse_hex(const char *s,Color *out) {
    size_t n=strlen(s); if(n==7&&s[0]=='#') s++; else if(n!=6) return false;
    u8 d[6]; for(int i=0;i<6;i++) if(!hd(s[i],&d[i])) return false;
    out->r=(u8)((d[0]<<4)|d[1]); out->g=(u8)((d[2]<<4)|d[3]); out->b=(u8)((d[4]<<4)|d[5]); return true;
}

static void keyboard(void) {
    SwkbdState st; char in[8]={0}; swkbdInit(&st,SWKBD_TYPE_WESTERN,2,7);
    swkbdSetHintText(&st,"#RRGGBB"); swkbdSetValidation(&st,SWKBD_NOTEMPTY_NOTBLANK,0,0);
    if(swkbdInputText(&st,in,sizeof(in))==SWKBD_BUTTON_CONFIRM) { Color c; if(parse_hex(in,&c)) custom_color=c; }
}

static int slider_x(int v) { return 62+(v*216)/255; }
static int slider_v(int x) { if(x<=62)return 0; if(x>=278)return 255; return ((x-62)*255)/216; }

static void slider(const Frame *f,int y,char lab,int v,Color fc,bool p) {
    Color tr={58,63,74},wh={245,247,250}; char s[2]={lab,0}; text(f,25,y-5,s,2,wh);
    roundrect(f,62,y,216,12,6,tr); roundrect(f,62,y,slider_x(v)-62,12,6,fc); circle(f,slider_x(v),y+6,p?9:7,p?dark(wh):wh);
}

static void draw_menu(const Frame *f) {
    Color panel={35,39,48},inactive={53,58,70},sel={58,102,190},wh={245,247,250},close={125,48,55},ap={48,119,72};
    clear_frame(f,bottom_color); roundrect(f,4,4,312,232,16,panel);
    Color a=tab==0?sel:inactive,b=tab==1?sel:inactive;
    if(pressed_button==BTN_TAB_COMMON)a=dark(a); if(pressed_button==BTN_TAB_CUSTOM)b=dark(b);
    roundrect(f,12,14,138,30,8,a); roundrect(f,170,14,138,30,8,b);
    centered(f,81,23,"COMMON",2,wh); centered(f,239,23,"CUSTOM",2,wh);
    if(tab==0) for(int i=0;i<COMMON_COUNT;i++){int col=i%4,row=i/4;Color c=common_colors[i];if(pressed_button==BTN_COLOR_BASE+i)c=dark(c);roundrect(f,14+col*73,56+row*41,65,34,8,c);}
    else {
        slider(f,65,'R',custom_color.r,(Color){220,65,65},pressed_button==BTN_SLIDER_R);
        slider(f,101,'G',custom_color.g,(Color){65,190,85},pressed_button==BTN_SLIDER_G);
        slider(f,137,'B',custom_color.b,(Color){65,110,220},pressed_button==BTN_SLIDER_B);
        Color ab=ap,hb=inactive;if(pressed_button==BTN_APPLY)ab=dark(ab);if(pressed_button==BTN_HEX)hb=dark(hb);
        roundrect(f,18,166,137,28,8,ab);roundrect(f,165,166,137,28,8,hb);centered(f,86,175,"APPLY",2,wh);centered(f,234,175,"CUSTOM",2,wh);
    }
    Color cb=close;if(pressed_button==BTN_CLOSE)cb=dark(cb);roundrect(f,12,204,296,24,8,cb);centered(f,160,211,"CLOSE MENU",2,wh);
}

static void draw_credits(const Frame *f) {
    Color panel={35,39,48},wh={245,247,250},close={70,90,125},exitc={125,48,55};
    clear_frame(f,bottom_color); roundrect(f,4,4,312,232,16,panel);
    centered(f,160,16,"COLOR 3DS",3,wh);
    centered(f,160,50,"A SIMPLE SCREEN COLOR",2,wh);
    centered(f,160,68,"CHANGER FOR NINTENDO 3DS",2,wh);
    centered(f,160,98,"MADE WITH AI",2,wh);
    centered(f,160,128,"THIS APP CHANGES COLORS",2,wh);
    centered(f,160,146,"INSIDE THE APP ONLY",2,wh);
    if(pressed_button==BTN_CREDITS_CLOSE)close=dark(close); if(pressed_button==BTN_CREDITS_EXIT)exitc=dark(exitc);
    roundrect(f,12,174,296,24,8,close);roundrect(f,12,204,296,24,8,exitc);
    centered(f,160,181,"CLOSE CREDITS",2,wh);centered(f,160,211,"CLOSE APP",2,wh);
}

static int hit(int x,int y) {
    if(credits_open){if(y>=174&&y<198&&x>=12&&x<308)return BTN_CREDITS_CLOSE;if(y>=204&&y<228&&x>=12&&x<308)return BTN_CREDITS_EXIT;return NO_BUTTON;}
    if(y>=14&&y<44){if(x>=12&&x<150)return BTN_TAB_COMMON;if(x>=170&&x<308)return BTN_TAB_CUSTOM;}
    if(tab==0&&x>=14&&x<306&&y>=56&&y<179){int col=(x-14)/73,row=(y-56)/41,lx=(x-14)%73,ly=(y-56)%41;if(col<4&&row<3&&lx<65&&ly<34){int i=row*4+col;if(i<COMMON_COUNT)return BTN_COLOR_BASE+i;}}
    if(tab==1){if(y>=59&&y<89)return BTN_SLIDER_R;if(y>=95&&y<125)return BTN_SLIDER_G;if(y>=131&&y<161)return BTN_SLIDER_B;if(y>=166&&y<194){if(x<155)return BTN_APPLY;if(x>=165)return BTN_HEX;}}
    if(y>=204&&y<228&&x>=12&&x<308)return BTN_CLOSE; return NO_BUTTON;
}

static void activate(int b,Target t) {
    if(b==BTN_CREDITS_CLOSE){credits_open=false;play_ui_tap();}
    else if(b==BTN_CREDITS_EXIT){credits_open=false;menu_open=false;app_exit_requested=true;play_ui_tap();}
    else if(b==BTN_TAB_COMMON){tab=0;play_ui_tap();}
    else if(b==BTN_TAB_CUSTOM){tab=1;play_ui_tap();}
    else if(b==BTN_CLOSE){menu_open=false;play_ui_tap();}
    else if(b==BTN_APPLY){apply_color(custom_color,t);play_color_sound(t);}
    else if(b==BTN_HEX){play_ui_tap();keyboard();}
    else if(b>=BTN_COLOR_BASE&&b<BTN_COLOR_BASE+COMMON_COUNT){apply_color(common_colors[b-BTN_COLOR_BASE],t);play_color_sound(t);}
}

static void touch_update(u32 down,u32 held) {
    static int touch_button=NO_BUTTON; static bool touching=false; touchPosition p;
    if(down&KEY_TOUCH){
        hidTouchRead(&p);touch_button=hit(p.px,p.py);pressed_button=touch_button;pressed_target=target(held);touching=true;return;
    }
    if(!touching)return;
    if(held&KEY_TOUCH){
        hidTouchRead(&p);if(hit(p.px,p.py)!=touch_button){touch_button=NO_BUTTON;pressed_button=NO_BUTTON;}return;
    }
    if(touch_button!=NO_BUTTON)activate(touch_button,pressed_target);
    touch_button=NO_BUTTON;pressed_button=NO_BUTTON;touching=false;
}

static void update_slider(u32 held) {
    if(!(held&KEY_TOUCH)||pressed_button<BTN_SLIDER_R||pressed_button>BTN_SLIDER_B)return;
    touchPosition p;hidTouchRead(&p);int v=slider_v(p.px);
    if(pressed_button==BTN_SLIDER_R)custom_color.r=v;else if(pressed_button==BTN_SLIDER_G)custom_color.g=v;else custom_color.b=v;
}

int main(void) {
    gfxInitDefault();
    init_audio();

    Color green={0,255,0};top_color=green;bottom_color=green;custom_color=green;
    while(aptMainLoop()&&!app_exit_requested){
        hidScanInput();u32 kDown=hidKeysDown(),kHeld=hidKeysHeld();
        if(kHeld&KEY_SELECT){
            if(!select_held){select_tick=svcGetSystemTick();select_held=true;}
            else if(!credits_open&&svcGetSystemTick()-select_tick>=HOLD_TICKS){credits_open=true;menu_open=false;pressed_button=NO_BUTTON;play_ui_tap();}
        } else select_held=false;
        if(kHeld&KEY_START){
            if(!start_held){start_tick=svcGetSystemTick();start_held=true;}
            else if(!menu_open&&!credits_open&&svcGetSystemTick()-start_tick>=HOLD_TICKS){menu_open=true;tab=0;pressed_button=NO_BUTTON;play_ui_tap();}
        } else start_held=false;
        if(kDown&KEY_B){
            if(credits_open){credits_open=false;play_ui_tap();}
            else if(menu_open){menu_open=false;play_ui_tap();}
            else break;
        }

        if(credits_open)touch_update(kDown,kHeld);
        else if(menu_open){touch_update(kDown,kHeld);update_slider(kHeld);}

        Frame top=frame_get(GFX_TOP),bottom=frame_get(GFX_BOTTOM);
        clear_frame(&top,top_color);clear_frame(&bottom,bottom_color);
        if(credits_open)draw_credits(&bottom);else if(menu_open)draw_menu(&bottom);
        gfxFlushBuffers();gspWaitForVBlank();gfxSwapBuffers();
    }
    exit_audio();
    gfxExit();
    return 0;
}

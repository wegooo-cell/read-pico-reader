/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读采用默认灰阶表相序（厂家 2.0.76 起完整 GL48），等值保持由派生表保证。
 * English: Reading uses the default gray-table order (full GL48 since vendor 2.0.76)
 *         with equal pairs held by the derived table.
 * 用户2026-10-29：普通保留Moku GL37加六相白，速读所选DU加四相白并保留定稿。
 * User2026-10-29: ordinary retains Moku GL37 plus six whites; FastBW adds four whites to the selected DU and retains settling.
 */
#include "display_reader_waveform.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#include <stddef.h>
#include <string.h>
#if KIIKO_S31_FAST_READER_PANELS
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PANEL_LUT_ATTR EXT_RAM_BSS_ATTR
#else
#define PANEL_LUT_ATTR
#endif
#endif
const EpdWaveform* display_reader_text_waveform(void) {
    const EpdWaveform* panel37=display_reader_panel37_waveform();
    return panel37?panel37:&E0470_TEXTTURN_WAVEFORM;
}

const EpdWaveform* display_reader_fast_text_waveform(void) {
    enum { EXTRA_WHITE=4, CAPACITY=24 };
    static uint8_t data[CAPACITY][16][4];
    static EpdWaveformPhases phases;
    static const EpdWaveformPhases* ranges[]={&phases};
    static const EpdWaveformMode mode={.type=MODE_GL16,.temp_ranges=1,.range_data=ranges};
    static const EpdWaveformMode* modes[]={&mode};
    static EpdWaveform waveform;
    if(phases.luts)return &waveform;
#if KIIKO_S31_DU14
    const EpdWaveform *base=display_du14_white_waveform();
    const int expected=14;
#else
    const EpdWaveform *base=&E0470_FULL_WAVEFORM;
    const int expected=20;
#endif
    const EpdWaveformPhases* original=e0470_waveform_phases(base,MODE_DU);
    if(!original||!original->luts||original->phase_times||original->phases!=expected)return NULL;
    int hold=0;
    for(int f=original->phases-1;f>=0;--f){
        bool active=false;
        for(int to=0;to<16&&!active;++to)for(int from=0;from<16;++from)
            if(e0470_phase_action(original,f,to,from)){active=true;break;}
        if(active)break;
        ++hold;
    }
    const int drive=original->phases-hold;
    memcpy(data,original->luts,(size_t)drive*16*4);
    memcpy(data+drive+EXTRA_WHITE,original->luts+(size_t)drive*16*4,(size_t)hold*16*4);
    // 同一轮末尾只推目标白，包含未变化白底；黑字不补黑，最后仍按原表保持。
    // In the same scan push only white targets, including unchanged blanks; never reinforce black, and finish on the original holds.
    for(int f=drive;f<drive+EXTRA_WHITE;++f)memset(data[f][15],0xaa,4);
    phases=(EpdWaveformPhases){original->phases+EXTRA_WHITE,(const uint8_t*)data,NULL};
    waveform=*base;
    waveform.num_modes=1;waveform.mode_data=modes;return &waveform;
}

static inline uint8_t prepare_ui_byte(uint8_t value) {
    static const uint8_t tones[16]={0,0,0,5,5,5,5,5,10,10,10,10,10,10,10,15};
    return (uint8_t)(tones[value&15]|(tones[value>>4]<<4));
}
static bool prepare_target(uint8_t* target,EpdRect area,bool four_gray) {
    if(!target||area.width<=0||area.height<=0)return false;
    const int pw=epd_width(),ph=epd_height();
    if(pw<=0||ph<=0||(pw&1))return false;
    enum EpdRotation rotation=epd_get_rotation();
    bool portrait=rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT;
    int lw=portrait?ph:pw,lh=portrait?pw:ph;
    int64_t right=(int64_t)area.x+area.width,bottom=(int64_t)area.y+area.height;
    int x=area.x<0?0:area.x,y=area.y<0?0:area.y;
    if(right>lw)right=lw;
    if(bottom>lh)bottom=lh;
    if(right<=x||bottom<=y)return false;
    int w=(int)right-x,h=(int)bottom-y;EpdRect phys;
    switch(rotation) {
        case EPD_ROT_LANDSCAPE:phys=(EpdRect){x,y,w,h};break;
        case EPD_ROT_PORTRAIT:phys=(EpdRect){pw-y-h,x,h,w};break;
        case EPD_ROT_INVERTED_LANDSCAPE:phys=(EpdRect){pw-x-w,ph-y-h,w,h};break;
        case EPD_ROT_INVERTED_PORTRAIT:phys=(EpdRect){y,ph-x-w,h,w};break;
        default:return false;
    }
    // 只改目标矩形；UI的13/14底色留浅灰，不能沿用文字映射把它们吞成白色。
    // Change only the target crop; retain UI13/14 fills as light gray instead of text-style white collapse.
    for(int row=phys.y;row<phys.y+phys.height;++row) {
        uint8_t* p=target+(size_t)row*(pw/2);
        int col=phys.x,end=col+phys.width;
        if(col&1){uint8_t v=p[col/2];p[col++/2]=(uint8_t)((v&15)|(four_gray?(prepare_ui_byte(v)&0xf0):((v&0x80)?0xf0:0)));}
        for(;col+1<end;col+=2){uint8_t v=p[col/2];p[col/2]=four_gray?prepare_ui_byte(v):(uint8_t)(((v&8)?15:0)|((v&0x80)?0xf0:0));}
        if(col<end){uint8_t v=p[col/2];p[col/2]=(uint8_t)((v&0xf0)|(four_gray?(prepare_ui_byte(v)&15):((v&8)?15:0)));}
    }
    return true;
}

bool display_reader_fast_text_prepare(EpdiyHighlevelState* hl,EpdRect area) {
    if(!hl||!hl->front_fb||!hl->back_fb||hl->front_fb==hl->back_fb||!display_reader_fast_text_waveform())return false;
    return prepare_target(hl->front_fb,area,false);
}
bool display_reader_panel_gray4_prepare(uint8_t* target,EpdRect area) {
    return prepare_target(target,area,true);
}
bool display_reader_fast_panels_enabled(void) {
#if KIIKO_S31_FAST_READER_PANELS
    return true;
#else
    return false;
#endif
}
const EpdWaveform* display_reader_panel_gray4_waveform(bool erase_underlay) {
#if KIIKO_S31_FAST_READER_PANELS
    enum{FAST_PHASES=16,ERASE_WHITE=18,ENTRY_PHASES=26};
    static PANEL_LUT_ATTR uint8_t fast[FAST_PHASES][16][4],entry[ENTRY_PHASES][16][4];
    static const EpdWaveformPhases phases[]={
        {FAST_PHASES,(const uint8_t*)fast,NULL},{ENTRY_PHASES,(const uint8_t*)entry,NULL}};
    static const EpdWaveformPhases *ranges[][1]={{&phases[0]},{&phases[1]}};
    static const EpdWaveformMode mode[]={{MODE_GL16,1,ranges[0]},{MODE_GL16,1,ranges[1]}};
    static const EpdWaveformMode *modes[][1]={{&mode[0]},{&mode[1]}};
    static EpdWaveform wave[]={
        {.num_modes=1,.num_temp_ranges=1,.mode_data=modes[0]},
        {.num_modes=1,.num_temp_ranges=1,.mode_data=modes[1]}};
    static bool ready;
    if(!ready){
        const EpdWaveformPhases *follow=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,MODE_DU);
        const EpdWaveformPhases *native=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_DU);
        if(!follow||!follow->luts||follow->phase_times||follow->phases!=8)return NULL;
        if(!native||!native->luts||native->phase_times||native->phases!=20)return NULL;
        for(int f=0;f<20;++f)
            if(e0470_phase_action(native,f,15,0)!=(f<ERASE_WHITE?2:0))return NULL;
        int black=0,white=0,dark=0,light=0;
        for(int f=0;f<8;++f){
            black+=e0470_phase_action(follow,f,0,15)==1;
            white+=e0470_phase_action(follow,f,15,0)==2;
            dark+=e0470_phase_action(follow,f,5,15)==1;
            light+=e0470_phase_action(follow,f,10,15)==1;
        }
        if(black!=7||white!=8||dark!=5||light!=3)return NULL;
        // 初次进入把每个中灰目标统一擦白后再推灰，包含等灰旧字和白起点；同面板反馈仍16相。
        // Entry resets every midgray target, including equal-gray glyphs and white origins, before FOLLOW gray pushes; feedback stays16 phases.
        // 一轮最多18白+5黑+3保持，不加前置清屏或白到白维护，也不裁原厂表。
        // One scan contains at most18 white+5 black+3 holds, without a preclear, unchanged-white maintenance or raw-table trimming.
        for(int profile=0;profile<2;++profile){
            uint8_t (*data)[16][4]=profile?entry:fast;
            const int reset=profile?ERASE_WHITE:8;
            for(int from=0;from<16;++from)for(int to=0;to<=15;to+=5){
                bool gray=to==5||to==10;
                if(from==to&&!(profile&&gray))continue;
                int offset=gray&&(profile||from!=15)?reset:0;
                if(offset||to==15)for(int f=0;f<reset;++f){
                    int a=e0470_phase_action(profile?native:follow,f,15,0);
                    data[f][to][from/4]|=(uint8_t)(a<<(6-2*(from%4)));
                }
                if(to!=15)for(int f=0;f<8;++f){
                    int a=e0470_phase_action(follow,f,to,15);
                    data[offset+f][to][from/4]|=(uint8_t)(a<<(6-2*(from%4)));
                }
            }
        }
        wave[0].temp_intervals=wave[1].temp_intervals=E0470_FULL_WAVEFORM.temp_intervals;ready=true;
    }
    return &wave[erase_underlay?1:0];
#else
    (void)erase_underlay;
    return NULL;
#endif
}

/* ---- 普通翻页柔和差分 / Soft ordinary page turn ----
 * 普通原样保留Moku的GL37加六相目标白（包含等白），末尾保留原定稿保持。
 * GL48无全保持尾时额外保留三相释放；等黑/灰仍保持，不回退到重驱原表。
 * Ordinary retains Moku's GL37 plus six white-target phases including equal whites, before its original holds.
 * GL48 without trailing holds adds three release phases; equal black/grays still hold instead of falling back to the re-driving raw table.
 * 用户2026-11-01：普通翻页擦白由四相增至八相，仍保留原定稿保持。
 * User2026-11-01: ordinary-turn white erase grows from four to eight phases before the original holds.
 * 用户2026-11-03：对齐Pico普通翻页——十二相整屏擦白后再补四相重推黑，压住整屏白推对黑字的冲淡。
 * User2026-11-03: align ordinary turns with Pico -- twelve whole-panel white phases followed by four
 * black re-drive phases before the settle holds, so the white push does not wash out black text.
 */
#define READER_SOFT_TURN_WHITE 12
#define READER_SOFT_TURN_BLACK 4
#define READER_SOFT_TURN_EXTRA (READER_SOFT_TURN_WHITE + READER_SOFT_TURN_BLACK)
static uint8_t s_soft_turn[E0470_FULL_GL16_FRAMES + READER_SOFT_TURN_EXTRA + 3][16][4];
static uint8_t s_text_turn[E0470_FULL_GL16_FRAMES + READER_SOFT_TURN_EXTRA + 3][16][4];
static EpdWaveformPhases s_soft_turn_phases;
static EpdWaveformPhases s_text_turn_phases;
static const EpdWaveformPhases* s_soft_turn_ranges[1];
static const EpdWaveformPhases* s_text_turn_ranges[1];
static EpdWaveformMode s_soft_turn_mode;
static EpdWaveformMode s_text_turn_mode;
static const EpdWaveformMode* s_soft_turn_modes[1];
static const EpdWaveformMode* s_text_turn_modes[1];
static EpdWaveform s_soft_turn_waveform;
static EpdWaveform s_text_turn_waveform;
static bool s_soft_turn_ready;
static bool s_text_turn_ready;

static void turn_set(uint8_t (*lut)[16][4],int phase,int to,int from,int action){
    const int shift=6-2*(from%4);
    uint8_t *p=&lut[phase][to][from/4];
    *p=(uint8_t)((*p&~(3u<<shift))|((unsigned)action<<shift));
}

static int turn_get(uint8_t (*lut)[16][4],int phase,int to,int from){
    return (lut[phase][to][from/4]>>(6-2*(from%4)))&3;
}

// 用户2026-11-04：文字页改回“白侧直接推灰”。灰阶标定页实测0.40档与原厂48相肉眼一致：
// 白起点的中灰直接压 round((15-灰阶)×0.40) 相（1~6相），去掉原厂“压满黑→尾段擦白回推”，
// 灰边与黑白端点同段出现，不再等尾段灰阶脉冲；其他起点仍用原厂序列。
// 插图页仍用原厂灰阶表（见display_reader_soft_turn_waveform），避免直推压坏灰阶图。
// User2026-11-04: text pages go back to the white-side gray push. The calibration page showed the
// 0.40 profile matching the vendor 48-phase reference by eye: white-origin midgrays darken directly
// with round((15-gray)*0.40) phases (1..6), dropping the vendor full-black-then-tail-erase so gray
// edges appear with the black/white endpoints; all other origins keep the vendor sequence. Image
// pages keep the vendor table via display_reader_soft_turn_waveform, so the push cannot flatten
// illustration tone.
#define READER_GRAY_DARKEN_SCALE 0.40f
static void turn_white_to_gray(uint8_t (*lut)[16][4],int phases){
    for(int v=1;v<15;++v){
        int first=-1;
        for(int f=0;f<phases;++f)
            if(turn_get(lut,f,v,15)==1){first=f;break;}
        if(first<0)continue;
        int pulses=(int)((15-v)*READER_GRAY_DARKEN_SCALE+0.5f);
        if(pulses<1)pulses=1;
        if(pulses>15)pulses=15;
        for(int f=0;f<phases;++f){
            if(turn_get(lut,f,v,15)==0)continue;
            turn_set(lut,f,v,15,f>=first&&f<first+pulses?1:0);
        }
    }
}

static const EpdWaveform* turn_waveform(bool gray_from_white){
    bool *ready=gray_from_white?&s_text_turn_ready:&s_soft_turn_ready;
    if(*ready)return gray_from_white?&s_text_turn_waveform:&s_soft_turn_waveform;
    const EpdWaveform *base=&E0470_WAVEFORM;
    const EpdWaveform *panel37=display_reader_panel37_waveform();
    if(panel37)base=panel37;
    const EpdWaveformPhases *gl=e0470_waveform_phases(base,MODE_GL16);
    uint8_t (*lut)[16][4]=gray_from_white?s_text_turn:s_soft_turn;
    if(!gl||!gl->luts||gl->phase_times||gl->phases+READER_SOFT_TURN_EXTRA>
        (int)(sizeof(s_soft_turn)/sizeof(s_soft_turn[0])))return &E0470_WAVEFORM;
    int hold=0;
    while(hold<gl->phases){
        const int f=gl->phases-1-hold;
        int nonzero=0;
        for(int to=0;to<16&&!nonzero;++to)
            for(int from=0;from<16;++from)
                if(e0470_phase_action(gl,f,to,from)){nonzero=1;break;}
        if(nonzero)break;
        ++hold;
    }
    if(hold<1)return &E0470_WAVEFORM;
    const int drive=gl->phases-hold;
    // 官方全像素时序 + 等黑去掉擦白 + “变成白/本来白”末尾补十二相擦白（插在定稿保持前），
    // 随后四相重推黑，抵消整屏白推对黑字的冲淡。
    // Official full-pixel timing, equal black drops its erase phases, and twelve extra white
    // phases (for every white target, blanks included) are inserted before the settle holds;
    // four black re-drive phases then darken every black target after the whole-panel white push.
    memcpy(lut,gl->luts,(size_t)drive*16*4);
    if(gray_from_white)turn_white_to_gray(lut,drive);
    memset(lut+drive,0,(size_t)READER_SOFT_TURN_EXTRA*16*4);
    memcpy(lut+drive+READER_SOFT_TURN_EXTRA,(const uint8_t*)gl->luts+(size_t)drive*16*4,
           (size_t)hold*16*4);
    for(int f=0;f<drive;++f)
        if(e0470_phase_action(gl,f,0,0)==2)turn_set(lut,f,0,0,0);
    for(int i=0;i<READER_SOFT_TURN_WHITE;++i)
        for(int from=0;from<16;++from)
            turn_set(lut,drive+i,15,from,2);
    for(int i=0;i<READER_SOFT_TURN_BLACK;++i)
        for(int from=0;from<16;++from)
            turn_set(lut,drive+READER_SOFT_TURN_WHITE+i,0,from,1);
    EpdWaveformPhases *phases=gray_from_white?&s_text_turn_phases:&s_soft_turn_phases;
    const EpdWaveformPhases **ranges=gray_from_white?s_text_turn_ranges:s_soft_turn_ranges;
    EpdWaveformMode *mode=gray_from_white?&s_text_turn_mode:&s_soft_turn_mode;
    const EpdWaveformMode **modes=gray_from_white?s_text_turn_modes:s_soft_turn_modes;
    EpdWaveform *out=gray_from_white?&s_text_turn_waveform:&s_soft_turn_waveform;
    *phases=(EpdWaveformPhases){gl->phases+READER_SOFT_TURN_EXTRA,(const uint8_t*)lut,NULL};
    ranges[0]=phases;
    *mode=(EpdWaveformMode){MODE_GL16,1,ranges};
    modes[0]=mode;
    *out=(EpdWaveform){.num_modes=1,.num_temp_ranges=1,.mode_data=modes,
                       .temp_intervals=base->temp_intervals};
    *ready=true;
    return out;
}

const EpdWaveform* display_reader_soft_turn_waveform(void){ return turn_waveform(false); }
const EpdWaveform* display_reader_text_turn_waveform(void){ return turn_waveform(true); }

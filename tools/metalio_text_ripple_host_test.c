/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：实际实验生成器/量化器与真实错相引擎；仅扫描、分配和时钟为宿主替身。
 * English: Real experimental generator/quantizer and stagger engine; host stubs own scan, allocation and clocks only.
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "metalio_text_ripple.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include "e0470_page_turn.h"
#ifdef KIIKO_TEST_MAIN_SWITCH_ERASE
#include "display_main_waveform.h"
#endif

enum {W=1216,H=684,N=W*H/2};
static uint8_t front[N], back[N], saved_front[N], saved_back[N], difference[W*H];
static bool lines[H];
static uint8_t columns[W/2];
static enum EpdRotation rotation;
static const uint8_t* const* staged_luts;
static const int8_t* staged_lines;
static const int8_t* staged_bands;
static const int *staged_x0, *staged_x1;
static int staged_count, scans, fail_at, scan_us=15000;
static int64_t now;
static unsigned phase_hits[64];
static int test_phases=22,test_bands=10,test_skip;
static bool test_paired_tail;
static e0470_turn_dir_t test_direction;
static EpdRect test_area;
static unsigned band_hits[8][64];
static void physical_to_logical(int px,int py,int* x,int* y);
static int expected_phase(int band) {
    // 固定预期时间轴，不复用实现的索引公式。/ Pin the expected timeline without reusing the implementation's index formula.
    static const int launch[8]={0,1,2,3,4,5,6,6};
    return scans+test_skip-(test_paired_tail?launch[band]:band);
}
int water_test_alloc_fail;

int epd_width(void) {return W;}
int epd_height(void) {return H;}
enum EpdRotation epd_get_rotation(void) {return rotation;}
EpdRect epd_full_screen(void) {return (EpdRect){0,0,W,H};}
void epd_poweron(void) {}
int64_t esp_timer_get_time(void) {return now;}
void esp_rom_delay_us(uint32_t us) {now+=us;}
void epd_clear_phase_luts(void) {staged_luts=NULL;staged_lines=NULL;staged_bands=NULL;staged_count=0;}
void epd_set_line_phase_luts(const uint8_t*const* lut,const int8_t* phase) {
    epd_clear_phase_luts();staged_luts=lut;staged_lines=phase;
}
void epd_set_col_phase_luts(const uint8_t*const* lut,const int* x0,const int* x1,const int8_t* phase,int count) {
    epd_clear_phase_luts();staged_luts=lut;staged_x0=x0;staged_x1=x1;staged_bands=phase;staged_count=count;
}
void epd_build_1ppB_lut_1k(uint8_t* lut,const EpdWaveformPhases* phase,int frame) {
    assert(phase->phases==test_phases && frame>=0 && frame<test_phases);memset(lut,frame,1024);
}
EpdRect epd_difference_image_cropped(const uint8_t* to,const uint8_t* from,EpdRect area,uint8_t* diff,bool* dirty,uint8_t* cols) {
    assert(to==front&&from==back&&diff==difference&&dirty==lines&&cols==columns);
    return area;
}
enum EpdDrawError epd_draw_base(EpdRect area,const uint8_t* data,EpdRect crop,enum EpdDrawMode mode,
 int temperature,const bool* dirty,const uint8_t* cols,const EpdWaveform* wave) {
    assert(area.width==W&&crop.height==H&&data==difference&&temperature==25);
    assert(dirty==lines&&cols==columns&&wave==&E0470_APPLY_WAVEFORM);
    assert(mode==(MODE_PACKING_1PPB_DIFFERENCE|MODE_DU));
    assert(staged_luts&&((staged_lines!=NULL)!=(staged_bands!=NULL)));
    bool seen[64]={0},seen_band[8]={0};
    for(int b=0;b<(staged_lines?H:staged_count);++b) {
        int phase=staged_lines?staged_lines[b]:staged_bands[b];
        if(test_paired_tail) {
            int band=b;
            if(staged_lines) {
                int lx,ly;physical_to_logical(0,b,&lx,&ly);
                const bool lr=test_direction<=E0470_TURN_RTL;
                int coord=lr?lx:ly,origin=lr?test_area.x:test_area.y;
                int span=lr?test_area.width:test_area.height;
                if(coord<origin||coord>=origin+span){assert(phase<0&&!dirty[b]);continue;}
                band=(coord-origin)*test_bands/span;
                if(test_direction==E0470_TURN_RTL||test_direction==E0470_TURN_BTT)band=test_bands-1-band;
            }
            assert(band>=0&&band<8);
            int expected=expected_phase(band);
            if(expected<0||expected>=test_phases)expected=-1;
            assert(phase==expected);
            if(phase>=0)seen_band[band]=true;
        }
        if(phase<0)continue;
        assert(phase<test_phases&&staged_luts[phase][0]==phase);
        if(test_bands==1)assert(phase==test_skip+scans);
        if(!staged_lines)assert(staged_x0[b]>=0&&staged_x0[b]<staged_x1[b]&&staged_x1[b]<=W);
        seen[phase]=true;
    }
    int active=0;
    for(int p=0;p<test_phases;++p){phase_hits[p]+=seen[p];active+=seen[p];}
    bool expected_seen[64]={0};
    for(int b=0;b<test_bands;++b){int p=expected_phase(b);if(p>=0&&p<test_phases)expected_seen[p]=true;}
    int expected=0;for(int p=0;p<test_phases;++p){expected+=expected_seen[p];assert(seen[p]==expected_seen[p]);}
    assert(active==expected);
    if(test_paired_tail)for(int b=0;b<8;++b)if(seen_band[b]){
        int p=expected_phase(b);assert(!band_hits[b][p]);++band_hits[b][p];
    }
    ++scans;now+=scan_us;
    return scans==fail_at?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS;
}
enum EpdDrawError epd_draw_base_from_phase(EpdRect area,const uint8_t* data,EpdRect crop,
    enum EpdDrawMode mode,int temperature,const bool* dirty,const uint8_t* cols,
    const EpdWaveform* wave,int first) {
    assert(area.width==W&&crop.height==H&&data==difference&&temperature==25);
    assert(mode==(MODE_PACKING_1PPB_DIFFERENCE|MODE_GL16)&&wave==&E0470_TEXTTURN_WAVEFORM);
    assert(dirty==lines&&cols==columns&&first==test_skip&&!staged_luts);
    for(int phase=first;phase<48;++phase){
        ++phase_hits[phase];++scans;now+=scan_us;
        if(scans==fail_at)return EPD_DRAW_EMPTY_LINE_QUEUE;
    }
    return EPD_DRAW_SUCCESS;
}
static void physical_to_logical(int px,int py,int* x,int* y) {
    switch(rotation) {
        case EPD_ROT_LANDSCAPE:*x=px;*y=py;break;
        case EPD_ROT_PORTRAIT:*x=py;*y=W-1-px;break;
        case EPD_ROT_INVERTED_LANDSCAPE:*x=W-1-px;*y=H-1-py;break;
        default:*x=H-1-py;*y=px;break;
    }
}
static int pixel(const uint8_t* fb,int x,int y) {return(fb[(size_t)y*W/2+x/2]>>((x&1)*4))&15;}
static void fill(void) {
    for(size_t i=0;i<N;++i){front[i]=(uint8_t)(i*73+19);back[i]=(uint8_t)(i*29+71);}
    memcpy(saved_front,front,N);memcpy(saved_back,back,N);
}
static void assert_crop(EpdRect area) {
    for(int py=0;py<H;++py)for(int px=0;px<W;++px) {
        int x,y;physical_to_logical(px,py,&x,&y);
        bool in=x>=area.x&&y>=area.y&&(int64_t)x<(int64_t)area.x+area.width&&(int64_t)y<(int64_t)area.y+area.height;
        int old=pixel(saved_front,px,py),expected=in?((old+2)/5)*5:old;
        assert(pixel(front,px,py)==expected);
    }
    assert(!memcmp(back,saved_back,N));
}
static void reader_tail_test(EpdiyHighlevelState* hl,const EpdWaveform* waveform,int phases) {
    test_paired_tail=true;test_bands=8;test_phases=phases;test_skip=0;
    for(int rot=0;rot<4;++rot)for(int dir=0;dir<4;++dir)for(int crop=0;crop<2;++crop){
        rotation=(enum EpdRotation)rot;test_direction=(e0470_turn_dir_t)dir;
        int lw=rot&1?H:W,lh=rot&1?W:H;
        test_area=crop?(EpdRect){17,33,lw-35,lh-71}:(EpdRect){0,0,lw,lh};
        fill();scans=fail_at=0;now=0;scan_us=15000;memset(band_hits,0,sizeof(band_hits));
        assert(e0470_reader_ripple_with_waveform(hl,test_area,test_direction,waveform,8)==EPD_DRAW_SUCCESS);
        assert(scans==phases+6&&now==(phases+6)*15000&&!staged_luts);
        assert(!memcmp(front,saved_front,N));
        for(int b=0;b<8;++b)for(int p=0;p<phases;++p)assert(band_hits[b][p]==1);
        for(int py=0;py<H;++py)for(int px=0;px<W;++px){int x,y;physical_to_logical(px,py,&x,&y);
            bool in=x>=test_area.x&&y>=test_area.y&&x<test_area.x+test_area.width&&y<test_area.y+test_area.height;
            assert(pixel(back,px,py)==pixel(in?front:saved_back,px,py));}
    }
    rotation=EPD_ROT_INVERTED_PORTRAIT;test_direction=E0470_TURN_RTL;test_area=(EpdRect){0,0,H,W};
    for(int failure=1;failure<=phases+6;++failure){
        fill();scans=0;fail_at=failure;memset(band_hits,0,sizeof(band_hits));
        assert(e0470_reader_ripple_with_waveform(hl,test_area,test_direction,waveform,8)==EPD_DRAW_EMPTY_LINE_QUEUE);
        assert(scans==failure&&!staged_luts&&!memcmp(back,saved_back,N));
    }
    fill();scans=fail_at=0;scan_us=18000;now=0;memset(band_hits,0,sizeof(band_hits));
    assert(e0470_reader_ripple_with_waveform(hl,test_area,test_direction,waveform,8)==EPD_DRAW_SUCCESS);
    assert(scans==phases+6&&now==(phases+6)*18000);
    e0470_page_turn_release();water_test_alloc_fail=1;fill();scans=0;
    assert(e0470_reader_ripple_with_waveform(hl,test_area,test_direction,waveform,8)==EPD_DRAW_NO_PHASES_AVAILABLE);
    assert(!scans&&!memcmp(back,saved_back,N));water_test_alloc_fail=0;
    test_paired_tail=false;
    printf("PASS: reader tail %d phases/eight bands/%d scans, pinned per-band timeline, four rotations/directions/odd crops, all failure ticks, slow scan and OOM\n",phases,phases+6);
}
int main(void) {
    EpdiyHighlevelState hl={.front_fb=front,.back_fb=back,.difference_fb=difference,.dirty_lines=lines,.dirty_columns=columns};
    e0470_waveform_init();
#ifdef KIIKO_TEST_MAIN_SWITCH_ERASE
    // Exercise the real compact main-page engine with50-gray/22-BW phases, not only LUT generation.
    const EpdWaveform *switch_sources[]={display_main_water_waveform(),display_main_bw_turn_waveform(),display_main_switch_waveform(display_main_normal_waveform(),MODE_GL16)};
    for(int kind=0;kind<3;++kind){
        const EpdWaveform *wave=display_main_switch_waveform(switch_sources[kind],MODE_GL16);
        test_phases=kind==2?52:kind?22:50;test_bands=8;test_skip=0;test_paired_tail=false;
#if KIIKO_S31_MAIN_CLEAN
        #ifndef KIIKO_TEST_WHITE_PHASES
#define KIIKO_TEST_WHITE_PHASES 6
#endif
        #ifdef KIIKO_TEST_ROOT_BACKGROUND
        wave=display_main_background_waveform_with_phases(wave,MODE_GL16,KIIKO_TEST_WHITE_PHASES);
#else
        wave=display_main_clean_waveform_with_phases(wave,MODE_GL16,KIIKO_TEST_WHITE_PHASES);
#endif
        test_phases+=KIIKO_TEST_WHITE_PHASES+2;
#endif
        assert(e0470_waveform_phases(wave,MODE_GL16)->phases==test_phases);
        for(int rot=0;rot<4;++rot)for(int dir=0;dir<4;++dir){
            rotation=(enum EpdRotation)rot;fill();scans=fail_at=0;now=0;scan_us=15000;
            memset(phase_hits,0,sizeof(phase_hits));
            EpdRect area={0,0,rot&1?H:W,(rot&1?W:H)-128};
            assert(e0470_page_turn_with_waveform_compact(&hl,area,(e0470_turn_dir_t)dir,wave,8)==EPD_DRAW_SUCCESS);
            assert(scans==test_phases+7&&now==scans*scan_us&&!staged_luts);
            for(int p=0;p<test_phases;++p)assert(phase_hits[p]==8);
        }
        rotation=EPD_ROT_INVERTED_PORTRAIT;
        for(int failure=1;failure<=test_phases+7;++failure){
            fill();scans=0;fail_at=failure;
            assert(e0470_page_turn_with_waveform_compact(&hl,(EpdRect){0,0,H,W-128},E0470_TURN_RTL,wave,8)==EPD_DRAW_EMPTY_LINE_QUEUE);
            assert(scans==failure&&!staged_luts&&!memcmp(back,saved_back,N));
        }
        e0470_page_turn_release();
    }
    puts("PASS: actual main-band engine,8 bands,all selected phases per band,4 rotations/directions,all failure ticks preserve real history");
    return 0;
#endif
    uint8_t vendor[3][48*64];const int modes[]={MODE_GL16,MODE_GC16,MODE_DU};
    for(int i=0;i<3;++i){const EpdWaveformPhases*p=e0470_waveform_phases(&E0470_WAVEFORM,modes[i]);memcpy(vendor[i],p->luts,(size_t)p->phases*64);}
#if KIIKO_S31_TEXT_RIPPLE
    assert(display_reader_fast_ripple_enabled());
    const EpdWaveform*wave=metalio_text_ripple_waveform();
    const EpdWaveformPhases*p=e0470_waveform_phases(wave,MODE_GL16);
    assert(wave!=&E0470_TEXTTURN_WAVEFORM&&p&&p->phases==22&&!p->phase_times);
    unsigned checked=0;
    for(int from=0;from<16;++from)for(int to=0;to<16;++to) {
        int black=0,white=0;
        for(int f=0;f<22;++f){int a=e0470_phase_action(p,f,to,from);assert(a>=0&&a<=2);black+=a==1;white+=a==2;++checked;
            if(to!=0&&to!=5&&to!=10&&to!=15)assert(a==0);
            if(to==from)assert(a==0);
        }
        if(to==0&&from!=0)assert(black==18&&!white);
        if(to==15&&from!=15)assert(white==18&&!black);
        if(to==15&&from==15)assert(!white&&!black);
        assert(black<=18&&white<=18);
    }
    assert(!metalio_text_ripple_prepare(NULL,(EpdRect){0,0,W,H}));
    for(int rot=0;rot<4;++rot) {
        rotation=(enum EpdRotation)rot;int lw=rot&1?H:W,lh=rot&1?W:H;
        const EpdRect areas[]={{0,0,lw,lh},{3,17,lw-8,lh-150},{-7,-11,39,43},{lw-5,lh-7,INT_MAX,INT_MAX},{11,21,1,1}};
        for(unsigned a=0;a<sizeof(areas)/sizeof(areas[0]);++a){fill();assert(metalio_text_ripple_prepare(&hl,areas[a]));assert_crop(areas[a]);}
        fill();assert(!metalio_text_ripple_prepare(&hl,(EpdRect){INT_MAX,INT_MAX,INT_MAX,INT_MAX}));assert(!memcmp(front,saved_front,N));
        for(int dir=0;dir<4;++dir){
            EpdRect area={0,0,lw,lh-128};fill();assert(metalio_text_ripple_prepare(&hl,area));
            memset(phase_hits,0,sizeof(phase_hits));scans=fail_at=0;now=0;
            assert(e0470_page_turn_with_waveform_compact(&hl,area,(e0470_turn_dir_t)dir,wave,10)==EPD_DRAW_SUCCESS);
            assert(scans==31&&now==31*scan_us&&!staged_luts);
            for(int phase=0;phase<22;++phase)assert(phase_hits[phase]==10);
            for(int py=0;py<H;++py)for(int px=0;px<W;++px){int x,y;physical_to_logical(px,py,&x,&y);bool in=x<area.width&&y<area.height;
                assert(pixel(back,px,py)==pixel(in?front:saved_back,px,py));}
        }
    }
    rotation=EPD_ROT_PORTRAIT;
    for(int failure=1;failure<=31;++failure){
        fill();assert(metalio_text_ripple_prepare(&hl,(EpdRect){0,0,H,W}));
        fail_at=failure;scans=0;
        assert(e0470_page_turn_with_waveform_compact(&hl,(EpdRect){0,0,H,W},E0470_TURN_RTL,wave,10)==EPD_DRAW_EMPTY_LINE_QUEUE);
        assert(scans==failure&&!staged_luts&&!memcmp(back,saved_back,N));
    }
    fill();fail_at=scans=0;scan_us=18000;now=0;
    assert(metalio_text_ripple_prepare(&hl,(EpdRect){0,0,H,W}));
    assert(e0470_page_turn_with_waveform_compact(&hl,(EpdRect){0,0,H,W},E0470_TURN_LTR,wave,10)==EPD_DRAW_SUCCESS);
    assert(scans==31&&now==31*18000);
    e0470_page_turn_release();water_test_alloc_fail=1;fill();scans=0;
    assert(e0470_page_turn_with_waveform_compact(&hl,(EpdRect){0,0,H,W},E0470_TURN_RTL,wave,10)==EPD_DRAW_NO_PHASES_AVAILABLE);
    assert(!scans&&!memcmp(back,saved_back,N));water_test_alloc_fail=0;
    reader_tail_test(&hl,wave,22);
    const EpdWaveform* clean=metalio_text_ripple_clean_waveform();
    const EpdWaveformPhases* cp=e0470_waveform_phases(clean,MODE_GL16);assert(cp&&cp->phases==28);
    for(int f=0;f<28;++f)for(int to=0;to<16;++to)for(int from=0;from<16;++from){
        int want=f<18?e0470_phase_action(p,f,to,from):f<24?(to==15?2:0):0;
        if (to == 15 && from == 15) want = f < 6 ? 2 : 0;
        assert(e0470_phase_action(cp,f,to,from)==want);
    }
    reader_tail_test(&hl,clean,28);
    metalio_text_ripple_disable();fill();assert(!display_reader_fast_ripple_enabled()&&!metalio_text_ripple_waveform());
    assert(!metalio_text_ripple_prepare(&hl,(EpdRect){0,0,H,W})&&!memcmp(front,saved_front,N));
    printf("PASS: %u action cells; four rotations/crops,22 phases/10 bands/31 ticks,31 scan failures,slow scan,OOM and boot-local fallback\n",checked);
#else
    fill();assert(!display_reader_fast_ripple_enabled()&&!metalio_text_ripple_waveform());
    assert(!metalio_text_ripple_prepare(&hl,(EpdRect){0,0,W,H})&&!memcmp(front,saved_front,N)&&!memcmp(back,saved_back,N));
    puts("PASS: regular build disables experimental quantization/waveform");
#endif
    for(int i=0;i<3;++i){const EpdWaveformPhases*p=e0470_waveform_phases(&E0470_WAVEFORM,modes[i]);assert(!memcmp(vendor[i],p->luts,(size_t)p->phases*64));}
    // 普通翻页逐迁移验证：删除的每一相都无驱动，余下相序/脉冲一项不少，灰阶不量化。
    // Check every ordinary transition: discarded phases have no drive; remaining phases/pulses stay ordered and targets remain unquantized.
    const EpdWaveformPhases*normal=e0470_waveform_phases(&E0470_TEXTTURN_WAVEFORM,MODE_GL16);
    test_phases=48;test_bands=1;scan_us=15000;water_test_alloc_fail=0;
    rotation=EPD_ROT_PORTRAIT;
    for(int from=0;from<16;++from)for(int to=0;to<16;++to){
        int first=0;while(first<48&&!e0470_phase_action(normal,first,to,from))++first;
        test_skip=first==48?47:first?first-1:0;
        memset(front,to|(to<<4),N);memset(back,from|(from<<4),N);
        scans=fail_at=0;now=0;memset(phase_hits,0,sizeof(phase_hits));
        assert(e0470_text_turn_with_waveform(&hl,(EpdRect){0,0,H,W},E0470_TURN_RTL,&E0470_TEXTTURN_WAVEFORM)==EPD_DRAW_SUCCESS);
        assert(scans==48-test_skip&&now==scans*scan_us&&!memcmp(front,back,N));
        for(int f=0;f<48;++f){assert(phase_hits[f]==(unsigned)(f>=test_skip));if(f<test_skip)assert(!e0470_phase_action(normal,f,to,from));}
    }
    for(int rot=0;rot<4;++rot){
        rotation=(enum EpdRotation)rot;
        memset(front,0,N);memset(back,0xff,N);memcpy(saved_back,back,N);
        int first=0;while(first<48&&!e0470_phase_action(normal,first,0,15))++first;
        test_skip=first?first-1:0;
        EpdRect area={0,0,rot&1?H:W,rot&1?W:H};
        for(int failure=1;failure<=48-test_skip;++failure){
            memcpy(back,saved_back,N);fail_at=failure;scans=0;
            assert(e0470_text_turn_with_waveform(&hl,area,E0470_TURN_RTL,&E0470_TEXTTURN_WAVEFORM)==EPD_DRAW_EMPTY_LINE_QUEUE);
            assert(scans==failure&&!memcmp(back,saved_back,N)&&!staged_luts);
        }
    }
    reader_tail_test(&hl,&E0470_TEXTTURN_WAVEFORM,48);
    e0470_page_turn_release();
    puts("PASS: ordinary16-level GL48,all256 transitions keep every active/tail phase,one setup hold,four rotations and every failure preserve history");
    return 0;
}

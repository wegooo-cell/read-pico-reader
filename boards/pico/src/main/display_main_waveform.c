/* SPDX-License-Identifier: Apache-2.0
 * 中文：主页保持等灰；退出书架只向白色加强擦除。柔和水波纹端点单向驱动，中灰保留原序列，不改厂商表和扫描时序。
 * English: Main pages hold equal grays; shelf exit strengthens whitening only toward white. Soft water uses directional endpoints and original intermediate-gray sequences without editing vendor tables or scan timing.
 * 用户修订：快刷亚克力保留校准GL16，移除偏深且慢的试验灰阶；保留同向擦白用于书架分页及退出。
 * User revision: fast acrylic retains calibrated GL16; remove the dark, slow experimental gray drive and keep directional whitening for shelf paging/exit.
 */
#include "display_main_waveform.h"
#include "display_waveform_smooth.h"
#include "esp_heap_caps.h"
#include "app_config.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

// 用户2026-10-29：涉及书架的根页切换补八相，其余补六相，插在原保持前；等灰保持。
// Root switches involving the shelf add eight changed-white phases; other roots add six before native settling.
const EpdWaveform* display_main_clean_waveform_with_phases(
    const EpdWaveform* source, enum EpdDrawMode draw_mode, unsigned white_phases
) {
#if KIIKO_PICO_MAIN_CLEAN
    enum { SLOTS=12, MAX_SOURCE=37 };
    typedef struct {
        const EpdWaveform *source;
        int mode;
        unsigned white_phases;
        EpdWaveformPhases phases;
        const EpdWaveformPhases *range;
        EpdWaveformMode profile;
        const EpdWaveformMode *profile_ptr;
        EpdWaveform wave;
    } slot_t;
    static slot_t slots[SLOTS];
    const int mode=draw_mode&0x3f;
    if(!source || (mode!=MODE_DU && mode!=MODE_GL16) ||
       (white_phases!=6 && white_phases!=8))return source;
    // Rebase a cached result before choosing another strength; never stack cleanup phases.
    for(unsigned i=0;i<SLOTS;++i)
        if(slots[i].source && &slots[i].wave==source && slots[i].mode==mode){source=slots[i].source;break;}
    for(unsigned i=0;i<SLOTS;++i){
        if(slots[i].source==source && slots[i].mode==mode &&
           slots[i].white_phases==white_phases)return &slots[i].wave;
    }
    const EpdWaveformPhases *original=e0470_waveform_phases(source,mode);
    if(!original || !original->luts || original->phase_times || original->phases<20 || original->phases>MAX_SOURCE ||
       source->num_temp_ranges!=1 || !source->temp_intervals)return source;
    unsigned index=0;while(index<SLOTS && slots[index].source)++index;
    if(index==SLOTS)return source;
    // Equal pixels are held, so only changed-pixel actions determine the native settling tail.
    int holds=0;
    for(int f=original->phases-1;f>=0&&holds<3;--f){
        bool empty=true;
        for(int to=0;to<16;++to)for(int from=0;from<16;++from)
            if(to!=from&&e0470_phase_action(original,f,to,from))empty=false;
        if(!empty)break;
        ++holds;
    }
    const int release=holds?0:1;
    const int white=(int)white_phases;
    const int count=original->phases+white+release;
    if(count>45)return source;
    uint8_t (*data)[16][4]=heap_caps_malloc((size_t)count*16*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!data)return source;
    const int drive=original->phases-holds;
    memcpy(data,original->luts,(size_t)drive*64);
    memset(data+drive,0,(size_t)white*64);
    memcpy(data+drive+white,original->luts+(size_t)drive*64,(size_t)holds*64);
    if(release)memset(data+count-1,0,64);
    for(int f=0;f<count;++f)
        for(int gray=0;gray<16;++gray)
            data[f][gray][gray/4]&=(uint8_t)~(3u<<(6-2*(gray%4)));
    for(int f=drive;f<drive+white;++f)
        for(int from=0;from<15;++from)data[f][15][from/4]|=(uint8_t)(2u<<(6-2*(from%4)));
    slot_t *slot=&slots[index];slot->mode=mode;slot->white_phases=white_phases;
    slot->phases=(EpdWaveformPhases){count,(const uint8_t*)data,NULL};slot->range=&slot->phases;
    slot->profile=(EpdWaveformMode){mode,1,&slot->range};slot->profile_ptr=&slot->profile;
    slot->wave=(EpdWaveform){.num_modes=1,.num_temp_ranges=1,.mode_data=&slot->profile_ptr,.temp_intervals=source->temp_intervals};
    slot->source=source;return &slot->wave;
#else
    (void)draw_mode;(void)white_phases;return source;
#endif
}

const EpdWaveform* display_main_clean_waveform(const EpdWaveform* source, enum EpdDrawMode mode) {
    return display_main_clean_waveform_with_phases(source,mode,6);
}

static uint8_t s_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_data, NULL};
static const EpdWaveformPhases* s_ranges[] = {&s_phases};
static const EpdWaveformMode s_mode = {5, 1, s_ranges};
static const EpdWaveformMode* s_modes[] = {&s_mode};
static EpdWaveform s_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_modes};
static bool s_ready;
#define MAIN_ERASE_EXTRA_FRAMES 4
#define MAIN_EXIT_FRAMES (E0470_FULL_DU_FRAMES + MAIN_ERASE_EXTRA_FRAMES)
static uint8_t s_exit_data[MAIN_EXIT_FRAMES][16][4];
static const EpdWaveformPhases s_exit_phases = {MAIN_EXIT_FRAMES, (const uint8_t*)s_exit_data, NULL};
static const EpdWaveformPhases* s_exit_ranges[] = {&s_exit_phases};
static const EpdWaveformMode s_exit_mode = {1, 1, s_exit_ranges};
static const EpdWaveformMode* s_exit_modes[] = {&s_exit_mode};
static EpdWaveform s_exit_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_exit_modes};
static bool s_exit_ready;
static uint8_t s_water_data[E0470_GL16_FRAMES][16][4];
static const EpdWaveformPhases s_water_phases = {E0470_GL16_FRAMES, (const uint8_t*)s_water_data, NULL};
static const EpdWaveformPhases* s_water_ranges[] = {&s_water_phases};
static const EpdWaveformMode s_water_mode = {5, 1, s_water_ranges};
static const EpdWaveformMode* s_water_modes[] = {&s_water_mode};
static EpdWaveform s_water_waveform = {.num_modes=1, .num_temp_ranges=1, .mode_data=s_water_modes};
static bool s_water_ready;
static uint8_t s_bw_data[E0470_FULL_DU_FRAMES][16][4];
static const EpdWaveformPhases s_bw_phases={E0470_FULL_DU_FRAMES,(const uint8_t*)s_bw_data,NULL};
static const EpdWaveformPhases* s_bw_ranges[]={&s_bw_phases};
static const EpdWaveformMode s_bw_mode={1,1,s_bw_ranges};
static const EpdWaveformMode* s_bw_modes[]={&s_bw_mode};
static EpdWaveform s_bw_waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=s_bw_modes};
static bool s_bw_ready;
_Static_assert(MAIN_EXIT_FRAMES <= E0470_GL16_FRAMES, "soft water must fit endpoint cleanup");

static int endpoint_action(const EpdWaveformPhases* bw, int f, int to, int from) {
    if (to == from) return 0;
    if (f < bw->phases) return e0470_phase_action(bw, f, to, from);
    // 只给变白像素补四相同向推动；未变白底与最终黑字均不增加推动。
    // Add four same-direction pushes only to whitening pixels, never unchanged backgrounds or final black text.
    return to == 15 && f < bw->phases + MAIN_ERASE_EXTRA_FRAMES ? 2 : 0;
}

const EpdWaveform* display_main_gray_waveform(void) {
    if (!s_ready) {
        const EpdWaveformPhases* original = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
        if (!original || original->phases != E0470_GL16_FRAMES || !original->luts || original->phase_times)
            return &E0470_WAVEFORM;
        memcpy(s_data, original->luts, sizeof(s_data));
        for (int f = 0; f < E0470_GL16_FRAMES; ++f)
            for (int v = 0; v < 16; ++v)
                s_data[f][v][v / 4] &= (uint8_t)~(3u << (6 - 2 * (v % 4)));
        s_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
        s_ready = true;
    }
    return &s_waveform;
}

const EpdWaveform* display_main_shelf_exit_waveform(void) {
    if (!s_exit_ready) {
        const EpdWaveformPhases* bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
        if (!bw || bw->phases != E0470_FULL_DU_FRAMES || !bw->luts || bw->phase_times)
            return &E0470_WAVEFORM;
        // 取消GC16中灰到白的反向压黑；一次输出内增强白推动，不另起清屏。
        // Remove GC16's reverse darkening on gray-to-white; strengthen white drive in one update without a separate clear.
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from) {
                for (int f = 0; f < MAIN_EXIT_FRAMES; ++f)
                    s_exit_data[f][to][from / 4] |= (uint8_t)(endpoint_action(bw, f, to, from) << (6 - 2 * (from % 4)));
            }
        s_exit_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
        s_exit_ready = true;
    }
    return &s_exit_waveform;
}

const EpdWaveform* display_main_water_waveform(void) {
    if (!s_water_ready) {
        const EpdWaveformPhases* bw = e0470_waveform_phases(&E0470_WAVEFORM, MODE_DU);
        const EpdWaveformPhases* gl = e0470_waveform_phases(&E0470_WAVEFORM, MODE_GL16);
        if (!bw || !gl || !bw->luts || !gl->luts || bw->phases != E0470_FULL_DU_FRAMES ||
            gl->phases != E0470_GL16_FRAMES || bw->phase_times || gl->phase_times)
            return &E0470_WAVEFORM;
        for (int to = 0; to < 16; ++to)
            for (int from = 0; from < 16; ++from)
                for (int f = 0; f < E0470_GL16_FRAMES; ++f) {
                    const int action = to == from ? 0 : to == 0 || to == 15
                        ? endpoint_action(bw, f, to, from) : e0470_phase_action(gl, f, to, from);
                    s_water_data[f][to][from / 4] |= (uint8_t)(action << (6 - 2 * (from % 4)));
                }
        s_water_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
        s_water_ready = true;
    }
    return &s_water_waveform;
}

const EpdWaveform* display_main_bw_waveform(void) {
    if(!s_bw_ready) {
        const EpdWaveformPhases* bw=e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU);
        if(!bw || !bw->luts || bw->phases!=E0470_FULL_DU_FRAMES || bw->phase_times)return &E0470_WAVEFORM;
        memcpy(s_bw_data,bw->luts,sizeof(s_bw_data));
        for(int f=0;f<E0470_FULL_DU_FRAMES;++f)for(int v=0;v<16;++v)
            s_bw_data[f][v][v/4]&=(uint8_t)~(3u<<(6-2*(v%4)));
        s_bw_waveform.temp_intervals=E0470_WAVEFORM.temp_intervals;s_bw_ready=true;
    }
    return &s_bw_waveform;
}

// 普通切页独立派生；水波纹/快刷不改，内存紧张回退现有柔和表。
// Derive ordinary output independently; water/fast stay unchanged and low memory falls back to the existing soft table.
const EpdWaveform* display_main_normal_waveform(void) {
    static EpdWaveformPhases phases;
    static const EpdWaveformPhases *ranges[]={&phases};
    static EpdWaveformMode mode={MODE_GL16,1,ranges};
    static const EpdWaveformMode *modes[]={&mode};
    static EpdWaveform waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=modes};
    if(phases.luts)return &waveform;
    const EpdWaveform *base=display_main_water_waveform();
    const EpdWaveformPhases *original=e0470_waveform_phases(base,MODE_GL16);
    if(!original||!original->luts||original->phases!=E0470_GL16_FRAMES||original->phase_times)return base;
    uint8_t *data=heap_caps_malloc(E0470_GL16_FRAMES*16*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!data)return base;
    memcpy(data,original->luts,E0470_GL16_FRAMES*16*4);
    for(int to=1;to<15;++to)for(int from=0;from<16;++from)
        if(to!=from)display_soften_gray_pair(data,original,to,from);
    phases=(EpdWaveformPhases){E0470_GL16_FRAMES,data,NULL};
    waveform.temp_intervals=base->temp_intervals;
    return &waveform;
}

// 仅书架普通切页：封面/材质灰阶完整保留，留白只向白推动，不再先压黑。
// Ordinary shelf transitions retain calibrated art grays; whitespace drives only white without reverse blackening.
const EpdWaveform* display_main_shelf_waveform(enum EpdDrawMode draw_mode) {
    static EpdWaveformPhases phases[2];
    static const EpdWaveformPhases *ranges[2][1]={{&phases[0]},{&phases[1]}};
    static EpdWaveformMode modes[2]={{MODE_GL16,1,ranges[0]},{MODE_GC16,1,ranges[1]}};
    static const EpdWaveformMode *mode_ptrs[2][1]={{&modes[0]},{&modes[1]}};
    static EpdWaveform waves[2]={
        {.num_modes=1,.num_temp_ranges=1,.mode_data=mode_ptrs[0]},
        {.num_modes=1,.num_temp_ranges=1,.mode_data=mode_ptrs[1]}};
    int index=draw_mode==MODE_GC16?1:0;
    if(phases[index].luts)return &waves[index];
    const EpdWaveformPhases *original=e0470_waveform_phases(&E0470_WAVEFORM,draw_mode);
    const EpdWaveformPhases *soft=e0470_waveform_phases(display_main_normal_waveform(),MODE_GL16);
    if(!original||!soft||!original->luts||!soft->luts||original->phase_times||soft->phase_times||original->phases>soft->phases)
        return &E0470_WAVEFORM;
    size_t size=(size_t)original->phases*16*4;
    uint8_t *data=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!data)return &E0470_WAVEFORM;
    memcpy(data,original->luts,size);
    for(int f=0;f<original->phases;++f)memcpy(data+f*16*4+15*4,soft->luts+f*16*4+15*4,4);
    phases[index]=(EpdWaveformPhases){original->phases,data,NULL};
    waves[index].temp_intervals=E0470_WAVEFORM.temp_intervals;
    return &waves[index];
}

// 用户2026-10-20恢复二级底色：浅灰卡片也使用原完整灰阶，白底沿用同向擦白；底栏由独立裁剪保护。
// User revision2026-10-20: restore complete original gray drive for light cards and directional white erasure; separate clipping protects navigation.
const EpdWaveform* display_main_ui_waveform(void) {
    return display_main_normal_waveform();
}

// 设置滚动只撤掉额外四相补白；中灰、字缘与原DU的完整擦白保持，不改其他页面或周期清理。
// Settings scrolling omits only four extra whitening phases, retaining midgrays, AA edges and native DU erasure without changing other pages or cleanup.
const EpdWaveform* display_settings_scroll_waveform(void) {
    // User revision2026-10-29: keep all native white erasure, insert eight
    // changed-white phases before three settle holds. Highlevel list output
    // supports45 phases independently of the41-phase staggered engine.
    enum {WHITE=8,HOLDS=3,PHASES=E0470_GL16_FRAMES+WHITE};
    static EpdWaveformPhases phases;
    static const EpdWaveformPhases *ranges[]={&phases};
    static EpdWaveformMode mode={MODE_GL16,1,ranges};
    static const EpdWaveformMode *modes[]={&mode};
    static EpdWaveform waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=modes};
    if(phases.luts)return &waveform;
    const EpdWaveform *base=display_main_normal_waveform();
    const EpdWaveformPhases *original=e0470_waveform_phases(base,MODE_GL16);
    if(!original||!original->luts||original->phases!=E0470_GL16_FRAMES||original->phase_times)return base;
    const int drive=original->phases-HOLDS;
    // Only equal-pixel maintenance may occur in the settling tail; it is held below.
    for(int f=drive;f<original->phases;++f)
        for(int to=0;to<16;++to)for(int from=0;from<16;++from)
            if(to!=from&&e0470_phase_action(original,f,to,from))return base;
    uint8_t (*data)[16][4]=heap_caps_malloc(PHASES*64,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!data)return base;
    memcpy(data,original->luts,(size_t)drive*64);
    memset(data+drive,0,WHITE*64);
    memcpy(data+drive+WHITE,original->luts+(size_t)drive*64,HOLDS*64);
    for(int f=0;f<PHASES;++f)for(int gray=0;gray<16;++gray)
        data[f][gray][gray/4]&=(uint8_t)~(3u<<(6-2*(gray%4)));
    for(int f=drive;f<drive+WHITE;++f)
        for(int from=0;from<15;++from)data[f][15][from/4]|=(uint8_t)(2u<<(6-2*(from%4)));
    phases=(EpdWaveformPhases){PHASES,(const uint8_t*)data,NULL};
    waveform.temp_intervals=base->temp_intervals;
    return &waveform;
}

// 设置列表滑动保留短黑白路径，只加强白向擦除；原FOLLOW/阅读/静态灰阶表均不变。
// Keep short BW settings motion, strengthening only white erasure without changing FOLLOW, reader or static-gray tables.
static uint8_t s_settings_motion_data[12][16][4];
static const EpdWaveformPhases s_settings_motion_phases={12,(const uint8_t*)s_settings_motion_data,NULL};
static const EpdWaveformPhases* s_settings_motion_ranges[]={&s_settings_motion_phases};
static const EpdWaveformMode s_settings_motion_mode={MODE_DU,1,s_settings_motion_ranges};
static const EpdWaveformMode* s_settings_motion_modes[]={&s_settings_motion_mode};
static const EpdWaveformTempInterval s_settings_motion_intervals[]={{.min=0,.max=50}};
const EpdWaveform PICO_SETTINGS_MOTION_WAVEFORM={
    .num_modes=1,.num_temp_ranges=1,.mode_data=s_settings_motion_modes,
    .temp_intervals=s_settings_motion_intervals};

const EpdWaveform* display_settings_motion_waveform(void) {
    static bool ready;
    if(!ready) {
        const EpdWaveformPhases* follow=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,MODE_DU);
        if(!follow||!follow->luts||follow->phase_times||follow->phases!=8||
           E0470_FOLLOW_WAVEFORM.num_temp_ranges!=1||!E0470_FOLLOW_WAVEFORM.temp_intervals||
           E0470_FOLLOW_WAVEFORM.temp_intervals[0].min!=0||
           E0470_FOLLOW_WAVEFORM.temp_intervals[0].max!=50)return NULL;
        memcpy(s_settings_motion_data,follow->luts,8u*64u);
        for(int f=8;f<12;++f)for(int from=0;from<15;++from)
            s_settings_motion_data[f][15][from/4]|=(uint8_t)(2u<<(6-2*(from%4)));
        // 拖动期间保持白底，松手时由灰阶定稿清残影。
        // Hold white backgrounds during motion and clean residual ghosts in the final gray update on lift.
        ready=true;
    }
    return &PICO_SETTINGS_MOTION_WAVEFORM;
}

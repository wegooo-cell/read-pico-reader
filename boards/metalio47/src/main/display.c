/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * S31用户最新修订：系统页面及子页黑白输出，只有侧滑返回调用既有水波纹；点按和左上角返回不播放。
 * Latest S31 revision: system pages/children use BW output; only edge returns call existing water, never taps or top-left back.
 * 刷屏出口：按模式推屏、HV 轨空闲下电、pclk 欠载回退。
 *
 * 子页返回沿用柔和端点并错相整理；周期清理不额外插入整屏白/黑刷新。
 * Child returns use soft endpoints and staggered cleanup, without an additional white/black full output.
 * Present path: push by mode, drop HV rails on idle, fall back pclk on
 * underrun.
 * 用户继续优化快刷响应：无灰阶条带时按32位并行归一八像素，已为黑白的字不重复写PSRAM；不删相位、不增加缓存或改变清残影和底栏边界。
 * User continues fast responsiveness tuning: normalize eight pixels per 32-bit word when no gray bands exist and avoid rewriting binary PSRAM words; retain phases, cleanup and navigation bounds without extra caches.
 * 用户修订：三种主页模式均固定底栏，仅局部移动横条；普通切页复用柔和灰阶表，黑白端点单向驱动、等灰保持，中灰保留校准相位。取消每三次全像素整理，仍每十二次完整清残影。快刷棋盘格输出、首次进入、解锁及手动全刷不变，不得伪造back。
 * User revision: all main modes retain navigation with local markers; ordinary transitions reuse soft grays with directional BW endpoints, held equal grays and calibrated changed midgrays. Remove third-page full-pixel settling but retain twelfth-page cleanup. Fast checkerboards, initial entry, unlock and manual full output remain; never fabricate back.
 * 用户修订：所有推屏及欠载恢复与 OTA 擦写共用递归锁；无升级任务时保持原波形、范围和节奏。
 * User revision: serialize all display output and underrun recovery with OTA flash operations; keep existing waveforms, bounds and cadence outside updates.
 * 用户要求加快主页水波纹：复用阅读的一拍一带调度，以8带37相共44拍、11090us完成；保留主页原灰阶序列、底栏边界、OTA锁及失败恢复。
 * User requests faster main-page ripple: reuse reader-style compact launches for eight bands/37 phases over 44 ticks at 11090us; retain main gray sequences, navigation bounds, OTA locking and recovery.
 * 用户修订：书架顶部文字、框线与封面/亚克力一起使用水波纹，不再拆分顶部差分刷新；底栏仍固定。
 * User revision: shelf headers, frames, covers and acrylic share one ripple instead of a separate header differential; navigation remains fixed.

 * 用户修订：纯文字水波纹复用S31默认灰阶相序和等灰保持；保留8带、S31节拍、最终灰阶和故障恢复。
 * User revision: pure-text ripple reuses the S31 default gray order and equal-gray hold; retain eight bands,S31 timing,target grays and recovery.
 * 用户最新指定：显式本地实验的普通文字与水波纹共用22相，普通整页同步；默认及失败回退保留GL48，文字备行保留192行。
 * Latest user request: explicit local Ordinary/Ripple share22 phases, with synchronous Ordinary; default/fallback retainGL48 and reader prefill stays192 rows.
 * 用户修订：对齐厂家 2.0.76，默认灰阶表改回完整 GL48（裁剪会削弱黑墨饱和并在强光下偏淡）；保留节拍、条带与恢复路径。
 * User revision: follow vendor 2.0.76 with the full GL48 default table (trimming weakened black saturation and paled under strong light); keep pacing, bands and recovery.
 * 用户2026-10-24同意独立GL37面板实验：仅验证同一底页的面板/滑块使用原厂裁剪，默认、正文重排和清理仍48相。
 * User authorizes a separate GL37 panel experiment on2026-10-24: only verified same-body overlays/sliders use vendor trimming; default, body reflow and cleaning remain48 phases.
 * 用户2026-10-25另授权书架16灰GL37本地实验：封面/书夹/外围同轮缩短，跨主页保留两相擦白；其他场景及完整清理不改。
 * User authorizes a separate shelf16-gray GL37 experiment on2026-10-25: shorten art/chrome together, keep two root-switch white drives; retain other scenes/full cleanup.
 * 用户2026-10-28优先主页面切换清残影：显式本地开关为真实根页切换补四拍变白、两拍释放；模式、灰阶、底栏裁剪及其他场景保持。
 * User prioritizes root-switch ghosting on2026-10-28: an explicit local flag adds four changed-white drives/two release holds only on real root switches, retaining modes/grays/navigation crops and other scenes.

 */

#include "display.h"
#include "read_pico_board.h"
#include "ota_online.h"
#include "continuous_du.h"

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#if KIIKO_S31_TEXT_RIPPLE
#include "metalio_text_ripple.h"
#endif
#include "ui/ui_nav_layout.h"
#include "metalio_hw.h"
#include "epd_pipeline.h"
#include "read_pico_transfer.h"

static const char* TAG = "read_pico";
static bool s_bulk_io;
static bool s_system_binary;
static bool s_system_navigation;
bool display_system_navigation_set(bool visible) {
    bool previous=s_system_navigation;s_system_navigation=visible;return previous;
}
bool display_system_binary_set(bool enabled) {
    bool previous=s_system_binary;s_system_binary=enabled;return previous;
}
static bool s_full_refresh_requested;
bool display_pipeline_enabled(void) { return epd_pipeline_enabled(); }
bool display_page_prepare_begin(void) {
    if(epd_pipeline_enabled())(void)metalio_tps_prepare();
    return false;
}
void display_page_prepare_end(bool held) {
    (void)held;metalio_tps_cancel_prepare();
}
void display_request_full_refresh(void) { s_full_refresh_requested = true; }
static int s_pclk_mhz = DISPLAY_PCLK_DEFAULT_MHZ;

void display_set_bulk_io(bool active) {
    s_bulk_io = active;
    ESP_LOGI(TAG, "bulk I/O scan margin %s", active ? "on" : "off");
}

// 对齐Moku/厂商：每次输出后立即断轨，保留XOE最短关闭等待及OTA递归互斥。
// Match Moku/vendor: cut HV after each output, retaining XOE shutdown delay and OTA exclusion.
static int64_t drive_release_retry_ms;
void rails_keepalive(void) {
    bool held=pico_online_display_begin();
    const int64_t now_ms=esp_timer_get_time()/1000;
    drive_release_retry_ms=read_pico_epd_release_drive()?0:now_ms+50;
    epd_poweroff();
    pico_online_display_end(held);
}
void rails_idle_check(int64_t now_ms) {
    if(!drive_release_retry_ms||now_ms<drive_release_retry_ms)return;
    bool held=pico_online_display_begin();
    drive_release_retry_ms=read_pico_epd_release_drive()?0:now_ms+50;
    pico_online_display_end(held);
}

// 20 相完整/原厂 DU 按厂家时序走 FULL；只有触摸笔迹用的 8 帧跟随 DU 走 FAST。
// 20-phase full / vendor DU uses FULL timing; the 8-frame FOLLOW DU used for ink trails uses FAST.
static void use_scan_for(const EpdWaveform* waveform, enum EpdDrawMode mode) {
    const bool fast = (mode & 0xF) == MODE_DU && waveform == &E0470_FOLLOW_WAVEFORM;
    read_pico_epd_use_scan(fast ? READ_PICO_EPD_SCAN_FAST : READ_PICO_EPD_SCAN_FULL);
    // 高层刷新保持整屏扫描；两条63行可用队列在第127行入队前启动。
    // High-level updates scan the full panel; two 63-slot queues start before line 127 is enqueued.
    epd_lcd_set_prefill_lines(s_bulk_io ? 127 : (fast ? 64 : 32));
}

// 自上次整屏 GC16 以来的整页差分次数。局部反馈不计数，否则菜单按压几次就会触发黑白全刷。
// Whole-page differentials since the last full GC16. Local feedback is excluded so a few taps cannot trigger flashing.
static int s_page_refreshes;
static int64_t s_page_last_full_us;
// 列表各自累计八次真实滚动；只替换当次用户输出，空闲和按钮反馈不触发。
// Each list accrues eight changed scrolls; replace that user output, never idle or button feedback.
enum { LIST_CLEAN_EVERY=8, LIST_CLEAN_SLOTS=8 };
static struct { unsigned id, changes; EpdRect area; } s_list_clean[LIST_CLEAN_SLOTS];
static unsigned s_list_replace;
static bool ui_page_changed(EpdiyHighlevelState* hl){
    return hl&&hl->front_fb&&hl->back_fb&&memcmp(hl->front_fb,hl->back_fb,(size_t)epd_width()*epd_height()/2)!=0;
}
static bool ui_page_cleanup_due(bool changed){
    if(!changed)return false;
    if(s_page_refreshes<APP_UI_GC16_EVERY)++s_page_refreshes;
    return APP_UI_GC16_EVERY>0&&s_page_refreshes>=APP_UI_GC16_EVERY&&
        esp_timer_get_time()-s_page_last_full_us>=APP_UI_GC16_MIN_INTERVAL_US;
}
static void ui_page_cleaned(enum EpdDrawError result){
    if(result==EPD_DRAW_SUCCESS){
        s_page_refreshes=0;s_page_last_full_us=esp_timer_get_time();
        memset(s_list_clean,0,sizeof(s_list_clean));s_list_replace=0;
    }
}
static bool ui_area_covers(EpdRect outer,EpdRect inner){
    return outer.x<=inner.x&&outer.y<=inner.y&&
        (int64_t)outer.x+outer.width>=(int64_t)inner.x+inner.width&&
        (int64_t)outer.y+outer.height>=(int64_t)inner.y+inner.height;
}
static void ui_native_cleaned(const EpdWaveform* wave,enum EpdDrawMode mode,bool full,
        const EpdRect* area,enum EpdDrawError result){
    if(result!=EPD_DRAW_SUCCESS||!full||wave!=&E0470_FULL_WAVEFORM||(mode&0xf)!=MODE_GC16)return;
    if(!area||ui_area_covers(*area,(EpdRect){0,0,epd_rotated_display_width(),UI_NAV_REFRESH_END})){
        ui_page_cleaned(result);return;
    }
    // 局部清理只能抵扣实际覆盖的列表，不能清掉主页或未覆盖区域的欠账。
    // Regional cleanup pays only covered list debt, never root or uncovered-region debt.
    for(unsigned i=0;i<LIST_CLEAN_SLOTS;++i)
        if(s_list_clean[i].id&&ui_area_covers(*area,s_list_clean[i].area))s_list_clean[i].changes=0;
}


static const void* s_main_armed;
static bool s_main_fault;
static bool s_main_ui_prefill_slow;
static bool s_main_switch_erase;
static unsigned s_main_white_phases=6;
unsigned display_main_quality_set(unsigned white_phases) {
    unsigned previous=s_main_white_phases;
    s_main_white_phases=white_phases==6||white_phases==8?white_phases:0;
    return previous;
}
static const EpdWaveform* main_clean_waveform(const EpdWaveform* wave,enum EpdDrawMode mode) {
    return display_main_clean_waveform_with_phases(wave,mode,s_main_white_phases);
}
static bool main_background_mode(void) {
    const int mode=app_settings_main_refresh_mode();
    return mode==APP_MAIN_REFRESH_FAST||mode==APP_MAIN_REFRESH_WATER;
}
static const EpdWaveform* main_switch_quality_waveform(const EpdWaveform* wave,enum EpdDrawMode mode) {
    return main_background_mode()?display_main_background_waveform_with_phases(wave,mode,s_main_white_phases):
        main_clean_waveform(wave,mode);
}
static bool s_main_shelf_home_erase;
bool display_main_shelf_home_erase_set(bool active) {
    bool previous=s_main_shelf_home_erase;
    s_main_shelf_home_erase=active;
    return previous;
}
static bool s_main_cover_frame;
bool display_main_switch_erase_set(bool active) {
    bool previous=s_main_switch_erase;
    s_main_switch_erase=active;
    return previous;
}
static const EpdWaveform* main_switch_waveform(const EpdWaveform* wave,enum EpdDrawMode mode) {
    return s_main_switch_erase ? display_main_switch_waveform(wave,mode) : wave;
}
// 只在四个根页的真实快刷切换维护背景；首进、同页、子页、灰阶或故障全刷不接管。
// Maintain background only on real fast switches between the four roots; leave initial/same/child/gray/full recovery paths alone.
static const EpdWaveform* main_fast_white_waveform(enum EpdDrawMode mode,
        bool changed,bool changing_page,bool full) {
    if(!s_main_switch_erase||!changed||!changing_page||full||(mode&0xf)!=MODE_DU||
       app_settings_main_refresh_mode()!=APP_MAIN_REFRESH_FAST)return NULL;
    return display_du14_white_waveform();
}
static bool s_main_monochrome;
static bool s_main_shelf_page;
static EpdRect s_main_bands[3];
static unsigned s_main_band_count;
static bool s_main_shelf_exit;
static bool s_main_changing_page;
_Static_assert((UI_NAV_REFRESH_END & 31) == 0, "fixed navigation begins on an aligned framebuffer column");
_Static_assert(UI_NAV_REFRESH_END <= UI_NAV_TOP, "aligned refresh must stop above navigation and marker");
_Static_assert(UI_NAV_MARKER_SCAN_END <= UI_NAV_TOP + 27, "marker crop must stop before navigation icons");
_Static_assert((UI_NAV_MARKER_TOP & 1) && !(UI_NAV_MARKER_BOTTOM & 1), "marker nibble guards match current geometry");

static bool main_nav_geometry(void) {
    // 当前板为倒置竖屏：逻辑 y 对应物理 x。其他屏型保持原刷新路径。
    // On this inverted-portrait board logical y maps to physical x; other layouts keep the existing presenter.
    return epd_get_rotation() == EPD_ROT_INVERTED_PORTRAIT &&
           epd_width() == 1216 && epd_height() == 684;
}

static EpdRect main_body_area(void) {
    // 内容截止到 y=1087，32列对齐也不能扩展到 y=1097 的小横条。
    // Content ends at y=1087, so 32-column expansion cannot reach the marker at y=1097.
    return (EpdRect){0, 0, epd_rotated_display_width(), UI_NAV_REFRESH_END};
}

static uint8_t main_bw_byte(uint8_t value) {
    return (value & 0x0f) >= 8 ? ((value & 0xf0) >= 0x80 ? 0xff : 0x0f)
                             : ((value & 0xf0) >= 0x80 ? 0xf0 : 0x00);
}

void display_monochrome_content(uint8_t* fb) {
    if(!fb||!main_nav_geometry())return;
    const size_t stride=(size_t)epd_width()/2;
    for(int y=0;y<epd_height();++y)for(int x=0;x<UI_NAV_REFRESH_END/2;x+=4){
        uint32_t value;uint8_t* p=fb+(size_t)y*stride+x;
        memcpy(&value,p,sizeof(value));
        const uint32_t binary=((value&UINT32_C(0x88888888))>>3)*15u;
        if(value!=binary)memcpy(p,&binary,sizeof(binary));
    }
}

void display_monochrome_frame(uint8_t* fb) {
    if(!fb)return;
    const size_t bytes=(size_t)epd_width()*epd_height()/2;
    size_t i=0;
    for(;i+4<=bytes;i+=4){
        uint32_t value;memcpy(&value,fb+i,4);
        const uint32_t binary=((value&UINT32_C(0x88888888))>>3)*15u;
        if(value!=binary)memcpy(fb+i,&binary,4);
    }
    for(;i<bytes;++i)fb[i]=main_bw_byte(fb[i]);
}

// 根页内容二值化，底栏保留绘制结果；子页没有底栏才二值整帧。
// Normalize root content while retaining painted navigation; only children normalize the full frame.
static void system_normalize(EpdiyHighlevelState* hl) {
    if(s_system_navigation && main_nav_geometry())display_monochrome_content(hl->front_fb);
    else display_monochrome_frame(hl->front_fb);
}

// 只在目标底栏确有新增灰阶时选择混合表；等灰保持不动，避免每次根页切换多扫一轮。
// Select the mixed table only for changed gray navigation targets; equal grays hold without another output.
static bool system_navigation_gray_changed(const EpdiyHighlevelState* hl) {
    if(!s_system_navigation || !main_nav_geometry())return false;
    const size_t stride=(size_t)epd_width()/2;
    for(int y=0;y<epd_height();++y)for(int x=UI_NAV_REFRESH_END/2;x<epd_width()/2;++x){
        uint8_t to=hl->front_fb[(size_t)y*stride+x],from=hl->back_fb[(size_t)y*stride+x];
        if(((to&15)!=0&&(to&15)!=15&&(to&15)!=(from&15)) ||
           ((to>>4)!=0&&(to>>4)!=15&&(to>>4)!=(from>>4)))return true;
    }
    return false;
}

static bool main_gray_column(int x) {
    for (unsigned i = 0; i < s_main_band_count; ++i)
        if (x >= s_main_bands[i].y && x < s_main_bands[i].y + s_main_bands[i].height) return true;
    return false;
}

static void main_normalize_body(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y) {
        uint8_t* row = hl->front_fb + y * stride;
        if (!s_main_band_count) {
            for (int x = 0; x < UI_NAV_REFRESH_END / 2; x += 4) {
                uint32_t value;
                memcpy(&value, row + x, sizeof(value));
                const uint32_t binary = ((value & UINT32_C(0x88888888)) >> 3) * 15u;
                if (value != binary) memcpy(row + x, &binary, sizeof(binary));
            }
            continue;
        }
        for (int x = 0; x < UI_NAV_REFRESH_END / 2; ++x)
            if (!main_gray_column(x * 2)) row[x] = main_bw_byte(row[x]);
    }
}

static bool main_body_changed(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y)
        if (memcmp(hl->front_fb + y * stride, hl->back_fb + y * stride, UI_NAV_REFRESH_END / 2)) return true;
    return false;
}

static void main_freeze_navigation(EpdiyHighlevelState* hl, bool update_marker) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y) {
        uint8_t* front = hl->front_fb + y * stride;
        const uint8_t* back = hl->back_fb + y * stride;
        if (!update_marker) {
            memcpy(front + UI_NAV_REFRESH_END / 2, back + UI_NAV_REFRESH_END / 2,
                   stride - UI_NAV_REFRESH_END / 2);
            continue;
        }
        // 只保留目标横条的五行；起点为奇数像素，同行字节的分隔线半字节仍取真实 back。
        // Keep only the five target marker rows; at the odd first pixel retain the neighboring divider nibble from actual back.
        memcpy(front + UI_NAV_REFRESH_END / 2, back + UI_NAV_REFRESH_END / 2,
               (UI_NAV_MARKER_TOP - UI_NAV_REFRESH_END) / 2);
        const int first = UI_NAV_MARKER_TOP / 2;
        front[first] = (front[first] & 0xf0) | (back[first] & 0x0f);
        memcpy(front + UI_NAV_MARKER_BOTTOM / 2, back + UI_NAV_MARKER_BOTTOM / 2,
               stride - UI_NAV_MARKER_BOTTOM / 2);
    }
}

void display_main_transition_disarm(void) {
    s_main_cover_frame=false;
    s_main_armed = NULL;
    s_main_shelf_page = false;
    s_main_band_count = 0;
    s_main_shelf_exit = false;
    s_main_changing_page = false;
    s_main_monochrome = false;
}

void display_main_transition_cancel(void) {
    display_main_transition_disarm();
}

void display_main_transition_arm(const void* owner, bool leaving_shelf, bool changing_page) {
    s_main_cover_frame=false;
    s_main_armed = owner;
    s_main_shelf_page = false;
    s_main_band_count = 0;
    s_main_shelf_exit = s_main_armed && leaving_shelf;
    s_main_changing_page = s_main_armed && changing_page;
    s_main_monochrome = false;
}

void display_main_transition_monochrome(void) {
    s_main_cover_frame=false;
    if(!s_main_armed)return;
    s_main_monochrome=true;s_main_band_count=0;
}

void display_main_transition_gray_bands(const EpdRect *bands, unsigned count) {
    s_main_cover_frame=false;
    if (!s_main_armed||s_main_monochrome) return;
    s_main_band_count = 0;
    if (!bands || count > 3) return;
    int last = 0;
    for (unsigned i = 0; i < count; ++i) {
        EpdRect b = bands[i];
        if (b.x || b.width != epd_rotated_display_width() || b.y < last || b.height <= 0 ||
            (b.y & 1) || (b.height & 1) || b.y > UI_NAV_REFRESH_END || b.height > UI_NAV_REFRESH_END - b.y) return;
        s_main_bands[i] = b; last = b.y + b.height;
    }
    s_main_band_count = count;
}

void display_main_transition_cover_bands(const EpdRect *bands,unsigned count) {
    display_main_transition_gray_bands(bands,count);
    s_main_cover_frame=s_main_armed&&count>0&&s_main_band_count==count;
}

void display_main_transition_shelf_page(void) {
    s_main_shelf_page = s_main_armed != NULL;
}

static enum EpdDrawError main_recover(EpdiyHighlevelState* hl, enum EpdDrawError error) {
    // 扫描失败后光学状态未知，必须从白底重建，不能复用半途图的 back。
    // A failed scan leaves unknown optical state; rebuild from white instead of reusing an intermediate back.
    display_main_transition_cancel();
    if (error & EPD_DRAW_EMPTY_LINE_QUEUE) {
        s_main_ui_prefill_slow = true;
        s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
        read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    }
    use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    s_main_fault = result != EPD_DRAW_SUCCESS;
    ui_page_cleaned(result);
    rails_keepalive();
    ESP_LOGW(TAG, "main transition recovery error=%d result=%d", error, result);
    return result;
}

static bool main_begin(EpdiyHighlevelState* hl, enum EpdDrawError* result) {
    const int64_t total_start = esp_timer_get_time();
    const void* owner = s_main_armed;
    const bool cover_frame=s_main_cover_frame;
    s_main_cover_frame=false;
    const bool shelf_exit = s_main_shelf_exit || s_main_shelf_page;
    s_main_shelf_page = false;
    const bool changing_page = s_main_changing_page;
    s_main_changing_page = false;
    const int refresh_mode = app_settings_main_refresh_mode();
    const bool monochrome=s_main_monochrome;
    s_main_monochrome=false;
    const bool water = !monochrome&&refresh_mode == APP_MAIN_REFRESH_WATER;
    const bool normal = !monochrome&&refresh_mode == APP_MAIN_REFRESH_NORMAL;
    const bool fast = monochrome||refresh_mode == APP_MAIN_REFRESH_FAST;
    s_main_shelf_exit = false;
    s_main_armed = NULL;
    if (!owner || !hl || !hl->front_fb || !hl->back_fb) return false;
    if (s_main_fault) { *result = main_recover(hl, EPD_DRAW_SUCCESS); return true; }
    if (!main_nav_geometry()) return false;
    // 主页面黑白沿用所选DU20/本地DU14，灰阶区域在同轮扫描中保留完整迁移。
    // Binary roots use selected DU20/local DU14; gray regions retain complete transitions in the same scan.
    main_freeze_navigation(hl, true);
    if (fast) main_normalize_body(hl);
    const bool changed = main_body_changed(hl);
    // 连横条也未变化时不唤醒高压；故障已在上面恢复，显式全刷仍走独立出口。
    // Avoid waking HV when even the marker is unchanged; faults recover above and explicit full refresh uses its own path.
    if (!changed && !ui_page_changed(hl)) {
        s_main_band_count = 0;
        epd_hl_waveform(hl, &E0470_WAVEFORM);
        use_scan_for(&E0470_WAVEFORM, MODE_GL16);
        rails_keepalive();
        *result = EPD_DRAW_SUCCESS;
        return true;
    }
    const bool fast_regions=fast&&changed&&s_main_band_count;
    const bool gray_fallback = water;
    // 黑白表的等灰迁移均不驱动；仅把有变化的横条并入同次扫描，固定图标保持原样。
    // The BW/exit tables hold every equal pair. Include only the changing marker
    // in this scan; frozen divider/labels/icons receive no drive. This removes
    // an additional comparison and eight-frame serial marker update.
    // 页面切换把横条并入同次输出，冻住的分隔线/图标由等值迁移保持。
    // Fuse the marker into the same transition; equal pairs hold frozen furniture/icons.
    bool marker_with_body=changed&&((fast&&!gray_fallback)||changing_page);
    EpdRect body_area=main_body_area();
    if(marker_with_body)body_area.height=UI_NAV_MARKER_SCAN_END;
    const EpdWaveform* body_wave = normal ? display_main_normal_waveform() : gray_fallback ? display_main_gray_waveform()
        : shelf_exit ? display_main_shelf_exit_waveform() : display_main_bw_waveform();
    enum EpdDrawMode body_mode = normal || gray_fallback ? MODE_GL16 : MODE_DU;
    bool body_full = false;
    // 变化页面共享24次/60秒清理；完整中灰相位不缩短，底栏边界保留。
    // Changed UI pages share the 24-update/60-second gate; keep full midgray phases and footer bounds.
    if(!monochrome&&ui_page_cleanup_due(changed)){
        body_wave=&E0470_FULL_WAVEFORM;body_mode=MODE_GC16;body_full=true;
    }
    // 周期清理直接使用原厂GC48，不允许柔和/短灰阶表替换；底栏保持原样。
    // Periodic cleanup uses raw GC48 without soft/short substitution; retain fixed navigation.
    const EpdWaveform *shelf37_soft=NULL,*shelf37_art=NULL;
    if(normal&&cover_frame&&!body_full){
        shelf37_soft=display_main_shelf37_waveform(false);
        if(shelf37_soft)shelf37_art=display_main_shelf37_waveform(true);
        if(!shelf37_art)shelf37_soft=NULL;
    }
    const bool shelf_regions=normal&&changed&&!body_full&&s_main_band_count&&(changing_page||shelf37_soft);
    if(shelf37_soft)body_wave=shelf37_soft;
    if(shelf_regions){marker_with_body=true;body_area.height=UI_NAV_MARKER_SCAN_END;}
    if(body_full){marker_with_body=false;body_area=main_body_area();}
    const EpdWaveform *fast_white=main_fast_white_waveform(body_mode,changed,changing_page,body_full);
    if(fast_white)body_wave=fast_white;
    if(!body_full)body_wave=main_switch_waveform(body_wave,body_mode);
    const bool clean_switch=s_main_switch_erase&&changed&&changing_page&&!body_full;
    const bool background_switch=clean_switch&&main_background_mode();
    const bool lower_erase=normal&&changed&&changing_page&&shelf_exit&&!body_full&&s_main_shelf_home_erase;
    // 先组合原下半区额外擦白，再派生清残影表，避免丢掉已验证的下半区补推。
    // Compose existing lower-body reinforcement before the quality profile so its verified white drives remain.
    const EpdWaveform *lower_wave=lower_erase?display_main_switch_waveform(body_wave,MODE_GL16):NULL;
    if(clean_switch){
        body_wave=main_switch_quality_waveform(body_wave,body_mode);
        if(lower_wave)lower_wave=main_clean_waveform(lower_wave,MODE_GL16);
    }
    use_scan_for(body_wave, body_mode);
    // 空闲普通/黑白根页减少备行等待，不改扫描时钟、相数和脉宽；欠载后本次开机回到192行。
    // Reduce prefill for idle ordinary/BW roots without changing clock/phases/pulses; an underrun restores192 rows for this boot.
    if((normal||monochrome)&&changing_page&&!s_bulk_io&&!s_main_ui_prefill_slow&&!read_pico_transfer_radio_busy())epd_lcd_set_prefill_lines(128);
    const int64_t power_start = esp_timer_get_time();
    epd_poweron();
    epd_hl_waveform(hl, body_wave);
    int64_t start = esp_timer_get_time();
    // 显式周期清理不进入固定底栏；普通/快刷/水波纹保留各自原扫描序列。
    // Periodic cleanup excludes fixed navigation; ordinary/fast/ripple retain their own scan sequences.
    if(fast_regions&&!body_full){
        const EpdRect last=s_main_bands[s_main_band_count-1];
        const int top=s_main_bands[0].y&~31;
        int bottom=(last.y+last.height+31)&~31;
        if(bottom>UI_NAV_REFRESH_END)bottom=UI_NAV_REFRESH_END;
        const EpdWaveform *gray_wave=main_switch_waveform(display_main_gray_waveform(),MODE_GL16);
        if(clean_switch)gray_wave=main_switch_quality_waveform(gray_wave,MODE_GL16);
        // 黑白、灰阶和横条共用一次真实差分；不再缓存灰阶、输出黑白后恢复、再补横条。
        // BW, grays and marker share one real difference: no gray cache, staged BW restoration or trailing marker pass.
        *result=e0470_update_regions(hl,body_area,(EpdRect){0,top,body_area.width,bottom-top},
            body_wave,gray_wave,MODE_GL16);
        if(*result==EPD_DRAW_NO_PHASES_AVAILABLE){
            // 低内存退回一次完整灰阶目标，不输出中间黑白画面。
            // Low memory falls back to one complete gray target without an intermediate BW frame.
            epd_hl_waveform(hl,gray_wave);
            *result=background_switch?epd_hl_update_area_full(hl,MODE_GL16,25,body_area):
                epd_hl_update_area(hl,MODE_GL16,25,body_area);
        }
    } else if(lower_erase){
        // 下半内容区多两拍向白，和上半区在同一轮扫描；固定底栏及灰/黑脉冲保持。
        // Add two lower-body white drives within the same scan; retain fixed navigation and gray/black pulses.
        *result=e0470_update_regions(hl,body_area,(EpdRect){0,544,body_area.width,544},
                                     body_wave,lower_wave,MODE_GL16);
        if(*result==EPD_DRAW_NO_PHASES_AVAILABLE)
            *result=epd_hl_update_area(hl,MODE_GL16,25,body_area);
    } else if(shelf_regions){
        // 同一帧逐列选择柔和/校准表，上下和中间一起开始，不显示串行的三段旧图。
        // Select soft/calibrated LUTs by columns in the same frame so chrome and art start together without three serial old-image regions.
        int top=0,bottom=UI_NAV_REFRESH_END;
        if(s_main_band_count){
            EpdRect last=s_main_bands[s_main_band_count-1];
            top=s_main_bands[0].y&~31;bottom=(last.y+last.height+31)&~31;
        }
        if(bottom>UI_NAV_REFRESH_END)bottom=UI_NAV_REFRESH_END;
        const EpdWaveform *soft=shelf37_soft?shelf37_soft:display_main_shelf_soft_waveform();
        const EpdWaveform *art=shelf37_art?shelf37_art:display_main_shelf_waveform(MODE_GL16);
        soft=main_switch_waveform(soft,MODE_GL16);art=main_switch_waveform(art,MODE_GL16);
        if(clean_switch){soft=main_clean_waveform(soft,MODE_GL16);art=main_clean_waveform(art,MODE_GL16);}
        *result=e0470_update_regions(hl,body_area,(EpdRect){0,top,body_area.width,bottom-top},
            soft,art,
            MODE_GL16);
        if(*result==EPD_DRAW_NO_PHASES_AVAILABLE){
            // 低内存仍一次输出整张目标；不退回分段等待或更改真实back。
            // Low memory still presents the whole target once, without serial waiting or a fabricated baseline.
            const EpdWaveform *fallback=main_switch_waveform(shelf37_soft?shelf37_soft:display_main_shelf_soft_waveform(),MODE_GL16);
            if(clean_switch)fallback=main_clean_waveform(fallback,MODE_GL16);
            epd_hl_waveform(hl,fallback);
            *result=epd_hl_update_area(hl,MODE_GL16,25,body_area);
        }
    } else if (water && changing_page && changed && !body_full) {
        // 水波纹直接输出最终灰阶图，不先刷黑白、不追加整页灰阶；失败由统一恢复处理。
        // Water outputs the final gray target directly, without a BW prepass or a second whole-page gray update; common recovery handles failure.
        read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FAST);
        epd_lcd_set_prefill_lines(127);
        EpdRect ripple = body_area;
        const int prior_tick=e0470_page_turn_tick_us();
        e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        const EpdWaveform *ripple_wave=main_switch_waveform(display_main_water_waveform(),MODE_GL16);
        if(clean_switch)ripple_wave=main_switch_quality_waveform(ripple_wave,MODE_GL16);
        *result = e0470_page_turn_with_waveform_compact(hl, ripple, E0470_TURN_RTL,
                                                       ripple_wave, E0470_TURN_FAST_BANDS);
        e0470_page_turn_set_tick_us(prior_tick);
        if (*result == EPD_DRAW_NO_PHASES_AVAILABLE) {
            use_scan_for(body_wave, MODE_GL16);
            epd_hl_waveform(hl, body_wave);
            *result = background_switch?epd_hl_update_area_full(hl,MODE_GL16,25,ripple):
                epd_hl_update_area(hl,MODE_GL16,25,ripple);
        }
        // 主页面动画不长期保留相位缓存，避免与联网和阅读资源争用。
        // Release main-animation LUTs instead of competing with networking and reader resources.
        e0470_page_turn_release();
    } else if (!changed) *result = EPD_DRAW_SUCCESS;
    // 等白维护覆盖内容区未变行/列，连同既有补白同轮输出，不追加GC反黑或第二轮。
    // Equal-white maintenance reaches unchanged content rows/columns in the same output as existing tails, without an additional GC or scan.
    else *result = (body_full || fast_white || background_switch) ? epd_hl_update_area_full(hl, body_mode, 25, body_area)
                            : epd_hl_update_area(hl, body_mode, 25, body_area);
    if(body_full)ui_native_cleaned(body_wave,body_mode,true,&body_area,*result);
    s_main_band_count = 0;
    if (*result == EPD_DRAW_SUCCESS && !marker_with_body) {
        const EpdRect marker = {0, UI_NAV_MARKER_TOP, epd_rotated_display_width(),
                               UI_NAV_MARKER_BOTTOM - UI_NAV_MARKER_TOP};
        // 同页重绘横条无变化时 highlevel 自动跳过；32列扩展范围中其余像素来自 back，保持不驱动。
        // Highlevel skips an unchanged marker; all other pixels in its expanded 32-column crop come from back and remain undriven.
        epd_hl_waveform(hl, &E0470_FOLLOW_WAVEFORM);
        use_scan_for(&E0470_FOLLOW_WAVEFORM, MODE_DU);
        *result = epd_hl_update_area(hl, MODE_DU, 25, marker);
    }
    if (*result != EPD_DRAW_SUCCESS) *result = main_recover(hl, *result);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    use_scan_for(&E0470_WAVEFORM, MODE_GL16);
    rails_keepalive();
    ESP_LOGI(TAG, "main refresh mode=%d shelf_exit=%d draw=%lldms result=%d total=%lldms power=%lldms", app_settings_main_refresh_mode(), shelf_exit, (esp_timer_get_time() - start) / 1000, *result,
        (esp_timer_get_time()-total_start)/1000,(start-power_start)/1000);
    (void)start;(void)total_start;(void)power_start;
    return true;
}

// 其他显示出口只取消未用入口；主页面失败时先重建完整目标，绝不回放旧缓存。
// Other outputs disarm unused entry permission; a main-page failure first rebuilds the full current target, never replaying old cache.
static bool main_before_update(EpdiyHighlevelState* hl, enum EpdDrawError* result) {
    display_main_transition_cancel();
    if (s_main_fault) { *result = main_recover(hl, EPD_DRAW_SUCCESS); return true; }
    return false;
}

// 所有按 fb 刷屏的出口都经这里。GL16 必须全像素（白底补 1 帧靠它打到）。
// 整页差分刷攒够 APP_UI_GC16_EVERY 次就把这一次升为全像素 GC16：区域、fb 都不变，只换模式，
// 屏上内容仍由 fb 决定，不会丢；全像素是为了让未变化像素也过一遍 LUT，否则压不掉灰底。
// 跟随 DU 波形只有 DU 一张表，不计数也不升级。
// Every fb present goes through here. GL16 must be full-pixel (the extra white
// frame depends on that). After APP_UI_GC16_EVERY whole-page updates, this one is
// promoted to full-pixel GC16: area and fb stay the same, only the mode
// changes, so content is not lost. Full-pixel is so unchanged pixels also run
// the LUT; otherwise the gray floor will not clear. FOLLOW DU has only a DU
// table and does not count or promote.
static enum EpdDrawError hl_update(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode, bool full,
    const EpdRect* area, bool allow_gl16_diff
) {
    if(s_system_binary){
        system_normalize(hl);
        if((mode&0xF)!=MODE_GC16){
            if(waveform!=&E0470_FOLLOW_WAVEFORM){
                const bool gray=system_navigation_gray_changed(hl);
                waveform=gray?display_main_navigation_waveform():display_main_bw_waveform();
                mode=gray?MODE_GL16:MODE_DU;
            }else mode=MODE_DU;
            full=false;allow_gl16_diff=true;
        }
    }
    // 已显示主页面的局部封面或滚动更新同样向内裁剪，防止32列对齐带出小横条。
    // Local cover or scroll updates on an already displayed main page use the same inward crop, excluding the marker after alignment.
    if (area && s_main_shelf_page && app_settings_main_fast_refresh()) {
        enum EpdDrawError result;
        if (main_begin(hl, &result)) return result;
    }
    EpdRect clipped;
    const bool protected_area = area && s_main_armed && !s_main_fault && main_nav_geometry();
    if (protected_area) {
        main_freeze_navigation(hl, false);
        if(s_main_monochrome){
            display_monochrome_content(hl->front_fb);
            waveform=display_main_bw_waveform();mode=MODE_DU;full=false;allow_gl16_diff=true;
        } else if (app_settings_main_fast_refresh()) main_normalize_body(hl);
        clipped = *area;
        if ((int64_t)clipped.y + clipped.height > UI_NAV_REFRESH_END)
            clipped.height = UI_NAV_REFRESH_END - clipped.y;
        if (clipped.height <= 0) { display_main_transition_disarm(); return EPD_DRAW_SUCCESS; }
        area = &clipped;
        // 只为默认表套用主页策略；显式柔和/短刷保持调用者选择，不通过访问器比较而额外分配缓存。
        // Apply main policy only to the default profile; preserve explicit soft/short profiles without allocating accessor comparisons.
        if (!full && waveform == &E0470_WAVEFORM && (mode & 0x0f) != MODE_GC16 &&
            (app_settings_main_refresh_mode()!=APP_MAIN_REFRESH_NORMAL || (mode & 0x0f)==MODE_GL16)) {
            bool has_gray = !app_settings_main_fast_refresh();
            for (unsigned i = 0; i < s_main_band_count; ++i)
                has_gray |= clipped.y < s_main_bands[i].y + s_main_bands[i].height && clipped.y + clipped.height > s_main_bands[i].y;
            waveform = app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL ? display_main_normal_waveform() :
                has_gray ? display_main_gray_waveform() : s_main_shelf_page
                ? display_main_shelf_exit_waveform() : display_main_bw_waveform();
            if(s_main_cover_frame&&app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL){
                const EpdWaveform *short_wave=display_main_shelf37_waveform(false);
                if(short_wave)waveform=short_wave;
            }
            mode = has_gray ? MODE_GL16 : MODE_DU;
            allow_gl16_diff = true;
            epd_hl_waveform(hl, waveform);
        }
    }
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    const bool whole_page=waveform!=&E0470_FOLLOW_WAVEFORM&&area==NULL;
    if(!s_system_binary&&whole_page&&(mode&0xF)!=MODE_GC16&&ui_page_cleanup_due(ui_page_changed(hl))){
        mode=(enum EpdDrawMode)((mode&~0xF)|MODE_GC16);full=true;waveform=&E0470_FULL_WAVEFORM;
    }
    if(!protected_area&&waveform==display_reader_text_waveform()&&(mode&0xF)==MODE_GL16)allow_gl16_diff=false;
    full = full || ((mode & 0xF) == MODE_GL16 && !allow_gl16_diff);
    if(!full)waveform=main_switch_waveform(waveform,mode);
    // 设置/文件根页由系统出口提交，不经过main_begin；同一根页切换许可仍要覆盖此路径。
    // Settings/files roots use the system presenter rather than main_begin; cover them under the same root-switch permission.
    if(!full&&s_main_switch_erase){
        waveform=main_switch_quality_waveform(waveform,mode);
        if(main_background_mode()){
            // 等白维护必须覆盖真实根页留白；只扩大本轮掩码，底栏图标仍在裁剪之外。
            // Equal-white maintenance must reach root blanks; widen this scan's mask while keeping navigation icons outside the crop.
            full=true;
            if(s_system_navigation&&main_nav_geometry()){
                clipped=area?*area:(EpdRect){0,0,epd_rotated_display_width(),UI_NAV_MARKER_SCAN_END};
                if((int64_t)clipped.y+clipped.height>UI_NAV_MARKER_SCAN_END)
                    clipped.height=UI_NAV_MARKER_SCAN_END-clipped.y;
                if(clipped.height<=0)return EPD_DRAW_SUCCESS;
                area=&clipped;
            }
        }
    }
    // 比较完成后只切换一次扫描档，避免快刷前先切 FULL 再切 FAST。
    // Select scan timing once after comparison, without a FULL-to-FAST round trip before fast updates.
    epd_hl_waveform(hl, waveform);
    use_scan_for(waveform, mode);
    enum EpdDrawError result;
    if (area != NULL) {
        result = full ? epd_hl_update_area_full(hl, mode, 25, *area)
                    : epd_hl_update_area(hl, mode, 25, *area);
    } else result = full ? epd_hl_update_screen_full(hl, mode, 25) : epd_hl_update_screen(hl, mode, 25);

    ui_native_cleaned(waveform,mode,full,area,result);
    if(result!=EPD_DRAW_SUCCESS)s_main_fault=true;
    return protected_area && result != EPD_DRAW_SUCCESS ? main_recover(hl, result) : result;
}

static enum EpdDrawError update_display_mode_unlocked(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL, false);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_mode_diff_unlocked(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL, true);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_fast_page_unlocked(EpdiyHighlevelState* hl) {
    enum EpdDrawError navigation;
    if (main_begin(hl, &navigation)) return navigation;
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if (ui_page_cleanup_due(ui_page_changed(hl))) {
        use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
        epd_poweron();
        epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
        enum EpdDrawError result = epd_hl_update_screen_full(hl, MODE_GC16, 25);
        epd_hl_waveform(hl, &E0470_WAVEFORM);

        ui_page_cleaned(result);
        rails_keepalive();
        ESP_LOGI(TAG, "fast navigation cleanup after %d transitions", APP_UI_FAST_GC16_EVERY);
        return result;
    }
    const bool ordinary = app_settings_main_refresh_mode() == APP_MAIN_REFRESH_NORMAL;
    const EpdWaveform* waveform = ordinary ? display_main_page_gray_waveform() : &E0470_WAVEFORM;
    use_scan_for(waveform, MODE_GL16);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    // 普通子页面与四主页使用同一柔和灰阶，不另加黑白预刷或延迟补刷；其他模式保留原周期。
    // Ordinary subpages share the roots' soft grayscale in one pass, without BW prepasses or delayed output; other modes retain their cadence.
    const bool settle = !ordinary && APP_UI_FAST_GL16_SETTLE_EVERY > 0 &&
                        s_page_refreshes % APP_UI_FAST_GL16_SETTLE_EVERY == 0;
    enum EpdDrawError result = settle ? epd_hl_update_screen_full(hl, MODE_GL16, 25)
                                      : epd_hl_update_screen(hl, MODE_GL16, 25);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_from_white_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    use_scan_for(waveform, mode);
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, mode, 25);

    epd_hl_waveform(hl, &E0470_WAVEFORM);
    if(waveform==&E0470_FULL_WAVEFORM&&(mode&0xf)==MODE_GC16)ui_page_cleaned(result);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_from_white_unlocked(EpdiyHighlevelState* hl) {
    return update_display_from_white_with(hl, &E0470_FULL_WAVEFORM, MODE_GC16);
}

static enum EpdDrawError update_display_white_unlocked(EpdiyHighlevelState* hl) {
    epd_hl_set_all_white(hl);
    return update_display_full(hl);
}

static bool s_white_exit;

void display_hold_white_exit(bool hold) {
    s_white_exit = hold;
}

bool display_take_white_exit(void) {
    const bool hold = s_white_exit;
    s_white_exit = false;
    return hold;
}

static enum EpdDrawError update_display_full_unlocked(EpdiyHighlevelState* hl) {
    s_full_refresh_requested = false;
    if(s_system_binary)system_normalize(hl);
    if(s_main_fault)return main_recover(hl,EPD_DRAW_SUCCESS);
    display_main_transition_cancel();
    epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
    epd_poweron();
    enum EpdDrawError result = hl_update(hl, &E0470_FULL_WAVEFORM, MODE_GC16, true, NULL, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_diff_with_unlocked(EpdiyHighlevelState*, const EpdWaveform*, enum EpdDrawMode, EpdRect);
static enum EpdDrawError update_display_area_full_with_unlocked(EpdiyHighlevelState*, const EpdWaveform*, enum EpdDrawMode, EpdRect);

static enum EpdDrawError update_display_image_gray_unlocked(EpdiyHighlevelState* hl) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    // 图片保留十六级原始灰阶；白底和完整 48 相波形稳定呈现层次。
    // Images keep sixteen source grays; a white baseline and the full 48-phase table preserve tones.
    use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    epd_hl_waveform(hl, &E0470_WAVEFORM);
    ui_page_cleaned(result);
    rails_keepalive();
    return result;
}

// 指定波形整屏刷一次，刷完把默认波形装回去。用来 A/B 两条灰阶表。
// Present the whole screen with a given waveform, then restore the default. Used to A/B two gray tables.
static enum EpdDrawError update_display_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, NULL, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_diff_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area, true);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_area_full_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    epd_poweron();
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, true, &area, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    rails_keepalive();
    return result;
}

static enum EpdDrawError update_display_reader_turn_unlocked(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir, unsigned bands, bool main_gray) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if(s_system_binary&&!main_gray)system_normalize(hl);
    // 用户2026-11-04：单带文字翻页用“白→灰直推”表；图文页仍走原厂柔和翻页表。
    // User2026-11-04: single-band text turns use the white-side gray push; image pages keep the
    // vendor soft-turn table.
    const EpdWaveform* turn_waveform = main_gray?display_main_water_waveform():s_system_binary
        ? (system_navigation_gray_changed(hl)?display_main_navigation_waveform():display_main_bw_turn_waveform())
        : (bands==1?display_reader_text_turn_waveform():display_reader_text_waveform());
    // 普通文字整轮在差分生成前预唤醒TPS，实际驱动仍由引擎按原顺序上电。
    // Pre-wake TPS before ordinary text differences; the engine still enables drive in the established sequence.
    const bool prepared=bands==1&&!s_system_binary&&!main_gray&&epd_pipeline_enabled();
    if(prepared)(void)metalio_tps_prepare();
    if(!main_gray&&!s_system_binary&&bands==1&&app_settings_reader_turn_effect()==2&&
        display_reader_fast_text_prepare(hl,area))turn_waveform=display_reader_fast_text_waveform();
#if KIIKO_S31_TEXT_RIPPLE
    // 用户指定普通也用同一22相；单带同步输出，水波纹才分条带，黑白速读仍为原厂DU20。
    // Ordinary shares the22-phase text sequence in one synchronous band; ripple staggers bands and FastBW retains vendorDU20.
    const bool experimental = !main_gray && !s_system_binary && app_settings_reader_turn_effect()!=2 &&
        metalio_text_ripple_prepare(hl, area);
    if (experimental) {
        turn_waveform = metalio_text_ripple_waveform();
        if(bands>1)bands = METALIO_TEXT_RIPPLE_BANDS;
    }
#endif
    // 阅读水波纹整页维护白底；系统侧滑和局部控件不进入此路径。
    // Maintain the whole reader ripple background; system edge transitions and local controls stay outside this route.
    const bool reader_ripple=!main_gray&&!s_system_binary&&bands>1&&
        app_settings_reader_turn_effect()==1&&area.x==0&&area.y==0&&
        area.width==epd_rotated_display_width()&&area.height==epd_rotated_display_height();
    if(reader_ripple){
        const EpdWaveform* clean=NULL;
#if KIIKO_S31_TEXT_RIPPLE
        if(experimental)clean=metalio_text_ripple_clean_waveform();
#endif
        turn_waveform=clean?clean:display_reader_soft_turn_waveform();
    }
    if(s_system_binary||main_gray){
        turn_waveform=main_switch_waveform(turn_waveform,MODE_GL16);
        if(s_main_switch_erase)turn_waveform=main_switch_quality_waveform(turn_waveform,MODE_GL16);
    }
    // 阅读的同步/水波纹在此分流；主页面及侧滑使用各自的既有路径。
    // Split synchronous/ripple reader output here; main pages and edge-back retain their own paths.
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FAST);
    epd_lcd_set_prefill_lines(s_bulk_io ? 127 : 64);
    // 文字供屏实测128行在第622行欠载；保留S31驱动映射的192行，不再提前开扫。
    // Reader128-row prefill underruns at row622 on hardware; retain the S31 driver's192-row mapping instead of starting early.
    if(!prepared)epd_poweron();
    // 通用GL16连相同灰边也会重新压黑；S31文字序列保持等灰和官方相序，直接用于条带LUT。
    // Generic GL16 re-drives even equal gray edges; S31 text phases hold equal grays and retain vendor pulse order in the band LUT.
    enum EpdDrawError result = bands == 1
        ? e0470_text_turn_with_waveform(hl, area, dir, turn_waveform)
        : s_system_binary
            ? e0470_page_turn_with_waveform_compact(hl, area, dir, turn_waveform, bands)
            : e0470_reader_ripple_with_waveform(hl, area, dir, turn_waveform, bands);
    if(prepared)metalio_tps_cancel_prepare();
    use_scan_for(&E0470_WAVEFORM, MODE_GL16);
    rails_keepalive();
    if (result == EPD_DRAW_SUCCESS) return result;
#if KIIKO_S31_TEXT_RIPPLE
    if (experimental) metalio_text_ripple_disable();
#endif
    if (result == EPD_DRAW_NO_PHASES_AVAILABLE) {
        ESP_LOGW(TAG, "water turn phases unavailable; use regular GL16");
        return update_display_area_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
    }
    // 中断可能把物理屏留在半途。清屏后按目标 front 重建整屏基准。
    // An interrupted scan may leave intermediate pixels. Rebuild the whole baseline from white.
    if (result & EPD_DRAW_EMPTY_LINE_QUEUE) {
        s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
        read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    }
    use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
    epd_clear();
    enum EpdDrawError recovered = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    epd_hl_waveform(hl, &E0470_WAVEFORM);

    ui_page_cleaned(recovered);
    rails_keepalive();
    ESP_LOGW(TAG, "water turn failed (%d), recovery=%d", result, recovered);
    return recovered;
}

// 供数不足时的兜底：把频率退回安全值，整屏白一次，让后面的差分刷有干净参考帧。
// Underrun fallback: drop to the safe clock and wipe the panel white so later differentials have a clean reference.
int display_pclk_mhz(void) { return s_pclk_mhz; }

static void guard_draw_result_unlocked(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    if (!(result & EPD_DRAW_EMPTY_LINE_QUEUE)) return;
    display_main_transition_cancel();
    s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
    read_pico_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
    epd_poweron();
    epd_clear();
    // 清物理屏后仅重置旧帧基准，保留目标页；否则局部刷新会留下整页白屏。
    // Reset only the old-frame baseline after clearing; preserving the target prevents blank pages after partial updates.
    enum EpdDrawError recovered = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    epd_hl_waveform(hl, &E0470_WAVEFORM);

    s_main_fault = recovered != EPD_DRAW_SUCCESS;

    if (recovered != EPD_DRAW_SUCCESS) ESP_LOGW(TAG, "underrun recovery failed (%d)", recovered);
    ui_page_cleaned(recovered);
    rails_keepalive();
    ESP_LOGW(TAG, "line queue underrun, pclk back to %d MHz, recovery=%d", DISPLAY_PCLK_SAFE_MHZ, recovered);
}

#ifdef KIIKO_LOCAL_LATENCY
int64_t local_display_elapsed_us;
#define LATENCY_DISPLAY_BEGIN() const int64_t local_display_start=esp_timer_get_time()
#define LATENCY_DISPLAY_END() (local_display_elapsed_us+=esp_timer_get_time()-local_display_start)
#else
#define LATENCY_DISPLAY_BEGIN() do {} while(0)
#define LATENCY_DISPLAY_END() do {} while(0)
#endif

// 连续相位全部完成后，back已是真实黑白画面；不因尚未补灰阶而多做一次整屏GC16。
// Once continuous phases finish, back is the real binary frame; pending gray restoration alone does not require full GC16.
static bool display_needs_full(void) { return s_full_refresh_requested; }

// 统一硬件出口，递归调用也必须覆盖整个扫描过程。/ Guard complete scans, including recursive fallback paths.
enum EpdDrawError update_display_mode(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_unlocked(hl, mode);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_mode_diff(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_diff_unlocked(hl, mode);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_fast_page(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_fast_page_unlocked(hl);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_from_white_with_unlocked(hl, waveform, mode);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_from_white_unlocked(hl);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_white(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    s_full_refresh_requested = false;
    enum EpdDrawError result = update_display_white_unlocked(hl);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_full(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = update_display_full_unlocked(hl);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

// 只由设置中的主动屏幕修复调用；采用S31已有清屏而非Pico盲刷参数。
// User-triggered Settings repair only; reuse S31 clear, never Pico blind-drive parameters.
enum EpdDrawError display_panel_repair(EpdiyHighlevelState* hl) {
    if(!hl||!hl->front_fb||!hl->back_fb)return EPD_DRAW_INVALID_CROP;
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    continuous_du_reset();s_full_refresh_requested=false;
    display_main_transition_cancel();
    epd_poweroff();use_scan_for(&E0470_FULL_WAVEFORM,MODE_GC16);epd_poweron();
    enum EpdDrawError result=epd_clear_checked();
    // 仅物理清屏成功才把白底作为参考；失败保留真实旧参考并请求完整恢复。
    // Only a successful physical clear establishes white history; failure requests full recovery.
    if(result==EPD_DRAW_SUCCESS) {
        result=update_display_from_white_with_unlocked(hl,&E0470_FULL_WAVEFORM,MODE_GC16);
        if(result!=EPD_DRAW_SUCCESS)result=main_recover(hl,result);
    }
    else (void)read_pico_epd_release_drive();
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    if(s_main_fault)s_full_refresh_requested=true;
    rails_keepalive();pico_online_display_end(held);
    LATENCY_DISPLAY_END();return result;
}

enum EpdDrawError update_display_image_gray(EpdiyHighlevelState* hl) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    // 专用图片出口不继承调用方的系统黑白策略，包含失败恢复也保留原始灰阶。
    // The dedicated image outlet bypasses a caller's BW policy, including recovery, without altering target grays.
    const bool binary = s_system_binary;
    s_system_binary = false;
    s_full_refresh_requested = false;
    enum EpdDrawError result = update_display_image_gray_unlocked(hl);
    s_system_binary = binary;
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_with_unlocked(hl, waveform, mode);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_with_unlocked(hl, waveform, mode, area);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_area_diff_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_diff_with_unlocked(hl, waveform, mode, area);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_settings_list(EpdiyHighlevelState* hl,unsigned list_id,EpdRect area) {
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    enum EpdDrawError result=EPD_DRAW_SUCCESS;
    if(!list_id||!hl||!hl->front_fb||!hl->back_fb||!main_nav_geometry()||area.x||
       area.width!=epd_rotated_display_width()||area.y<0||area.height<=0||
       area.y>UI_NAV_REFRESH_END||area.height>UI_NAV_REFRESH_END-area.y||
       (area.y&1)||(area.height&1))result=EPD_DRAW_INVALID_CROP;
    else if(s_main_fault||display_needs_full())result=update_display_full_unlocked(hl);
    else {
        display_main_transition_cancel();
        bool changed=false;
        const size_t stride=(size_t)epd_width()/2;
        for(int y=0;y<epd_height()&&!changed;++y)
            changed=memcmp(hl->front_fb+(size_t)y*stride+area.y/2,
                hl->back_fb+(size_t)y*stride+area.y/2,(size_t)area.height/2)!=0;
        if(changed){
            unsigned slot=LIST_CLEAN_SLOTS;
            for(unsigned i=0;i<LIST_CLEAN_SLOTS;++i)
                if(s_list_clean[i].id==list_id&&
                   ui_area_covers(s_list_clean[i].area,area)&&ui_area_covers(area,s_list_clean[i].area)){slot=i;break;}
            if(slot==LIST_CLEAN_SLOTS){
                for(unsigned i=0;i<LIST_CLEAN_SLOTS;++i)if(!s_list_clean[i].id){slot=i;break;}
                if(slot==LIST_CLEAN_SLOTS){slot=s_list_replace;s_list_replace=(s_list_replace+1)%LIST_CLEAN_SLOTS;}
                s_list_clean[slot].id=list_id;s_list_clean[slot].area=area;s_list_clean[slot].changes=0;
            }
            if(s_list_clean[slot].changes<LIST_CLEAN_EVERY)++s_list_clean[slot].changes;
            if(s_list_clean[slot].changes>=LIST_CLEAN_EVERY){
                use_scan_for(&E0470_FULL_WAVEFORM,MODE_GC16);
                result=e0470_update_native_area(hl,area,&E0470_FULL_WAVEFORM);
                ui_native_cleaned(&E0470_FULL_WAVEFORM,MODE_GC16,true,&area,result);
                epd_hl_waveform(hl,&E0470_WAVEFORM);
                rails_keepalive();
            } else {
                const EpdWaveform* upper=display_settings_scroll_waveform();
                const EpdWaveform* lower=display_settings_scroll_lower_waveform();
                const int bottom=(area.y+area.height)&~31;
                const int middle=(area.y+area.height/2+31)&~31;
                use_scan_for(upper,MODE_GL16);epd_hl_waveform(hl,upper);
                // 同轮输出上下两种力度；精确裁剪保护题头/底栏，等黑/等灰仍保持。
                // Present both strengths together; exact masks protect header/nav and equal black/grays still hold.
                result=middle<bottom?e0470_update_regions(hl,area,(EpdRect){0,middle,area.width,bottom-middle},
                    upper,lower,MODE_GL16):EPD_DRAW_NO_PHASES_AVAILABLE;
                if(result==EPD_DRAW_NO_PHASES_AVAILABLE)
                    result=update_display_area_diff_with_unlocked(hl,upper,MODE_GL16,area);
                epd_hl_waveform(hl,&E0470_WAVEFORM);rails_keepalive();
            }
        }
    }
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area) {
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    // 控件只扫自身；显示成功后还原未消费的主页许可，不缓存或回放页面像素。
    // Scan only the control, then restore the unused main permission without caching or replaying page pixels.
    const void* owner=s_main_armed;
    bool shelf=s_main_shelf_page,exit=s_main_shelf_exit,changing=s_main_changing_page,monochrome=s_main_monochrome;
    EpdRect bands[3];memcpy(bands,s_main_bands,sizeof(bands));
    unsigned count=s_main_band_count;
    bool full=display_needs_full(),restore=owner&&!s_main_fault&&!full;
    display_main_transition_disarm();
    enum EpdDrawError result=full?update_display_full_unlocked(hl):
        update_display_area_diff_with_unlocked(hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
    if(restore&&result==EPD_DRAW_SUCCESS&&!s_main_fault) {
        s_main_armed=owner;s_main_shelf_page=shelf;s_main_shelf_exit=exit;
        s_main_changing_page=changing;s_main_monochrome=monochrome;s_main_band_count=count;memcpy(s_main_bands,bands,sizeof(bands));
    }
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_area_full_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_full_with_unlocked(hl, waveform, mode, area);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_water_turn(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_reader_turn_unlocked(hl, area, dir, E0470_TURN_FAST_BANDS, false);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

// 灰阶系统根页使用本机主页波形，不进入阅读文字量化或22相实验。
// Gray system roots use their native main LUT, outside reader quantization and the22-phase experiment.
enum EpdDrawError update_display_main_turn(EpdiyHighlevelState* hl,EpdRect area,e0470_turn_dir_t dir) {
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    const int prior_tick=e0470_page_turn_tick_us();
    e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
    enum EpdDrawError result=display_needs_full()?update_display_full_unlocked(hl):
        update_display_reader_turn_unlocked(hl,area,dir,E0470_TURN_FAST_BANDS,true);
    e0470_page_turn_set_tick_us(prior_tick);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

// 同步单带文字翻页：与水波纹相同灰阶/节拍，不错相、不追加第二轮。
// One synchronous band shares ripple gray pulses/timing, with no stagger or second output.
enum EpdDrawError update_display_text_turn(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    int prior_tick=e0470_page_turn_tick_us();
    e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_reader_turn_unlocked(hl, area, dir, 1, false);
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    e0470_page_turn_set_tick_us(prior_tick);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

// 检查实际提交的对齐物理列，旧中灰到黑白可以走原厂DU，不因“有图片”强制48相。
// Inspect the aligned physical crop: native DU accepts real old grays and BW targets, independent of image metadata.
static bool transition_gray_changed(EpdiyHighlevelState* hl, EpdRect area) {
    if(!hl||!hl->front_fb||!hl->back_fb||hl->front_fb==hl->back_fb||area.width<=0||area.height<=0)return true;
    const int pw=epd_width(),ph=epd_height();
    if(pw<=0||ph<=0||(pw&1))return true;
    enum EpdRotation rotation=epd_get_rotation();
    bool portrait=rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT;
    int lw=portrait?ph:pw,lh=portrait?pw:ph;
    int64_t right=(int64_t)area.x+area.width,bottom=(int64_t)area.y+area.height;
    int x=area.x<0?0:area.x,y=area.y<0?0:area.y;
    if(right>lw)right=lw;
    if(bottom>lh)bottom=lh;
    if(right<=x||bottom<=y)return true;
    int w=(int)right-x,h=(int)bottom-y;EpdRect phys;
    switch(rotation) {
        case EPD_ROT_LANDSCAPE:phys=(EpdRect){x,y,w,h};break;
        case EPD_ROT_PORTRAIT:phys=(EpdRect){pw-y-h,x,h,w};break;
        case EPD_ROT_INVERTED_LANDSCAPE:phys=(EpdRect){pw-x-w,ph-y-h,w,h};break;
        case EPD_ROT_INVERTED_PORTRAIT:phys=(EpdRect){y,ph-x-w,h,w};break;
        default:return true;
    }
    int x0=phys.x/32*32,x1=(phys.x+phys.width+31)/32*32;
    if(x1>pw)x1=pw;
    for(int row=phys.y;row<phys.y+phys.height;++row) {
        size_t offset=(size_t)row*(pw/2);
        for(int col=x0/2;col<x1/2;++col){
            uint8_t to=hl->front_fb[offset+col],from=hl->back_fb[offset+col],delta=to^from;
            if(!delta)continue;
            unsigned lo=to&15,hi=to>>4;
            if(((delta&15)&&lo!=0&&lo!=15)||((delta&240)&&hi!=0&&hi!=15))return true;
        }
    }
    return false;
}

enum EpdDrawError update_display_gray_transition(EpdiyHighlevelState* hl,EpdRect area) {
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    const bool binary=s_system_binary;
    s_system_binary=false;
    enum EpdDrawError result;
    if(display_needs_full())result=update_display_full_unlocked(hl);
    else {
        bool gray=transition_gray_changed(hl,area);
        // 图文及离开图片共用主页已验证的两拍擦白，只增强变化为白的像素；中灰原序列和释放尾段完整保留。
        // Image/exit transitions reuse the tested two white drives, only on changed white targets; retain gray sequences and release tails.
        const enum EpdDrawMode mode=gray?MODE_GL16:MODE_DU;
        const EpdWaveform* waveform=display_main_switch_waveform(
            gray?display_main_shelf_soft_waveform():display_main_bw_waveform(),mode);
        result=update_display_area_diff_with_unlocked(hl,waveform,mode,area);
        ESP_LOGI(TAG,"gray transition route=%s area=%d,%d,%d,%d result=%d",gray?"gray-white-erase":"bw-white-erase",area.x,area.y,area.width,area.height,result);
    }
    s_system_binary=binary;
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    LATENCY_DISPLAY_BEGIN();
    bool held = pico_online_display_begin();
    guard_draw_result_unlocked(hl, result);
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
}

// 图形面板恢复原厂完整灰阶，不量化、不借文字短表；每次区域定稿修正中灰历史。
// Graphical panels retain complete vendor grays without text quantization; settle the region each time to correct midgray history.
enum EpdDrawError update_display_reader_panel(EpdiyHighlevelState* hl,EpdRect area) {
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    const bool binary=s_system_binary;
    s_system_binary=false;
    enum EpdDrawError result=display_needs_full()?update_display_full_unlocked(hl):
        update_display_area_full_with_unlocked(hl,&E0470_WAVEFORM,MODE_GL16,area);
    s_system_binary=binary;
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    LATENCY_DISPLAY_END();
    return result;
}

// 只供已确认底页上的面板/滑块；换字体后正文重排和手动清理仍由48相出口负责。
// Only verified same-body panels/sliders use this route; font reflow and manual cleaning retain48-phase owners.
static bool s_reader_panel37_failed;
enum EpdDrawError update_display_reader_panel_fast(EpdiyHighlevelState* hl,EpdRect area) {
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    const bool binary=s_system_binary;
    s_system_binary=false;
    const EpdWaveform *wave=NULL;
    enum EpdDrawError result;
    if(display_needs_full())result=update_display_full_unlocked(hl);
    else {
        if(!s_reader_panel37_failed)wave=display_reader_panel37_waveform();
        result=wave?update_display_area_diff_with_unlocked(hl,wave,MODE_GL16,area):
            update_display_area_full_with_unlocked(hl,&E0470_WAVEFORM,MODE_GL16,area);
        if(wave&&result!=EPD_DRAW_SUCCESS)s_reader_panel37_failed=true;
    }
    s_system_binary=binary;
    s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    ESP_LOGI(TAG,"reader panel profile=%s result=%d",wave?"GL37":"native48",result);
    LATENCY_DISPLAY_END();
    return result;
}

enum EpdDrawError update_display_reader_panel_gray4(EpdiyHighlevelState* hl,EpdRect area,bool erase_underlay) {
    if(!display_reader_fast_panels_enabled())return update_display_reader_panel_fast(hl,area);
    static bool failed;
    LATENCY_DISPLAY_BEGIN();
    bool held=pico_online_display_begin();
    const bool binary=s_system_binary;s_system_binary=false;
    const EpdWaveform* wave=failed?NULL:display_reader_panel_gray4_waveform(erase_underlay);
    enum EpdDrawError result;
    if(!hl||!display_reader_panel_gray4_prepare(hl->front_fb,area))result=EPD_DRAW_NO_PHASES_AVAILABLE;
    else if(display_needs_full()){wave=NULL;result=update_display_full_unlocked(hl);}
    else {
        const EpdWaveform* fallback=wave||s_reader_panel37_failed?NULL:display_reader_panel37_waveform();
        // 入场必须包含等灰像素；复用单轮批量出口的精确掩码，避免差分跳过旧字或32列对齐扫出面板。
        // Entry includes equal-gray pixels through exact-crop batch masks, avoiding diff omissions and32-column spill outside the sheet.
        if(wave&&erase_underlay){
            use_scan_for(wave,MODE_GL16);
            result=e0470_text_turn_with_waveform(hl,area,E0470_TURN_RTL,wave);
            epd_hl_waveform(hl,&E0470_WAVEFORM);
            rails_keepalive();
        }
        else result=wave?update_display_area_diff_with_unlocked(hl,wave,MODE_GL16,area):
            fallback?update_display_area_diff_with_unlocked(hl,fallback,MODE_GL16,area):
            update_display_area_full_with_unlocked(hl,&E0470_WAVEFORM,MODE_GL16,area);
        if(wave&&result!=EPD_DRAW_SUCCESS)failed=true;
        if(fallback&&result!=EPD_DRAW_SUCCESS)s_reader_panel37_failed=true;
    }
    s_system_binary=binary;s_main_fault=result!=EPD_DRAW_SUCCESS;
    pico_online_display_end(held);
    ESP_LOGI(TAG,"reader four-gray panel profile=%s result=%d",wave?(erase_underlay?"gray4-entry26":"gray4-16"):"native-gray",result);
    LATENCY_DISPLAY_END();return result;
}

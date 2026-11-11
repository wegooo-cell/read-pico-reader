/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * S31用户最新修订：系统内容黑白、根页底栏保留灰底；根页遵循水波纹模式，子页只侧滑返回沿LTR动画。
 * Latest S31 revision: BW system content retains gray root navigation; roots honor water mode and children animate LTR only on edge return.
 * 事件循环。只做和页面无关的事：读触摸并去抖、三个实体键与菜单把手的路由、切页
 * 时调 on_exit/on_enter、按回调返回值决定怎么刷屏、电源键锁屏、SD 卡上的字体延迟
 * 加载。页面自己的状态和刷新节奏都在 main/apps/ 各自的文件里。
 *
 * Event loop for page-agnostic work: touch debounce, the three keys and
 * the menu handle, on_exit/on_enter, present, lock, delayed SD font load.
 * Page state and refresh cadence stay in main/apps/; gesture recognition is shared.
 *
 * 冻结（用户2026-10-16修订）：四个主页面左右键循环切换，中键长按全刷；书架中键短按切换左右键的分页控制。
 * 主页面中键短按延后到抬手，长按不执行短按；离开主页面立即取消按住状态。
 * 阅读和子页面沿用自己的映射与手势，把手按下即触发。
 * Frozen (user revision 2026-10-16): main roots cycle via side keys; middle hold refreshes fully, and a shelf middle tap toggles paging control.
 * Defer the root middle tap until release, suppress it after a hold, and cancel held input on root exit.
 * Reader/subpages retain their mappings and gestures; the menu handle still fires on contact.
 * 用户修订：S31上下音量键在主页面复用左右键派发，阅读和二级页仍交给页面。
 * User revision: S31 volume edges share root side-key dispatch; reading and children remain page-owned.
 * 用户修订：AI长按打开玻璃短按映射选择器；正常开合各一次区域推屏，选择不执行动作，锁屏/USB/升级保留保护。
 * User revision: hold AI for a glass tap-action chooser; one bounded output per normal transition, selection never executes, and lock/USB/update guards remain.
 * 用户修订：四个主页面底栏作为统一导航路由在按下时报点即响应，模态和阅读手势保持原归属。
 * User revision: root-tab routing commits on initial contact; modal controls and reader gestures retain their owners.
 * 用户修订：系统页面切换使用 GL16，不再每次 GC16 全刷；锁屏、手动强刷和周期清残影仍可全刷。
 * User revision: system page changes use GL16 instead of GC16; lock, manual refresh and periodic ghost cleanup may still use full refresh.
 * 用户修订：设置可选普通/快刷/水波纹，默认普通；普通保留原灰阶路线，快刷采用确认的黑白主页面和亚克力灰阶。用户反馈残影和黑带后，书架退出只同向擦白，主页水波纹改白底/24窄带且端点单向推动，中灰保留校准序列；五行横条局部移动、图标固定，没有异步补刷。
 * User revision: settings offer ordinary/fast/water, default ordinary. Ordinary retains the original gray path; fast uses approved monochrome main screens and gray acrylic. Following reported ghosts and dark bands, shelf exit whitens directionally; main water uses white backgrounds, 24 narrow bands and directional endpoints, retaining calibrated midgrays. Move the five-row marker locally, retain icons, and schedule no asynchronous completion.
 * 用户修订：普通模式也固定主页面底栏，仅局部移动横条；快刷亚克力采用规则一像素棋盘格，与已缓存封面一起输出。普通及水波纹保留灰阶，首次进入及解锁仍完整绘制底栏。
 * User revision: ordinary main screens also retain navigation and move only its marker locally; fast acrylic uses a regular one-pixel checkerboard in the same output as cached covers. Ordinary/water retain grays; initial entry and unlock still draw complete navigation.
 * 冻结：返回消费最近切页来源，恢复原页及菜单位置，不跳固定首页。
 * Frozen: Return consumes the latest page origin, restoring its page and menu position.
 * 用户采用顶部快捷层：仅顶部32px起手下滑72px才接管，覆盖时冻结页内输入/轮询，WiFi直接连接最近保存网络/再次断开，蓝牙开启并异步连接最近成功的翻页器，全刷和锁屏复用原路径。用户要求三个模式均真实灰阶并加深轮廓；关闭还原顶部底图并局部整理，不修改底栏。
 * User adopts the top quick layer: capture only a 72px downward swipe starting within the top 32px; suspend page input/ticks while covered. WiFi connects saved credentials or disconnects on the next tap; Bluetooth enables and queues a connection to the most recently successful turner. Reuse full refresh and lock. Per the user, all modes retain grays with clearer outlines. Closing restores the bounded top underlay and locally settles it, retaining navigation.
 * 用户修订：快捷层只在展开/收起重绘顶部；圆键按下立即局部DU反馈，状态变化不重排底页或生成玻璃。
 * User revision: only opening/closing repaints the top; key presses receive immediate local DU feedback without underlay re-layout or glass regeneration.
 * 用户2026-10-19修订：下拉菜单锁屏前先收起并还原底页；点全刷自动收起，仅对还原后的页面全刷。
 * 用户2026-10-24修订：S31下拉/AI菜单收回时，恢复完整底页并以一次GC48清理消除矩形残影；合并为收回输出，不增加菜单功能或先局部再全刷。
 * User revision 2026-10-24: S31 quick/AI close restores the page and uses one GC48 cleanup to remove rectangular ghosts, replacing the close output without new actions or a local pass before cleanup.
 * User revision 2026-10-19: dismiss and restore the underlay before locking; full refresh dismisses the layer and refreshes only the restored page.
 * 用户点击反馈：底栏及圆形返回键按下局部放大，释放复位；只暂存一个小控件，切页、全刷及锁屏丢弃缓存，不回放旧画面。
 * User click feedback: locally enlarge navigation/round backs on press and restore on release. Retain one small control only; navigation, full refresh and lock discard it without replaying old frames.
 * 用户修订：确认主动锁屏后，拼贴样式先给局部圆锁反馈，再保存/等待阅读预渲染；自动休眠和直接关机/重启不加此反馈。
 * User revision: acknowledge accepted manual collage locks before saves/reader joins; omit the badge for automatic sleep and direct shutdown/restart.
 * 用户2026-10-21修订：二级页常驻灰底栏，内容区右滑直接使用阅读翻页识别结果，不限左边缘及额外角度；键盘保留自己的手势，返回仍复用清理及本板一次LTR水波纹。
 * User revision2026-10-21: children retain gray navigation; content right-swipes reuse reading recognition without left-edge or extra angle restrictions. Keyboards keep their gestures; back reuses cleanup and one board-local LTR ripple.
 */

#ifdef ESP_PLATFORM
#include "ui_locale_format.h"
#endif
#include "app_loop.h"
#if KIIKO_BOARD_METALIO47
#include "anti_fade.h"
#include "sd_recovery.h"
#else
#endif
#include "auto_lock.h"
#include "crash_note.h"

#include <string.h>

#include "app_registry.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "ble_page_turner.h"
#include "ota_online.h"
#include "continuous_du.h"
#include "display.h"
#include "display_main_waveform.h"
#include "ui_nav.h"
#include "ui_nav_layout.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_crashlog.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "sleep.h"
#include "time_service.h"
#include "ui_avatar.h"
#include "book/book_cover_jobs.h"
#include "book/book_lock_collage.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "setup_state.h"
#include "ui_nav.h"
#include "ui_gesture.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "ui_click_feedback.h"
#include "usb_storage.h"
#if KIIKO_BOARD_METALIO47
#include "metalio_hw.h"
#include "ui_ai_menu.h"
#include "book/book_source.h"
#include "online_font.h"
#else
#endif
#include "local_latency_probe.h"

#define TAG "app_loop"

// 触摸芯片报点率远高于屏幕，主循环空转一轮只等这么久。
// Touch reports far faster than the panel; idle this long per loop.
#define LOOP_TICK_MS 5
// SD 卡插上或者刚挂好时，隔一会儿再试一次存在设置里的字体。
// Retry the saved SD font shortly after a card appears or mounts.
#define FONT_RETRY_INTERVAL_MS 750
#define MEDIA_POLL_INTERVAL_MS 500

static int64_t s_lock_ignore_until_ms;
extern const app_desc_t app_transfer;
extern const app_desc_t app_book;

static const app_desc_t* s_presented_main;
static ui_quick_menu_t s_quick;
static unsigned s_quick_status;
static int s_quick_pressed=-1;
static const app_desc_t* s_presented_root;
#if KIIKO_BOARD_METALIO47
static ui_ai_menu_t s_ai;
static unsigned s_quick_slider_dirty;
static unsigned s_quick_slider_drawn[2],s_quick_slider_level[2];
#else
#endif
static void quick_reset(void) {
#if KIIKO_BOARD_METALIO47
    ui_ai_menu_end(&s_ai);metalio_ai_clear();
    ui_quick_menu_reset(&s_quick);ui_quick_menu_end();s_quick_pressed=-1;s_quick_slider_dirty=0;
#else
    ui_quick_menu_reset(&s_quick);
    ui_quick_menu_end();
    s_quick_pressed=-1;
#endif
}
static bool ai_overlay_open(void){
#if KIIKO_BOARD_METALIO47
    return s_ai.open;
#else
    return false;
#endif
}
static bool s_nav_contact_down,s_nav_polled;
// 锁屏占用整屏后旧主页面底栏不再是物理基准；解锁第一帧必须完整绘制。
// A lock screen replaces the old navigation baseline; the first unlocked frame must render complete navigation.
static void main_lock_boundary(void) {
#if KIIKO_BOARD_METALIO47
    metalio_frontlight_commit();
#else
#endif
    (void)ble_pt_take_standby();
    ui_click_feedback_reset();
    quick_reset();
    s_presented_main = NULL;
    display_main_transition_cancel();
}

static unsigned quick_status(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    bool wifi = status.mode == READ_PICO_TRANSFER_MODE_STA && status.state != READ_PICO_TRANSFER_STOPPED;
#if KIIKO_BOARD_METALIO47
    return (wifi ? 1u : 0u) | (app_settings_ble_turner() ? 2u : 0u) | (status.network_ready ? 4u : 0u) | (metalio_frontlight_enabled()?8u:0u);
#else
    return (wifi ? 1u : 0u) | (app_settings_ble_turner() ? 2u : 0u) | (status.network_ready ? 4u : 0u);
#endif
}

// 同步输出完再接收下一条触摸，不安排旧层回放；错误时关闭层并可靠全刷当前页。
// Complete presentation before accepting another contact, with no stale replay; close and full-refresh the current page on error.
static void quick_present(app_ctx_t *ctx, const app_desc_t *current, bool full) {
    const int64_t started=esp_timer_get_time();
    EpdRect motion_area={0};
    bool motion=ui_click_feedback_nav_finish(ctx->fb,&motion_area);
    ui_click_feedback_begin(ctx->fb);
    display_main_transition_cancel();
    unsigned status=quick_status();
    if(full&&s_quick.open){
        // 复用合成帧，只还原按压键；不重排底页或重算玻璃。/ Reuse the composed frame, restoring pressed keys without re-rendering or reblurring.
#if KIIKO_BOARD_METALIO47
        static const unsigned bits[]={1u,2u,0u,8u};
#else
        static const unsigned bits[]={1u,2u,0u,0u};
#endif
        for(int i=0;i<4;++i){
            bool active=(status&bits[i])!=0,was_active=(s_quick_status&bits[i])!=0;
            if(i==s_quick_pressed||active!=was_active){
                EpdRect dirty;(void)ui_quick_menu_draw_button(ctx->fb,i,active,false,&dirty);
            }
        }
        if((status^s_quick_status)&6u)ui_quick_menu_draw_status(ctx->fb);
    }else{
#if KIIKO_BOARD_METALIO47
        if(s_quick.open){
            s_quick_slider_dirty=0;
            s_quick_slider_level[0]=metalio_frontlight_position(false);
            s_quick_slider_level[1]=metalio_frontlight_position(true);
            s_quick_slider_drawn[0]=ui_quick_brightness_position(s_quick_slider_level[0]);
            s_quick_slider_drawn[1]=metalio_frontlight_position(true);
            ui_quick_menu_draw(ctx->fb,status&1u,status&2u,status&8u,metalio_frontlight_position(false),s_quick_slider_drawn[1],
                current==&app_book&&app_book_reader_body_visible());
        }else{
            bool restored=ui_quick_menu_restore(ctx->fb);ui_quick_menu_end();
            if(!restored&&current->render)current->render(ctx,ctx->fb);
        }
#else
        if(s_quick.open)ui_quick_menu_draw(ctx->fb,status&1u,status&2u,current==&app_book&&app_book_reader_body_visible());
        else{
            bool restored=ui_quick_menu_restore(ctx->fb);ui_quick_menu_end();
            if(!restored&&current->render)current->render(ctx,ctx->fb);
        }
#endif
    }
    s_quick_status=status;s_quick_pressed=-1;
    const int64_t composed=esp_timer_get_time();
    (void)started;(void)composed;
#if KIIKO_BOARD_METALIO47
    EpdRect area=ui_quick_menu_rect();
    // 收回只提交一次完整底页清理，不先刷矩形再全刷，避免玻璃灰阶与周围白底留下边界。
    // Close cleans the restored page once, without a preceding rectangle pass or a glass/white boundary.
    enum EpdDrawError error=(full||!s_quick.open)?update_display_full(ctx->hl)
        :update_display_gray_transition(ctx->hl,area);
#else
    enum EpdDrawError error=full?update_display_full(ctx->hl)
        :update_display_area_full_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_quick_menu_rect());
#endif
    guard_draw_result(ctx->hl,error);
#if KIIKO_BOARD_METALIO47
    if(motion&&!full&&s_quick.open&&error==EPD_DRAW_SUCCESS){
#else
    if(motion&&!full&&error==EPD_DRAW_SUCCESS){
#endif
        error=update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,motion_area);
        guard_draw_result(ctx->hl,error);
    }
    ESP_LOGI(TAG,"quick %s compose=%lldms display=%lldms",
        s_quick.open?"open":"close",(long long)((composed-started)/1000),
        (long long)((esp_timer_get_time()-composed)/1000));
    if(error!=EPD_DRAW_SUCCESS){quick_reset();app_present(ctx,current,APP_REDRAW_FULL);}
}

// 先收起菜单并恢复底页，再执行全刷或锁屏；保留底图直到还原完成。
// Dismiss before refresh/lock; retain the underlay until restoration finishes.
static void quick_dismiss(app_ctx_t *ctx,const app_desc_t *current,bool full) {
#if KIIKO_BOARD_METALIO47
    metalio_frontlight_commit();
#endif
    ui_quick_menu_reset(&s_quick);
    quick_present(ctx,current,full);
}

#ifdef KIIKO_LOCAL_LATENCY
static void local_probe_quick(app_ctx_t *ctx,const app_desc_t *current,bool open) {
    const int64_t start=esp_timer_get_time();
    if(current->render&&!current->holds_pmu&&!usb_storage_active()&&!pico_online_busy()&&s_quick.open!=open) {
        if(open){ui_quick_menu_reset(&s_quick);s_quick.open=true;quick_present(ctx,current,false);}
        else quick_dismiss(ctx,current,false);
    }
    ESP_LOGI("latency","QUICK open=%d us=%lld",s_quick.open,(long long)(esp_timer_get_time()-start));
}
#endif

#if KIIKO_BOARD_METALIO47
// Compose changed buttons and slider bands, then present one bounded differential.
static EpdRect quick_join(EpdRect a,EpdRect b) {
    if(!a.height)return b;
    if(!b.height)return a;
    int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;
    int right=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width;
    int bottom=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;
    return (EpdRect){x,y,right-x,bottom-y};
}
static void quick_controls_present(app_ctx_t *ctx,const app_desc_t *current) {
#else
// 菜单打开期间只改圆键和必要的状态栏；按下不排字、不重画页面或重算玻璃。
// While open, update only keys and the necessary status strip; pressing never shapes text, renders a page or regenerates glass.
static void quick_buttons_present(app_ctx_t *ctx,const app_desc_t *current) {
#endif
    if(!s_quick.open)return;
#if KIIKO_BOARD_METALIO47
    unsigned status=quick_status(),changed=(status^s_quick_status)&11u;
    int pressed=ui_quick_menu_pressed(&s_quick);EpdRect dirty={0};
    static const unsigned bits[]={1u,2u,0u,8u};
    for(unsigned i=0;i<4;++i)if((changed&bits[i])||((pressed==(int)i)!=(s_quick_pressed==(int)i))) {
        EpdRect r;if(ui_quick_menu_draw_button(ctx->fb,i,(status&bits[i])!=0,pressed==(int)i,&r))dirty=quick_join(dirty,r);
#else
    unsigned status=quick_status();
    int pressed=ui_quick_menu_pressed(&s_quick);
    enum EpdDrawError error=EPD_DRAW_SUCCESS;
    for(int i=0;i<4;++i){
        bool active=i<2&&(status&(1u<<i));
        bool was_active=i<2&&(s_quick_status&(1u<<i));
        if(active==was_active&&(pressed==i)==(s_quick_pressed==i))continue;
        EpdRect dirty;
        if(ui_quick_menu_draw_button(ctx->fb,i,active,pressed==i,&dirty)){
            error=update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,dirty);
            guard_draw_result(ctx->hl,error);
            if(error!=EPD_DRAW_SUCCESS)break;
        }
#endif
    }
#if KIIKO_BOARD_METALIO47
    for(unsigned i=0;i<2;++i){
        unsigned level=metalio_frontlight_position(i!=0);
        unsigned value=i?level:ui_quick_brightness_position(level);
        if(level==s_quick_slider_level[i])value=s_quick_slider_drawn[i];
        // 拖动时保留原始像素位置，PWM值未跨档也让填充边缘跟手；松手不额外定稿。
        // Track raw drag pixels even within one PWM step; releasing adds no settle scan.
        if(s_quick.hit==(int)(4+i) && (s_quick.tracking || s_quick_slider_dirty&(1u<<i)))
            value=(unsigned)s_quick.slider_position;
        s_quick_slider_level[i]=level;
        if(value==s_quick_slider_drawn[i])continue;
        EpdRect band=ui_quick_slider_change_rect(i!=0,s_quick_slider_drawn[i],value);
        if(band.height){ui_quick_menu_draw_slider(ctx->fb,i!=0,value);dirty=quick_join(dirty,band);}
        s_quick_slider_drawn[i]=value;
#else
    bool network_changed=(status&5u)==5u,was_network=(s_quick_status&5u)==5u;
    if(error==EPD_DRAW_SUCCESS&&(((status^s_quick_status)&2u)||network_changed!=was_network)){
        ui_quick_menu_draw_status(ctx->fb);
        error=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){0,0,UI_LOCK_WIDTH,76});
        guard_draw_result(ctx->hl,error);
#endif
    }
#if KIIKO_BOARD_METALIO47
    s_quick_slider_dirty=0;
    enum EpdDrawError error=EPD_DRAW_SUCCESS;
    if(dirty.height){error=update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,dirty);
        guard_draw_result(ctx->hl,error);}
    bool network=(status&5u)==5u,was_network=(s_quick_status&5u)==5u;
    if(error==EPD_DRAW_SUCCESS&&(((status^s_quick_status)&2u)||network!=was_network)){
        // 状态栏与Pico一样单独走灰阶，不让它把滑块局刷扩展到整个玻璃层。
        // Like Pico, refresh status separately in grayscale without expanding slider feedback over the glass layer.
        ui_quick_menu_draw_status(ctx->fb);
        error=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){0,0,UI_LOCK_WIDTH,76});
        guard_draw_result(ctx->hl,error);
    }
    if(error!=EPD_DRAW_SUCCESS){quick_reset();app_present(ctx,current,APP_REDRAW_FULL);return;}
#else
    if(error!=EPD_DRAW_SUCCESS){
        quick_reset();app_present(ctx,current,APP_REDRAW_FULL);return;
    }
#endif
    s_quick_status=status;s_quick_pressed=pressed;
}

static void quick_toggle_wifi(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (status.state != READ_PICO_TRANSFER_STOPPED) {
        // 上传期间拒绝断开，复用存储任务已有的原子准入保护。
        // Refuse disconnecting during upload using the storage owner's atomic admission guard.
        (void)read_pico_transfer_try_stop_if_idle();
    } else {
        char ssid[33] = {0}; bool configured = false;
        if (read_pico_transfer_get_saved_wifi(ssid, &configured) == ESP_OK && configured) {
#if KIIKO_BOARD_METALIO47
            // S31由原生无线共存仲裁，保留翻页器连接。/ Native S31 coexistence retains BLE.
#else
            // 先释放蓝牙占用，再用既有异步连接/重试流程；不新开上传服务器或改写凭据。
            // Release Bluetooth resources first, then reuse connection/retry handling without starting uploads or rewriting credentials.
            ble_pt_sync(false);
#endif
            const read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
            (void)read_pico_transfer_start(&cfg);
        }
    }
}
static bool main_page_ready(app_ctx_t* ctx, const app_desc_t* app);
static bool navigation_page_ready(app_ctx_t* ctx,const app_desc_t* app) {
    if(app->navigation_visible&&!app->navigation_visible(ctx))return false;
    return app->persistent_navigation || main_page_ready(ctx,app);
}
static bool navigation_input_ready(app_ctx_t* ctx,const app_desc_t* app) {
    return !ai_overlay_open() && navigation_page_ready(ctx,app) && (!app->navigation_enabled || app->navigation_enabled(ctx));
}

static void main_present_finished(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    if(redraw!=APP_REDRAW_NONE)s_presented_root=main_page_ready(ctx,app)?app:NULL;
    if (redraw == APP_REDRAW_DONE || !navigation_page_ready(ctx, app)) s_presented_main = NULL;
    else if (redraw != APP_REDRAW_NONE) s_presented_main = app;
    display_main_transition_disarm();
}

void app_lock_ignore_for(int64_t ms) {
    s_lock_ignore_until_ms = esp_timer_get_time() / 1000 + ms;
}

#if KIIKO_BOARD_METALIO47
static bool app_binary_ui(app_ctx_t *ctx,const app_desc_t *app) {
    return app->binary_ui && (!app->grayscale_ui || !app->grayscale_ui(ctx));
}
#endif
// 引导出口只接受已经持久化的完成/跳过，子页返回仍由引导拥有。
// Setup owns child returns and accepts global exits only after persisted Finish/Skip.
static void setup_navigation_guard(app_ctx_t *ctx,const app_desc_t *current){
    if(!current->setup_flow)return;
    ctx->request_return=false;ctx->request_menu=false;
    if(ctx->request_app&&!ctx->request_app->setup_flow&&app_setup_pending())ctx->request_app=NULL;
}
static void app_present_impl(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    // 文件根页/目录沿用主页擦白，其他结构变化保持原有底栏清理。
    // File hub/directory transitions reuse root white maintenance; other structural changes retain footer cleanup.
    const bool child_transition=redraw==APP_REDRAW_PAGE && s_presented_main==app &&
        (s_presented_root==app)!=main_page_ready(ctx,app);
    if(child_transition&&!app->white_child_transition)redraw=APP_REDRAW_FULL;
#ifdef KIIKO_LOCAL_LATENCY
    const int64_t probe_start=esp_timer_get_time();
    extern int64_t local_display_elapsed_us;
    const int64_t probe_display=local_display_elapsed_us;
#define LATENCY_PRESENT_DONE() do { if(redraw==APP_REDRAW_PAGE||redraw==APP_REDRAW_FULL) \
    ESP_LOGI("latency","PRESENT page=%d redraw=%d total_us=%lld display_us=%lld paint_us=%lld", \
    app_index_of(app),redraw,(long long)(esp_timer_get_time()-probe_start), \
    (long long)(local_display_elapsed_us-probe_display), \
    (long long)(esp_timer_get_time()-probe_start-(local_display_elapsed_us-probe_display))); } while(0)
#else
#define LATENCY_PRESENT_DONE() do {} while(0)
#endif
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
#if KIIKO_BOARD_METALIO47
    bool returning=ctx->request_back_ripple;
    const bool root_water=app_binary_ui(ctx,app) && s_presented_main && s_presented_main!=app && navigation_page_ready(ctx,app) &&
        app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER;
    if ((app_binary_ui(ctx,app) || returning) && (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)) {
        ctx->request_back_ripple = false;
        ui_click_feedback_reset();
        ui_click_feedback_begin(ctx->fb);
        display_main_transition_cancel();
        ui_text_set_system_scale(true);
        bool prepared=redraw==APP_REDRAW_PAGE?display_page_prepare_begin():false;
        if (app->render) app->render(ctx, ctx->fb);
        // 动画和高层区域都使用旋转后逻辑坐标；物理全屏会触发裁剪错误与整屏恢复。
        // Animation/high-level areas use rotated logical coordinates; a physical full screen causes crop failure and full recovery.
        const EpdRect screen={0,0,epd_rotated_display_width(),
            navigation_page_ready(ctx,app)?UI_NAV_MARKER_SCAN_END:epd_rotated_display_height()};
        result = redraw == APP_REDRAW_FULL ? update_display_full(ctx->hl) : returning || root_water
            ? (app->grayscale_ui&&app->grayscale_ui(ctx)
                ? update_display_main_turn(ctx->hl,screen,returning?E0470_TURN_LTR:E0470_TURN_RTL)
                : update_display_water_turn(ctx->hl,screen,returning?E0470_TURN_LTR:E0470_TURN_RTL))
            : update_display_area_diff_with(ctx->hl,display_main_bw_waveform(),MODE_DU,screen);
        if(redraw==APP_REDRAW_PAGE)display_page_prepare_end(prepared);
        guard_draw_result(ctx->hl, result);
        main_present_finished(ctx, app, redraw);
        LATENCY_PRESENT_DONE();
        return;
    }
#else
    if(ctx->request_back_ripple && (redraw==APP_REDRAW_PAGE || redraw==APP_REDRAW_FULL)) {
        // 返回目标只合成一次，跳过旧控件复位扫描；显式全刷优先，水波纹沿用Pico原有入口。
        // Compose the destination once without an old-control restore scan; explicit full refresh wins, otherwise reuse Pico water.
        ctx->request_back_ripple=false;
        ui_click_feedback_reset();ui_click_feedback_begin(ctx->fb);
        display_main_transition_cancel();ui_text_set_system_scale(true);
        if(app->render)app->render(ctx,ctx->fb);
        result=redraw==APP_REDRAW_FULL?update_display_full(ctx->hl):
            update_display_main_turn(ctx->hl,(EpdRect){0,0,epd_rotated_display_width(),
                navigation_page_ready(ctx,app)?UI_NAV_MARKER_SCAN_END:epd_rotated_display_height()},E0470_TURN_LTR);
        guard_draw_result(ctx->hl,result);main_present_finished(ctx,app,redraw);
        LATENCY_PRESENT_DONE();return;
    }
#endif
    if (redraw == APP_REDRAW_PAGE && ui_click_feedback_active() && !ui_click_feedback_nav_pending()) {
        // 切页固定底栏之前先复位真实按压像素，避免把放大的图标当作长期基准。
        // Restore physical pressed pixels before freezing navigation, so an enlarged icon never becomes its baseline.
        EpdRect area;
        if (ui_click_feedback_release(ctx->fb, &area)) {
            display_main_transition_cancel();
            result = update_display_area_diff_with(ctx->hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area);
            guard_draw_result(ctx->hl, result);
            if (result != EPD_DRAW_SUCCESS) redraw = APP_REDRAW_FULL;
        }
    }
    if(redraw==APP_REDRAW_FULL || (redraw==APP_REDRAW_PAGE && !navigation_page_ready(ctx,app)))
        ui_click_feedback_reset();
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) ui_click_feedback_begin(ctx->fb);
    ui_text_set_system_scale(true);
    // 后台目录/封面完成及滚动仍属于当前主页面，不能借普通整页出口闪到底栏。
    // Background directory/cover completion and scrolling remain on this main page and must not flash navigation via ordinary page output.
    if ((redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_AREA) &&
        s_presented_main == app && navigation_page_ready(ctx, app)
#if KIIKO_BOARD_METALIO47
        && !app_binary_ui(ctx,app)
#endif
        ) display_main_transition_arm(app, false, child_transition&&app->white_child_transition);
#if KIIKO_BOARD_METALIO47
    if(!(app->grayscale_ui&&app->grayscale_ui(ctx))&&navigation_page_ready(ctx,app)&&(app_index_of(app)==2||app_index_of(app)==3) &&
       app_settings_main_refresh_mode()!=APP_MAIN_REFRESH_WATER)display_main_transition_monochrome();
    const bool prepare_page=redraw==APP_REDRAW_PAGE&&navigation_page_ready(ctx,app);
    const bool prepare_held=prepare_page?display_page_prepare_begin():false;
#endif
    if (app->present != NULL && app->present(ctx, redraw)) {
#if KIIKO_BOARD_METALIO47
        if(prepare_page)display_page_prepare_end(prepare_held);
#endif
        main_present_finished(ctx, app, redraw);
        LATENCY_PRESENT_DONE();
        return;
    }
    switch (redraw) {
        case APP_REDRAW_NONE:
        case APP_REDRAW_DONE:
            main_present_finished(ctx, app, redraw);
            return;
        case APP_REDRAW_AREA: {
            // 回调已经画好了 fb，这里只负责把它那一块推上屏。
            // The callback already painted fb; push that rectangle only.
            EpdRect area = app->area_hint != NULL
                ? app->area_hint(ctx)
                : ui_content_refresh_area();
            result = update_display_area_with(
                ctx->hl, &E0470_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area
            );
            break;
        }
        case APP_REDRAW_FULL:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = APP_PAGE_FORCE_FULL
                ? update_display_full(ctx->hl)
                : update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
        case APP_REDRAW_PAGE:
        default:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = update_display_fast_page(ctx->hl);
            break;
    }
    guard_draw_result(ctx->hl, result);
#if KIIKO_BOARD_METALIO47
    if(prepare_page)display_page_prepare_end(prepare_held);
#endif
    main_present_finished(ctx, app, redraw);
    LATENCY_PRESENT_DONE();
#undef LATENCY_PRESENT_DONE
}

// 将系统页策略限制在实际推屏作用域；下拉玻璃、锁屏及阅读不继承它。
// Scope system policy to presentation so quick-menu glass, lock and reader do not inherit it.
void app_present(app_ctx_t* ctx,const app_desc_t* app,app_redraw_t redraw) {
    const bool root_switch=redraw==APP_REDRAW_PAGE &&
        s_presented_root && s_presented_root!=app && main_page_ready(ctx,app);
    const bool child_white=redraw==APP_REDRAW_PAGE && app->white_child_transition &&
        s_presented_main==app && (s_presented_root==app)!=main_page_ready(ctx,app);
    const bool white_switch=root_switch||child_white;
    const bool shelf_switch=root_switch &&
        (app_index_of(s_presented_root)==1 || app_index_of(app)==1);
    unsigned prior_quality=display_main_quality_set(white_switch?(shelf_switch?8u:6u):0u);
    bool prior_fast_background=display_main_fast_background_set(root_switch &&
        app_settings_main_refresh_mode()==APP_MAIN_REFRESH_FAST);
#if KIIKO_BOARD_METALIO47
    bool prior_erase=display_main_switch_erase_set(white_switch);
    bool prior_home_erase=display_main_shelf_home_erase_set(root_switch &&
        app_index_of(s_presented_root)==1 && app_index_of(app)==0);
    bool previous=display_system_binary_set(app_binary_ui(ctx,app));
    bool previous_nav=display_system_navigation_set(navigation_page_ready(ctx,app));
#endif
    app_present_impl(ctx,app,redraw);
#if KIIKO_BOARD_METALIO47
    display_system_navigation_set(previous_nav);
    display_system_binary_set(previous);
    display_main_switch_erase_set(prior_erase);
    display_main_shelf_home_erase_set(prior_home_erase);
#endif
    display_main_fast_background_set(prior_fast_background);
    display_main_quality_set(prior_quality);
}

// 反馈只推控件附近，失败后重新绘制当前页面；不占用底栏切页许可。
// Push feedback only near its control, repainting the current page on failure without consuming navigation permission.
static void click_present(app_ctx_t *ctx, const app_desc_t *current, EpdRect area) {
#if KIIKO_BOARD_METALIO47
    if(app_binary_ui(ctx,current)&&navigation_page_ready(ctx,current)&&(app_index_of(current)==2||app_index_of(current)==3))display_monochrome_content(ctx->fb);
#endif
#if KIIKO_BOARD_METALIO47
    bool previous=display_system_binary_set(app_binary_ui(ctx,current));
    bool previous_nav=display_system_navigation_set(navigation_page_ready(ctx,current));
#endif
    enum EpdDrawError error = update_display_click_feedback(ctx->hl,area);
    guard_draw_result(ctx->hl, error);
    if (error != EPD_DRAW_SUCCESS) app_present(ctx, current, APP_REDRAW_FULL);
#if KIIKO_BOARD_METALIO47
    display_system_navigation_set(previous_nav);
    display_system_binary_set(previous);
#endif
}

// 中断边界先清页面装饰再清识别器。/ Clear decoration before resetting recognition.
static void cancel_gesture(app_ctx_t* ctx, const app_desc_t* app, ui_gesture_t* gesture) {
    if (gesture->active && app->on_gesture) {
        ui_gesture_event_t event = { .type = UI_GESTURE_CANCEL,
            .x0 = gesture->x0, .y0 = gesture->y0, .x = gesture->x, .y = gesture->y };
        const app_desc_t* requested = ctx->request_app;
        bool menu_requested = ctx->request_menu;
        bool return_requested = ctx->request_return;
        app_redraw_t redraw = app->on_gesture(ctx, &event);
        ctx->request_app = requested;
        ctx->request_menu = menu_requested;
        ctx->request_return = return_requested;
        ui_gesture_reset(gesture);
        app_present(ctx, app, redraw);
    }
    ui_gesture_reset(gesture);
}

static void present_page(app_ctx_t* ctx, const app_desc_t* app,
                         ui_gesture_t* gesture, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)
        cancel_gesture(ctx, app, gesture);
    app_present(ctx, app, redraw);
}

// 内容任意横向起点沿用翻页识别；状态栏、底栏和明确接管滑动的控件保留原语义。
// Reuse reading recognition from any horizontal content origin, retaining status/navigation and control-owned gestures.
static bool edge_back_contact(app_ctx_t *ctx,const app_desc_t *app,const ui_gesture_event_t *event) {
    return app->on_key && app->edge_back_enabled && app->edge_back_enabled(ctx) &&
        event->x0 < UI_LOCK_WIDTH && event->y0 >= 76 &&
        event->y0 < (navigation_page_ready(ctx,app)?UI_NAV_TOUCH_TOP:UI_LOCK_HEIGHT) &&
        (!app->back_swipe_area || app->back_swipe_area(ctx,event));
}
static app_redraw_t dispatch_page_gesture(app_ctx_t *ctx,const app_desc_t *app,const ui_gesture_event_t *event) {
    if (event->type == UI_GESTURE_SWIPE_R && edge_back_contact(ctx,app,event)) {
        ui_click_feedback_reset();
        ctx->request_back_ripple=true;
        app_redraw_t redraw=app->on_key(ctx,UI_KEY_1);
        if(redraw!=APP_REDRAW_PAGE && redraw!=APP_REDRAW_FULL && !ctx->request_return && !ctx->request_app)
            ctx->request_back_ripple=false;
        return redraw;
    }
    return app->on_gesture(ctx,event);
}

typedef struct {
    unsigned updates;
    int64_t last_ms;
    EpdRect area;
} menu_feedback_t;

// 菜单反馈只推叶内单行。/ Menu feedback pushes one local row only.
static void menu_feedback(app_ctx_t* ctx, const app_desc_t* current,
                          int leaf, int hit, bool pressed, menu_feedback_t* feedback) {
    EpdRect area;
    int row = hit - leaf * UI_MENU_ITEMS_PER_PAGE;
    if (!ui_menu_row_rect(leaf, row, &area)) return;
    ui_draw_menu_row_pressed(ctx->fb, current, leaf, row, pressed);
    if (feedback->updates == 0) feedback->area = area;
    else {
        int bottom = feedback->area.y + feedback->area.height;
        int next_bottom = area.y + area.height;
        if (area.y < feedback->area.y) feedback->area.y = area.y;
        feedback->area.height = (bottom > next_bottom ? bottom : next_bottom) - feedback->area.y;
    }
    feedback->updates++;
    feedback->last_ms = ctx->now_ms;
    guard_draw_result(ctx->hl, update_display_area_with(
        ctx->hl, &E0470_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area));
    if (feedback->updates >= UI_SETTLE_DU_MAX) {
        guard_draw_result(ctx->hl, update_display_area_with(
            ctx->hl, &E0470_WAVEFORM, MODE_GL16, feedback->area));
        feedback->updates = 0;
    }
}

// 切页：先让上一页收尾，再给新页一次 on_enter，最后整页画出来。
// Leave the old page, enter the new one, then present a full page.
static bool main_page_ready(app_ctx_t* ctx, const app_desc_t* app) {
    int index = app_index_of(app);
    return index >= 0 && index < 4 &&
           app->main_page_visible && app->main_page_visible(ctx);
}

// 主页面左右键循环切页；中键短按交给页面，长按统一全刷。阅读及子页继续使用原映射。
// Main roots cycle with side keys, delegate a middle tap, and share middle-hold full refresh. Reader/subpage mappings stay page-owned.
static app_redraw_t main_key_action(app_ctx_t *ctx,const app_desc_t *current,int key,bool held) {
    if(held)return key==UI_KEY_2?APP_REDRAW_FULL:APP_REDRAW_NONE;
    app_redraw_t redraw=APP_REDRAW_NONE;
    if(current->on_main_key && current->on_main_key(ctx,key,&redraw))return redraw;
    int tab=app_index_of(current);
    if(tab>=0 && tab<4 && (key==UI_KEY_1||key==UI_KEY_3))
        ui_nav_request(ctx,(tab+(key==UI_KEY_1?3:1))%4);
    return APP_REDRAW_NONE;
}

#if KIIKO_BOARD_METALIO47
// 同一主键路径保留书架分页模式与导航保护。/ Share root keys, including shelf paging and navigation guards.
static app_redraw_t volume_key_action(app_ctx_t *ctx,const app_desc_t *current,int volume) {
    if(!volume || ctx->request_app || ctx->request_menu || ctx->request_return ||
       usb_storage_active() || pico_online_busy())return APP_REDRAW_NONE;
    int key=volume<0?UI_KEY_1:UI_KEY_3;
    if(main_page_ready(ctx,current))return main_key_action(ctx,current,key,false);
    app_redraw_t redraw = APP_REDRAW_NONE;
    if(current==&app_book && app_book_volume_key(ctx,volume<0?-1:1,&redraw))return redraw;
    return current->owns_keys && current->on_key?current->on_key(ctx,key):APP_REDRAW_NONE;
}
#endif

#if KIIKO_BOARD_METALIO47
// 所有控件先合成；展开保留有界灰阶，收回以一次完整底页清理消除边框，不叠加第二轮。
// Compose first: open uses bounded grays; close cleans the restored page once without another output.
static void ai_push(app_ctx_t *ctx,const app_desc_t *current,EpdRect area){
    display_main_transition_cancel();
    bool binary=display_system_binary_set(false);
    bool nav=display_system_navigation_set(false);
    int64_t at=esp_timer_get_time();(void)at;
    enum EpdDrawError result=s_ai.open?update_display_gray_transition(ctx->hl,area):update_display_full(ctx->hl);
    display_system_navigation_set(nav);display_system_binary_set(binary);
    ESP_LOGI(TAG,"AI menu display=%lldms result=%d",(long long)((esp_timer_get_time()-at)/1000),(int)result);
    guard_draw_result(ctx->hl,result);
    if(result!=EPD_DRAW_SUCCESS){
        (void)ui_ai_menu_restore(&s_ai,ctx->fb);ui_ai_menu_end(&s_ai);
        app_present(ctx,current,APP_REDRAW_FULL);
    }
}
static void ai_dismiss(app_ctx_t *ctx,const app_desc_t *current,bool present){
    if(!s_ai.open)return;
    EpdRect rect=s_ai.rect;bool restored=ui_ai_menu_restore(&s_ai,ctx->fb);ui_ai_menu_end(&s_ai);
    if(!restored){if(present)app_present(ctx,current,APP_REDRAW_FULL);else if(current->render)current->render(ctx,ctx->fb);}
    else if(present)ai_push(ctx,current,rect);
}
static app_redraw_t ai_short_action(app_ctx_t *ctx,const app_desc_t *current){
    app_ai_action_t action=app_settings_ai_short();
    if(action==APP_AI_NONE)return APP_REDRAW_NONE;
    if(action==APP_AI_BOOKMARK){
        if(current!=&app_book || !app_book_reader_body_visible())return APP_REDRAW_NONE;
        if(app_book_ai_bookmark())(void)metalio_haptic_click();
        return APP_REDRAW_PAGE;
    }
    (void)metalio_haptic_click();
    if(action==APP_AI_REFRESH)return APP_REDRAW_FULL;
    if(action==APP_AI_FRONTLIGHT){metalio_frontlight_toggle();return APP_REDRAW_NONE;}
    if(action==APP_AI_HOME)ui_nav_request(ctx,0);
    return APP_REDRAW_NONE;
}
#endif

static void switch_to(
    app_ctx_t* ctx, const app_desc_t** current, const app_desc_t* next
) {
    if (!next || (next==*current && (!next->persistent_navigation || main_page_ready(ctx,next)))) return;
#if defined(KIIKO_LOCAL_LATENCY) || KIIKO_BOARD_METALIO47
    const int64_t probe_start=esp_timer_get_time();
    const int probe_from=app_index_of(*current);
#endif
    bool main_source = navigation_page_ready(ctx, *current);
    bool shelf_source = main_source && app_index_of(*current) == 1;
    display_main_transition_cancel();
    if ((*current)->on_exit != NULL) (*current)->on_exit(ctx);
#if defined(KIIKO_LOCAL_LATENCY) || KIIKO_BOARD_METALIO47
    const int64_t probe_exit=esp_timer_get_time();
#endif
    *current = next;
    app_font_activate_system();
#if defined(KIIKO_LOCAL_LATENCY) || KIIKO_BOARD_METALIO47
    const int64_t probe_font=esp_timer_get_time();
#endif
    if (next->on_enter != NULL) next->on_enter(ctx);
#if defined(KIIKO_LOCAL_LATENCY) || KIIKO_BOARD_METALIO47
    const int64_t probe_enter=esp_timer_get_time();
#endif
    if (main_source && navigation_page_ready(ctx, next)) display_main_transition_arm(next, shelf_source, true);
    app_present(ctx, next, next->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
    display_main_transition_disarm();
#ifdef KIIKO_LOCAL_LATENCY
    if(local_probe_contact_start_us){
        ESP_LOGI("latency","CONTACT to=%d total_us=%lld",app_index_of(next),(long long)(esp_timer_get_time()-local_probe_contact_start_us));
        local_probe_contact_start_us=0;
    }
#endif
#if defined(KIIKO_LOCAL_LATENCY) || KIIKO_BOARD_METALIO47
    ESP_LOGI("latency","SWITCH from=%d to=%d mode=%d exit_us=%lld font_us=%lld enter_us=%lld present_us=%lld total_us=%lld",probe_from,app_index_of(next),app_settings_main_refresh_mode(),
        (long long)(probe_exit-probe_start),(long long)(probe_font-probe_exit),(long long)(probe_enter-probe_font),
        (long long)(esp_timer_get_time()-probe_enter),(long long)(esp_timer_get_time()-probe_start));
#endif
    ESP_LOGI(TAG, "page -> %s", next->title);
}

// 菜单页不是 app，单独画。/ The menu is not an app; present it here.
static void present_menu(app_ctx_t* ctx, const app_desc_t* current, int leaf, menu_feedback_t* feedback) {
    s_presented_main = NULL;
    feedback->updates = 0;
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
    ui_draw_menu_page(ctx->fb, current, leaf);
    guard_draw_result(ctx->hl, update_display_fast_page(ctx->hl));
}

// 空槽常带着抬起事件或残留坐标，不能进页面看到的快照；抬手后保留最后一次位置。
// Empty slots often carry lift events or stale coords; keep last position on lift.
static void debounce_touch(
    cst836u_touch_t* latest, const cst836u_touch_t* raw, bool released
) {
    if (raw->touched) {
        *latest = *raw;
        for (int i = 0; i < CST836U_MAX_POINTS; i++) {
            if (!latest->points[i].active) {
                memset(&latest->points[i], 0, sizeof(latest->points[i]));
            }
        }
    } else if (released) {
        latest->touched = false;
        latest->count = 0;
        for (int i = 0; i < CST836U_MAX_POINTS; i++) {
            latest->points[i].active = false;
        }
    }
    memcpy(latest->raw, raw->raw, sizeof(raw->raw));
}

static bool try_load_saved_sd_font(bool allow_probe) {
#if KIIKO_BOARD_METALIO47
    if (app_sd_recovery_busy()) return false;
    if (!app_sleep_media_ready()) return false;
#endif
    read_pico_sd_info_t info;
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    if (!info.mounted) {
        if (allow_probe && err == ESP_ERR_INVALID_STATE) read_pico_sd_start_probe();
        return false;
    }
    return app_font_retry_active();
}

static int64_t s_media_recovery_after_ms;
static unsigned s_media_recovery_attempts;
// 页面已释放句柄后才排队恢复；缺卡等待稳定插入，持续故障按1/3/10/30秒退避。
// Queue recovery only after page handles close; wait for insertion and back off persistent faults by 1/3/10/30 seconds.
static void recover_media(app_ctx_t *ctx, const read_pico_sd_info_t *info) {
    if (s_media_recovery_attempts && !info->present) {
        s_media_recovery_after_ms = ctx->now_ms; return;
    }
    if (!read_pico_sd_needs_recovery() || ctx->now_ms < s_media_recovery_after_ms || pico_online_busy()) return;
    read_pico_transfer_status_t transfer = {0};
    read_pico_transfer_get_status(&transfer);
    if (transfer.state == READ_PICO_TRANSFER_UPLOADING) return;
    bool covers_ready = book_cover_jobs_quiesce(0);
    bool avatar_ready = ui_avatar_quiesce(0);
    if (!covers_ready || !avatar_ready) return;
    esp_err_t err = read_pico_sd_start_recovery();
    if (err == ESP_ERR_NOT_FINISHED) return;
    static const unsigned delays[] = {1000,3000,10000,30000};
    unsigned index = s_media_recovery_attempts < 3 ? s_media_recovery_attempts : 3;
    s_media_recovery_after_ms = ctx->now_ms + delays[index];
    if (s_media_recovery_attempts < 4) ++s_media_recovery_attempts;
}
// 先停文件使用者和字体；Pico在后续轮询中等任务释放，再异步重挂。
// Stop page/font consumers first; Pico waits for worker release on later polls before asynchronous remount.
static bool poll_media(app_ctx_t* ctx, const app_desc_t* current,
                       bool* mounted, bool* invalidated) {
#if KIIKO_BOARD_METALIO47
    if (app_sd_recovery_busy()) return false;
    static bool wake_pending;
    static bool wake_consumers_closed;
    if (!app_sleep_media_ready()) {
        wake_pending=true;
        // 挂卡优先：字体/书籍恢复失败时仍让健康检查运行；挂载任务期间不抢生命周期。
        // Media comes first: failed reopen must not suppress health checks or steal a mount task.
        if (usb_storage_active() || pico_online_busy() || app_download_active()) return false;
        read_pico_transfer_status_t owner={0};
        online_font_status_t download={0};
        read_pico_transfer_get_status(&owner);
        online_font_status(&download);
        if (owner.state != READ_PICO_TRANSFER_STOPPED || download.busy) return false;
        read_pico_sd_poll_health();
        if (!app_sleep_media_failed()) return false;
        bool covers=book_cover_jobs_quiesce(0), avatar=ui_avatar_quiesce(0);
        if (!covers || !avatar) return false;
        bool changed=!wake_consumers_closed;
        if (changed) {
            // 部分句柄可能已恢复，必须全部释放，不能带着旧VFS句柄重挂。
            // Partial resumes may own live descriptors: close all before replacing the VFS session.
            if (current->on_media_lost) current->on_media_lost(ctx);
            book_close();ttf_font_close_embedded();ttf_font_unload();
            (void)ttf_font_open_builtin();ui_text_set_system_font(false);
            ui_avatar_invalidate();book_lock_collage_invalidate();
            *mounted=false;*invalidated=true;
            wake_consumers_closed=true;
        }
        if (read_pico_sd_invalidate_io() == ESP_OK) {
            app_sleep_discard_media_io();
            wake_consumers_closed=false;
            s_media_recovery_after_ms=0;s_media_recovery_attempts=0;
        }
        return changed;
    }
    // 句柄恢复不一定改变挂载/字体路径，也必须补一次完整页面。
    // Resuming handles may leave mount/font identity unchanged; still repaint the page once.
    bool wake_restored=wake_pending;
    wake_pending=false;
    wake_consumers_closed=false;
#endif
    if (usb_storage_active()) {
        s_media_recovery_attempts = 0; s_media_recovery_after_ms = 0;
        *mounted = false; *invalidated = false; return false;
    }
    read_pico_transfer_status_t health_transfer = {0};
    read_pico_transfer_get_status(&health_transfer);
    if (!pico_online_busy() && health_transfer.state != READ_PICO_TRANSFER_UPLOADING)
        read_pico_sd_poll_health();
    read_pico_sd_info_t info = {0};
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_INVALID_STATE && info.present && !*invalidated && !read_pico_sd_needs_recovery()) {
        esp_err_t probe = read_pico_sd_start_probe();
        if (probe == ESP_ERR_NOT_FINISHED) return false;
    }
    if (err == ESP_ERR_NOT_FINISHED) return false;
    bool ready = err == ESP_OK && info.mounted;
    bool lost = *mounted && !ready;
    bool gained = !*mounted && ready;
    *mounted = ready;
    if (ready) *invalidated = false;
    if (ready) { s_media_recovery_attempts = 0; s_media_recovery_after_ms = 0; }
    if (gained || lost) { ui_avatar_invalidate(); (void)book_cover_jobs_quiesce(0); book_lock_collage_invalidate(); }
    if (gained && current->on_media_ready) current->on_media_ready(ctx);
#if KIIKO_BOARD_METALIO47
    bool font_restored=gained && app_font_recover_media();
#endif
    if (!lost) {
        if (!ready) recover_media(ctx,&info);
#if KIIKO_BOARD_METALIO47
        if(wake_restored && ready) {
            // 新的唤醒IO会话已验证，清除上一会话的字体拒绝再恢复选择。
            // Verified wake IO starts a new session; clear old font rejection before restoring intent.
            if(!gained)(void)app_font_recover_media();
            if(!gained && current->on_media_ready)current->on_media_ready(ctx);
            return true;
        }
        return font_restored;
#else
        return false;
#endif
    }
    *invalidated = true;
    if (current->on_media_lost) current->on_media_lost(ctx);
    if (ttf_font_ready() && !ttf_font_is_builtin()) ttf_font_open_builtin();
    ui_text_set_system_font(false);
    return true;
}

// 电源动作先保存当前页面，再定稿一张不带时钟的静态画面；避免关机后留下会误读的冻结时间。
// Save the current page first, then settle a clock-free static frame so shutdown never leaves a misleading frozen time.
// 锁屏前先由页面保存进度，再持久化一次恢复上下文；不在翻页或点击时写入。
// Flush page progress before persisting one lock checkpoint; never write it on page turns or taps.
static void prepare_lock(app_ctx_t *ctx, const app_desc_t *current, bool feedback) {
#if KIIKO_BOARD_METALIO47
    const int64_t started_at = esp_timer_get_time();
#endif
    if (feedback) app_lock_feedback(ctx->hl);
    if (current->on_before_lock) current->on_before_lock(ctx);
    int tab = app_index_of(current);
    pico_resume_t resume = {.tab = tab >= 0 && tab < 4 ? (uint8_t)tab : 0};
    bool fullscreen = false;
    if (tab == 1 && app_book_resume_context(resume.path, sizeof(resume.path), &fullscreen)) {
        resume.reader = 1;
        resume.fullscreen = fullscreen;
    }
#if KIIKO_BOARD_METALIO47
    app_lock_set_resume_ready(pico_boot_save_resume(&resume) == ESP_OK);
    ESP_LOGI(TAG, "S31 lock checkpoint_ms=%lld", (long long)((esp_timer_get_time() - started_at) / 1000));
    (void)started_at;
#else
    (void)pico_boot_save_resume(&resume);
#endif
}
static void run_power_action(app_ctx_t *ctx, const app_desc_t *current, bool restart) {
    prepare_lock(ctx, current, false);
    ui_power_final_draw(ctx->fb, restart);
    guard_draw_result(ctx->hl, update_display_full(ctx->hl));
    app_lock_wait_key_idle(800);
    epd_poweroff();
    pico_boot_clear_resume();
    if (restart) app_restart_host();
    esp_err_t error = app_enter_host_sleep(APP_SLEEP_OFF);
    if (error != ESP_OK) ESP_LOGW(TAG, "power off failed: %s", esp_err_to_name(error));
    // 关机交接失败时回到可操作原页；不关闭书籍后再重新自动开书。/ A failed shutdown returns to the operable current page without closing/reopening the book.
    app_present(ctx, current, APP_REDRAW_PAGE);
}

bool app_poll_main_navigation(app_ctx_t *ctx) {
    if(!ctx||!ctx->tp)return false;
    if(ctx->request_app)return true;
    cst836u_touch_t touch={0};
    if(cst836u_read(ctx->tp,&touch)!=ESP_OK||!touch.touched||touch.count!=1) {
        s_nav_contact_down=s_nav_polled=false;return false;
    }
    int index=ui_nav_hit(touch.x,touch.y);
    if(index<0)return false;
    if(!s_nav_contact_down) {
        s_nav_contact_down=s_nav_polled=true;
#if KIIKO_BOARD_METALIO47
        (void)metalio_haptic_click();
        if(index!=1)ui_nav_request(ctx,index);
#else
        if(index!=1)ui_nav_request(ctx,index);
#endif
        ctx->user_activity=true;
        if(ctx->fb&&ctx->hl) {
            EpdRect area;ui_click_feedback_nav_request(index);
            if(ui_click_feedback_nav_tick(ctx->fb,esp_timer_get_time()/1000,&area))
                click_present(ctx,&app_book,area);
        }
    }
    return ctx->request_app!=NULL;
}

void app_loop_run(const app_loop_config_t* config) {
    // 记忆为开启时，开机也排队重连；栈启动仍由原有无线调度控制。/ Queue startup reconnect for a persisted enabled setting; retain existing radio lifecycle control.
    if(app_settings_ble_turner())ble_pt_reconnect_recent();
    s_media_recovery_attempts = 0; s_media_recovery_after_ms = 0;
    s_presented_main = NULL;
    s_nav_contact_down=s_nav_polled=false;
    const app_desc_t* current = config->first_app != NULL
        ? config->first_app
        : app_home_page();

    cst836u_info_t touch_info = { 0 };
    cst836u_get_info(config->tp, &touch_info);
    cst836u_touch_t latest = { 0 };

    app_ctx_t ctx = {
        .hl = config->hl,
        .fb = config->fb,
        .acc = config->acc,
        .tp = config->tp,
        .sensor_ready = config->sensor_ready,
        .continuous_ready = continuous_du_init(),
        .touch = &latest,
        .touch_info = &touch_info,
    };

    bool was_touched = false;
    quick_reset();
    bool menu_open = false;
    ui_gesture_t gesture = {0};
    bool pinching = false, pinch_input_owned = false, pinch_drain = false;
    ui_power_dialog_t power_dialog = {0};
#if KIIKO_BOARD_METALIO47
    bool power_dialog_open = false; ui_power_dialog_end();
#else
    bool power_dialog_open = false;
#endif
    int menu_pressed = UI_MENU_HIT_NONE;
    menu_feedback_t feedback = {0};
    int menu_leaf = 0;
    const app_desc_t* return_app = NULL;
    int return_leaf = 0, return_menu_leaf = 0;
    bool return_to_menu = false;
    int held_key = -1;
    bool held_main_key=false;
    int64_t held_since_ms = 0;
    int64_t last_lock_poll_ms = 0;
    int64_t last_font_retry_ms = 0;
    int64_t last_media_poll_ms = 0;
    int64_t last_network_poll_ms = 0;
    int64_t last_input_ms = esp_timer_get_time() / 1000;
    uint32_t last_ble_input = ble_pt_input_serial();
    unsigned idle_minutes = app_settings_auto_lock_minutes();
    unsigned cover_policy = (unsigned)app_settings_main_refresh_mode() | ((unsigned)app_settings_system_contrast() << 8);
    read_pico_sd_info_t initial_media = {0};
    (void)read_pico_sd_get_info(&initial_media);
    bool media_mounted = initial_media.mounted || (ttf_font_ready() && !ttf_font_is_builtin());
    bool media_invalidated = false;
    app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);

    // The SD probe starts during board setup and is usually ready by the first product frame.
    // Retry here so the very first bookshelf paint already has a full CJK system face.
    try_load_saved_sd_font(false);
    rp_boot_mark("sdfont");
    if (current->on_enter != NULL) current->on_enter(&ctx);
    rp_boot_mark("enter");
    poll_media(&ctx, current, &media_mounted, &media_invalidated);
    // 首帧必须整屏 GC16：fb 里还是 app_main 画的开机图，DU 盖不掉。
    // First frame must be full GC16: fb still holds the splash; DU cannot cover it.
    app_present(&ctx, current, APP_REDRAW_FULL);
    rp_boot_mark("frame");
    pico_boot_ready();
    ESP_LOGI(TAG, "UI ready on %s", current->title);
    // 上次崩溃的现场（异常类型与触发地址）：有的话打一遍，串口和屏幕各有一份。
    // The previous panic scene, when there was one: logged here, and shown on the home screen.
    crash_note_log();
#ifdef KIIKO_LOCAL_LATENCY
    local_latency_init();
#endif

    while (true) {
        unsigned requested_cover_policy = (unsigned)app_settings_main_refresh_mode() | ((unsigned)app_settings_system_contrast() << 8);
        if (requested_cover_policy != cover_policy) {
            app_home_cover_mode_changed(); app_book_cover_mode_changed();
            cover_policy = requested_cover_policy;
        }
        cst836u_touch_t touch = { 0 };
        esp_err_t err = cst836u_read(config->tp, &touch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CST836U read failed: %s", esp_err_to_name(err));
            memset(&touch, 0, sizeof(touch));
        }

        if(err!=ESP_OK||!touch.touched)s_nav_contact_down=s_nav_polled=false;
        const bool pressed = touch.touched && !was_touched;
        const bool released = !touch.touched && was_touched;
#if KIIKO_BOARD_METALIO47
        // 三键反馈在覆盖层之前统一接收，按住/长按/抬手不重复；不依赖页面动作。
        // Receive bottom-key feedback before overlays; holds/releases never repeat it or depend on page actions.
        if(err==ESP_OK&&pressed&&touch.count==1&&ui_key_hit_test(touch.x,touch.y)>=0)
            (void)metalio_haptic_click();
#endif
        debounce_touch(&latest, &touch, released);
        ctx.now_ms = esp_timer_get_time() / 1000;
#if KIIKO_BOARD_METALIO47
        // S31无后台CW32快照，轮询BQ/充电器（适配器内部每2秒限流）。
        // S31 polls its gauge/charger; the adapter rate-limits I2C to once per two seconds.
        if (read_pico_pmu_ready()) (void)read_pico_pmu_poll();
#else
#endif
        if (err == ESP_OK && (touch.touched || released)) last_input_ms = ctx.now_ms;
        if (ctx.now_ms - last_network_poll_ms >= 500) {
            last_network_poll_ms = ctx.now_ms;
            read_pico_transfer_service_poll();
            app_time_service_poll(!touch.touched &&
                (ctx.now_ms-last_input_ms>=10000||(current->setup_flow&&app_time_service_online_sync_pending())) &&
#if KIIKO_BOARD_METALIO47
                !app_sd_recovery_busy() && !current->holds_pmu && !usb_storage_active() && !app_download_active() && !pico_online_busy() && !ble_pt_connected());
#else
                !current->holds_pmu && !usb_storage_active() && !pico_online_busy() && !ble_pt_connected());
#endif
        }
        ctx.pressed = pressed;
        ctx.released = released;
        ctx.consumed = false;
        // A load may have captured navigation after the prior switch. Consume it at the normal boundary.

        ctx.request_menu = false;
        ctx.request_return = false;
        ctx.request_back_ripple = false;
        bool selected_from_menu = false;
        const bool power_input_owned = power_dialog_open;
        bool quick_lock_requested = false;
        bool quick_input_owned = false;

#if KIIKO_BOARD_METALIO47
        // Manual recovery owns SD/font lifecycle. Keep its page live, but do not reopen files,
        // start an automatic remount, navigate, draw overlays or sleep while its worker owns VFS.
        if (app_sd_recovery_busy()) {
            held_key=-1;held_main_key=false;
            ui_click_feedback_reset();
            cancel_gesture(&ctx,current,&gesture);
            (void)metalio_take_ai();(void)metalio_take_volume();
            read_pico_pmu_drain_events();
            last_input_ms=ctx.now_ms;last_font_retry_ms=ctx.now_ms;
            if(current->on_tick)app_present(&ctx,current,current->on_tick(&ctx));
            was_touched=touch.touched;
            vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
            continue;
        }
#endif

        if (ctx.now_ms - last_media_poll_ms >= MEDIA_POLL_INTERVAL_MS) {
            last_media_poll_ms = ctx.now_ms;
            if (poll_media(&ctx, current, &media_mounted, &media_invalidated)) {
                power_dialog_open = false;
                ui_power_dialog_end();
                ui_power_dialog_reset(&power_dialog, touch.touched);
                quick_reset();
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                menu_pressed = UI_MENU_HIT_NONE;
                ctx.consumed = true;
                ctx.pressed = ctx.released = false;
                was_touched = touch.touched;
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else app_present(&ctx, current, APP_REDRAW_FULL);
                // 本轮旧输入不再触发动作，下一轮恢复正常事件处理。
                // Discard this tick's stale input, then resume normal dispatch next tick.
                vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                continue;
            }
        }

#if KIIKO_BOARD_METALIO47
        // 实体AI输入先于页面触摸派发；覆盖时冻结底页，关层的抬手也不可穿透。
        // Route physical AI before page touches; freeze the underlay and consume the closing release.
        bool ai_owned=s_ai.open;
        int ai_event=metalio_take_ai();
        if(ai_event || s_ai.open){
            read_pico_transfer_status_t ai_transfer;read_pico_transfer_get_status(&ai_transfer);
            bool ai_allowed=!current->setup_flow && !power_input_owned && !menu_open && !s_quick.open && !current->holds_pmu &&
                current->render && !usb_storage_active() && !app_download_active() && !pico_online_busy() &&
                ai_transfer.state!=READ_PICO_TRANSFER_UPLOADING && !ctx.request_app && !ctx.request_menu && !ctx.request_return;
            if(s_ai.open&&!ai_allowed){ai_dismiss(&ctx,current,true);ai_owned=true;}
            if(ai_event && ai_allowed && !touch.touched && !ui_click_feedback_active() && !ui_click_feedback_nav_pending()){
                held_key=-1;cancel_gesture(&ctx,current,&gesture);last_input_ms=ctx.now_ms;ai_owned=true;
                app_book_pause_auto();
                if(s_ai.open)ai_dismiss(&ctx,current,true);
                else if(ai_event==METALIO_AI_LONG){
                    int64_t at=esp_timer_get_time();(void)at;
                    if(ui_ai_menu_begin(&s_ai,ctx.fb,app_settings_ai_short(),current==&app_book&&app_book_reader_body_visible())){
                        app_book_pause_auto();
                        ESP_LOGI(TAG,"AI menu compose=%lldms",(long long)((esp_timer_get_time()-at)/1000));
                        (void)metalio_haptic_click();ai_push(&ctx,current,s_ai.rect);
                    }
                }else if(ai_event==METALIO_AI_SHORT){
                    present_page(&ctx,current,&gesture,ai_short_action(&ctx,current));
                }
            }
            if(s_ai.open){
                ai_owned=true;
                int choice=ui_ai_menu_feed(&s_ai,&ctx,err==ESP_OK);
                if(err==ESP_OK&&ctx.pressed&&touch.count==1&&s_ai.armed&&s_ai.pressed>=0)
                    (void)metalio_haptic_click();
                if(choice==UI_AI_CLOSE)ai_dismiss(&ctx,current,true);
                else if(choice>=0){
                    last_input_ms=ctx.now_ms;
                    if(app_settings_set_ai_short((app_ai_action_t)choice)==ESP_OK){
                        ai_dismiss(&ctx,current,true);
                    }else ai_push(&ctx,current,ui_ai_menu_save_error(&s_ai,ctx.fb));
                }
            }
        }
        if(ai_owned){quick_input_owned=true;ctx.consumed=true;held_key=-1;cancel_gesture(&ctx,current,&gesture);}
#else
        bool ai_owned = false;
#endif
        if (!touch.touched) pinch_drain = false;
        pinch_input_owned = pinch_drain;
        bool pinch_ready = err == ESP_OK && !power_input_owned && !quick_input_owned && !ai_owned && !s_quick.open && !menu_open &&
            !ctx.request_app && current->wants_pinch && current->on_pinch && current->wants_pinch(&ctx);
        if (pinching && (!pinch_ready || touch.count < 2)) {
            pinching = false;
            if (!power_input_owned && !quick_input_owned && !ai_owned && !s_quick.open && !menu_open && !ctx.request_app &&
                current->wants_pinch && current->on_pinch && current->wants_pinch(&ctx)) {
                app_pinch_t end = {.active = false};
                app_redraw_t redraw = current->on_pinch(&ctx, &end);
                if (redraw != APP_REDRAW_NONE) app_present(&ctx, current, redraw);
            }
            ui_gesture_reset(&gesture);
            pinch_input_owned = true;
            pinch_drain = touch.touched;
        } else if (pinch_ready && touch.count >= 2) {
            if (!pinching) {
                held_key = -1; cancel_gesture(&ctx, current, &gesture); pinching = true;
            }
            app_pinch_t pinch = {.active = true,
                .ax = touch.points[0].x, .ay = touch.points[0].y,
                .bx = touch.points[1].x, .by = touch.points[1].y};
            app_redraw_t redraw = current->on_pinch(&ctx, &pinch);
            if (redraw != APP_REDRAW_NONE) app_present(&ctx, current, redraw);
            pinch_input_owned = true;
        }
        // 双指只占输入，不跳过电源、媒体、后台任务与页面请求。
        // Pinch owns input only: power, media, background work and navigation still run.
        if (pinch_input_owned) { ctx.consumed = true; ctx.pressed = ctx.released = false; quick_input_owned = true; }

        if (!pinch_input_owned && !power_input_owned && !menu_open && !quick_input_owned) {
#if KIIKO_BOARD_METALIO47
            if (err != ESP_OK) { ui_quick_menu_cancel_input(&s_quick); metalio_frontlight_commit(); quick_input_owned=s_quick.open; }
#else
            if (err != ESP_OK) {
                ui_quick_menu_cancel_input(&s_quick);
                quick_input_owned=s_quick.open;
            }
#endif
            else {
                ui_quick_action_t action;
                quick_input_owned = ui_quick_menu_feed(&s_quick, &ctx,
                    current->render && !current->holds_pmu && !usb_storage_active() && !pico_online_busy(), &action);
#if KIIKO_BOARD_METALIO47
                if(ctx.pressed&&ui_quick_menu_pressed(&s_quick)>=0)(void)metalio_haptic_click();
#endif
                if (quick_input_owned) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    ctx.consumed = true;
                }
#if KIIKO_BOARD_METALIO47
                if (action == UI_QUICK_OPEN) app_book_pause_auto();
                if (action == UI_QUICK_OPEN || action == UI_QUICK_CLOSE) { metalio_frontlight_commit(); quick_present(&ctx, current, false); }
#else
                if (action == UI_QUICK_OPEN) app_book_pause_auto();
                if (action == UI_QUICK_OPEN || action == UI_QUICK_CLOSE) quick_present(&ctx, current, false);
#endif
                else if (action == UI_QUICK_WIFI) {
                    quick_toggle_wifi();
                } else if (action == UI_QUICK_BLUETOOTH) {
                    bool enable=!app_settings_ble_turner();
                    app_settings_set_ble_turner(enable);
                    if(enable)ble_pt_reconnect_recent();
                    else {ble_pt_cancel_recent();ble_pt_sync(false);}
                } else if (action == UI_QUICK_FULL) quick_dismiss(&ctx, current, true);
#if KIIKO_BOARD_METALIO47
                else if(action==UI_QUICK_FRONTLIGHT) {
                    metalio_frontlight_toggle();
                } else if(action==UI_QUICK_BRIGHTNESS || action==UI_QUICK_WARMTH) {
                    const bool warm=action==UI_QUICK_WARMTH;
                    unsigned value=warm?(unsigned)s_quick.slider_position:ui_quick_brightness_value(s_quick.slider_position);
                    unsigned old=metalio_frontlight_position(warm);
                    if(old!=value)
                        metalio_frontlight_preview_position(warm?metalio_frontlight_position(false):value,
                                                  warm?value:metalio_frontlight_position(true));
                    s_quick_slider_dirty|=warm?2u:1u;
                } else if(action==UI_QUICK_LIGHT_COMMIT) {
                    metalio_frontlight_commit();
                }
#else
                else if (action == UI_QUICK_LOCK) quick_lock_requested = true;
#endif
            }
#if KIIKO_BOARD_METALIO47
            quick_controls_present(&ctx,current);
#else
            quick_buttons_present(&ctx,current);
#endif
        }

        if (power_input_owned && err == ESP_OK) {
            ctx.consumed = false;
            EpdRect dirty;
#if KIIKO_BOARD_METALIO47
            bool power_was_tracking=power_dialog.tracking;
#endif
            ui_power_action_t action = ui_power_dialog_feed(&power_dialog, &ctx, &dirty);
#if KIIKO_BOARD_METALIO47
            if(!power_was_tracking&&power_dialog.tracking&&power_dialog.hit>0)(void)metalio_haptic_click();
#endif
            if (action == UI_POWER_ACTION_NONE && dirty.height > 0) {
                ui_power_dialog_draw_controls(ctx.fb, &power_dialog, dirty);
                enum EpdDrawError result = update_display_area_diff_with(
                    ctx.hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, dirty);
                guard_draw_result(ctx.hl, result);
                if (result != EPD_DRAW_SUCCESS) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, touch.touched);
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
            } else {
                if (action == UI_POWER_ACTION_CANCEL) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, false);
                    if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                    else app_present(&ctx, current, APP_REDRAW_PAGE);
                } else if (action == UI_POWER_ACTION_SHUTDOWN) {
#if KIIKO_BOARD_METALIO47
                    // 明确的滑动关机始终断电；锁屏的延时策略不能把关机变成待机。
                    // An explicit shutdown always powers off; lock policy must not turn it into standby.
                    const bool staged_shutdown=false;
#else
                    const bool staged_shutdown=app_settings_staged_shutdown();
#endif
                    if (!staged_shutdown) {
                        power_dialog_open = false;
                        ui_power_dialog_end();
                        ui_power_dialog_reset(&power_dialog, false);
                        run_power_action(&ctx, current, false);
                    } else {
                        power_dialog_open = false;
                        ui_power_dialog_end();
                        ui_power_dialog_reset(&power_dialog, false);
                        prepare_lock(&ctx, current, true);
                        extern const app_desc_t app_book;
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp,
                                             current == &app_book && app_book_reader_body_visible());
                        (void)ble_pt_take_standby();
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        poll_media(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.pressed = false;
                        ctx.released = false;
                        app_present(&ctx, current, APP_REDRAW_PAGE);
                    }
                } else if (action == UI_POWER_ACTION_RESTART) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, false);
                    run_power_action(&ctx, current, true);
                }
            }
            ctx.consumed = true;
        }

        if (err != ESP_OK) {
            EpdRect motion_area;
            if(ui_click_feedback_nav_finish(ctx.fb,&motion_area))click_present(&ctx,current,motion_area);
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            EpdRect dirty;
            ui_power_dialog_cancel_input(&power_dialog, &dirty);
            if (power_dialog_open && dirty.height > 0) {
                ui_power_dialog_draw_controls(ctx.fb, &power_dialog, dirty);
                guard_draw_result(ctx.hl, update_display_area_diff_with(
                    ctx.hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, dirty));
            }
            ctx.consumed = true;
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
        }
        EpdRect click_area;
        if (ui_click_feedback_active() && (err != ESP_OK || touch.count > 1 ||
            quick_input_owned || power_input_owned || menu_open ||
            (!ui_click_feedback_nav_pending() && (released || touch.count != 1 ||
            ui_click_feedback_cancel_at(touch.x, touch.y))))) {
            if (ui_click_feedback_release(ctx.fb, &click_area)) click_present(&ctx, current, click_area);
        }
        // 主页面底栏在首次单指报点就确认；抬手、轻滑和停留不再重复切页。
        // Commit root navigation on the first single contact; lift, small motion and holds never repeat it.
        bool nav_press_owned=!quick_input_owned&&s_nav_polled&&touch.touched&&touch.count==1&&navigation_input_ready(&ctx,current)&&ui_nav_hit(touch.x,touch.y)>=0;
        if(nav_press_owned)ctx.consumed=true;
        if (err == ESP_OK && pressed && !nav_press_owned && touch.count == 1 && !power_input_owned &&
            !quick_input_owned && !menu_open && !usb_storage_active() &&
            navigation_input_ready(&ctx, current)) {
            const int tab = ui_nav_hit(touch.x, touch.y);
            if (tab >= 0) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                ctx.user_activity = true;
                nav_press_owned = true;
                s_nav_contact_down=true;
#if KIIKO_BOARD_METALIO47
                (void)metalio_haptic_click();
                if(tab!=app_index_of(current)||!main_page_ready(&ctx,current)||ctx.request_app)ui_nav_request(&ctx,tab);
#else
                if(tab!=app_index_of(current)||!main_page_ready(&ctx,current)||ctx.request_app)ui_nav_request(&ctx,tab);
#endif
                ui_click_feedback_nav_request(tab);
                if(ui_click_feedback_nav_tick(ctx.fb,esp_timer_get_time()/1000,&click_area))
                    click_present(&ctx,current,click_area);
            }
        }
        if (err == ESP_OK && pressed && touch.count == 1 && !nav_press_owned && !power_input_owned &&
            !quick_input_owned && !menu_open && !usb_storage_active() &&
            ui_click_feedback_press(ctx.fb, touch.x, touch.y, &click_area))
            click_present(&ctx, current, click_area);
        if (!power_input_owned && !quick_input_owned && !nav_press_owned && pressed && err == ESP_OK) {
            held_key = -1;
            const int key = ui_key_hit_test(touch.x, touch.y);
            const bool handle_hit = ui_menu_handle_hit_test(touch.x, touch.y) &&
                (!current->menu_handle_enabled || current->menu_handle_enabled(&ctx));
            ctx.consumed = true;
            if (key >= 0 || handle_hit) {
                cancel_gesture(&ctx, current, &gesture);
                if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
            }
            if (key >= 0 && current->owns_keys && !menu_open) {
                bool main_keys=main_page_ready(&ctx,current);
                app_redraw_t redraw=main_keys
                    ? (key==UI_KEY_2?APP_REDRAW_NONE:main_key_action(&ctx,current,key,false))
                    : current->on_key && !(key==UI_KEY_2 && current->defer_middle_short)
                        ? current->on_key(&ctx,key):APP_REDRAW_NONE;
                present_page(&ctx,current,&gesture,redraw);
                if(touch.count==1 && ((main_keys && key==UI_KEY_2)||(!main_keys && current->on_key_long))) {
                    held_key=key;held_since_ms=ctx.now_ms;held_main_key=main_keys;
                }
            } else if (key == UI_KEY_2) {
                if (menu_open) {
                    feedback.updates = 0;
                    ui_draw_menu_page(ctx.fb, current, menu_leaf);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                } else if (current->present != NULL && current->present(&ctx, APP_REDRAW_FULL)) {
                } else if (current->render != NULL) {
                    current->render(&ctx, ctx.fb);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                }
            } else if ((key == UI_KEY_3 || handle_hit) && !usb_storage_active()) {
                menu_open = !menu_open;
                if (menu_open) {
                    menu_leaf = ui_menu_leaf_for_app(current);
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else app_present(&ctx, current, APP_REDRAW_PAGE);
            } else if (key >= 0 && menu_open) {
                int next = menu_leaf + (key == UI_KEY_1 ? -1 : 1);
                if (next >= 0 && next < ui_menu_leaf_count()) {
                    menu_leaf = next;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                }
            } else if (key >= 0) {
                app_redraw_t redraw = current->on_key ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                if (redraw == APP_REDRAW_NONE)
                    ESP_LOGI(TAG, "KEY%d pressed, no action bound", key + 1);
                present_page(&ctx, current, &gesture, redraw);
            } else if (menu_open) {
                int hit = ui_menu_hit_test(touch.x, touch.y, menu_leaf);
                if (hit == UI_MENU_HIT_PREV || hit == UI_MENU_HIT_NEXT) {
                    menu_leaf += hit == UI_MENU_HIT_NEXT ? 1 : -1;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else if (hit >= 0 && touch.count == 1) {
                    menu_pressed = hit;
                    menu_feedback(&ctx, current, menu_leaf, hit, true, &feedback);
                }
            } else if (current->on_gesture) {
                ctx.consumed = false;
            } else {
                // 旧页按下先收到 consumed=true，仅 NONE 为同轮 tick 放行。
                // Legacy press sees consumed=true; only NONE clears it for this tick.
                app_redraw_t redraw = current->on_touch
                    ? current->on_touch(&ctx, &latest) : APP_REDRAW_NONE;
                app_present(&ctx, current, redraw);
                if (redraw == APP_REDRAW_NONE) ctx.consumed = false;
            }
        }
        if (err == ESP_OK) was_touched = touch.touched;
        if (!power_input_owned && !quick_input_owned && menu_open && menu_pressed >= 0) {
            int hit = ui_menu_hit_test(latest.x, latest.y, menu_leaf);
            if (touch.count > 1 || hit != menu_pressed || released) {
                int chosen = menu_pressed;
                menu_feedback(&ctx, current, menu_leaf, chosen, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
                if (released && err == ESP_OK && hit == chosen) {
                    menu_open = false;
                    ctx.request_app = app_at(chosen);
                    selected_from_menu = true;
                    if (ctx.request_app == current) app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            }
        }
        if (!pinch_input_owned && !power_input_owned && !quick_input_owned && !menu_open && current->on_gesture && err == ESP_OK) {
            ui_gesture_event_t event;
            if (ui_gesture_feed(&gesture, &ctx, &event)) {
                app_redraw_t redraw =
                    dispatch_page_gesture(&ctx,current,&event);
                present_page(&ctx, current, &gesture, redraw);
                if (redraw != APP_REDRAW_NONE) ctx.consumed = true;
            }
        }

        ctx.now_ms = esp_timer_get_time() / 1000;
#if KIIKO_BOARD_METALIO47
        if(!s_quick.open) metalio_frontlight_commit();
#else
#endif
        rails_idle_check(ctx.now_ms);

        // 电源键短按默认锁屏；阅读正文可选择将它用于下一页。
        // A short power press normally locks; the reader body may use it for the next page.
#if KIIKO_BOARD_METALIO47
        if (!app_sd_recovery_busy() &&
            (quick_lock_requested || (read_pico_pmu_ready() && !current->holds_pmu && !usb_storage_active()
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS))) {
#else
        if (quick_lock_requested || (read_pico_pmu_ready() && !current->holds_pmu && !usb_storage_active()
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS)) {
#endif
            last_lock_poll_ms = ctx.now_ms;
            read_pico_pmu_key_action_t key_action = quick_lock_requested ? READ_PICO_PMU_KEY_SHORT : read_pico_pmu_take_key_action();
            if (key_action != READ_PICO_PMU_KEY_NONE) {
#if KIIKO_BOARD_METALIO47
                ai_dismiss(&ctx,current,key_action==READ_PICO_PMU_KEY_SHORT);
#endif
                last_input_ms = ctx.now_ms;
                bool quick_was_open = s_quick.open;
                if (quick_was_open) quick_dismiss(&ctx,current,false);
                if (!quick_lock_requested && ctx.now_ms < s_lock_ignore_until_ms) {
                    ESP_LOGI(TAG, "ignore boot power-key action %d", (int)key_action);
                } else if (key_action == READ_PICO_PMU_KEY_LONG) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                    menu_pressed = UI_MENU_HIT_NONE;
                    extern const app_desc_t app_book;
                    bool reader_power_lock = !menu_open && current == &app_book &&
                        app_book_reader_body_visible() && app_settings_reader_power_turn();
                    if (reader_power_lock) {
                        prepare_lock(&ctx, current, true);
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp, true);
                        (void)ble_pt_take_standby();
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        poll_media(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.consumed = true;
                        ctx.pressed = false;
                        ctx.released = false;
                        app_present(&ctx, current, APP_REDRAW_PAGE);
                    } else {
                        power_dialog_open = true;
                        ui_power_dialog_reset(&power_dialog, touch.touched);
                        ctx.request_app = NULL;
                        ctx.request_return = ctx.request_menu = false;
                        main_lock_boundary();
                        ui_text_set_system_scale(true);
                        ui_power_dialog_draw(ctx.fb, &power_dialog);
                        enum EpdDrawError result = update_display_area_with(
                            ctx.hl, &E0470_WAVEFORM, MODE_GL16, ui_power_dialog_rect());
                        guard_draw_result(ctx.hl, result);
                        if (result != EPD_DRAW_SUCCESS) {
                            power_dialog_open = false;
                            ui_power_dialog_end();
                            ui_power_dialog_reset(&power_dialog, touch.touched);
                            app_present(&ctx, current, APP_REDRAW_FULL);
                        }
                    }
                } else if (!power_dialog_open) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                    menu_pressed = UI_MENU_HIT_NONE;
                    app_redraw_t power_redraw = !quick_lock_requested && !quick_was_open && !menu_open && current->on_power_short
                        ? current->on_power_short(&ctx) : APP_REDRAW_NONE;
                    if (power_redraw != APP_REDRAW_NONE) {
                        present_page(&ctx, current, &gesture, power_redraw);
                        ctx.consumed = true;
                    } else {
                        prepare_lock(&ctx, current, true);
                        extern const app_desc_t app_book;
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp,
                                             !menu_open && current == &app_book && app_book_reader_body_visible());
                        (void)ble_pt_take_standby();
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        poll_media(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.consumed = true;
                        ctx.pressed = false;
                        ctx.released = false;
                        // 醒来还在同一页，重画一次免得留着锁屏图。
                        // Still the same page; redraw so the lock image does not stay.
                        if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                        else app_present(&ctx, current, APP_REDRAW_PAGE);
                    }
                }
            }
        }

        // 设置里存的字体在 SD 卡上，开机时卡可能还没挂好，这里定期重试。
        // The saved font lives on the card; retry until the mount is ready.
        // The saved font lives on the card; retry until the mount is ready.
        if (!power_dialog_open && !s_quick.open && !ai_overlay_open() && !usb_storage_active() && current != &app_transfer &&
            ctx.now_ms - last_font_retry_ms >= FONT_RETRY_INTERVAL_MS) {
            last_font_retry_ms = ctx.now_ms;
            if (try_load_saved_sd_font(!media_invalidated)) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                menu_pressed = UI_MENU_HIT_NONE;
                if (menu_open) {
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else {
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
                ESP_LOGI(TAG, "TTF font ready");
            }
        }

        if(held_key>=0 && held_main_key && !main_page_ready(&ctx,current)) {
            held_key=-1;held_main_key=false;
        }
        if (!power_dialog_open && released && held_key == UI_KEY_2 && ui_key_hit_test(latest.x, latest.y) == UI_KEY_2 &&
            (current->defer_middle_short || main_page_ready(&ctx,current)) && !menu_open &&
            !ctx.request_app && !ctx.request_menu && !ctx.request_return) {
            int key = held_key;
            held_key = -1;
            ctx.consumed = true;
            bool held=ctx.now_ms-held_since_ms>=UI_LONG_PRESS_MS;
            app_redraw_t redraw=main_page_ready(&ctx,current)?main_key_action(&ctx,current,key,held):
                held?current->on_key_long(&ctx,key):current->on_key(&ctx,key);
            present_page(&ctx, current, &gesture, redraw);
        }
        if (!power_dialog_open && held_key >= 0) {
            if (menu_open || ctx.request_app || ctx.request_menu || ctx.request_return || !touch.touched ||
                touch.count != 1 || ui_key_hit_test(touch.x, touch.y) != held_key ||
                ctx.now_ms < held_since_ms) {
                held_key = -1;
            } else if (!pressed && ctx.now_ms - held_since_ms >= UI_LONG_PRESS_MS) {
                int key = held_key;
                held_key = -1;
                ctx.consumed = true;
                present_page(&ctx,current,&gesture,main_page_ready(&ctx,current)?
                    main_key_action(&ctx,current,key,true):current->on_key_long(&ctx,key));
            }
        }
#if KIIKO_BOARD_METALIO47
        // 音量键主页面沿用左右键，阅读固定翻页；覆盖层不透传。/ Volume follows root side keys, fixes reader turns, and cannot pass through overlays.
        int volume=metalio_take_volume();
        if(volume && !power_dialog_open && !power_input_owned && !menu_open && !s_quick.open && !ai_overlay_open() && !quick_input_owned) {
            cancel_gesture(&ctx,current,&gesture); last_input_ms=ctx.now_ms;
            present_page(&ctx,current,&gesture,volume_key_action(&ctx,current,volume));
        }
#else
#endif
        // 蓝牙翻页器：BLE 栈只在这里跟随设置启停。必须走 ble_pt_sync()——自己写
        // 「设置与状态不一致就 start()」会让启动失败的那次判断每轮都成立，变成每秒
        // 几十次重复初始化并把内存耗光。sync() 内部有内存闸门、失败退避和崩溃自锁。
        // Bluetooth page-turner: the BLE stack follows the setting only here, and only through
        // ble_pt_sync(). Rolling our own "state differs from setting, so start()" makes a failed
        // start true on every tick, re-initialising dozens of times a second until memory runs
        // out; sync() carries the memory gate, the failure back-off and the crash-loop lock.
        // WiFi 在 C6、BLE 在 S3；传输仍与 BLE 竞争 S3 内部内存，网络占用先拆 BLE，结束后恢复。
        // WiFi runs on C6 and BLE on S3; both consume S3 internal memory, so network leases pause BLE and release it afterwards.
        read_pico_transfer_status_t transfer;
        read_pico_transfer_get_status(&transfer);
        const bool wifi_busy = transfer.state != READ_PICO_TRANSFER_STOPPED ||
            pico_online_busy();
#if KIIKO_BOARD_METALIO47
        ble_pt_sync(app_settings_ble_turner());
#else
        ble_pt_sync(app_settings_ble_turner() && !wifi_busy);
#endif
        ble_pt_poll();
        bool remote_standby=ble_pt_take_standby();
        if (s_quick.open || ai_overlay_open() || quick_input_owned) {
            // 覆盖层期间丢弃页内蓝牙输入，关闭后不重放翻页或学习事件。
            // Discard page BLE input while covered, without replaying turns or learning edges after closing.
            ble_pt_event_t key_event; ble_pt_raw_t raw_event;
            while (ble_pt_pop_key(&key_event)) {}
            while (ble_pt_pop_raw(&raw_event)) {}
        }
#if KIIKO_BOARD_METALIO47
        if (s_quick.open && quick_status() != s_quick_status) quick_controls_present(&ctx, current);
#else
        if (s_quick.open && quick_status() != s_quick_status) quick_buttons_present(&ctx,current);
#endif
        if (!power_dialog_open && !s_quick.open && !quick_input_owned && !ui_click_feedback_active() && !menu_open && !ctx.request_app && !ctx.request_menu && !ctx.request_return && current->on_tick != NULL) {
            app_redraw_t redraw = current->on_tick(&ctx);
            if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) held_key = -1;
            present_page(&ctx, current, &gesture, redraw);
        }
        if (ctx.user_activity) { last_input_ms = ctx.now_ms; ctx.user_activity = false; }
        // 充电插拔不属于用户输入，不打断锁屏/触摸或覆盖层，只更新已显示的状态栏。
        // Cable changes are not user input; update the drawn header alone without disturbing locks/touch/overlays.
        if (!power_dialog_open && !s_quick.open && !quick_input_owned && !menu_open &&
            !current->holds_pmu && !usb_storage_active() && !touch.touched &&
            !ui_click_feedback_active() && !ctx.request_app && !ctx.request_menu && !ctx.request_return &&
            (current!=&app_book||app_book_status_bar_visible())&&ui_nav_refresh_power(ctx.fb)) {
#if KIIKO_BOARD_METALIO47
            bool previous=display_system_binary_set(app_binary_ui(&ctx,current));
            bool previous_nav=display_system_navigation_set(navigation_page_ready(&ctx,current));
            guard_draw_result(ctx.hl,update_display_area_diff_with(ctx.hl,&E0470_FOLLOW_WAVEFORM,
                MODE_DU,ui_nav_status_area()));
            display_system_navigation_set(previous_nav);
            display_system_binary_set(previous);
#else
            // 普通/水波纹保留状态栏灰阶边缘，快刷保持原黑白局刷。
            // Retain gray header edges in ordinary/water modes and short binary updates in fast mode.
            bool fast=app_settings_main_refresh_mode()==APP_MAIN_REFRESH_FAST;
            guard_draw_result(ctx.hl,update_display_area_diff_with(ctx.hl,
                fast?&E0470_FOLLOW_WAVEFORM:display_main_gray_waveform(),fast?MODE_DU:MODE_GL16,
                ui_nav_status_area()));
#endif
        }
        uint32_t ble_input = ble_pt_input_serial();
        if (ble_input != last_ble_input) { last_ble_input = ble_input; last_input_ms = ctx.now_ms; }
        unsigned requested_idle = app_settings_auto_lock_minutes();
#if KIIKO_BOARD_METALIO47
        bool idle_blocked = app_sd_recovery_busy() || usb_storage_active() || app_download_active() || wifi_busy || current == &app_transfer ||
#else
        bool idle_blocked = usb_storage_active() || wifi_busy || current == &app_transfer ||
#endif
            current->holds_pmu || power_dialog_open || touch.touched ||
            ctx.request_app || ctx.request_menu || ctx.request_return;
        if (idle_blocked || requested_idle != idle_minutes) last_input_ms = ctx.now_ms;
        idle_minutes = requested_idle;
        // Recognized standby uses the existing lock/resume path, with normal lock safety gates.
        extern const app_desc_t app_book;
        bool remote_lock=remote_standby && !menu_open && !s_quick.open && !quick_input_owned && !ai_overlay_open() &&
            current==&app_book && app_book_reader_body_visible();
        if (!idle_blocked && ctx.now_ms >= s_lock_ignore_until_ms &&
            (remote_lock || auto_lock_due(ctx.now_ms, last_input_ms, idle_minutes))) {
#if KIIKO_BOARD_METALIO47
            // 不因配置层常亮阻止自动锁屏；透明锁屏须先恢复真实底页，避免缓存玻璃面板。
            // A chooser never disables idle lock; restore the physical underlay before transparent lock artwork can cache it.
            ai_dismiss(&ctx,current,true);
#endif
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
            prepare_lock(&ctx, current, remote_lock);
            extern const app_desc_t app_book;
            main_lock_boundary();
            enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp,
                                 !menu_open && current == &app_book && app_book_reader_body_visible());
            (void)ble_pt_take_standby();
            display_request_full_refresh();
            ctx.now_ms = esp_timer_get_time() / 1000;
            last_input_ms = ctx.now_ms;
            poll_media(&ctx, current, &media_mounted, &media_invalidated);
            last_media_poll_ms = ctx.now_ms;
            ctx.pressed = ctx.released = false;
            if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
            else app_present(&ctx, current, APP_REDRAW_PAGE);
        }
        if (menu_open && feedback.updates > 0
            && ctx.now_ms - feedback.last_ms >= UI_SETTLE_IDLE_MS) {
            guard_draw_result(ctx.hl, update_display_area_with(
                ctx.hl, &E0470_WAVEFORM, MODE_GL16, feedback.area));
            feedback.updates = 0;
        }
        if (!menu_open) feedback.updates = 0;
#ifdef KIIKO_LOCAL_LATENCY
        static int64_t probe_heartbeat;
        if(ctx.now_ms-probe_heartbeat>5000){probe_heartbeat=ctx.now_ms;ESP_LOGI("latency","STATE touch=%d menu=%d dialog=%d usb=%d",touch.touched,menu_open,power_dialog_open,usb_storage_active());}
        if(!menu_open&&!power_dialog_open&&!usb_storage_active()&&!touch.touched) {
            app_redraw_t probe=local_latency_poll(&ctx,current);
            if(probe!=APP_REDRAW_NONE)app_present(&ctx,current,probe);
        }
#endif
        // 返回来源优先；普通请求仍按切页优先处理。
        // Return requests win; ordinary requests still prioritize page switches.
        // USB MSC 持有 TF 卡时禁止任何页面切换；必须先安全停止并重新挂载。
        // Never navigate while USB MSC owns the card; stop and remount it first.
        if (usb_storage_active()) {
            ctx.request_return = false;
            ctx.request_app = NULL;
            ctx.request_menu = false;
        }
        // 只有完成/跳过已成功持久化，才允许引导请求主页；防止按键/菜单捷径绕过。
        // Only a successfully persisted Finish/Skip may leave setup for the Home page.
        setup_navigation_guard(&ctx,current);
        if (!power_dialog_open && (ctx.request_return || ctx.request_app || ctx.request_menu)) {
            if (!ui_click_feedback_nav_pending() && ui_click_feedback_release(ctx.fb, &click_area))
                click_present(&ctx, current, click_area);
            quick_reset();
            held_key = -1;
            const app_desc_t* next = ctx.request_app;
            const bool go_back = ctx.request_return;
            cancel_gesture(&ctx, current, &gesture);
            menu_pressed = UI_MENU_HIT_NONE;
            ctx.request_app = NULL;
            ctx.request_menu = false;
            ctx.request_return = false;
            ctx.consumed = true;
            if (go_back) {
                bool main_source = navigation_page_ready(&ctx, current);
                bool shelf_source = main_source && app_index_of(current) == 1;
                display_main_transition_cancel();
                menu_open = return_app ? return_to_menu : true;
                menu_leaf = return_app ? return_menu_leaf : ui_menu_leaf_for_app(current);
                if (return_app) {
                    if (current->on_exit) current->on_exit(&ctx);
                    current = return_app;
                    return_app = NULL;
                    app_font_activate_system();
                    if (current->on_enter) current->on_enter(&ctx);
                    // 初始化后恢复通用页码，避免 on_enter 的默认页码覆盖来源。
                    // Restore the shared leaf after initialization overrides its default.
                    ctx.leaf = return_leaf;
                }
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else {
                    if (main_source && navigation_page_ready(&ctx, current)) display_main_transition_arm(current, shelf_source, true);
                    app_present(&ctx, current, APP_REDRAW_PAGE);
                    display_main_transition_disarm();
                }
            } else if (next) {
                if (next != current) {
                    return_app = current;
                    return_leaf = ctx.leaf;
                    return_to_menu = selected_from_menu;
                    return_menu_leaf = menu_leaf;
                }
                menu_open = false;
                switch_to(&ctx, &current, next);

            } else {
                menu_open = true;
                menu_leaf = ui_menu_leaf_for_app(current);
                present_menu(&ctx, current, menu_leaf, &feedback);
            }
        }
        // 按下效果已经显示，只定时复位；不把是否切页当作反馈触发条件。
        // The pressed pose is already visible; settle by timer independently of navigation.
        if(ui_click_feedback_nav_pending()) {
            if(err!=ESP_OK || touch.count>1 || menu_open || power_dialog_open || s_quick.open ||
                usb_storage_active() || ai_overlay_open() || !navigation_page_ready(&ctx,current))ui_click_feedback_reset();
            else if(ui_click_feedback_nav_tick(ctx.fb,esp_timer_get_time()/1000,&click_area))
                click_present(&ctx,current,click_area);
        }
#if KIIKO_BOARD_METALIO47
        // 锁屏/PIN阻塞主循环，唤醒后last_input已重置；不创建定时唤醒。
        // Sleep/PIN block this loop and reset last_input on wake; no timer wakeup is created.
        bool fade_blocked=idle_blocked||menu_open||power_dialog_open||s_quick.open||quick_input_owned||
            ui_click_feedback_active()||ui_click_feedback_nav_pending()||touch.touched||touch.count>1||
            ctx.consumed||ctx.request_app||ctx.request_menu||ctx.request_return||ctx.now_ms<s_lock_ignore_until_ms;
        if(anti_fade_due(ctx.now_ms,last_input_ms,fade_blocked)){
            display_request_full_refresh();
            app_present(&ctx,current,APP_REDRAW_PAGE);
        }
#else
#endif
        // 连续列表相位期间缩短等待；每次仍读触摸，并给其他任务让出一个调度周期。
        // Reduce waits during continuous list phases; poll touch each time and yield one scheduling tick.
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}

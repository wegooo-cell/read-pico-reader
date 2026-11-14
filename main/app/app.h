/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 产品页面的统一接口。每个页面是一个 app_desc_t，放在 main/apps/ 下自己的文件里，
 * 绘制、命中、轮询和页内状态都封在那一个文件中；app_loop.c 只按这套回调调度，
 * 加一个页面不必再改主循环。
 *
 * Shared page contract. Each page is an app_desc_t in its own main/apps/ file.
 * Draw, hit-test, poll and page state stay there. app_loop.c only dispatches
 * these callbacks; adding a page does not touch the loop.
 *
 * 冻结：手势回调替代旧触摸回调；三键接管页须保留强刷与菜单出口。
 * Frozen: Gesture callbacks replace legacy touch callbacks. Pages owning keys
 * must retain full refresh and menu exits.
 * 冻结：返回恢复最近切页来源，菜单来源保留菜单位置。
 * Frozen: Return restores the latest page origin, preserving its menu position.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cst836u.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "app_config.h"
#include "sc7a20h_lab.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 回调返回值告诉主循环该怎么把 framebuffer 推上屏。
/// / What the loop should do with the framebuffer.
typedef enum {
    /// 什么都没变，不刷屏。/ Nothing changed.
    APP_REDRAW_NONE = 0,
    /// 只有 area_hint() 那块变了，使用 APP_DYNAMIC_REFRESH_MODE。回调必须自己先画好 fb。
    /// / Push area_hint() with APP_DYNAMIC_REFRESH_MODE. Caller paints fb first.
    APP_REDRAW_AREA,
    /// 由主循环调 render() 重画整页，使用 APP_PAGE_REFRESH_MODE。
    /// / Loop calls render() and presents with APP_PAGE_REFRESH_MODE.
    APP_REDRAW_PAGE,
    /// 同上；APP_PAGE_FORCE_FULL 置 1 时升为整屏 GC16，否则仍走 APP_PAGE_REFRESH_MODE。
    /// / Same; full-screen GC16 when APP_PAGE_FORCE_FULL is 1.
    APP_REDRAW_FULL,
    /// 回调自己已经刷完屏了（自检、刷新测试这类要分步显示进度的页）。
    /// / Page already presented (self-test, refresh bench, …).
    APP_REDRAW_DONE,
} app_redraw_t;

typedef struct app_desc_s app_desc_t;
typedef struct ui_gesture_event_s ui_gesture_event_t;

typedef struct {
    EpdiyHighlevelState* hl;
    uint8_t* fb;
    sc7a20h_handle_t acc;
    cst836u_handle_t tp;
    bool sensor_ready;
    bool continuous_ready;
    int64_t now_ms;
    /// 去抖后的触摸快照：空槽已清零，抬手后保留最后一次坐标。
    /// / Debounced touch: empty slots cleared; last coords kept after lift.
    const cst836u_touch_t* touch;
    const cst836u_info_t* touch_info;
    /// 本轮是不是按下沿 / 抬起沿，触摸页的跟随节流要用。
    /// / Press/release edge this tick. Touch tracking uses this to throttle.
    bool pressed;
    bool released;
    /// 分页页（字体列表）当前停在第几页，由 KEY1 和页内按钮共同改。
    /// / Leaf page index (font list). KEY1 and in-page buttons both change it.
    int leaf;
    /// 本轮已经被别的分支处理掉了，页内的跟随逻辑要跳过这一轮。
    /// / Another branch already handled this tick; skip in-page tracking.
    bool consumed;
    /// 页面识别到输入时置位，主循环消费后清除，避免晃动翻页被当成空闲。
    /// Set for recognized input; the loop consumes and clears it so shake turns reset idle time.
    bool user_activity;
    /// 页面想跳到别的页时填这里，主循环会在本轮末尾切过去。
    /// / Request a page switch; the loop applies it at the end of the tick.
    const struct app_desc_s* request_app;
    /// 本轮末打开菜单；切页请求优先。/ Open the menu at tick end; page requests take priority.
    bool request_menu;
    /// 返回最近切页来源；菜单来源直接恢复菜单，无历史打开当前所属菜单；优先于其它请求。
    /// Return to the latest page origin and its menu; no history opens the current menu leaf. Overrides other requests.
    bool request_return;
    /// 右滑返回的一次性动画意图，只有成功切页才消费，各板复用自己的水波纹。/ One-shot right-swipe return, consumed only by accepted navigation through each board's existing ripple.
    bool request_back_ripple;
} app_ctx_t;

typedef struct {
    /// true = 两指仍在屏上；false = 双指抬起。/ true while both contacts stay down; false on release.
    bool active;
    /// 第一指 / First contact.
    uint16_t ax, ay;
    /// 第二指 / Second contact.
    uint16_t bx, by;
} app_pinch_t;

struct app_desc_s {
    /// 菜单里显示的标题与副标题。/ Menu title and subtitle.
    const char* title;
    const char* detail;
    /// true = 这一页会长时间独占 PMU，主循环的锁屏按键轮询要让路（设备功能自检）。
    /// / true = page owns the PMU; lock-key poll yields (device self-test).
    bool holds_pmu;
    /// true = 进页走 APP_REDRAW_FULL（均衡配置下整屏 GC16）。内容和上一页差别大的页面用，避免差分刷留边。
    /// / true = enter with APP_REDRAW_FULL so a differential update does not leave a seam.
    bool enter_full;
    /// 系统内容采用快刷差分，根页底栏保留灰阶；Pico设置头像也保留灰阶。/ System content uses fast differences, retaining gray root navigation and Pico settings avatars.
    bool binary_ui;
#if KIIKO_BOARD_METALIO47
    /// 可选：当前子视图需要灰阶时绕过二值化；只读查询，不推屏。/ Optional pure query to bypass binary output for the current gray subview.
    bool (*grayscale_ui)(app_ctx_t* ctx);
#endif
    /// 二级系统页也保留底栏；阅读正文不继承。/ Retain navigation in system children without changing reader-body chrome.
    bool persistent_navigation;
    /// 根页/子页结构切换复用主页擦白；手动全刷和故障恢复仍优先。/ Use root white maintenance for root/child transitions; explicit full refresh and fault recovery retain priority.
    bool white_child_transition;
    /// 可选：保留底栏但在升级写入等不可中断阶段禁止切页。/ Optionally block navigation during noninterruptible stages such as firmware writes.
    bool (*navigation_enabled)(app_ctx_t* ctx);
    /// 自定义操作栏替代导航时返回false；输入与刷新范围同时独立。/ False when a custom action bar replaces navigation, including its scan range.
    bool (*navigation_visible)(app_ctx_t* ctx);
    /// 菜单关闭时接管三键，页面必须提供强刷和菜单入口。
    /// Own all three keys outside the menu; provide full refresh and menu exits.
    bool owns_keys;
    /// 引导及其子页只能通过明确完成/跳过退出；通用快捷入口不能绕过。/ Setup and its children cannot escape through global navigation shortcuts.
    bool setup_flow;
    /// 中键短按在抬起时执行，便于长按触发不同动作。/ Fire middle-key short action on release so a hold can differ.
    bool defer_middle_short;
    /// 可选：按当前页面决定右下角菜单把手是否响应。/ Decide whether the bottom-right menu handle is active.
    bool (*menu_handle_enabled)(app_ctx_t* ctx);
    /// 可选：当前确实显示主页面，允许本地测试优化底栏切页；阅读/子页面返回 false。
    /// Optional: true only for a main-tab view eligible for local transition testing; reader/subviews return false.
    bool (*main_page_visible)(app_ctx_t* ctx);
    /// 可选：当前二级页允许右滑返回，沿用翻页识别事件和on_key(UI_KEY_1)收尾。/ Optional secondary-view admission; right-swipe back reuses reading gesture events and on_key(UI_KEY_1) cleanup.
    bool (*edge_back_enabled)(app_ctx_t* ctx);
    /// 可选：按起点保留键盘等控件自己的滑动；不限制普通内容从边缘起手。/ Optionally reserve control-owned origins such as keyboards, without restricting normal content to screen edges.
    bool (*back_swipe_area)(app_ctx_t* ctx, const ui_gesture_event_t* event);
    /// 进页/离页。on_enter 里做上电、唤醒传感器、拉一次数据这类副作用。
    /// / Enter/exit. on_enter powers up, wakes sensors, takes a first sample.
    void (*on_enter)(app_ctx_t* ctx);
    void (*on_exit)(app_ctx_t* ctx);
    /// TF 挂载失效：停止并等待后台任务、关闭文件；主循环随后回退字体并重绘，菜单打开时也通知。
    /// On TF mount loss, join background work and close files before font fallback/redraw; also called behind menus.
    void (*on_media_lost)(app_ctx_t* ctx);
    /// TF 首次挂载或显式重新挂载后，页面按需更新目录与封面缓存。
    /// Refresh page-owned listings or cover caches after a first or explicit TF mount.
    void (*on_media_ready)(app_ctx_t* ctx);
    /// 锁屏前保存页面的阅读状态。/ Persist page state before lock rendering.
    void (*on_before_lock)(app_ctx_t* ctx);
    /// 可选：消费电源键短按；返回 NONE 时沿用锁屏，DONE 表示已消费但无需刷新。
    /// Optional: consume a short power press; NONE keeps locking, DONE consumes without redraw.
    app_redraw_t (*on_power_short)(app_ctx_t* ctx);
    /// 纯绘制，不做 I2C 写、蜂鸣这类副作用，整屏强刷才能安全复用它。
    /// / Paint only. No I2C writes or buzzer. KEY2 full redraw reuses this.
    void (*render)(app_ctx_t* ctx, uint8_t* fb);
    /// 自定义推屏。返回 true 表示页面已经刷过。
    /// / Custom present. Return true if the page updated the display.
    bool (*present)(app_ctx_t* ctx, app_redraw_t redraw);
    /// 设置后独占触摸派发，替代 on_touch。/ Exclusive gesture dispatch replaces on_touch.
    app_redraw_t (*on_gesture)(app_ctx_t* ctx, const ui_gesture_event_t* event);
    /// 可选：本页此刻要原始双指触摸（整页插图缩放）。返回 true 时双指交给 on_pinch，不再走通用手势。
    /// / Optional: raw two-contact input right now (whole-page illustration zoom). While it returns
    /// true two contacts go to on_pinch instead of the shared single-finger gesture path.
    bool (*wants_pinch)(app_ctx_t* ctx);
    /// 可选：双指事件；active=false 表示两指都已离开。/ Optional two-contact event; active=false on release.
    app_redraw_t (*on_pinch)(app_ctx_t* ctx, const app_pinch_t* pinch);
    app_redraw_t (*on_touch)(app_ctx_t* ctx, const cst836u_touch_t* touch);
    /// 默认只收 UI_KEY_1；owns_keys 页面在菜单外接收三键。
    /// / Only KEY1 normally; owns_keys pages receive all three keys outside the menu.
    app_redraw_t (*on_key)(app_ctx_t* ctx, int key);
    /// 可选主页面短按覆盖；返回true表示已消费，边界也不能误切主页面。
    /// Optional main-page short-key override; true consumes the input, including paging boundaries.
    bool (*on_main_key)(app_ctx_t* ctx,int key,app_redraw_t *redraw);
    /// 可选：owns_keys 页面同键持续按住500ms触发一次；defer_middle_short 时中键短按只在抬起执行。
    /// Optional: fire after a 500ms hold; with defer_middle_short, the middle short action runs only on release.
    app_redraw_t (*on_key_long)(app_ctx_t* ctx, int key);
    /// 每轮都调，页面自己判断到不到刷新间隔。
    /// / Called every tick; the page decides if its interval has elapsed.
    app_redraw_t (*on_tick)(app_ctx_t* ctx);
    /// APP_REDRAW_AREA 要刷的区域。/ Area for APP_REDRAW_AREA.
    EpdRect (*area_hint)(app_ctx_t* ctx);
    /// 同一份实现挂多个 app 时用来区分（加速度计的读数页与诊断页共享绘制代码）。
    /// / Distinguishes views that share one implementation (IMU live vs lab).
    void* user;
};

// 锁屏：浅睡按键或拿起回原页；软睡 EN=0，再短按开机；关机 EN=0，长按开机。
// Lock: light-sleep key or pickup returns to the page; soft sleep drops EN
// and a short press powers on; power-off drops EN and needs a long press.
#define APP_LOCK_POLL_MS 200
#define APP_LOCK_IGNORE_BOOT_MS 2000
#define APP_LOCK_IGNORE_SELFTEST_MS 2000

/// 按 redraw 把 framebuffer 推上屏，顺带做 pclk 欠载兜底。APP_REDRAW_DONE 直接返回。
/// / Present fb for redraw, with a pclk-underrun fallback. APP_REDRAW_DONE returns.
void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw);
/// 之后 ms 毫秒内忽略电源键短按，避免开机或自检的残留事件立刻把屏锁掉。
/// / Ignore short power-key presses for ms so boot/self-test leftovers do not lock.
void app_lock_ignore_for(int64_t ms);

#ifdef __cplusplus
}
#endif

/// Yield a long main-page load when another bottom tab is pressed. No display or lifecycle calls.
bool app_poll_main_navigation(app_ctx_t *ctx);

// Background artwork yields to both a held finger and a complete short tap.
bool app_touch_input_pending(app_ctx_t *ctx);

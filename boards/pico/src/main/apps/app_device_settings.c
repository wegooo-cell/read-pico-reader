/*
 * 用户2026-11-11追加：补齐加载提示、贴顶小收藏、封面进度开关、调色板同页入口及进度/升降排序；保留已完成首页与阅读功能。
 * 阅读修订取代旧自动暂停约束：0–10秒±1秒，切换全屏/操作不关闭开关，安全菜单暂缓计时；离书/锁屏/书末保留安全停止。两板原按键页、状态栏避让、控件对齐及黑框提示一并修复。
 * User2026-11-11: complete the remaining shelf previews and retain existing Home/reader work. Auto turns use0–10s steps, keep enabled across fullscreen/manual input, defer in menus and stop at lifecycle/end boundaries. Restore native board-specific key pages, aligned controls, protected touch headers and black notice outlines.
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：分组设置页；网络短时对时后 PMU 持续走时，开机恢复系统时钟。
 * English: Grouped settings; temporary WiFi sync seeds the PMU, whose RTC restores time at boot.
 * 用户修订：设置列表滑动仅差分刷新内容，滑动过程中不周期插入黑白清屏。
 * User revision: settings lists scroll with content-only differentials and no periodic black/white wipe during swipes.
 * 用户授权新输入法：资料卡和签名共用九宫格/全键盘及离线词语候选，保存仍由设置页负责。
 * Authorized keyboard revision: profile and signature share T9/QWERTY and offline phrases; this page still owns saving.
 * 用户修订：资料卡与签名输入只画变化区域，不因输入次数触发整屏黑白清屏。
 * User revision: profile/signature input paints changed regions without whole-screen wipes based on typing counts.
 * 用户修订：快刷说明标注棋盘格亚克力，普通与水波纹继续保留灰阶；三种主页面刷新模式均固定底栏。
 * User revision: the fast-mode description identifies checkerboard acrylic; ordinary/water keep grays and all main-screen modes retain navigation.
 * 用户修订：联网升级先画反馈再启动任务；进度只追加黑色段，不擦除已完成段，不累计整页清屏。升级前释放不在显示的封面缓存。
 * User revision: present feedback before starting online work; append black progress without erasing completed segments or counting page cleanup. Release off-screen cover caches before updating.
 * 用户确认：字号并入系统字体并预览；选择字体后询问同步阅读字体，设置按连接/显示/个性化/升级分组。
 * Approved revision: size and preview live with the UI face; ask before syncing the reading face, and group connection/display/personalization/maintenance.
 * 用户修订：设置题头与列表周围留白保持纯白，普通主页切入/周期整理复用书架同轮分区输出；题头柔和，内容白像素只向白擦除，灰阶保留。普通列表滑动使用柔和差分；手动全刷、离散滚动和快刷/水波纹节奏保持。
 * User revision: keep setting chrome/list whitespace white. Ordinary root entry/periodic cleanup reuse simultaneous shelf regions with soft chrome, directional white content and preserved grays. Scroll ordinary lists with soft differences; retain manual full refresh, discrete gestures and fast/ripple pacing.
 * 用户2026-10-20修订：设置二级页常驻灰底栏，内容止于底栏上方，返回清理和不可中断任务守卫保持。
 * User revision2026-10-20: settings children retain gray navigation; content stops above it, reusing back cleanup and noninterruptible-job guards.
 * S31用户修订：设置主列表内容规整为黑白并使用DU20，固定底栏不变；子页的字体/壁纸预览仍保留灰阶。
 * S31 user revision: normalize root settings content to BW and use DU20, retaining fixed navigation and gray font/wallpaper previews in subpages.
 * 用户撤回跟手滚动：三种主页模式统一恢复一次手势推进一段列表并差分刷新的原逻辑，无惯性、浅墨或停稳补刷。
 * User rollback: all main modes return to one discrete list step per gesture with differential updates, without inertia, light motion ink or idle restoration.
 * 用户2026-11-07同步S31列表交互：滑动临时黑白缓存，连续按住跟手，停稳800ms后原Pico擦白恢复灰阶。
 * User revision2026-11-07: sync S31 cached BW list motion/held dragging and800ms idle white/gray recovery, using Pico FOLLOW8 and its own original gray profile.
 * 用户修订：Pico 设置列表松手直接用灰阶清残影并画最终位置，不等待空闲。
 * User revision: on lift, clean the Pico settings list and draw its final position in one gray update.
 * 用户2026-10-21修订：设置列表滚动保留文字抗锯齿和灰阶头像，撤掉书架额外四相补白，列表仍一次输出且不影响固定底栏。
 * User revision2026-10-21: keep AA text and gray avatars during list scrolls, omit the shelf's four added white phases, and retain one output with fixed navigation.
 */
#ifdef ESP_PLATFORM
#include "ui_locale_format.h"
#endif
#include "setup_state.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "app_content_open.h"
#include "app_registry.h"
#include "app_transfer_mode.h"
#include "settings.h"
#include "factory_reset.h"
#include "book_cover_jobs.h"
#include "lock_pin_flow.h"
#include "ui_pinpad.h"
#include "book_lock_collage.h"
#include "book_lock_bookmark.h"
#include "boot_state.h"
#include "ble_page_turner.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "esp_log.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_root_style.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "ui_wallpaper.h"
#include "ui_avatar.h"
#include "read_pico_search.h"
#include "book_store.h"
#include "book_cover.h"
#include "ota_update.h"
#include "ota_online.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "display.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include "pmu_selftest.h"
#include "soc/rtc_cntl_reg.h"

static void fit_value(char *value, int width);
static void settings_card(uint8_t *fb, EpdRect rect, int radius, uint8_t fill, uint8_t edge);
static char s_notice[96];
typedef enum { SETTINGS_MAIN, SETTINGS_WIFI, SETTINGS_TIME,
               SETTINGS_TIME_EDIT, SETTINGS_SHELF_STYLE, SETTINGS_SYSTEM_FONT,
               SETTINGS_SYSTEM_SIZE, SETTINGS_SYSTEM_CONTRAST, SETTINGS_MAIN_REFRESH, SETTINGS_LOCK_STYLE,
               SETTINGS_WALLPAPER, SETTINGS_WALLPAPER_PREVIEW,
               SETTINGS_CONFIG, SETTINGS_UPGRADE, SETTINGS_BOOT, SETTINGS_FACTORY_RESET,
               SETTINGS_POWER_SLEEP, SETTINGS_AUTO_LOCK, SETTINGS_PROFILE, SETTINGS_AVATAR,
               SETTINGS_BLUETOOTH, SETTINGS_BLE_SCAN,
               SETTINGS_LOCK_PIN, SETTINGS_PIN_ENTRY, SETTINGS_TEXT_EDIT, SETTINGS_LANGUAGE } settings_page_t;
static settings_page_t s_page;
static bool s_shelf_style_requested,s_shelf_style_from_manage;
void app_device_settings_request_shelf_style(void){s_shelf_style_requested=true;}
static app_redraw_t shelf_style_finish(app_ctx_t*ctx){
    if(s_shelf_style_from_manage){
        s_shelf_style_from_manage=false;app_book_return_from_shelf_style();ctx->request_return=true;return APP_REDRAW_NONE;
    }
    s_page=SETTINGS_MAIN;return APP_REDRAW_PAGE;
}

static int settings_content_end(void) {
    return UI_NAV_REFRESH_END;
}
static int settings_viewport_end(void) {
    return UI_NAV_TOP;
}
static void settings_navigation(uint8_t *fb) {
    ui_nav_draw(fb,3);
}
static int settings_nav_hit(uint16_t x,uint16_t y) {
    return ui_nav_hit(x,y);
}

static bool s_reset_confirm, s_reset_pending;
static char s_reset_notice[96];
static ui_pinpad_t s_pinpad;
static lock_pin_flow_t s_pinflow;
static bool s_pin_pending;
static unsigned s_pin_failures;
static int64_t s_pin_retry_at;
static void pin_setup_close(void) {
    ui_pinpad_end(&s_pinpad); lock_pin_flow_end(&s_pinflow); s_pin_pending = false;
}
static void pin_setup_open(app_ctx_t *ctx, bool disable) {
    pin_setup_close(); lock_pin_flow_begin(&s_pinflow, disable);
    ui_pinpad_begin(&s_pinpad, ctx->fb, lock_pin_flow_title(&s_pinflow));
    s_pinpad.navigation=true;
    s_page = SETTINGS_PIN_ENTRY;
}
static app_redraw_t pin_paint(app_ctx_t *ctx, EpdRect area) {
    ui_pinpad_paint(ctx->fb, &s_pinpad, area);
    guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, area));
    return APP_REDRAW_DONE;
}
static app_redraw_t pin_setup_tick(app_ctx_t *ctx) {
    if (s_pinpad.blocked && ctx->now_ms >= s_pin_retry_at) {
        ui_pinpad_reset(&s_pinpad, NULL, "请重新输入");
        return pin_paint(ctx, ui_pinpad_entry_area());
    }
    if (!s_pin_pending) return APP_REDRAW_NONE;
    s_pin_pending = false;
    lock_pin_step_t prior = s_pinflow.step;
    esp_err_t error = lock_pin_flow_input(&s_pinflow, s_pinpad.digits);
    if (error == ESP_OK && s_pinflow.step == PIN_FLOW_DONE) {
        pin_setup_close(); s_page = SETTINGS_LOCK_PIN;
        snprintf(s_notice, sizeof(s_notice), "%s", lock_pin_enabled() ? "锁屏密码已保存" : "锁屏密码已关闭");
        return APP_REDRAW_PAGE;
    }
    const char *notice = error == ESP_OK ? "" : error == ESP_ERR_INVALID_RESPONSE ? "两次输入不同，请重设" :
        error == ESP_ERR_INVALID_ARG ? "原密码不正确，请重试" : "保存失败，请重试";
    ui_pinpad_reset(&s_pinpad, lock_pin_flow_title(&s_pinflow), notice);
    if (error == ESP_ERR_INVALID_ARG && prior == PIN_FLOW_OLD && ++s_pin_failures >= 3) {
        s_pin_failures = 0; s_pin_retry_at = ctx->now_ms + 10000;
        snprintf(s_pinpad.notice, sizeof(s_pinpad.notice), "请等待 10 秒后重试"); s_pinpad.blocked = true;
    }
    return pin_paint(ctx, (EpdRect){122, 218, 440, 247});
}
static int s_style_scroll, s_main_scroll;
// 新样式使用新编号；旧3/4仍退回亚克力，列表保持5。
// New style gets a new ID; retired3/4 still fall back to acrylic and list remains5.
static const uint8_t s_shelf_style_ids[] = {1, 2, 6, 5};
#define SHELF_STYLE_CHOICES 4
static bool s_collage_prepare_pending;
static bool s_scroll_drag_consumed, s_scroll_present_pending;
typedef struct {
    bool tracking, active, following, disabled, frame_pending, settle_pending;
    settings_page_t page;
    int *offset, limit, origin_offset, origin_y, press_offset, press_x, press_y, shown_offset;
    int width, height, travel_x, travel_y;
    int quick_direction;
    enum EpdRotation rotation;
    int64_t frame_ms, press_ms, sample_ms;
    int64_t quick_release_ms;
    EpdRect area;
    uint8_t *bits;
} settings_drag_t;
static settings_drag_t s_list_drag;
#define SETTINGS_DRAG_SLOP_PX 12
#define SETTINGS_QUICK_MIN_PX 6
#define SETTINGS_QUICK_MAX_PX SETTINGS_QUICK_STEP_PX
#define SETTINGS_QUICK_MS 300
#define SETTINGS_SHORT_QUICK_MS 150
#define SETTINGS_QUICK_REPEAT_MS 650
#define SETTINGS_QUICK_STEP_PX ((settings_content_end()-(s_page==SETTINGS_MAIN?160:190))/2)
#define SETTINGS_HOLD_FRAME_MS 60
#define SETTINGS_HOLD_CACHE_MAX (512u * 1024u)
// 蓝牙翻页器子页：滚动位置、正在学习哪个动作（0 未学，1 上一页，2 下一页）、提示行。
// Bluetooth sub-page: scroll offset, which action is being learned (0 idle, 1 prev, 2 next),
// and a notice line.
static int s_ble_scroll, s_ble_learning;
static char s_ble_notice[64];
static char s_ble_feedback[64];
static int s_font_page, s_wallpaper_page;
static bool s_sync_pending;
static bool s_font_sync_confirm;
static char s_font_sync_path[TTF_FONT_PATH_MAX];
static bool s_config_confirm;
// 屏幕修复：点一下就开始。整屏盲刷要几秒，活儿交给 tick——不能在触摸回调里做。
// Panel repair: one tap starts it. The blind full-screen drive takes seconds, so the work goes to the
// tick rather than a touch callback.
static bool s_panel_repair_pending;
static bool s_boot_pending;
static bool s_upgrade_confirm, s_upgrade_online, s_local_pending, s_upgrade_restart;
static pico_ota_info_t s_local_update;
static EXT_RAM_BSS_ATTR pico_update_status_t s_update;
static char s_upgrade_notice[96];
static uint32_t s_update_drawn_percent, s_update_drawn_at;
static pico_update_state_t s_update_drawn_state;
static bool s_update_drawn_busy, s_upgrade_offer_pending;
typedef enum { UPGRADE_JOB_NONE, UPGRADE_JOB_CHECK, UPGRADE_JOB_DOWNLOAD } upgrade_job_t;
static upgrade_job_t s_upgrade_job;
static int s_upgrade_bar_width;
static uint32_t upgrade_percent(void) {
    uint32_t percent = s_update.release.size ? (uint64_t)s_update.received * 100 / s_update.release.size : 0;
    return percent > 100 ? 100 : percent;
}
static void upgrade_remember(uint32_t now) {
    s_update_drawn_state = s_update.state;
    s_update_drawn_busy = s_update.busy;
    s_update_drawn_percent = upgrade_percent();
    s_update_drawn_at = now;
}
// 结果等任务收尾后一次显示；进度只在五秒且增长五个百分点后补画新增像素。
// Show results once after worker cleanup; append progress after five seconds and five percentage points.
static app_redraw_t upgrade_status_redraw(uint32_t now) {
    bool downloading = s_update.state == PICO_UPDATE_DOWNLOADING;
    if (s_update.busy && s_update.state != PICO_UPDATE_CHECKING && !downloading)
        return APP_REDRAW_NONE;
    bool offer = s_upgrade_offer_pending && !s_update.busy && s_update.state == PICO_UPDATE_AVAILABLE;
    if (offer) {
        s_upgrade_offer_pending = false;
        s_upgrade_confirm = s_upgrade_online = true;
    } else if (!s_update.busy) s_upgrade_offer_pending = false;
    if (offer || s_update.state != s_update_drawn_state || s_update.busy != s_update_drawn_busy) {
        upgrade_remember(now);
        return APP_REDRAW_PAGE;
    }
    uint32_t percent = upgrade_percent();
    if (downloading && percent >= s_update_drawn_percent + 5 && now - s_update_drawn_at >= 5000) {
        upgrade_remember(now);
        return APP_REDRAW_AREA;
    }
    return APP_REDRAW_NONE;
}
static EpdRect upgrade_progress_area(void) { return (EpdRect){52, 468, 580, 64}; }
static void draw_upgrade_percent(uint8_t *fb) {
    uint32_t percent = upgrade_percent();
    EpdRect number = {496, 472, 132, 42};
    ui_fill_rect_fast(number, UI_GRAY_WHITE, fb);
    char progress[32]; snprintf(progress, sizeof(progress), "%lu%%", (unsigned long)percent);
    ui_text_fixed(fb, 624, 477, 26, progress, EPD_DRAW_ALIGN_RIGHT, false);
    ui_image_bw_rect(fb, number);
}
static void draw_upgrade_progress(uint8_t *fb) {
    ui_fill_rect_fast(upgrade_progress_area(), UI_GRAY_WHITE, fb);
    draw_upgrade_percent(fb);
    ui_fill_rect_fast((EpdRect){60, 518, 564, 10}, UI_GRAY_BLACK, fb);
    ui_fill_rect_fast((EpdRect){61, 519, 562, 8}, UI_GRAY_WHITE, fb);
    s_upgrade_bar_width = (int)(562 * upgrade_percent() / 100);
    if (s_upgrade_bar_width)
        ui_fill_rect_fast((EpdRect){61, 519, s_upgrade_bar_width, 8}, UI_GRAY_BLACK, fb);
}
static void draw_upgrade_progress_delta(uint8_t *fb) {
    int width = (int)(562 * upgrade_percent() / 100);
    if (width <= s_upgrade_bar_width) return;
    draw_upgrade_percent(fb);
    ui_fill_rect_fast((EpdRect){61 + s_upgrade_bar_width, 519, width - s_upgrade_bar_width, 8}, UI_GRAY_BLACK, fb);
    s_upgrade_bar_width = width;
}
// 排队时只改 UI 快照；本次画面推送结束后的 tick 才启动 TLS/擦写。
// Scheduling changes only the UI snapshot; start TLS/flash on the tick after its presentation.
static void upgrade_schedule(bool downloading, uint32_t now) {
    s_upgrade_job = downloading ? UPGRADE_JOB_DOWNLOAD : UPGRADE_JOB_CHECK;
    s_update.state = downloading ? PICO_UPDATE_DOWNLOADING : PICO_UPDATE_CHECKING;
    s_update.busy = true;
    s_update.received = 0;
    snprintf(s_update.message, sizeof(s_update.message), "%s", downloading
        ? "正在下载，短暂断网会自动重连" : "正在连接并检查更新");
    s_upgrade_offer_pending = !downloading;
    upgrade_remember(now);
}
static app_redraw_t upgrade_start_pending(void) {
    bool downloading = s_upgrade_job == UPGRADE_JOB_DOWNLOAD;
    s_upgrade_job = UPGRADE_JOB_NONE;
    app_home_cover_mode_changed();
    app_book_cover_mode_changed();
    esp_err_t error = downloading ? pico_online_download() : pico_online_check();
    pico_online_get_status(&s_update);
    if (error == ESP_OK) return APP_REDRAW_NONE;
    s_upgrade_offer_pending = false;
    s_update.busy = false;
    if (s_update.state != PICO_UPDATE_FAILED) s_update.message[0] = 0;
    s_update.state = PICO_UPDATE_FAILED;
    if (!s_update.message[0]) snprintf(s_update.message, sizeof(s_update.message), "无法开始升级，请退出传书后重试");
    snprintf(s_upgrade_notice, sizeof(s_upgrade_notice), "%s", s_update.message);
    return APP_REDRAW_PAGE;
}
// 日志按 UTF-8 字符和实际字宽换行，限制六行，不影响下载时的局部刷新。
// Wrap notes on UTF-8 boundaries using measured widths; cap at six lines without changing download refreshes.
static bool upgrade_note_line(const char **cursor, char *line, size_t capacity) {
    if (!cursor || !*cursor || !line || capacity < 5) return false;
    const char *p = *cursor;
    while (*p && (strchr("\r\n\t ;", *p) || !strncmp(p, "；", 3)))
        p += !strncmp(p, "；", 3) ? 3 : 1;
    size_t used = 0;
    line[0] = 0;
    while (*p && !strchr("\r\n;", *p) && strncmp(p, "；", 3)) {
        unsigned char lead = (unsigned char)*p;
        size_t bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (bytes > strlen(p) || used + bytes >= capacity) break;
        memcpy(line + used, p, bytes);
        line[used + bytes] = 0;
        if (used && ui_text_fixed_width_px(26, line) > 540) { line[used] = 0; break; }
        used += bytes;
        p += bytes;
    }
    *cursor = p;
    return used != 0;
}
static void draw_upgrade_notes(uint8_t *fb) {
    const char *cursor = s_update.release.notes;
    char line[sizeof(s_update.release.notes)];
    if (!*cursor) {
        ui_text_fixed(fb, 60, 530, 26, "本次更新暂无说明", EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    for (int row = 0; row < 6 && upgrade_note_line(&cursor, line, sizeof(line)); ++row) {
        if (row == 5 && *cursor) {
            size_t used = strlen(line);
            while (used && (used + 4 > sizeof(line) || ui_text_fixed_width_px(26, line) > 514)) {
                do { --used; } while (used && ((unsigned char)line[used] & 0xc0) == 0x80);
                line[used] = 0;
            }
            memcpy(line + used, "…", 4);
        }
        ui_text_fixed(fb, 60, 530 + row * 38, 26, line, EPD_DRAW_ALIGN_LEFT, false);
    }
}
static EpdRect upgrade_confirm_button(bool online, bool accept) {
    return (EpdRect){accept ? 364 : 60, online ? 842 : 613, 260, 64};
}
static void upgrade_open(void) {
    (void)pico_ota_inspect(PICO_OTA_UPDATE_PATH, &s_local_update);
    pico_online_get_status(&s_update);
    s_upgrade_confirm = s_local_pending = s_upgrade_restart = false;
    s_upgrade_offer_pending = false;
    s_upgrade_job = UPGRADE_JOB_NONE;
    s_upgrade_bar_width = 0;
    upgrade_remember(esp_timer_get_time() / 1000);
    s_upgrade_notice[0] = 0;
    s_page = SETTINGS_UPGRADE;
}
static const char *const TAG = "device_settings";
// 主设置始终单行；大字号只改变字形，字体预览单独保留足够空间。
// Main rows never stack; larger glyphs share one centerline, with extra space only in font previews.
static bool settings_stacked(void) { return app_settings_system_font_size() >= 150; }
static int settings_profile_height(void) { return 106; }
static int settings_main_row_height(void) {
    return 82;
}
#define SETTINGS_ROW_H settings_main_row_height()
#define SETTINGS_WIRELESS_ROWS 3
#define SETTINGS_DISPLAY_ROWS 8
#define SETTINGS_PERSONAL_ROWS 5
#define SETTINGS_MAINTENANCE_ROWS 5
#define SETTINGS_WIRELESS_Y (180 + settings_profile_height() + 77)
#define SETTINGS_DISPLAY_Y (SETTINGS_WIRELESS_Y + SETTINGS_WIRELESS_ROWS * SETTINGS_ROW_H + 78)
#define SETTINGS_PERSONAL_Y (SETTINGS_DISPLAY_Y + SETTINGS_DISPLAY_ROWS * SETTINGS_ROW_H + 78)
#define SETTINGS_MAINTENANCE_Y (SETTINGS_PERSONAL_Y + SETTINGS_PERSONAL_ROWS * SETTINGS_ROW_H + 78)
// 设置行图标：32 像素盒，在行高 68 里垂直居中；墨色统一，避免一行一个灰度。
// Setting row icons: a 32 px box centred in the 68 px row with one shared ink level.
#define SETTINGS_UNLOCK_ICON 18
#define SETTINGS_ICON_PX 32
#define SETTINGS_ICON_INK UI_GRAY_BLACK
// 主页面滚动上限：把最后一组的最后一行刚好推到导航栏之上，多留 8px 余量。
// Main-page scroll limit: just enough to bring the last row of the last group above the nav bar,
// with 8 px to spare.
#define SETTINGS_SCROLL_MAX \
    (SETTINGS_MAINTENANCE_Y + SETTINGS_MAINTENANCE_ROWS * SETTINGS_ROW_H - UI_NAV_REFRESH_END + 12)
static char s_editor[96], s_editor_notice[80];
static ui_text_edit_t s_editor_input;
static bool s_editor_signature;
static const char *system_font_label(const char *path) {
    if (!path || !path[0]) return "思源黑体";
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    static char stem[TTF_FONT_NAME_MAX];
    snprintf(stem, sizeof(stem), "%s", name);
    size_t len = strlen(stem);
    if (len >= 4 && !strcasecmp(stem + len - 4, ".ttf")) stem[len - 4] = 0;
    if (!strcasecmp(stem, "Hei")) return "思源黑体（TF 卡）";
    return ttf_font_localized_name(stem);
}
typedef struct {
    EpdRect size, preview;
    int preview_title, font_title, first, row_height, slots;
} system_font_layout_t;
static system_font_layout_t system_font_layout(void) {
    system_font_layout_t l = settings_stacked()
        ? (system_font_layout_t){{36,197,612,183},{36,471,612,216},413,722,785,142,2}
        : (system_font_layout_t){{36,197,612,100},{36,379,612,154},322,560,615,99,4};
    l.slots=(settings_viewport_end()-42-l.first)/l.row_height;
    return l;
}
static EpdRect system_font_item_rect(int slot) {
    system_font_layout_t l = system_font_layout();
    return (EpdRect){36,l.first + slot * l.row_height,612,l.row_height - 16};
}
static EpdRect system_font_sync_button(bool sync) { return (EpdRect){sync ? 351 : 72,683,261,74}; }
static void draw_system_font_sync(uint8_t *fb) {
    // 仅一个有界弹窗，不额外打开阅读字体。/ One bounded dialog; never load the reading face here.
    settings_card(fb,(EpdRect){48,389,588,401},28,UI_GRAY_WHITE,0x58);
    ui_text_fixed_vc(fb,342,448,32,"同步到阅读字体？",EPD_DRAW_ALIGN_CENTER,false);
    char name[80]; snprintf(name,sizeof(name),"已选择「%s」",system_font_label(s_font_sync_path));
    while (ui_text_fixed_width_px(27,name) > 520 && strlen(name) > 6) {
        size_t n = strlen(name) - 1;
        while (n && ((unsigned char)name[n] & 0xc0) == 0x80) --n;
        name[n] = 0;
    }
    ui_text_fixed_vc(fb,342,516,27,name,EPD_DRAW_ALIGN_CENTER,false);
    ui_text_fixed_vc(fb,342,570,24,"是否让阅读正文也使用这个字体？",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_fixed_vc(fb,342,614,23,s_notice[0]?s_notice:"不同步时，保留原来的阅读字体。",EPD_DRAW_ALIGN_CENTER,false);
    for (int i=0;i<2;++i) {
        EpdRect r=system_font_sync_button(i);
        settings_card(fb,r,20,i ? UI_GRAY_BLACK : UI_GRAY_WHITE,0x58);
        ui_text_fixed_vc(fb,r.x+r.width/2,r.y+r.height/2,25,i ? "同步阅读字体" : "仅系统字体",EPD_DRAW_ALIGN_CENTER,i);
    }
}
static int s_year, s_month, s_day, s_hour, s_minute;
#define WALLPAPER_MAX 64
typedef struct { char path[288]; char name[96]; } wallpaper_item_t;
// 壁纸列表放 PSRAM，理由同上。
// The wallpaper list lives in PSRAM for the same reason.
static wallpaper_item_t *s_wallpapers;

static bool wallpapers_alloc(void) {
    if (!s_wallpapers)
        s_wallpapers = heap_caps_calloc(WALLPAPER_MAX, sizeof(wallpaper_item_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_wallpapers != NULL;
}
static int s_wallpaper_count;
static int s_wallpaper_selected;
static bool s_wallpaper_confirm, s_wallpaper_preview_ok;
static bool s_bookmark_picker;
static uint8_t *s_bookmark_preview;
static uint8_t *s_bookmark_preview_base;

static void wallpaper_scan_dir(const char *root) {
    if (!wallpapers_alloc()) return;
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while (s_wallpaper_count < WALLPAPER_MAX && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strncmp(entry->d_name, "._", 2)) continue;
        const char *ext = strrchr(entry->d_name, '.');
        // 壁纸列表接受 PNG/JPG 与星瞳云下发的 XTH；透明书封仍只认 PNG。
        // Wallpapers accept PNG/JPG and Xteink cloud XTH; transparent bookmarks stay PNG-only.
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png") &&
                     strcasecmp(ext, ".xth"))) continue;
        if(s_bookmark_picker && strcasecmp(ext,".png"))continue;
        wallpaper_item_t *item = &s_wallpapers[s_wallpaper_count];
        if (snprintf(item->path, sizeof(item->path), "%s/%s", root, entry->d_name) >= sizeof(item->path)) continue;
        struct stat st;
        if (stat(item->path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > BOOK_IMAGE_FILE_MAX) continue;
        size_t name_len = strnlen(entry->d_name, sizeof(item->name) - 1);
        memcpy(item->name, entry->d_name, name_len);
        item->name[name_len] = 0;
        ++s_wallpaper_count;
    }
    closedir(dir);
}
static void bookmark_preview_release(void) {
    free(s_bookmark_preview);s_bookmark_preview=NULL;
    free(s_bookmark_preview_base);s_bookmark_preview_base=NULL;
}
static void bookmark_preview_prepare(const uint8_t *background) {
    free(s_bookmark_preview);s_bookmark_preview=NULL;
    if(!s_bookmark_picker || s_wallpaper_selected<0 || s_wallpaper_selected>=s_wallpaper_count)return;
    const size_t bytes=684u*1216u/2u;
    if(!background || heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<(s_bookmark_preview_base?bytes:2*bytes)+2304u*1024u)return;
    // 一次保存目录画面，切换PNG预览时不反复叠加上一张；离开预览即释放。
    // Save the directory frame once so swiping never nests old PNG previews; release on leaving preview.
    if(!s_bookmark_preview_base){
        s_bookmark_preview_base=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(s_bookmark_preview_base)memcpy(s_bookmark_preview_base,background,bytes);
    }
    if(!s_bookmark_preview_base)return;
    uint8_t *canvas=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(canvas)memcpy(canvas,s_bookmark_preview_base,bytes);
    if(canvas && book_lock_bookmark_draw(canvas,s_wallpapers[s_wallpaper_selected].path))s_bookmark_preview=canvas;
    else free(canvas);
}
static void wallpaper_scan(void) {
    s_wallpaper_count = s_wallpaper_page = 0;
    wallpaper_scan_dir("/sdcard/pictures");
    wallpaper_scan_dir("/sdcard/images");
    wallpaper_scan_dir("/sdcard");
}


static void profile_editor_open(bool signature) {
    s_editor_signature = signature;
    snprintf(s_editor, sizeof(s_editor), "%s", signature ? app_settings_status_signature() : app_settings_device_name());
    ui_text_edit_init(&s_editor_input, s_editor, sizeof(s_editor));
    s_editor_notice[0] = 0;
    ui_keyboard_begin(&s_editor_input, false);
    s_page = SETTINGS_TEXT_EDIT;
}




static int days_in_month(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) return 29;
    return days[month - 1];
}

// 公历日期转 UTC 天数；界面统一按北京时间 UTC+8 编辑。
// Convert a Gregorian date to UTC days; the editor uses Beijing time (UTC+8).
static int64_t civil_days(int year, int month, int day) {
    year -= month <= 2;
    int era = year / 400;
    int yoe = year - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

static void time_open(void) {
    time_t now = time(NULL);
    if (now < 1704067200) {
        const pmu_snapshot_t *pmu = read_pico_pmu_get();
        if (pmu && pmu->time_synced && pmu->unix_sec >= 1704067200) now = pmu->unix_sec;
    }
    if (now < 1704067200) {
        char month[4] = "Jan"; int day = 1, year = 2024;
        if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3) {
            strcpy(month, "Jan"); day = 1; year = 2024;
        }
        static const char *const months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        int m = 1; while (m < 12 && strcmp(month, months[m - 1])) ++m;
        s_year = year; s_month = m; s_day = day; s_hour = s_minute = 0;
    } else {
        struct tm date;
        time_t local = now + 8 * 3600;
        gmtime_r(&local, &date);
        s_year = date.tm_year + 1900; s_month = date.tm_mon + 1; s_day = date.tm_mday;
        s_hour = date.tm_hour; s_minute = date.tm_min;
    }
    s_notice[0] = 0;
    s_page = SETTINGS_TIME_EDIT;
}

static void time_adjust(int field, int step) {
    if (field == 0) s_year = s_year + step > 2099 ? 2024 : s_year + step < 2024 ? 2099 : s_year + step;
    if (field == 1) s_month = s_month + step > 12 ? 1 : s_month + step < 1 ? 12 : s_month + step;
    if (field == 2) s_day = s_day + step > days_in_month(s_year, s_month) ? 1 :
                            s_day + step < 1 ? days_in_month(s_year, s_month) : s_day + step;
    if (field == 3) s_hour = (s_hour + step + 24) % 24;
    if (field == 4) s_minute = (s_minute + step + 60) % 60;
    if (s_day > days_in_month(s_year, s_month)) s_day = days_in_month(s_year, s_month);
}

static bool time_save(void) {
    int64_t epoch = civil_days(s_year, s_month, s_day) * 86400 +
                    s_hour * 3600 + s_minute * 60 - 8 * 3600;
    if (epoch < 1704067200 || epoch > 4102444799LL) return false;
    struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    if (settimeofday(&tv, NULL)) return false;
    uint8_t payload[4] = {(uint8_t)epoch, (uint8_t)(epoch >> 8),
                          (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24)};
    esp_err_t err = read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
    if (err == ESP_OK) err = read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (err != ESP_OK || !pmu || !pmu->time_synced) {
        snprintf(s_notice, sizeof(s_notice), "系统时间已设置，PMU 同步失败");
        return false;
    }
    snprintf(s_notice, sizeof(s_notice), "时间已保存，并同步到电源管理芯片");
    s_page = SETTINGS_TIME;
    return true;
}

static bool settings_plain_list(void) {
    return s_page==SETTINGS_MAIN||s_page==SETTINGS_BLUETOOTH||s_page==SETTINGS_BLE_SCAN;
}
static void settings_card(uint8_t *fb, EpdRect rect, int radius, uint8_t fill, uint8_t edge) {
    // Limit binary treatment to decoration; glyph AA, avatar pixels and previews are untouched.
    if(settings_plain_list()) {
        ui_fill_round_rect(fb,rect,radius,fill<128?UI_GRAY_BLACK:UI_GRAY_WHITE);
        ui_draw_round_rect(fb,rect,radius,UI_GRAY_BLACK);
    } else {
        ui_fill_round_rect(fb, rect, radius, fill);
        ui_draw_control_frame(fb, rect, radius, edge);
    }
}
static void settings_divider(uint8_t *fb, int y, int x, int width) {
    if(settings_plain_list())ui_hairline(fb,y,x,width,UI_GRAY_BLACK);
    else ui_draw_separator(fb, y, x, width, 0x60);
}
static void settings_value(uint8_t *fb,int x,int cy,int px,const char *value) {
    char shown[128]; snprintf(shown,sizeof(shown),"%s",value);
    size_t n=strlen(shown);
    bool arrow=n>=3 && !strcmp(shown+n-3,"›");
    if (arrow) {
        shown[n-=3]=0;
        while (n && shown[n-1]==' ') shown[--n]=0;
        ui_draw_icon(fb,x-4,cy,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
    ui_text_vc(fb,x-(arrow?28:0),cy,px,shown,EPD_DRAW_ALIGN_RIGHT,false);
}
static void row(uint8_t *fb, int y, const char *label, const char *value) {
    ui_text(fb, 54, y + 17, 27, label, EPD_DRAW_ALIGN_LEFT, false);
    settings_value(fb,627,y+20+ui_text_effective_px(21)/2,21,value);
    settings_divider(fb, y + 68, 54, 575);
}
static void section(uint8_t *fb, int y, const char *title) {
    ui_text(fb, 40, y, 22, title, EPD_DRAW_ALIGN_LEFT, false);
}

/* ---- 蓝牙翻页器子页的版式 / Layout of the Bluetooth sub-page ---- */

// 行高比 setting_group 的 68 略大，因为这一页每行右侧常常要放一个按钮。
// Rows are a little taller than setting_group's 68 because these rows often carry a button.
#define BLE_ROW_H 84
// 按钮右缘与 row() 的值右缘对齐；值文字右端再让出按钮宽度，两者不重叠。
// The button's right edge aligns with row()'s value edge; the value text then stops short of the
// button, so the two never overlap.
#define BLE_BTN_W 118
#define BLE_BTN_RIGHT 624
#define BLE_VALUE_RIGHT (BLE_BTN_RIGHT - BLE_BTN_W - 17)

// 渲染和命中测试都调这个函数，两边看到的是同一份坐标——否则会出现「按钮画在这儿、
// 点在别处」，这一页之前就是这么坏的。
// Rendering and hit testing both call this, so they see identical coordinates. Without it a
// control ends up drawn in one place and tapped in another, which is exactly how this page was
// broken before.
typedef struct {
    int toggle_top;                  // 开关卡片顶；-1 表示被学习提示取代 / switch card top, -1 when the learn prompt replaces it
    int status_top;
    int notice_top;                  // -1 表示没有提示 / -1 when absent
    int bonds_title_top;
    int bond_top[BLE_PT_MAX_BONDS];  // 已配对行顶；-1 表示该位不存在 / bonded row tops, -1 when absent
    int bond_rows;
    int scan_entry_top;              // 「扫描设备 ›」入口行顶 / the "scan devices" entry row
    int learn_title_top;
    int learn_top[2];
    int footer_top;
    int content_bottom;              // 内容底，用于滚动上限 / content bottom, for the scroll limit
} ble_layout_t;

static ble_layout_t ble_layout(void) {
    ble_layout_t l;
    memset(&l, 0, sizeof(l));
    int y = 202 - s_ble_scroll;
    l.toggle_top = s_ble_learning || s_ble_feedback[0] ? -1 : y;
    y += 126 + 38;
    l.status_top = y;
    y += BLE_ROW_H + 20;
    l.notice_top = s_ble_notice[0] ? y : -1;
    if (s_ble_notice[0]) y += 38;
    l.bonds_title_top = y;
    y += 38;
    l.bond_rows = (int)ble_pt_bond_count();
    for (int i = 0; i < BLE_PT_MAX_BONDS; ++i)
        l.bond_top[i] = i < l.bond_rows ? y + i * BLE_ROW_H : -1;
    y += (l.bond_rows ? l.bond_rows : 1) * BLE_ROW_H;
    l.scan_entry_top = y + 16;
    y = l.scan_entry_top + BLE_ROW_H + 30;
    l.learn_title_top = y;
    l.learn_top[0] = y + 38;
    l.learn_top[1] = l.learn_top[0] + BLE_ROW_H;
    l.footer_top = l.learn_top[1] + BLE_ROW_H + 25;
    l.content_bottom = l.footer_top + 66;
    return l;
}

// 一行的左标签 + 让出按钮后的右值，外加分隔线。与 row() 同高，但右端不会压到按钮。
// A row's left label plus a right value that stops short of the button, with the usual divider.
// Same height as row(), but the value never runs under the button.
// 学到的键按整帧显示：帧里非零的字节照原样列出来。同一个键每次按下发的字节一样，所以这串
// 标识每次都一样；不同的键只要有一个字节不同，读出来就不同。
// A learned key shows its whole frame: the non-zero bytes, in order. The same key sends the same
// bytes every press, so this identifier never changes, and two keys that differ in any byte read
// differently.
static void format_bound_key(const ble_pt_frame_t *frame, char *out, size_t size) {
    if (!frame || !frame->len) {
        if (size) out[0] = 0;
        return;
    }
    size_t used = 0;
    unsigned shown = 0;
    for (size_t i = 0; i < frame->len && used + 4 < size; ++i) {
        if (!frame->bytes[i]) continue;
        if (shown == 3) {  // 值列只有 140px：最多列 3 个字节，多的用 + 表示 / the value column holds three
            snprintf(out + used, size - used, " +");
            return;
        }
        const int written = snprintf(out + used, size - used, shown ? " %02X" : "%02X", frame->bytes[i]);
        if (written <= 0) break;
        used += (size_t)written;
        ++shown;
    }
    if (!shown) snprintf(out, size, "00");  // 理论上到不了：全零帧不给绑 / unreachable: a zero frame is never bound
}

static void ble_row(uint8_t *fb, int y, const char *label, const char *value) {
    if (y + BLE_ROW_H < 190 || y >= settings_viewport_end()) return;
    settings_card(fb, (EpdRect){36, y, 612, BLE_ROW_H - 8}, 18, UI_GRAY_WHITE, 0x70);
    char name[96]; snprintf(name, sizeof(name), "%s", label);
    fit_value(name, 290);
    ui_text_vc(fb, 60, y + 38, 26, name, EPD_DRAW_ALIGN_LEFT, false);
    if (value && value[0]) {
        char shown[96]; snprintf(shown, sizeof(shown), "%s", value); fit_value(shown, 140);
        settings_value(fb,BLE_VALUE_RIGHT,y+38,21,shown);
    }
}

// 行右侧的按钮。竖直居中于该行，右缘与值文字对齐。
// The button on a row's right: vertically centred in the row, right edge aligned with the value.
static EpdRect ble_button_rect(int row_top) {
    return (EpdRect){BLE_BTN_RIGHT - BLE_BTN_W, row_top + 10, BLE_BTN_W, 56};
}

// 三级页：进页即持续扫描，选中一台连上就退回二级页。列表长了也不影响二级页。
// Third-level page: scanning starts on entry and runs continuously; picking a device connects
// and returns to level two. A long list never affects the page above it.
typedef struct {
    int devices_top;
    int device_rows;
    int status_top;
    int content_bottom;
} ble_scan_layout_t;

// 扫描列表只列真的广播了名字的设备。没有广播名时组件把地址回落成名字，那一行对用户
// 认不出是什么设备，而且地址太长会把信号强度挤掉。
// The scan list shows only devices that actually advertised a name. Without one the component
// falls back to the address: nothing the user can recognise, and long enough to crowd the signal
// strength off the row.
static bool ble_scan_visible(const ble_pt_device_t *dev) {
    return dev != NULL && dev->has_name;
}

static int ble_scan_visible_count(void) {
    int visible = 0;
    const uint8_t count = ble_pt_device_count();
    for (uint8_t i = 0; i < count; ++i) {
        if (ble_scan_visible(ble_pt_device(i))) ++visible;
    }
    return visible;
}

// 取第 row 台有名字的设备；遍历顺序仍是组件的原始顺序。
// The row-th device that has a name; the walk keeps the component's order.
static const ble_pt_device_t *ble_scan_visible_at(int row) {
    if (row < 0) return NULL;
    const uint8_t count = ble_pt_device_count();
    int seen = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const ble_pt_device_t *dev = ble_pt_device(i);
        if (!ble_scan_visible(dev)) continue;
        if (seen++ == row) return dev;
    }
    return NULL;
}

static ble_scan_layout_t ble_scan_layout(void) {
    ble_scan_layout_t l;
    memset(&l, 0, sizeof(l));
    l.status_top = 200;
    l.devices_top = 300;
    l.device_rows = ble_scan_visible_count();
    l.content_bottom = l.devices_top + (l.device_rows ? l.device_rows * BLE_ROW_H : 68) + 40;
    return l;
}
static void back_header(uint8_t *fb, const char *title) {
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, title, EPD_DRAW_ALIGN_CENTER, false);
    settings_divider(fb, 174, 36, 612);
}

static void setting_icon(uint8_t *fb, int index, int cx, int cy) {
    // 每个设置项对应一个 Lucide 图标；索引顺序由各分组传入的 icons[] 数组决定。
    // One Lucide icon per setting row; the index order comes from each group's icons[] array.
    static const ui_icon_t icons[] = {
        UI_ICON_WIFI,              // 0 无线连接
        UI_ICON_BLUETOOTH,         // 1 蓝牙
        UI_ICON_TYPE,              // 2 系统字体、阅读操作
        UI_ICON_A_LARGE_SMALL,     // 3 系统字号
        UI_ICON_LIBRARY_BIG,       // 4 书架样式
        UI_ICON_TICKET,            // 5 锁屏样式
        UI_ICON_CLOCK,             // 6 日期与时间
        UI_ICON_CONTRAST,          // 7 系统对比度
        UI_ICON_COPY,              // 8 保存与恢复
        UI_ICON_POWER,             // 9 关机睡眠
        UI_ICON_TEXT_ALIGN_START,  // 10 状态栏签名
        UI_ICON_REFRESH_CW,        // 11 首页强刷
        UI_ICON_DOWNLOAD,          // 12 系统升级
        UI_ICON_CPU,               // 13 BOOT 刷机
        UI_ICON_EYE_CLOSED,        // 14 自动休眠
        UI_ICON_SLIDERS_HORIZONTAL, // 15 主页刷新模式 / Main refresh mode
        UI_ICON_LOCK,              // 16 锁屏密码 / lock screen password
        UI_ICON_ROTATE_CW,         // 17 屏幕修复 / panel repair
        UI_ICON_LOCK,              // 18 双击解锁 / double-press unlock
        [20] = UI_ICON_CHART_COLUMN, // 20 书籍百分比 / cover progress
    };
    if (index < 0 || index >= (int)(sizeof(icons) / sizeof(icons[0]))) return;
    ui_draw_icon(fb, cx, cy, SETTINGS_ICON_PX, icons[index], SETTINGS_ICON_INK);
}

static void settings_fit_text(char *value, int px, int width) {
    while (value[0] && ui_text_fixed_width_px(ui_text_effective_px(px),value)>width) {
        size_t n=strlen(value)-1;while(n&&((unsigned char)value[n]&0xc0)==0x80)--n;value[n]=0;
    }
}
static void fit_value(char *value, int width) { settings_fit_text(value,20,width); }

static void settings_switch(uint8_t *fb, int center_y, bool active) {
    // 灰色轨道在滚动差分扫描下会经过深色相位产生黑闪；纯黑白无灰阶过渡，任何刷新模式都稳定。
    // A gray track transits dark phases under the scroll's differential scan and flashes
    // black; pure black/white holds clean in every refresh mode.
    EpdRect track = {555, center_y - 19, 69, 39};
    if (active) ui_fill_round_rect(fb, track, 19, UI_GRAY_BLACK);
    else {
        ui_fill_round_rect(fb, track, 19, UI_GRAY_WHITE);
        ui_draw_round_rect(fb, track, 19, 0x30);
    }
    int cx = active ? track.x + 49 : track.x + 20;
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, 0x30, fb);
}

static void setting_group(uint8_t *fb, int title_y, const char *title,
                          int card_y, const int icons[], const char *const labels[],
                          const char *const values[], int count) {
    const int row_height = SETTINGS_ROW_H;
    title_y -= s_main_scroll;
    card_y -= s_main_scroll;
    // 屏外的行不测字、不生成字形；保留卡片边界与原滚动坐标。
    // Skip off-screen glyph work while preserving card boundaries and scroll coordinates.
    if (card_y + count * row_height <= 160 || title_y >= settings_viewport_end()) return;
    if (title_y + ui_text_effective_px(20) > 160 && title_y < settings_viewport_end())
        ui_text(fb, 42, title_y, 20, title, EPD_DRAW_ALIGN_LEFT, false);
    settings_card(fb, (EpdRect){36, card_y, 612, count * row_height}, 22,
                  UI_GRAY_WHITE, 0x70);
    for (int i = 0; i < count; ++i) {
        int y = card_y + i * row_height;
        if (y + row_height <= 160 || y >= settings_viewport_end()) continue;
        if (i) settings_divider(fb, y, 88, 544);
        setting_icon(fb, icons[i], 67, y + row_height / 2);
        if(icons[i]==20){
            ui_text_vc(fb,100,y+row_height/2,25,labels[i],EPD_DRAW_ALIGN_LEFT,false);
            settings_switch(fb,y+row_height/2,app_settings_shelf_progress_badges());continue;
        }
        if (icons[i] == SETTINGS_UNLOCK_ICON) {
            ui_text_vc(fb, 100, y + row_height / 2, 25, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            settings_switch(fb, y + row_height / 2, app_settings_wake_presses() == 2);
            continue;
        }
        char shown[128]; snprintf(shown,sizeof(shown),"%s",values[i]);
        int value_width = 594 - 100 - 20 - ui_text_fixed_width_px(ui_text_effective_px(25),labels[i]);
        if (value_width > 215) value_width = 215;
        if (value_width < 0) value_width = 0;
        fit_value(shown,value_width);
        ui_text_vc(fb,100,y+row_height/2,25,labels[i],EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,594,y+row_height/2,20,shown,EPD_DRAW_ALIGN_RIGHT,false);
        // 导航是图标，不依赖用户字体是否包含 › 字形，也不会被截短掉。
        // Navigation is an icon, independent of the font cmap and value truncation.
        ui_draw_icon(fb,625,y+row_height/2,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
}

static void setting_toggle(uint8_t *fb, int y, const char *title, const char *detail, bool active) {
    settings_card(fb, (EpdRect){36, y, 612, 126}, 22, UI_GRAY_WHITE, 0x70);
    ui_text(fb, 59, y + 24, 27, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 59, y + 75, 19, detail, EPD_DRAW_ALIGN_LEFT, false);
    settings_switch(fb, y + 62, active);
}

static void draw_style_thumbnail(uint8_t *fb, int style, int top) {
    if (top < 242 || top + 170 >= settings_viewport_end()) return;
    if (style == 5) {
        ui_fill_round_rect(fb, (EpdRect){68,top,548,170}, 12, UI_GRAY_WHITE);
        for (int i=0;i<3;++i) {
            int y=top+9+i*52;
            ui_fill_rect_fast((EpdRect){86,y,31,43},i==1?0x80:0x40,fb);
            ui_fill_rect_fast((EpdRect){137,y+6,218,3},0,fb);
            ui_fill_rect_fast((EpdRect){137,y+19,84,2},0x70,fb);
            ui_fill_rect_fast((EpdRect){137,y+34,435,4},0xd0,fb);
            ui_fill_rect_fast((EpdRect){137,y+34,120+i*75,4},0x40,fb);
        }
        return;
    }
    const int left = 99;
    for (int i = 0; i < 3; ++i) {
        int x = left + 20 + i * 155;
        EpdRect cover = {x, top, 116, 145};
        ui_fill_rect_fast(cover, i == 1 ? 0x60 : i == 2 ? 0xa0 : 0x40, fb);
        ui_draw_round_rect(fb, cover, 1, 0x30);
        ui_draw_round_rect(fb, (EpdRect){x + 7, top + 7, 102, 130}, 1, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 47, x + 28, 60, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 54, x + 39, 38, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 111, x + 44, 28, i == 1 ? 0xc0 : 0xe0);
    }
    if (style == 1) {
        ui_fill_rect_fast((EpdRect){left, top + 143, 486, 16}, 0x30, fb);
        ui_hairline(fb, top + 143, left, 486, 0x80);
    } else if (style == 2) {
        ui_draw_acrylic_guard(fb, (EpdRect){left, top + 94, 486, 70});
    } else if (style == 6) {
        ui_draw_liquid_shelf_rail(fb, (EpdRect){left, top + 126, 486, 36});
    }
}

// 仅 82x82 灰阶头像，不缓存整屏或第二份字库；文件变更/拔卡会使缓存失效。
// One 82x82 gray avatar, never a full frame or second font; file changes/removal invalidate it.
static EXT_RAM_BSS_ATTR uint8_t s_avatar_pixels[82*82];
static char s_avatar_path[288];
static bool s_avatar_ready, s_avatar_pending;
static char s_main_wifi[33];
static bool s_main_wifi_saved;
static void render_content(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    if (s_page == SETTINGS_PIN_ENTRY) { ui_pinpad_paint(fb, &s_pinpad, ui_pinpad_full()); return; }
    ui_clear_page(fb);
    if(s_page==SETTINGS_MAIN)ui_root_background(fb);
    if (s_page == SETTINGS_FACTORY_RESET) {
        ui_nav_status(fb);
        back_header(fb,"恢复出厂设置");
        ui_text_box(fb,(EpdRect){54,260,576,170},26,
            "重置系统偏好、资料卡、WiFi、蓝牙配对和锁屏密码。",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){54,450,576,170},23,
            "保留书籍、字体、图片、阅读记录和硬件校准。",EPD_DRAW_ALIGN_LEFT,false);
        if(s_reset_confirm)ui_text_box(fb,(EpdRect){54,645,576,80},23,"确认后将恢复默认设置并重启。",EPD_DRAW_ALIGN_LEFT,false);
        if(s_reset_notice[0])ui_text_box(fb,(EpdRect){54,735,576,90},21,s_reset_notice,EPD_DRAW_ALIGN_LEFT,false);
        ui_draw_button(fb,(EpdRect){54,850,576,78},s_reset_pending?"正在恢复…":s_reset_confirm?"确认恢复并重启":"恢复出厂设置",true);
        ui_draw_button(fb,(EpdRect){54,950,576,78},"取消",false);
        settings_navigation(fb);return;
    }

    // 保留 ui_clear_page 的白底，灰阶只由头像、卡片与字形生成。
    // Keep ui_clear_page's white background; avatars, cards and glyphs alone generate grays.
    ui_nav_status(fb);
    if (s_page == SETTINGS_LOCK_PIN) {
        back_header(fb, "锁屏密码");
        row(fb, 266, "四位数字密码", !lock_pin_available() ? "暂不可用" : lock_pin_enabled() ? "已开启" : "已关闭");
        if (!lock_pin_available()) ui_text_vc(fb, 342, 408, 24, "锁屏设置暂不可用", EPD_DRAW_ALIGN_CENTER, false);
        else if (lock_pin_enabled()) {
            row(fb, 406, "修改密码", "验证原密码  ›");
            row(fb, 512, "关闭密码", "验证原密码  ›");
            // 宽限期：锁屏后该时间内唤醒/开机免密；点按在 关闭/5分钟/1小时/8小时/24小时 间循环。
            // Grace: wakes inside the window skip the PIN; tapping cycles off/5min/1h/8h/24h.
            const unsigned grace = app_settings_pin_grace_code();
            const char *grace_text = grace == 1 ? "5 分钟" : grace == 2 ? "1 小时" :
                grace == 3 ? "8 小时" : grace == 4 ? "24 小时" : "关闭";
            row(fb, 618, "解锁宽限期", grace_text);
        } else row(fb, 406, "开启密码", "设置四位密码  ›");
        if (s_notice[0]) ui_text_vc(fb, 342, 730, 23, s_notice, EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 800, 21, "修改和关闭密码需验证原密码", EPD_DRAW_ALIGN_CENTER, false);
        settings_navigation(fb); return;
    }
    if (s_page == SETTINGS_PROFILE) {
        back_header(fb, "个人资料");
        ui_text(fb, 42, 214, 21, "自定义名称与头像", EPD_DRAW_ALIGN_LEFT, false);
        row(fb, 266, "设备名称", app_settings_device_name());
        row(fb, 366, "更换头像", app_settings_avatar_path()[0] ? "已选择图片  ›" : "默认图标  ›");
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_AVATAR) {
        back_header(fb, "选择头像");
        section(fb, 232, "TF 卡 pictures 文件夹中的 JPG / PNG 图片");
        row(fb, 270, "使用默认图标", app_settings_avatar_path()[0] ? "选择  ›" : "当前  ✓");
        if (!s_wallpaper_count) ui_text(fb, 52, 384, 23, "未找到图片，请放入 pictures 文件夹", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < 7; ++i) {
            int index = s_wallpaper_page * 7 + i;
            if (index >= s_wallpaper_count) break;
            EpdRect box = {36, 374 + i * 96, 612, 80};
            bool active = !strcmp(s_wallpapers[index].path, app_settings_avatar_path());
            settings_card(fb, box, 17, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[index].name);
            fit_value(name, 490);
            ui_text_vc(fb, 60, box.y + 40, 24, name, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(611, box.y + 40, 7, UI_GRAY_BLACK, fb);
        }
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_TEXT_EDIT) {
        back_header(fb, s_editor_signature ? "状态栏签名" : "设备名称");
        ui_text(fb, 642, 95, 24, "完成", EPD_DRAW_ALIGN_RIGHT, false);
        ui_text(fb, 36, 201, 21, s_editor_signature ? "状态栏中间显示，留空则隐藏" : "显示在设置页的资料卡", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_input_draw(fb, &s_editor_input, (EpdRect){36, 243, 612, 82}, 28, false, NULL);
        if (s_editor_notice[0]) ui_text_fixed_vc(fb, 36, 510, 22, s_editor_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_keyboard_draw(fb, UI_SYSTEM_KEYBOARD_TOP);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_SHELF_STYLE) {
        static const char *const styles[] = {"深色书轨", "亚克力书架", "液态玻璃书架", "列表书架"};
        static const char *const descriptions[] = {"每页 9 本", "每页 9 本", "薄玻璃挡条 · 每页 9 本", "作者与进度 · 每页 4 本"};
        for (int i = 0; i < SHELF_STYLE_CHOICES; ++i) {
            int y = 263 + i * 253 - s_style_scroll;
            if (y + 230 < 242 || y >= settings_viewport_end()) continue;
            EpdRect card = {36, y, 612, 230};
            int style = s_shelf_style_ids[i];
            bool active = app_settings_shelf_style() == style;
            settings_card(fb, card, 24, active ? 0xd0 : UI_GRAY_WHITE,
                          active ? 0x58 : 0x70);
            draw_style_thumbnail(fb, style, y + 11);
            if (y + 188 >= 242 && y + 222 < settings_viewport_end()) ui_text_box(fb, (EpdRect){68,y+180,230,42}, 23, styles[i], EPD_DRAW_ALIGN_LEFT, false);
            if (y + 192 >= 242 && y + 220 < settings_viewport_end()) ui_text_box(fb, (EpdRect){310,y+184,272,36}, 17, descriptions[i], EPD_DRAW_ALIGN_LEFT, false);
            if (y + 194 >= 242 && y + 220 < settings_viewport_end()) {
                epd_draw_circle(609, y + 207, 13, UI_GRAY_BLACK, fb);
                if (active) epd_fill_circle(609, y + 207, 6, UI_GRAY_BLACK, fb);
            }
        }
        ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,242},UI_GRAY_WHITE,fb);
        ui_fill_rect_fast((EpdRect){0,settings_viewport_end(),UI_LOCK_WIDTH,UI_LOCK_HEIGHT-settings_viewport_end()},UI_GRAY_WHITE,fb);
        ui_nav_status(fb);ui_nav_back(fb,36,79);
        ui_text_vc(fb,342,107,34,"书架样式",EPD_DRAW_ALIGN_CENTER,false);
        ui_text(fb,36,207,23,"选择书架外观与排列方式",EPD_DRAW_ALIGN_LEFT,false);
        settings_navigation(fb);
        return;
    }
    if(s_page==SETTINGS_LANGUAGE){
        back_header(fb,"语言");
        const char *labels[]={"简体中文","English"};
        for(int i=0;i<2;++i){EpdRect rect={36,300+i*154,612,122};
            ui_draw_choice_round_rect(fb,rect,22,app_setup_english()==(i==1));
            ui_text_box(fb,ui_inset_rect(rect,20),34,labels[i],EPD_DRAW_ALIGN_CENTER,app_setup_english()==(i==1));
        }
        ui_text_box(fb,(EpdRect){36,642,612,88},24,"仅切换系统语言，书籍和输入内容保留原文",EPD_DRAW_ALIGN_CENTER,false);
        if(s_notice[0])ui_text_box(fb,(EpdRect){36,778,612,80},23,s_notice,EPD_DRAW_ALIGN_CENTER,false);
        settings_navigation(fb);return;
    }
    if (s_page == SETTINGS_TIME_EDIT) {
        ui_nav_back(fb, 36, 79);
        ui_text(fb, 36, 151, 47, "日期与时间", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 36, 216, 23, "北京时间 · UTC+8", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, 259, 36, 612, UI_GRAY_LIGHT);
        static const char *const labels[] = {"年", "月", "日期", "时", "分"};
        int values[] = {s_year, s_month, s_day, s_hour, s_minute};
        for (int i = 0; i < 5; ++i) {
            int x = 36 + i * 122;
            ui_text(fb, x + 52, 300, 24, labels[i], EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_control_frame(fb, (EpdRect){x, 354, 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53, 385, 30, "+", EPD_DRAW_ALIGN_CENTER, false);
            char value[8]; snprintf(value, sizeof(value), i == 0 ? "%04d" : "%02d", values[i]);
            ui_text(fb, x + 53, 456, i == 0 ? 34 : 39, value, EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_control_frame(fb, (EpdRect){x, 535, 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53, 566, 30, "−", EPD_DRAW_ALIGN_CENTER, false);
        }
        ui_text(fb, 36, 649, 23, "点上方 + 或下方 − 调整数字", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36, 767, 612, 78}, "保存时间", false);
        ui_draw_button(fb, (EpdRect){36, 874, 612, 78}, "取消", false);
        if (s_notice[0]) ui_text(fb, 36, 996, 22, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        return;
    }
    if (s_page == SETTINGS_WIFI) {
        back_header(fb, "WiFi");
        section(fb, 253, "已保存网络");
        row(fb, 296, s_main_wifi_saved ? s_main_wifi : "尚未配置", s_main_wifi_saved ? "已保存" : "");
        row(fb, 408, "连接或更改网络", "前往传输  ›");
        ui_text(fb, 54, 529, 21, "配网后可在日期与时间中进行 WiFi 对时。", EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_FONT) {
        back_header(fb, "系统字体");
        system_font_layout_t l = system_font_layout();
        settings_card(fb,l.size,22,UI_GRAY_WHITE,0x70);
        char size[16]; snprintf(size,sizeof(size),"%u%%",app_settings_system_font_size());
        if (settings_stacked()) {
            ui_text_vc(fb,60,247,27,"系统字号",EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,589,247,27,size,EPD_DRAW_ALIGN_RIGHT,false);
            ui_text_vc(fb,60,328,18,"轻点切换，最大 200%",EPD_DRAW_ALIGN_LEFT,false);
        } else {
            ui_text_vc(fb,58,227,25,"系统字号",EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,58,269,18,"轻点切换，最大 200%",EPD_DRAW_ALIGN_LEFT,false);
            ui_text_vc(fb,589,247,26,size,EPD_DRAW_ALIGN_RIGHT,false);
        }
        ui_draw_icon(fb,619,247,28,UI_ICON_ROTATE_CW,0x38);
        section(fb,l.preview_title,"效果预览");
        settings_card(fb,l.preview,22,UI_GRAY_WHITE,0x70);
        ui_text_vc(fb,60,l.preview.y+(settings_stacked()?68:51),29,"阅读，让时间慢下来",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,60,l.preview.y+(settings_stacked()?151:107),25,"Aa 123",EPD_DRAW_ALIGN_LEFT,false);
        section(fb,l.font_title,"选择字体");
        const char *selected = app_settings_system_font_path();
        int count = ttf_font_count(), first = s_font_page*l.slots;
        for (int slot=0;slot<l.slots && first+slot<=count;++slot) {
            int index=first+slot;
            const ttf_font_item_t *item=index?ttf_font_item(index-1):NULL;
            bool active=item?!strcmp(selected,item->path):!selected[0];
            EpdRect box=system_font_item_rect(slot);
            settings_card(fb,box,20,active?0xe0:UI_GRAY_WHITE,0x70);
            char label[TTF_FONT_NAME_MAX];
            snprintf(label,sizeof(label),"%s",item?system_font_label(item->path):"思源黑体（内建）");
            settings_fit_text(label,26,box.width-100);
            ui_text_vc(fb,60,box.y+box.height/2,26,label,EPD_DRAW_ALIGN_LEFT,false);
            if (active) epd_fill_circle(613,box.y+box.height/2,6,UI_GRAY_BLACK,fb);
        }
        char pages[24];snprintf(pages,sizeof(pages),"%d / %d",s_font_page+1,(count+l.slots)/l.slots);
        ui_text_fixed_vc(fb,648,settings_viewport_end()-16,18,pages,EPD_DRAW_ALIGN_RIGHT,false);
        if (s_notice[0]) ui_text_fixed_vc(fb,42,settings_viewport_end()-16,18,s_notice,EPD_DRAW_ALIGN_LEFT,false);
        settings_navigation(fb);
        if (s_font_sync_confirm) draw_system_font_sync(fb);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_SIZE) {
        back_header(fb, "系统字号");
        section(fb, 248, "界面文字大小");
        for (int i = 0; i < 11; ++i) {
            int value = 100 + i * 10;
            EpdRect box = {36 + (i % 2) * 316, 298 + (i / 2) * 122, 296, 102};
            bool active = app_settings_system_font_size() == value;
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            char label[48]; snprintf(label, sizeof(label), "%d%%", value);
            ui_text_vc(fb, box.x + 24, box.y + 51, 29, label, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(box.x + box.width - 25, box.y + 51, 8, UI_GRAY_BLACK, fb);
        }
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_MAIN_REFRESH) {
        back_header(fb, "主页刷新模式");
        section(fb, 248, "首页 · 书架 · 文件管理 · 设置");
        const char* names[] = {"普通", "快刷", "水波纹"};
        const char* details[] = {"保留灰阶层次，兼顾画面清晰度", "黑白快刷，亚克力用细棋盘格", "以水波纹切换页面，保留灰阶"};
        for (int i = 0; i < 3; ++i) {
            const EpdRect box = {36, 298 + i * 158, 612, 132};
            const bool active = app_settings_main_refresh_mode() == (app_main_refresh_mode_t)i;
            settings_card(fb, box, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x58);
            ui_text_vc(fb, 64, box.y + 38, 30, names[i], EPD_DRAW_ALIGN_LEFT, false);
            char detail[128]; snprintf(detail, sizeof(detail), "%s", details[i]); fit_value(detail, 498);
            ui_text_vc(fb, 64, box.y + 89, 22, detail, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(607, box.y + 38, 8, UI_GRAY_BLACK, fb);
        }
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_CONTRAST) {
        back_header(fb, "系统对比度");
        section(fb, 248, "调整系统界面的灰阶层次与清晰度");
        static const char *const names[] = {"柔和", "标准", "清晰", "鲜明", "高对比"};
        for (int i = 0; i < 5; ++i) {
            int value = 100 + i * 10;
            EpdRect box = {36, 298 + i * 116, 612, 90};
            bool active = app_settings_system_contrast() == value;
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text_vc(fb, 64, box.y + 45, 28, names[i], EPD_DRAW_ALIGN_LEFT, false);
            char shown[16]; snprintf(shown, sizeof(shown), "%d%%", value);
            ui_text_vc(fb, 570, box.y + 45, 22, shown, EPD_DRAW_ALIGN_RIGHT, false);
            if (active) epd_fill_circle(605, box.y + 45, 8, UI_GRAY_BLACK, fb);
        }
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_LOCK_STYLE) {
        back_header(fb, "锁屏样式");
        section(fb, 248, "选择电源键锁屏后的画面");
        const char *labels[] = {"壁纸锁屏", "阅读票根", "书架拼贴", "透明 PNG 书封"};
        const char *details[] = {"使用 TF 卡中的 JPG / PNG 图片", "书封、进度和阅读记录", "真实封面 · 名称与数量 · 系统字体", "当前页面作底 · 白底 PNG 自动去白"};
        const uint8_t values[] = {1, 0, 2, 3};
        for (int i = 0; i < 4; ++i) {
            EpdRect box = {36, 300 + i * 154, 612, 132};
            bool active = app_settings_lock_style() == values[i];
            settings_card(fb, box, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text(fb, 64, box.y + 23, 31, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 64, box.y + 79, 20, details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 66, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb,54,966,21,"透明书封放入 pictures · 推荐 684 × 1216",EPD_DRAW_ALIGN_LEFT,false);
        if (app_settings_wallpaper_path()[0])
            row(fb, 1010, "当前壁纸", strrchr(app_settings_wallpaper_path(), '/') + 1);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_WALLPAPER) {
        back_header(fb,s_bookmark_picker?"选择透明书封":"选择壁纸");
        section(fb,244,s_bookmark_picker?"pictures · 透明 PNG · 保留整张画布":"TF 卡图片 · JPG / PNG · 不超过 50 MB");
        if (!s_wallpaper_count)
            ui_text(fb, 54, 325, 25, "未找到图片，请放入 TF 卡的 pictures 文件夹", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < 8; ++i) {
            int index = s_wallpaper_page * 8 + i;
            if (index >= s_wallpaper_count) break;
            EpdRect box = {36, 292 + i * 92, 612, 78};
            bool active = !strcmp(s_wallpapers[index].path,s_bookmark_picker?app_settings_bookmark_path():app_settings_wallpaper_path());
            settings_card(fb, box, 18, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[index].name);
            fit_value(name, 490);
            ui_text_vc(fb, 60, box.y + 39, 24, name, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 39, 6, UI_GRAY_BLACK, fb);
        }
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        back_header(fb,s_bookmark_picker?"预览透明书封":"预览壁纸");
        EpdRect image = {166, 194, 352, 626};
        ui_fill_round_rect(fb, image, 8, UI_GRAY_WHITE);
        s_wallpaper_preview_ok = s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count &&
            (s_bookmark_picker ? s_bookmark_preview != NULL : ui_wallpaper_draw(fb, s_wallpapers[s_wallpaper_selected].path, image));
        if(s_bookmark_picker && s_bookmark_preview)book_lock_bookmark_preview_paint(fb,s_bookmark_preview,image);
        ui_draw_round_rect(fb, image, 8, 0x70);
        if (!s_wallpaper_preview_ok)
            ui_text_vc(fb, 342, 507, 23, s_bookmark_picker?"PNG无法加载，请更换图片":"图片无法预览", EPD_DRAW_ALIGN_CENTER, false);
        if (s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count) {
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[s_wallpaper_selected].name);
            fit_value(name, 570);
            ui_text_vc(fb, 342, 859, 24, name, EPD_DRAW_ALIGN_CENTER, false);
            char position[40];
            snprintf(position, sizeof(position), "%d / %d · 上下滑动切换", s_wallpaper_selected + 1, s_wallpaper_count);
            ui_text_vc(fb, 342, 908, 20, position, EPD_DRAW_ALIGN_CENTER, false);
        }
        if (s_wallpaper_preview_ok) {
            EpdRect button = {94, 962, 496, 70};
            ui_fill_round_rect(fb, button, 22, 0x30);
            ui_text_vc(fb, 342, 997, 25, s_bookmark_picker?"使用透明书封":"设为锁屏壁纸", EPD_DRAW_ALIGN_CENTER, true);
        }
        if (s_wallpaper_confirm) {
            EpdRect dialog = {68, 435, 548, 306};
            ui_fill_round_rect(fb, dialog, 26, UI_GRAY_WHITE);
            ui_draw_control_frame(fb, dialog, 26, 0x50);
            ui_text_vc(fb, 342, 503, 31, s_bookmark_picker?"开启透明书封锁屏？":"设为壁纸锁屏？", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 342, 565, 20, s_bookmark_picker?"以锁屏前当前页面为背景":"确认后，短按电源键将显示此图片", EPD_DRAW_ALIGN_CENTER, false);
            EpdRect cancel = {94, 643, 232, 68}, confirm = {358, 643, 232, 68};
            ui_fill_round_rect(fb, cancel, 18, 0xe0);
            ui_fill_round_rect(fb, confirm, 18, 0x30);
            ui_text_vc(fb, 210, 677, 24, "取消", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 474, 677, 24, "确认", EPD_DRAW_ALIGN_CENTER, true);
        }
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_TIME) {
        back_header(fb, "日期与时间");
        time_t now = time(NULL);
        char shown[64] = "未设置";
        if (now >= 1704067200) {
            struct tm tm; time_t local = now + 8 * 3600;
            if (gmtime_r(&local, &tm)) snprintf(shown, sizeof(shown), "%04d-%02d-%02d  %02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        }
        section(fb, 253, "当前时间 · 北京时间");
        row(fb, 296, shown, "");
        row(fb, 407, "通过 WiFi 对时", "立即同步  ›");
        row(fb, 518, "手动设置", "编辑  ›");
        ui_text(fb, 54, 656, 21, "对时成功后由内部时钟持续走时；无需保持联网。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 54, 694, 21, "长时间离线可能产生误差，建议定期重新对时。", EPD_DRAW_ALIGN_LEFT, false);
        if (s_notice[0]) ui_text(fb, 54, 772, 22, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_AUTO_LOCK) {
        back_header(fb, "自动休眠锁屏");
        section(fb, 228, "无操作后自动锁屏");
        const char *labels[] = {"1 分钟", "5 分钟", "10 分钟", "关闭"};
        const uint8_t values[] = {1, 5, 10, 0};
        for (int i = 0; i < 4; ++i) {
            EpdRect box = {36, 272 + i * 112, 612, 92};
            bool active = app_settings_auto_lock_minutes() == values[i];
            settings_card(fb, box, 20, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text_vc(fb, 64, box.y + box.height / 2, 30, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + box.height / 2, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb, 48, 760, 23, "操作后重新计时，传输和升级期间暂停", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 810, 23, "锁屏先浅睡，10 分钟后进入深睡", EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        back_header(fb, "关机睡眠");
        section(fb, 236, "选择电源菜单中“关机”的方式");
        const char *shutdown_labels[] = {"彻底关机", "先浅睡，10 分钟后深睡"};
        const char *shutdown_details[] = {"完全断电，长按电源键开机",
                                          "锁屏画面保留；深睡后短按电源键开机"};
        for (int i = 0; i < 2; ++i) {
            int y = 278 + i * 136;
            bool active = app_settings_staged_shutdown() == (i == 1);
            settings_card(fb, (EpdRect){36, y, 612, 124}, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text(fb, 64, y + 20, 29, shutdown_labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 64, y + 72, 20, shutdown_details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, y + 62, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb, 54, 558, 21, "浅睡时按键直接返回，深睡时会重新开机。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 54, 594, 21, "锁屏样式同时适用于壁纸与阅读票根。", EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_BOOT) {
        back_header(fb, "电脑刷机");
        settings_card(fb, (EpdRect){36, 254, 612, 298}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 288, 29, "进入 BOOT 模式", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 350, 22, "用于官网完整刷机或故障恢复", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){60, 438, 564, 78}, s_boot_pending ? "正在进入" : "进入 BOOT 模式", false);
        ui_text(fb, 48, 620, 22, "请用 USB 连接电脑，再打开官网刷机页", EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb); return;
    }
    if (s_page == SETTINGS_UPGRADE) {
        back_header(fb, "系统升级");
        settings_card(fb, (EpdRect){36, 202, 612, 120}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 224, 22, "当前版本", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 264, 29, esp_app_get_description()->version, EPD_DRAW_ALIGN_LEFT, false);
        if (s_upgrade_confirm) {
            settings_card(fb, (EpdRect){36, 356, 612, s_upgrade_online ? 574 : 344}, 22, UI_GRAY_WHITE, 0x70);
            ui_text_fixed(fb, 60, 383, 31, s_upgrade_online ? "发现新版本" : "确认 TF 卡升级", EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, 60, 447, 28, s_upgrade_online ? s_update.release.version : s_local_update.candidate_version,
                    EPD_DRAW_ALIGN_LEFT, false);
            if (s_upgrade_online) {
                ui_text_fixed(fb, 60, 494, 26, "本次更新", EPD_DRAW_ALIGN_LEFT, false);
                draw_upgrade_notes(fb);
            }
            ui_text_fixed(fb, 60, s_upgrade_online ? 770 : 513, 24, "升级期间请保持供电", EPD_DRAW_ALIGN_LEFT, false);
            ui_text_fixed(fb, 60, s_upgrade_online ? 806 : 560, 23, "设置、阅读记录与 TF 卡文件保留", EPD_DRAW_ALIGN_LEFT, false);
            ui_draw_button(fb, upgrade_confirm_button(s_upgrade_online, false), s_upgrade_online ? "稍后" : "取消", false);
            ui_draw_button(fb, upgrade_confirm_button(s_upgrade_online, true), s_upgrade_online ? "开始更新" : "确认升级", true);
        } else {
            settings_card(fb, (EpdRect){36, 356, 612, 278}, 22, UI_GRAY_WHITE, 0x70);
            ui_text(fb, 60, 383, 29, "联网 OTA", EPD_DRAW_ALIGN_LEFT, false);
            char detail[96];
            snprintf(detail, sizeof(detail), "%s", s_update.message[0] ? s_update.message : "连接 WiFi，检查官网发布的新版本");
            fit_value(detail, 540);
            ui_text(fb, 60, 436, 21, detail, EPD_DRAW_ALIGN_LEFT, false);
            if (s_update.state == PICO_UPDATE_AVAILABLE)
                ui_text(fb, 60, 477, 24, s_update.release.version, EPD_DRAW_ALIGN_LEFT, false);
            if (s_update.state == PICO_UPDATE_DOWNLOADING) draw_upgrade_progress(fb);
            ui_draw_button(fb, (EpdRect){60, 550, 564, 62}, s_update.busy ? "取消联网更新" :
                s_update.state == PICO_UPDATE_AVAILABLE ? "开始更新" : "检查更新", s_update.state == PICO_UPDATE_AVAILABLE);
            settings_card(fb, (EpdRect){36, 662, 612, 262}, 22, UI_GRAY_WHITE, 0x70);
            ui_text(fb, 60, 689, 29, "TF 卡升级", EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 60, 739, 23, "升级包放在 TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 60, 786, 21, s_local_update.ready ? s_local_update.candidate_version : "把官网升级包放入 TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
            ui_draw_button(fb, (EpdRect){60, 840, 564, 62}, s_local_pending ? "正在安装，请勿断电" :
                s_local_update.ready ? "安装 TF 卡升级包" : "重新检查 TF 卡", s_local_update.ready);
        }
        if (s_upgrade_notice[0]) ui_text(fb, 48, 957, 21, s_upgrade_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 1010, 20, "首次安装或修复，请通过官网完整刷机", EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_CONFIG) {
        back_header(fb, "保存与恢复配置");
        section(fb, 248, "换机或刷机后，快速恢复个性化设置");
        settings_card(fb, (EpdRect){36, 299, 612, 197}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 326, 26, "TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 377, 23, "配置备份文件", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 435, 19, "文字排版、设备资料、阅读记录与书签", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36, 561, 612, 83}, "保存当前配置到 TF 卡", false);
        ui_draw_button(fb, (EpdRect){36, 681, 612, 83},
                       s_config_confirm ? "再次点按，确认恢复配置" : "从 TF 卡恢复配置", false);
        if (s_notice[0]) ui_text(fb, 48, 819, 21, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 899, 19, "包含阅读记录、设备资料和已存 WiFi。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 939, 19, "书籍、字体和图片仍需留在 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 979, 19, "备份含 WiFi 密码，请妥善保管 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_BLE_SCAN) {
        ui_nav_back(fb, 36, 79);
        ui_text_vc(fb, 342, 107, 34, "扫描设备", EPD_DRAW_ALIGN_CENTER, false);
        const ble_scan_layout_t l = ble_scan_layout();
        const int scroll = s_ble_scroll;
        // 持续扫描中：告诉用户正在找，以及找到几台。
        // Continuous scan: say that it is running and how many turned up.
        char head[64];
        if (ble_pt_scanning()) snprintf(head, sizeof(head), "正在搜索…已找到 %u 台", (unsigned)ble_scan_visible_count());
        else snprintf(head, sizeof(head), "扫描已停止");
        ui_text(fb, 44, l.status_top - scroll, 23, head, EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < l.device_rows; ++i) {
            const ble_pt_device_t *dev = ble_scan_visible_at(i);
            if (!dev) continue;
            char label[48];
            snprintf(label, sizeof(label), "%s", dev->name);
            fit_value(label, 360);
            char value[44];
            snprintf(value, sizeof(value), "%d dBm%s%s", dev->rssi, dev->hid ? " · HID" : "",
                     dev->bonded ? " · 已配对" : "");
            int top = l.devices_top - scroll + i * BLE_ROW_H;
            if (top + BLE_ROW_H >= 190 && top < settings_viewport_end()) {
                settings_card(fb, (EpdRect){36, top, 612, BLE_ROW_H - 8}, 18, UI_GRAY_WHITE, 0x70);
                ui_text(fb, 60, top + 12, 25, label, EPD_DRAW_ALIGN_LEFT, false);
                ui_text(fb, 60, top + 45, 19, value, EPD_DRAW_ALIGN_LEFT, false);
                ui_draw_icon(fb,(624)-8,top + 38,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
            }
        }
        if (!l.device_rows)
            ui_text(fb, 44, l.devices_top - scroll + 18, 23, "附近还没有发现设备",
                    EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, 190}, UI_GRAY_WHITE, fb);
        ui_nav_status(fb);
        back_header(fb, "扫描设备");
        settings_navigation(fb);
        return;
    }
    if (s_page == SETTINGS_BLUETOOTH) {
        ui_nav_back(fb, 36, 79);
        ui_text_vc(fb, 342, 107, 34, "蓝牙翻页器", EPD_DRAW_ALIGN_CENTER, false);
        const ble_layout_t l = ble_layout();

        // 学习按键时把提示顶到最上面，用户不用滚回去看。
        // While learning, the prompt is pinned to the top so the user need not scroll back.
        if (s_ble_learning || s_ble_feedback[0]) {
            settings_card(fb, (EpdRect){36, 202 - s_ble_scroll, 612, 126}, 22, 0xd0, 0x58);
            ui_text_vc(fb, 342, 265 - s_ble_scroll, 24,
                       !s_ble_learning ? s_ble_feedback :
                       s_ble_learning == 1 ? "请按翻页器上「上一页」要用的键"
                                           : "请按翻页器上「下一页」要用的键",
                       EPD_DRAW_ALIGN_CENTER, false);
            if (!s_ble_learning)
                ui_text_vc(fb, 342, 305 - s_ble_scroll, 19, "轻点此处返回蓝牙开关", EPD_DRAW_ALIGN_CENTER, false);
        } else {
            setting_toggle(fb, l.toggle_top, "启用蓝牙", "开启后可配对蓝牙翻页器；关闭会释放蓝牙内存",
                           app_settings_ble_turner());
        }

        section(fb, l.status_top - 38, "状态");
        char ble_status[80];
        if (!app_settings_ble_turner()) snprintf(ble_status, sizeof(ble_status), "已关闭");
        else if (!ble_pt_running()) {
            read_pico_transfer_status_t network;
            read_pico_transfer_get_status(&network);
            snprintf(ble_status, sizeof(ble_status), network.state != READ_PICO_TRANSFER_STOPPED ? "WiFi 使用中，蓝牙暂停" : "启动中…");
        }
        else if (ble_pt_connected()) snprintf(ble_status, sizeof(ble_status), "已连接 %s", ble_pt_connected_name());
        else if (ble_pt_connecting()) snprintf(ble_status, sizeof(ble_status), "连接中…");
        else snprintf(ble_status, sizeof(ble_status), "未连接");
        ble_row(fb, l.status_top, "当前", ble_status);
        if (l.notice_top >= 0) ui_text(fb, 44, l.notice_top, 21, s_ble_notice, EPD_DRAW_ALIGN_LEFT, false);

        // 已配对：点行连接，右侧按钮删除。/ Bonded peers: the row connects, the button forgets.
        section(fb, l.bonds_title_top, "已配对");
        if (!l.bond_rows)
            ble_row(fb, l.bonds_title_top + 38, "暂无已配对设备", "");
        for (int i = 0; i < l.bond_rows && i < BLE_PT_MAX_BONDS; ++i) {
            const ble_pt_bond_t *bond = ble_pt_bond((uint8_t)i);
            if (!bond) continue;
            char label[40];
            snprintf(label, sizeof(label), "%s", bond->name[0] ? bond->name : bond->addr);
            fit_value(label, 300);
            const bool live = ble_pt_connected() && !strcmp(ble_pt_connected_name(), bond->name);
            ble_row(fb, l.bond_top[i], label, live ? "已连接" : "连接  ›");
            ui_draw_button(fb, ble_button_rect(l.bond_top[i]), "删除", false);
        }

        ble_row(fb, l.scan_entry_top, "添加翻页器", "扫描设备");
        ui_draw_icon(fb,(624)-8,l.scan_entry_top + 38,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);

        section(fb, l.learn_title_top, "按键映射");
        static const char *const learn_names[] = {"上一页", "下一页"};
        static const ble_pt_action_t learn_actions[] = {BLE_PT_ACTION_PREV, BLE_PT_ACTION_NEXT};
        for (int i = 0; i < 2; ++i) {
            // 学到的键直接显示出来；没学过就走内置映射。
            // Show the learned key itself; with none, the built-in map applies.
            char bound_label[40];
            ble_pt_frame_t bound;
            if (ble_pt_binding(learn_actions[i], &bound)) format_bound_key(&bound, bound_label, sizeof(bound_label));
            else snprintf(bound_label, sizeof(bound_label), "内置按键");
            ble_row(fb, l.learn_top[i], learn_names[i], bound_label);
            ui_draw_button(fb, ble_button_rect(l.learn_top[i]), "学习", false);
        }
        ui_text(fb, 48, l.footer_top, 20, "上下、左右及 PageUp / PageDown 均可翻页", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, l.footer_top + 34, 20, "仅支持 BLE 翻页器，按键只在阅读时生效", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, 190}, UI_GRAY_WHITE, fb);
        ui_nav_status(fb);
        back_header(fb, "蓝牙翻页器");
        settings_navigation(fb);
        return;
    }
    const int profile_y=180-s_main_scroll, profile_h=settings_profile_height();
    if (profile_y+profile_h>160) {
        settings_card(fb,(EpdRect){36,profile_y,612,profile_h},24,UI_GRAY_WHITE,0x70);
        int avatar_y=profile_y+(profile_h-82)/2;
        bool avatar_ok=s_avatar_ready;
        if (avatar_ok) ui_wallpaper_draw_avatar(fb,s_avatar_pixels,(EpdRect){57,avatar_y,82,82},20);
        if (!avatar_ok) {
            ui_fill_round_rect(fb,(EpdRect){57,avatar_y,82,82},20,0x30);
            ui_text_fixed_vc(fb,98,avatar_y+41,49,"K",EPD_DRAW_ALIGN_CENTER,true);
        }
        char name[64];snprintf(name,sizeof(name),"%s",app_settings_device_name());
        int width=588-ui_text_fixed_width_px(ui_text_effective_px(20),"编辑")-188;
        while (name[0] && ui_text_fixed_width_px(ui_text_effective_px(32),name)>width) {
            size_t n=strlen(name)-1;while(n&&((unsigned char)name[n]&0xc0)==0x80)--n;name[n]=0;
        }
        ui_text_vc(fb,164,profile_y+53,32,name,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_vc(fb,588,profile_y+53,20,"编辑",EPD_DRAW_ALIGN_RIGHT,false);
        ui_draw_icon(fb,625,profile_y+profile_h/2,26,UI_ICON_CHEVRON_RIGHT,UI_GRAY_BLACK);
    }
    char wifi[56];snprintf(wifi,sizeof(wifi),"%s",s_main_wifi_saved?s_main_wifi:"未配置");
    const char *connection_labels[]={"WiFi","蓝牙翻页器","日期与时间"};
    const char *connection_values[]={wifi,app_settings_ble_turner()?"已开启":"已关闭","设置"};
    const int connection_icons[]={0,1,6};
    setting_group(fb,SETTINGS_WIRELESS_Y-49,"连接与时间",SETTINGS_WIRELESS_Y,
                  connection_icons,connection_labels,connection_values,SETTINGS_WIRELESS_ROWS);
    char font_name[96],contrast[24],idle[24],signature[96];
    snprintf(font_name,sizeof(font_name),"%s · %u%%",system_font_label(app_settings_system_font_path()),app_settings_system_font_size());
    snprintf(contrast,sizeof(contrast),"%u%%",app_settings_system_contrast());
    unsigned minutes=app_settings_auto_lock_minutes();
    if(minutes)snprintf(idle,sizeof(idle),"%u 分钟",minutes);else snprintf(idle,sizeof(idle),"%s","关闭");
    const char *display_labels[]={"系统字体","系统对比度","主页刷新模式","首页强刷","自动休眠锁屏","双击解锁","关机睡眠","语言"};
    const char *display_values[]={font_name,contrast,
        app_settings_main_refresh_mode()==APP_MAIN_REFRESH_FAST?"快刷":app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER?"水波纹":"普通",
        app_settings_home_full_refresh()?"开启":"关闭",idle,"",app_settings_staged_shutdown()?"先浅后深":"彻底断电",app_setup_english()?"English":"简体中文"};
    const int display_icons[]={2,7,15,11,14,SETTINGS_UNLOCK_ICON,9,2};
    setting_group(fb,SETTINGS_DISPLAY_Y-49,"显示",SETTINGS_DISPLAY_Y,
                  display_icons,display_labels,display_values,SETTINGS_DISPLAY_ROWS);
    snprintf(signature,sizeof(signature),"%s",app_settings_status_signature()[0]?app_settings_status_signature():"未设置");
    const char *personal_labels[]={"书架样式","书籍百分比显示","锁屏样式","锁屏密码","状态栏签名"};
    const char *personal_values[]={app_settings_shelf_style()==5?"列表书架":app_settings_shelf_style()==6?"液态玻璃书架":app_settings_shelf_style()==1?"深色书轨":"亚克力书架","",
        app_settings_lock_style()==3?"透明书封":app_settings_lock_style()==2?"书架拼贴":app_settings_lock_style()==1?"壁纸":"阅读票根",
        lock_pin_enabled()?"已开启":"已关闭",signature};
    const int personal_icons[]={4,20,5,16,10};
    setting_group(fb,SETTINGS_PERSONAL_Y-49,"个性化功能",SETTINGS_PERSONAL_Y,
                  personal_icons,personal_labels,personal_values,SETTINGS_PERSONAL_ROWS);
    const char *maintenance_labels[]={"系统升级","保存与恢复","屏幕修复","BOOT 刷机","恢复出厂设置"};
    const char *maintenance_values[]={"OTA / TF 卡","配置","清除条纹","电脑刷机","重置系统"};
    const int maintenance_icons[]={12,8,17,13,11};
    setting_group(fb,SETTINGS_MAINTENANCE_Y-49,"升级和恢复",SETTINGS_MAINTENANCE_Y,
                  maintenance_icons,maintenance_labels,maintenance_values,SETTINGS_MAINTENANCE_ROWS);
    // 主列表也要有一行提示位：确认、修复结果都写在这里，不然点了没有任何回音。
    // The main list needs a notice line too: confirmation and the repair result land here, or a tap
    // would have no answer at all.
    ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,160},UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
    ui_text(fb, 36, 91, 52, "设置", EPD_DRAW_ALIGN_LEFT, false);
    if (s_notice[0]) {
        // 提示移到「设置」标题右侧同一行、中心对齐标题墨迹跨度中点（基线与上下沿实测，任何字体同轨）：
        // 自导入的大字面字体不再与标题或下方列表互相压住——原 y=138 的固定间距按内置字体预留。
        // The notice shares the title's row, centred on the title's measured ink span: tall
        // imported faces no longer collide with the title or the list below — the fixed y=138 gap
        // was reserved for the built-in face.
        const int notice_x = 36 + 2 * ui_text_effective_px(52) + 24;
        ui_text_fixed_vc(fb, notice_x, ui_text_ink_center_y(91, 52, "设置"),
                         20, s_notice, EPD_DRAW_ALIGN_LEFT, false);
    }
    settings_navigation(fb);
}

static void render(app_ctx_t *ctx,uint8_t *fb) {
    render_content(ctx,fb);
    settings_navigation(fb);
}

static void settings_drag_reset(void) {
    free(s_list_drag.bits);
    memset(&s_list_drag,0,sizeof(s_list_drag));
}

// UI坐标与4bpp物理内存分开，旋转后仍只读写列表，不触碰题头/导航。
// Keep logical UI coordinates separate from physical4bpp memory, including rotated list-only copies.
static size_t settings_drag_pixel(int x,int y,int *shift) {
    const int width=s_list_drag.width,height=s_list_drag.height;
    int px=x,py=y;
    switch(s_list_drag.rotation) {
        case EPD_ROT_PORTRAIT:px=width-1-y;py=x;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=width-1-x;py=height-1-y;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=height-1-x;break;
        default:break;
    }
    *shift=(px&1)*4;
    return (size_t)py*(width/2)+px/2;
}

static bool settings_drag_cache(app_ctx_t *ctx) {
    settings_drag_t *drag=&s_list_drag;
    const int width=epd_width(),height=epd_height();
    const enum EpdRotation rotation=epd_get_rotation();
    if(drag->bits) {
        if(drag->width==width&&drag->height==height&&drag->rotation==rotation)return true;
        free(drag->bits);drag->bits=NULL;
    }
    const bool portrait=rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT;
    const int logical_width=portrait?height:width,logical_height=portrait?width:height;
    if(!ctx->hl||!ctx->fb||!ctx->hl->back_fb||ctx->fb==ctx->hl->back_fb||
       width<=0||height<=0||(width&1)||logical_width!=UI_LOCK_WIDTH||
       logical_height!=UI_LOCK_HEIGHT||drag->limit<0||drag->limit>4096)return false;
    const size_t rows=(size_t)drag->area.height+(size_t)drag->limit;
    // 按逻辑列打包，竖向滚动对应Pico物理内存中连续的4bpp字节。
    // Pack logical columns: vertical scrolling then writes contiguous Pico physical4bpp bytes.
    const size_t stride=(rows+7u)/8u;
    const size_t bytes=UI_LOCK_WIDTH*stride;
    if(bytes>SETTINGS_HOLD_CACHE_MAX||
       heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<bytes+128u*1024u)return false;
    uint8_t *bits=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!bits)return false;
    drag->width=width;drag->height=height;drag->rotation=rotation;
    const int saved=*drag->offset;
    // 一次捕获整个有界列表；此后每帧只搬运1bpp，不测字、不生成字形、不缩放头像。
    // Capture the bounded list once; later frames only move1bpp without shaping glyphs or resizing avatars.
    static const uint8_t threshold[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for(int row=0;row<(int)rows;) {
        const int offset=row<drag->limit?row:drag->limit;
        const int skip=row-offset;
        int count=drag->area.height-skip;
        if(count>(int)rows-row)count=(int)rows-row;
        *drag->offset=offset;
        render(ctx,ctx->fb);
        for(int x=0;x<UI_LOCK_WIDTH;++x)for(int y=0;y<count;++y) {
            int shift;
            size_t pixel=settings_drag_pixel(x,drag->area.y+skip+y,&shift);
            unsigned gray=(ctx->fb[pixel]>>shift)&15u;
            if(gray==0||(gray<15&&gray<=threshold[((row+y)&3)*4+(x&3)]))
                bits[(size_t)x*stride+(row+y)/8]|=(uint8_t)(0x80u>>((row+y)&7));
        }
        row+=count;
    }
    *drag->offset=saved;
    // 捕获借用front，恢复真实已显示back；顶部/底栏逐字节保持原样。
    // Restore the actually displayed back after borrowing front, preserving chrome byte-for-byte.
    memcpy(ctx->fb,ctx->hl->back_fb,(size_t)width*height/2);
    drag->bits=bits;
    return true;
}

// 页面空闲时准备缓存，不推屏；已有触摸、未提交目标或头像仍在加载时不抢第一帧。
// Warm the cache without output only while idle, yielding to touch, uncommitted targets and pending avatars.
static void settings_drag_warm(app_ctx_t *ctx,int *offset,int limit) {
    settings_drag_t *drag=&s_list_drag;
    if(drag->bits||drag->active||drag->tracking||drag->disabled||
       !settings_plain_list()||limit<=0||ctx->consumed||ctx->request_app||s_scroll_present_pending||
       (ctx->touch&&ctx->touch->touched)||(s_page==SETTINGS_MAIN&&s_avatar_pending))return;
    const int width=epd_width(),height=epd_height();
    if(!ctx->hl||!ctx->fb||!ctx->hl->back_fb||width<=0||height<=0||
       (size_t)width*height!=(size_t)UI_LOCK_WIDTH*UI_LOCK_HEIGHT||
       memcmp(ctx->fb,ctx->hl->back_fb,(size_t)width*height/2))return;
    const int top=s_page==SETTINGS_MAIN?160:190;
    drag->page=s_page;drag->offset=offset;drag->limit=limit;
    drag->area=(EpdRect){0,top,UI_LOCK_WIDTH,settings_content_end()-top};
    if(!settings_drag_cache(ctx))drag->disabled=true;
}

// 八个黑白位展开为四个4bpp字节；设备与主机测试均为小端。
// Expand eight BW bits to four4bpp bytes on the device/host's little-endian layout.
static uint32_t settings_drag_expand(uint8_t bits) {
    static const uint16_t four[16]={
        0xffff,0x0fff,0xf0ff,0x00ff,0xff0f,0x0f0f,0xf00f,0x000f,
        0xfff0,0x0ff0,0xf0f0,0x00f0,0xff00,0x0f00,0xf000,0x0000};
    return four[bits>>4]|((uint32_t)four[bits&15]<<16);
}

static void settings_drag_blit(app_ctx_t *ctx) {
    const settings_drag_t *drag=&s_list_drag;
    const size_t stride=((size_t)drag->area.height+drag->limit+7u)/8u;
    const bool portrait=drag->rotation==EPD_ROT_PORTRAIT;
    const bool inverted=drag->rotation==EPD_ROT_INVERTED_PORTRAIT;
    if((portrait||inverted)&&!(drag->area.y&1)&&!(drag->area.height&1)) {
        // 每轮输出按物理行连续写入；避免60万次旋转/半字节读改写及跨行PSRAM访问。
        // Write contiguous physical rows, avoiding600k rotations/nibble read-modifies and strided PSRAM writes.
        static const uint8_t pair[4]={0xff,0x0f,0xf0,0};
        // 已提交的黑白帧可直接平移，只展开新露出的边缘；奇数像素/首帧走原完整路径。
        // Shift committed BW pixels and expand only exposed edges; odd shifts/first frames retain the full path.
        int first=0,end=drag->area.height;
        const int delta=*drag->offset-drag->shown_offset;
        const bool reuse=drag->active&&drag->shown_offset>=0&&ctx->hl&&ctx->hl->back_fb&&
            ctx->fb!=ctx->hl->back_fb&&delta&&!(delta%2)&&abs(delta)<drag->area.height;
        const int copy_y=delta>0?0:-delta,copy_count=drag->area.height-abs(delta);
        if(reuse) {
            first=delta>0?copy_count:0;
            end=delta>0?drag->area.height:-delta;
        }
        for(int x=0;x<UI_LOCK_WIDTH;++x) {
            const uint8_t *column=drag->bits+(size_t)x*stride;
            const int row=inverted?drag->height-1-x:x;
            uint8_t *target=ctx->fb+(size_t)row*(drag->width/2);
            if(reuse) {
                const int column_byte=inverted?(drag->area.y+copy_y)/2:
                    (drag->width-drag->area.y-copy_y-copy_count)/2;
                const int source_byte=column_byte+(inverted?delta/2:-delta/2);
                const uint8_t *shown=ctx->hl->back_fb+(size_t)row*(drag->width/2);
                memcpy(target+column_byte,shown+source_byte,(size_t)copy_count/2);
            }
            int y=first;
            for(;y+8<=end;y+=8) {
                const size_t bit=(size_t)*drag->offset+y,index=bit/8;
                const unsigned shift=bit&7;
                uint8_t bits=column[index];
                if(shift)bits=(uint8_t)((bits<<shift)|(column[index+1]>>(8-shift)));
                if(portrait) {
                    bits=(uint8_t)(((bits&0x55u)<<1)|((bits>>1)&0x55u));
                    bits=(uint8_t)(((bits&0x33u)<<2)|((bits>>2)&0x33u));
                    bits=(uint8_t)((bits<<4)|(bits>>4));
                }
                const uint32_t pixels=settings_drag_expand(bits);
                const int column_byte=inverted?(drag->area.y+y)/2:
                    (drag->width-drag->area.y-y-8)/2;
                memcpy(target+column_byte,&pixels,sizeof(pixels));
            }
            for(;y<end;y+=2) {
                const size_t bit=(size_t)*drag->offset+y,index=bit/8;
                const unsigned shift=bit&7;
                uint8_t bits=column[index];
                if(shift)bits=(uint8_t)((bits<<shift)|
                    (index+1<stride?column[index+1]>>(8-shift):0));
                uint8_t pixels=pair[bits>>6];
                if(portrait)pixels=(uint8_t)((pixels<<4)|(pixels>>4));
                target[inverted?(drag->area.y+y)/2:(drag->width-drag->area.y-y)/2-1]=pixels;
            }
        }
        return;
    }
    for(int y=0;y<drag->area.height;++y)for(int x=0;x<UI_LOCK_WIDTH;++x) {
        const size_t bit=(size_t)*drag->offset+y;
        const unsigned gray=(drag->bits[(size_t)x*stride+bit/8]&(0x80u>>(bit&7)))?0u:15u;
        int shift;
        size_t pixel=settings_drag_pixel(x,drag->area.y+y,&shift);
        ctx->fb[pixel]=(uint8_t)((ctx->fb[pixel]&~(15u<<shift))|(gray<<shift));
    }
}

static bool settings_drag_repeated(const settings_drag_t *drag,int dy) {
    return dy && drag->quick_direction == (dy < 0 ? 1 : -1) &&
        drag->press_ms >= drag->quick_release_ms &&
        drag->press_ms-drag->quick_release_ms <= SETTINGS_QUICK_REPEAT_MS;
}

static app_redraw_t settings_drag_sample(app_ctx_t *ctx,int x,int y,bool lifting) {
    settings_drag_t *drag=&s_list_drag;
    if(!drag->tracking)return APP_REDRAW_NONE;
    if(ctx->now_ms<drag->sample_ms)return APP_REDRAW_NONE;
    drag->sample_ms=ctx->now_ms;
    int dy=y-drag->press_y;
    int ax=abs(x-drag->press_x),ay=abs(dy);
    if(ax>drag->travel_x)drag->travel_x=ax;
    if(ay>drag->travel_y)drag->travel_y=ay;
    // 快滑判定期间只记录坐标，松手前不推中间帧。
    // Record motion without painting while the contact can still be a quick flick.
    if(drag->disabled||ctx->now_ms-drag->press_ms<=SETTINGS_QUICK_MS)return APP_REDRAW_NONE;
    int next=drag->origin_offset+drag->origin_y-y;
    if(next<0)next=0;
    if(next>drag->limit)next=drag->limit;
    if(!drag->following) {
        if(abs(dy)<SETTINGS_DRAG_SLOP_PX||abs(dy)<=abs(x-drag->press_x)||
           next==*drag->offset)return APP_REDRAW_NONE;
        if(!lifting&&!drag->active) {
            if(!settings_drag_cache(ctx)){drag->disabled=true;return APP_REDRAW_NONE;}
            drag->active=true;drag->shown_offset=-1;
        }
        drag->following=true;
    }
    *drag->offset=next;
    if(lifting)return APP_REDRAW_NONE;
    if(next==drag->shown_offset||
       (drag->shown_offset>=0&&ctx->now_ms-drag->frame_ms<SETTINGS_HOLD_FRAME_MS))return APP_REDRAW_NONE;
    drag->frame_pending=true;
    return APP_REDRAW_AREA;
}

static app_redraw_t settings_drag_finish(app_ctx_t *ctx) {
    settings_drag_t *drag=&s_list_drag;
    if(drag->tracking&&(drag->active||*drag->offset!=drag->press_offset))
        drag->settle_pending=true;
    drag->tracking=false;
    drag->following=false;
    drag->frame_pending=false;
    return drag->settle_pending&&!(ctx->touch&&ctx->touch->touched)&&!ctx->consumed
        ?APP_REDRAW_AREA:APP_REDRAW_NONE;
}

static bool settings_drag_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev,
    int *offset,int limit,app_redraw_t *redraw) {
    *redraw=APP_REDRAW_NONE;
    if(ev->type==UI_GESTURE_PRESS) {
        const int top=s_page==SETTINGS_MAIN?160:190;
        if(settings_plain_list()&&limit>0&&ev->y0>=top&&ev->y0<settings_content_end()) {
            if(s_list_drag.page!=s_page||s_list_drag.offset!=offset||s_list_drag.limit!=limit)
                settings_drag_reset();
            if(!s_list_drag.bits)s_list_drag.disabled=false;
            s_list_drag.tracking=true;s_list_drag.following=false;
            s_list_drag.page=s_page;s_list_drag.offset=offset;s_list_drag.limit=limit;
            s_list_drag.press_offset=s_list_drag.origin_offset=*offset;s_list_drag.origin_y=ev->y0;
            s_list_drag.press_x=ev->x0;s_list_drag.press_y=ev->y0;
            s_list_drag.press_ms=ctx->now_ms;s_list_drag.travel_x=s_list_drag.travel_y=0;
            s_list_drag.sample_ms=ctx->now_ms;
            s_list_drag.area=(EpdRect){0,top,UI_LOCK_WIDTH,settings_content_end()-top};
        } else if(s_list_drag.settle_pending) {
            s_list_drag.quick_direction=0;
        } else {
            settings_drag_reset();
        }
        return false;
    }
    if(!s_list_drag.tracking)return false;
    if(s_list_drag.page!=s_page){settings_drag_reset();return false;}
    if(ev->type!=UI_GESTURE_MOVE&&ev->type!=UI_GESTURE_LONG_PRESS) {
        const bool lift=s_list_drag.tracking&&ctx->released&&ctx->touch&&
            !ctx->touch->touched&&!ctx->touch->count&&!ctx->consumed&&
            ctx->now_ms>=s_list_drag.sample_ms;
        if(lift)
            (void)settings_drag_sample(ctx,ev->x,ev->y,true);
        bool owned=s_list_drag.following;
        const int dy=(int)ev->y-s_list_drag.press_y,dx=abs((int)ev->x-s_list_drag.press_x);
        const int distance=abs(dy);
        const int horizontal=dx>s_list_drag.travel_x?dx:s_list_drag.travel_x;
        const int64_t elapsed=ctx->now_ms-s_list_drag.press_ms;
        const bool repeated=settings_drag_repeated(&s_list_drag,dy);
        // 未达到跟手门槛的快滑也能翻半屏，但要求更短的接触和明确的纵向位移。
        // Flicks below the drag threshold still step half a viewport on lift if brief and clearly vertical.
        const bool short_flick=elapsed<=SETTINGS_SHORT_QUICK_MS&&distance>2*horizontal;
        s_list_drag.quick_direction=0;
        if(lift&&elapsed<=SETTINGS_QUICK_MS&&distance>=SETTINGS_QUICK_MIN_PX&&distance>horizontal&&
           (distance>=SETTINGS_DRAG_SLOP_PX||short_flick)&&
           (repeated||(distance<=SETTINGS_QUICK_MAX_PX&&s_list_drag.travel_y<=SETTINGS_QUICK_MAX_PX))) {
            int next=s_list_drag.press_offset+(dy<0?SETTINGS_QUICK_STEP_PX:-SETTINGS_QUICK_STEP_PX);
            if(next<0)next=0;
            if(next>s_list_drag.limit)next=s_list_drag.limit;
            *offset=next;
            s_list_drag.quick_direction=dy<0?1:-1;
            s_list_drag.quick_release_ms=ctx->now_ms;
            s_scroll_drag_consumed=false;
            owned=true;
        }
        *redraw=settings_drag_finish(ctx);
        return owned||*redraw!=APP_REDRAW_NONE;
    }
    if(ev->type==UI_GESTURE_MOVE) {
        *redraw=settings_drag_sample(ctx,ev->x,ev->y,false);
        // 快滑期间也接管 MOVE，避免离散滚动兜底提前移动列表。
        // Own MOVE during flick detection so the discrete fallback cannot move the list early.
        return s_list_drag.following||ctx->now_ms-s_list_drag.press_ms<=SETTINGS_QUICK_MS;
    }
    return s_list_drag.following;
}

// 原识别器在静止长按后不再发MOVE；读本轮有效单指快照仍允许长按再拖。
// The recognizer stops MOVE after a stationary long press; valid raw snapshots still allow hold-then-drag.
static app_redraw_t settings_drag_tick(app_ctx_t *ctx) {
    if(s_list_drag.page!=s_page||ctx->request_app){settings_drag_reset();return APP_REDRAW_NONE;}
    if(!s_list_drag.tracking) {
        if(s_list_drag.settle_pending&&!(ctx->touch&&ctx->touch->touched)&&!ctx->consumed)
            return APP_REDRAW_AREA;
        return APP_REDRAW_NONE;
    }
    if(ctx->consumed)return APP_REDRAW_NONE;
    if(!ctx->touch||!ctx->touch->touched||ctx->touch->count!=1)return settings_drag_finish(ctx);
    return settings_drag_sample(ctx,ctx->touch->x,ctx->touch->y,false);
}

static bool settings_drag_present(app_ctx_t *ctx) {
    settings_drag_t *drag=&s_list_drag;
    const bool settle=drag->settle_pending&&!drag->tracking&&
        !(ctx->touch&&ctx->touch->touched);
    if(!drag->frame_pending&&!settle)return false;
    if(ctx->request_app||drag->page!=s_page){settings_drag_reset();return true;}
    display_main_transition_cancel();s_scroll_present_pending=false;
    if(settle) {
        render(ctx,ctx->fb);
        // 复用Pico已有的完整灰阶+12相目标白擦除+4相重推黑；白→白也维护，清理黑白快刷后的光学残影。
        // Reuse Pico's existing full-gray/12-white/four-black sequence, maintaining even white-to-white after temporary BW motion.
        // 松手直接绘制最终灰阶，省掉最后一帧黑白；保留缓存供下次跟手。
        // Draw final grays on lift instead of a final BW frame; keep the cache for the next drag.
        guard_draw_result(ctx->hl,update_display_area_full_with(ctx->hl,
            display_reader_soft_turn_waveform(),MODE_GL16,drag->area));
        drag->active=false;
        drag->settle_pending=false;
        drag->shown_offset=-1;
        s_scroll_drag_consumed=false;
    } else {
        settings_drag_blit(ctx);
        const EpdWaveform* motion=display_settings_motion_waveform();
        // 仅变化像素加强擦除，未变白底和静止字均保持，避免按住拖动时整片白闪。
        // Strengthen changed-pixel erasure while holding unchanged backgrounds/glyphs, avoiding whole-background flashes during held dragging.
        guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,
            motion?motion:&E0470_FOLLOW_WAVEFORM,MODE_DU,drag->area));
        drag->frame_pending=false;drag->shown_offset=*drag->offset;drag->frame_ms=ctx->now_ms;
    }
    return true;
}

// 离散回退若在松手才移动，也走同一灰阶定稿出口。
// The discrete fallback also settles in gray when it first moves on lift.
static void settings_drag_quick(app_ctx_t *ctx) {
    settings_drag_t *drag=&s_list_drag;
    if(settings_plain_list()&&drag->offset&&ctx->released&&!drag->tracking)
        drag->settle_pending=true;
}

static void settings_prepare_profile(void) {
    const char *path=app_settings_avatar_path();
    if(pico_boot_recovery()||!path[0]){ui_avatar_request("",60);s_avatar_ready=s_avatar_pending=false;s_avatar_path[0]=0;return;}
    if(strcmp(path,s_avatar_path))s_avatar_ready=false;
    snprintf(s_avatar_path,sizeof(s_avatar_path),"%s",path);
    ui_avatar_request(path,82);
    ui_avatar_state_t state=ui_avatar_poll(path,82,s_avatar_pixels);
    s_avatar_ready=state==UI_AVATAR_READY;s_avatar_pending=state==UI_AVATAR_PENDING;
}
static void on_enter(app_ctx_t *ctx) {
    settings_drag_reset();
    (void)ctx;
    pin_setup_close();
    s_notice[0] = 0;
    s_ble_feedback[0] = 0;
    s_page = SETTINGS_MAIN;
    s_shelf_style_from_manage=s_shelf_style_requested;s_shelf_style_requested=false;
    if(s_shelf_style_from_manage)s_page=SETTINGS_SHELF_STYLE;
    s_style_scroll = s_main_scroll = s_font_page = s_wallpaper_page = 0;
    s_collage_prepare_pending = false;
    s_scroll_drag_consumed = s_scroll_present_pending = false;
    s_wallpaper_selected = -1;
    s_bookmark_picker=false;bookmark_preview_release();
    s_wallpaper_confirm = s_wallpaper_preview_ok = false;
    s_sync_pending = false;
    s_font_sync_confirm = false; s_font_sync_path[0] = 0;
    s_config_confirm = false;
    s_panel_repair_pending = false;
    s_boot_pending = s_local_pending = s_upgrade_restart = false;
    s_upgrade_offer_pending = s_upgrade_confirm = false;
    (void)read_pico_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved);
    settings_prepare_profile();
}

// 主循环主动消费按键；识别不需要再触摸屏幕。/ Consume key edges on ticks without requiring another touch.
static bool ble_receive_feedback(void) {
    ble_pt_event_t unused;
    while (ble_pt_pop_key(&unused)) {}
    bool changed = false;
    ble_pt_raw_t raw;
    while (ble_pt_pop_raw(&raw)) {
        if (!raw.pressed || !ble_pt_connected()) continue;
        if (s_ble_learning) {
            const ble_pt_action_t action = s_ble_learning == 1 ? BLE_PT_ACTION_PREV : BLE_PT_ACTION_NEXT;
            const esp_err_t error = ble_pt_bind(action, &raw.frame);
            snprintf(s_ble_feedback, sizeof(s_ble_feedback), "%s",
                     error == ESP_OK ? (action == BLE_PT_ACTION_PREV ? "识别成功 · 已绑定上一页" : "识别成功 · 已绑定下一页")
                                     : "识别成功 · 绑定保存失败，请重试");
            s_ble_learning = 0;
            s_ble_scroll = 0;
            changed = true;
            // 同一帧的其余字节不得覆盖这次学习结果。/ Drain remaining bytes without overwriting this learned result.
            while (ble_pt_pop_raw(&raw)) {}
            break;
        }
        if (!s_ble_feedback[0]) {
            snprintf(s_ble_feedback, sizeof(s_ble_feedback), "识别成功 · 已收到翻页器按键");
            s_ble_scroll = 0;
            changed = true;
        }
    }
    return changed;
}

static app_redraw_t profile_editor_paint(app_ctx_t *ctx, bool field);
static app_redraw_t reset_tick(void) {
    s_reset_pending=false;
    read_pico_transfer_status_t state;read_pico_transfer_get_status(&state);
    esp_err_t error=ESP_ERR_INVALID_STATE;
    if(!pico_online_busy() && state.state!=READ_PICO_TRANSFER_UPLOADING &&
       book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000)) {
        ble_pt_cancel_recent();
        error=ble_pt_stop(2000);
        if(error==ESP_OK) {
            read_pico_transfer_stop();
            error=app_factory_reset();
        }
    }
    if(error==ESP_OK) {esp_restart();return APP_REDRAW_NONE;}
    s_reset_confirm=false;
    snprintf(s_reset_notice,sizeof(s_reset_notice),"恢复未完成，请稍后重试：%s",esp_err_to_name(error));
    return APP_REDRAW_PAGE;
}

static app_redraw_t on_tick(app_ctx_t *ctx) {
    if(s_list_drag.tracking||s_list_drag.settle_pending) {
        app_redraw_t redraw=settings_drag_tick(ctx);
        if(redraw!=APP_REDRAW_NONE||s_list_drag.settle_pending)return redraw;
    }
    // 屏幕修复：先画最终结果，再在 tick 中整屏恢复；成功后不重复推页。
    // Paint the final notice before the tick's full restoration; do not push the page twice on success.
    if (s_panel_repair_pending) {
        s_panel_repair_pending = false;
        if (s_page != SETTINGS_MAIN) return APP_REDRAW_NONE;
        s_scroll_present_pending = false;
        snprintf(s_notice, sizeof(s_notice), "屏幕修复完成；若条纹仍在可再做一次");
        render(ctx, ctx->fb);
        if (display_panel_repair(ctx->hl) != EPD_DRAW_SUCCESS) {
            snprintf(s_notice, sizeof(s_notice), "屏幕输出未完成，请稍后重试");
            return APP_REDRAW_FULL;
        }
        return APP_REDRAW_DONE;
    }

    if (s_page==SETTINGS_MAIN && s_avatar_pending && !(ctx->touch && ctx->touch->touched)) {
        ui_avatar_state_t state=ui_avatar_poll(s_avatar_path,82,s_avatar_pixels);
        s_avatar_pending=state==UI_AVATAR_PENDING;
        s_avatar_ready=state==UI_AVATAR_READY;
        if (s_avatar_ready && s_main_scroll < 180+settings_profile_height()-160) {
            settings_drag_reset();
            render(ctx,ctx->fb);
            EpdRect area={57,180-s_main_scroll+(settings_profile_height()-82)/2,82,82};
            if (area.y<160) { area.height-=160-area.y; area.y=160; }
            if(area.height>0)guard_draw_result(ctx->hl,update_display_area_diff_with(ctx->hl,
                app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL?display_main_normal_waveform():display_main_gray_waveform(),MODE_GL16,area));
            return APP_REDRAW_DONE;
        }
    }
    if(s_reset_pending && s_page==SETTINGS_FACTORY_RESET)return reset_tick();
    if (s_page == SETTINGS_PIN_ENTRY) return pin_setup_tick(ctx);
    if (s_collage_prepare_pending && s_page == SETTINGS_LOCK_STYLE) {
        s_collage_prepare_pending = false;
        // 先让选中反馈可见，再生成卡上缓存；render 本身只绘图。
        // Show selection feedback first, then prepare the SD cache; render stays paint-only.
        const size_t bytes = UI_LOCK_WIDTH * UI_LOCK_HEIGHT / 2u;
        size_t available = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t *preview = available > bytes + 1024u * 1024u
            ? heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
        if (preview) {
            app_lock_font_t saved;
            app_font_begin_lock(&saved);
            (void)book_lock_collage_draw(preview);
            app_font_end_lock(&saved);
            free(preview);
        }
        return APP_REDRAW_NONE;
    }

    if (s_page == SETTINGS_TEXT_EDIT) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return profile_editor_paint(ctx, false);
        return ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED
            ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BLUETOOTH && ble_receive_feedback()) return APP_REDRAW_PAGE;
    // 蓝牙启动失败的原因转成提示。放在这里而不是 render()：render 必须是纯绘制，
    // 而 take_* 会清空状态。
    // Fold a Bluetooth start failure into the notice here rather than in render(), which must
    // stay pure, and because take_* clears the state.
    // 蓝牙页要跟着栈的状态重绘：开关拨开后栈是异步起来的，配对表、扫描结果和连接状态
    // 都在页面画完以后才变。不重绘的话用户看到的永远是进页那一刻的快照，只能退出去再进来。
    // The Bluetooth pages redraw with the stack: enabling it starts it asynchronously, and the
    // bond list, scan results and link state all change after the page was painted. Without a
    // poll the user only ever sees the snapshot from when they entered, and has to leave and
    // come back.
    if (s_page == SETTINGS_BLUETOOTH || s_page == SETTINGS_BLE_SCAN) {
        static uint32_t ble_seen;
        const uint32_t sig = (app_settings_ble_turner() ? 1u : 0u) | (ble_pt_running() ? 2u : 0u) |
                             (ble_pt_connected() ? 4u : 0u) | (ble_pt_connecting() ? 8u : 0u) |
                             (ble_pt_scanning() ? 16u : 0u) |
                             ((uint32_t)ble_pt_bond_count() << 8) |
                             ((uint32_t)ble_scan_visible_count() << 16);
        if (sig != ble_seen) {
            ble_seen = sig;
            return APP_REDRAW_PAGE;
        }
    }
    if (s_page == SETTINGS_BLUETOOTH && !s_ble_notice[0]) {
        char why[64];
        if (ble_pt_take_start_failure(why, sizeof(why))) {
            snprintf(s_ble_notice, sizeof(s_ble_notice), "%s", why);
            return APP_REDRAW_PAGE;
        }
    }

    (void)ctx;
    if (s_boot_pending) {
        s_boot_pending = false;
        pmu_selftest_prepare_powerdown();
        uint8_t req[2] = {0, 0};
        esp_err_t error = read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_RESET, req, sizeof(req));
        if (error != ESP_OK) ESP_LOGW(TAG, "BOOT PMU notice: %s", esp_err_to_name(error));
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart(); return APP_REDRAW_NONE;
    }
    if (s_upgrade_restart) { s_upgrade_restart = false; esp_restart(); return APP_REDRAW_NONE; }
    if (s_page == SETTINGS_UPGRADE) {
        if (s_upgrade_job != UPGRADE_JOB_NONE) return upgrade_start_pending();
        if (s_local_pending) {
            s_local_pending = false;
            esp_err_t error = pico_ota_install(PICO_OTA_UPDATE_PATH, s_upgrade_notice, sizeof(s_upgrade_notice));
            s_upgrade_restart = error == ESP_OK;
            return APP_REDRAW_PAGE;
        }
        pico_online_get_status(&s_update);
        if (s_update.state == PICO_UPDATE_READY && !s_update.busy) {
            if (pico_online_commit() == ESP_OK) { esp_restart(); return APP_REDRAW_NONE; }
            pico_online_get_status(&s_update);
        }
        uint32_t now = esp_timer_get_time() / 1000;
        app_redraw_t redraw = upgrade_status_redraw(now);
        if (redraw == APP_REDRAW_AREA) draw_upgrade_progress_delta(ctx->fb);
        return redraw;
    }
    if (!s_sync_pending) {
        if(settings_plain_list()) {
            int *offset=s_page==SETTINGS_MAIN?&s_main_scroll:&s_ble_scroll;
            int limit=s_page==SETTINGS_MAIN?SETTINGS_SCROLL_MAX:
                (s_page==SETTINGS_BLUETOOTH?ble_layout().content_bottom+s_ble_scroll:
                    ble_scan_layout().content_bottom)-settings_content_end()+12;
            settings_drag_warm(ctx,offset,limit);
        }
        return APP_REDRAW_NONE;
    }
    s_sync_pending = false;
    uint32_t utc = 0;
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    esp_err_t err = read_pico_transfer_sync_time(&utc);
    bool network_time_received = err == ESP_OK;
    if (network_time_received) {
        // 临时 WiFi 收尾可能耗时；写入 PMU 时取当前 S3 时间，避免回写旧秒数。
        // Temporary WiFi teardown takes time; seed the PMU from the current S3 clock.
        utc = (uint32_t)time(NULL);
        uint8_t payload[4] = {(uint8_t)utc, (uint8_t)(utc >> 8), (uint8_t)(utc >> 16), (uint8_t)(utc >> 24)};
        err = read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
        if (err == ESP_OK) err = read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
        if (err == ESP_OK) {
            const pmu_snapshot_t *pmu = read_pico_pmu_get();
            if (!pmu || !pmu->time_synced || pmu->unix_sec < utc || pmu->unix_sec - utc > 10)
                err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGI(TAG, "time sync network=%d ready=%d utc=%lu result=%s",
             network.mode, network.network_ready, (unsigned long)utc, esp_err_to_name(err));
    const char *notice = err == ESP_OK ? "WiFi 对时完成，内部时钟已接管" :
        network_time_received ? "已获取网络时间，内部时钟写入失败" :
        err == ESP_ERR_NOT_FOUND ? "请先在 WiFi 设置中配置网络" :
        err == ESP_ERR_INVALID_STATE ? "WiFi 尚未连接，请稍后再试" :
        err == ESP_ERR_TIMEOUT ? "网络对时超时，请检查网络后重试" :
        "网络对时失败，请检查网络后重试";
    snprintf(s_notice, sizeof(s_notice), "%s", notice);
    return APP_REDRAW_PAGE;
}

static EpdRect s_input_area;
static bool s_input_layout, s_input_settle;
static app_redraw_t profile_editor_paint(app_ctx_t *ctx, bool field) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, UI_SYSTEM_KEYBOARD_TOP, (EpdRect){36, 243, 612, 82}, 28, false, NULL, field);
    s_input_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t profile_editor_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type == UI_GESTURE_PRESS)
        return ui_keyboard_press(ev->x0, ev->y0, UI_SYSTEM_KEYBOARD_TOP, ctx->now_ms) ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    bool feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
    if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
        bool page = ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
        return (page || feedback) ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_TAP) return feedback ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
    int x = ev->x0, y = ev->y0;
    if (y < 160) {
        if (x < 160) { ui_keyboard_end(); s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
        if (x > 510) goto save_text;
    }
    if (ui_text_input_tap(&s_editor_input, (EpdRect){36, 243, 612, 82}, 28,
                          false, NULL, x, y)) return profile_editor_paint(ctx, true);
    ui_keyboard_result_t result = ui_keyboard_tap(x, y, UI_SYSTEM_KEYBOARD_TOP, esp_timer_get_time() / 1000);
    if (result == UI_KEYBOARD_DONE) goto save_text;
    return result == UI_KEYBOARD_CHANGED ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
save_text:
    if (ui_keyboard_pending()) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "请先选择候选字");
        return APP_REDRAW_PAGE;
    }
    if (s_editor_signature) app_settings_set_status_signature(s_editor);
    else if (s_editor[0]) app_settings_set_device_name(s_editor);
    else { snprintf(s_editor_notice, sizeof(s_editor_notice), "设备名称不能为空"); return APP_REDRAW_PAGE; }
    const char *saved = s_editor_signature ? app_settings_status_signature() : app_settings_device_name();
    if (strcmp(saved, s_editor)) {
        snprintf(s_editor_notice, sizeof(s_editor_notice), "保存失败，请检查设置存储空间");
        return APP_REDRAW_PAGE;
    }
    ui_keyboard_end();
    s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE;
    return APP_REDRAW_PAGE;
}

// 越过阈值按段推进，一次手势只更新一次，松手不再播放惯性。
// Advance one discrete step after crossing the threshold, with one update per gesture and no release inertia.
static app_redraw_t scroll_gesture(const ui_gesture_event_t *ev, int *offset, int limit, bool *handled) {
    *handled = ev->type != UI_GESTURE_TAP || s_scroll_drag_consumed;
    if (ev->type == UI_GESTURE_PRESS) {
        s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_CANCEL) {
        s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    if (ev->type != UI_GESTURE_MOVE && ev->type != UI_GESTURE_SWIPE_U && ev->type != UI_GESTURE_SWIPE_D)
        return APP_REDRAW_NONE;
    if (ev->y0 < (s_page == SETTINGS_MAIN ? 160 : 190) || ev->y0 >= settings_viewport_end())
        return APP_REDRAW_NONE;
    if (s_scroll_drag_consumed) {
        if (ev->type != UI_GESTURE_MOVE) s_scroll_drag_consumed = false;
        return APP_REDRAW_NONE;
    }
    int dy = (int)ev->y - ev->y0, dx = (int)ev->x - ev->x0;
    int distance = abs(dy);
    if (ev->type == UI_GESTURE_MOVE && (distance < 108 || distance <= abs(dx))) return APP_REDRAW_NONE;
    int step = distance < 204 ? 204 : distance > 450 ? 450 : distance;
    bool up = ev->type == UI_GESTURE_SWIPE_U || (ev->type == UI_GESTURE_MOVE && dy < 0);
    int next = *offset + (up ? step : -step);
    if (limit < 0) limit = 0;
    if (next < 0) next = 0;
    if (next > limit) next = limit;
    if (ev->type == UI_GESTURE_MOVE) s_scroll_drag_consumed = true;
    if (next == *offset) return APP_REDRAW_NONE;
    *offset = next;
    s_scroll_present_pending = true;
    return APP_REDRAW_AREA;
}

static app_redraw_t reset_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(ev->type!=UI_GESTURE_TAP || s_reset_pending)return APP_REDRAW_NONE;
    int tab=settings_nav_hit(ev->x0,ev->y0);
    if(tab>=0){s_reset_confirm=false;ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
    if((ev->y0<190 && ev->x0<170)||ui_rect_hit((EpdRect){54,950,576,78},ev->x0,ev->y0)) {
        s_reset_confirm=false;s_page=SETTINGS_MAIN;return APP_REDRAW_PAGE;
    }
    if(ui_rect_hit((EpdRect){54,850,576,78},ev->x0,ev->y0)) {
        s_reset_notice[0]=0;
        if(s_reset_confirm)s_reset_pending=true;
        else s_reset_confirm=true;
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_page==SETTINGS_SYSTEM_FONT && s_font_sync_confirm) {
        if (ev->type!=UI_GESTURE_TAP) return APP_REDRAW_NONE;
        for (int i=0;i<2;++i) {
            if (!ui_rect_hit(system_font_sync_button(i),ev->x0,ev->y0)) continue;
            if (i) {
                app_settings_set_font_path(s_font_sync_path);
                if (strcmp(app_settings_font_path(),s_font_sync_path)) {
                    snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;
                }
            }
            s_notice[0]=0;s_font_sync_confirm=false;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_PIN_ENTRY) {
        EpdRect dirty; ui_pin_result_t result = ui_pinpad_handle(&s_pinpad, ev, &dirty);
        if (result == UI_PIN_CANCEL) { pin_setup_close(); s_page = SETTINGS_LOCK_PIN; return APP_REDRAW_PAGE; }
        if (result == UI_PIN_COMPLETE) s_pin_pending = true;
        if (result == UI_PIN_NONE) return APP_REDRAW_NONE;
        ui_pinpad_paint_input(ctx->fb, &s_pinpad);
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, dirty));
        return APP_REDRAW_DONE;
    }
    if (s_page == SETTINGS_LOCK_PIN && ev->type == UI_GESTURE_TAP && lock_pin_available() &&
        (ui_rect_hit((EpdRect){36,406,612,70},ev->x0,ev->y0) ||
         (lock_pin_enabled() && ui_rect_hit((EpdRect){36,512,612,70},ev->x0,ev->y0)))) {
        if (ctx->now_ms < s_pin_retry_at) { snprintf(s_notice, sizeof(s_notice), "请等待后重试"); return APP_REDRAW_PAGE; }
        pin_setup_open(ctx, ev->y0 >= 512); return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_LOCK_PIN && ev->type == UI_GESTURE_TAP && lock_pin_enabled() &&
        ui_rect_hit((EpdRect){36, 606, 612, 84}, ev->x0, ev->y0)) {
        app_settings_set_pin_grace_code((app_settings_pin_grace_code() + 1) % 5);
        return APP_REDRAW_PAGE;
    }

    if (s_page == SETTINGS_BOOT) {
        if (ev->type != UI_GESTURE_TAP || s_boot_pending) return APP_REDRAW_NONE;
        if (ev->y0 < 190 && ev->x0 < 170) { s_page = SETTINGS_MAIN; (void)read_pico_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
        if (ui_rect_hit((EpdRect){60, 438, 564, 78}, ev->x0, ev->y0)) { s_boot_pending = true; return APP_REDRAW_PAGE; }
        int tab = settings_nav_hit(ev->x0, ev->y0);
        if (tab >= 0) ui_nav_request(ctx, tab);
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_UPGRADE) {
        if (ev->type != UI_GESTURE_TAP || s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        int y = ev->y0;
        if (y < 190 && ev->x0 < 170) {
            s_upgrade_job = UPGRADE_JOB_NONE;
            s_upgrade_offer_pending = false;
            if (s_upgrade_confirm) s_upgrade_confirm = false;
            else { pico_online_cancel_join(); s_page = SETTINGS_MAIN; }
            return APP_REDRAW_PAGE;
        }
        int tab = settings_nav_hit(ev->x0, y);
        if (tab >= 0) { s_upgrade_job = UPGRADE_JOB_NONE; pico_online_cancel_join(); ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        if (s_upgrade_confirm) {
            bool accept = ui_rect_hit(upgrade_confirm_button(s_upgrade_online, true), ev->x0, y);
            if (accept || ui_rect_hit(upgrade_confirm_button(s_upgrade_online, false), ev->x0, y)) {
                s_upgrade_confirm = false;
                s_upgrade_offer_pending = false;
                if (accept) {
                    if (s_upgrade_online) {
                        upgrade_schedule(true, esp_timer_get_time() / 1000);
                    } else s_local_pending = true;
                }
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (ev->x0 < 60 || ev->x0 >= 624) return APP_REDRAW_NONE;
        if (y >= 550 && y < 612) {
            s_upgrade_notice[0] = 0;
            if (s_update.busy) { s_upgrade_job = UPGRADE_JOB_NONE; s_upgrade_offer_pending = false; pico_online_cancel_join(); }
            else if (s_update.state == PICO_UPDATE_AVAILABLE) { s_upgrade_confirm = true; s_upgrade_online = true; }
            else {
                upgrade_schedule(false, esp_timer_get_time() / 1000);
                return APP_REDRAW_PAGE;
            }
            pico_online_get_status(&s_update);
            upgrade_remember(esp_timer_get_time() / 1000);
            return APP_REDRAW_PAGE;
        }
        if (y >= 840 && y < 902 && !s_update.busy) {
            (void)pico_ota_inspect(PICO_OTA_UPDATE_PATH, &s_local_update);
            if (s_local_update.ready) { s_upgrade_confirm = true; s_upgrade_online = false; }
            else snprintf(s_upgrade_notice, sizeof(s_upgrade_notice), "%s", s_local_update.message);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_page==SETTINGS_FACTORY_RESET)return reset_gesture(ctx,ev);
    if (s_page == SETTINGS_TEXT_EDIT) return profile_editor_gesture(ctx, ev);
    if (s_page == SETTINGS_MAIN || s_page == SETTINGS_BLUETOOTH || s_page == SETTINGS_BLE_SCAN || s_page == SETTINGS_SHELF_STYLE) {
        int limit = SETTINGS_SCROLL_MAX;
        int *offset = &s_main_scroll;
        if (s_page == SETTINGS_SHELF_STYLE) {
            limit=263+SHELF_STYLE_CHOICES*253-23-settings_viewport_end()+12;
            offset=&s_style_scroll;
        } else if (s_page != SETTINGS_MAIN) {
            int bottom = s_page == SETTINGS_BLUETOOTH ? ble_layout().content_bottom + s_ble_scroll : ble_scan_layout().content_bottom;
            limit = bottom - settings_content_end() + 12;
            offset = &s_ble_scroll;
        }
        bool handled;
        app_redraw_t motion;
        if(settings_drag_gesture(ctx,ev,offset,limit,&motion))return motion;
        app_redraw_t redraw = scroll_gesture(ev, offset, limit, &handled);
        if(redraw==APP_REDRAW_AREA&&s_list_drag.tracking) {
            s_list_drag.origin_offset=*offset;s_list_drag.origin_y=ev->y;
        }
        if (handled) return redraw;
    }

    if (s_page == SETTINGS_AVATAR && (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int pages = (s_wallpaper_count + 6) / 7;
        int old = s_wallpaper_page;
        if (ev->type == UI_GESTURE_SWIPE_U && s_wallpaper_page + 1 < pages) ++s_wallpaper_page;
        if (ev->type == UI_GESTURE_SWIPE_D && s_wallpaper_page > 0) --s_wallpaper_page;
        if (s_wallpaper_page == old) return APP_REDRAW_NONE;
        s_scroll_present_pending = true;
        return APP_REDRAW_AREA;
    }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW && !s_wallpaper_confirm &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int next = s_wallpaper_selected + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
        if (next < 0 || next >= s_wallpaper_count) return APP_REDRAW_NONE;
        s_wallpaper_selected = next;
        bookmark_preview_prepare(ctx->fb);
        s_wallpaper_page = next / 8;
        return APP_REDRAW_PAGE;
    }
    if ((s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_WALLPAPER) &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int *page = s_page == SETTINGS_SYSTEM_FONT ? &s_font_page : &s_wallpaper_page;
        int count = s_page == SETTINGS_SYSTEM_FONT ? ttf_font_count() + 1 : s_wallpaper_count;
        int slots=s_page==SETTINGS_SYSTEM_FONT?system_font_layout().slots:8;
        int pages = count > 0 ? (count + slots - 1) / slots : 1;
        int old = *page;
        if (ev->type == UI_GESTURE_SWIPE_U && *page + 1 < pages) ++*page;
        if (ev->type == UI_GESTURE_SWIPE_D && *page > 0) --*page;
        if (*page == old) return APP_REDRAW_NONE;
        s_scroll_present_pending = true;
        return APP_REDRAW_AREA;
    }
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (s_page == SETTINGS_WALLPAPER_PREVIEW && s_wallpaper_confirm) {
        if (ev->y0 >= 643 && ev->y0 < 711 && ev->x0 >= 358 && ev->x0 < 590) {
            const char *path = s_wallpapers[s_wallpaper_selected].path;
            if(s_bookmark_picker)app_settings_set_bookmark_path(path);
            else app_settings_set_wallpaper_path(path);
            if (!strcmp(s_bookmark_picker?app_settings_bookmark_path():app_settings_wallpaper_path(), path)) {
                app_settings_set_lock_style(s_bookmark_picker?3:1);
                s_page = SETTINGS_LOCK_STYLE;
                bookmark_preview_release();
            }
        }
        s_wallpaper_confirm = false;
        return APP_REDRAW_PAGE;
    }
    if(s_page==SETTINGS_LANGUAGE&&ev->type==UI_GESTURE_TAP){
        if(ev->y0<190){s_page=SETTINGS_MAIN;s_notice[0]=0;return APP_REDRAW_PAGE;}
        for(int i=0;i<2;++i)if(ui_rect_hit((EpdRect){36,300+i*154,612,122},ev->x0,ev->y0)){
            if(app_setup_set_english(i==1)!=ESP_OK){snprintf(s_notice,sizeof(s_notice),"语言保存失败，请重试");return APP_REDRAW_PAGE;}
            s_notice[0]=0;return APP_REDRAW_FULL;
        }
    }
    if (s_page == SETTINGS_TIME_EDIT) {
        if (ev->y0 < 190) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
        if (ev->y0 >= 354 && ev->y0 < 597) {
            int field = (ev->x0 - 36) / 122;
            if (ev->x0 >= 36 && field >= 0 && field < 5 && (ev->x0 - 36) % 122 < 106) {
                if (ev->y0 < 416) time_adjust(field, 1);
                else if (ev->y0 >= 535) time_adjust(field, -1);
                else return APP_REDRAW_NONE;
                return APP_REDRAW_PAGE;
            }
        }
        if (ev->y0 >= 767 && ev->y0 < 845) { (void)time_save(); return APP_REDRAW_PAGE; }
        if (ev->y0 >= 874 && ev->y0 < 952) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    int tab = settings_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    int y = ev->y0;
    if (s_page == SETTINGS_PROFILE) {
        if(y<190)return shelf_style_finish(ctx);
        if (y >= 266 && y < 350) { profile_editor_open(false); return APP_REDRAW_PAGE; }
        if (y >= 366 && y < 450) { s_bookmark_picker=false;wallpaper_scan(); s_page = SETTINGS_AVATAR; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_AVATAR) {
        if (y < 190) { s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
        if (y >= 270 && y < 354) { app_settings_set_avatar_path(""); s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
        if (y >= 374 && y < 1046) {
            int i = (y - 374) / 96, index = s_wallpaper_page * 7 + i;
            if (index >= 0 && index < s_wallpaper_count && (y - 374) % 96 < 80) {
                app_settings_set_avatar_path(s_wallpapers[index].path);
                s_page = SETTINGS_PROFILE;
                return APP_REDRAW_PAGE;
            }
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        if (y < 190) { bookmark_preview_release(); s_page = SETTINGS_WALLPAPER; return APP_REDRAW_PAGE; }
        if (s_wallpaper_preview_ok && y >= 194 && y < 1032) {
            s_wallpaper_confirm = true;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_WALLPAPER && y < 190) {
        s_page = SETTINGS_LOCK_STYLE;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_SHELF_STYLE) {
        if(y<190)return shelf_style_finish(ctx);
        for (int i = 0; i < SHELF_STYLE_CHOICES; ++i) {
            if (y >= 242 && y >= 263 + i * 253 - s_style_scroll && y < 493 + i * 253 - s_style_scroll && y < settings_viewport_end() && ev->x0 >= 36 && ev->x0 < 648) {
                app_settings_set_shelf_style(s_shelf_style_ids[i]);
                if(app_settings_shelf_style()!=s_shelf_style_ids[i]){snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;}
                return shelf_style_finish(ctx);
            }
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BLE_SCAN && y < 190) {
        ble_pt_scan_stop(); s_page = SETTINGS_BLUETOOTH; s_ble_scroll = 0; return APP_REDRAW_PAGE;
    }
    if (s_page != SETTINGS_MAIN && y < 190) { s_page = SETTINGS_MAIN; (void)read_pico_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_LOCK_PIN) return APP_REDRAW_NONE;
    if (s_page == SETTINGS_AUTO_LOCK) {
        const uint8_t values[] = {1, 5, 10, 0};
        for (int i = 0; i < 4; ++i) if (ui_rect_hit((EpdRect){36, 272 + i * 112, 612, 92}, ev->x0, y)) {
            app_settings_set_auto_lock_minutes(values[i]);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }

    if (s_page == SETTINGS_WIFI) {
        if (y >= 408 && y < 478) {
            extern const app_desc_t app_transfer;
            app_transfer_request_wifi_setup();
            ctx->request_app = &app_transfer;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_FONT) {
        system_font_layout_t l=system_font_layout();
        if (ui_rect_hit(l.size,ev->x0,y)) {
            unsigned prior=app_settings_system_font_size();
            unsigned next=prior>=200?100:prior+10;
            app_settings_set_system_font_size(next);
            if (app_settings_system_font_size()!=next) {
                snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;
            }
            s_font_page=0;
            if (s_main_scroll>SETTINGS_SCROLL_MAX) s_main_scroll=SETTINGS_SCROLL_MAX;
            ui_text_set_system_scale(true);s_notice[0]=0;return APP_REDRAW_PAGE;
        }
        for (int slot=0;slot<l.slots;++slot) {
            if (!ui_rect_hit(system_font_item_rect(slot),ev->x0,y)) continue;
            int index=s_font_page*l.slots+slot;
            if (index>ttf_font_count()) return APP_REDRAW_NONE;
            const ttf_font_item_t *item=index?ttf_font_item(index-1):NULL;
            if (index && !item) return APP_REDRAW_NONE;
            snprintf(s_font_sync_path,sizeof(s_font_sync_path),"%s",item?item->path:"");
            app_settings_set_system_font_path(s_font_sync_path);
            if (strcmp(app_settings_system_font_path(),s_font_sync_path)) {
                snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");return APP_REDRAW_PAGE;
            }
            app_font_activate_system();s_notice[0]=0;
            s_font_sync_confirm=strcmp(app_settings_font_path(),s_font_sync_path)!=0;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_MAIN_REFRESH) {
        for (int i = 0; i < 3; ++i) {
            if (!ui_rect_hit((EpdRect){36, 298 + i * 158, 612, 132}, ev->x0, y)) continue;
            app_settings_set_main_refresh_mode((app_main_refresh_mode_t)i);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_SIZE) {
        for (int index = 0; index < 11; ++index) {
            EpdRect box = {36 + (index % 2) * 316, 298 + (index / 2) * 122, 296, 102};
            if (!ui_rect_hit(box, ev->x0, y)) continue;
            app_settings_set_system_font_size((uint8_t)(100 + index * 10));
            ui_text_set_system_scale(true);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_SYSTEM_CONTRAST) {
        if (y < 298 || y >= 878) return APP_REDRAW_NONE;
        int index = (y - 298) / 116;
        app_settings_set_system_contrast((uint8_t)(100 + index * 10));
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_LOCK_STYLE) {
        if (y >= 300 && y < 432) {
            s_bookmark_picker=false;wallpaper_scan();
            s_page = SETTINGS_WALLPAPER;
            return APP_REDRAW_PAGE;
        }
        if (y >= 454 && y < 586) {
            app_settings_set_lock_style(0);
            return APP_REDRAW_PAGE;
        }
        if (y >= 608 && y < 740) {
            app_settings_set_lock_style(2);
            s_collage_prepare_pending = true;
            return APP_REDRAW_PAGE;
        }
        if(y>=762 && y<894){
            s_bookmark_picker=true;wallpaper_scan();s_page=SETTINGS_WALLPAPER;
            return APP_REDRAW_PAGE;
        }
        if (y >= 1010 && app_settings_wallpaper_path()[0]) {
            s_bookmark_picker=false;wallpaper_scan();
            s_page = SETTINGS_WALLPAPER;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_WALLPAPER) {
        if (y < 292 || y >= 1028) return APP_REDRAW_NONE;
        int index = s_wallpaper_page * 8 + (y - 292) / 92;
        if (index >= s_wallpaper_count) return APP_REDRAW_NONE;
        s_wallpaper_selected = index;
        bookmark_preview_prepare(ctx->fb);
        s_wallpaper_confirm = false;
        s_page = SETTINGS_WALLPAPER_PREVIEW;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_TIME) {
        if (y >= 407 && y < 477) {
            s_notice[0] = 0; s_sync_pending = true;
            snprintf(s_notice, sizeof(s_notice), "正在获取网络时间…");
            return APP_REDRAW_PAGE;
        }
        if (y >= 518 && y < 588) { time_open(); return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        if (y >= 278 && y < 402) {
            app_settings_set_staged_shutdown(false);
            return APP_REDRAW_PAGE;
        }
        if (y >= 414 && y < 538) {
            app_settings_set_staged_shutdown(true);
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_CONFIG) {
        if (y >= 561 && y < 644) {
            s_config_confirm = false;
            esp_err_t err = app_settings_backup_save();
            snprintf(s_notice, sizeof(s_notice), "%s",
                     err == ESP_OK ? "已保存到 TF 卡根目录" :
                     err == ESP_ERR_INVALID_STATE ? "未识别到 TF 卡，请插卡后重试" :
                     "保存失败，请检查 TF 卡剩余空间");
            return APP_REDRAW_PAGE;
        }
        if (y >= 681 && y < 764) {
            if (!s_config_confirm) {
                s_config_confirm = true;
                snprintf(s_notice, sizeof(s_notice), "恢复将覆盖当前设置，请再点按一次确认");
                return APP_REDRAW_PAGE;
            }
            s_config_confirm = false;
            esp_err_t err = app_settings_backup_restore();
            if (err == ESP_OK) {
                ttf_font_scan();
                app_font_activate_system();
                ui_text_set_system_scale(true);
                // 阅读记录恢复后令首页和书架重新取进度、时长与排序。
                // Rebuild home and shelf caches after restoring reading records.
                book_store_notify_changed();
            }
            char saved_ssid[33] = {0};
            bool wifi_saved = false;
            if (err == ESP_OK)
                (void)read_pico_transfer_get_saved_wifi(saved_ssid, &wifi_saved);
            snprintf(s_notice, sizeof(s_notice), "%s",
                     err == ESP_OK ? (wifi_saved ? "配置与阅读记录已恢复；WiFi 请重新连接" :
                                                   "配置与阅读记录已恢复") :
                     err == ESP_ERR_INVALID_STATE ? "未识别到 TF 卡，请插卡后重试" :
                     err == ESP_ERR_NOT_FOUND ? "未找到配置备份文件" :
                     err == ESP_ERR_INVALID_RESPONSE ? "配置文件损坏或版本不兼容" :
                     "恢复未完成，请重试");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BLUETOOTH) {
        const ble_layout_t l = ble_layout();
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        // 版式给的是屏幕坐标（已减去滚动量），所以这里直接用 y 比，不再加 scroll。
        // The layout yields screen coordinates (scroll already subtracted), so compare y directly.
        const int ty = y;

        // 学习时轻点取消；识别结果轻点后返回开关。/ Tap to cancel learning or dismiss the received-key result.
        if (s_ble_learning) {
            s_ble_learning = 0;
            snprintf(s_ble_notice, sizeof(s_ble_notice), "已取消学习");
            return APP_REDRAW_PAGE;
        }
        if (s_ble_feedback[0] && ty >= 202 - s_ble_scroll && ty < 328 - s_ble_scroll) {
            s_ble_feedback[0] = 0;
            return APP_REDRAW_PAGE;
        }
        if (ty < 160 || ty >= settings_viewport_end()) return APP_REDRAW_NONE;

        if (l.toggle_top >= 0 && ty >= l.toggle_top && ty < l.toggle_top + 126) {
            app_settings_set_ble_turner(!app_settings_ble_turner());
            // 明确关掉时把失败计数和原因一起清掉：否则自锁之后用户没有重试的途径。
            // Switching it off explicitly clears the failure count and reason; without this a
            // self-locked stack leaves the user no way to try again.
            if (!app_settings_ble_turner()) ble_pt_reset_failure();
            else ble_pt_reconnect_recent();
            snprintf(s_ble_notice, sizeof(s_ble_notice), "%s",
                     app_settings_ble_turner() ? "已开启，尝试连接上次设备" : "已关闭，蓝牙内存已释放");
            return APP_REDRAW_PAGE;
        }
        if (ty >= l.scan_entry_top && ty < l.scan_entry_top + BLE_ROW_H) {
            if (!app_settings_ble_turner() || !ble_pt_running()) {
                snprintf(s_ble_notice, sizeof(s_ble_notice), "请先开启蓝牙翻页器"); return APP_REDRAW_PAGE;
            }
            // 进扫描页就开持续扫描，离页时停掉。/ Scan continuously while the page is open.
            ble_pt_scan_start(BLE_PT_SCAN_FOREVER);
            s_ble_scroll = 0;
            s_ble_notice[0] = 0;
            s_page = SETTINGS_BLE_SCAN;
            return APP_REDRAW_PAGE;
        }
        for (int i = 0; i < 2; ++i) {
            const EpdRect btn = ble_button_rect(l.learn_top[i]);
            // 命中判定必须同时比 x：按钮的纵向范围几乎覆盖整行，只比 y 会让整行都算按钮。
            // The hit test needs the horizontal test too: the button's vertical span covers almost
            // the whole row, so comparing y alone makes the entire row count as the button.
            if (ev->x0 >= btn.x && ev->x0 < btn.x + btn.width && ty >= btn.y && ty < btn.y + btn.height) {
                if (!ble_pt_connected()) {
                    snprintf(s_ble_notice, sizeof(s_ble_notice), "请先连接翻页器再学习按键");
                    return APP_REDRAW_PAGE;
                }
                ble_pt_raw_t stale;
                while (ble_pt_pop_raw(&stale)) {}
                // 把此刻那一帧当基准：点「学习」时用户还没按键，外设发的就是静止帧。有些遥控器
                // 的静止帧带着非零常量，不这样标一下，那串常量会被当成一次按下学进去。
                // Pin the frame present now as the baseline: the user has not pressed yet, so what the
                // remote sends is its resting frame. Some remotes keep non-zero constants there, and
                // without this the constants get learned as if they were a press.
                ble_pt_learn_begin();
                s_ble_feedback[0] = s_ble_notice[0] = 0;
                s_ble_learning = i + 1;
                s_ble_scroll = 0;
                return APP_REDRAW_PAGE;
            }
        }
        // 已配对行：右侧按钮忘记，行本身连接。/ Bonded rows: the button forgets, the row connects.
        for (int i = 0; i < l.bond_rows && i < BLE_PT_MAX_BONDS; ++i) {
            const ble_pt_bond_t *bond = ble_pt_bond((uint8_t)i);
            if (!bond) continue;
            const EpdRect btn = ble_button_rect(l.bond_top[i]);
            if (ev->x0 >= btn.x && ev->x0 < btn.x + btn.width && ty >= btn.y && ty < btn.y + btn.height) {
                if (ble_pt_connected()) ble_pt_disconnect();
                ble_pt_forget(bond->addr);
                snprintf(s_ble_notice, sizeof(s_ble_notice), "已删除该配对");
                return APP_REDRAW_PAGE;
            }
            if (ty >= l.bond_top[i] && ty < l.bond_top[i] + BLE_ROW_H) {
                if (ble_pt_connected() || ble_pt_connecting()) ble_pt_disconnect();
                snprintf(s_ble_notice, sizeof(s_ble_notice),
                         ble_pt_connect(bond->addr) == ESP_OK ? "正在连接…" : "连接失败，请重试");
                return APP_REDRAW_PAGE;
            }
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BLE_SCAN) {
        const ble_scan_layout_t l = ble_scan_layout();
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        if (y < 160 || y >= settings_viewport_end()) return APP_REDRAW_NONE;
        const int ty = y + s_ble_scroll;
        // 选一台就连，连上后退回二级页。扫描在离页时停。
        // Picking one connects, then returns to level two; scanning stops on the way out.
        for (int i = 0; i < l.device_rows; ++i) {
            const ble_pt_device_t *dev = ble_scan_visible_at(i);
            if (!dev) continue;
            const int top = l.devices_top + i * BLE_ROW_H;
            if (ty < top || ty >= top + BLE_ROW_H) continue;
            ble_pt_scan_stop();
            if (ble_pt_connected() || ble_pt_connecting()) ble_pt_disconnect();
            snprintf(s_ble_notice, sizeof(s_ble_notice),
                     ble_pt_connect(dev->addr) == ESP_OK ? "正在连接…" : "连接失败，请重试");
            s_page = SETTINGS_BLUETOOTH;
            s_ble_scroll = 0;
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (y<160 || y>=UI_NAV_TOP || ev->x0<36 || ev->x0>=648) return APP_REDRAW_NONE;
    y+=s_main_scroll;
    if (y>=180 && y<180+settings_profile_height()) { s_page=SETTINGS_PROFILE;return APP_REDRAW_PAGE; }
    int row;
    if (y>=SETTINGS_WIRELESS_Y && y<SETTINGS_WIRELESS_Y+SETTINGS_WIRELESS_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_WIRELESS_Y)/SETTINGS_ROW_H;
        if(row==0) {
            extern const app_desc_t app_transfer;app_transfer_request_wifi_setup();ctx->request_app=&app_transfer;return APP_REDRAW_NONE;
        }
        if(row==1) {
            s_page=SETTINGS_BLUETOOTH;s_ble_scroll=s_ble_learning=0;s_ble_notice[0]=s_ble_feedback[0]=0;
            ble_pt_raw_t unused;while(ble_pt_pop_raw(&unused)){}return APP_REDRAW_PAGE;
        }
        s_page=SETTINGS_TIME;return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_DISPLAY_Y && y<SETTINGS_DISPLAY_Y+SETTINGS_DISPLAY_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_DISPLAY_Y)/SETTINGS_ROW_H;
        if(row==0) { ttf_font_scan();s_font_page=0;s_notice[0]=0;s_page=SETTINGS_SYSTEM_FONT; }
        else if(row==1)s_page=SETTINGS_SYSTEM_CONTRAST;
        else if(row==2)s_page=SETTINGS_MAIN_REFRESH;
        else if(row==3)app_settings_set_home_full_refresh(!app_settings_home_full_refresh());
        else if(row==4)s_page=SETTINGS_AUTO_LOCK;
        else if(row==5)app_settings_set_wake_presses(app_settings_wake_presses()==2?1:2);
        else if(row==6)s_page=SETTINGS_POWER_SLEEP;
        else s_page=SETTINGS_LANGUAGE;
        return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_PERSONAL_Y && y<SETTINGS_PERSONAL_Y+SETTINGS_PERSONAL_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_PERSONAL_Y)/SETTINGS_ROW_H;
        if(row==0) {s_page=SETTINGS_SHELF_STYLE;s_style_scroll=0;}
        else if(row==1){if(!app_settings_set_shelf_progress_badges(!app_settings_shelf_progress_badges()))snprintf(s_notice,sizeof(s_notice),"保存失败，请重试");}
        else if(row==2)s_page=SETTINGS_LOCK_STYLE;
        else if(row==3){s_page=SETTINGS_LOCK_PIN;s_notice[0]=0;}
        else profile_editor_open(true);
        return APP_REDRAW_PAGE;
    }
    if(y>=SETTINGS_MAINTENANCE_Y && y<SETTINGS_MAINTENANCE_Y+SETTINGS_MAINTENANCE_ROWS*SETTINGS_ROW_H) {
        row=(y-SETTINGS_MAINTENANCE_Y)/SETTINGS_ROW_H;
        if(row==4) {s_page=SETTINGS_FACTORY_RESET;s_reset_confirm=s_reset_pending=false;s_reset_notice[0]=0;}
        else if(row==0)upgrade_open();
        else if(row==1){s_page=SETTINGS_CONFIG;s_config_confirm=false;s_notice[0]=0;}
        else if(row==2){
            // 屏幕在刷新中途断电会停在半帧，常规差分刷新永远盖不掉那道条纹；点一下就整屏盲刷
            // 一遍，然后自己把当前页面（含底栏）推回面板。
            // A power cut mid-refresh leaves the panel half-driven, and an ordinary differential
            // refresh can never cover that stripe. One tap runs the blind full-screen pass, which
            // then pushes the current page -- navigation bar included -- back onto the panel.
            s_panel_repair_pending=true;
            snprintf(s_notice,sizeof(s_notice),"屏幕修复中…");
            return APP_REDRAW_PAGE;
        }
        else {s_boot_pending=false;s_page=SETTINGS_BOOT;}
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    settings_drag_reset();
    s_reset_confirm=s_reset_pending=false;
    if(s_font_sync_confirm) {
        s_font_sync_confirm=false;s_notice[0]=0;
        if(key!=1)return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_PIN_ENTRY) { pin_setup_close(); s_page = SETTINGS_LOCK_PIN; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_UPGRADE) {
        s_upgrade_job = UPGRADE_JOB_NONE;
        if (s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        pico_online_cancel_join(); s_upgrade_confirm = false;
    }
    if (s_page == SETTINGS_BLE_SCAN) {
        ble_pt_scan_stop();
        if (key != 1) { s_page = SETTINGS_BLUETOOTH; s_ble_scroll = 0; return APP_REDRAW_PAGE; }
    }
    if (key == 1) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if(s_page==SETTINGS_SHELF_STYLE&&s_shelf_style_from_manage)return shelf_style_finish(ctx);
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        if (s_wallpaper_confirm) s_wallpaper_confirm = false;
        else { bookmark_preview_release(); s_page = SETTINGS_WALLPAPER; }
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_WALLPAPER) { s_page = SETTINGS_LOCK_STYLE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_AVATAR) { s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TEXT_EDIT) { ui_keyboard_end(); s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_PROFILE) { s_page = SETTINGS_MAIN; (void)read_pico_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TIME_EDIT) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
    if (s_page != SETTINGS_MAIN) { s_page = SETTINGS_MAIN; (void)read_pico_transfer_get_saved_wifi(s_main_wifi,&s_main_wifi_saved); settings_prepare_profile(); return APP_REDRAW_PAGE; }
    ui_nav_request(ctx, 0);
    return APP_REDRAW_NONE;
}
static void settings_exit(app_ctx_t *ctx) {
    s_shelf_style_from_manage=false;s_shelf_style_requested=false;
    settings_drag_reset();
    s_reset_confirm=s_reset_pending=false;
    bookmark_preview_release();s_bookmark_picker=false;
    s_font_sync_confirm=false;s_font_sync_path[0]=0;
    pin_setup_close();
    s_collage_prepare_pending = false;
    s_upgrade_job = UPGRADE_JOB_NONE;
    (void)ctx; ui_keyboard_end(); pico_online_cancel_join(); ble_pt_scan_stop(); s_ble_learning = 0;
}
static void on_before_lock(app_ctx_t *ctx) {
    settings_drag_reset();
    s_reset_confirm=s_reset_pending=false;
    s_font_sync_confirm=false;s_font_sync_path[0]=0;
    if (s_page == SETTINGS_PIN_ENTRY) { pin_setup_close(); s_page = SETTINGS_LOCK_PIN; }
    s_upgrade_job = UPGRADE_JOB_NONE;
    (void)ctx; pico_online_cancel_join(); ble_pt_scan_stop(); s_ble_learning = 0;
    (void)ble_pt_stop(2000);
}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }
static void settings_media_lost(app_ctx_t *ctx) {
    settings_drag_reset();
    bookmark_preview_release();
    (void)ctx; s_avatar_ready=s_avatar_pending=false; s_avatar_path[0]=0;
}
static void settings_media_ready(app_ctx_t *ctx) { settings_drag_reset(); (void)ctx; settings_prepare_profile(); }
static bool settings_edge_back(app_ctx_t *ctx) {
    (void)ctx;
    return s_page != SETTINGS_MAIN && !s_reset_pending && !s_local_pending &&
        !s_upgrade_restart && !s_pin_pending && !s_font_sync_confirm;
}
static bool settings_back_swipe_area(app_ctx_t *ctx,const ui_gesture_event_t *event) {
    (void)ctx;
    return s_page!=SETTINGS_TEXT_EDIT || event->y0<UI_SYSTEM_KEYBOARD_TOP;
}
static bool main_page_visible(app_ctx_t *ctx) { (void)ctx; return s_page == SETTINGS_MAIN; }
static bool settings_navigation_enabled(app_ctx_t *ctx) {
    (void)ctx;
    return !s_reset_pending && !s_local_pending && !s_upgrade_restart && !s_pin_pending && !s_boot_pending;
}
static bool settings_present(app_ctx_t *ctx, app_redraw_t redraw) {
    if(redraw==APP_REDRAW_FULL||redraw==APP_REDRAW_PAGE)settings_drag_reset();
    if(redraw==APP_REDRAW_AREA) {
        if(s_scroll_present_pending)settings_drag_quick(ctx);
        if(settings_drag_present(ctx))return true;
    }
    if (s_page == SETTINGS_PIN_ENTRY && (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)) {
        display_main_transition_cancel(); render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, (EpdRect){0,0,UI_LOCK_WIDTH,UI_NAV_REFRESH_END}));
        return true;
    }
    if (redraw == APP_REDRAW_AREA && s_page == SETTINGS_TEXT_EDIT) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_input_area));
            s_input_settle = false;
            return true;
        }
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
            s_input_layout ? &E0470_WAVEFORM : &E0470_FOLLOW_WAVEFORM,
            s_input_layout ? MODE_GL16 : MODE_DU, s_input_area));
        return true;
    }
    if (redraw == APP_REDRAW_AREA && s_scroll_present_pending &&
        (s_page == SETTINGS_MAIN || s_page == SETTINGS_BLUETOOTH || s_page == SETTINGS_BLE_SCAN ||
         s_page == SETTINGS_SHELF_STYLE || s_page == SETTINGS_SYSTEM_FONT ||
         s_page == SETTINGS_WALLPAPER || s_page == SETTINGS_AVATAR)) {
        s_scroll_present_pending = false;
        render(ctx, ctx->fb);
        int top = s_page == SETTINGS_MAIN ? 160 : s_page == SETTINGS_SHELF_STYLE ? 242 : 190;
        EpdRect area = {0, top, UI_LOCK_WIDTH, settings_content_end() - top};
        // 只驱动变化像素，滚动次数不触发黑白清屏；题头与底栏保持稳定。
        // Drive changed pixels only; scroll counts never trigger a wipe, and header/nav stay stable.
        // 列表滚动采用45相有力擦白：保留原驱动并增加八相变白，末尾三相保持；文字/头像灰阶不变。
        // List scrolling keeps native drives plus eight changed-white phases and three settle holds; glyph/avatar grays remain unchanged.
        display_main_transition_cancel();
        const EpdWaveform *wave=display_settings_scroll_waveform();
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,wave,MODE_GL16,area));
        return true;
    }
    if (s_page != SETTINGS_UPGRADE) return false;
    if (redraw == APP_REDRAW_PAGE) {
        display_main_transition_cancel();
        render(ctx, ctx->fb);
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM,
                          MODE_GL16, (EpdRect){0, 0, UI_LOCK_WIDTH, settings_content_end()}));
        return true;
    }
    if (redraw == APP_REDRAW_AREA) {
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl, &E0470_FOLLOW_WAVEFORM,
                          MODE_DU, upgrade_progress_area()));
        return true;
    }
    return false;
}

const app_desc_t app_device_settings = {
    .binary_ui = false,
    .title = "设置 Settings", .detail = "显示、连接与设备", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .main_page_visible = main_page_visible, .edge_back_enabled = settings_edge_back,
    .back_swipe_area=settings_back_swipe_area,
    .persistent_navigation=true,.navigation_enabled=settings_navigation_enabled,
    .on_enter = on_enter, .on_exit = settings_exit, .on_before_lock = on_before_lock, .render = render, .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key,
    .present = settings_present, .on_media_lost=settings_media_lost, .on_media_ready=settings_media_ready,
};

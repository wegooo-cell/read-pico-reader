/*
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
 */
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
#include "ble_page_turner.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "esp_log.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "ui_wallpaper.h"
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
#include "e0470_epaper_waveform.h"
#include "pmu_selftest.h"
#include "soc/rtc_cntl_reg.h"

static void fit_value(char *value, int width);
static char s_notice[96];
typedef enum { SETTINGS_MAIN, SETTINGS_WIFI, SETTINGS_TIME,
               SETTINGS_TIME_EDIT, SETTINGS_SHELF_STYLE, SETTINGS_SYSTEM_FONT,
               SETTINGS_SYSTEM_SIZE, SETTINGS_SYSTEM_CONTRAST, SETTINGS_LOCK_STYLE,
               SETTINGS_WALLPAPER, SETTINGS_WALLPAPER_PREVIEW,
               SETTINGS_CONFIG, SETTINGS_UPGRADE, SETTINGS_BOOT,
               SETTINGS_POWER_SLEEP, SETTINGS_AUTO_LOCK, SETTINGS_PROFILE, SETTINGS_AVATAR,
               SETTINGS_BLUETOOTH, SETTINGS_BLE_SCAN,
               SETTINGS_TEXT_EDIT } settings_page_t;
static settings_page_t s_page;
static int s_style_scroll, s_main_scroll;
static bool s_scroll_drag_consumed, s_scroll_present_pending;
// 蓝牙翻页器子页：滚动位置、正在学习哪个动作（0 未学，1 上一页，2 下一页）、提示行。
// Bluetooth sub-page: scroll offset, which action is being learned (0 idle, 1 prev, 2 next),
// and a notice line.
static int s_ble_scroll, s_ble_learning;
static char s_ble_notice[64];
static char s_ble_feedback[64];
static int s_font_page, s_wallpaper_page;
static bool s_sync_pending;
static bool s_config_confirm;
static bool s_boot_pending;
static bool s_upgrade_confirm, s_upgrade_online, s_local_pending, s_upgrade_restart;
static pico_ota_info_t s_local_update;
static EXT_RAM_BSS_ATTR pico_update_status_t s_update;
static char s_upgrade_notice[96];
static uint32_t s_update_drawn_percent, s_update_drawn_at;
static pico_update_state_t s_update_drawn_state;
static bool s_update_drawn_busy, s_upgrade_offer_pending;
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
// 结果等任务收尾后一次显示；进度只在五秒且变化五个百分点后局部更新。
// Show results once after worker cleanup; update only progress after five seconds and five percentage points.
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
static void draw_upgrade_progress(uint8_t *fb) {
    uint32_t percent = upgrade_percent();
    epd_fill_rect(upgrade_progress_area(), UI_GRAY_WHITE, fb);
    char progress[32]; snprintf(progress, sizeof(progress), "%lu%%", (unsigned long)percent);
    ui_text(fb, 624, 477, 26, progress, EPD_DRAW_ALIGN_RIGHT, false);
    ui_fill_round_rect(fb, (EpdRect){60, 519, 564, 8}, 4, 0xc0);
    if (percent) ui_fill_round_rect(fb, (EpdRect){60, 519, (int)(564 * percent / 100), 8}, 4, 0x30);
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
    upgrade_remember(esp_timer_get_time() / 1000);
    s_upgrade_notice[0] = 0;
    s_page = SETTINGS_UPGRADE;
}
static const char *const TAG = "device_settings";
#define SETTINGS_WIRELESS_Y 315
#define SETTINGS_DISPLAY_Y 495
#define SETTINGS_ROW_H 68
#define SETTINGS_DISPLAY_ROWS 6
#define SETTINGS_DEVICE_Y (SETTINGS_DISPLAY_Y + SETTINGS_DISPLAY_ROWS * SETTINGS_ROW_H + 41)
// 「阅读与设备」组的行数。滚动上限由它推导：主页面最后一行必须能完整落在
// 底部导航栏（UI_NAV_TOP）之上的可点区里，否则最后一行永远露不出来，也点不到。
// Row count of the 阅读与设备 group. The scroll limit is derived from it: the last main-page row
// must be able to sit fully inside the tappable band above the bottom nav (UI_NAV_TOP), or it
// can never be revealed or tapped.
#define SETTINGS_DEVICE_ROWS 4
#define SETTINGS_MAINTENANCE_Y (SETTINGS_DEVICE_Y + SETTINGS_DEVICE_ROWS * SETTINGS_ROW_H + 66)
// 设置行图标：32 像素盒，在行高 68 里垂直居中；墨色统一，避免一行一个灰度。
// Setting row icons: a 32 px box centred in the 68 px row with one shared ink level.
#define SETTINGS_ICON_PX 32
#define SETTINGS_ICON_INK 0x58
// 主页面滚动上限：把最后一组的最后一行刚好推到导航栏之上，多留 8px 余量。
// Main-page scroll limit: just enough to bring the last row of the last group above the nav bar,
// with 8 px to spare.
#define SETTINGS_SCROLL_MAX \
    (SETTINGS_MAINTENANCE_Y + 3 * SETTINGS_ROW_H - UI_NAV_TOP + 12)
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

static void wallpaper_scan_dir(const char *root) {
    if (!wallpapers_alloc()) return;
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while (s_wallpaper_count < WALLPAPER_MAX && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strncmp(entry->d_name, "._", 2)) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))) continue;
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

static void settings_card(uint8_t *fb, EpdRect rect, int radius, uint8_t fill, uint8_t edge) {
    ui_fill_round_rect(fb, rect, radius, fill);
    ui_draw_round_rect(fb, rect, radius, edge);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1,
                                     rect.width - 2, rect.height - 2}, radius - 1, edge);
}
static void settings_divider(uint8_t *fb, int y, int x, int width) {
    epd_fill_rect((EpdRect){x, y, width, 2}, 0x78, fb);
}
static void row(uint8_t *fb, int y, const char *label, const char *value) {
    ui_text(fb, 54, y + 17, 27, label, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 627, y + 20, 21, value, EPD_DRAW_ALIGN_RIGHT, false);
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
// 学到的键按原始边沿显示：字节值为主，字节下标和报告号非零时才补上，免得两个键看起来一样。
// Show a learned key as its raw edge: the byte value carries it, and the byte index and report id
// are appended only when they are non-zero so two keys never read alike.
static void format_bound_key(uint32_t code, char *out, size_t size) {
    const unsigned value = code & 0xFFu;
    const unsigned byte_index = (code >> 8) & 0xFFu;
    const unsigned report_id = (code >> 16) & 0xFFu;
    if (!byte_index && !report_id) snprintf(out, size, "0x%02X", value);
    else if (!report_id) snprintf(out, size, "0x%02X b%u", value, byte_index);
    else snprintf(out, size, "0x%02X r%u b%u", value, report_id, byte_index);
}

static void ble_row(uint8_t *fb, int y, const char *label, const char *value) {
    if (y + BLE_ROW_H < 190 || y >= UI_NAV_TOP) return;
    settings_card(fb, (EpdRect){36, y, 612, BLE_ROW_H - 8}, 18, UI_GRAY_WHITE, 0x70);
    char name[96]; snprintf(name, sizeof(name), "%s", label);
    fit_value(name, 290);
    ui_text_vc(fb, 60, y + 38, 26, name, EPD_DRAW_ALIGN_LEFT, false);
    if (value && value[0]) {
        char shown[96]; snprintf(shown, sizeof(shown), "%s", value); fit_value(shown, 140);
        ui_text_vc(fb, BLE_VALUE_RIGHT, y + 38, 21, shown, EPD_DRAW_ALIGN_RIGHT, false);
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
        UI_ICON_TIMER,             // 14 自动休眠
    };
    if (index < 0 || index >= (int)(sizeof(icons) / sizeof(icons[0]))) return;
    ui_draw_icon(fb, cx, cy, SETTINGS_ICON_PX, icons[index], SETTINGS_ICON_INK);
}

static void fit_value(char *value, int width) {
    while (*value && ttf_text_width_px(ui_text_effective_px(20), value) > width) {
        size_t n = strlen(value) - 1;
        while (n && ((unsigned char)value[n] & 0xc0) == 0x80) --n;
        value[n] = 0;
    }
}

static void setting_group(uint8_t *fb, int title_y, const char *title,
                          int card_y, const int icons[], const char *const labels[],
                          const char *const values[], int count) {
    const int row_height = SETTINGS_ROW_H;
    title_y -= s_main_scroll;
    card_y -= s_main_scroll;
    ui_text(fb, 42, title_y, 20, title, EPD_DRAW_ALIGN_LEFT, false);
    settings_card(fb, (EpdRect){36, card_y, 612, count * row_height}, 22,
                  UI_GRAY_WHITE, 0x70);
    for (int i = 0; i < count; ++i) {
        int y = card_y + i * row_height;
        if (i) settings_divider(fb, y, 88, 544);
        setting_icon(fb, icons[i], 67, y + row_height / 2);
        ui_text(fb, 100, y + 19, 25, labels[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 619, y + 22, 20, values[i], EPD_DRAW_ALIGN_RIGHT, false);
    }
}

static void setting_toggle(uint8_t *fb, int y, const char *title, const char *detail, bool active) {
    settings_card(fb, (EpdRect){36, y, 612, 126}, 22, UI_GRAY_WHITE, 0x70);
    ui_text(fb, 59, y + 24, 27, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 59, y + 75, 19, detail, EPD_DRAW_ALIGN_LEFT, false);
    EpdRect track = {555, y + 43, 69, 39};
    ui_fill_round_rect(fb, track, 19, active ? 0x58 : 0xc4);
    int cx = active ? track.x + 49 : track.x + 20;
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, 0x78, fb);
}

static void draw_style_thumbnail(uint8_t *fb, int style, int top) {
    if (top < 242 || top + 170 >= UI_NAV_TOP) return;
    const int left = 99;
    if (style == 5) {
        for (int i = 0; i < 2; ++i) {
            int y = top + i * 78;
            ui_fill_round_rect(fb, (EpdRect){left, y, 486, 68}, 8, UI_GRAY_WHITE);
            ui_draw_round_rect(fb, (EpdRect){left, y, 486, 68}, 8, 0x80);
            epd_fill_rect((EpdRect){left + 10, y + 8, 38, 52}, i ? 0x98 : 0x50, fb);
            ui_text_fixed_vc(fb, left + 65, y + 23, 26, i ? "看清每一本书" : "清晰的大字书名", EPD_DRAW_ALIGN_LEFT, false);
            ui_hairline(fb, y + 48, left + 65, 136, 0x90);
        }
        return;
    }
    if (style == 4) {
        for (int i = 0; i < 2; ++i) {
            EpdRect cover = {left + 20 + i * 128, top + 3, 92, 135};
            epd_fill_rect(cover, i ? 0x78 : 0x48, fb);
            ui_draw_round_rect(fb, cover, 0, 0x28);
            epd_fill_rect((EpdRect){cover.x + cover.width + 1, cover.y + 5, 6, 130}, 0xc8, fb);
        }
        for (int i = 0; i < 7; ++i) {
            EpdRect spine = {left + 286 + i * 28, top + 29, 24, 109};
            epd_fill_rect(spine, i % 2 ? 0x78 : 0x48, fb);
            ui_draw_round_rect(fb, spine, 0, 0x28);
        }
        epd_fill_rect((EpdRect){left, top + 140, 486, 12}, 0x40, fb);
        return;
    }
    for (int i = 0; i < 3; ++i) {
        int x = left + 20 + i * 155;
        EpdRect cover = {x, top, 116, 145};
        epd_fill_rect(cover, i == 1 ? 0x60 : i == 2 ? 0xa0 : 0x40, fb);
        ui_draw_round_rect(fb, cover, 1, 0x30);
        ui_draw_round_rect(fb, (EpdRect){x + 7, top + 7, 102, 130}, 1, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 47, x + 28, 60, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 54, x + 39, 38, i == 1 ? 0xc0 : 0xe0);
        ui_hairline(fb, top + 111, x + 44, 28, i == 1 ? 0xc0 : 0xe0);
    }
    if (style == 1) {
        epd_fill_rect((EpdRect){left, top + 143, 486, 16}, 0x30, fb);
        ui_hairline(fb, top + 143, left, 486, 0x80);
    } else if (style == 2) {
        ui_draw_acrylic_guard(fb, (EpdRect){left, top + 94, 486, 70});
    } else {
        for (int i = 0; i < 3; ++i)
            ui_draw_frosted_pocket(fb, (EpdRect){left + 8 + i * 155, top + 65, 143, 100});
    }
}

static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    ui_clear_page(fb);

    if (s_page == SETTINGS_MAIN || s_page == SETTINGS_SHELF_STYLE)
        epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, 0xe0, fb);
    ui_nav_status(fb);
    if (s_page == SETTINGS_PROFILE) {
        back_header(fb, "个人资料");
        ui_text(fb, 42, 214, 21, "自定义名称与头像", EPD_DRAW_ALIGN_LEFT, false);
        row(fb, 266, "设备名称", app_settings_device_name());
        row(fb, 366, "更换头像", app_settings_avatar_path()[0] ? "已选择图片  ›" : "默认图标  ›");
        ui_text(fb, 44, 612, 21, "Pico reader by Kiiko", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
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
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_TEXT_EDIT) {
        back_header(fb, s_editor_signature ? "状态栏签名" : "设备名称");
        ui_text(fb, 642, 95, 24, "完成", EPD_DRAW_ALIGN_RIGHT, false);
        ui_text(fb, 36, 201, 21, s_editor_signature ? "状态栏中间显示，留空则隐藏" : "显示在设置页的 Pico 资料卡", EPD_DRAW_ALIGN_LEFT, false);
        ui_text_input_draw(fb, &s_editor_input, (EpdRect){36, 243, 612, 82}, 28, false, NULL);
        if (s_editor_notice[0]) ui_text_fixed_vc(fb, 36, 510, 22, s_editor_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_keyboard_draw(fb, 560);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_SHELF_STYLE) {
        ui_nav_back(fb, 36, 79);
        ui_text_vc(fb, 342, 107, 34, "书架样式", EPD_DRAW_ALIGN_CENTER, false);
        ui_text(fb, 36, 207, 23, "新增清晰书单 · 大字双行书名", EPD_DRAW_ALIGN_LEFT, false);
        static const char *const styles[] = {"深色书轨", "亚克力书架", "半透明书袋", "封面与书脊 · 测试版", "清晰书单"};
        static const char *const descriptions[] = {"封面落在书轨上", "透明亚克力挡板", "每本独立透明书袋", "非正式版本", "大字 · 每页5本"};
        for (int i = 0; i < 5; ++i) {
            int y = 263 + i * 253 - s_style_scroll;
            if (y + 230 < 242 || y > 1095) continue;
            EpdRect card = {36, y, 612, 230};
            int style = i + 1;
            bool active = app_settings_shelf_style() == style;
            settings_card(fb, card, 24, active ? 0xd0 : UI_GRAY_WHITE,
                          active ? 0x58 : 0x70);
            draw_style_thumbnail(fb, style, y + 11);
            if (y + 190 < 1096) ui_text(fb, 68, y + 188, 23, styles[i], EPD_DRAW_ALIGN_LEFT, false);
            if (y + 195 < 1096) ui_text(fb, 440, y + 192, 17, descriptions[i], EPD_DRAW_ALIGN_LEFT, false);
            if (y + 220 < 1096) {
                epd_draw_circle(609, y + 207, 13, UI_GRAY_BLACK, fb);
                if (active) epd_fill_circle(609, y + 207, 6, UI_GRAY_BLACK, fb);
            }
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_TIME_EDIT) {
        ui_nav_back(fb, 36, 79);
        ui_text(fb, 36, 151, 47, "日期与时间", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 36, 216, 23, "北京时间 · UTC+8", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, 259, 36, 612, UI_GRAY_LIGHT);
        static const char *const labels[] = {"年", "月", "日", "时", "分"};
        int values[] = {s_year, s_month, s_day, s_hour, s_minute};
        for (int i = 0; i < 5; ++i) {
            int x = 36 + i * 122;
            ui_text(fb, x + 52, 300, 24, labels[i], EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_round_rect(fb, (EpdRect){x, 354, 106, 62}, 8, UI_GRAY_BLACK);
            ui_text_vc(fb, x + 53, 385, 30, "+", EPD_DRAW_ALIGN_CENTER, false);
            char value[8]; snprintf(value, sizeof(value), i == 0 ? "%04d" : "%02d", values[i]);
            ui_text(fb, x + 53, 456, i == 0 ? 34 : 39, value, EPD_DRAW_ALIGN_CENTER, false);
            ui_draw_round_rect(fb, (EpdRect){x, 535, 106, 62}, 8, UI_GRAY_BLACK);
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
        char ssid[33] = {0}; bool saved = false;
        (void)read_pico_transfer_get_saved_wifi(ssid, &saved);
        section(fb, 253, "已保存网络");
        row(fb, 296, saved ? ssid : "尚未配置", saved ? "已保存" : "");
        row(fb, 408, "连接或更改网络", "前往传输  ›");
        ui_text(fb, 54, 529, 21, "配网后可在日期与时间中进行 WiFi 对时。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_SYSTEM_FONT) {
        back_header(fb, "系统字体");
        section(fb, 244, "只影响首页、书架和设置等系统界面");
        const char *selected = app_settings_system_font_path();
        int count = ttf_font_count();
        int first = s_font_page * 8;
        for (int slot = 0; slot < 8; ++slot) {
            int item_index = first + slot;
            if (item_index > count) break;
            EpdRect box = {36, 292 + slot * 92, 612, 78};
            const ttf_font_item_t *item = item_index ? ttf_font_item(item_index - 1) : NULL;
            bool active = item ? !strcmp(selected, item->path) : !selected[0];
            settings_card(fb, box, 18, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_draw_round_rect(fb, box, 18, active ? 0x90 : 0xd0);
            char label[TTF_FONT_NAME_MAX];
            snprintf(label, sizeof(label), "%s", item ? system_font_label(item->path) : "思源黑体（内建）");
            while (label[0] && ttf_text_width_px(ui_text_effective_px(26), label) > box.width - 102) {
                size_t len = strlen(label) - 1;
                while (len && ((unsigned char)label[len] & 0xc0) == 0x80) --len;
                label[len] = 0;
            }
            ui_text_vc(fb, 60, box.y + 39, 26, label, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 39, 6, UI_GRAY_BLACK, fb);
        }
        char pages[32]; snprintf(pages, sizeof(pages), "%d / %d", s_font_page + 1, (count + 8) / 8);
        ui_text(fb, 648, 1050, 20, pages, EPD_DRAW_ALIGN_RIGHT, false);
        ui_nav_draw(fb, 3);
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
        ui_nav_draw(fb, 3);
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
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_LOCK_STYLE) {
        back_header(fb, "锁屏样式");
        section(fb, 248, "选择电源键锁屏后的画面");
        const char *labels[] = {"壁纸锁屏", "阅读票根"};
        const char *details[] = {"使用 TF 卡中的 JPG / PNG 图片", "书封、进度和阅读记录"};
        const uint8_t values[] = {1, 0};
        for (int i = 0; i < 2; ++i) {
            EpdRect box = {36, 300 + i * 154, 612, 132};
            bool active = app_settings_lock_style() == values[i];
            settings_card(fb, box, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text(fb, 64, box.y + 23, 31, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 64, box.y + 79, 20, details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 66, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb, 54, 659, 22, "选择壁纸后，可继续从 TF 卡更换图片", EPD_DRAW_ALIGN_LEFT, false);
        if (app_settings_wallpaper_path()[0])
            row(fb, 710, "当前壁纸", strrchr(app_settings_wallpaper_path(), '/') + 1);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_WALLPAPER) {
        back_header(fb, "选择壁纸");
        section(fb, 244, "TF 卡图片 · JPG / PNG · 不超过 50 MB");
        if (!s_wallpaper_count)
            ui_text(fb, 54, 325, 25, "未找到图片，请放入 TF 卡的 pictures 文件夹", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 0; i < 8; ++i) {
            int index = s_wallpaper_page * 8 + i;
            if (index >= s_wallpaper_count) break;
            EpdRect box = {36, 292 + i * 92, 612, 78};
            bool active = !strcmp(s_wallpapers[index].path, app_settings_wallpaper_path());
            settings_card(fb, box, 18, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            char name[96]; snprintf(name, sizeof(name), "%s", s_wallpapers[index].name);
            fit_value(name, 490);
            ui_text_vc(fb, 60, box.y + 39, 24, name, EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 39, 6, UI_GRAY_BLACK, fb);
        }
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        back_header(fb, "预览壁纸");
        EpdRect image = {166, 194, 352, 626};
        ui_fill_round_rect(fb, image, 8, UI_GRAY_WHITE);
        s_wallpaper_preview_ok = s_wallpaper_selected >= 0 && s_wallpaper_selected < s_wallpaper_count &&
            ui_wallpaper_draw(fb, s_wallpapers[s_wallpaper_selected].path, image);
        ui_draw_round_rect(fb, image, 8, 0x70);
        if (!s_wallpaper_preview_ok)
            ui_text_vc(fb, 342, 507, 23, "图片无法预览", EPD_DRAW_ALIGN_CENTER, false);
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
            ui_text_vc(fb, 342, 997, 25, "设为锁屏壁纸", EPD_DRAW_ALIGN_CENTER, true);
        }
        if (s_wallpaper_confirm) {
            EpdRect dialog = {68, 435, 548, 306};
            ui_fill_round_rect(fb, dialog, 26, UI_GRAY_WHITE);
            ui_draw_round_rect(fb, dialog, 26, 0x50);
            ui_text_vc(fb, 342, 503, 31, "设为壁纸锁屏？", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 342, 565, 20, "确认后，短按电源键将显示此图片", EPD_DRAW_ALIGN_CENTER, false);
            EpdRect cancel = {94, 643, 232, 68}, confirm = {358, 643, 232, 68};
            ui_fill_round_rect(fb, cancel, 18, 0xe0);
            ui_fill_round_rect(fb, confirm, 18, 0x30);
            ui_text_vc(fb, 210, 677, 24, "取消", EPD_DRAW_ALIGN_CENTER, false);
            ui_text_vc(fb, 474, 677, 24, "确认", EPD_DRAW_ALIGN_CENTER, true);
        }
        ui_nav_draw(fb, 3);
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
        ui_nav_draw(fb, 3);
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
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_POWER_SLEEP) {
        back_header(fb, "关机睡眠");
        section(fb, 248, "选择电源菜单中“关机”的方式");
        const char *labels[] = {"彻底关机", "先浅睡，10 分钟后深睡"};
        const char *details[] = {"完全断电，长按电源键开机",
                                 "锁屏画面保留；深睡后短按电源键开机"};
        for (int i = 0; i < 2; ++i) {
            EpdRect box = {36, 300 + i * 154, 612, 132};
            bool active = app_settings_staged_shutdown() == (i == 1);
            settings_card(fb, box, 22, active ? 0xd0 : UI_GRAY_WHITE, 0x70);
            ui_text(fb, 64, box.y + 23, 29, labels[i], EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 64, box.y + 79, 20, details[i], EPD_DRAW_ALIGN_LEFT, false);
            if (active) epd_fill_circle(609, box.y + 66, 8, UI_GRAY_BLACK, fb);
        }
        ui_text(fb, 54, 664, 21, "浅睡时按键直接返回，深睡时会重新开机。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 54, 708, 21, "锁屏样式同时适用于壁纸与阅读票根。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_BOOT) {
        back_header(fb, "电脑刷机");
        settings_card(fb, (EpdRect){36, 254, 612, 298}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 288, 29, "进入 BOOT 模式", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 350, 22, "用于官网完整刷机或故障恢复", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){60, 438, 564, 78}, s_boot_pending ? "正在进入" : "进入 BOOT 模式", false);
        ui_text(fb, 48, 620, 22, "请用 USB 连接电脑，再打开官网刷机页", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3); return;
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
            ui_text(fb, 60, 739, 23, "根目录：Pico-update.bin", EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, 60, 786, 21, s_local_update.ready ? s_local_update.candidate_version : "把官网升级包放入 TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
            ui_draw_button(fb, (EpdRect){60, 840, 564, 62}, s_local_pending ? "正在安装，请勿断电" :
                s_local_update.ready ? "安装 TF 卡升级包" : "重新检查 TF 卡", s_local_update.ready);
        }
        if (s_upgrade_notice[0]) ui_text(fb, 48, 957, 21, s_upgrade_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 1010, 20, "首次安装或修复，请通过官网完整刷机", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
        return;
    }
    if (s_page == SETTINGS_CONFIG) {
        back_header(fb, "保存与恢复配置");
        section(fb, 248, "换机或刷机后，快速恢复个性化设置");
        settings_card(fb, (EpdRect){36, 299, 612, 197}, 22, UI_GRAY_WHITE, 0x70);
        ui_text(fb, 60, 326, 26, "TF 卡根目录", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 377, 23, "Pico-settings.backup", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 60, 435, 19, "文字排版、设备资料、阅读记录与书签", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){36, 561, 612, 83}, "保存当前配置到 TF 卡", false);
        ui_draw_button(fb, (EpdRect){36, 681, 612, 83},
                       s_config_confirm ? "再次点按，确认恢复配置" : "从 TF 卡恢复配置", false);
        if (s_notice[0]) ui_text(fb, 48, 819, 21, s_notice, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 899, 19, "包含阅读记录、设备资料和已存 WiFi。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 939, 19, "书籍、字体和图片仍需留在 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, 979, 19, "备份含 WiFi 密码，请妥善保管 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
        ui_nav_draw(fb, 3);
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
            if (top + BLE_ROW_H >= 190 && top < UI_NAV_TOP) {
                settings_card(fb, (EpdRect){36, top, 612, BLE_ROW_H - 8}, 18, UI_GRAY_WHITE, 0x70);
                ui_text(fb, 60, top + 12, 25, label, EPD_DRAW_ALIGN_LEFT, false);
                ui_text(fb, 60, top + 45, 19, value, EPD_DRAW_ALIGN_LEFT, false);
                ui_text_vc(fb, 624, top + 38, 26, "›", EPD_DRAW_ALIGN_RIGHT, false);
            }
        }
        if (!l.device_rows)
            ui_text(fb, 44, l.devices_top - scroll + 18, 23, "附近还没有发现设备",
                    EPD_DRAW_ALIGN_LEFT, false);
        epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, 190}, UI_GRAY_WHITE, fb);
        ui_nav_status(fb);
        back_header(fb, "扫描设备");
        ui_nav_draw(fb, 3);
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
        ui_text_vc(fb, 624, l.scan_entry_top + 38, 26, "›", EPD_DRAW_ALIGN_RIGHT, false);

        section(fb, l.learn_title_top, "按键映射");
        static const char *const learn_names[] = {"上一页", "下一页"};
        const uint32_t bound[] = {ble_pt_binding(BLE_PT_ACTION_PREV), ble_pt_binding(BLE_PT_ACTION_NEXT)};
        for (int i = 0; i < 2; ++i) {
            // 学到的键直接显示出来；没学过就走内置映射。
            // Show the learned key itself; with none, the built-in map applies.
            char bound_label[40];
            if (bound[i]) format_bound_key(bound[i], bound_label, sizeof(bound_label));
            else snprintf(bound_label, sizeof(bound_label), "内置按键");
            ble_row(fb, l.learn_top[i], learn_names[i], bound_label);
            ui_draw_button(fb, ble_button_rect(l.learn_top[i]), "学习", false);
        }
        ui_text(fb, 48, l.footer_top, 20, "上下、左右及 PageUp / PageDown 均可翻页", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 48, l.footer_top + 34, 20, "仅支持 BLE 翻页器，按键只在阅读时生效", EPD_DRAW_ALIGN_LEFT, false);
        epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, 190}, UI_GRAY_WHITE, fb);
        ui_nav_status(fb);
        back_header(fb, "蓝牙翻页器");
        ui_nav_draw(fb, 3);
        return;
    }
    const int profile_y = 164 - s_main_scroll;
    if (profile_y + 106 > 160) {
        settings_card(fb, (EpdRect){36, profile_y, 612, 106}, 24, UI_GRAY_WHITE, 0x70);
        bool avatar_ok = app_settings_avatar_path()[0] &&
            ui_wallpaper_draw_rounded(fb, app_settings_avatar_path(),
                                      (EpdRect){57, profile_y + 11, 82, 82}, 20);
        if (!avatar_ok) {
            ui_fill_round_rect(fb, (EpdRect){57, profile_y + 11, 82, 82}, 20, 0x30);
            ui_text(fb, 98, profile_y + 25, 49, "P", EPD_DRAW_ALIGN_CENTER, true);
        }
        char profile_name[64]; snprintf(profile_name, sizeof(profile_name), "%s", app_settings_device_name());
        while (profile_name[0] && ttf_text_width_px(ui_text_effective_px(30), profile_name) > 345) {
            size_t n = strlen(profile_name) - 1;
            while (n && ((unsigned char)profile_name[n] & 0xc0) == 0x80) --n;
            profile_name[n] = 0;
        }
        ui_text(fb, 164, profile_y + 19, 30, profile_name, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 164, profile_y + 62, 19, "Pico reader by Kiiko", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 618, profile_y + 62, 19, "编辑  ›", EPD_DRAW_ALIGN_RIGHT, false);
        const pmu_snapshot_t *pmu = read_pico_pmu_get();
        {
            char battery[12] = "--%";
            int percent = pmu_battery_percent(pmu);
            if (percent >= 0) snprintf(battery, sizeof(battery), "%u%%", (unsigned)percent);
            // 电量和“编辑”从同一左边界起排，避免数字较短时看起来偏右。
            // Start the percentage at the edit label's left edge so short numbers do not appear offset.
            int edit_left = 618 - ui_text_fixed_width_px(ui_text_effective_px(19), "编辑  ›");
            ui_text(fb, edit_left, profile_y + 26, 22, battery, EPD_DRAW_ALIGN_LEFT, false);
        }
    }
    char wifi_ssid[33] = {0}; bool wifi_saved = false;
    (void)read_pico_transfer_get_saved_wifi(wifi_ssid, &wifi_saved);
    const char *wireless_labels[] = {"WiFi", "蓝牙翻页器"};
    char wireless_value[56]; snprintf(wireless_value, sizeof(wireless_value), "%s  ›", wifi_saved ? wifi_ssid : "未配置");
    fit_value(wireless_value, 235);
    const char *wireless_values[] = {wireless_value,
                                    app_settings_ble_turner() ? "已开启  ›" : "已关闭  ›"};
    static const int wireless_icons[] = {0, 1};
    setting_group(fb, 285, "无线连接", SETTINGS_WIRELESS_Y,
                  wireless_icons, wireless_labels, wireless_values, 2);
    const char *reading_labels[] = {"系统字体", "系统字号", "系统对比度", "书架样式", "状态栏签名", "首页强刷"};
    char font[96];
    const char *chosen_font = app_settings_system_font_path();
    snprintf(font, sizeof(font), "%s  ›", system_font_label(chosen_font));
    fit_value(font, 235);
    char size[32]; snprintf(size, sizeof(size), "%u%%  ›", app_settings_system_font_size());
    char contrast[32]; snprintf(contrast, sizeof(contrast), "%u%%  ›", app_settings_system_contrast());
    static const char *const styles[] = {"深色书轨  ›", "深色书轨  ›", "亚克力书架  ›", "半透明书袋  ›", "书脊测试版  ›", "清晰书单  ›"};
    char signature_value[96];
    snprintf(signature_value, sizeof(signature_value), "%s  ›",
             app_settings_status_signature()[0] ? app_settings_status_signature() : "未设置");
    fit_value(signature_value, 235);
    const char *reading_values[] = {font, size, contrast, styles[app_settings_shelf_style()],
                                    signature_value, app_settings_home_full_refresh() ? "开启  ›" : "关闭  ›"};
    static const int reading_icons[] = {2, 3, 7, 4, 10, 11};
    setting_group(fb, SETTINGS_DISPLAY_Y - 30, "显示", SETTINGS_DISPLAY_Y,
                  reading_icons, reading_labels, reading_values, SETTINGS_DISPLAY_ROWS);
    char idle_value[32];
    unsigned idle_minutes = app_settings_auto_lock_minutes();
    if (idle_minutes) snprintf(idle_value, sizeof(idle_value), "%u 分钟  ›", idle_minutes);
    else strcpy(idle_value, "关闭  ›");
    const char *display_labels[] = {"锁屏样式", "关机睡眠", "日期与时间", "自动休眠锁屏"};
    const char *display_values[] = {app_settings_lock_style() ? "壁纸  ›" : "阅读票根  ›",
                                    app_settings_staged_shutdown() ? "先浅后深  ›" : "彻底断电  ›",
                                    "设置  ›", idle_value};
    static const int display_icons[] = {5, 9, 6, 14};
    _Static_assert(sizeof(display_icons) / sizeof(display_icons[0]) == SETTINGS_DEVICE_ROWS,
                   "each device settings row requires an icon");
    setting_group(fb, SETTINGS_DEVICE_Y - 34, "阅读与设备", SETTINGS_DEVICE_Y,
                  display_icons, display_labels, display_values, SETTINGS_DEVICE_ROWS);
    static const char *const maintenance_labels[] = {"系统升级", "保存与恢复", "BOOT 刷机"};
    static const char *const maintenance_values[] = {"OTA / TF 卡  ›", "配置  ›", "电脑刷机  ›"};
    static const int maintenance_icons[] = {12, 8, 13};
    setting_group(fb, SETTINGS_MAINTENANCE_Y - 34, "升级和恢复", SETTINGS_MAINTENANCE_Y,
                  maintenance_icons, maintenance_labels, maintenance_values, 3);
    epd_fill_rect((EpdRect){0, 0, UI_LOCK_WIDTH, 160}, 0xe0, fb);
    ui_nav_status(fb);
    ui_text(fb, 36, 91, 52, "设置", EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 3);
}

static void on_enter(app_ctx_t *ctx) {
    (void)ctx;
    s_notice[0] = 0;
    s_ble_feedback[0] = 0;
    s_page = SETTINGS_MAIN;
    s_style_scroll = s_main_scroll = s_font_page = s_wallpaper_page = 0;
    s_scroll_drag_consumed = s_scroll_present_pending = false;
    s_wallpaper_selected = -1;
    s_wallpaper_confirm = s_wallpaper_preview_ok = false;
    s_sync_pending = false;
    s_config_confirm = false;
    s_boot_pending = s_local_pending = s_upgrade_restart = false;
    s_upgrade_offer_pending = s_upgrade_confirm = false;
}

// 主循环主动消费按键；识别不需要再触摸屏幕。/ Consume key edges on ticks without requiring another touch.
static bool ble_receive_feedback(void) {
    ble_pt_event_t unused;
    while (ble_pt_pop_key(&unused)) {}
    bool changed = false;
    ble_pt_raw_t raw;
    while (ble_pt_pop_raw(&raw)) {
        if (!raw.pressed || raw.was_rest || !ble_pt_connected()) continue;
        if (s_ble_learning) {
            const ble_pt_action_t action = s_ble_learning == 1 ? BLE_PT_ACTION_PREV : BLE_PT_ACTION_NEXT;
            const esp_err_t error = ble_pt_bind(action, ble_pt_raw_code(&raw));
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
static app_redraw_t on_tick(app_ctx_t *ctx) {
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
        if (redraw == APP_REDRAW_AREA) draw_upgrade_progress(ctx->fb);
        return redraw;
    }
    if (!s_sync_pending) return APP_REDRAW_NONE;
    s_sync_pending = false;
    uint32_t utc = 0;
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    esp_err_t err = read_pico_transfer_sync_time(&utc);
    bool network_time_received = err == ESP_OK;
    if (network_time_received) {
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
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 560, (EpdRect){36, 243, 612, 82}, 28, false, NULL, field);
    s_input_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t profile_editor_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type == UI_GESTURE_PRESS)
        return ui_keyboard_press(ev->x0, ev->y0, 560, ctx->now_ms) ? profile_editor_paint(ctx, false) : APP_REDRAW_NONE;
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
    ui_keyboard_result_t result = ui_keyboard_tap(x, y, 560, esp_timer_get_time() / 1000);
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

// 手指越过阈值就推进至少三行；一轮拖动只刷新一次，松手不再重复滚动。
// Advance at least three rows on crossing the drag threshold; refresh once per drag, without repeating on release.
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
    if (ev->y0 < (s_page == SETTINGS_MAIN ? 160 : 190) || ev->y0 >= UI_NAV_TOP)
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

static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_page == SETTINGS_BOOT) {
        if (ev->type != UI_GESTURE_TAP || s_boot_pending) return APP_REDRAW_NONE;
        if (ev->y0 < 190 && ev->x0 < 170) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
        if (ui_rect_hit((EpdRect){60, 438, 564, 78}, ev->x0, ev->y0)) { s_boot_pending = true; return APP_REDRAW_PAGE; }
        int tab = ui_nav_hit(ev->x0, ev->y0);
        if (tab >= 0) ui_nav_request(ctx, tab);
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_UPGRADE) {
        if (ev->type != UI_GESTURE_TAP || s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        int y = ev->y0;
        if (y < 190 && ev->x0 < 170) {
            s_upgrade_offer_pending = false;
            if (s_upgrade_confirm) s_upgrade_confirm = false;
            else { pico_online_cancel_join(); s_page = SETTINGS_MAIN; }
            return APP_REDRAW_PAGE;
        }
        int tab = ui_nav_hit(ev->x0, y);
        if (tab >= 0) { pico_online_cancel_join(); ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
        if (s_upgrade_confirm) {
            bool accept = ui_rect_hit(upgrade_confirm_button(s_upgrade_online, true), ev->x0, y);
            if (accept || ui_rect_hit(upgrade_confirm_button(s_upgrade_online, false), ev->x0, y)) {
                s_upgrade_confirm = false;
                s_upgrade_offer_pending = false;
                if (accept) {
                    if (s_upgrade_online) {
                        if (pico_online_download() != ESP_OK) snprintf(s_upgrade_notice, sizeof(s_upgrade_notice), "无法开始升级，请重新检查");
                        pico_online_get_status(&s_update);
                        upgrade_remember(esp_timer_get_time() / 1000);
                    } else s_local_pending = true;
                }
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (ev->x0 < 60 || ev->x0 >= 624) return APP_REDRAW_NONE;
        if (y >= 550 && y < 612) {
            s_upgrade_notice[0] = 0;
            if (s_update.busy) { s_upgrade_offer_pending = false; pico_online_cancel_join(); }
            else if (s_update.state == PICO_UPDATE_AVAILABLE) { s_upgrade_confirm = true; s_upgrade_online = true; }
            else {
                s_upgrade_offer_pending = pico_online_check() == ESP_OK;
                if (!s_upgrade_offer_pending) snprintf(s_upgrade_notice, sizeof(s_upgrade_notice), "无法检查更新，请退出传书后重试");
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
    if (s_page == SETTINGS_TEXT_EDIT) return profile_editor_gesture(ctx, ev);
    if (s_page == SETTINGS_MAIN || s_page == SETTINGS_BLUETOOTH || s_page == SETTINGS_BLE_SCAN) {
        int limit = SETTINGS_SCROLL_MAX;
        int *offset = &s_main_scroll;
        if (s_page != SETTINGS_MAIN) {
            int bottom = s_page == SETTINGS_BLUETOOTH ? ble_layout().content_bottom + s_ble_scroll : ble_scan_layout().content_bottom;
            limit = bottom - UI_NAV_TOP + 12;
            offset = &s_ble_scroll;
        }
        bool handled;
        app_redraw_t redraw = scroll_gesture(ev, offset, limit, &handled);
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
        s_wallpaper_page = next / 8;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_SHELF_STYLE && (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int next = s_style_scroll + (ev->type == UI_GESTURE_SWIPE_U ? 253 : -253);
        if (next < 0) next = 0;
        if (next > 506) next = 506;
        if (next == s_style_scroll) return APP_REDRAW_NONE;
        s_style_scroll = next;
        s_scroll_present_pending = true;
        return APP_REDRAW_AREA;
    }
    if ((s_page == SETTINGS_SYSTEM_FONT || s_page == SETTINGS_WALLPAPER) &&
        (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
        int *page = s_page == SETTINGS_SYSTEM_FONT ? &s_font_page : &s_wallpaper_page;
        int count = s_page == SETTINGS_SYSTEM_FONT ? ttf_font_count() + 1 : s_wallpaper_count;
        int pages = count > 0 ? (count + 7) / 8 : 1;
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
            app_settings_set_wallpaper_path(path);
            if (!strcmp(app_settings_wallpaper_path(), path)) {
                app_settings_set_lock_style(1);
                s_page = SETTINGS_LOCK_STYLE;
            }
        }
        s_wallpaper_confirm = false;
        return APP_REDRAW_PAGE;
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
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    int y = ev->y0;
    if (s_page == SETTINGS_PROFILE) {
        if (y < 190) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
        if (y >= 266 && y < 350) { profile_editor_open(false); return APP_REDRAW_PAGE; }
        if (y >= 366 && y < 450) { wallpaper_scan(); s_page = SETTINGS_AVATAR; return APP_REDRAW_PAGE; }
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
        if (y < 190) { s_page = SETTINGS_WALLPAPER; return APP_REDRAW_PAGE; }
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
        if (y < 190) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
        for (int i = 0; i < 5; ++i) {
            if (ev->x0 >= 36 && ev->x0 < 648 && y >= 242 && y < UI_NAV_TOP &&
                y >= 263 + i * 253 - s_style_scroll && y < 493 + i * 253 - s_style_scroll) {
                app_settings_set_shelf_style((uint8_t)(i + 1));
                s_page = SETTINGS_MAIN;
                return APP_REDRAW_PAGE;
            }
        }
        return APP_REDRAW_NONE;
    }
    if (s_page == SETTINGS_BLE_SCAN && y < 190) {
        ble_pt_scan_stop(); s_page = SETTINGS_BLUETOOTH; s_ble_scroll = 0; return APP_REDRAW_PAGE;
    }
    if (s_page != SETTINGS_MAIN && y < 190) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
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
        if (y < 292 || y >= 1028) return APP_REDRAW_NONE;
        int index = s_font_page * 8 + (y - 292) / 92;
        int count = ttf_font_count();
        if (index > count) return APP_REDRAW_NONE;
        const ttf_font_item_t *item = index ? ttf_font_item(index - 1) : NULL;
        app_settings_set_system_font_path(item ? item->path : "");
        app_font_activate_system();
        return APP_REDRAW_PAGE;
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
            wallpaper_scan();
            s_page = SETTINGS_WALLPAPER;
            return APP_REDRAW_PAGE;
        }
        if (y >= 454 && y < 586) {
            app_settings_set_lock_style(0);
            return APP_REDRAW_PAGE;
        }
        if (y >= 710 && app_settings_wallpaper_path()[0]) {
            wallpaper_scan();
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
        if (y >= 300 && y < 432) {
            app_settings_set_staged_shutdown(false);
            return APP_REDRAW_PAGE;
        }
        if (y >= 454 && y < 586) {
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
                     err == ESP_ERR_NOT_FOUND ? "未找到 Pico-settings.backup" :
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
        if (ty < 160 || ty >= UI_NAV_TOP) return APP_REDRAW_NONE;

        if (l.toggle_top >= 0 && ty >= l.toggle_top && ty < l.toggle_top + 126) {
            app_settings_set_ble_turner(!app_settings_ble_turner());
            // 明确关掉时把失败计数和原因一起清掉：否则自锁之后用户没有重试的途径。
            // Switching it off explicitly clears the failure count and reason; without this a
            // self-locked stack leaves the user no way to try again.
            if (!app_settings_ble_turner()) ble_pt_reset_failure();
            snprintf(s_ble_notice, sizeof(s_ble_notice), "%s",
                     app_settings_ble_turner() ? "已开启，正在等待设备" : "已关闭，蓝牙内存已释放");
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
        if (y < 160 || y >= UI_NAV_TOP) return APP_REDRAW_NONE;
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
    if (y < 160 || y >= UI_NAV_TOP) return APP_REDRAW_NONE;
    y += s_main_scroll;
    if (y >= 164 && y < 270) {
        s_page = SETTINGS_PROFILE;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_WIRELESS_Y && y < SETTINGS_WIRELESS_Y + SETTINGS_ROW_H) {
        extern const app_desc_t app_transfer;
        app_transfer_request_wifi_setup();
        ctx->request_app = &app_transfer;
        return APP_REDRAW_NONE;
    }
    if (y >= SETTINGS_WIRELESS_Y + SETTINGS_ROW_H && y < SETTINGS_WIRELESS_Y + 2 * SETTINGS_ROW_H) {
        s_page = SETTINGS_BLUETOOTH;
        s_ble_scroll = 0;
        s_ble_learning = 0;
        s_ble_notice[0] = s_ble_feedback[0] = 0;
        ble_pt_raw_t unused;
        while (ble_pt_pop_raw(&unused)) {}
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DISPLAY_Y && y < SETTINGS_DISPLAY_Y + SETTINGS_ROW_H) {
        ttf_font_scan();
        s_font_page = 0;
        s_page = SETTINGS_SYSTEM_FONT;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DISPLAY_Y + SETTINGS_ROW_H && y < SETTINGS_DISPLAY_Y + 2 * SETTINGS_ROW_H) {
        s_page = SETTINGS_SYSTEM_SIZE;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DISPLAY_Y + 2 * SETTINGS_ROW_H && y < SETTINGS_DISPLAY_Y + 3 * SETTINGS_ROW_H) {
        s_page = SETTINGS_SYSTEM_CONTRAST;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DISPLAY_Y + 3 * SETTINGS_ROW_H && y < SETTINGS_DISPLAY_Y + 4 * SETTINGS_ROW_H) {
        s_page = SETTINGS_SHELF_STYLE;
        s_style_scroll = 0;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DISPLAY_Y + 4 * SETTINGS_ROW_H && y < SETTINGS_DISPLAY_Y + 5 * SETTINGS_ROW_H) {
        profile_editor_open(true);
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DISPLAY_Y + 5 * SETTINGS_ROW_H && y < SETTINGS_DISPLAY_Y + 6 * SETTINGS_ROW_H) {
        app_settings_set_home_full_refresh(!app_settings_home_full_refresh());
        return APP_REDRAW_PAGE;
    }

    if (y >= SETTINGS_DEVICE_Y && y < SETTINGS_DEVICE_Y + SETTINGS_ROW_H) {
        s_page = SETTINGS_LOCK_STYLE;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DEVICE_Y + SETTINGS_ROW_H && y < SETTINGS_DEVICE_Y + 2 * SETTINGS_ROW_H) {
        s_page = SETTINGS_POWER_SLEEP;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DEVICE_Y + 2 * SETTINGS_ROW_H && y < SETTINGS_DEVICE_Y + 3 * SETTINGS_ROW_H) {
        s_page = SETTINGS_TIME;
        return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_DEVICE_Y + 3 * SETTINGS_ROW_H && y < SETTINGS_DEVICE_Y + 4 * SETTINGS_ROW_H) {
        s_page = SETTINGS_AUTO_LOCK; return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_MAINTENANCE_Y && y < SETTINGS_MAINTENANCE_Y + SETTINGS_ROW_H) {
        upgrade_open(); return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_MAINTENANCE_Y + SETTINGS_ROW_H && y < SETTINGS_MAINTENANCE_Y + 2 * SETTINGS_ROW_H) {
        s_page = SETTINGS_CONFIG; s_config_confirm = false; s_notice[0] = 0; return APP_REDRAW_PAGE;
    }
    if (y >= SETTINGS_MAINTENANCE_Y + 2 * SETTINGS_ROW_H && y < SETTINGS_MAINTENANCE_Y + 3 * SETTINGS_ROW_H) {
        s_boot_pending = false; s_page = SETTINGS_BOOT; return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if (s_page == SETTINGS_UPGRADE) {
        if (s_local_pending || s_upgrade_restart) return APP_REDRAW_NONE;
        pico_online_cancel_join(); s_upgrade_confirm = false;
    }
    if (s_page == SETTINGS_BLE_SCAN) {
        ble_pt_scan_stop();
        if (key != 1) { s_page = SETTINGS_BLUETOOTH; s_ble_scroll = 0; return APP_REDRAW_PAGE; }
    }
    if (key == 1) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_page == SETTINGS_WALLPAPER_PREVIEW) {
        if (s_wallpaper_confirm) s_wallpaper_confirm = false;
        else s_page = SETTINGS_WALLPAPER;
        return APP_REDRAW_PAGE;
    }
    if (s_page == SETTINGS_WALLPAPER) { s_page = SETTINGS_LOCK_STYLE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_AVATAR) { s_page = SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TEXT_EDIT) { ui_keyboard_end(); s_page = s_editor_signature ? SETTINGS_MAIN : SETTINGS_PROFILE; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_PROFILE) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
    if (s_page == SETTINGS_TIME_EDIT) { s_page = SETTINGS_TIME; return APP_REDRAW_PAGE; }
    if (s_page != SETTINGS_MAIN) { s_page = SETTINGS_MAIN; return APP_REDRAW_PAGE; }
    ui_nav_request(ctx, 0);
    return APP_REDRAW_NONE;
}
static void settings_exit(app_ctx_t *ctx) {
    (void)ctx; ui_keyboard_end(); pico_online_cancel_join(); ble_pt_scan_stop(); s_ble_learning = 0;
}
static void on_before_lock(app_ctx_t *ctx) {
    (void)ctx; pico_online_cancel_join(); ble_pt_scan_stop(); s_ble_learning = 0;
    (void)ble_pt_stop(2000);
}
static bool no_menu_handle(app_ctx_t *ctx) { (void)ctx; return false; }
static bool settings_present(app_ctx_t *ctx, app_redraw_t redraw) {
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
        EpdRect area = {0, top, UI_LOCK_WIDTH, UI_NAV_TOP - top};
        // 只驱动变化像素，滚动次数不触发黑白清屏；题头与底栏保持稳定。
        // Drive changed pixels only; scroll counts never trigger a wipe, and header/nav stay stable.
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, area));
        return true;
    }
    if (redraw != APP_REDRAW_AREA || s_page != SETTINGS_UPGRADE) return false;
    guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &E0470_WAVEFORM,
                      MODE_GL16, upgrade_progress_area()));
    return true;
}

const app_desc_t app_device_settings = {
    .title = "设置 Settings", .detail = "显示、连接与设备", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = no_menu_handle,
    .on_enter = on_enter, .on_exit = settings_exit, .on_before_lock = on_before_lock, .render = render, .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key,
    .present = settings_present,
};

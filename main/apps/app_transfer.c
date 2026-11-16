/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 传书页负责两种接入模式生命周期、书源注入与状态展示；HTTP和文件接收属于独立组件。
 * Transfer page owns both network modes, storage injection and status; the component receives files.
 *
 * 冻结：按用户双模式要求提供热点与已有WiFi；离开传书时停服，配网后已连接的 STA 保持在线；深睡/断电会中断传输。
 * render只画快照；内置存储单文件上限由book_store提供，不格式化TF卡。
 * 上传并发扫描曾欠载，页内增加预填余量；离页停服后恢复，不改波形或像素时钟。
 * 用户要求设备端配网：停服后扫描选网，ASCII密码只在连接保存时写入，离开输入页即清空。
 * 用户要求输入光标：密码框常亮光标可轻点定位，显示/隐藏保留位置，离页一并清空。
 * 用户授权新输入法：WiFi 密码共用英文、大小写、数字及符号键盘，不启用中文候选。
 * 用户验收要求停止后恢复进入传书前的页面或菜单位置；遗忘网络必须确认。
 * 用户要求联网后提供网址二维码；热点用单码切换连接/网页，网络变更清除旧码。
 * 卡失效时停止并汇合接收任务，清除旧容量与二维码；当前请求不得切换存储源。
 * 用户修订：首次引导内复用配网页时由引导独占底部区域，不绘制主导航。
 * 配网列表与确认页底栏可切换主页面；密码页底部保留输入操作键，已连接的 STA 在离页后保活。
 * 正在上传时禁止主动切页；USB 退出必须先断开电脑再启动本机重新挂载。
 * Frozen: AP/STA transfer stops on exit while a provisioned connected STA stays online; deep sleep or power loss interrupts transfer.
 * Render only paints snapshots; book_store defines the flash file limit. Never format the TF card.
 * Concurrent uploads underrun scan queues; increase prefill until service exit without changing waveforms or pixel clocks.
 * User-requested device provisioning scans while stopped; save ASCII passwords only on connect and clear input on leaving the editor.
 * User-requested caret: tap to position a steady password caret; show/hide preserves position, and exit clears it too.
 * Authorized keyboard revision: WiFi passwords share English, case, number and symbol keys without Chinese candidates.
 * 用户修订：输入密码只刷新输入框与字数，布局切换才重画键盘；不在打字时全刷。
 * User revision: password typing refreshes its field/count; repaint keys only on layout switches, with no typing-triggered wipes.
 * Acceptance requires returning to the page or menu position used to enter transfer; forgetting WiFi requires confirmation.
 * User-requested URL QR follows network readiness; AP switches one code between joining and browsing, discarding stale codes on changes.
 * Lost media stops and joins reception and clears capacity/QR; never switch storage beneath an active request.
 * Provisioning lists and confirmation keep the main tabs active; the password editor keeps its input actions, and connected STA persists after exit.
 * Navigation waits for an active upload; USB exit disconnects the host before local remount.
 */
#ifdef ESP_PLATFORM
#include "ui_locale_format.h"
#endif
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "app_content_open.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_title.h"
#include "display.h"
#if KIIKO_BOARD_METALIO47
#include "display_main_waveform.h"
#endif
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "read_pico_transfer.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_text_input.h"
#include "ui_keyboard.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_wifi_qr.h"
#include "usb_storage.h"

// 引导复用网络后端时不登记或绘制主页面底栏。/ Setup reuses networking without root-tab ink or hit registrations.
static bool s_setup_chrome;
static bool s_cover_wifi_only;
void app_transfer_set_setup_chrome(bool active){s_setup_chrome=active;}
static void transfer_navigation(uint8_t *fb,int active) {
    if(!s_setup_chrome && !s_cover_wifi_only)ui_nav_draw(fb,active);
}

static const char* TAG = "transfer_page";
static book_store_root_t s_root;
static read_pico_transfer_status_t s_status;
static bool s_start_pending, s_settle, s_any_changed;
#if KIIKO_BOARD_METALIO47
static bool s_progress_pending;
#endif
static bool s_media_lost;
static int s_pressed = -1;
static read_pico_transfer_mode_t s_mode = READ_PICO_TRANSFER_MODE_AP;
static bool s_session_started;
static int64_t s_poll_ms;
static uint64_t s_free;
static size_t s_heap_before, s_internal_before;
static EpdRect s_area;
static bool s_qr_url, s_qr_ready;
static char s_usb_message[96];
static bool s_usb_requested, s_wifi_setup_requested, s_wifi_settings_only;
static bool s_wifi_upload_requested, s_hotspot_requested, s_direct_entry, s_usb_entry;
static bool s_usb_start_pending;
static int64_t s_usb_retry_ms;
static bool s_usb_host_connected;
static uint64_t s_usb_capacity_bytes;

static uint32_t s_signature_revision, s_signature_seen;
static bool transfer_signature_get(char *out, size_t cap) {
    const char *text = app_settings_status_signature();
    if (!out || !cap || strlen(text) >= cap) return false;
    snprintf(out, cap, "%s", text); return true;
}
static esp_err_t transfer_signature_set(const char *text) {
    app_settings_set_status_signature(text);
    if (strcmp(app_settings_status_signature(), text)) return ESP_FAIL;
    __atomic_add_fetch(&s_signature_revision, 1, __ATOMIC_RELEASE);
    return ESP_OK;
}

static bool transfer_book_file(const char *path) {
    const char *ext = strrchr(path, '.');
    return ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt"));
}

static void transfer_file_deleted(const char *path) {
    if (transfer_book_file(path)) {
        (void)book_progress_forget(path);
        (void)book_title_clear(path);
    }
    if (!strcmp(app_settings_system_font_path(), path)) app_settings_set_system_font_path("");
    if (!strcmp(app_settings_font_path(), path)) app_settings_set_font_path("");
    if (!strcmp(app_settings_wallpaper_path(), path)) app_settings_set_wallpaper_path("");
}

static void transfer_file_moved(const char *old_path, const char *new_path, uint32_t size) {
    if (transfer_book_file(old_path)) {
        book_progress_t progress = {0};
        bool saved = book_progress_load(old_path, size, &progress);
        char last[BOOK_STORE_PATH_MAX] = {0};
        bool was_last = book_progress_last_path(last, sizeof(last)) && !strcmp(last, old_path);
        if (saved) (void)book_progress_save(new_path, &progress);
        (void)book_progress_forget(old_path);
        if (was_last) (void)book_progress_set_last_path(new_path);
    }
    if (transfer_book_file(old_path)) (void)book_title_clear(old_path);
    if (!strcmp(app_settings_system_font_path(), old_path)) app_settings_set_system_font_path(new_path);
    if (!strcmp(app_settings_font_path(), old_path)) app_settings_set_font_path(new_path);
    if (!strcmp(app_settings_wallpaper_path(), old_path)) app_settings_set_wallpaper_path(new_path);
}

static bool transfer_set_wallpaper(const char *path) {
    if (strncmp(path, "/sdcard/pictures/", 17)) return false;
    app_settings_set_wallpaper_path(path);
    if (strcmp(app_settings_wallpaper_path(), path)) return false;
    app_settings_set_lock_style(1);
    return app_settings_lock_style() == 1;
}

void app_transfer_request_method_picker(void) {
    s_usb_requested = s_hotspot_requested = s_wifi_upload_requested = s_wifi_setup_requested = false;
    s_wifi_settings_only = false;
}

void app_transfer_request_usb_start(void) {
    s_usb_requested = true;
}

void app_transfer_request_wifi_setup(void) {
    s_wifi_setup_requested = true;
    s_wifi_settings_only = true;
    s_cover_wifi_only = false;
}

void app_transfer_request_cover_wifi(void) {
    app_transfer_request_wifi_setup();
    s_cover_wifi_only = true;
}

static void return_from_wifi_setup(app_ctx_t *ctx, bool connected) {
    if (s_cover_wifi_only) app_book_return_from_cover_wifi(connected);
    ctx->request_return = true;
}

void app_transfer_request_wifi_upload(void) {
    s_wifi_upload_requested = true;
    s_wifi_settings_only = false;
}

void app_transfer_request_hotspot_start(void) {
    s_hotspot_requested = true;
    s_wifi_settings_only = false;
}

static void clear_qr(void) {
    ui_wifi_qr_clear();
    s_qr_ready = false;
}

// 仅在网络快照变化或用户切换时编码，render与上传进度不触发编码。
// Encode only on network snapshot changes or user switching, never from render or upload progress.
static void prepare_qr(void) {
    clear_qr();
    if (!s_status.network_ready) return;
    s_qr_ready = s_qr_url || s_mode == READ_PICO_TRANSFER_MODE_STA
        ? ui_wifi_qr_prepare_url(s_status.url)
        : ui_wifi_qr_prepare(s_status.ssid, READ_PICO_TRANSFER_PASSWORD);
}

typedef enum { TRANSFER_METHODS, TRANSFER_HOME, TRANSFER_NETWORKS, TRANSFER_PASSWORD } transfer_view_t;
#define NETWORK_ROWS 4
#define PASSWORD_MAX 64
static transfer_view_t s_view;
static read_pico_transfer_network_t s_networks[READ_PICO_TRANSFER_SCAN_MAX], s_selected;
static size_t s_network_count, s_network_page;
static bool s_scan_pending, s_network_connect_pending, s_saved_configured, s_password_visible, s_forget_confirm;
static char s_saved_ssid[33], s_password[PASSWORD_MAX + 1], s_network_message[96], s_method_message[96];
static ui_text_edit_t s_password_input;

static esp_err_t s_scan_error;

static void render(app_ctx_t* ctx, uint8_t* fb);
static void stop_session(void);
static bool stop_upload_if_idle(void);

static void transfer_header(uint8_t *fb, const char *title, const char *subtitle) {
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, title, EPD_DRAW_ALIGN_CENTER, false);
    if (subtitle && subtitle[0]) ui_text(fb, 36, 153, 20, subtitle, EPD_DRAW_ALIGN_LEFT, false);
    if (subtitle && subtitle[0]) ui_hairline(fb, 190, 36, 612, UI_GRAY_LIGHT);
}

static int transfer_content_end(void) {
    return UI_NAV_REFRESH_END;
}
static int transfer_footer_top(void) {
    return UI_NAV_REFRESH_END-102;
}
static EpdRect method_control_rect(int id) {
    return (EpdRect){36, 224 + id * 112, 612, 112};
}

// S05：入口只负责选择传输方式，具体服务信息留给下一页。
// S05 keeps method selection separate from the service details shown on the next page.
static void draw_methods(uint8_t *fb) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, transfer_content_end()}, 0xe0, fb);
    transfer_header(fb, "Wifi/热点传书", NULL);
    ui_text(fb, 42, 180, 21, "选择方式", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect card = {36, 224, 612, 224};
    ui_fill_round_rect(fb, card, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, card, 24, 0x60);
    ui_draw_round_rect(fb, (EpdRect){37, 225, 610, 222}, 23, 0x60);
    static const char *titles[] = {"WiFi传书", "热点传书"};
    static const char *details[] = {
        "与手机或电脑连接同一个无线网络",
        "由墨水屏创建临时网络，离线也能传书"
    };
    for (int i = 0; i < 2; ++i) {
        EpdRect row = method_control_rect(i);
        if (s_pressed == i) ui_draw_pressed_round_rect(fb, row, 0);
        if (i) ui_fill_rect_fast((EpdRect){68, row.y, 548, 2}, 0x80, fb);
        int cy = row.y + row.height / 2;
        if (i == 0) ui_nav_wifi_icon(fb, 72, cy, 30, 0x6a);
        else if (i == 1) ui_draw_icon(fb, 72, cy, 48, UI_ICON_RADIO_TOWER, 0x6a);
        ui_text(fb, 112, row.y + 21, 27, titles[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 112, row.y + 64, 19, details[i], EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, 617, cy, 29, "›", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_method_message[0]) {
        ui_fill_round_rect(fb, (EpdRect){36, 478, 612, 76}, 20, 0xd8);
        ui_text_vc(fb, 342, 516, 20, s_method_message, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_text(fb, 42, 594, 19, "浏览器传书支持 EPUB、TXT、字体和图片文件。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 42, 634, 18, "传输完成后，新内容会自动出现在书架或字体列表。", EPD_DRAW_ALIGN_LEFT, false);
    transfer_navigation(fb,2);
}

/* ---- 设备端配网 / Device provisioning ---- */
static void fit_label(char* text, int px, int width) {
    while (*text && ttf_text_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static void clear_password(void) {
    volatile char* p = s_password;
    for (size_t i = 0; i < sizeof(s_password); ++i) p[i] = 0;
    ui_text_edit_init(&s_password_input, s_password, sizeof(s_password));
    s_password_visible = false;
    ui_keyboard_end();
}

static EpdRect network_control_rect(int id) {
    if (id >= 0 && id < NETWORK_ROWS) return (EpdRect){36, 465 + id * 95, 612, 95};
    if (id == 6) return (EpdRect){501, 244, 123, 56};
    if (id == 11) return s_forget_confirm ? ui_row_rect(0, 2, 600, UI_BTN_H) : (EpdRect){501, 315, 123, 52};
    if (id == 12) return ui_row_rect(1, 2, 600, UI_BTN_H);
    bool paged = s_network_count > NETWORK_ROWS;
    if (id == 7) return paged ? (EpdRect){36, transfer_footer_top()-103, 64, 66} : (EpdRect){0};
    if (id == 8) return paged ? (EpdRect){112, transfer_footer_top()-103, 460, 66} : (EpdRect){36, transfer_footer_top()-103, 612, 66};
    if (id == 9) return paged ? (EpdRect){584, transfer_footer_top()-103, 64, 66} : (EpdRect){0};
    return (EpdRect){0};
}

static EpdRect password_control_rect(int id) {
    if (id == 44) return (EpdRect){36, 434, 278, 68};
    if (id == 45) return (EpdRect){500, 215, 148, 58};
    if (id == 46) return (EpdRect){330, 434, 318, 68};
    if (id == 47) return (EpdRect){UI_MARGIN, 288, ui_content_width(), 86};
    return (EpdRect){0};
}

static void provisioning_button(uint8_t* fb, EpdRect rect, const char* text, int id) {
    if (s_pressed == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, text, false);
}

static void network_border(uint8_t *fb, EpdRect rect, int radius) {
    ui_draw_round_rect(fb, rect, radius, 0x50);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2}, radius - 1, 0x50);
}

static void network_button(uint8_t *fb, int id, const char *label, bool dark) {
    EpdRect rect = network_control_rect(id);
    ui_fill_round_rect(fb, rect, 16, s_pressed == id ? 0xb0 : dark ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    if (!dark) network_border(fb, rect, 16);
    ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2, 24, label,
               EPD_DRAW_ALIGN_CENTER, dark && s_pressed != id);
}

static void draw_networks(uint8_t* fb) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, transfer_content_end()}, 0xe0, fb);
    if (s_forget_confirm) {
        transfer_header(fb, "遗忘网络", "仅清除 WiFi 连接信息，不影响图书");
        char name[33];
        snprintf(name, sizeof(name), "%s", s_saved_ssid);
        fit_label(name, UI_PX_BODY, ui_content_width());
        ui_text(fb, UI_MARGIN, 350, UI_PX_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, UI_MARGIN, 450, UI_PX_CAPTION, s_network_message, EPD_DRAW_ALIGN_LEFT, false);
        provisioning_button(fb, network_control_rect(11), "确认遗忘", 11);
        provisioning_button(fb, network_control_rect(12), "取消", 12);
        transfer_navigation(fb,3);
        return;
    }
    transfer_header(fb, s_cover_wifi_only ? "连接 WiFi" : "无线网络", NULL);
    ui_text(fb, 42, 182, 24, "当前网络", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect current = {36, 224, 612, 154};
    ui_fill_round_rect(fb, current, 22, UI_GRAY_WHITE);
    network_border(fb, current, 22);
    if (s_saved_configured) {
        char saved[40]; snprintf(saved, sizeof(saved), "%s", s_saved_ssid); fit_label(saved, 29, 375);
        ui_nav_wifi_icon(fb, 74, 265, 28, UI_GRAY_BLACK);
        ui_text(fb, 104, 243, 29, saved, EPD_DRAW_ALIGN_LEFT, false);
        const char *state = s_status.network_ready ? "已连接" :
            s_network_connect_pending || s_status.state == READ_PICO_TRANSFER_STARTING ? "正在连接…" : "已保存 · 尚未连接";
        ui_text(fb, 64, 297, 23, state, EPD_DRAW_ALIGN_LEFT, false);
        char address[80];
        const char *url = !strncmp(s_status.url, "http://", 7) ? s_status.url + 7 : s_status.url;
        snprintf(address, sizeof(address), "%.79s", s_cover_wifi_only ? (s_status.network_ready ? "可继续获取封面" : "连接后自动获取封面") :
             s_status.network_ready ? url[0] ? url : "已连接，可传书和对时" : "连接后可传书和对时");
        fit_label(address, 21, 419);
        ui_text(fb, 64, 336, 21, address, EPD_DRAW_ALIGN_LEFT, false);
        network_button(fb, 6, s_status.network_ready ? (s_cover_wifi_only ? "继续" : "断开") : "连接",
                       !s_status.network_ready || s_cover_wifi_only);
        if (s_pressed == 11) ui_draw_pressed_round_rect(fb, network_control_rect(11), 14);
        ui_text_vc(fb, 563, 341, 21, "遗忘网络", EPD_DRAW_ALIGN_CENTER, false);
    } else {
        ui_text(fb, 64, 253, 29, "尚未保存网络", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 312, 23, "请从下方列表选择 2.4 GHz WiFi", EPD_DRAW_ALIGN_LEFT, false);
    }
    char count[64];
    snprintf(count, sizeof(count), "%u 个 · %u/%u 页", (unsigned)s_network_count,
             (unsigned)s_network_page + 1, (unsigned)(s_network_count ? (s_network_count + NETWORK_ROWS - 1) / NETWORK_ROWS : 1));
    ui_text(fb, 42, 418, 24, "附近网络", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 642, 420, 21, count, EPD_DRAW_ALIGN_RIGHT, false);
    EpdRect list = {36, 465, 612, 380};
    ui_fill_round_rect(fb, list, 22, UI_GRAY_WHITE);
    if (s_scan_pending) {
        ui_text_vc(fb, 342, 655, 25, "正在扫描 2.4 GHz WiFi", EPD_DRAW_ALIGN_CENTER, false);
    } else if (!s_network_count) {
        ui_text_vc(fb, 342, 620, 27, s_scan_error == ESP_OK ? "没有发现可用网络" : "扫描没有完成",
                   EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 678, 22, s_scan_error == ESP_OK ? "请靠近路由器后重新扫描" : "点下方按钮自动重试",
                   EPD_DRAW_ALIGN_CENTER, false);
    }
    for (int row = 0; !s_scan_pending && row < NETWORK_ROWS; ++row) {
        size_t index = s_network_page * NETWORK_ROWS + row;
        if (index >= s_network_count) break;
        const read_pico_transfer_network_t* network = &s_networks[index];
        EpdRect rect = network_control_rect(row);
        if (s_pressed == row) ui_draw_pressed_round_rect(fb, rect, 0);
        if (row) ui_fill_rect_fast((EpdRect){64, rect.y, 556, 2}, 0x70, fb);
        char name[33], detail[64];
        snprintf(name, sizeof(name), "%s", network->ssid);
        fit_label(name, 28, 520);
        ui_text(fb, 64, rect.y + 16, 28, name, EPD_DRAW_ALIGN_LEFT, false);
        snprintf(detail, sizeof(detail), "%s · %s", network->rssi > -60 ? "信号强" : network->rssi > -75 ? "信号良好" : "信号一般",
                 !network->supported ? "暂不支持" : network->saved ? (network->requires_password ? "已保存密码 · 可直连" : "已保存 · 可直连") : network->requires_password ? "需要密码" : "开放网络");
        ui_text(fb, 64, rect.y + 59, 21, detail, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, 615, rect.y + 47, 27, "›", EPD_DRAW_ALIGN_CENTER, false);
    }
    network_border(fb, list, 22);
    if (s_network_count > NETWORK_ROWS) {
        network_button(fb, 7, "‹", false);
        network_button(fb, 9, "›", false);
    }
    network_button(fb, 8, s_scan_error == ESP_OK ? "重新扫描" : "重试扫描", false);
    if (s_network_count <= NETWORK_ROWS) ui_draw_icon(fb, 597, 906, 32, UI_ICON_ROTATE_CW, UI_GRAY_BLACK);
    ui_text(fb, 42, 976, 21, "支持 2.4 GHz WiFi（信道 1–13）", EPD_DRAW_ALIGN_LEFT, false);
    char message[96];
    snprintf(message, sizeof(message), "%s", s_network_message[0] ? s_network_message :
             s_cover_wifi_only ? "连接 WiFi 后进入获取封面" :
             s_status.network_ready ? "退出此页后，WiFi 仍保持连接。" : "连接后，可退出此页继续使用。");
    fit_label(message, 21, 600);
    ui_text(fb, 42, transfer_footer_top()+38, 21, message, EPD_DRAW_ALIGN_LEFT, false);
    transfer_navigation(fb,3);
}

static void draw_password(uint8_t* fb) {
    ui_clear_page(fb);
    transfer_header(fb, "连接 WiFi", "输入完成后连接，密码只保存在本机");
    char name[33];
    snprintf(name, sizeof(name), "%s", s_selected.ssid);
    fit_label(name, UI_PX_BODY, ui_content_width());
    ui_text(fb, UI_MARGIN, 176, UI_PX_BODY, name, EPD_DRAW_ALIGN_LEFT, false);
    char caption[80];
    size_t len = strlen(s_password);
    snprintf(caption, sizeof(caption), "%s · %u/%u", s_selected.requires_password ? "密码" : "开放网络，无需密码", (unsigned)len, PASSWORD_MAX);
    ui_text_fixed_vc(fb, UI_MARGIN, 240, 22, caption, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_input_draw(fb, &s_password_input, password_control_rect(47), UI_PX_BODY,
                       !s_password_visible, NULL);
    ui_text_fixed_vc(fb, 36, 399, 22, s_network_message[0] ? s_network_message : "支持英文、数字、符号和空格", EPD_DRAW_ALIGN_LEFT, false);
    provisioning_button(fb, password_control_rect(44), "取消", 44);
    provisioning_button(fb, password_control_rect(45), s_password_visible ? "隐藏" : "显示密码", 45);
    provisioning_button(fb, password_control_rect(46), "连接并保存", 46);
    ui_keyboard_draw(fb, UI_SYSTEM_KEYBOARD_TOP);
}

static void queue_network_start(read_pico_transfer_mode_t mode) {
    s_mode = mode;
    s_qr_url = mode == READ_PICO_TRANSFER_MODE_STA;
    clear_qr();
    s_view = s_wifi_settings_only ? TRANSFER_NETWORKS : TRANSFER_HOME;
    s_forget_confirm = false;
    s_scan_pending = false;
    s_pressed = -1;
    clear_password();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = s_mode;
    read_pico_transfer_get_saved_wifi(s_status.wifi_ssid, &s_status.wifi_configured);
    s_saved_configured = s_status.wifi_configured;
    snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", s_status.wifi_ssid);
    if (s_wifi_settings_only) {
        s_network_connect_pending = true;
        snprintf(s_network_message, sizeof(s_network_message), "正在连接 %s…", s_status.wifi_ssid);
    } else s_start_pending = true;
}

static bool enter_networks(void) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    stop_session();
    s_start_pending = false;
    s_view = TRANSFER_NETWORKS;
    s_forget_confirm = false;
    s_network_count = s_network_page = 0;
    s_scan_pending = true;
    s_scan_error = ESP_OK;
    s_network_message[0] = 0;
    s_saved_ssid[0] = 0;
    s_saved_configured = false;
    read_pico_transfer_get_saved_wifi(s_saved_ssid, &s_saved_configured);
    clear_password();
    return true;
}

static bool start_hotspot_service(void) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    stop_session();
    s_mode = READ_PICO_TRANSFER_MODE_AP;
    s_qr_url = false;
    clear_qr();
    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = s_mode;
    s_start_pending = true;
    s_view = TRANSFER_HOME;
    s_method_message[0] = 0;
    return true;
}

// 提示页先呈现，下一轮才同步扫描；输入页不轮询已停服的传书状态。
// Present the scan notice first, scan synchronously next tick, and do not poll stopped transfer state in the editor.
static app_redraw_t network_ui_tick(app_ctx_t* ctx) {
    if (s_view != TRANSFER_NETWORKS || s_forget_confirm || ctx->consumed) return APP_REDRAW_NONE;
    if (s_network_connect_pending) {
        s_network_connect_pending = false;
        read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA,
            .network_only = true, .cover_lookup = s_cover_wifi_only};
        ttf_font_cache_clear();
        esp_err_t err = read_pico_transfer_start(&cfg);
        s_session_started = err == ESP_OK;
        read_pico_transfer_get_status(&s_status);
        if (s_cover_wifi_only && err == ESP_OK && s_status.network_ready) {
            return_from_wifi_setup(ctx, true);
            return APP_REDRAW_NONE;
        }
        if (err != ESP_OK) snprintf(s_network_message, sizeof(s_network_message), "连接启动失败，请重试");
        return APP_REDRAW_PAGE;
    }
    if (s_session_started) {
        read_pico_transfer_service_poll();
        read_pico_transfer_status_t next = {0};
        read_pico_transfer_get_status(&next);
        bool changed = next.state != s_status.state || next.network_ready != s_status.network_ready ||
                       next.last_error != s_status.last_error;
        const bool just_connected = !s_status.network_ready && next.network_ready &&
            next.mode == READ_PICO_TRANSFER_MODE_STA;
        s_status = next;
        if (s_cover_wifi_only && just_connected) {
            return_from_wifi_setup(ctx, true);
            return APP_REDRAW_NONE;
        }
        if (s_status.network_ready) snprintf(s_network_message, sizeof(s_network_message), "连接成功");
        else if (s_status.state == READ_PICO_TRANSFER_ERROR) snprintf(s_network_message, sizeof(s_network_message), "连接失败，请检查密码或信号");
        return changed ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    }
    if (!s_scan_pending) return APP_REDRAW_NONE;
    s_scan_pending = false;
    s_network_count = s_network_page = 0;
    // The scan notice is already in the framebuffer. Drop rebuildable glyph
    // bitmaps before starting WiFi, then recreate them while drawing the result.
    // This prevents the hosted radio from competing with the UI cache at init.
    ttf_font_cache_clear();
    esp_err_t err = read_pico_transfer_scan_wifi(s_networks, &s_network_count);
    s_scan_error = err;
    if (err != ESP_OK) {
        s_network_count = 0;
        snprintf(s_network_message, sizeof(s_network_message), "%s",
                 err == ESP_ERR_NO_MEM ? "内存已清理，请点下方按钮重试" : "扫描失败，请点下方按钮重试");
    }
    else if (!s_network_count) snprintf(s_network_message, sizeof(s_network_message), "未发现网络，请靠近路由器后重扫");
    else s_network_message[0] = 0;
    s_pressed = -1;
    return APP_REDRAW_PAGE;
}

static int provisioning_hit(uint16_t x, uint16_t y) {
    if (s_forget_confirm) {
        for (int i = 11; i <= 12; ++i) if (ui_rect_hit(network_control_rect(i), x, y)) return i;
        return -1;
    }
    if (s_view == TRANSFER_PASSWORD) {
        for (int i = 44; i < 48; ++i) if (ui_rect_hit(password_control_rect(i), x, y)) return i;
    } else {
        for (int i = 0; i < 12; ++i) {
            if (i < NETWORK_ROWS && (s_scan_pending || s_network_page * NETWORK_ROWS + i >= s_network_count)) continue;
            if ((i == 6 || i == 11) && !s_saved_configured) continue;
            if (ui_rect_hit(network_control_rect(i), x, y)) return i;
        }
    }
    return -1;
}

static app_redraw_t provisioning_action(int id) {
    if (s_forget_confirm) {
        if (id == 12) { s_forget_confirm = false; s_network_message[0] = 0; }
        else if (id == 11) {
            // 断联后页面标记可能已丢失；以组件的真实会话为准，上传中不能清配置。
            // A failed reconnect may lose the page flag; stop the actual component session, never an active upload.
            if (!stop_upload_if_idle()) {
                snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再遗忘");
                return APP_REDRAW_PAGE;
            }
            s_network_connect_pending = s_scan_pending = false;
            esp_err_t err = read_pico_transfer_forget_wifi();
            if (err == ESP_OK) {
                read_pico_transfer_get_status(&s_status);
                s_forget_confirm = false;
                s_saved_configured = false;
                s_saved_ssid[0] = 0;
                snprintf(s_network_message, sizeof(s_network_message), "已遗忘网络");
            } else snprintf(s_network_message, sizeof(s_network_message), "遗忘失败，请重试或取消");
        }
        return APP_REDRAW_PAGE;
    }
    if (s_view == TRANSFER_NETWORKS) {
        if (id < NETWORK_ROWS) {
            s_selected = s_networks[s_network_page * NETWORK_ROWS + id];
            if (!s_selected.supported) {
                snprintf(s_network_message, sizeof(s_network_message), "此网络认证暂不支持，请选择其他网络");
                return APP_REDRAW_PAGE;
            }
            if(s_selected.saved){
                esp_err_t err=read_pico_transfer_select_saved_wifi(s_selected.ssid);
                if(err==ESP_OK){queue_network_start(READ_PICO_TRANSFER_MODE_STA);return APP_REDRAW_PAGE;}
                snprintf(s_network_message,sizeof(s_network_message),"保存的凭据暂不可用，请重新输入密码");
            }
            if (!s_selected.requires_password) {
                esp_err_t err = read_pico_transfer_save_wifi(s_selected.ssid, "");
                if (err == ESP_OK) {
                    queue_network_start(READ_PICO_TRANSFER_MODE_STA);
                    return APP_REDRAW_PAGE;
                }
                snprintf(s_network_message, sizeof(s_network_message), "开放网络保存失败，请重试");
                return APP_REDRAW_PAGE;
            }
            clear_password();
            s_network_message[0] = 0;
            ui_keyboard_begin(&s_password_input, true);
            s_view = TRANSFER_PASSWORD;
        } else if (id == 6) {
            if (s_status.network_ready) {
                if (!stop_upload_if_idle()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再断开");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = false;
                snprintf(s_network_message, sizeof(s_network_message), "已断开，连接信息保留");
            } else {
                // 未连上不等于驱动已停止；重试前先释放仍在等待或超时的旧 STA。
                // Disconnected does not mean stopped; release the pending or timed-out STA before retrying.
                if (!stop_upload_if_idle()) {
                    snprintf(s_network_message, sizeof(s_network_message), "正在传输，请完成后再连接");
                    return APP_REDRAW_PAGE;
                }
                s_network_connect_pending = false;
                queue_network_start(READ_PICO_TRANSFER_MODE_STA);
            }
        }
        else if (id == 7 && s_network_page) --s_network_page;
        else if (id == 8) {
            if (s_session_started) stop_session();
            s_scan_pending = true; s_scan_error = ESP_OK; s_network_message[0] = 0;
        }
        else if (id == 9 && (s_network_page + 1) * NETWORK_ROWS < s_network_count) ++s_network_page;
        else if (id == 10) queue_network_start(s_mode);
        else if (id == 11) { s_forget_confirm = true; s_network_message[0] = 0; }
        return APP_REDRAW_PAGE;
    }
    size_t len = strlen(s_password);
    if (id == 44) { clear_password(); s_network_message[0] = 0; s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
    else if (id == 45) s_password_visible = !s_password_visible;
    else if (id == 46) {
        if (s_selected.requires_password && len < 8) {
            snprintf(s_network_message, sizeof(s_network_message), "加密网络密码至少 8 个字符");
        } else {
            esp_err_t err = read_pico_transfer_save_wifi(s_selected.ssid,
                s_selected.requires_password ? s_password : "");
            if (err == ESP_OK) { queue_network_start(READ_PICO_TRANSFER_MODE_STA); return APP_REDRAW_PAGE; }
            snprintf(s_network_message, sizeof(s_network_message), "%s", err == ESP_ERR_INVALID_ARG ?
                     "密码须 8–63 字符或 64 位十六进制" : "保存失败，输入已保留，请重试");
        }
    }
    return APP_REDRAW_AREA;
}

static bool s_input_layout, s_input_settle;
static app_redraw_t password_paint(app_ctx_t *ctx, bool field, bool controls) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, UI_SYSTEM_KEYBOARD_TOP, password_control_rect(47),
        UI_PX_BODY, !s_password_visible, NULL, field);
    EpdRect extra = {UI_MARGIN, 215, ui_content_width(), controls ? 287 : 159};
    if (update.field || controls) {
        ui_clear_rect_fast(ctx->fb, (EpdRect){UI_MARGIN, 215, ui_content_width() - 156, 58});
        char caption[80];
        snprintf(caption, sizeof(caption), "%s · %u/%u", s_selected.requires_password ? "密码" : "开放网络，无需密码", (unsigned)strlen(s_password), PASSWORD_MAX);
        ui_text_fixed_vc(ctx->fb, UI_MARGIN, 240, 22, caption, EPD_DRAW_ALIGN_LEFT, false);
    }
    if (controls) {
        ui_clear_rect_fast(ctx->fb, (EpdRect){36, 377, 612, 36});
        ui_text_fixed_vc(ctx->fb, 36, 399, 22, s_network_message[0] ? s_network_message : "支持英文、数字、符号和空格", EPD_DRAW_ALIGN_LEFT, false);
        for (int i = 44; i <= 46; ++i) {
            EpdRect button = password_control_rect(i); ui_clear_rect_fast(ctx->fb, button);
            provisioning_button(ctx->fb, button, i == 44 ? "取消" : i == 45 ? s_password_visible ? "隐藏" : "显示密码" : "连接并保存", i);
        }
    }
    if (update.field || controls) update.area = update.area.width ? ui_rect_union(update.area, extra) : extra;
    s_area = update.area; s_input_layout = update.layout; s_input_settle = update.settle; s_settle = false;
    return update.area.width ? APP_REDRAW_AREA : APP_REDRAW_NONE;
}
static app_redraw_t provisioning_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_view == TRANSFER_PASSWORD) {
        if (ev->type == UI_GESTURE_PRESS)
            return ui_keyboard_press(ev->x0, ev->y0, UI_SYSTEM_KEYBOARD_TOP, ctx->now_ms) ? password_paint(ctx, false, false) : APP_REDRAW_NONE;
        bool feedback = ev->type != UI_GESTURE_LONG_PRESS && ui_keyboard_release();
        if (ev->type != UI_GESTURE_TAP) return feedback ? password_paint(ctx, false, false) : APP_REDRAW_NONE;
        int x = ev->x0, y = ev->y0;
        if (ui_text_input_tap(&s_password_input, password_control_rect(47), UI_PX_BODY, !s_password_visible, NULL, x, y)) {
            return password_paint(ctx, true, false);
        }
        for (int i = 44; i <= 46; ++i) if (ui_rect_hit(password_control_rect(i), x, y)) {
            app_redraw_t changed = provisioning_action(i);
            if (changed == APP_REDRAW_AREA) return password_paint(ctx, true, true);
            return changed;
        }
        ui_keyboard_result_t result = ui_keyboard_tap(x, y, UI_SYSTEM_KEYBOARD_TOP, ctx->now_ms);
        if (result == UI_KEYBOARD_DONE) {
            app_redraw_t changed = provisioning_action(46);
            if (changed == APP_REDRAW_PAGE) return changed;
            result = UI_KEYBOARD_CHANGED;
        }
        if (result == UI_KEYBOARD_CHANGED) return password_paint(ctx, false, y >= 990 && x >= 534);
        return APP_REDRAW_NONE;
    }
    int old = s_pressed;
    int origin = provisioning_hit(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    bool action = ev->type == UI_GESTURE_TAP && old >= 0 && old == origin && provisioning_hit(ev->x, ev->y) == origin;
    EpdRect old_rect = s_view == TRANSFER_PASSWORD ? password_control_rect(old >= 0 ? old : origin >= 0 ? origin : 0)
                                                  : network_control_rect(old >= 0 ? old : origin >= 0 ? origin : 0);
    if (action) {
        if (s_cover_wifi_only && origin == 6 && s_status.network_ready) {
            // Aggregation is set at driver init. Reconnect to the saved network if
            // the current association was opened by another feature.
            if (!s_status.cover_lookup) {
                if (stop_upload_if_idle()) queue_network_start(READ_PICO_TRANSFER_MODE_STA);
                else snprintf(s_network_message, sizeof(s_network_message), "网络正在使用，请稍后重试");
                return APP_REDRAW_PAGE;
            }
            return_from_wifi_setup(ctx, true);
            return APP_REDRAW_NONE;
        }
        if (s_view == TRANSFER_PASSWORD && origin == 47) {
            ui_text_input_tap(&s_password_input, password_control_rect(47), UI_PX_BODY,
                              !s_password_visible, NULL, ev->x0, ev->y0);
            render(ctx, ctx->fb);
            s_area = password_control_rect(47);
            s_settle = false;
            return APP_REDRAW_AREA;
        }
        app_redraw_t redraw = provisioning_action(origin);
        if (redraw == APP_REDRAW_PAGE) return redraw;
        render(ctx, ctx->fb);
        s_area = ui_rect_union(old_rect, (EpdRect){UI_MARGIN, 240, ui_content_width(), 744});
        s_settle = false;
        return APP_REDRAW_AREA;
    }
    if (old == s_pressed) return APP_REDRAW_NONE;
    render(ctx, ctx->fb);
    s_area = old_rect;
    s_settle = false;
    return APP_REDRAW_AREA;
}

static EpdRect status_rect(void) { return (EpdRect){36, 676, 612, 270}; }
#if KIIKO_BOARD_METALIO47
// Only the live text and bar; exclude the card border, capacity and controls.
static EpdRect progress_rect(void) {
    EpdRect r = status_rect();
    return (EpdRect){r.x + 24, r.y + 18, r.width - 48, 150};
}
#endif
static uint64_t free_bytes(void* arg) { return book_store_free_bytes(arg); }

static void draw_status(uint8_t* fb) {
    EpdRect area = status_rect();
    ui_clear_rect_fast(fb, area);
    ui_fill_round_rect(fb, area, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, area, 24, 0x98);
    const char* state = "网络已停止";
    if (s_start_pending || s_status.state == READ_PICO_TRANSFER_STARTING) state = "正在准备传书…";
    else if (s_status.state == READ_PICO_TRANSFER_READY) state = "等待上传";
    else if (s_status.state == READ_PICO_TRANSFER_UPLOADING) state = "正在接收";
    else if (s_status.state == READ_PICO_TRANSFER_ERROR) state = s_status.network_ready ? "上传失败，请重试" :
        s_mode == READ_PICO_TRANSFER_MODE_AP ? "热点传书未启动" : "WiFi 传书未启动";
    if (s_media_lost) state = "存储已移除，传书已停止";
    ui_text(fb, area.x + 28, area.y + 24, 26, state, EPD_DRAW_ALIGN_LEFT, false);
    char line[256];
    if (s_media_lost) snprintf(line, sizeof(line), "重新进入传书可使用内置存储");
    else if (s_status.state == READ_PICO_TRANSFER_ERROR && !s_status.network_ready)
        snprintf(line, sizeof(line), "原因：%s", esp_err_to_name(s_status.last_error));
    else if (s_mode == READ_PICO_TRANSFER_MODE_AP)
        snprintf(line, sizeof(line), "已连接 %u 台 · 已完成 %u 本", s_status.sta_count, s_status.done_count);
    else snprintf(line, sizeof(line), "同网浏览器上传 · 已完成 %u 本", s_status.done_count);
    ui_text(fb, area.x + 28, area.y + 68, 19, line, EPD_DRAW_ALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%s", s_status.cur_name);
    while (*line && ttf_text_width_px(ui_text_effective_px(19), line) > area.width - 280) {
        size_t n = strlen(line) - 1;
        while (n && ((unsigned char)line[n] & 0xc0) == 0x80) --n;
        line[n] = 0;
    }
    ui_text(fb, area.x + 28, area.y + 108, 19, line[0] ? line : "尚无正在传输的文件", EPD_DRAW_ALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%.1f / %.1f MB", s_status.cur_bytes / 1048576.0, s_status.cur_total / 1048576.0);
    ui_text(fb, area.x + area.width - 28, area.y + 108, 19, line, EPD_DRAW_ALIGN_RIGHT, false);
    EpdRect track = {area.x + 28, area.y + 150, area.width - 56, 10};
    ui_fill_round_rect(fb, track, 5, 0xc8);
    if (s_status.cur_total) {
        uint64_t width = (uint64_t)s_status.cur_bytes * track.width / s_status.cur_total;
        track.width = width > (uint64_t)track.width ? track.width : (int)width;
        ui_fill_round_rect(fb, track, 5, UI_GRAY_BLACK);
    }
    char capacity[112];
    if (s_media_lost) snprintf(capacity, sizeof(capacity), "TF 卡已移除，原存储已停用");
    else snprintf(capacity, sizeof(capacity), "%s · 剩余 %.1f MB%s", s_root.is_flash ? "内置" : "TF 卡",
                  s_free / 1048576.0, s_root.is_flash ? " · 单文件 ≤ 1 MB" : "");
    ui_text(fb, area.x + 28, area.y + 190, 18, capacity, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, area.x + 28, area.y + 228, 18,
            s_status.last_error != ESP_OK ? "传输未完成，请检查网络后重试" : "同名文件会覆盖；完成后自动更新书架",
            EPD_DRAW_ALIGN_LEFT, false);
}

static EpdRect control_rect(int id) {
    if (id == 0) return (EpdRect){36, 608, 296, 56};
    if (id == 1) return (EpdRect){352, 608, 296, 56};
    if (id == 2) return (EpdRect){36, transfer_footer_top(), 612, 76};
    return (EpdRect){0};
}

static void render_content(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    if (s_usb_entry) {
        ui_clear_page(fb);
        bool active = usb_storage_active();
        transfer_header(fb, "USB 读卡", NULL);
        ui_text(fb, 42, 181, 20, "连接状态", EPD_DRAW_ALIGN_LEFT, false);
        EpdRect status = {36, 211, 612, 190};
        ui_fill_round_rect(fb, status, 24, UI_GRAY_WHITE);
        ui_draw_round_rect(fb, status, 24, 0x90);
        const char *state = s_usb_start_pending ? "正在准备 TF 卡" :
            !active ? "USB 读卡未开启" :
            s_usb_host_connected ? "电脑已连接" : "等待电脑连接";
        ui_text(fb, 64, 241, 31, state, EPD_DRAW_ALIGN_LEFT, false);
        if (active) {
            ui_text(fb, 64, 296, 20, "磁盘名称  SD_CARD", EPD_DRAW_ALIGN_LEFT, false);
            char size[64];
            snprintf(size, sizeof(size), "TF 卡容量  %.1f GB", s_usb_capacity_bytes / 1073741824.0);
            ui_text(fb, 64, 337, 20, size, EPD_DRAW_ALIGN_LEFT, false);
        } else ui_text(fb, 64, 309, 20, s_usb_message, EPD_DRAW_ALIGN_LEFT, false);

        ui_text(fb, 42, 450, 20, "文件存放位置", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, (EpdRect){36, 478, 612, 206}, 24, UI_GRAY_WHITE);
        ui_text(fb, 64, 510, 27, "books", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 553, 19, "EPUB、TXT 书籍", EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, 580, 64, 556, 0xb0);
        ui_text(fb, 64, 597, 27, "fonts", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 640, 19, "TTF、OTF 字体", EPD_DRAW_ALIGN_LEFT, false);

        ui_text(fb, 42, 740, 20, "完成传输", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, (EpdRect){36, 766, 612, 147}, 24, UI_GRAY_WHITE);
        ui_text(fb, 64, 802, 21, "1  在电脑上等待复制完成", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, 64, 855, 21, "2  安全弹出 SD_CARD，再结束读卡", EPD_DRAW_ALIGN_LEFT, false);
        ui_fill_round_rect(fb, control_rect(2), 22, s_pressed == 2 ? 0x60 : 0x38);
        ui_text_vc(fb, 342, transfer_footer_top()+38, 24, active ? "结束 USB 读卡" : "返回文件管理",
                   EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
    if (s_view == TRANSFER_METHODS) { draw_methods(fb); return; }
    if (s_view == TRANSFER_NETWORKS) { draw_networks(fb); return; }
    if (s_view == TRANSFER_PASSWORD) { draw_password(fb); return; }
    ui_clear_page(fb);
    transfer_header(fb, s_mode == READ_PICO_TRANSFER_MODE_AP ? "热点传书" : "WiFi 传书", NULL);
    EpdRect connection = {36, 168, 612, 132};
    ui_fill_round_rect(fb, connection, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, connection, 24, 0x98);
    char name[64];
    snprintf(name, sizeof(name), "%s", s_mode == READ_PICO_TRANSFER_MODE_AP ? s_status.ssid : s_status.wifi_ssid);
    if (!name[0]) snprintf(name, sizeof(name), "%s", s_mode == READ_PICO_TRANSFER_MODE_AP ? "kiikoread 热点" : "已保存的 WiFi");
    fit_label(name, 26, 420);
    ui_text(fb, 66, 190, 26, name, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 66, 236, 19,
            s_mode == READ_PICO_TRANSFER_MODE_AP ? "口令：" READ_PICO_TRANSFER_PASSWORD :
            s_status.network_ready ? "设备已连接；手机需同网" :
            s_start_pending || s_status.state == READ_PICO_TRANSFER_STARTING ? "正在连接已保存网络…" :
            s_status.wifi_configured ? "已保存，尚未连接" : "尚未选择 WiFi",
            EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, 605, 234, 20, s_status.network_ready ? "服务已开启" : "准备中", EPD_DRAW_ALIGN_RIGHT, false);
    int url_px = UI_PX_CAPTION;
    while (url_px > 14 && ttf_text_width_px(url_px, s_status.url) > 560) --url_px;
    if (s_status.network_ready && s_qr_ready)
        ui_wifi_qr_draw(fb, (EpdRect){222, 330, 240, 240});
    else {
        ui_draw_round_rect(fb, (EpdRect){222, 330, 240, 240}, 8, 0xa0);
        ui_text_vc(fb, 342, 450, 22, s_start_pending ? "正在生成二维码…" : "等待网络地址…", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_mode == READ_PICO_TRANSFER_MODE_AP) {
        ui_text_vc(fb, 342, 583, 20,
                   s_status.network_ready ? s_status.url : "热点就绪后显示访问地址",
                   EPD_DRAW_ALIGN_CENTER, false);
        for (int i = 0; i < 2; ++i) {
            EpdRect tab = control_rect(i);
            ui_fill_round_rect(fb, tab, 18, (s_qr_url == (i == 1)) ? 0x42 : UI_GRAY_WHITE);
            ui_draw_round_rect(fb, tab, 18, 0x92);
            ui_text_vc(fb, tab.x + tab.width / 2, tab.y + tab.height / 2, 21,
                       i ? "打开传书网页" : "连接 kiikoread 热点", EPD_DRAW_ALIGN_CENTER, s_qr_url == (i == 1));
        }
    } else {
        ui_text_vc(fb, 342, 602, url_px,
                   s_status.network_ready ? s_status.url : "服务启动后显示访问地址", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 642, 18, "手机或电脑扫码后，即可在浏览器上传内容", EPD_DRAW_ALIGN_CENTER, false);
    }
    draw_status(fb);
    EpdRect back = control_rect(2);
    if (s_pressed == 2) ui_draw_pressed_round_rect(fb, back, UI_BTN_RADIUS);
    ui_draw_button(fb, back,
                   s_mode == READ_PICO_TRANSFER_MODE_AP && s_status.state == READ_PICO_TRANSFER_ERROR && !s_status.network_ready
                       ? "重试热点传书" : "停止传输服务", false);
    transfer_navigation(fb,2);
}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    render_content(ctx,fb);
#if KIIKO_BOARD_METALIO47
    display_monochrome_content(fb);
#endif
    transfer_navigation(fb,s_wifi_settings_only?3:2);
}


static void on_enter(app_ctx_t* ctx) {
    (void)ctx;
    display_set_bulk_io(true);
    s_media_lost = false;
    memset(&s_status, 0, sizeof(s_status));
    s_qr_url = s_mode == READ_PICO_TRANSFER_MODE_STA;
    clear_qr();
    memset(&s_root, 0, sizeof(s_root));
    s_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_start_pending = false;
    s_usb_message[0] = 0;
    s_usb_start_pending = false;
    s_usb_retry_ms = 0;
    s_usb_host_connected = false;
    s_usb_capacity_bytes = 0;
    s_method_message[0] = 0;
    s_view = TRANSFER_METHODS;
    s_direct_entry = s_usb_requested || s_wifi_upload_requested || s_hotspot_requested;
    s_usb_entry = s_usb_requested;
    s_forget_confirm = false;
    s_scan_pending = false;
    s_network_connect_pending = false;
    clear_password();
    s_session_started = false;
    s_pressed = -1;
    s_settle = s_any_changed = false;
#if KIIKO_BOARD_METALIO47
    s_progress_pending = false;
#endif
    s_status.mode = s_mode;
    read_pico_transfer_get_saved_wifi(s_status.wifi_ssid, &s_status.wifi_configured);
    s_poll_ms = 0;
    s_free = 0;
    if (s_usb_requested) {
        read_pico_sd_info_t usb_sd = {0};
        if (read_pico_sd_get_info(&usb_sd) == ESP_OK) s_usb_capacity_bytes = usb_sd.capacity_bytes;
        s_usb_requested = false;
        s_view = TRANSFER_HOME;
        s_usb_start_pending = true;
        snprintf(s_usb_message, sizeof(s_usb_message), "正在卸载本机 TF 卡并启动 USB…");
    } else if (s_hotspot_requested) {
        s_hotspot_requested = false;
        if (!start_hotspot_service()) {
            s_view = TRANSFER_HOME;
            s_status.state = READ_PICO_TRANSFER_ERROR;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        }
    } else if (s_wifi_upload_requested) {
        s_wifi_upload_requested = false;
        // 配网页可能留下常驻的仅联网会话；传书前先释放它再启动上传服务。
        // Provisioning may leave a network-only session alive; release it before starting upload.
        if (s_status.wifi_configured && read_pico_transfer_try_stop_if_idle())
            queue_network_start(READ_PICO_TRANSFER_MODE_STA);
        else if (s_status.wifi_configured) {
            s_view = TRANSFER_HOME;
            s_status.state = READ_PICO_TRANSFER_ERROR;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        }
        else if (!enter_networks()) {
            s_view = TRANSFER_NETWORKS;
            s_scan_pending = false;
            snprintf(s_network_message, sizeof(s_network_message), "网络正在结束，请稍后再试");
        }
    } else if (s_wifi_setup_requested) {
        s_wifi_setup_requested = false;
        read_pico_transfer_get_status(&s_status);
        if (s_status.network_ready && s_status.mode == READ_PICO_TRANSFER_MODE_STA) {
            s_session_started = true;
            s_view = TRANSFER_NETWORKS;
            s_saved_configured = s_status.wifi_configured;
            snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", s_status.wifi_ssid);
            snprintf(s_network_message, sizeof(s_network_message), "连接成功");
        } else {
            (void)enter_networks();
        }
    } else {
        s_wifi_settings_only = false;
    }
}

static void stop_session(void) {
    clear_qr();
    // 停服后读最终计数，保留本页跨模式文件变更的记录。
    // Join reception before reading counts and retain file changes across mode switches.
    read_pico_transfer_stop();
    read_pico_transfer_get_status(&s_status);
    if (s_session_started && s_status.changed_count) s_any_changed = true;
    s_session_started = false;
}

static bool stop_upload_if_idle(void) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    stop_session();
    s_start_pending = false;
    return true;
}

// 必须先等待 HTTP 退出，再清除旧根；不会让在途请求写到另一存储。
// Join HTTP before clearing the old root; in-flight requests never move to another storage.
static void transfer_on_media_lost(app_ctx_t* ctx) {
    (void)ctx;
    if (s_root.is_flash || s_cover_wifi_only) return;
    stop_session();
    s_start_pending = s_scan_pending = false;
    s_view = s_direct_entry ? TRANSFER_HOME : TRANSFER_METHODS;
    clear_password();
    s_pressed = -1;
    s_free = 0;
    memset(&s_root, 0, sizeof(s_root));
    s_media_lost = true;
    s_settle = false;
}

static void transfer_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    s_usb_start_pending = false;
    if (usb_storage_active()) {
        esp_err_t err = usb_storage_stop();
        if (err == ESP_OK) s_any_changed = true;
        else ESP_LOGW(TAG, "USB remained active on page exit: %s", esp_err_to_name(err));
    }
    clear_password();
    s_scan_pending = s_network_connect_pending = false;
    read_pico_transfer_status_t latest = {0};
    read_pico_transfer_get_status(&latest);
    bool keep_sta = s_wifi_settings_only && latest.network_ready &&
                    latest.mode == READ_PICO_TRANSFER_MODE_STA;
    if (keep_sta) {
        clear_qr();
        s_session_started = false; // 全局循环接管保活；再次进页会从组件状态恢复。
    } else stop_session();
    s_cover_wifi_only = false;
    s_wifi_settings_only = false;
    s_usb_entry = false;
    s_direct_entry = false;
    display_set_bulk_io(false);
    if (s_any_changed) book_store_notify_changed();
    ESP_LOGI(TAG, "leave keep_sta=%d heap before=%u after=%u internal_before=%u internal_after=%u",
             keep_sta,
             (unsigned)s_heap_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)s_internal_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (s_view == TRANSFER_PASSWORD) {
        if (ctx->consumed) return APP_REDRAW_NONE;
        bool held = !ctx->released && ctx->touch && ctx->touch->touched && ctx->touch->count == 1;
        if (ui_keyboard_hold_tick(held, ctx->touch ? ctx->touch->x : 0,
            ctx->touch ? ctx->touch->y : 0, ctx->now_ms) == UI_KEYBOARD_CHANGED) return password_paint(ctx, false, false);
        if (ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) == UI_KEYBOARD_CHANGED)
            return password_paint(ctx, false, false);
    }
    uint32_t revision = __atomic_load_n(&s_signature_revision, __ATOMIC_ACQUIRE);
    if (revision != s_signature_seen) {
        s_signature_seen = revision;
        return APP_REDRAW_PAGE;
    }
    if (s_usb_entry) {
        if (!s_usb_start_pending) {
            bool connected = usb_storage_connected();
            if (connected == s_usb_host_connected) return APP_REDRAW_NONE;
            s_usb_host_connected = connected;
            return APP_REDRAW_PAGE;
        }
        if (s_usb_retry_ms && ctx->now_ms - s_usb_retry_ms < 250) return APP_REDRAW_NONE;
        s_usb_retry_ms = ctx->now_ms;
        esp_err_t err = usb_storage_start();
        if (err == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_usb_start_pending = false;
        s_usb_host_connected = usb_storage_connected();
        if (err != ESP_OK)
            snprintf(s_usb_message, sizeof(s_usb_message), "USB 模式启动失败：%s", esp_err_to_name(err));
        return APP_REDRAW_PAGE;
    }
    if (usb_storage_active()) return APP_REDRAW_NONE;
    if (s_view == TRANSFER_METHODS) return APP_REDRAW_NONE;
    if (s_view != TRANSFER_HOME) return network_ui_tick(ctx);
    if (s_start_pending) {
        s_start_pending = false;
        // 传书模式也应在启动无线电前释放可重建字形，避免内部内存不足。
        // Release rebuildable glyphs before starting the radio for uploads too.
        ttf_font_cache_clear();
        esp_err_t err = book_store_upload_root(&s_root);
        if (err == ESP_OK) {
            s_media_lost = false;
            s_free = book_store_free_bytes(&s_root);
            read_pico_transfer_cfg_t cfg = {.mode = s_mode, .root_dir = s_root.path, .is_flash = s_root.is_flash,
                .file_limit = book_store_file_limit(&s_root), .free_bytes_cb = free_bytes, .free_bytes_ctx = &s_root,
                .file_changed_cb = book_progress_forget, .file_deleted_cb = transfer_file_deleted,
                .file_moved_cb = transfer_file_moved,
                .wallpaper_set_cb = transfer_set_wallpaper,
                .signature_get_cb = transfer_signature_get, .signature_set_cb = transfer_signature_set,
                .title_get_cb = NULL, .title_set_cb = NULL};
            err = read_pico_transfer_start(&cfg);
            s_session_started = err == ESP_OK;
            read_pico_transfer_get_status(&s_status);
            if (err == ESP_OK) s_free = book_store_free_bytes(&s_root);
        }
        s_status.mode = s_mode;
        if (err != ESP_OK) { s_status.state = READ_PICO_TRANSFER_ERROR; s_status.last_error = err; }
        if (err == ESP_OK) prepare_qr();
        else clear_qr();
        ESP_LOGI(TAG, "start root=%s result=%s", s_root.path, esp_err_to_name(err));
        return APP_REDRAW_PAGE;
    }
    if (ctx->consumed || ctx->now_ms - s_poll_ms < 2000) return APP_REDRAW_NONE;
    s_poll_ms = ctx->now_ms;
    read_pico_transfer_service_poll();
    read_pico_transfer_status_t next = {0};
    read_pico_transfer_get_status(&next);
#if KIIKO_BOARD_METALIO47
    // At most one small DU difference every two seconds, including new files.
    // Keep a failed scan pending so an unchanged receive snapshot can retry.
    if (next.state == READ_PICO_TRANSFER_UPLOADING) {
        bool changed = s_progress_pending || s_status.state != next.state ||
            s_status.cur_bytes != next.cur_bytes || s_status.cur_total != next.cur_total ||
            s_status.done_count != next.done_count || s_status.sta_count != next.sta_count ||
            strcmp(s_status.cur_name, next.cur_name);
        s_status = next;
        s_settle = false;
        if (!changed) return APP_REDRAW_NONE;
        s_progress_pending = true;
        draw_status(ctx->fb);
        display_monochrome_content(ctx->fb);
        s_area = progress_rect();
        return APP_REDRAW_AREA;
    }
    s_progress_pending = false;
#else
    // Other boards retain their existing upload refresh policy.
    if (next.state == READ_PICO_TRANSFER_UPLOADING && s_status.state == READ_PICO_TRANSFER_UPLOADING) {
        s_status.cur_bytes = next.cur_bytes;
        s_status.sta_count = next.sta_count;
        return APP_REDRAW_NONE;
    }
#endif
    if (s_status.state == READ_PICO_TRANSFER_ERROR && next.state == READ_PICO_TRANSFER_STOPPED) return APP_REDRAW_NONE;
    bool network_changed = next.network_ready != s_status.network_ready ||
        next.mode != s_status.mode || strcmp(next.ssid, s_status.ssid) ||
        next.wifi_configured != s_status.wifi_configured || strcmp(next.url, s_status.url) ||
        strcmp(next.wifi_ssid, s_status.wifi_ssid);
    if (!network_changed && next.state == s_status.state && next.sta_count == s_status.sta_count &&
        next.cur_bytes == s_status.cur_bytes && next.cur_total == s_status.cur_total &&
        next.done_count == s_status.done_count && next.changed_count == s_status.changed_count && next.last_error == s_status.last_error &&
        !strcmp(next.cur_name, s_status.cur_name)) return APP_REDRAW_NONE;
    s_settle = next.changed_count != s_status.changed_count || next.done_count != s_status.done_count ||
        (s_status.state == READ_PICO_TRANSFER_UPLOADING && next.state != READ_PICO_TRANSFER_UPLOADING);
    s_status = next;
    if (network_changed) prepare_qr();
    if (s_settle) {
        s_free = book_store_free_bytes(&s_root);
        draw_status(ctx->fb);
        s_area = status_rect();
        return APP_REDRAW_AREA;
    }
    if (network_changed) { s_free = book_store_free_bytes(&s_root); return APP_REDRAW_PAGE; }
    draw_status(ctx->fb);
    s_area = status_rect();
    return APP_REDRAW_AREA;
}

static int hit_control(uint16_t x, uint16_t y) {
    if (usb_storage_active()) return ui_rect_hit(control_rect(2), x, y) ? 2 : -1;
    if (s_mode == READ_PICO_TRANSFER_MODE_AP && s_view == TRANSFER_HOME)
        for (int i = 0; i < 2; ++i) if (ui_rect_hit(control_rect(i), x, y)) return i;
    return ui_rect_hit(control_rect(2), x, y) ? 2 : -1;
}

static int hit_method(uint16_t x, uint16_t y) {
    for (int i = 0; i < 2; ++i) if (ui_rect_hit(method_control_rect(i), x, y)) return i;
    return -1;
}

static app_redraw_t stop_usb_for_navigation(app_ctx_t *ctx, bool home) {
    s_usb_start_pending = false;
    if (usb_storage_active()) {
        esp_err_t err = usb_storage_stop();
        if (err != ESP_OK) {
            snprintf(s_usb_message, sizeof(s_usb_message), "USB 写入未完成：%s，请在电脑弹出后重试", esp_err_to_name(err));
            return APP_REDRAW_PAGE;
        }
        s_any_changed = true;
    }
    if (home) ui_nav_request(ctx, 0);
    else if (s_direct_entry) ctx->request_return = true;
    else s_view = TRANSFER_METHODS;
    return ctx->request_return || home ? APP_REDRAW_NONE : APP_REDRAW_PAGE;
}

static app_redraw_t method_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    int old = s_pressed;
    int origin = hit_method(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    if (ev->type == UI_GESTURE_TAP && old >= 0 && old == origin && hit_method(ev->x, ev->y) == origin) {
        if (origin == 0) {
            if (s_status.wifi_configured) {
                if (stop_upload_if_idle()) {
                    queue_network_start(READ_PICO_TRANSFER_MODE_STA);
                    s_method_message[0] = 0;
                    return APP_REDRAW_PAGE;
                }
            } else if (enter_networks()) return APP_REDRAW_PAGE;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        } else if (origin == 1) {
            if (start_hotspot_service()) return APP_REDRAW_PAGE;
            snprintf(s_method_message, sizeof(s_method_message), "传输正在结束，请稍后再试");
        }
        return APP_REDRAW_PAGE;
    }
    if (old == s_pressed) return APP_REDRAW_NONE;
    // 按下反馈只刷新单行；整页刷新会取消尚未松手的手势，导致入口无法点击。
    // Refresh only the pressed row; a page redraw cancels the active gesture before its tap.
    render(ctx, ctx->fb);
    s_area = method_control_rect(old >= 0 ? old : s_pressed);
    s_settle = false;
    return APP_REDRAW_AREA;
}

static app_redraw_t on_gesture(app_ctx_t* ctx, const ui_gesture_event_t* ev) {
    if (s_status.state == READ_PICO_TRANSFER_UPLOADING) return APP_REDRAW_NONE;

    if (ev->type == UI_GESTURE_TAP && ev->y0 < 160 && ev->x0 < 120) {
        if (s_usb_entry || usb_storage_active()) return stop_usb_for_navigation(ctx, false);
        if (s_view == TRANSFER_PASSWORD) { clear_password(); s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
        if (s_view == TRANSFER_NETWORKS) {
            if (s_wifi_settings_only) { return_from_wifi_setup(ctx, false); return APP_REDRAW_NONE; }
            if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
            s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE;
        }
        if (s_view == TRANSFER_HOME) {
            if (!stop_upload_if_idle()) return APP_REDRAW_NONE;
            if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
            s_view = TRANSFER_METHODS;
            return APP_REDRAW_PAGE;
        }
        ctx->request_return = true;
        return APP_REDRAW_NONE;
    }
    if (s_view == TRANSFER_METHODS) return method_gesture(ctx, ev);
    if (s_view != TRANSFER_HOME) return provisioning_gesture(ctx, ev);
    int old = s_pressed;
    int origin = hit_control(ev->x0, ev->y0);
    s_pressed = ev->type == UI_GESTURE_PRESS ? origin : -1;
    if (ev->type == UI_GESTURE_TAP && old >= 0 && old == origin &&
        hit_control(ev->x, ev->y) == origin) {
        if (s_usb_entry) {
            return stop_usb_for_navigation(ctx, false);
        }
        if (origin == 0 || origin == 1) {
            bool url = origin == 1;
            if (s_qr_url != url) {
                s_qr_url = url;
                prepare_qr();
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (origin == 2) {
            if (s_mode == READ_PICO_TRANSFER_MODE_AP && s_status.state == READ_PICO_TRANSFER_ERROR &&
                !s_status.network_ready && start_hotspot_service()) return APP_REDRAW_PAGE;
            // 即使没有切页历史也先停服，取消尚未执行的启动请求。
            // Stop even without navigation history and cancel a queued start.
            if (!stop_upload_if_idle()) return APP_REDRAW_NONE;
            if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
            s_view = TRANSFER_METHODS;
            return APP_REDRAW_PAGE;
        }
    }
    if (s_pressed == old) return APP_REDRAW_NONE;
    render(ctx, ctx->fb);
    s_settle = false;
    s_area = control_rect(old >= 0 ? old : s_pressed);
    return APP_REDRAW_AREA;
}

static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if (s_usb_entry || usb_storage_active()) return stop_usb_for_navigation(ctx, key == UI_KEY_2);
    if (s_view == TRANSFER_HOME && !stop_upload_if_idle()) return APP_REDRAW_NONE;
    if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    if (s_view == TRANSFER_PASSWORD) { clear_password(); s_view = TRANSFER_NETWORKS; return APP_REDRAW_PAGE; }
    if (s_view == TRANSFER_NETWORKS) {
        if (s_wifi_settings_only || s_direct_entry) { return_from_wifi_setup(ctx, false); return APP_REDRAW_NONE; }
        s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE;
    }
    if (s_view == TRANSFER_HOME) {
        if (s_direct_entry) { ctx->request_return = true; return APP_REDRAW_NONE; }
        s_view = TRANSFER_METHODS; return APP_REDRAW_PAGE;
    }
    ctx->request_return = true;
    return APP_REDRAW_NONE;
}

static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw != APP_REDRAW_AREA) return false;
#if KIIKO_BOARD_METALIO47
    if (s_view == TRANSFER_HOME && s_progress_pending) {
        int err = update_display_area_diff_with(ctx->hl,
            display_main_bw_waveform(), MODE_DU, s_area);
        guard_draw_result(ctx->hl, err);
        if (!err) s_progress_pending = false;
        return true;
    }
#endif
    if (s_view == TRANSFER_PASSWORD) {
        if (s_input_settle) {
            guard_draw_result(ctx->hl, update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_area));
            s_input_settle = false;
            return true;
        }
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
            s_input_layout ? &E0470_WAVEFORM : &E0470_FOLLOW_WAVEFORM,
            s_input_layout ? MODE_GL16 : MODE_DU, s_area));
        return true;
    }
    guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, s_settle ? &E0470_WAVEFORM : &E0470_FOLLOW_WAVEFORM,
                      s_settle ? MODE_GL16 : MODE_DU, s_area));
    s_settle = false;
    return true;
}

static bool transfer_edge_back(app_ctx_t *ctx) {
    (void)ctx;
    return !usb_storage_active() && s_status.state != READ_PICO_TRANSFER_UPLOADING;
}
static bool transfer_navigation_enabled(app_ctx_t *ctx) {
    return !s_cover_wifi_only && transfer_edge_back(ctx);
}
static bool transfer_menu_handle_enabled(app_ctx_t* ctx) {
    (void)ctx;
    return false;
}

static bool transfer_back_swipe_area(app_ctx_t *ctx,const ui_gesture_event_t *event) {
    (void)ctx;
    return s_view!=TRANSFER_PASSWORD || event->y0<UI_SYSTEM_KEYBOARD_TOP;
}

const app_desc_t app_transfer = {
    .binary_ui = true,.persistent_navigation=true,.navigation_enabled=transfer_navigation_enabled,
    .on_media_lost = transfer_on_media_lost,
    .title = "传书 Transfer", .detail = "热点或已有 WiFi，浏览器上传", .enter_full = false,
    .on_enter = on_enter, .on_exit = transfer_on_exit, .render = render, .present = present,
    .on_tick = on_tick, .on_gesture = on_gesture, .on_key = on_key, .owns_keys = true,
    .menu_handle_enabled = transfer_menu_handle_enabled, .edge_back_enabled = transfer_edge_back,
    .back_swipe_area=transfer_back_swipe_area,
};

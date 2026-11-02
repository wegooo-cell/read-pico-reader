/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：异常启动暂用内建字体，避免自动重载故障资源；不覆盖用户字体设置。
 * User revision: recovery starts use the built-in face without retrying faulty resources or overwriting saved font choices.
 * 中文：系统默认使用内建思源黑体子集，TF 卡黑体补足缺字；用户选择的字体覆盖默认外观。
 * English: The built-in Source Han Sans face is the default; the SD face fills missing glyphs and explicit choices override it.
 * 用户修订：票根始终借用系统字体，浅睡解锁恢复锁屏前的实际字体和上下文，不覆盖保存的选择。
 * User revision: tickets always borrow the system face; light-sleep wake restores the actual previous face/context without altering saved choices.
 */
#include "app_font_context.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "boot_state.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_nav.h"
#if KIIKO_BOARD_METALIO47
#include "esp_timer.h"
#include "esp_log.h"
#include "read_pico_sd.h"
#include "sleep.h"
#endif

static bool s_reading;
// 失败字体按退避间隔重试；保持用户选择，避免反复卸载当前槽。/ Back off failed faces without changing the saved choice or repeatedly unloading the active slot.
static char s_rejected[TTF_FONT_PATH_MAX];
static time_t s_rejected_mtime;
static off_t s_rejected_size;
#if KIIKO_BOARD_METALIO47
static int64_t s_rejected_retry_us;
static unsigned s_rejected_attempts;
#endif
static bool font_file(const char *path, struct stat *st) {
    return path && stat(path, st) == 0 && S_ISREG(st->st_mode) && st->st_size > 0;
}

static const char* system_path(char fallback[TTF_FONT_PATH_MAX]) {
    if (pico_boot_recovery()) return TTF_FONT_BUILTIN;
#if KIIKO_BOARD_METALIO47
    if(app_sleep_media_pending() || !read_pico_sd_io_ready())return ttf_font_path();
#endif
    const char* chosen = app_settings_system_font_path();
    if (chosen[0]) return chosen;
    if (snprintf(fallback, TTF_FONT_PATH_MAX, "%s/Hei.ttf", app_settings_fonts_dir()) >= TTF_FONT_PATH_MAX)
        return TTF_FONT_BUILTIN;
    struct stat st;
    if (stat(fallback, &st) == 0 && S_ISREG(st.st_mode)) return fallback;
    if (!ttf_font_count()) ttf_font_scan();
    const ttf_font_item_t *item = ttf_font_item(0);
    return item && font_file(item->path, &st) ? item->path : TTF_FONT_BUILTIN;
}

static bool activate(const char* path) {
#if KIIKO_BOARD_METALIO47
    // 有意暂停TF时不stat/卸载旧槽；挂载恢复后沿原选择重试。
    // Do not stat/unload retained faces while media is intentionally paused; retry the saved choice after mount.
    if(!ttf_font_path_is_builtin(path) && (app_sleep_media_pending() || !read_pico_sd_io_ready()))return false;
    const bool had_builtin=ttf_font_ready()&&ttf_font_is_builtin();
#endif
    struct stat st = {0};
    bool builtin = ttf_font_path_is_builtin(path);
    bool valid = builtin || font_file(path, &st);
    bool rejected = !builtin && !strcmp(s_rejected, path) &&
        s_rejected_mtime == st.st_mtime && s_rejected_size == st.st_size;
#if KIIKO_BOARD_METALIO47
    // 唤醒时的解析错误也可能是短暂状态；所有失败都有限速的下一次机会。
    // Wake-time parse failures may be transient too; every failure gets a rate-limited retry.
    if (valid && ttf_font_ready() && !strcmp(ttf_font_path(), path)) {
        if (!builtin) { s_rejected[0]=0; s_rejected_attempts=0; }
        return false;
    }
    if (rejected && s_rejected_retry_us && esp_timer_get_time() >= s_rejected_retry_us)
        rejected=false;
#endif
    if (!valid || rejected) path = TTF_FONT_BUILTIN;
    if (ttf_font_ready() && !strcmp(ttf_font_path(), path)) return false;
#if KIIKO_BOARD_METALIO47
    esp_err_t err=ttf_font_open(path);
    if (err == ESP_OK) {
#else
    if (ttf_font_open(path) == ESP_OK) {
#endif
        if (!ttf_font_path_is_builtin(path)) s_rejected[0] = 0;
#if KIIKO_BOARD_METALIO47
        if (!ttf_font_path_is_builtin(path)) s_rejected_attempts=0;
#endif
        return true;
    }
    if (!ttf_font_path_is_builtin(path)) {
#if KIIKO_BOARD_METALIO47
        if (strcmp(s_rejected,path) || s_rejected_mtime!=st.st_mtime || s_rejected_size!=st.st_size)
            s_rejected_attempts=0;
        static const unsigned retry_ms[]={1000,3000,10000,30000,300000};
        unsigned index=s_rejected_attempts<4?s_rejected_attempts:4;
        if(s_rejected_attempts<5)++s_rejected_attempts;
        s_rejected_retry_us=esp_timer_get_time()+(int64_t)retry_ms[index]*1000;
        ESP_LOGW("font_context","font load failed: %s err=%s retry_ms=%u",path,esp_err_to_name(err),retry_ms[index]);
#endif
        snprintf(s_rejected, sizeof(s_rejected), "%s", path);
        s_rejected_mtime = st.st_mtime;
        s_rejected_size = st.st_size;
    }
#if KIIKO_BOARD_METALIO47
    if (ttf_font_ready() && ttf_font_is_builtin()) return !had_builtin;
#else
    if (ttf_font_ready() && ttf_font_is_builtin()) return false;
#endif
    return ttf_font_open_builtin() == ESP_OK;
}

bool app_font_activate_system(void) {
    s_reading = false;
    ttf_set_stroke_adjust(0);
    ttf_set_antialias(true);
    const char* chosen = app_settings_system_font_path();
    char fallback[TTF_FONT_PATH_MAX];
    const char* path = system_path(fallback);
    bool changed = activate(path);
    // 未选字体时由内建思源黑体绘制界面；TTF 仍保留完整字库供文件名缺字回退。
    // Built-in Source Han Sans draws default UI; the full TTF remains loaded for missing glyphs.
    ui_text_set_system_font(!pico_boot_recovery() && chosen[0] && ttf_font_ready() &&
                           !strcmp(chosen, ttf_font_path()) && !ttf_font_is_builtin());
    ui_text_set_system_scale(true);
    ui_nav_status_font_context(false, changed);
    return changed;
}

bool app_font_activate_reading(void) {
    s_reading = true;
    ttf_set_stroke_adjust_fine(app_settings_book_weight_fine());
    ttf_set_antialias(app_settings_book_antialias());
    const char* saved = pico_boot_recovery() ? TTF_FONT_BUILTIN : app_settings_font_path();
    bool changed = activate(ttf_font_path_is_builtin(saved) ? TTF_FONT_BUILTIN : saved);
    ui_text_set_system_font(false);
    ui_text_set_system_scale(false);
    ui_nav_status_font_context(true, changed);
    return changed;
}

bool app_font_retry_active(void) {
    return s_reading ? app_font_activate_reading() : app_font_activate_system();
}
#if KIIKO_BOARD_METALIO47
bool app_font_recover_media(void) {
    s_rejected[0] = 0;
    s_rejected_attempts = 0;
    s_rejected_retry_us = 0;
    return app_font_retry_active();
}
#endif

void app_font_begin_lock(app_lock_font_t *saved) {
    if (!saved) return;
    snprintf(saved->path, sizeof(saved->path), "%s", ttf_font_path());
    saved->reading = s_reading;
    app_font_activate_system();
}

void app_font_end_lock(const app_lock_font_t *saved) {
    if (!saved) return;
#if KIIKO_BOARD_METALIO47
#else
    (void)activate(saved->path);
#endif
    s_reading = saved->reading;
#if KIIKO_BOARD_METALIO47
    const char *restore=saved->path;
    // 暂时退回内建不能覆盖阅读意图；锁屏前未加载成功的TF字体在解锁时再尝试。
    // A temporary built-in fallback does not replace reader intent; retry the selected TF face on unlock.
    if (pico_boot_recovery()) restore=TTF_FONT_BUILTIN;
    else if(s_reading && ttf_font_path_is_builtin(restore)) restore=app_settings_font_path();
    if(ttf_font_path_is_builtin(restore)) restore=TTF_FONT_BUILTIN;
    // activate的false也表示“暂缓/未改变”，不能据此卸载仍待恢复的字体。
    // False also means deferred/unchanged; it must not discard a face still awaiting IO restoration.
    if (!app_sleep_media_pending() &&
        (ttf_font_path_is_builtin(restore)||read_pico_sd_io_ready())) (void)activate(restore);
#else
#endif
    ttf_set_stroke_adjust_fine(s_reading ? app_settings_book_weight_fine() : 0);
    ttf_set_antialias(!s_reading || app_settings_book_antialias());
    const char *chosen = app_settings_system_font_path();
    ui_text_set_system_font(!s_reading && !pico_boot_recovery() && chosen[0] &&
                           !strcmp(chosen, ttf_font_path()) && !ttf_font_is_builtin());
    ui_text_set_system_scale(!s_reading);
    ui_nav_status_font_context(s_reading, false);
}

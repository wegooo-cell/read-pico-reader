#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：验证 Pico 刷屏期间底栏点击缓冲/预排队状态机与主循环调度边界。
English: Verify Pico refresh-window bottom-nav tap buffering state machine and main-loop scheduling bounds."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP_LOOP = ROOT / 'boards/pico/src/main/app/app_loop.c'
if not APP_LOOP.is_file():
    APP_LOOP = ROOT / 'main/app/app_loop.c'

HARNESS_C = r'''/* SPDX-License-Identifier: Apache-2.0
 * 中文：用真实 Pico app_loop.c 在刷屏桩回调内注入触摸，验证底栏预排队全部边界。
 * English: Inject touches inside display stubs against real Pico app_loop.c to verify nav tap buffering bounds.
 */
#include "app_loop.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "boot_state.h"
#include "read_pico_transfer.h"
#include "book/book_weread_cover.h"
#include "book/book_xteink_sync.h"
#include "ble_page_turner.h"
#include "ui_nav_layout.h"
#include "online_font.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool app_loop_test_nav_armed(void);
extern bool app_loop_test_nav_feed(const cst836u_touch_t *sample, int64_t now_ms);
extern bool app_poll_main_navigation(app_ctx_t *ctx);

/* ---- 测试桩与可编程剧本 / Test stubs and scripted frame sequences ---- */
static jmp_buf g_done;
static uint8_t g_fb[128];
static int g_step, g_max_steps = 16;
static int64_t g_now_us, g_time_step_us = 2000;

typedef struct { int x, y, count, err; } touch_sample_t;
static touch_sample_t g_loop_samples[32];
static int g_loop_sample_count;

typedef void (*display_hook_fn)(void);
static display_hook_fn g_on_page_present;
static display_hook_fn g_on_click_present;
static display_hook_fn g_on_power_dialog;
static display_hook_fn g_on_quick_present;
static display_hook_fn g_on_menu_present;

static int g_nav_req_log[16], g_nav_req_count, g_nav_phase;
static int g_nav_finish_calls, g_nav_press_draws, g_delay_calls, g_click_presents;
static int g_enters[6], g_exits[6], g_fulls, g_pages, g_cst_reads;
static bool g_nav_pending, g_click_active_flag, g_main_ready[4], g_usb_active;
static bool g_reader_active, g_power_due, g_menu_on_tick, g_req_tab1_on_tick, g_live_tab3_after_enter1;
static bool g_last_armed_on_power, g_last_armed_on_quick, g_last_armed_on_menu;
static bool g_last_armed_on_reader, g_last_armed_on_usb;
static bool g_power_checked, g_quick_checked, g_menu_checked, g_reader_checked, g_usb_checked;
static bool g_poll_nav_ok;
bool test_reader_open = false;
bool test_usb_storage_active(void) { return g_usb_active; }

static app_desc_t g_roots[4];
static app_desc_t g_reader;
const app_desc_t app_book = {.title = "book"};
const app_desc_t app_transfer = {.title = "transfer"};

static int tab_x(int tab) { return tab * 171 + 80; }
static touch_sample_t nav_pt(int tab) { return (touch_sample_t){tab_x(tab), UI_NAV_TOUCH_TOP + 20, 1, 0}; }
static touch_sample_t up_pt(void) { return (touch_sample_t){0, 0, 0, 0}; }
static void inject_nav(int tab, int64_t advance_ms) {
    g_now_us += advance_ms * 1000;
    cst836u_touch_t s = {.touched = true, .count = 1, .x = (uint16_t)tab_x(tab), .y = UI_NAV_TOUCH_TOP + 20};
    (void)app_loop_test_nav_feed(&s, g_now_us / 1000);
}
static void inject_up(int64_t advance_ms) {
    g_now_us += advance_ms * 1000;
    cst836u_touch_t s = {.touched = false, .count = 0};
    (void)app_loop_test_nav_feed(&s, g_now_us / 1000);
}
static void inject_body_down(int64_t advance_ms) {
    g_now_us += advance_ms * 1000;
    cst836u_touch_t s = {.touched = true, .count = 1, .x = 200, .y = UI_NAV_TOUCH_TOP - 40};
    (void)app_loop_test_nav_feed(&s, g_now_us / 1000);
}
static void inject_two_finger(int tab, int64_t advance_ms) {
    g_now_us += advance_ms * 1000;
    cst836u_touch_t s = {.touched = true, .count = 2, .x = (uint16_t)tab_x(tab), .y = UI_NAV_TOUCH_TOP + 20};
    (void)app_loop_test_nav_feed(&s, g_now_us / 1000);
}

static bool root_main_visible(app_ctx_t *ctx) {
    (void)ctx;
    for (int i = 0; i < 4; ++i) {
        (void)i;
    }
    return true;
}
static bool root0_visible(app_ctx_t *c) { (void)c; return g_main_ready[0]; }
static bool root1_visible(app_ctx_t *c) { (void)c; return g_main_ready[1]; }
static bool root2_visible(app_ctx_t *c) { (void)c; return g_main_ready[2]; }
static bool root3_visible(app_ctx_t *c) { (void)c; return g_main_ready[3]; }

static void root_render(app_ctx_t *c, uint8_t *f) { (void)c; (void)f; }
static void root_enter_0(app_ctx_t *c) { (void)c; ++g_enters[0]; }
static void root_enter_1(app_ctx_t *c) { (void)c; ++g_enters[1]; }
static void root_enter_2(app_ctx_t *c) { (void)c; ++g_enters[2]; }
static void root_enter_3(app_ctx_t *c) { (void)c; ++g_enters[3]; }
static void reader_enter(app_ctx_t *c) { (void)c; ++g_enters[4]; }
static void root_exit_0(app_ctx_t *c) { (void)c; ++g_exits[0]; }
static void root_exit_1(app_ctx_t *c) { (void)c; ++g_exits[1]; }
static void root_exit_2(app_ctx_t *c) { (void)c; ++g_exits[2]; }
static void root_exit_3(app_ctx_t *c) { (void)c; ++g_exits[3]; }
static void reader_exit(app_ctx_t *c) { (void)c; ++g_exits[4]; }
static bool reader_nav_enabled(app_ctx_t *c) { (void)c; return !g_reader_active; }
static bool reader_nav_visible(app_ctx_t *c) { (void)c; return !g_reader_active; }

void ui_nav_request(app_ctx_t *ctx, int index);

static app_redraw_t quick_open_tick(app_ctx_t *ctx) {
    (void)ctx;
    if (g_req_tab1_on_tick) {
        g_req_tab1_on_tick = false;
        ui_nav_request(ctx, 1);
    }
    if (g_menu_on_tick && g_step == 1) {
        g_menu_on_tick = false;
        ctx->request_menu = true;
    }
    return APP_REDRAW_NONE;
}

static void reset_env(void) {
    g_step = 0;
    g_max_steps = 16;
    g_now_us = 1000000;
    g_time_step_us = 2000;
    g_loop_sample_count = 0;
    memset(g_loop_samples, 0, sizeof(g_loop_samples));
    g_on_page_present = NULL;
    g_on_click_present = NULL;
    g_on_power_dialog = NULL;
    g_on_quick_present = NULL;
    g_on_menu_present = NULL;
    memset(g_nav_req_log, 0, sizeof(g_nav_req_log));
    g_nav_req_count = g_nav_phase = 0;
    g_nav_finish_calls = g_nav_press_draws = g_delay_calls = g_click_presents = g_cst_reads = 0;
    memset(g_enters, 0, sizeof(g_enters));
    memset(g_exits, 0, sizeof(g_exits));
    g_fulls = g_pages = 0;
    g_nav_pending = false;
    g_click_active_flag = false;
    for (int i = 0; i < 4; ++i) g_main_ready[i] = true;
    g_usb_active = false;
    g_reader_active = false;
    g_power_due = false;
    g_menu_on_tick = false;
    g_req_tab1_on_tick = false;
    g_live_tab3_after_enter1 = false;
    g_last_armed_on_power = g_last_armed_on_quick = g_last_armed_on_menu = false;
    g_last_armed_on_reader = g_last_armed_on_usb = false;
    g_power_checked = g_quick_checked = g_menu_checked = g_reader_checked = g_usb_checked = false;
    g_poll_nav_ok = false;

    memset(g_roots, 0, sizeof(g_roots));
    g_roots[0] = (app_desc_t){.title="home", .persistent_navigation=true, .main_page_visible=root0_visible, .render=root_render, .on_enter=root_enter_0, .on_exit=root_exit_0, .on_tick=quick_open_tick};
    g_roots[1] = (app_desc_t){.title="shelf", .persistent_navigation=true, .main_page_visible=root1_visible, .render=root_render, .on_enter=root_enter_1, .on_exit=root_exit_1};
    g_roots[2] = (app_desc_t){.title="files", .persistent_navigation=true, .main_page_visible=root2_visible, .render=root_render, .on_enter=root_enter_2, .on_exit=root_exit_2};
    g_roots[3] = (app_desc_t){.title="settings", .persistent_navigation=true, .main_page_visible=root3_visible, .render=root_render, .on_enter=root_enter_3, .on_exit=root_exit_3};
    g_reader = (app_desc_t){.title="reader", .persistent_navigation=false, .navigation_enabled=reader_nav_enabled, .navigation_visible=reader_nav_visible, .render=root_render, .on_enter=reader_enter, .on_exit=reader_exit};
    (void)root_main_visible;
}

static void add_sample(touch_sample_t s) {
    assert(g_loop_sample_count < 32);
    g_loop_samples[g_loop_sample_count++] = s;
}

static void run_loop(const app_desc_t *start_app) {
    app_loop_config_t cfg = {
        .hl = (EpdiyHighlevelState *)1,
        .fb = g_fb,
        .tp = (cst836u_handle_t)1,
        .first_app = start_app ? start_app : &g_roots[0],
    };
    if (!setjmp(g_done)) app_loop_run(&cfg);
}

/* ---- 必需桩函数 / Required link stubs ---- */
const char *esp_err_to_name(int err) { (void)err; return "ERR"; }
int E0470_WAVEFORM, E0470_FOLLOW_WAVEFORM;
bool continuous_du_init(void) { return true; }
int64_t esp_timer_get_time(void) { return g_now_us; }
void vTaskDelay(int t) {
    (void)t;
    ++g_delay_calls;
    g_now_us += g_time_step_us;
    if (++g_step >= g_max_steps) longjmp(g_done, 1);
}
int cst836u_read(void *tp, cst836u_touch_t *out) {
    (void)tp;
    ++g_cst_reads;
    touch_sample_t s = g_step < g_loop_sample_count ? g_loop_samples[g_step] : up_pt();
    if (g_live_tab3_after_enter1 && g_enters[1] == 1) {
        s = nav_pt(3);
        g_live_tab3_after_enter1 = false;
    }
    if (s.err) return s.err;
    memset(out, 0, sizeof(*out));
    out->count = (uint8_t)s.count;
    out->x = (uint16_t)s.x;
    out->y = (uint16_t)s.y;
    out->touched = s.count > 0;
    return ESP_OK;
}
int cst836u_get_info(void *tp, cst836u_info_t *info) { (void)tp; memset(info, 0, sizeof(*info)); return 0; }

const app_desc_t *app_home_page(void) { return &g_roots[0]; }
const app_desc_t *app_at(int i) { return (i >= 0 && i < 4) ? &g_roots[i] : NULL; }
int app_index_of(const app_desc_t *a) {
    for (int i = 0; i < 4; ++i) if (a == &g_roots[i]) return i;
    return -1;
}
int ui_nav_hit(uint16_t x, uint16_t y) { return (y >= UI_NAV_TOUCH_TOP && y < 1216 && x < 684) ? x / 171 : -1; }
void ui_nav_request(app_ctx_t *ctx, int index) {
    if (index >= 0 && index < 4) {
        ctx->request_app = &g_roots[index];
        assert(g_nav_req_count < 16);
        g_nav_req_log[g_nav_req_count++] = index;
    }
}

void ui_click_feedback_begin(uint8_t *fb) { (void)fb; }
void ui_click_feedback_reset(void) { g_nav_pending = false; g_click_active_flag = false; g_nav_phase = 0; }
bool ui_click_feedback_active(void) { return g_click_active_flag; }
bool ui_click_feedback_nav_pending(void) { return g_nav_pending; }
void ui_click_feedback_nav_request(int tab) { (void)tab; g_nav_pending = true; g_nav_phase = 0; }
bool ui_click_feedback_nav_tick(uint8_t *fb, int64_t now_ms, EpdRect *area) {
    (void)fb; (void)now_ms;
    *area = (EpdRect){10, UI_NAV_TOP + 15, 60, 60};
    if (g_nav_phase == 0) {
        g_nav_phase = 1;
        g_click_active_flag = true;
        ++g_nav_press_draws;
        return true;
    }
    ++g_nav_finish_calls;
    g_nav_pending = false;
    g_click_active_flag = false;
    g_nav_phase = 0;
    return true;
}
bool ui_click_feedback_nav_finish(uint8_t *fb, EpdRect *area) {
    (void)fb;
    if (!g_nav_pending) return false;
    ++g_nav_finish_calls;
    g_nav_pending = false;
    g_click_active_flag = false;
    *area = (EpdRect){10, UI_NAV_TOP + 15, 60, 60};
    return true;
}
bool ui_click_feedback_press(uint8_t *fb, int x, int y, EpdRect *area) { (void)fb;(void)x;(void)y;(void)area; return false; }
bool ui_click_feedback_release(uint8_t *fb, EpdRect *area) { (void)fb; *area=(EpdRect){0}; g_nav_pending=false; g_click_active_flag=false; return false; }
bool ui_click_feedback_cancel_at(int x, int y) { (void)x;(void)y; return false; }

void guard_draw_result(EpdiyHighlevelState *hl, enum EpdDrawError err) { (void)hl;(void)err; }
enum EpdDrawError update_display_full(EpdiyHighlevelState *hl) {
    (void)hl; ++g_fulls;
    if (g_reader_active) { g_reader_checked = true; g_last_armed_on_reader = app_loop_test_nav_armed(); }
    if (g_usb_active) { g_usb_checked = true; g_last_armed_on_usb = app_loop_test_nav_armed(); }
    return EPD_DRAW_SUCCESS;
}
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState *hl) {
    (void)hl; ++g_pages;
    if (g_reader_active) { g_reader_checked = true; g_last_armed_on_reader = app_loop_test_nav_armed(); }
    if (g_usb_active) { g_usb_checked = true; g_last_armed_on_usb = app_loop_test_nav_armed(); }
    if (g_on_page_present) g_on_page_present();
    return EPD_DRAW_SUCCESS;
}
enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState *hl, EpdRect area) {
    (void)hl; (void)area; ++g_click_presents;
    if (g_on_click_present) g_on_click_present();
    return EPD_DRAW_SUCCESS;
}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState *h, const void *w, int m, EpdRect a) {
    (void)h; (void)w; (void)m;
    if (a.width == 684 && a.height == 1216) { g_power_checked = true; g_last_armed_on_power = app_loop_test_nav_armed(); }
    if (g_on_power_dialog) g_on_power_dialog();
    return EPD_DRAW_SUCCESS;
}
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState *h, const void *w, int m, EpdRect a) { (void)h;(void)w;(void)m;(void)a; return EPD_DRAW_SUCCESS; }
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState *h, const void *w, int m, EpdRect a) { (void)h;(void)w;(void)m;(void)a; return EPD_DRAW_SUCCESS; }
enum EpdDrawError update_display_mode(EpdiyHighlevelState *h, int m) {
    (void)h;(void)m;
    if (g_usb_active) { g_usb_checked = true; g_last_armed_on_usb = app_loop_test_nav_armed(); }
    if (g_reader_active) { g_reader_checked = true; g_last_armed_on_reader = app_loop_test_nav_armed(); }
    return EPD_DRAW_SUCCESS;
}
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState *h, int m) { (void)h;(void)m; return EPD_DRAW_SUCCESS; }
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState *h, EpdRect a, int d) { (void)h;(void)a;(void)d; return EPD_DRAW_SUCCESS; }
enum EpdDrawError update_display_main_turn(EpdiyHighlevelState *h, EpdRect a, int d) { (void)h;(void)a;(void)d; return EPD_DRAW_SUCCESS; }
enum EpdDrawError update_display_white(EpdiyHighlevelState *h) { (void)h; return EPD_DRAW_SUCCESS; }
EpdRect epd_full_screen(void) { return (EpdRect){0, 0, 1216, 684}; }
int epd_rotated_display_width(void) { return 684; }
int epd_rotated_display_height(void) { return 1216; }
void epd_fill_triangle(int a,int b,int c,int d,int e,int f,uint8_t g,uint8_t *h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
bool ui_nav_refresh_power(uint8_t *fb) { (void)fb; return false; }
EpdRect ui_nav_status_area(void) { return (EpdRect){0, 12, 684, 56}; }
bool display_take_white_exit(void) { return false; }
void display_main_transition_arm(const void *a, bool b, bool c) { (void)a;(void)b;(void)c; }
void display_main_transition_disarm(void) {}
void display_main_transition_cancel(void) {}
unsigned display_main_quality_set(unsigned p) { (void)p; return 0; }
void display_request_full_refresh(void) {}
const void *display_main_gray_waveform(void) { return &E0470_WAVEFORM; }
int app_settings_main_refresh_mode(void) { return APP_MAIN_REFRESH_NORMAL; }
bool app_settings_main_fast_refresh(void) { return false; }
uint8_t app_settings_system_contrast(void) { return 100; }
uint8_t app_settings_auto_lock_minutes(void) { return 0; }
bool app_settings_ble_turner(void) { return false; }
void app_settings_set_ble_turner(bool e) { (void)e; }
bool app_settings_reader_power_turn(void) { return false; }
bool app_settings_staged_shutdown(void) { return false; }
const char *app_settings_font_path(void) { return ""; }
bool app_setup_pending(void) { return false; }
void rails_idle_check(int64_t t) { (void)t; }
void epd_poweroff(void) {}
bool read_pico_pmu_ready(void) { return g_power_due; }
void read_pico_pmu_drain_events(void) {}
read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void) {
    if (g_power_due && g_step >= 1) { g_power_due = false; return READ_PICO_PMU_KEY_LONG; }
    return READ_PICO_PMU_KEY_NONE;
}
void enter_lock_and_sleep(EpdiyHighlevelState *h, int64_t *i, void *a, void *t, bool r) { (void)h;(void)i;(void)a;(void)t;(void)r; }
void app_lock_feedback(EpdiyHighlevelState *h) { (void)h; }
void app_lock_wait_key_idle(int m) { (void)m; }
esp_err_t app_enter_host_sleep(app_sleep_mode_t m) { (void)m; return ESP_OK; }
void app_restart_host(void) {}
void pico_boot_ready(void) {}
esp_err_t pico_boot_save_resume(const pico_resume_t *r) { (void)r; return ESP_OK; }
void pico_boot_clear_resume(void) {}
void app_book_cover_mode_changed(void) {}
void app_home_cover_mode_changed(void) {}
void app_book_shelf_prefetch_first(app_ctx_t *c) { (void)c; }
void app_book_shelf_prefetch_cancel(void) {}
void app_book_shelf_prefetch_reset(void) {}
void app_book_ticket_sample(int64_t n, bool e) { (void)n;(void)e; }
bool app_book_reader_body_visible(void) { return g_reader_active; }
bool app_book_status_bar_visible(void) { return !g_reader_active; }
bool app_book_checkpoint_ready(void) { return true; }
bool app_book_resume_context(char *p, size_t c, bool *f) { (void)p;(void)c;(void)f; return false; }
void app_book_pause_auto(void) {}
void app_lock_set_resume_ready(bool r) { (void)r; }
void ui_text_set_system_scale(bool s) { (void)s; }
void ui_text_set_system_font(bool s) { (void)s; }
void app_font_activate_system(void) {}
bool ttf_font_path_is_builtin(const char *p) { (void)p; return true; }
bool ttf_font_ready(void) { return true; }
bool ttf_font_is_builtin(void) { return true; }
const char *ttf_font_path(void) { return ""; }
int ttf_font_open(const char *p) { (void)p; return 0; }
int ttf_font_open_builtin(void) { return 0; }
int read_pico_sd_get_info(read_pico_sd_info_t *i) { *i = (read_pico_sd_info_t){true, true}; return 0; }
int read_pico_sd_start_probe(void) { return 0; }
void read_pico_sd_poll_health(void) {}
bool read_pico_sd_needs_recovery(void) { return false; }
int read_pico_sd_start_recovery(void) { return 0; }
void read_pico_transfer_idle_poll(bool u) { (void)u; }
void read_pico_transfer_get_status(read_pico_transfer_status_t *s) { memset(s, 0, sizeof(*s)); }
bool read_pico_transfer_try_stop_if_idle(void) { return true; }
int read_pico_transfer_get_saved_wifi(char *s, bool *c) { (void)s; *c = false; return 0; }
int read_pico_transfer_start(const read_pico_transfer_cfg_t *c) { (void)c; return 0; }
void read_pico_transfer_resume_after_sleep(void) {}
bool pico_online_busy(void) { return false; }
void ble_pt_sync(bool e) { (void)e; }
void ble_pt_poll(void) {}
bool ble_pt_take_standby(void) { return false; }
bool ble_pt_connected(void) { return false; }
uint32_t ble_pt_input_serial(void) { return 0; }
void ble_pt_reconnect_recent(void) {}
void ble_pt_cancel_recent(void) {}
bool ble_pt_pop_key(ble_pt_event_t *e) { (void)e; return false; }
bool ble_pt_pop_raw(ble_pt_raw_t *r) { (void)r; return false; }
bool app_usb_storage_poll(app_ctx_t *c, bool p) { (void)c;(void)p; return false; }
void crash_note_log(void) {}
bool app_time_service_online_sync_pending(void) { return false; }
void app_time_service_poll(bool i) { (void)i; }
void ui_avatar_invalidate(void) {}
bool book_cover_jobs_quiesce(unsigned t) { (void)t; return true; }
bool ui_avatar_quiesce(unsigned t) { (void)t; return true; }
void book_lock_collage_invalidate(void) {}
void online_font_status(online_font_status_t *s) { memset(s, 0, sizeof(*s)); }
book_weread_cover_status_t book_weread_cover_status(void) { return (book_weread_cover_status_t){0}; }
bool xteink_sync_busy(void) { return false; }
void ui_draw_menu_page(uint8_t *fb, const app_desc_t *a, int l) {
    (void)fb;(void)a;(void)l;
    g_menu_checked = true;
    g_last_armed_on_menu = app_loop_test_nav_armed();
    if (g_on_menu_present) g_on_menu_present();
}
void ui_draw_menu_row_pressed(uint8_t *f, const app_desc_t *a, int l, int r, bool p) {(void)f;(void)a;(void)l;(void)r;(void)p;}
int ui_menu_leaf_for_app(const app_desc_t *a) { (void)a; return 0; }
int ui_menu_leaf_count(void) { return 1; }
int ui_menu_hit_test(uint16_t x, uint16_t y, int l) { (void)x;(void)y;(void)l; return UI_MENU_HIT_NONE; }
bool ui_menu_row_rect(int l, int r, EpdRect *a) { (void)l;(void)r;*a=(EpdRect){0}; return false; }
int ui_key_hit_test(uint16_t x, uint16_t y) { (void)x;(void)y; return -1; }
bool ui_menu_handle_hit_test(uint16_t x, uint16_t y) { (void)x;(void)y; return false; }
void ui_quick_menu_end(void) {}
void ui_quick_menu_draw(uint8_t *f, bool wifi, bool bluetooth, bool soften_text) {
    (void)f;(void)wifi;(void)bluetooth;(void)soften_text;
    g_quick_checked = true;
    g_last_armed_on_quick = app_loop_test_nav_armed();
    if (g_on_quick_present) g_on_quick_present();
}
void ui_quick_menu_draw_status(uint8_t *f) { (void)f; }
bool ui_quick_menu_draw_button(uint8_t *f, int i, bool a, bool p, EpdRect *d) { (void)f;(void)i;(void)a;(void)p;*d=(EpdRect){0}; return false; }
bool ui_quick_menu_restore(uint8_t *fb) { (void)fb; return true; }
EpdRect ui_content_refresh_area(void) { return (EpdRect){0, 76, 684, 1012}; }
EpdRect ui_power_dialog_rect(void) { return (EpdRect){0, 0, 684, 1216}; }
void ui_power_dialog_draw(uint8_t *f, const ui_power_dialog_t *d) { (void)f;(void)d; }
void ui_power_dialog_draw_controls(uint8_t *f, const ui_power_dialog_t *d, EpdRect a) { (void)f;(void)d;(void)a; }
void ui_power_dialog_end(void) {}
void ui_power_final_draw(uint8_t *f, bool r) { (void)f;(void)r; }

/* ---- 测试回调与场景 / Test callbacks & scenarios ---- */
static void hook_short_tap_tab2(void) {
    assert(app_loop_test_nav_armed());
    if (g_enters[1] == 1 && g_enters[2] == 0 && g_enters[3] == 0) {
        inject_up(5);
        inject_nav(2, 10);
        inject_up(30);
    }
}

static void hook_wait_release_then_tab3(void) {
    assert(app_loop_test_nav_armed());
    if (g_enters[1] == 1 && g_enters[3] == 0) {
        // 仍按住旧指（tab 2 位置），必须被 wait_release 忽略；抬起再点 tab 3 才捕获。
        // Held finger from pre-refresh contact must be ignored by wait_release; new tap after release captures.
        inject_nav(2, 5);
        inject_up(20);
        inject_nav(3, 15);
        inject_up(20);
    }
}

static void hook_last_wins_tab2_then_tab3(void) {
    if (g_enters[1] == 1 && g_enters[3] == 0) {
        inject_up(5);
        inject_nav(2, 10);
        inject_up(15);
        inject_nav(3, 15);
        inject_up(15);
    }
}

static void hook_same_tab_impatient(void) {
    if (g_enters[1] == 1) {
        inject_up(5);
        inject_nav(1, 10);
        inject_up(20);
    }
}

static void hook_held_across_end_tab2(void) {
    if (g_enters[1] == 1 && g_enters[2] == 0) {
        inject_up(5);
        inject_nav(2, 10);
    }
}

static void hook_two_finger_and_drag_in(void) {
    if (g_enters[1] == 1) {
        inject_up(5);
        inject_two_finger(2, 10);
        inject_up(10);
        inject_body_down(10);
        inject_nav(2, 15);
        inject_up(15);
    }
}

static void hook_stale_ttl_tab2(void) {
    if (g_enters[1] == 1 && g_enters[2] == 0) {
        inject_up(5);
        inject_nav(2, 10);
        inject_up(20);
        g_now_us += 1300000; // 1.3s > NAV_TAP_QUEUE_TTL_MS (1200ms)
    }
}

static void hook_click_override_to_tab3(void) {
    if (g_click_presents == 1 && g_enters[3] == 0) {
        assert(app_loop_test_nav_armed());
        inject_up(5);
        inject_nav(3, 20);
        inject_up(40);
    }
}

static void hook_overlay_tries_inject(void) {
    // 若在非导航覆盖层期间未解除/阻止，则尝试注入会失败 / Injecting when disarmed returns false
    assert(!app_loop_test_nav_armed());
    cst836u_touch_t s = {.touched = true, .count = 1, .x = (uint16_t)tab_x(2), .y = UI_NAV_TOUCH_TOP + 20};
    assert(!app_loop_test_nav_feed(&s, g_now_us / 1000 + 10));
}

static void hook_poll_main_nav_while_armed(void) {
    if (g_enters[1] == 1 && !g_poll_nav_ok) {
        assert(app_loop_test_nav_armed());
        inject_up(5);
        inject_nav(2, 5);
        app_ctx_t scan = {.tp = (cst836u_handle_t)1};
        assert(app_poll_main_navigation(&scan) && scan.request_app == &g_roots[2] && scan.user_activity);
        g_poll_nav_ok = true;
    }
}

int main(void) {
    // 1. 刷屏期间短按 tab 2（按下并抬起）：下一拍自动切到 tab 2 且跳过旧图标单独缩回扫描与空闲延时。
    // 1. Short tap on tab 2 during switch_to refresh adopts tab 2 next tick and skips extra shrink DU & delay.
    reset_env();
    g_on_page_present = hook_short_tap_tab2;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[2] == 1);
    assert(g_nav_req_count == 2 && g_nav_req_log[0] == 1 && g_nav_req_log[1] == 2);
    // 切换 1->2 时跳过 tab 1 的 nav_finish 与空闲延时（4 次延时对应 5 轮循环），仅最后 tab 2 静止后调用 1 次 nav_finish。
    // Skipped tab 1 nav_finish and idle delay (5 iterations across 4 vTaskDelays); only tab 2 settles once.
    assert(g_nav_finish_calls == 1 && g_nav_press_draws == 2);
    assert(g_delay_calls == 4 && g_cst_reads == 15);
    assert(!app_loop_test_nav_armed());

    // 2. 初始按下持续到刷新开始时必须忽略原手势；直到抬手后再点 tab 3 才捕获。
    // 2. Finger still down entering refresh triggers wait_release; new press after release captures tab 3.
    reset_env();
    g_on_page_present = hook_wait_release_then_tab3;
    g_max_steps = 5;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(nav_pt(1)); // 仍在按住中 / Still held when loop exits switch_to if wait_release hadn't seen up
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[3] == 1 && g_enters[2] == 0);

    // 3. 同一刷新窗口连续点 tab 2 再点 tab 3：最后一次覆盖胜出（直接切到 3，绝不经过 2）。
    // 3. Multiple mid-refresh taps (tab 2 then tab 3): last-wins switches straight to tab 3.
    reset_env();
    g_on_page_present = hook_last_wins_tab2_then_tab3;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[3] == 1 && g_enters[2] == 0);

    // 4. 在根页上焦急连点同一 tab（tab 1）丢弃；若处于子页（main_page_ready==false）则允许回到根页。
    // 4. Same-tab tap dropped when root ready, honored when subpage is active (main_page_ready==false).
    reset_env();
    g_on_page_present = hook_same_tab_impatient;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_nav_req_count == 1);

    reset_env();
    g_main_ready[1] = false; // 子页可见底栏 / Subpage with persistent footer
    g_on_page_present = hook_same_tab_impatient;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_nav_req_count == 2 && g_nav_req_log[1] == 1);

    // 5. 手指按住 tab 2 跨越刷新结束：仅消费 1 次，s_nav_contact_down 阻断松手前重复触发。
    // 5. Finger stays down on tab 2 across refresh end: fires once only, no repeat trigger.
    reset_env();
    g_on_page_present = hook_held_across_end_tab2;
    g_max_steps = 6;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(nav_pt(2));
    add_sample(nav_pt(2));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[2] == 1 && g_nav_req_count == 2);

    // 6. 刷新结束瞬间主循环读到实时按下新 tab 3：实时点按覆盖旧缓存 tab 2。
    // 6. Live touch on tab 3 at loop top overrides stale buffered tab 2.
    reset_env();
    g_req_tab1_on_tick = true;
    g_live_tab3_after_enter1 = true;
    g_on_page_present = hook_short_tap_tab2;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[3] == 1 && g_enters[2] == 0);

    // 7. 多指触控与正文滑入底栏：全部拒绝。
    // 7. Multi-touch and body-started drag into footer are rejected.
    reset_env();
    g_on_page_present = hook_two_finger_and_drag_in;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[2] == 0 && g_nav_req_count == 1);

    // 8. 超过 1200 ms TTL 过期丢弃。/ 8. Buffered tap older than 1200 ms TTL is discarded.
    reset_env();
    g_on_page_present = hook_stale_ttl_tab2;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[1] == 1 && g_enters[2] == 0 && g_nav_req_count == 1);

    // 9. 阅读/电源框/快捷菜单/主菜单/USB 期间不布防且拒绝缓冲，同时测 click_present 期间改点 tab 3 立即覆盖。
    // 9. Reader/power/quick/menu/USB never arm capture; tap during click_present overrides next target.
    reset_env();
    g_on_click_present = hook_click_override_to_tab3;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_enters[3] == 1 && g_enters[1] == 0 && g_nav_req_log[0] == 1 && g_nav_req_log[1] == 3);

    reset_env();
    g_reader_active = true;
    test_reader_open = true;
    g_on_page_present = hook_overlay_tries_inject;
    g_max_steps = 3;
    run_loop(&g_reader);
    assert(g_reader_checked && !g_last_armed_on_reader);
    test_reader_open = false;

    reset_env();
    g_power_due = true;
    g_time_step_us = 3000000;
    g_on_power_dialog = hook_overlay_tries_inject;
    g_max_steps = 4;
    run_loop(&g_roots[0]);
    assert(g_power_checked && !g_last_armed_on_power);

    reset_env();
    g_on_quick_present = hook_overlay_tries_inject;
    g_max_steps = 5;
    add_sample(up_pt());
    add_sample((touch_sample_t){200, 10, 1, 0});
    add_sample((touch_sample_t){200, 100, 1, 0});
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_quick_checked && !g_last_armed_on_quick);

    reset_env();
    g_menu_on_tick = true;
    g_on_menu_present = hook_overlay_tries_inject;
    g_max_steps = 4;
    run_loop(&g_roots[0]);
    assert(g_menu_checked && !g_last_armed_on_menu);

    reset_env();
    g_usb_active = true;
    g_on_page_present = hook_overlay_tries_inject;
    g_max_steps = 3;
    run_loop(&g_roots[0]);
    assert(g_usb_checked && !g_last_armed_on_usb);

    // 10. app_poll_main_navigation 在布防深度 > 0 时消费已缓存非 1 号页签，而不直接抢读 I2C。
    // 10. app_poll_main_navigation consumes buffered non-1 tab when armed depth > 0.
    reset_env();
    g_on_page_present = hook_poll_main_nav_while_armed;
    g_max_steps = 4;
    add_sample(up_pt());
    add_sample(nav_pt(1));
    add_sample(up_pt());
    run_loop(&g_roots[0]);
    assert(g_poll_nav_ok);

    puts("PASS: Pico refresh-window nav tap buffer (short tap, wait_release, last-wins, same-tab/subpage, held-across, live-override, multi-touch/drag, TTL, click-override, overlay/reader blocks)");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='pico-nav-tap-buffer-') as d:
        tmp = Path(d)
        power_c = tmp / 'power.c'
        harness = tmp / 'nav_tap_buffer_host_test.c'
        exe = tmp / 'nav_tap_buffer_host_test'
        subprocess.run([
            'python3', str(ROOT / 'tools/power_dialog_host_test.py'),
            '--input-c', str(power_c),
        ], check=True)
        harness.write_text(HARNESS_C, encoding='utf-8')
        subprocess.run([
            'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I' + str(ROOT / 'tools/app_loop_stubs'),
            '-I' + str(ROOT / 'main'),
            '-I' + str(ROOT / 'components/ble_page_turner/include'),
            '-I' + str(ROOT / 'main/app'),
            '-I' + str(ROOT / 'main/ui'),
            '-I' + str(ROOT / 'main/font'),
            '-I' + str(ROOT / 'tools/ui_gesture_stubs'),
            str(power_c),
            str(harness),
            str(APP_LOOP),
            str(ROOT / 'main/ui/ui_gesture.c'),
            str(ROOT / 'main/ui/ui_quick_menu_input.c'),
            '-o', str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()

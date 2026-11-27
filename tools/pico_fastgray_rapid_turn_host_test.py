#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：验证 Pico 快速灰阶连续黑白快翻、局部正文阈值、停手定稿、周期清理合并及退出边界。
English: Verify Pico fast-gray rapid BW turns, body thresholding, idle settle, cleanup coalescing and cancellation boundaries.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y, width, height; } EpdRect;
typedef struct { int id; } EpdWaveform;
typedef struct { bool touched; int x, y, count; } touch_state_t;
typedef struct {
    void *hl;
    uint8_t *fb;
    int64_t now_ms;
    bool request_app;
    touch_state_t *touch;
    int leaf;
} app_ctx_t;
typedef int book_view_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_DONE } app_redraw_t;
enum EpdDrawError { EPD_DRAW_SUCCESS, EPD_DRAW_OTHER_ERROR };
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
enum { READING, SHELF, MANAGE, BULK, EDIT, SEARCH, TOC, READER_PANEL_NONE, READER_PANEL_TOOLS, READER_PANEL_BACKGROUND_PREVIEW };
enum { READER_PANEL_TOUCH_SETTINGS = 90, READER_PANEL_TOUCH_MORE = 91 };
enum EpdDrawMode { MODE_DU, MODE_GL16, MODE_GC16, APP_PAGE_REFRESH_MODE = MODE_GL16 };
enum { E0470_TURN_RTL = 0, E0470_TURN_LTR = 1 };

#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_BAR_H 96
#define UI_BAR_TOP (UI_LOCK_HEIGHT - 24 - UI_BAR_H)
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
#define E0470_TURN_FAST_TICK_US 11090
#define BOOK_STORE_PATH_MAX 256
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define TT_STAMP(x) ((void)0)
#define TT_SPAN(k, x) do { x; } while (0)
#define READER_RAPID_TURN 1
#define READER_RAPID_GAP_MS 500
#define READER_RAPID_SETTLE_MS 700
#define READER_TICKET_DAY_MS UINT64_C(86400000)

#define W 1216
#define H 684
#define FB_BYTES (W * H / 2)

static const EpdWaveform E0470_WAVEFORM = {0}, E0470_FULL_WAVEFORM = {1}, E0470_FOLLOW_WAVEFORM = {2};
static const EpdWaveform soft_turn_waveform = {3}, gray8_turn_waveform = {4}, white_push_waveform = {5}, image_turn_waveform = {6};
#define display_main_normal_waveform() (&E0470_WAVEFORM)
static const EpdWaveform *display_main_gray_waveform(void) { return &E0470_WAVEFORM; }
static const EpdWaveform *display_reader_soft_turn_waveform(void) { return &soft_turn_waveform; }
static const EpdWaveform *display_reader_gray8_turn_waveform(void) { return &gray8_turn_waveform; }
static const EpdWaveform *display_reader_white_push_waveform(void) { return &white_push_waveform; }
static const EpdWaveform *display_reader_image_turn_waveform(void) { return &image_turn_waveform; }

static uint8_t fb[FB_BYTES], kept_fb[FB_BYTES], captured_fb[FB_BYTES];
static int s_view = READING, s_presented_view = READING, s_reader_panel = READER_PANEL_NONE;
#define s_toolbar (s_reader_panel != READER_PANEL_NONE)
static bool s_clear_confirm, s_reader_fullscreen, s_reader_text_frame = true;
static bool s_reader_turn_pending, s_water_turn_pending, s_reader_cleanup, s_reader_dc_recover;
static bool s_reader_image_refresh_pending, s_reader_footer_pending, s_chapter_display_pending;
static bool s_shelf_feedback_pending, s_cover_batch_present_pending, s_shelf_page_pending;
static bool s_reader_clock_pending, s_input_settle, s_shelf_key_paint_pending;
static bool s_rc_full_was_visible, s_rc_double_pending, s_rc_auto_turning, s_txt_opening;
static bool s_shelf_key_mode, s_category_menu, s_import_candidates, s_lock_reader_reopen, s_lock_reader_fullscreen;
static bool s_shelf_page_key_valid, s_shelf_page_loading, s_shelf_page_cached;
static bool s_save_due, s_save_failed;
static uint8_t s_save_due_turns, s_back_turns;
static int s_water_turn_dir, s_du_count, s_shelf_key_delta, s_shelf_key_pressed, s_reader_slider = -1;
static enum EpdDrawMode s_mode = MODE_GL16;
static unsigned s_turns, s_unsaved, s_cover_dirty_mask, s_cover_present_mask, s_dc_recover_pages;
static unsigned s_stats_pending_turns, s_session_turns;
static size_t s_page, s_chapter, s_chapter_count = 2, s_text_len = 500, s_jump_offset;
static EpdRect s_area, s_du_area;
static int64_t s_last_turn_ms, s_stats_activity_ms, s_reader_clock_minute, s_reader_clock_painted_minute, s_du_ms;
static int64_t s_reader_footer_due_ms, s_reader_notice_until, s_stats_last_ms;
static bool s_part_more;
static size_t s_part_base;
static const char *s_text = "text";
static char s_path[BOOK_STORE_PATH_MAX] = "/sdcard/book.epub";
static char s_reader_notice[64], s_batch_message[64], s_lock_reader_path[BOOK_STORE_PATH_MAX];
static struct { bool pending; } s_reader_tap;
static bool s_rapid_turn_pending, s_rapid_settle_due, s_rapid_cleanup_due, s_rapid_settle_pending;
static int64_t s_turn_done_ms = -1000000;

static int64_t test_now_ms = 10000;
static int test_effect = 3, test_full_pages = 0, test_images = 0;
static bool test_low_ghost, test_immersive, test_touch_pending, test_fail_push, test_draw_locked, test_rapid_enabled = true;
static int test_push_calls, test_water_calls, test_dc_calls, test_footer_commits, test_white_pushes;
static int test_saves;
static EpdRect test_areas[16];
static int test_modes[16];
static const EpdWaveform *test_waves[16];
static bool test_full_flags[16];

static int epd_width(void) { return W; }
static int epd_height(void) { return H; }
static enum EpdRotation epd_get_rotation(void) { return EPD_ROT_INVERTED_PORTRAIT; }
static int64_t esp_timer_get_time(void) { return test_now_ms * 1000; }
static bool app_touch_input_pending(app_ctx_t *ctx) { (void)ctx; return test_touch_pending; }
static uint8_t app_settings_reader_turn_effect(void) { return (uint8_t)test_effect; }
static bool app_settings_reader_rapid_turn(void) { return test_rapid_enabled; }
static uint8_t app_settings_reader_full_pages(void) { return (uint8_t)test_full_pages; }
static bool app_settings_low_ghost(void) { return test_low_ghost; }
static bool app_settings_reader_immersive(void) { return test_immersive; }
static const char *app_settings_book_background_path(void) { return ""; }
static bool reader_status_visible(void) { return !s_reader_fullscreen; }
static EpdRect reader_status_area(void) { return (EpdRect){0, 0, UI_LOCK_WIDTH, 80}; }
static int64_t ui_nav_clock_minute(void) { return 42; }
static size_t book_layout_page_count(void) { return 10; }
static size_t book_chapter_count(void) { return s_chapter_count; }
static int book_layout_page_image_count(size_t page) { (void)page; return test_images; }
static unsigned percent(size_t page) { return (unsigned)((page + 1) * 10); }
static void reader_zoom_reset(void) {}
static void rc_pause_auto(void) {}
static void rc_stop_auto(void) {}
static void rc_cancel_side(void) {}
static bool rc_custom_modal(void) { return false; }
static void reader_indent_hold_cancel(void) {}
static bool reader_indent_present(app_ctx_t *ctx, app_redraw_t r) { (void)ctx; (void)r; return false; }
static void lock_draw(void) { assert(!test_draw_locked); test_draw_locked = true; }
static void unlock_draw(void) { assert(test_draw_locked); test_draw_locked = false; }
static void invalidate_prep(void) {}
static void app_font_activate_reading(void) {}
static void app_font_activate_system(void) {}
static void copy_text(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s); }
static bool load_chapter(app_ctx_t *ctx, size_t ch, size_t off, bool last) { (void)ctx; (void)off; (void)last; s_chapter = ch; s_page = 0; return true; }
static void skip_hidden_image_pages(app_ctx_t *ctx, int dir) { (void)ctx; (void)dir; }
static size_t current_toc_position(void) { return s_chapter; }
static int book_toc_open(size_t pos) { return (int)pos; }
static void ticket_count_turn(void) { ++s_session_turns; ++s_stats_pending_turns; }
static void save_progress(void) { ++test_saves; s_unsaved = 0; s_save_due = false; s_save_due_turns = 0; }
static void render(app_ctx_t *ctx, uint8_t *buf) { (void)ctx; (void)buf; }
static void prepare_covers(app_ctx_t *ctx) { (void)ctx; }
static void cancel_shelf_open(void) {}
static void book_weread_cover_cancel(void) {}
static void book_shelf_page_release(void) {}
static void shelf_category_remember(app_ctx_t *ctx) { (void)ctx; }
static void app_book_ticket_sample(int64_t ms, bool v) { (void)ms; (void)v; }
static bool app_book_resume_context(char *p, size_t n, bool *f) { (void)p; (void)n; (void)f; return false; }
static void book_progress_set_last_path(const char *p) { (void)p; }
static void flush_ticket_stats(void) {}
static bool book_import_running(void) { return false; }
static void book_import_cancel(void) {}
static bool ui_reader_background_prepare(const char *p) { (void)p; return true; }
static void prepare_inline_image(void) {}
static bool kick_prep(void) { return false; }
static void reader_prefetch_end(bool started) { assert(!started); }
static void display_main_transition_cancel(void) {}
static void display_main_transition_shelf_page(void) {}
static bool shelf_cover_frame_ready(void) { return true; }
static void pico_boot_book_end(bool ok) { (void)ok; }
static void guard_draw_result(void *hl, enum EpdDrawError err) { (void)hl; (void)err; }
static int e0470_page_turn_tick_us(void) { return E0470_TURN_FAST_TICK_US; }
static void e0470_page_turn_set_tick_us(int us) { assert(us == E0470_TURN_FAST_TICK_US); }
static void record_push(const EpdWaveform *w, int mode, EpdRect area, bool full) {
    assert(test_push_calls < 16);
    test_waves[test_push_calls] = w;
    test_modes[test_push_calls] = mode;
    test_areas[test_push_calls] = area;
    test_full_flags[test_push_calls] = full;
    ++test_push_calls;
}
static enum EpdDrawError update_display_full(void *hl) { (void)hl; record_push(&E0470_WAVEFORM, MODE_GC16, (EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT}, true); return test_fail_push ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS; }
static enum EpdDrawError update_display_with(void *hl, const EpdWaveform *w, int m) { (void)hl; record_push(w, m, (EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT}, true); return test_fail_push ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS; }
static enum EpdDrawError update_display_area_diff_with(void *hl, const EpdWaveform *w, int m, EpdRect a) { (void)hl; record_push(w, m, a, false); return test_fail_push ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS; }
static enum EpdDrawError update_display_area_full_with(void *hl, const EpdWaveform *w, int m, EpdRect a) {
    (void)hl;
    if (w == &white_push_waveform) ++test_white_pushes;
    record_push(w, m, a, true);
    return test_fail_push ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_reader_area(void *hl, const EpdWaveform *w, int m, EpdRect a, bool full) {
    (void)hl;
    memcpy(captured_fb, fb, FB_BYTES);
    record_push(w, m, a, full);
    return test_fail_push ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_water_turn(void *hl, EpdRect a, int dir) {
    (void)hl; (void)dir;
    ++test_water_calls;
    record_push(&soft_turn_waveform, MODE_GL16, a, false);
    return test_fail_push ? EPD_DRAW_OTHER_ERROR : EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_fast_page(void *hl) { (void)hl; assert(false); return EPD_DRAW_SUCCESS; }
static enum EpdDrawError display_dc_recover_light(void *hl) { (void)hl; ++test_dc_calls; return EPD_DRAW_SUCCESS; }
static bool present_reader_metadata(app_ctx_t *ctx) { (void)ctx; ++test_footer_commits; s_reader_footer_pending = false; return true; }
'''

unit += '\n'.join(line for line in (ROOT / 'main/ui/ui_reader_layout.h').read_text().splitlines()
                  if not line.startswith(('#include', '#pragma'))) + '\n'
unit += function('clip_refresh_rect', ROOT / 'main/ui/ui_kit.c') + '\n'
unit += function('ui_rect_union', ROOT / 'main/ui/ui_kit.c') + '\n'
for name in ('reader_area', 'reader_fullscreen_progress_area', 'progress_rect', 'reader_rapid_reset', 'reader_rapid_threshold_body'):
    unit += function(name, BOOK) + '\n'

unit += r'''
// 模拟 paint_reading：向 s_keep_cur（kept_fb）写入带灰边的正文与独立上下栏灰阶，并拷入 fb。
// Simulate paint_reading: populate kept_fb with gray body + chrome nibbles and copy into ctx->fb.
static void fill_kept_page(size_t page) {
    const uint8_t body_byte = (uint8_t)(0x46u + (page & 3u)); // nibbles 0x6..0x9 / 0x4..0x7 (mixed <8 and >=8)
    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W / 2; ++col) {
            int lx = row;
            int ly = col * 2;
            (void)lx;
            // In EPD_ROT_INVERTED_PORTRAIT: phys.x = ly (where ly=0 is top of logical screen, ly=1215 is bottom)
            // Keep chrome nibbles distinct (0x55) from body nibbles to verify body-only thresholding.
            EpdRect body = reader_area();
            bool in_body = ly >= body.y && ly + 1 < body.y + body.height;
            kept_fb[(size_t)row * (W / 2) + col] = in_body ? body_byte : 0x55u;
        }
    }
}
static app_redraw_t paint_reading(app_ctx_t *ctx, enum EpdDrawMode mode) {
    (void)mode;
    s_water_turn_pending = false;
    s_reader_turn_pending = false;
    s_rapid_turn_pending = false;
    s_reader_image_refresh_pending = test_images > 0;
    fill_kept_page(s_page);
    memcpy(ctx->fb, kept_fb, FB_BYTES);
    s_area = reader_area();
    s_mode = MODE_GL16;
    return APP_REDRAW_AREA;
}
'''

for name in ('set_reader_view', 'present', 'reader_turn_chrome', 'turn_page', 'reader_rapid_settle_tick', 'before_lock'):
    unit += function(name, BOOK) + '\n'

unit += r'''
static uint8_t rapid_expected_byte(uint8_t v) {
    return (uint8_t)(((v & 8u) ? 15u : 0u) | ((v & 0x80u) ? 0xf0u : 0u));
}

static void check_body_thresholded_and_chrome_intact(const uint8_t *got, const uint8_t *kept) {
    const EpdRect body = reader_area();
    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W / 2; ++col) {
            size_t idx = (size_t)row * (W / 2) + col;
            int x_start = col * 2;
            uint8_t g = got[idx], k = kept[idx];
            if (x_start >= body.y && x_start + 1 < body.y + body.height) {
                assert(g == rapid_expected_byte(k));
            } else {
                assert(g == k);
            }
        }
    }
}

static void reset_test(void) {
    s_view = s_presented_view = READING;
    s_reader_panel = READER_PANEL_NONE;
    s_clear_confirm = s_reader_fullscreen = s_reader_dc_recover = s_reader_cleanup = false;
    s_reader_turn_pending = s_water_turn_pending = s_reader_image_refresh_pending = false;
    s_reader_footer_pending = false;
    s_reader_text_frame = true;
    s_mode = MODE_GL16;
    s_page = 0;
    s_chapter = 0;
    s_turns = 0;
    s_unsaved = 0;
    s_dc_recover_pages = 0;
    test_effect = 3;
    test_rapid_enabled = true;
    test_full_pages = 0;
    test_images = 0;
    test_low_ghost = false;
    test_touch_pending = false;
    test_fail_push = false;
    test_now_ms = 10000;
    reader_rapid_reset();
    test_push_calls = test_water_calls = test_dc_calls = test_footer_commits = test_white_pushes = test_saves = 0;
}

static void advance_ms(app_ctx_t *ctx, int64_t delta) {
    test_now_ms += delta;
    ctx->now_ms = test_now_ms;
}

static void drive_turn(app_ctx_t *ctx, int dir, int64_t delta_ms) {
    advance_ms(ctx, delta_ms);
    app_redraw_t r = turn_page(ctx, dir);
    assert(r != APP_REDRAW_NONE);
    assert(present(ctx, r));
}

int main(void) {
    touch_state_t touch = {0};
    app_ctx_t ctx = { .fb = fb, .now_ms = test_now_ms, .touch = &touch };

    // 1. 单次慢翻（fx=3）：直接整屏 8 灰阶推送，不挂起定稿。
    reset_test();
    drive_turn(&ctx, 1, 1000);
    assert(test_push_calls == 1 && test_waves[0] == &gray8_turn_waveform && test_modes[0] == MODE_GL16 && test_full_flags[0]);
    assert(test_footer_commits == 1 && !s_rapid_settle_due && !s_reader_footer_pending);
    assert(reader_rapid_settle_tick(&ctx) == APP_REDRAW_NONE);

    // 2. fx=1（水波纹）即使开启连续快翻也不走黑白 DU；关闭连续快翻时 fx=0 与 fx=3 也不走黑白 DU。
    reset_test();
    test_effect = 1;
    drive_turn(&ctx, 1, 1000);
    test_push_calls = test_water_calls = 0;
    drive_turn(&ctx, 1, 100);
    assert(test_push_calls == 1 && test_modes[0] == MODE_GL16 && test_water_calls == 1 && !s_rapid_settle_due);

    for (int i = 0; i < 2; ++i) {
        reset_test();
        test_effect = i ? 3 : 0;
        test_rapid_enabled = false;
        drive_turn(&ctx, 1, 1000);
        test_push_calls = test_water_calls = 0;
        drive_turn(&ctx, 1, 100);
        assert(test_push_calls == 1 && test_modes[0] == MODE_GL16 && !test_water_calls);
        assert(test_waves[0] == (test_effect == 3 ? &gray8_turn_waveform : &soft_turn_waveform));
        assert(!s_rapid_settle_due);
    }

    // 2B. fx=0（普通）开启连续快翻：快翻中间页走黑白 DU，停手后复用保留帧按普通 16 灰阶（soft_turn）定稿；周期清理同样合并。
    reset_test();
    test_effect = 0;
    test_full_pages = 3;
    drive_turn(&ctx, 1, 1000); // s_turns = 1 (slow soft_turn)
    assert(test_push_calls == 1 && test_waves[0] == &soft_turn_waveform && test_modes[0] == MODE_GL16);
    test_push_calls = test_footer_commits = 0;
    drive_turn(&ctx, 1, 100);  // s_turns = 2 (rapid DU)
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FOLLOW_WAVEFORM && test_modes[0] == MODE_DU && !test_full_flags[0]);
    check_body_thresholded_and_chrome_intact(captured_fb, kept_fb);
    advance_ms(&ctx, READER_RAPID_SETTLE_MS);
    test_push_calls = test_footer_commits = 0;
    app_redraw_t ord_settle = reader_rapid_settle_tick(&ctx);
    assert(ord_settle == APP_REDRAW_AREA && present(&ctx, ord_settle));
    assert(test_push_calls == 1 && test_waves[0] == &soft_turn_waveform && test_modes[0] == MODE_GL16 && test_full_flags[0]);
    assert(test_footer_commits == 1 && !s_rapid_settle_due && !memcmp(fb, kept_fb, FB_BYTES));
    // 定稿重置 s_turn_done_ms，因此后续先慢翻一页建立新节奏，再连翻跨越全刷阈值并合并为一次 GC16 清理。
    test_push_calls = 0;
    drive_turn(&ctx, 1, 100);  // post-settle anchor turn (s_turns = 3 -> triggers normal cleanup)
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FULL_WAVEFORM && test_modes[0] == MODE_GC16 && !s_rapid_settle_due);
    drive_turn(&ctx, 1, 100);  // s_turns = 1 (rapid after anchor)
    drive_turn(&ctx, 1, 100);  // s_turns = 2 (rapid)
    drive_turn(&ctx, 1, 100);  // s_turns = 3 -> cleanup deferred!
    assert(s_rapid_cleanup_due && s_rapid_settle_due && !s_reader_cleanup);
    advance_ms(&ctx, READER_RAPID_SETTLE_MS);
    test_push_calls = 0;
    ord_settle = reader_rapid_settle_tick(&ctx);
    assert(ord_settle == APP_REDRAW_AREA && present(&ctx, ord_settle));
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FULL_WAVEFORM && test_modes[0] == MODE_GC16 && test_full_flags[0]);

    // 3. fx=3 连续快翻（1 次慢翻 + 3 次 100 ms 快翻），开启低残影也要跳过中间页白推和页脚刷新。
    reset_test();
    test_low_ghost = true;
    drive_turn(&ctx, 1, 1000); // page 1 slow
    assert(test_push_calls == 1 && test_waves[0] == &gray8_turn_waveform && test_footer_commits == 1);
    for (int i = 0; i < 3; ++i) {
        test_push_calls = test_footer_commits = test_white_pushes = 0;
        drive_turn(&ctx, 1, 100);
        assert(test_push_calls == 1 && test_waves[0] == &E0470_FOLLOW_WAVEFORM && test_modes[0] == MODE_DU && !test_full_flags[0]);
        assert(memcmp(&test_areas[0], &(EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, sizeof(EpdRect)) != 0);
        assert(test_areas[0].y == reader_area().y && test_areas[0].height == reader_area().height);
        assert(test_footer_commits == 0 && test_white_pushes == 0);
        assert(s_reader_footer_pending && s_reader_footer_due_ms == test_now_ms + READER_RAPID_SETTLE_MS + 300);
        assert(s_rapid_settle_due);
        // 核对真实 4bpp 像素：仅正文区域被阈值成黑白，上下栏像素及 kept_fb 保持完整灰阶。
        check_body_thresholded_and_chrome_intact(captured_fb, kept_fb);
    }

    // 4. 停手定稿门禁：699 ms 不触发，700 ms 按住或有触控待处理不触发，完全空闲 700 ms 后定稿。
    advance_ms(&ctx, 699);
    assert(reader_rapid_settle_tick(&ctx) == APP_REDRAW_NONE);
    advance_ms(&ctx, 1);
    touch.touched = true;
    assert(reader_rapid_settle_tick(&ctx) == APP_REDRAW_NONE);
    touch.touched = false;
    test_touch_pending = true;
    assert(reader_rapid_settle_tick(&ctx) == APP_REDRAW_NONE);
    test_touch_pending = false;

    test_push_calls = test_footer_commits = 0;
    app_redraw_t settle = reader_rapid_settle_tick(&ctx);
    assert(settle == APP_REDRAW_AREA && !s_rapid_settle_due);
    assert(present(&ctx, settle));
    assert(test_push_calls == 1 && test_waves[0] == &gray8_turn_waveform && test_modes[0] == MODE_GL16 && test_full_flags[0]);
    assert(test_footer_commits == 1 && !s_reader_footer_pending && s_turn_done_ms == -1000000);
    // 定稿使用未二值化的 kept_fb 原始灰阶，且定稿后紧接着翻页仍走慢翻路径。
    assert(!memcmp(fb, kept_fb, FB_BYTES));
    test_push_calls = 0;
    drive_turn(&ctx, 1, 200);
    assert(test_push_calls == 1 && test_waves[0] == &gray8_turn_waveform && !s_rapid_settle_due);

    // 5A. 周期清理合并：full_pages=3，第 3 页在快翻中挂起清理；停手定稿直接合为一次 GC16 清理。
    reset_test();
    test_full_pages = 3;
    drive_turn(&ctx, 1, 1000); // s_turns = 1 (slow)
    drive_turn(&ctx, 1, 100);  // s_turns = 2 (rapid)
    test_push_calls = test_footer_commits = 0;
    drive_turn(&ctx, 1, 100);  // s_turns = 3 -> cleanup deferred!
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FOLLOW_WAVEFORM && test_modes[0] == MODE_DU);
    assert(s_rapid_cleanup_due && s_rapid_settle_due && !s_reader_cleanup);
    advance_ms(&ctx, READER_RAPID_SETTLE_MS);
    test_push_calls = test_footer_commits = 0;
    settle = reader_rapid_settle_tick(&ctx);
    assert(settle == APP_REDRAW_AREA && s_reader_cleanup && !s_rapid_cleanup_due);
    assert(present(&ctx, settle));
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FULL_WAVEFORM && test_modes[0] == MODE_GC16 && test_full_flags[0]);
    assert(test_footer_commits == 1 && !s_reader_cleanup && !s_rapid_settle_due);

    // 5B. 若定稿前先慢翻：该慢翻直接补做欠下的周期清理，不再额外定稿。
    reset_test();
    test_full_pages = 3;
    drive_turn(&ctx, 1, 1000); // s_turns = 1
    drive_turn(&ctx, 1, 100);  // s_turns = 2 (rapid)
    drive_turn(&ctx, 1, 100);  // s_turns = 3 (rapid, defers cleanup)
    assert(s_rapid_cleanup_due && s_rapid_settle_due);
    test_push_calls = test_footer_commits = 0;
    drive_turn(&ctx, 1, 600);  // slow turn before 700 ms settle!
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FULL_WAVEFORM && test_modes[0] == MODE_GC16 && test_full_flags[0]);
    assert(test_footer_commits == 1 && !s_rapid_cleanup_due && !s_rapid_settle_due);
    advance_ms(&ctx, READER_RAPID_SETTLE_MS);
    assert(reader_rapid_settle_tick(&ctx) == APP_REDRAW_NONE);

    // 6. 边界：插图页、低残影第 15 页 DC 恢复、DU 刷屏失败、锁屏与切目录均正确清理快翻状态。
    // 6A. 快翻中落在插图页：立即按插图灰阶呈现并清掉定稿，且折叠欠下的清理。
    reset_test();
    test_full_pages = 3;
    drive_turn(&ctx, 1, 1000); // s_turns = 1
    drive_turn(&ctx, 1, 100);  // s_turns = 2
    drive_turn(&ctx, 1, 100);  // s_turns = 3 (deferred cleanup)
    test_images = 1;
    test_push_calls = 0;
    drive_turn(&ctx, 1, 100);  // lands on image page, s_turns = 4 -> non-rapid image page + owed cleanup
    assert(!s_rapid_turn_pending && !s_rapid_settle_due && !s_rapid_cleanup_due);
    assert(test_push_calls == 1 && test_waves[0] == &E0470_FULL_WAVEFORM && test_modes[0] == MODE_GC16);

    // 6B. 低残影 15 页 DC 恢复：不进入快翻 DU，执行轻量 DC 恢复并清理状态。
    reset_test();
    test_low_ghost = true;
    drive_turn(&ctx, 1, 1000);
    drive_turn(&ctx, 1, 100);
    assert(s_rapid_settle_due);
    s_dc_recover_pages = 14;
    test_dc_calls = 0;
    drive_turn(&ctx, 1, 100);
    assert(test_dc_calls == 1 && !s_rapid_settle_due && !s_rapid_turn_pending && !s_rapid_cleanup_due);

    // 6C. 快翻 DU 推屏失败不会把 s_rapid_settle_due 卡死为 true。
    reset_test();
    drive_turn(&ctx, 1, 1000);
    test_fail_push = true;
    drive_turn(&ctx, 1, 100);
    assert(!s_rapid_settle_due);
    // 即便第二拍快翻失败，也不应残留 s_rapid_settle_due。
    reset_test();
    drive_turn(&ctx, 1, 1000);
    drive_turn(&ctx, 1, 100);
    assert(s_rapid_settle_due);
    test_fail_push = true;
    drive_turn(&ctx, 1, 100);
    assert(!s_rapid_settle_due);

    // 6D. before_lock 与 set_reader_view(TOC) 重置全部快翻状态。
    reset_test();
    drive_turn(&ctx, 1, 1000);
    drive_turn(&ctx, 1, 100);
    assert(s_rapid_settle_due);
    before_lock(&ctx);
    assert(!s_rapid_settle_due && !s_rapid_cleanup_due && !s_rapid_turn_pending && s_turn_done_ms == -1000000);

    reset_test();
    drive_turn(&ctx, 1, 1000);
    drive_turn(&ctx, 1, 100);
    assert(s_rapid_settle_due);
    set_reader_view(TOC);
    assert(!s_rapid_settle_due && !s_rapid_cleanup_due && !s_rapid_turn_pending && s_turn_done_ms == -1000000);

    puts("PASS: Pico fast-gray rapid BW turns, body-only threshold, 700ms idle settle, cleanup coalescing and cancellation boundaries");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='pico-fastgray-rapid-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                    '-g', '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

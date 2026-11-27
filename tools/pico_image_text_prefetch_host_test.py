#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：Pico 插图页后台预绘正文文字层的宿主回归。用真实 book_layout.c 核对插图页上
book_layout_draw_slice 与 book_layout_draw_page 的正文字节一致、插图槽位留白；再从 app_book.c 抽取真实
函数，覆盖预绘目标、进入插图页保留/作废、paint_reading 命中与 draw_reader 现画逐字节一致、缩放/换页/
代次/工具栏/隐藏插图/释放 s_next_fb 时的安全回退。
English: Host regressions for background body-text prefetch on Pico image pages. The real book_layout.c
proves draw_slice and draw_page paint identical body bytes on image pages with blank image slots; real
app_book.c functions are then extracted to cover prefetch targets, keep/invalidate on entering an image
page, paint_reading hits that equal draw_reader byte for byte, and safe fallback on zoom, page change,
layout generation, toolbar, hidden images and a released s_next_fb."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'boards/pico/src/main/apps/app_book.c'
if not BOOK.is_file():
    BOOK = ROOT / 'main/apps/app_book.c'

LAYOUT_UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"

int test_heap_fail_after = -1;
size_t test_heap_largest = 0;
int test_guide_segments, test_guide_first_x, test_guide_first_y, test_guide_second_y, test_guide_last_y, test_guide_height;
uint8_t test_guide_gray;
static int s_font_slot = 0;
#define FBW 400
#define FBH 400
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) { (void)runs; (void)count; }
int ttf_font_select(int slot) { int p = s_font_slot; s_font_slot = slot; return p; }
int ttf_font_selected(void) { return s_font_slot; }
int ttf_get_weight(void) { return 400; }
void ttf_set_weight(int weight) { (void)weight; }
bool ttf_font_slot_ready(int slot) { return slot == 0; }
int ttf_text_width_px(int px, const char* text) {
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) if ((*p & 0xc0) != 0x80) ++n;
    return n * (px > 0 ? px / 2 : 8);
}
int ttf_text_left_bearing_px(int px, const char* text) { (void)px; (void)text; return 0; }
int ttf_ascender_px(int px) { return px * 4 / 5; }
void ttf_measure_line_px(int px, const char *text, int *above, int *below) { (void)text; *above = px * 4 / 5; *below = px / 5; }
// 把落点与文本哈希写进缓冲：两条路径只要落点或内容有一处不同，缓冲就逐字节不等。
// Stamp position and a text hash into the buffer so any difference in placement or content shows byte-wise.
static void stamp(uint8_t* fb, int x, int y, int px, const char* text) {
    unsigned h = 2166136261u;
    for (const char* p = text; *p; ++p) h = (h ^ (unsigned char)*p) * 16777619u;
    for (int i = 0; i < px; ++i) {
        int xx = x + i;
        if (xx < 0 || xx >= FBW || y < 0 || y >= FBH) continue;
        fb[y * FBW + xx] = (uint8_t)(h >> (i % 24)) | 1;
    }
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text, enum EpdFontFlags align, uint8_t fg, uint8_t bg) { (void)align; (void)fg; (void)bg; stamp(fb, x, y, px, text); }
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text, int t, uint8_t fg, uint8_t bg) { (void)t; ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg); }
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text, int t, int tw, uint8_t fg, uint8_t bg) { (void)t; (void)tw; ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg); }
int ttf_draw_text_px_rotated(uint8_t* fb, int x, int y, int px, const char* text, uint8_t fg, uint8_t bg) { ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg); return 0; }
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int px, const char* text, int t, uint8_t fg, uint8_t bg) { (void)t; ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg); return 0; }
int ttf_vertical_text_height_px(int px, const char* text, int t) { (void)t; return ttf_text_width_px(px, text); }
static bool dims(void *ctx, int image, int *w, int *h) { (void)ctx; (void)image; *w = 100; *h = 80; return true; }

int main(void) {
    book_layout_set_image_dims(dims, NULL);
    const char *para = "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt";
    static char text[4096];
    size_t len = 0;
    blk_t blocks[5];
    memset(blocks, 0, sizeof blocks);
    // 文字 / 插图 / 文字 / 插图 / 文字：两张行内插图，各自带记录。
    // Text, image, text, image, text: two inline images, each with a record.
    for (int i = 0; i < 5; ++i) {
        blocks[i].image = -1;
        blocks[i].offset = len;
        if (i % 2) {
            blocks[i].len = 3; blocks[i].image = i / 2; blocks[i].image_width = 100; blocks[i].image_height = 80;
            memcpy(text + len, "IMG", 3);
        } else {
            blocks[i].len = strlen(para);
            memcpy(text + len, para, blocks[i].len);
        }
        len += blocks[i].len;
        if (i < 4) text[len++] = '\n';
    }
    text[len] = 0;
    EpdRect rect = {10, 10, 300, 380};
    assert(book_layout_build_blocks(text, len, blocks, 5, rect, 20));
    size_t pages = book_layout_page_count();
    unsigned image_pages = 0, multi_slice = 0;
    for (size_t page = 0; page < pages; ++page) {
        uint8_t *a = malloc(FBW * FBH), *b = malloc(FBW * FBH);
        assert(a && b);
        memset(a, 0xff, FBW * FBH); memset(b, 0xff, FBW * FBH);
        book_layout_draw_page(a, page, rect, 20);
        book_layout_draw_cursor_t cursor = {0};
        int result = 0; unsigned slices = 0;
        do { result = book_layout_draw_slice(b, page, rect, 20, &cursor); ++slices; assert(result >= 0 && slices < 1000); } while (!result);
        // 正文文字层逐字节一致 / The body-text layer is byte-identical.
        assert(memcmp(a, b, FBW * FBH) == 0);
        const int images = book_layout_page_image_count(page);
        if (images > 0) {
            ++image_pages;
            if (slices > 1) ++multi_slice;
            // 插图槽位两条路径都留白，等 draw_reader_outer 贴图。
            // Image slots stay blank on both paths for draw_reader_outer to fill.
            for (int slot = 0; slot < images; ++slot) {
                int index = -1, y = 0, w = 0, h = 0;
                assert(book_layout_page_image_at(page, slot, &index, &y, &w, &h));
                for (int row = rect.y + y; row < rect.y + y + h; ++row)
                    for (int col = 0; col < FBW; ++col) assert(a[row * FBW + col] == 0xff && b[row * FBW + col] == 0xff);
            }
        }
        free(a); free(b);
    }
    assert(image_pages >= 1 && multi_slice >= 1);
    book_layout_free();
    puts("PASS: real book_layout draw_slice matches draw_page byte for byte on image pages, image slots stay blank");
    return 0;
}
'''

APP_UNIT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define HTML_TEXT_MAX_BLOCKS 4096
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED -3
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define TT_SPAN(field, ...) do { __VA_ARGS__; } while (0)
#define TT_STAMP(field) ((void)0)
#define atomic_load(p) (*(p))
#define s_toolbar (s_reader_panel != READER_PANEL_NONE)
#define FB 64
#define SLOT_LO 8
#define SLOT_HI 16
#define BOOK_PREP_RESERVE (1024u * 1024u)

typedef int esp_err_t;
typedef struct { int x, y, width, height; } EpdRect;
typedef struct { uint8_t *gray; int width, height; } reader_image_t;
typedef struct { uint8_t *fb; int page; uint32_t generation; } reader_keep_t;
typedef struct {
    uint32_t generation, image_at;
    size_t page, offset;
    int64_t used;
    bool started, complete, failed;
} book_layout_draw_cursor_t;
typedef struct { uint8_t *fb; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_DONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL } app_redraw_t;
enum EpdDrawMode { MODE_GL16, MODE_DU, MODE_GC16 };
enum { READER_PANEL_NONE = 0, READER_PANEL_SETTINGS = 1 };

static const char *TAG = "test";

// 真实静态状态 / Real static state
static bool s_water_turn_pending, s_reader_turn_pending, s_reader_image_refresh_pending, s_clear_confirm, s_zoom_active;
static int s_reader_panel = READER_PANEL_NONE;
static size_t s_page;
static int s_next_page = -1, s_prep_page = -1, s_px = 20;
static uint32_t s_next_generation;
static uint8_t *s_next_fb;
static reader_keep_t s_keep_cur = {NULL, -1, 0}, s_keep_prev = {NULL, -1, 0};
static reader_image_t *s_page_images;
static int s_page_image_count;
static size_t s_page_images_for = SIZE_MAX, s_image_count, s_chapter;
static uint32_t s_page_images_generation;
static book_layout_draw_cursor_t s_prep_cursor;
static EpdRect s_area;
static bool s_reader_footer_pending, s_image_prep_running;
static enum EpdDrawMode s_mode;
static char **s_images;

// 测试控制 / Test controls
static uint32_t test_gen = 7;
static bool test_hide_images, test_prep_take = true, test_zoom_on, test_status_visible = true;
static const char *test_background = "";
static size_t test_psram = 4u * 1024u * 1024u;
static unsigned test_slices_per_page = 4;

// 监视 / Spies
static unsigned spy_draw_page, spy_slice_calls, spy_release_images, spy_next_chapter_clear, spy_covers, spy_fonts, spy_status;

// 第 3、5 页带插图（各一个槽位）；槽位字节区间 [SLOT_LO,SLOT_HI) 在正文层里留白。
// Pages 3 and 5 carry one image slot each; bytes [SLOT_LO,SLOT_HI) stay blank in the body layer.
static int page_images(size_t page) { return page == 3 || page == 5; }
static void body_row(uint8_t *fb, size_t page, unsigned row) {
    for (unsigned i = row * 8; i < row * 8 + 8; ++i)
        if (i < SLOT_LO || i >= SLOT_HI || !page_images(page)) fb[i] = (uint8_t)(page * 31 + i * 3 + 1);
}
static int64_t esp_timer_get_time(void) { return 0; }
static void ui_reader_background_prepare(const char *p) { (void)p; }
static void lock_draw(void) {}
static void unlock_draw(void) {}
static size_t fb_bytes(void) { return FB; }
static uint32_t book_layout_generation(void) { return test_gen; }
static size_t book_layout_page_count(void) { return 9; }
static int book_layout_page_image_count(size_t page) { return test_hide_images ? 0 : page_images(page); }
static int reader_image_slots(size_t page) { return book_layout_page_image_count(page); }
static const char *app_settings_book_background_path(void) { return test_background; }
static bool app_settings_reader_hide_images(void) { return test_hide_images; }
static bool reader_keep_buffers(void) { return false; }
static bool reader_keep_holds(const reader_keep_t *k, int page) { (void)k; (void)page; return false; }
static bool reader_status_visible(void) { return test_status_visible; }
static void reader_status_paint(uint8_t *fb) { fb[FB - 2] = 0x5a; ++spy_status; }
static EpdRect reader_area(void) { return (EpdRect){0, 0, 100, 100}; }
static EpdRect body_rect(void) { return (EpdRect){0, 0, 100, 100}; }
static void ui_text_set_system_scale(bool s) { (void)s; }
static void ui_clear_page(uint8_t *fb) { memset(fb, 0xff, FB); }
static unsigned s_back_turns;
static const reader_image_t *reader_zoom_image(void) { static reader_image_t zoom; return test_zoom_on ? &zoom : NULL; }
// 现画整页正文：与 draw_slice 逐行拼出的结果相同（真实布局层一致性见 layout 测试）。
// Full-page body draw; identical to the row-by-row draw_slice result (real layout parity is the layout test).
static void book_layout_draw_page(uint8_t *fb, size_t page, EpdRect rect, int px) {
    (void)rect; (void)px; ++spy_draw_page;
    for (unsigned row = 0; row < 8; ++row) body_row(fb, page, row);
}
static int book_layout_draw_slice(uint8_t *fb, size_t page, EpdRect rect, int px, book_layout_draw_cursor_t *c) {
    (void)rect; (void)px;
    if (!c->generation) { c->generation = test_gen; c->page = page; }
    if (c->generation != test_gen || c->page != page || c->failed) return -1;
    if (c->complete) return 1;
    ++spy_slice_calls;
    body_row(fb, page, (unsigned)c->offset);
    c->started = true;
    if (++c->offset >= test_slices_per_page * 2) { c->complete = true; return 1; }
    return 0;
}
// 外层：插图贴进槽位；缩放时放大图盖住整个正文区；页眉页脚打一个标记。
// Outer layer: image pixels in the slot; zoom magnifies over the whole body; chrome leaves a mark.
static void draw_reader_outer(uint8_t *fb, size_t page) {
    if (page_images(page) && !test_hide_images) {
        for (unsigned i = SLOT_LO; i < SLOT_HI; ++i) fb[i] = 0x11;
    }
    fb[FB - 1] = 0xee;
    if (test_status_visible) fb[FB - 2] = 0x5a;
}
static void next_chapter_clear(void) { ++spy_next_chapter_clear; }
static void invalidate_covers(void) { ++spy_covers; }
static int ttf_font_cache_clear_all(void) { ++spy_fonts; return 0; }
static void reader_image_prep_cancel(void) {}
static void release_page_images(void) { ++spy_release_images; }
static bool reader_image_prep_take(uint32_t generation, int count) { (void)generation; (void)count; return test_prep_take; }
static size_t heap_caps_get_free_size(int caps) { (void)caps; return test_psram; }
static void *heap_caps_calloc(size_t n, size_t sz, int caps) { (void)n; (void)sz; (void)caps; return NULL; }
static void *heap_caps_aligned_alloc(size_t a, size_t n, int caps) { (void)a; (void)caps; return malloc(n); }
static void vTaskDelay(int ticks) { (void)ticks; }
static EpdRect book_layout_image_rect(void) { return (EpdRect){0, 0, 100, 100}; }
static bool reader_image_slot(size_t page, int slot, int *index, int *y, int *width, int *height) {
    (void)page; (void)slot; (void)index; (void)y; (void)width; (void)height; return false;
}
static int book_chapter_png_info(size_t chapter, const char *src, size_t *size, unsigned *w, unsigned *h) {
    (void)chapter; (void)src; (void)size; (void)w; (void)h; return ESP_ERR_NOT_SUPPORTED;
}
static int book_chapter_image(size_t chapter, const char *src, uint8_t **encoded, size_t *size, bool *png) {
    (void)chapter; (void)src; (void)encoded; (void)size; (void)png; return ESP_FAIL;
}
static const char *esp_err_to_name(esp_err_t err) { (void)err; return "ERR"; }
static bool book_image_dimensions(const uint8_t *d, size_t n, bool png, unsigned *w, unsigned *h) { (void)d; (void)n; (void)png; (void)w; (void)h; return false; }
static void reader_image_fit(unsigned sw, unsigned sh, int *w, int *h) { (void)sw; (void)sh; (void)w; (void)h; }
static uint8_t *alloc_page_bitmap(int *w, int *h, size_t *pixels) { (void)w; (void)h; (void)pixels; return NULL; }
static int book_chapter_png_grayscale(size_t c, const char *s, int w, int h, uint8_t *o) { (void)c; (void)s; (void)w; (void)h; (void)o; return ESP_FAIL; }
static bool book_image_grayscale(const uint8_t *d, size_t n, bool png, int w, int h, uint8_t *o) { (void)d; (void)n; (void)png; (void)w; (void)h; (void)o; return false; }
'''

APP_MAIN = r'''
static uint8_t ctxbuf[FB], expect[FB];
static app_ctx_t ctx = {.fb = ctxbuf};

static void reset_world(void) {
    reset_prefetch();
    s_keep_cur = s_keep_prev = (reader_keep_t){NULL, -1, 0};
    s_zoom_active = false; test_zoom_on = false; test_hide_images = false; test_prep_take = true;
    test_background = ""; test_psram = 4u * 1024u * 1024u; test_gen = 7; s_back_turns = 0;
    s_reader_panel = READER_PANEL_NONE; s_clear_confirm = false; test_status_visible = true;
    s_page_images_for = SIZE_MAX; s_page_images_generation = test_gen;
    if (!s_next_fb) s_next_fb = malloc(FB);
    memset(s_next_fb, 0xcd, FB);
    spy_draw_page = spy_slice_calls = 0;
}
// 把预绘跑到完成，等价后台任务逐行画完目标页。
// Run the prefetch to completion, as the background task would row by row.
static void prefetch_page(int page) {
    s_next_page = -1; s_next_generation = 0; s_prep_page = page; memset(&s_prep_cursor, 0, sizeof s_prep_cursor);
    int r;
    while ((r = reader_prefetch_step()) == 0) {}
    assert(r == 1 && s_next_page == page && s_next_generation == test_gen);
}
// 以 draw_reader 现画为基准，核对 paint_reading 的输出。
// Compare paint_reading output against a fresh draw_reader as the reference.
static void expect_fresh(void) {
    memset(expect, 0, FB);
    draw_reader(expect, s_page);
}

int main(void) {
    // 1. 预绘目标允许带插图页；背景、越界仍排除；往回翻同样。
    // 1. Prefetch target may be an image page; backgrounds and out-of-range stay excluded; backward too.
    reset_world();
    s_page = 2; assert(page_images(3) && reader_prefetch_target() == 3);
    s_page = 4; assert(reader_prefetch_target() == 5);
    s_page = 4; s_back_turns = 2; assert(reader_prefetch_target() == 3); s_back_turns = 0;
    test_background = "paper"; assert(reader_prefetch_target() == -1); test_background = "";
    s_page = 8; assert(reader_prefetch_target() == -1);

    // 2. 预绘好的插图页：进入时保留，paint_reading 命中，输出与 draw_reader 现画逐字节一致。
    // 2. A fully prefetched image page is kept on entry; paint_reading hits and matches draw_reader byte for byte.
    reset_world();
    s_page = 2; prefetch_page(3);
    assert(spy_draw_page == 0);
    s_page = 3; memset(ctxbuf, 0, FB);
    unsigned draws_before = spy_draw_page, releases_before = spy_release_images;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_release_images == releases_before + 1 && s_page_images_for == 3);
    assert(spy_draw_page == draws_before);               // 命中，没有现画正文 / hit, no fresh body draw
    expect_fresh();
    assert(memcmp(ctxbuf, expect, FB) == 0);
    assert(ctxbuf[SLOT_LO] == 0x11 && ctxbuf[FB - 1] == 0xee && ctxbuf[FB - 2] == 0x5a);
    assert(s_next_fb && s_next_page == 3);              // 命中后预绘缓冲仍属有效，下一页预绘会照常覆盖 / buffer remains valid; the next-page prefetch simply overwrites it
    // 命中后下一页（文字页 4）照常成为预绘目标。/ After the hit the next text page becomes the target as usual.
    assert(reader_prefetch_target() == 4 && s_next_page != reader_prefetch_target());
    prefetch_page(4);
    assert(s_next_page == 4);

    // 3. 半途预绘（未画完）：进入时保留游标，paint_reading 续画完成再命中。
    // 3. A half-finished prefetch keeps its cursor on entry and paint_reading completes it, then hits.
    reset_world();
    s_page = 2; s_next_page = -1; s_prep_page = 3; memset(&s_prep_cursor, 0, sizeof s_prep_cursor);
    assert(reader_prefetch_step() == 0 && reader_prefetch_step() == 0);
    assert(s_prep_cursor.started && s_next_page == -1);
    s_page = 3; memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_draw_page == draws_before && spy_slice_calls == test_slices_per_page * 2);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);

    // 4. 预绘结果是别页：进入插图页作废，现画，不用别页正文。
    // 4. A prefetch of another page is invalidated on entering the image page; draw fresh, never reuse foreign text.
    reset_world();
    s_page = 2; prefetch_page(4);
    s_page = 3; memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_draw_page == draws_before + 1 && s_next_page == -1 && s_prep_page == -1);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);

    // 5. 布局代次变化：即使 s_next_page 恰是本页也作废；预绘代次过期也不命中。
    // 5. A layout-generation change invalidates even when s_next_page equals this page; a stale prefetch never hits.
    reset_world();
    s_page = 2; prefetch_page(3);
    s_page_images_generation = test_gen - 1;             // 本页插图缓存属于旧代次 / image cache belongs to the old generation
    s_page = 3; memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_draw_page == draws_before + 1 && s_next_page == -1);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);
    reset_world();
    s_page = 2; prefetch_page(3); ++test_gen;            // 预绘完成后重排 / reflow after the prefetch finished
    s_page_images_generation = test_gen;
    s_page = 3; memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_draw_page == draws_before + 1);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);

    // 6. 缩放激活：draw_reader 不画正文，命中路径不得拷贝预绘文字层。
    // 6. Zoom active: draw_reader paints no body, so the hit path must not copy the prefetched text.
    reset_world();
    s_page = 2; prefetch_page(3); s_page = 3;
    s_zoom_active = true; test_zoom_on = true;
    memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    expect_fresh();
    assert(memcmp(ctxbuf, expect, FB) == 0);
    assert(spy_draw_page == draws_before);               // 缩放态 draw_reader 也不画正文 / draw_reader skips the body in zoom too
    assert(ctxbuf[0] == 0xff && ctxbuf[FB - 3] == 0xff); // 正文区没有文字层 / no text layer under the zoom
    // 缩放激活但无可缩放目标（图像未就绪）：与 draw_reader 一致，仍画正文，可命中。
    // Zoom flag set but no zoomable image: draw_reader still paints the body, so the hit is allowed.
    test_zoom_on = false; memset(ctxbuf, 0, FB);
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0 && ctxbuf[0] != 0xff);
    // 退出缩放：预绘缓冲仍有效则再次命中。/ After leaving zoom the intact prefetch hits again.
    s_zoom_active = false; memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_draw_page == draws_before);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);

    // 7. 工具栏/确认框打开：不命中，走 draw_reader。
    // 7. Toolbar or confirmation open: no hit; fall back to draw_reader.
    reset_world();
    s_page = 2; prefetch_page(3); s_page = 3; s_reader_panel = READER_PANEL_SETTINGS;
    memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA && spy_draw_page == draws_before + 1);
    s_reader_panel = READER_PANEL_NONE; s_clear_confirm = true; draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA && spy_draw_page == draws_before + 1);
    s_clear_confirm = false;

    // 8. 隐藏插图：prepare_inline_image 直接返回、不碰预绘；本页按文字页命中，输出与现画一致。
    // 8. Hidden images: prepare_inline_image returns without touching the prefetch; the page hits as text.
    reset_world();
    test_hide_images = true;
    s_page = 2; prefetch_page(3); s_page = 3;
    memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(spy_draw_page == draws_before && s_next_page == 3);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0 && ctxbuf[SLOT_LO] != 0x11);
    test_hide_images = false;

    // 9. 内存紧张：同步解码前 release_image_headroom 释放 s_next_fb 并作废，不留悬空指针，paint 现画。
    // 9. Memory pressure: before a synchronous decode release_image_headroom frees s_next_fb and invalidates; no dangling pointer, paint draws fresh.
    reset_world();
    s_page = 2; prefetch_page(3); s_page = 3;
    test_prep_take = false; test_psram = 2u * 1024u * 1024u;
    s_keep_cur = (reader_keep_t){malloc(FB), 2, test_gen}; s_keep_prev = (reader_keep_t){malloc(FB), 1, test_gen};
    memset(ctxbuf, 0, FB); draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(s_next_fb == NULL && s_next_page == -1 && s_prep_page == -1);
    assert(!s_keep_cur.fb && !s_keep_prev.fb && spy_covers == 1 && spy_fonts == 1);
    assert(spy_draw_page == draws_before + 1);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);
    // 缓冲已释放后重复绘制仍安全（s_next_fb 为空不命中）。
    // Repainting after the buffer is gone stays safe: a null s_next_fb never hits.
    memset(ctxbuf, 0, FB);
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    expect_fresh(); assert(memcmp(ctxbuf, expect, FB) == 0);
    // 余量充足时（>=3 MB）不释放，预绘照样保留并命中。
    // With ample headroom (>= 3 MB) nothing is released; the prefetch is kept and hits.
    reset_world();
    s_page = 2; prefetch_page(3); s_page = 3; test_prep_take = false;
    uint8_t *before = s_next_fb; draws_before = spy_draw_page;
    assert(paint_reading(&ctx, MODE_GL16) == APP_REDRAW_AREA);
    assert(s_next_fb == before && s_next_page == 3 && spy_draw_page == draws_before);

    // 10. 文字页行为不变：进入文字页不因预绘属于别页而作废。
    // 10. Text pages are unchanged: entering one never invalidates a prefetch that belongs elsewhere.
    reset_world();
    s_page = 3; prefetch_page(4); s_page = 4;
    s_page_images_for = SIZE_MAX;
    prepare_inline_image();
    assert(s_next_page == 4);
    s_page = 6; s_page_images_for = SIZE_MAX; prepare_inline_image();
    assert(s_next_page == 4);                              // 文字页不 reset（与改动前一致）/ no reset on a text page, as before

    free(s_next_fb);
    puts("PASS: image-page text prefetch is kept and hit byte-identically to draw_reader, and falls back safely on foreign page/generation/zoom/toolbar/hidden images/headroom release");
    return 0;
}
'''


def run_layout_parity(temp: Path):
    stubs = temp / 'stubs'
    stubs.mkdir()
    (stubs / 'ttf_font.h').write_text((ROOT / 'tools/book_layout_stubs/ttf_font.h').read_text())
    (stubs / 'esp_err.h').write_text((ROOT / 'tools/book_epub_stubs/esp_err.h').read_text())
    (stubs / 'esp_heap_caps.h').write_text(
        (ROOT / 'tools/book_layout_stubs/esp_heap_caps.h').read_text() +
        '\nstatic inline void* heap_caps_calloc(size_t count, size_t n, int caps){(void)caps;return test_heap_fail()?NULL:calloc(count,n);}\n'
        'static inline void heap_caps_free(void* p){free(p);}\n'
        'static inline size_t heap_caps_get_free_size(int caps){(void)caps;return 4u*1024u*1024u;}\n'
        'static inline size_t heap_caps_get_largest_free_block(int caps){(void)caps;return 4u*1024u*1024u;}\n')
    (temp / 'layout.c').write_text(LAYOUT_UNIT)
    exe = temp / 'layout'
    subprocess.run([
        'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
        '-I' + str(stubs), '-I' + str(ROOT / 'tools/book_layout_stubs'),
        '-I' + str(ROOT / 'main/book'), '-I' + str(ROOT / 'main'),
        str(temp / 'layout.c'), str(ROOT / 'main/book/book_layout.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def run_app_state(temp: Path):
    unit = APP_UNIT
    names = ('reset_prefetch', 'invalidate_prep', 'reader_keep_free', 'reader_prefetch_step',
             'reader_prefetch_resume', 'reader_prefetch_target', 'release_image_headroom',
             'prepare_inline_image', 'paint_reading', 'draw_reader')
    # 前向声明：这些函数在源码里互相引用。/ Forward declarations for the mutual references.
    unit += 'static void reset_prefetch(void);\nstatic void invalidate_prep(void);\nstatic void release_image_headroom(void);\n' \
            'static void prepare_inline_image(void);\nstatic void draw_reader(uint8_t *fb, size_t page);\n' \
            'static int reader_prefetch_step(void);\nstatic bool reader_prefetch_resume(size_t page);\n'
    for name in names:
        unit += function(name, BOOK) + '\n'
    assert 'zoom_hides_body' in function('paint_reading', BOOK)
    assert 'book_layout_page_image_count((size_t)target)' not in function('reader_prefetch_target', BOOK)
    unit += APP_MAIN
    (temp / 'app.c').write_text(unit)
    exe = temp / 'app'
    subprocess.run([
        'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
        '-g', '-fsanitize=address,undefined', str(temp / 'app.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def main():
    with tempfile.TemporaryDirectory(prefix='pico-image-text-prefetch-') as d:
        temp = Path(d)
        (temp / 'a').mkdir(); (temp / 'b').mkdir()
        run_layout_parity(temp / 'a')
        run_app_state(temp / 'b')


if __name__ == '__main__':
    main()

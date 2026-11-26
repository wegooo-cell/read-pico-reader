#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：第3项章末预排下一章首页主机回归（TXT/EPUB 隔离、确认门禁、回翻/重排/OOM/中途退出边界）。
English: Item #3 cross-chapter preload host regressions (TXT/EPUB isolation, commit gate, backward/reflow/OOM/exit bounds)."""
from pathlib import Path
import subprocess
import tempfile
import zipfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'boards/pico/src/main/apps/app_book.c'
if not BOOK.is_file():
    BOOK = ROOT / 'main/apps/app_book.c'


def make_test_epub(path: Path):
    container = b'<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
    opf = (
        b'<?xml version="1.0" encoding="utf-8"?>'
        b'<package xmlns="http://www.idpf.org/2007/opf" version="2.0">'
        b'<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Preload</dc:title></metadata>'
        b'<manifest>'
        b'<item id="c0" href="ch0.xhtml" media-type="application/xhtml+xml"/>'
        b'<item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>'
        b'<item id="img1" href="pic.png" media-type="image/png"/>'
        b'</manifest>'
        b'<spine><itemref idref="c0"/><itemref idref="c1"/></spine>'
        b'</package>'
    )
    ch0 = (
        '<?xml version="1.0" encoding="utf-8"?><html><body>'
        '<h1>第一章 起点</h1><p>当前章节正文内容，用来验证活动排版不被下一章预排破坏。</p>'
        '</body></html>'
    ).encode('utf-8')
    ch1 = (
        '<?xml version="1.0" encoding="utf-8"?><html><body>'
        '<h1>第二章 预排目标</h1>'
        '<p>第二章第一段，应该被后台分步预排并预绘首页，不能提前污染当前章状态。</p>'
        '<p>第二章第二段<a href="#n1">注1</a>，包含更多内容用于跨页分页测试。</p>'
        '<aside id="n1">脚注说明文本</aside>'
        '</body></html>'
    ).encode('utf-8')
    # 最小 1x1 有效 PNG / Minimal 1x1 valid PNG
    png = (
        b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x00\x00\x00\x00:~\x9bU'
        b'\x00\x00\x00\nIDATx\x9cc`\x00\x00\x00\x02\x00\x01H\xaf\xa4q\x00\x00\x00\x00IEND\xaeB`\x82'
    )
    with zipfile.ZipFile(path, 'w') as zf:
        zf.writestr('META-INF/container.xml', container)
        zf.writestr('OEBPS/content.opf', opf)
        zf.writestr('OEBPS/ch0.xhtml', ch0)
        zf.writestr('OEBPS/ch1.xhtml', ch1)
        zf.writestr('OEBPS/pic.png', png)


def run_source_and_stage_test(temp: Path):
    stubs = temp / 'stubs'
    stubs.mkdir()
    # 组装主机测试桩头文件 / Provide unified ttf_font.h + epdiy.h + esp_err.h + esp_heap_caps.h + esp_log.h
    (stubs / 'ttf_font.h').write_text(
        (ROOT / 'tools/book_layout_stubs/ttf_font.h').read_text() +
        '\nvoid ttf_font_close_embedded(void);\n'
        'int ttf_font_open_mem(uint8_t* data, size_t len, const char* label);\n'
    )
    (stubs / 'esp_err.h').write_text((ROOT / 'tools/book_epub_stubs/esp_err.h').read_text())
    (stubs / 'esp_heap_caps.h').write_text(
        (ROOT / 'tools/book_layout_stubs/esp_heap_caps.h').read_text() +
        '\nstatic inline void* heap_caps_calloc(size_t count, size_t n, int caps){(void)caps;return test_heap_fail()?NULL:calloc(count,n);}\n'
        'static inline void heap_caps_free(void* p){free(p);}\n'
        'static inline size_t heap_caps_get_free_size(int caps){(void)caps;return 4u*1024u*1024u;}\n'
        'static inline size_t heap_caps_get_largest_free_block(int caps){(void)caps;return 4u*1024u*1024u;}\n'
    )
    epub_path = temp / 'preload.epub'
    txt_path = temp / 'preload.txt'
    make_test_epub(epub_path)
    txt_path.write_text(
        '第一章 起始\n第一段文字。\n第二段文字。\n'
        '第二章 下一章\n下一章第一段。\n下一章第二段。\n',
        encoding='utf-8',
    )
    c_src = temp / 'source_stage_test.c'
    c_src.write_text(rf'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_source.h"
#include "book_layout.h"
#include "ttf_font.h"

int test_heap_fail_after = -1;
size_t test_heap_largest = 0;
int test_guide_segments, test_guide_first_x, test_guide_first_y, test_guide_second_y, test_guide_last_y, test_guide_height;
uint8_t test_guide_gray;
static int s_font_slot = 0;

void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) {{ (void)runs; (void)count; }}
int ttf_font_select(int slot) {{ int p = s_font_slot; s_font_slot = slot; return p; }}
int ttf_font_selected(void) {{ return s_font_slot; }}
int ttf_get_weight(void) {{ return 400; }}
void ttf_set_weight(int weight) {{ (void)weight; }}
bool ttf_font_slot_ready(int slot) {{ return slot == 0; }}
int ttf_text_width_px(int px, const char* text) {{
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p)
        if ((*p & 0xc0) != 0x80) ++n;
    return n * (px > 0 ? px / 2 : 8);
}}
int ttf_text_left_bearing_px(int px, const char* text) {{ (void)px; (void)text; return 0; }}
int ttf_ascender_px(int px) {{ return px * 4 / 5; }}
void ttf_measure_line_px(int px, const char *text, int *above, int *below) {{
    (void)text; *above = px * 4 / 5; *below = px / 5;
}}
static unsigned s_drawn_calls = 0;
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text, enum EpdFontFlags align, uint8_t fg, uint8_t bg) {{
    (void)x; (void)y; (void)px; (void)text; (void)align; (void)fg; (void)bg;
    if (fb) fb[0] = (uint8_t)(fb[0] ^ 0x55);
    ++s_drawn_calls;
}}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text, int t, uint8_t fg, uint8_t bg) {{
    (void)t; ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg);
}}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text, int t, int tw, uint8_t fg, uint8_t bg) {{
    (void)t; (void)tw; ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg);
}}
int ttf_draw_text_px_rotated(uint8_t* fb, int x, int y, int px, const char* text, uint8_t fg, uint8_t bg) {{
    ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg); return 0;
}}
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int px, const char* text, int t, uint8_t fg, uint8_t bg) {{
    (void)t; ttf_draw_text_px(fb, x, y, px, text, 0, fg, bg); return 0;
}}
int ttf_vertical_text_height_px(int px, const char* text, int t) {{ (void)t; return ttf_text_width_px(px, text); }}
void ttf_font_close_embedded(void) {{}}
int ttf_font_open_mem(uint8_t* data, size_t len, const char* label) {{ (void)len; (void)label; free(data); return -1; }}

int main(void) {{
    // 1. TXT 预载隔离：book_chapter_preload 不得破坏当前章 book_navigation_position，commit 后才切换。
    // 1. TXT preload isolation: book_chapter_preload keeps active book_navigation_position intact until commit.
    assert(book_open("{txt_path}") == ESP_OK);
    assert(book_chapter_count() == 2);
    html_text_t ch0 = {{0}}, cand_txt = {{0}};
    book_chapter_map_t map = {{0}};
    assert(book_chapter_load_blocks(0, &ch0) == ESP_OK);
    size_t nav0 = book_navigation_position(0, 0);
    assert(nav0 == 0);
    assert(book_chapter_preload(1, &cand_txt, &map) == ESP_OK);
    assert(cand_txt.len > 0 && map.mapped && map.chapter == 1);
    assert(book_navigation_position(0, 0) == 0);
    assert(book_navigation_position(1, 0) == SIZE_MAX);
    book_chapter_commit_map(&map);
    assert(book_navigation_position(1, 0) == 1);
    html_text_free(&ch0);
    html_text_free(&cand_txt);
    book_close();

    // 2. EPUB 预载与分步排版隔离：未提交前活动章节页表与代次不变，释放无泄漏。
    // 2. EPUB preload and staged layout isolation: active page table and generation stay unchanged until commit; no leaks on free.
    assert(book_open("{epub_path}") == ESP_OK);
    assert(book_chapter_count() == 2);
    html_text_t epub0 = {{0}}, epub1 = {{0}};
    book_chapter_map_t epub_map = {{0}};
    assert(book_chapter_load_blocks(0, &epub0) == ESP_OK);
    EpdRect rect = {{10, 10, 240, 200}};
    assert(book_layout_build_blocks(epub0.utf8, epub0.len, epub0.blocks, epub0.count, rect, 24));
    uint32_t gen0 = book_layout_generation();
    size_t pages0 = book_layout_page_count();
    assert(book_chapter_preload(1, &epub1, &epub_map) == ESP_OK);
    assert(epub1.note_count == 1 && !epub_map.mapped);
    book_layout_stage_params_t params = {{
        .utf8 = epub1.utf8, .len = epub1.len, .blocks = epub1.blocks, .block_count = epub1.count,
        .runs = epub1.runs, .run_count = epub1.run_count,
        .inline_notes = epub1.notes, .inline_note_count = epub1.note_count,
        .rect = rect, .px = 24,
    }};
    book_layout_stage_t *stage = book_layout_stage_begin(&params);
    assert(stage);
    int res = 0;
    do {{ res = book_layout_stage_step(stage, 1); assert(res >= 0); }} while (!res);
    assert(book_layout_generation() == gen0 && book_layout_page_count() == pages0);
    uint8_t fb[4] = {{0xff, 0xff, 0xff, 0xff}};
    book_layout_draw_cursor_t cur = {{0}};
    do {{ res = book_layout_stage_draw_slice(stage, fb, 0, rect, 24, &cur); assert(res >= 0); }} while (!res);
    assert(s_drawn_calls > 0 && book_layout_generation() == gen0);
    // 放弃候选章，当前章完全不受影响。/ Abandon staged candidate; active chapter remains untouched.
    book_layout_stage_free(stage);
    html_text_free(&epub1);
    assert(book_layout_generation() == gen0 && book_layout_page_count() == pages0);
    book_layout_free();
    html_text_free(&epub0);
    book_close();
    puts("PASS: real TXT navigation-map isolation and EPUB staged preload isolation");
    return 0;
}}
''')
    exe = temp / 'source_stage_test'
    subprocess.run([
        'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
        '-I' + str(stubs),
        '-I' + str(ROOT / 'tools/book_layout_stubs'),
        '-I' + str(ROOT / 'tools/zip_host_stubs'),
        '-I' + str(ROOT / 'tools/book_epub_stubs'),
        '-I' + str(ROOT / 'main/book'),
        '-I' + str(ROOT / 'main'),
        str(c_src),
        str(ROOT / 'main/book/book_source.c'),
        str(ROOT / 'main/book/book_index_cache.c'),
        str(ROOT / 'main/book/book_txt.c'),
        str(ROOT / 'main/book/gbk.c'),
        str(ROOT / 'main/book/book_epub.c'),
        str(ROOT / 'main/book/book_image_header.c'),
        str(ROOT / 'main/book/zip_reader.c'),
        str(ROOT / 'main/book/html_text.c'),
        str(ROOT / 'main/book/book_layout.c'),
        '-lz', '-o', str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)


def run_app_state_machine_test(temp: Path):
    source = BOOK.read_text()
    # 从 Pico app_book.c 直接提取结构体、常量与状态机函数 / Extract production structs/constants and state-machine functions directly from Pico app_book.c
    block_start = source.index('/* ---- 章末预排下一章首页 / Cross-chapter next-page pre-layout ---- */')
    block_end = source.index('\nstatic void free_book(void) {', block_start)
    preload_block = source[block_start:block_end]

    unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOK_STORE_PATH_MAX 288
#define BOOK_PREP_RESERVE (1024u * 1024u)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define TT_STAMP(x) ((void)0)

typedef int esp_err_t;
typedef int EpdWaveform;
typedef struct { int x, y, width, height; } EpdRect;
typedef struct { bool touched; } touch_t;
typedef struct { void *hl; uint8_t *fb; bool consumed, request_app, request_menu, request_return; touch_t *touch; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_DONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL } app_redraw_t;
enum { READING, SHELF, TOC, MANAGE, BULK, EDIT, SEARCH, READER_PANEL_NONE = 0, READER_PANEL_FONT = 1,
       READER_PANEL_TOUCH_SETTINGS = 80, READER_PANEL_TOUCH_MORE = 81, READER_PANEL_BACKGROUND_PREVIEW = 98 };
enum { BOOK_KIND_TXT = 0, BOOK_KIND_EPUB = 1 };
enum EpdDrawMode { MODE_GL16, MODE_DU, MODE_GC16 };
enum EpdDrawError { EPD_DRAW_SUCCESS, EPD_DRAW_ERROR };
enum { APP_PAGE_REFRESH_MODE = MODE_GL16, E0470_TURN_FAST_TICK_US = 14000 };
static const char *TAG = "test";
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

typedef struct {
    size_t offset, len;
    int image;
} blk_t;
typedef struct { size_t offset, len; uint8_t slot; } html_run_t;
typedef struct { size_t marker_offset; int image; } html_note_t;
typedef struct {
    char *utf8;
    size_t len;
    blk_t *blocks;
    size_t count;
    char **images;
    size_t image_count;
    html_run_t *runs;
    size_t run_count;
    html_note_t *notes;
    size_t note_count;
    size_t part_base, source_begin, source_end;
    bool part_more;
} html_text_t;
typedef struct { size_t chapter, begin, end, title; bool mapped; } book_chapter_map_t;
typedef struct { uint32_t generation; size_t page, offset; bool started, complete, failed; } book_layout_draw_cursor_t;
typedef struct {
    const char *utf8; size_t len; const blk_t *blocks; size_t block_count;
    const html_run_t *runs; size_t run_count; const html_note_t *inline_notes; size_t inline_note_count;
    bool (*dims_fn)(void*, int, int*, int*); void *dims_ctx;
    size_t lead_skip; unsigned lead_height, lead_width; EpdRect rect; int px;
} book_layout_stage_params_t;
typedef struct book_layout_stage {
    size_t chapter_marker;
    size_t pages;
    unsigned step_calls, draw_calls;
    bool done, fail_step, fail_draw, page0_img;
} book_layout_stage_t;
typedef struct { uint32_t file_size; uint16_t chapter; uint32_t byte_off; uint8_t px, pct; uint32_t last_open_s; } book_progress_t;
typedef struct pending_progress { char path[BOOK_STORE_PATH_MAX]; book_progress_t value; bool dirty, progress_saved; struct pending_progress *next; } pending_progress_t;
typedef struct { size_t chapter; char **images; size_t count; const html_note_t *notes; size_t note_count; } reader_dims_ctx_t;
typedef struct { uint8_t *fb; int page; uint32_t generation; } reader_keep_t;
static struct { bool valid; size_t chapter, offset; int64_t until_ms; } s_chapter_jump_undo;
static bool s_input_settle;

static int s_view = READING, s_presented_view = READING, s_reader_panel = READER_PANEL_NONE;
static bool s_toolbar, s_clear_confirm, s_fonts_reload_pending, s_reader_rule_pending, s_part_more;
static bool s_book_fonts_applied = true, s_embedded_fonts = true;
static bool s_reader_fullscreen, s_shelf_feedback_pending, s_reader_cleanup, s_reader_image_refresh_pending;
static bool s_water_turn_pending, s_reader_footer_pending, s_reader_turn_pending, s_reader_text_frame;
static bool s_reader_dc_recover, s_rc_full_was_visible, s_shelf_key_paint_pending, s_txt_opening;
static bool s_cover_batch_present_pending, s_shelf_page_pending, s_reader_clock_pending;
static int s_pressed_control = -1, s_water_turn_dir = 1;
static int64_t s_reader_clock_minute, s_reader_clock_painted_minute, s_du_ms;
static unsigned s_du_count, s_cover_dirty_mask, s_cover_present_mask;
static EpdRect s_area, s_du_area;
static enum EpdDrawMode s_mode = MODE_GL16;
static uint8_t s_back_turns;
static bool s_chapter_display_pending;
static size_t s_chapter = 0, s_page = 4, s_chapter_total = 4, s_layout_pages = 5;
static size_t s_text_len = 100, s_block_count, s_run_count, s_note_count, s_image_count;
static size_t s_part_base, s_part_begin, s_part_end, s_selected_toc = 0, s_jump_offset, s_jump_page;
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_row_height, s_chapter_lead_height, s_chapter_lead_width;
static int s_footnote_note = -1, s_px = 48, s_unsaved = 0;
static uint32_t s_file_size = 1000, s_turns = 0;
static uint8_t s_save_due_turns = 0;
static bool s_save_due = false, s_save_failed = false;
static char s_path[BOOK_STORE_PATH_MAX] = "/sdcard/book.epub";
static char s_font_path[192] = "/sdcard/font.ttf";
static char s_cur_font[192] = "/sdcard/font.ttf";
static char s_title[128] = "Ch0", s_message[128], s_chapter_heading_title[128], s_chapter_heading_label[32];
static char *s_text = NULL;
static blk_t *s_blocks = NULL;
static html_run_t *s_runs = NULL;
static html_note_t *s_notes = NULL;
static char **s_images = NULL;
static reader_dims_ctx_t s_reader_dims_ctx;
static size_t s_page_images_for = SIZE_MAX;
static uint32_t s_page_images_generation = 0;
static int s_next_page = -1, s_prep_page = -1;
static uint32_t s_next_generation = 0, s_layout_gen = 1;
static uint8_t *s_next_fb = NULL;
static book_layout_draw_cursor_t s_prep_cursor;
static reader_keep_t s_keep_cur = {NULL, -1, 0}, s_keep_prev = {NULL, -1, 0};

// 测试控制与监视桩 / Test controls & spies
static size_t test_free_psram = 4u * 1024u * 1024u;
static int test_kind = BOOK_KIND_EPUB;
static bool test_touch_pending = false;
static int test_preload_err = ESP_OK;
static bool test_stage_fail_begin = false, test_stage_fail_step = false, test_stage_fail_draw = false;
static bool test_page0_image = false;
static const char *test_bg = "";
static int test_push_err = EPD_DRAW_SUCCESS;
static unsigned test_preload_calls = 0, test_commit_map_calls = 0, test_save_calls = 0;
static unsigned test_fallback_loads = 0, test_keep_frees = 0, test_cover_invalidates = 0, test_font_cache_clears = 0;
static book_progress_t test_saved_progress;
static int64_t test_now_us = 1000;
static bool test_locked = false;
static pending_progress_t test_pending = {.path = "/sdcard/book.epub"};
static pending_progress_t *s_pending = &test_pending;
static unsigned test_step_budget = 6;
static unsigned test_draw_slices = 4;

static void lock_draw(void) { assert(!test_locked); test_locked = true; }
static void unlock_draw(void) { assert(test_locked); test_locked = false; }
static int64_t esp_timer_get_time(void) { return test_now_us += 500; }
static size_t fb_bytes(void) { return 64; }
static size_t heap_caps_get_free_size(int caps) { (void)caps; return test_free_psram; }
static void *heap_caps_aligned_alloc(size_t a, size_t n, int caps) { (void)a; (void)caps; return malloc(n); }
static bool app_touch_input_pending(app_ctx_t *ctx) { (void)ctx; return test_touch_pending; }
static int book_kind(void) { return test_kind; }
static size_t book_chapter_count(void) { return s_chapter_total; }
static size_t book_layout_page_count(void) { return s_layout_pages; }
static uint32_t book_layout_generation(void) { return s_layout_gen; }
static size_t book_layout_page_start_offset(size_t p) { return p * 10; }
static uint32_t book_total_bytes(void) { return 400; }
static uint32_t book_chapter_byte_offset(size_t c) { return (uint32_t)(c * 100); }
static EpdRect body_rect(void) { return (EpdRect){0, 0, 600, 1000}; }
static const char *ttf_font_path(void) { return s_cur_font; }
static bool app_settings_book_fonts(void) { return s_embedded_fonts; }
static unsigned app_settings_book_heading_size(void) { return 100; }
static const char *app_settings_book_background_path(void) { return test_bg; }
static void ui_text_set_system_scale(bool s) { (void)s; }
static void copy_text(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src ? src : ""); }
static int reader_prefetch_target(void) { return (s_page + 1 < s_layout_pages && !s_back_turns) ? (int)s_page + 1 : (s_page > 0 && s_back_turns ? (int)s_page - 1 : -1); }
static bool reader_prefetch_buffer(void) { if (!s_next_fb && test_free_psram > fb_bytes() + BOOK_PREP_RESERVE) s_next_fb = calloc(1, fb_bytes()); return s_next_fb != NULL; }
static void reset_prefetch(void) { s_next_page = s_prep_page = -1; s_next_generation = 0; memset(&s_prep_cursor, 0, sizeof(s_prep_cursor)); }
static void reader_keep_free(void) { ++test_keep_frees; free(s_keep_cur.fb); free(s_keep_prev.fb); s_keep_cur = s_keep_prev = (reader_keep_t){NULL, -1, 0}; }
static void invalidate_prep(void);
static void release_page_images(void) { s_page_images_for = SIZE_MAX; }
static void prepare_inline_image(void) { s_page_images_for = s_page; s_page_images_generation = s_layout_gen; }
static int reader_image_slots(size_t page) { (void)page; return test_page0_image ? 1 : 0; }
static int book_layout_page_image_count(size_t page) { return reader_image_slots(page); }
static bool reader_image_dims(void *ctx, int image, int *w, int *h) { (void)ctx; (void)image; *w = 10; *h = 10; return true; }
static void draw_reader_flow_heading(void) {}
static void book_layout_set_image_dims(bool (*fn)(void*, int, int*, int*), void *ctx) { (void)fn; (void)ctx; }
static void book_layout_set_heading_draw(void (*fn)(void)) { (void)fn; }
static size_t epub_chapter_lead(const html_text_t *t, const char *toc, char *h, size_t hc, char *l, size_t lc) {
    (void)t; (void)toc; snprintf(h, hc, "Heading"); snprintf(l, lc, "Ch"); return 0;
}
static void compute_chapter_lead_metrics(const char *t, const char *l, unsigned h, unsigned sz, unsigned *oh, unsigned *ow) {
    (void)t; (void)l; (void)h; (void)sz; *oh = 0; *ow = 0;
}
static esp_err_t book_chapter_title(size_t ch, char *buf, size_t cap) { snprintf(buf, cap, "Ch%zu", ch); return ESP_OK; }
static void html_notes_free(html_note_t *n, size_t c) { (void)c; free(n); }
static void html_text_free(html_text_t *t) {
    if (!t) return;
    free(t->utf8); free(t->blocks); free(t->runs); html_notes_free(t->notes, t->note_count);
    for (size_t i = 0; i < t->image_count; ++i) free(t->images[i]);
    free(t->images);
    memset(t, 0, sizeof(*t));
}
static esp_err_t book_chapter_preload(size_t ch, html_text_t *out, book_chapter_map_t *map) {
    ++test_preload_calls;
    if (test_preload_err != ESP_OK) return test_preload_err;
    memset(out, 0, sizeof(*out));
    out->utf8 = strdup("Preloaded chapter text");
    out->len = strlen(out->utf8);
    if (test_kind == BOOK_KIND_TXT && map) *map = (book_chapter_map_t){ch, 1, 2, 0, true};
    return ESP_OK;
}
static void book_chapter_commit_map(const book_chapter_map_t *map) {
    if (map && map->mapped) ++test_commit_map_calls;
}
static void book_excerpt_offer(const char *p, size_t c, const html_text_t *t) { (void)p; (void)c; (void)t; }
static book_layout_stage_t *book_layout_stage_begin(const book_layout_stage_params_t *p) {
    (void)p;
    if (test_stage_fail_begin) return NULL;
    book_layout_stage_t *s = calloc(1, sizeof(*s));
    s->pages = 3;
    s->page0_img = test_page0_image;
    return s;
}
static int book_layout_stage_step(book_layout_stage_t *s, unsigned max_steps) {
    (void)max_steps;
    if (!s || test_stage_fail_step) { if (s) s->fail_step = true; return -1; }
    if (s->done) return 1;
    if (++s->step_calls >= test_step_budget) { s->done = true; return 1; }
    return 0;
}
static size_t book_layout_stage_page_count(const book_layout_stage_t *s) { return s ? s->pages : 0; }
static int book_layout_stage_page_image_count(const book_layout_stage_t *s, size_t p) { (void)p; return s && s->page0_img ? 1 : 0; }
static int book_layout_stage_page_image(const book_layout_stage_t *s, size_t p) { (void)p; return s && s->page0_img ? 0 : -1; }
static int book_layout_stage_draw_slice(book_layout_stage_t *s, uint8_t *fb, size_t p, EpdRect r, int px, book_layout_draw_cursor_t *c) {
    (void)p; (void)r; (void)px;
    if (!s || test_stage_fail_draw) return -1;
    if (!c->generation) { c->generation = 99; c->page = 0; }
    c->started = true;
    ++s->draw_calls;
    fb[c->offset++] = (uint8_t)s->draw_calls;
    if (s->draw_calls >= test_draw_slices) { c->complete = true; return 1; }
    return 0;
}
static bool book_layout_stage_commit(book_layout_stage_t *s, void *dims) {
    (void)dims;
    if (!s || !s->done) return false;
    s_layout_pages = s->pages;
    ++s_layout_gen;
    free(s);
    return true;
}
static void book_layout_stage_free(book_layout_stage_t *s) { free(s); }
static bool load_chapter_at(app_ctx_t *ctx, size_t ch, size_t off, bool last, const char *a, size_t so) {
    (void)ctx; (void)off; (void)a; (void)so;
    ++test_fallback_loads;
    s_chapter = ch; s_page = last ? s_layout_pages - 1 : 0; ++s_layout_gen;
    return true;
}
static size_t uxTaskGetStackHighWaterMark(void *t) { (void)t; return 4096; }
static void invalidate_covers(void) { ++test_cover_invalidates; }
static int ttf_font_cache_clear_all(void) { ++test_font_cache_clears; return 0; }
static pending_progress_t *pending_find(const char *path) { return strcmp(path, test_pending.path) ? NULL : &test_pending; }
static bool pending_flush(pending_progress_t *p) { test_saved_progress = p->value; p->dirty = false; p->progress_saved = true; return true; }
static void reader_current_chapter(char *dst, size_t cap) { snprintf(dst, cap, "Ch%zu", s_chapter); }
static void book_ticket_note_position(const char *p, const book_progress_t *v, const char *c, bool s) { (void)p; (void)v; (void)c; (void)s; ++test_save_calls; }

// present() / save_progress() / kick_prep() 所需桩函数 / Stubs needed for present() / save_progress() / kick_prep()
static size_t reader_saved_offset(size_t page) { return page * 10; }
static unsigned percent(size_t page) { return (unsigned)((s_chapter * 25) + page); }
static bool rc_custom_modal(void) { return false; }
static void prepare_covers(app_ctx_t *ctx) { (void)ctx; }
static bool reader_indent_present(app_ctx_t *ctx, app_redraw_t r) { (void)ctx; (void)r; return false; }
static bool reader_status_visible(void) { return false; }
static EpdRect reader_status_area(void) { return (EpdRect){0,0,0,0}; }
static const int E0470_WAVEFORM=0,E0470_FULL_WAVEFORM=1,E0470_FOLLOW_WAVEFORM=2;
static const int *display_main_gray_waveform(void){return &E0470_WAVEFORM;}
static const int *display_main_normal_waveform(void){return &E0470_WAVEFORM;}
static const int *display_reader_soft_turn_waveform(void){return &E0470_WAVEFORM;}
static const int *display_reader_image_turn_waveform(void){return &E0470_WAVEFORM;}
static const int *display_reader_gray8_turn_waveform(void){return &E0470_WAVEFORM;}
static const int *display_reader_white_push_waveform(void){return &E0470_WAVEFORM;}
static enum EpdDrawError update_display_full(void *h){(void)h;return test_push_err;}
static enum EpdDrawError update_display_area_diff_with(void *h,const int *w,enum EpdDrawMode m,EpdRect a){(void)h;(void)w;(void)m;(void)a;return test_push_err;}
static enum EpdDrawError update_display_area_full_with(void *h,const int *w,enum EpdDrawMode m,EpdRect a){(void)h;(void)w;(void)m;(void)a;return test_push_err;}
static enum EpdDrawError update_display_reader_area(void *h,const int *w,enum EpdDrawMode m,EpdRect a,bool f){(void)h;(void)w;(void)m;(void)a;(void)f;return test_push_err;}
static enum EpdDrawError update_display_with(void *h,const int *w,enum EpdDrawMode m){(void)h;(void)w;(void)m;return test_push_err;}
static enum EpdDrawError update_display_water_turn(void *h,EpdRect a,int d){(void)h;(void)a;(void)d;return test_push_err;}
static enum EpdDrawError update_display_fast_page(void *h){(void)h;return test_push_err;}
static enum EpdDrawError display_dc_recover_light(void *h){(void)h;return test_push_err;}
static void display_main_transition_cancel(void){}
static void display_main_transition_shelf_page(void){}
static void ui_reader_background_prepare(const char *p){(void)p;}
static int64_t ui_nav_clock_minute(void){return 1;}
static void render(app_ctx_t *c,uint8_t *f){(void)c;(void)f;}
static bool kick_prep(void);
static EpdRect reader_area(void){return (EpdRect){0,0,684,1216};}
static EpdRect progress_rect(void){return (EpdRect){0,1168,684,48};}
static void epd_scan_totals(uint32_t *a,uint32_t *b,uint32_t *c){*a=*b=*c=0;}
static int e0470_page_turn_tick_us(void){return 0;}
static void e0470_page_turn_set_tick_us(int u){(void)u;}
static bool app_settings_low_ghost(void){return false;}
static int app_settings_reader_turn_effect(void){return 0;}
static void reader_prefetch_end(bool p){(void)p;}
static void pico_boot_book_end(bool ok){(void)ok;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static void guard_draw_result(void *h,enum EpdDrawError e){(void)h;(void)e;}
static bool shelf_cover_frame_ready(void){return true;}
static bool present_reader_metadata(app_ctx_t *c){(void)c;return true;}
static bool reader_keep_holds(const reader_keep_t *k,int p){(void)k;(void)p;return false;}
static void *s_draw_lock, *s_prep_done, *s_prep_task;
static int s_prep_stop;
static void *xSemaphoreCreateMutex(void){return (void*)1;}
static void *xSemaphoreCreateBinary(void){return (void*)1;}
static void prep_task(void *a){(void)a;}
static int xTaskCreatePinnedToCore(void(*fn)(void*),const char *n,int st,void *a,int pr,void **out,int core){(void)fn;(void)n;(void)st;(void)a;(void)pr;(void)out;(void)core;return 0;}
static void xSemaphoreTake(void *s,int t){(void)s;(void)t;}
static void xTaskNotifyGive(void *t){(void)t;}
#define pdPASS 1
#define portMAX_DELAY 0
#define atomic_store_explicit(p,v,m) (*(p)=(v))
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
static void next_chapter_clear(void);
'''
    unit += function('save_progress', BOOK) + '\n'
    unit += function('invalidate_prep', BOOK) + '\n'
    unit += preload_block + '\n'
    unit += function('kick_prep', BOOK) + '\n'
    unit += function('release_image_headroom', BOOK) + '\n'
    unit += function('load_chapter', BOOK) + '\n'
    unit += function('present', BOOK) + '\n'
    unit += r'''
static void reset_env(void) {
    next_chapter_clear();
    free(s_text); free(s_blocks); free(s_runs); html_notes_free(s_notes, s_note_count);
    for (size_t i = 0; i < s_image_count; ++i) free(s_images[i]);
    free(s_images);
    s_text = strdup("active"); s_text_len = 6;
    s_blocks = NULL; s_block_count = 0; s_runs = NULL; s_run_count = 0;
    s_notes = NULL; s_note_count = 0; s_images = NULL; s_image_count = 0;
    s_view = s_presented_view = READING;
    s_reader_panel = READER_PANEL_NONE;
    s_toolbar = s_clear_confirm = s_fonts_reload_pending = s_reader_rule_pending = s_part_more = false;
    s_chapter = 0; s_page = 4; s_layout_pages = 5; s_chapter_total = 4;
    s_back_turns = 0; s_chapter_display_pending = false;
    s_book_fonts_applied = s_embedded_fonts = true;
    copy_text(s_path, sizeof(s_path), "/sdcard/book.epub");
    copy_text(s_font_path, sizeof(s_font_path), "/sdcard/font.ttf");
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/font.ttf");
    ++s_layout_gen;
    reset_prefetch();
    test_free_psram = 4u * 1024u * 1024u;
    test_kind = BOOK_KIND_EPUB;
    test_touch_pending = false;
    test_preload_err = ESP_OK;
    test_stage_fail_begin = test_stage_fail_step = test_stage_fail_draw = false;
    test_page0_image = false;
    test_bg = "";
    test_push_err = EPD_DRAW_SUCCESS;
    test_preload_calls = test_commit_map_calls = test_save_calls = test_fallback_loads = 0;
    test_keep_frees = test_cover_invalidates = test_font_cache_clears = 0;
    test_step_budget = 6; test_draw_slices = 4;
    memset(&test_saved_progress, 0, sizeof(test_saved_progress));
}

int main(void) {
    touch_t touch = {0};
    uint8_t fb_mem[64] = {0};
    app_ctx_t ctx = {.hl = (void*)1, .fb = fb_mem, .touch = &touch};

    // 1. 完整空闲推进与准备/确认分离：未确认前保存的是旧章，显示成功后才确认新章。
    // 1. Full idle progression & prepare/commit separation: old chapter persists before commit and until display succeeds.
    reset_env();
    s_page = 3; // 不是章末页 / Not last page yet
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_IDLE && test_preload_calls == 0);
    s_page = 4; // 章末页 / Last page
    reader_next_chapter_idle(&ctx); // IDLE -> PARSED
    assert(s_next_ch.state == NEXT_CH_PARSED && s_chapter == 0 && test_preload_calls == 1);
    save_progress();
    assert(test_saved_progress.chapter == 0);
    // 触摸待处理时立即让路 / Yield when touch is pending
    test_touch_pending = true;
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_PARSED);
    test_touch_pending = false;
    // 推进至 READY（经由 LAYOUT -> DRAWING -> READY）/ Advance to READY (LAYOUT -> DRAWING -> READY)
    for (int i = 0; i < 8 && s_next_ch.state != NEXT_CH_READY; ++i)
        reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.page0_drawn && s_chapter == 0);
    // 预排完成后若未进章就退出/存盘，必须保持旧章位置。/ Saving or exiting before adopting keeps the prior chapter position.
    save_progress();
    assert(test_saved_progress.chapter == 0);

    // 2. 采用新章 + 推屏成功才确认进度的门禁。/ 2. Display-confirmation gate: persist new chapter only after display succeeds.
    assert(load_chapter(&ctx, 1, 0, false));
    assert(test_fallback_loads == 0 && s_chapter == 1 && s_page == 0 && s_chapter_display_pending);
    assert(s_next_page == 0 && s_next_generation == s_layout_gen && s_next_ch.state == NEXT_CH_IDLE);
    // 模拟推屏失败后再存盘/退出：不得覆盖旧章进度！/ Failed display keeps prior chapter persisted across save/exit.
    test_push_err = EPD_DRAW_ERROR;
    assert(present(&ctx, APP_REDRAW_AREA));
    assert(s_chapter_display_pending);
    save_progress();
    assert(test_saved_progress.chapter == 0);
    // 推屏成功后门禁解除，允许确认新章进度。/ Successful display clears gate so progress can persist.
    test_push_err = EPD_DRAW_SUCCESS;
    assert(present(&ctx, APP_REDRAW_AREA));
    assert(!s_chapter_display_pending);
    save_progress();
    assert(test_saved_progress.chapter == 1 && test_saved_progress.byte_off == 0);

    // 3. 部分预排中途翻页（PARSED / partial LAYOUT / partial DRAWING）可无缝接续。
    // 3. Turn during partial preload (PARSED / partial LAYOUT / partial DRAWING) resumes seamlessly.
    reset_env();
    reader_next_chapter_idle(&ctx); // PARSED
    assert(load_chapter(&ctx, 1, 0, false) && test_fallback_loads == 0 && s_chapter == 1);

    reset_env();
    test_step_budget = 100;
    reader_next_chapter_idle(&ctx); // PARSED
    test_step_budget = 6;
    // 模拟 PARSED 推进一步进入部分 LAYOUT / Simulate partial LAYOUT after one step from PARSED
    lock_draw();
    assert(next_chapter_begin_layout_locked());
    assert(book_layout_stage_step(s_next_ch.layout, 1) == 0);
    unlock_draw();
    assert(s_next_ch.state == NEXT_CH_LAYOUT);
    assert(load_chapter(&ctx, 1, 0, false) && test_fallback_loads == 0 && s_chapter == 1);

    reset_env();
    test_draw_slices = 10;
    reader_next_chapter_idle(&ctx); // PARSED
    reader_next_chapter_idle(&ctx); // LAYOUT -> DRAWING
    assert(s_next_ch.state == NEXT_CH_READY || s_next_ch.state == NEXT_CH_DRAWING);
    // 构造已画两片的半途 DRAWING 状态 / Construct partial DRAWING state with slices already rendered
    reset_env();
    reader_next_chapter_idle(&ctx);
    lock_draw();
    assert(next_chapter_begin_layout_locked());
    while (book_layout_stage_step(s_next_ch.layout, 4) == 0) {}
    assert(reader_prefetch_buffer());
    s_next_ch.uses_next_fb = true;
    s_next_ch.state = NEXT_CH_DRAWING;
    assert(book_layout_stage_draw_slice(s_next_ch.layout, s_next_fb, 0, body_rect(), s_px, &s_next_ch.draw_cursor) == 0);
    unlock_draw();
    assert(load_chapter(&ctx, 1, 0, false));
    assert(s_prep_page == 0 && s_prep_cursor.started && s_prep_cursor.generation == s_layout_gen && s_next_page == -1);

    // 4. 回翻与 kick_prep 立即作废候选章；TXT 书同时提交映射。
    // 4. Backward turn and kick_prep invalidate candidate immediately; TXT commits navigation map on adopt.
    reset_env();
    test_kind = BOOK_KIND_TXT;
    reader_next_chapter_idle(&ctx); // PARSED
    reader_next_chapter_idle(&ctx); // READY
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.txt_map.mapped);
    s_back_turns = 1;
    assert(reader_next_chapter_target() == -1);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_IDLE);
    // 重建 TXT 候选后模拟回翻并被 kick_prep 接管 s_next_fb / Rebuild TXT candidate, then turn back and let kick_prep take s_next_fb
    s_back_turns = 0;
    reader_next_chapter_idle(&ctx);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.uses_next_fb);
    s_page = 3; s_back_turns = 1;
    (void)kick_prep();
    assert(s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.uses_next_fb);
    // 回到末页重建 TXT 候选并采用 / Rebuild TXT candidate on last page and adopt
    s_page = 4; s_back_turns = 0;
    reader_next_chapter_idle(&ctx);
    reader_next_chapter_idle(&ctx);
    assert(load_chapter(&ctx, 1, 0, false) && test_commit_map_calls == 1);

    // 5. 改字号/重排/换书/换字体/切换书内字体失效候选章并释放内存。
    // 5. Font-size/reflow/book/font/embedded-font changes invalidate candidate and free resources.
    reset_env();
    reader_next_chapter_idle(&ctx);
    uint32_t old_gen = s_next_ch.source_generation;
    ++s_layout_gen; // 改字号或边距重排 / Font size or margin reflow
    assert(!next_chapter_can_adopt(1));
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_PARSED && s_next_ch.source_generation == s_layout_gen &&
           s_next_ch.source_generation != old_gen && test_preload_calls == 2);

    reset_env();
    reader_next_chapter_idle(&ctx);
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/other.ttf");
    assert(!next_chapter_can_adopt(1));
    s_embedded_fonts = false;
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/font.ttf");
    assert(!next_chapter_can_adopt(1));
    s_embedded_fonts = true;
    copy_text(s_path, sizeof(s_path), "/sdcard/another.epub");
    assert(!next_chapter_can_adopt(1));

    // 6. PSRAM 不足进入 FAILED；大图解码让路优先释放候选章再释放保留页。
    // 6. Low PSRAM enters FAILED; image decode headroom reclaims staged candidate before retained pages.
    reset_env();
    test_free_psram = BOOK_NEXT_CH_RESERVE - 1024;
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_FAILED && !s_next_ch.layout && !s_next_ch.text.utf8);

    reset_env();
    reader_next_chapter_idle(&ctx); // PARSED
    test_free_psram = 2u * 1024u * 1024u;
    release_image_headroom();
    assert(s_next_ch.state == NEXT_CH_IDLE && test_keep_frees == 1);

    // 7. 首页含插图或设置阅读背景时跳过 s_next_fb 预绘，仍完成分页预排。
    // 7. Skip s_next_fb drawing when page 0 has images or reading background, while finishing pagination.
    reset_env();
    test_page0_image = true;
    reader_next_chapter_idle(&ctx);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_READY && !s_next_ch.uses_next_fb && !s_next_ch.page0_drawn);

    reset_env();
    test_bg = "/sdcard/bg.png";
    reader_next_chapter_idle(&ctx);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_READY && !s_next_ch.uses_next_fb && !s_next_ch.page0_drawn);

    // 8. 退出/换书（free_book 路径）清理。/ 8. Exit/book-switch (free_book path) cleanup.
    next_chapter_clear();
    free(s_text); free(s_next_fb);
    puts("PASS: Pico cross-chapter state machine, display-confirmation gate, partial resume, backward/reflow/OOM/image/bg guards");
    return 0;
}
'''
    c_src = temp / 'app_state_machine_test.c'
    c_src.write_text(unit)
    exe = temp / 'app_state_machine_test'
    subprocess.run([
        'cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
        '-g', '-fsanitize=address,undefined', str(c_src), '-o', str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)


def main():
    with tempfile.TemporaryDirectory(prefix='pico-chapter-preload-') as d:
        temp = Path(d)
        run_source_and_stage_test(temp)
        run_app_state_machine_test(temp)


if __name__ == '__main__':
    main()

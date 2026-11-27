#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：跨章预排主机回归（TXT/EPUB 隔离、确认门禁、往前提前开始、往回预排上一章末页、同章预绘优先、方向切换/重排/OOM/中途退出边界）。
English: Cross-chapter preload host regressions (TXT/EPUB isolation, commit gate, early forward start, backward previous-chapter last page, same-chapter prefetch priority, direction switch/reflow/OOM/exit bounds)."""
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

    // 3. 越章保留：commit_keep 把被替换的活动排版整体移成完成态候选；无排版时 kept 为空；候选可绘制且不动活动排版；可再提交回来。
    // 3. Keep across a crossing: commit_keep moves the replaced active layout into a finished stage; no layout means no kept stage; the stage draws without touching the active one and can be committed back.
    assert(book_open("{epub_path}") == ESP_OK);
    html_text_t ka = {{0}}, kb = {{0}};
    book_chapter_map_t kmap = {{0}};
    assert(book_chapter_load_blocks(0, &ka) == ESP_OK);
    assert(book_chapter_preload(1, &kb, &kmap) == ESP_OK);
    int dims_a = 0, dims_b = 0;
    book_layout_stage_params_t pb = {{
        .utf8 = kb.utf8, .len = kb.len, .blocks = kb.blocks, .block_count = kb.count,
        .runs = kb.runs, .run_count = kb.run_count, .inline_notes = kb.notes, .inline_note_count = kb.note_count,
        .rect = rect, .px = 24,
    }};
    book_layout_stage_params_t pa = {{
        .utf8 = ka.utf8, .len = ka.len, .blocks = ka.blocks, .block_count = ka.count,
        .runs = ka.runs, .run_count = ka.run_count, .inline_notes = ka.notes, .inline_note_count = ka.note_count,
        .rect = rect, .px = 24,
    }};
    book_layout_free();
    assert(book_layout_stage_detach_active(&dims_a) == NULL); // 空活动排版 / empty active layout
    book_layout_stage_t *sb = book_layout_stage_begin(&pb);
    assert(sb);
    do {{ res = book_layout_stage_step(sb, 4); assert(res >= 0); }} while (!res);
    size_t pages_b = book_layout_stage_page_count(sb);
    book_layout_stage_t *kept = (book_layout_stage_t *)0x1;
    assert(!book_layout_stage_commit_keep(NULL, &dims_b, &dims_a, &kept) && kept == NULL);
    assert(book_layout_stage_commit_keep(sb, &dims_b, &dims_a, &kept));
    assert(kept == NULL && book_layout_page_count() == pages_b); // 没有可留的排版 / nothing to keep
    book_layout_stage_t *sa = book_layout_stage_begin(&pa);
    assert(sa);
    do {{ res = book_layout_stage_step(sa, 4); assert(res >= 0); }} while (!res);
    size_t pages_a = book_layout_stage_page_count(sa);
    uint32_t gen_before = book_layout_generation();
    assert(book_layout_stage_commit_keep(sa, &dims_a, &dims_b, &kept));
    assert(kept && book_layout_stage_page_count(kept) == pages_b && book_layout_page_count() == pages_a);
    assert(book_layout_generation() != gen_before);
    // 候选可以直接绘制末页，活动排版与代次不变。/ The kept stage draws its last page without touching the active layout or generation.
    {{
        uint32_t gen_active = book_layout_generation();
        unsigned calls_before = s_drawn_calls;
        uint8_t kfb[4] = {{0xff, 0xff, 0xff, 0xff}};
        book_layout_draw_cursor_t kc = {{0}};
        do {{ res = book_layout_stage_draw_slice(kept, kfb, pages_b - 1, rect, 24, &kc); assert(res >= 0); }} while (!res);
        assert(s_drawn_calls > calls_before && book_layout_generation() == gen_active && book_layout_page_count() == pages_a);
    }}
    // 再提交回来：被留下的排版重新成为活动排版，页数一致。/ Committing it back makes the kept layout active again with the same page count.
    assert(book_layout_stage_commit(kept, &dims_b));
    assert(book_layout_page_count() == pages_b);
    book_layout_free();
    html_text_free(&ka);
    html_text_free(&kb);
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
typedef struct { void *hl; uint8_t *fb; bool consumed, request_app, request_menu, request_return; touch_t *touch; int leaf; } app_ctx_t;
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
    bool done, fail_step, fail_draw;
    int img_page;
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
static int test_img_page = -1;
static size_t test_stage_pages = 3, test_last_draw_page = SIZE_MAX, test_preload_last = SIZE_MAX;
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
static int reader_prefetch_target(void);
static bool reader_back_chapter_trend(void);
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
    test_preload_last = ch;
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
static unsigned test_begin_calls = 0, test_step_calls = 0;
static book_layout_stage_t *book_layout_stage_begin(const book_layout_stage_params_t *p) {
    (void)p;
    ++test_begin_calls;
    if (test_stage_fail_begin) return NULL;
    book_layout_stage_t *s = calloc(1, sizeof(*s));
    s->pages = test_stage_pages;
    s->img_page = test_page0_image ? 0 : test_img_page;
    return s;
}
static int book_layout_stage_step(book_layout_stage_t *s, unsigned max_steps) {
    (void)max_steps;
    ++test_step_calls;
    if (!s || test_stage_fail_step) { if (s) s->fail_step = true; return -1; }
    if (s->done) return 1;
    if (++s->step_calls >= test_step_budget) { s->done = true; return 1; }
    return 0;
}
static size_t book_layout_stage_page_count(const book_layout_stage_t *s) { return s ? s->pages : 0; }
static int book_layout_stage_page_image_count(const book_layout_stage_t *s, size_t p) { return s && (int)p == s->img_page ? 1 : 0; }
static int book_layout_stage_page_image(const book_layout_stage_t *s, size_t p) { return s && (int)p == s->img_page ? 0 : -1; }
static int book_layout_stage_draw_slice(book_layout_stage_t *s, uint8_t *fb, size_t p, EpdRect r, int px, book_layout_draw_cursor_t *c) {
    (void)r; (void)px;
    if (!s || test_stage_fail_draw) return -1;
    test_last_draw_page = p;
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
// 活动排版桩：detach 把当前页数搬进新完成态候选并清空，commit_keep = detach + commit。
// Active-layout stub: detach moves the current page count into a new finished stage and empties it; commit_keep = detach + commit.
static unsigned test_detach_calls = 0, test_stage_frees = 0;
static bool test_detach_fail = false;
static void *test_last_detach_ctx = NULL;
static book_layout_stage_t *book_layout_stage_detach_active(void *dims) {
    if (!s_layout_pages || test_detach_fail) return NULL;
    book_layout_stage_t *k = calloc(1, sizeof(*k));
    k->pages = s_layout_pages;
    k->done = true;
    k->img_page = -1;
    s_layout_pages = 0;
    ++s_layout_gen;
    ++test_detach_calls;
    test_last_detach_ctx = dims;
    return k;
}
static bool book_layout_stage_commit_keep(book_layout_stage_t *s, void *dims, void *kept_dims, book_layout_stage_t **kept) {
    if (kept) *kept = NULL;
    if (!s || !s->done) return false;
    if (kept) *kept = book_layout_stage_detach_active(kept_dims);
    return book_layout_stage_commit(s, dims);
}
static void book_layout_stage_free(book_layout_stage_t *s) { if (s) ++test_stage_frees; free(s); }
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
// 冷加载（load_chapter_at 真实代码）所需桩：活动排版由 s_layout_pages 表示，build 失败只失败一次。
// Stubs for the real cold-load code: the active layout is s_layout_pages; build fails once when asked.
static bool test_build_fail_once = false, test_build_fail_always = false;
static size_t test_new_pages = 7, test_old_pages_for_restore = 5, test_part_more_load = 0;
static unsigned test_build_calls = 0, test_free_book_calls = 0;
static bool test_build_failed = false;
static void book_layout_set_runs(const html_run_t *r, size_t c) { (void)r; (void)c; }
static void book_layout_set_inline_images(const html_note_t *n, size_t c) { (void)n; (void)c; }
static void book_layout_set_chapter_lead(size_t s, unsigned h) { (void)s; (void)h; }
static void book_layout_set_chapter_lead_width(unsigned w) { (void)w; }
static size_t book_layout_page_for_offset(size_t off) { (void)off; return 0; }
static void apply_chapter_lead(const char *t, const char *l, size_t skip, unsigned height) {
    (void)t; (void)l; s_chapter_lead_skip = skip; s_chapter_lead_height = height; s_chapter_lead_width = 0;
}
static bool book_layout_build_blocks(const char *u, size_t len, const blk_t *b, size_t n, EpdRect r, int px) {
    (void)u; (void)len; (void)b; (void)n; (void)r; (void)px;
    ++test_build_calls;
    s_layout_pages = 0; ++s_layout_gen;
    if (test_build_fail_always || test_build_fail_once) { test_build_fail_once = false; test_build_failed = true; return false; }
    s_layout_pages = test_build_failed ? test_old_pages_for_restore : test_new_pages;
    return true;
}
static esp_err_t book_chapter_load_part(size_t ch, size_t off, bool last, const char *a, size_t so, size_t *local, html_text_t *out) {
    (void)ch; (void)off; (void)last; (void)a; (void)so; *local = 0;
    memset(out, 0, sizeof(*out));
    out->utf8 = strdup("Cold loaded text"); out->len = strlen(out->utf8);
    out->part_more = test_part_more_load != 0;
    return test_preload_err;
}
static void free_book(void) { ++test_free_book_calls; }
'''
    unit += function('save_progress', BOOK) + '\n'
    unit += function('reader_back_chapter_trend', BOOK) + '\n'
    unit += function('reader_prefetch_target', BOOK) + '\n'
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
    test_img_page = -1; test_stage_pages = 3; test_last_draw_page = SIZE_MAX; test_preload_last = SIZE_MAX;
    s_part_base = 0; s_keep_cur.fb = NULL;
    free(s_next_fb); s_next_fb = NULL;
    test_bg = "";
    test_push_err = EPD_DRAW_SUCCESS;
    test_preload_calls = test_commit_map_calls = test_save_calls = test_fallback_loads = 0;
    test_detach_calls = test_stage_frees = 0; test_detach_fail = false; test_last_detach_ctx = NULL;
    test_begin_calls = test_step_calls = 0;
    test_build_fail_once = test_build_fail_always = test_build_failed = false;
    test_new_pages = 7; test_old_pages_for_restore = 5; test_part_more_load = 0;
    test_build_calls = test_free_book_calls = 0;
    s_chapter_lead_skip = 0; s_chapter_lead_row_height = s_chapter_lead_height = s_chapter_lead_width = 0;
    s_chapter_heading_title[0] = s_chapter_heading_label[0] = 0;
    copy_text(s_title, sizeof(s_title), "Ch0");
    test_keep_frees = test_cover_invalidates = test_font_cache_clears = 0;
    test_step_budget = 6; test_draw_slices = 4;
    memset(&test_saved_progress, 0, sizeof(test_saved_progress));
}

static int plan_target(void) {
    int dir; bool draw_ok, work_ok;
    return reader_next_chapter_plan(&dir, &draw_ok, &work_ok);
}
static void run_idle(app_ctx_t *ctx, int n) { for (int i = 0; i < n; ++i) reader_next_chapter_idle(ctx); }

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
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.target_drawn && s_chapter == 0);
    // 预排完成后若未进章就退出/存盘，必须保持旧章位置。/ Saving or exiting before adopting keeps the prior chapter position.
    save_progress();
    assert(test_saved_progress.chapter == 0);

    // 2. 采用新章 + 推屏成功才确认进度的门禁。/ 2. Display-confirmation gate: persist new chapter only after display succeeds.
    assert(load_chapter(&ctx, 1, 0, false));
    assert(test_fallback_loads == 0 && s_chapter == 1 && s_page == 0 && s_chapter_display_pending);
    assert(s_next_page == 0 && s_next_generation == s_layout_gen);
    // 越过边界后刚离开的第 0 章作为反向候选留下（不再解析/排版）。/ The just-left chapter 0 stays as the reverse candidate after the crossing.
    assert(s_next_ch.kept && s_next_ch.chapter == 0 && s_next_ch.dir == -1 && s_next_ch.state == NEXT_CH_LAYOUT_DONE);
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
    assert(plan_target() == -1);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_IDLE);
    // 重建 TXT 候选后模拟回翻并被 kick_prep 接管 s_next_fb / Rebuild TXT candidate, then turn back and let kick_prep take s_next_fb
    s_back_turns = 0;
    reader_next_chapter_idle(&ctx);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.uses_next_fb);
    s_page = 3; s_back_turns = 2;
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
    assert(!next_chapter_can_adopt(1, 1));
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_PARSED && s_next_ch.source_generation == s_layout_gen &&
           s_next_ch.source_generation != old_gen && test_preload_calls == 2);

    reset_env();
    reader_next_chapter_idle(&ctx);
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/other.ttf");
    assert(!next_chapter_can_adopt(1, 1));
    s_embedded_fonts = false;
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/font.ttf");
    assert(!next_chapter_can_adopt(1, 1));
    s_embedded_fonts = true;
    copy_text(s_path, sizeof(s_path), "/sdcard/another.epub");
    assert(!next_chapter_can_adopt(1, 1));

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
    assert(s_next_ch.state == NEXT_CH_READY && !s_next_ch.uses_next_fb && !s_next_ch.target_drawn);

    reset_env();
    test_bg = "/sdcard/bg.png";
    reader_next_chapter_idle(&ctx);
    reader_next_chapter_idle(&ctx);
    assert(s_next_ch.state == NEXT_CH_READY && !s_next_ch.uses_next_fb && !s_next_ch.target_drawn);

    // 9. 往前提前开始：距章末 <=3 页且同章下一页预绘完成才解析/排版，不占 s_next_fb，末页才预绘。
    // 9. Early forward start: parse/layout from <=3 pages before the end once the same-chapter prefetch is done; never touches s_next_fb; drawing waits for the last page.
    reset_env();
    assert(reader_prefetch_buffer());
    memset(s_next_fb, 0x5a, fb_bytes());
    s_page = 0; // 剩余 4 页：太早 / 4 pages left: too early
    run_idle(&ctx, 3);
    assert(s_next_ch.state == NEXT_CH_IDLE && test_preload_calls == 0);
    s_page = 2; // 剩余 2 页，但同章第 3 页预绘未完成：让路 / 2 left, but same-chapter page 3 not drawn: yield
    s_prep_page = 3; s_next_page = -1;
    run_idle(&ctx, 3);
    assert(s_next_ch.state == NEXT_CH_IDLE && test_preload_calls == 0);
    s_next_page = 3; s_next_generation = s_layout_gen; // 预绘完成 / prefetch finished
    run_idle(&ctx, 6);
    assert(s_next_ch.state == NEXT_CH_LAYOUT_DONE && s_next_ch.dir == 1 && test_preload_calls == 1);
    assert(!s_next_ch.uses_next_fb && test_last_draw_page == SIZE_MAX);
    for (size_t i = 0; i < fb_bytes(); ++i) assert(s_next_fb[i] == 0x5a);
    assert(s_next_page == 3 && s_prep_page == 3);
    // 同章预绘重新开始（翻到下一页）时，只解析/排版的候选保留，且等预绘完成前不再推进。
    // When the next same-chapter prefetch starts, a parse/layout-only candidate is kept and idles until it finishes.
    reset_env();
    assert(reader_prefetch_buffer());
    s_page = 2; s_next_page = 3; s_next_generation = s_layout_gen; s_prep_page = 3;
    run_idle(&ctx, 1);
    assert(s_next_ch.state == NEXT_CH_PARSED);
    s_page = 3; // 目标第 4 页未画 / target page 4 not drawn
    (void)kick_prep();
    assert(s_next_ch.state == NEXT_CH_PARSED && s_prep_page == 4);
    run_idle(&ctx, 3);
    assert(s_next_ch.state == NEXT_CH_PARSED && test_preload_calls == 1);
    s_next_page = 4; s_next_generation = s_layout_gen; // 预绘完成后才继续 / continue only after prefetch finishes
    run_idle(&ctx, 4);
    assert(s_next_ch.state == NEXT_CH_LAYOUT_DONE && !s_next_ch.uses_next_fb);
    // 翻到末页：同章无预绘目标，才借用 s_next_fb 预绘首页。/ On the last page (no same-chapter target) page 0 is drawn into s_next_fb.
    s_page = 4;
    run_idle(&ctx, 2);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.target_drawn && s_next_ch.uses_next_fb);
    assert(test_last_draw_page == 0 && test_preload_calls == 1);
    assert(load_chapter(&ctx, 1, 0, false) && test_fallback_loads == 0 && s_page == 0 && s_next_page == 0);

    // 10. 往回预排：第 0 页 + 往回趋势，解析/排版上一章并预绘末页；采用后落在末页。
    // 10. Backward preload: page 0 + backward trend lays out the previous chapter and pre-draws its last page; adoption lands on it.
    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2; test_stage_pages = 4;
    assert(reader_prefetch_target() == -1 && plan_target() == 1);
    run_idle(&ctx, 6);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.dir == -1 && s_next_ch.chapter == 1);
    assert(test_preload_last == 1 && s_next_ch.target_page == 3 && s_next_ch.target_drawn);
    assert(test_last_draw_page == 3);
    assert(!kick_prep() && s_next_ch.state == NEXT_CH_READY); // 页 0 无同章目标，不清候选 / no same-chapter target keeps the candidate
    save_progress();
    assert(test_saved_progress.chapter == 2);
    assert(!next_chapter_can_adopt(1, 1) && next_chapter_can_adopt(1, -1));
    assert(load_chapter(&ctx, 1, 0, true));
    assert(test_fallback_loads == 0 && s_chapter == 1 && s_page == 3 && s_jump_page == 3);
    assert(s_chapter_display_pending && s_next_page == 3 && s_prep_page == 3 &&
           s_next_generation == s_layout_gen);
    assert(s_next_ch.kept && s_next_ch.chapter == 2 && s_next_ch.dir == 1 && s_next_ch.target_page == 0);
    // 推屏失败不确认；成功后才保存末页位置。/ A failed present does not confirm; success persists the last-page position.
    test_push_err = EPD_DRAW_ERROR;
    assert(present(&ctx, APP_REDRAW_AREA));
    assert(s_chapter_display_pending);
    save_progress();
    assert(test_saved_progress.chapter == 2);
    test_push_err = EPD_DRAW_SUCCESS;
    assert(present(&ctx, APP_REDRAW_AREA));
    assert(!s_chapter_display_pending);
    save_progress();
    assert(test_saved_progress.chapter == 1 && test_saved_progress.byte_off == 30);
    // 与现画一致：同样落在末页（现画路径 s_layout_pages-1）。/ Same landing page as a fresh load.
    {
        size_t adopted_page = s_page;
        reset_env();
        s_chapter = 2; s_page = 0; s_back_turns = 2; test_stage_pages = 4;
        s_layout_pages = 4; // 桩：现画后的页数 / stub: page count after a fresh build
        assert(load_chapter(&ctx, 1, 0, true) && test_fallback_loads == 1);
        assert(s_page == s_layout_pages - 1 && s_page == adopted_page);
    }

    // 11. 往回预排的边界：趋势、章号、分段流、工具栏、方向、同时只有一个候选。
    // 11. Backward bounds: trend, chapter, part streams, toolbar, direction, one candidate at a time.
    reset_env();
    s_chapter = 2; s_page = 0;
    s_back_turns = 0; assert(plan_target() == -1);
    s_back_turns = 1; assert(plan_target() == -1); // 无保留页需连续两次 / needs two turns without kept pages
    s_keep_cur.fb = malloc(8); assert(plan_target() == 1); free(s_keep_cur.fb); s_keep_cur.fb = NULL;
    s_back_turns = 2; assert(plan_target() == 1);
    s_chapter = 0; assert(plan_target() == -1);
    s_chapter = 2; s_part_base = 5; assert(plan_target() == -1); s_part_base = 0;
    s_part_more = true; assert(plan_target() == -1); s_part_more = false;
    s_toolbar = true; assert(plan_target() == -1); s_toolbar = false;
    s_page = 1; assert(plan_target() == -1); // 非第 0 页：同章往回预绘 / mid-chapter: same-chapter backward prefetch
    s_page = 0;
    // 往回候选遇到往前趋势：释放旧候选，只剩一个方向。/ A forward trend releases the backward candidate.
    run_idle(&ctx, 6);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.dir == -1 && s_next_ch.uses_next_fb);
    s_back_turns = 0;
    run_idle(&ctx, 1);
    assert(s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.uses_next_fb && s_next_page == -1);
    s_page = 4; // 往前：末页触发下一章 / forward: last page targets the next chapter
    run_idle(&ctx, 6);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.dir == 1 && s_next_ch.chapter == 3);
    // 反向：往前候选遇到往回趋势。/ And a backward trend releases the forward candidate.
    s_page = 0; s_back_turns = 2;
    run_idle(&ctx, 1);
    assert(s_next_ch.state == NEXT_CH_IDLE || s_next_ch.dir == -1);
    run_idle(&ctx, 6);
    assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.dir == -1 && s_next_ch.chapter == 1);

    // 12. 往回采用：排版未完成时同步排完再现画末页；末页含插图不预绘；方向/代次/字体/书变化不采用。
    // 12. Backward adoption with unfinished layout lays out synchronously; image last page is not pre-drawn; direction/generation/font/book changes are not adopted.
    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2; test_stage_pages = 5;
    run_idle(&ctx, 1); // PARSED
    assert(s_next_ch.state == NEXT_CH_PARSED);
    assert(load_chapter(&ctx, 1, 0, true) && test_fallback_loads == 0 && s_page == 4 && s_next_page == -1);
    assert(s_chapter_display_pending);

    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2; test_stage_pages = 3; test_img_page = 2;
    run_idle(&ctx, 4);
    assert(s_next_ch.state == NEXT_CH_READY && !s_next_ch.uses_next_fb && s_next_ch.target_has_images);
    assert(load_chapter(&ctx, 1, 0, true) && s_page == 2 && s_next_page == -1);

    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2;
    run_idle(&ctx, 6);
    assert(next_chapter_can_adopt(1, -1));
    ++s_layout_gen; assert(!next_chapter_can_adopt(1, -1)); --s_layout_gen;
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/other.ttf"); assert(!next_chapter_can_adopt(1, -1));
    copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/font.ttf"); assert(next_chapter_can_adopt(1, -1));
    s_embedded_fonts = false; assert(!next_chapter_can_adopt(1, -1)); s_embedded_fonts = true;
    copy_text(s_path, sizeof(s_path), "/sdcard/other.epub"); assert(!next_chapter_can_adopt(1, -1));
    copy_text(s_path, sizeof(s_path), "/sdcard/book.epub");
    assert(!next_chapter_can_adopt(0, -1) && !next_chapter_can_adopt(3, -1));
    // 重排后候选被释放并按新代次重建。/ A reflow releases it and rebuilds under the new generation.
    ++s_layout_gen;
    run_idle(&ctx, 1);
    assert(s_next_ch.state == NEXT_CH_PARSED && s_next_ch.source_generation == s_layout_gen && s_next_ch.dir == -1);
    // 采用前提交失败回退：层叠失败时清候选并走现画。/ Failed commit clears the candidate and falls back to a fresh load.
    test_stage_fail_step = true;
    assert(load_chapter(&ctx, 1, 0, true) && test_fallback_loads == 1 && s_next_ch.state == NEXT_CH_IDLE);
    test_stage_fail_step = false;

    // 13. 内存不足：往回预排进入 FAILED 且不反复重试；采用时走现画。
    // 13. Low memory: backward preload enters FAILED without retry loops; adoption falls back to a fresh load.
    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2;
    test_free_psram = BOOK_NEXT_CH_RESERVE - 1024;
    run_idle(&ctx, 3);
    assert(s_next_ch.state == NEXT_CH_FAILED && s_next_ch.dir == -1 && test_preload_calls == 0);
    assert(!next_chapter_can_adopt(1, -1));
    assert(load_chapter(&ctx, 1, 0, true) && test_fallback_loads == 1);
    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2;
    test_preload_err = ESP_ERR_NO_MEM;
    run_idle(&ctx, 3);
    assert(s_next_ch.state == NEXT_CH_FAILED && test_preload_calls == 1);
    run_idle(&ctx, 3); // 失败标记保持，不反复重试 / the failure marker holds without retries
    assert(s_next_ch.state == NEXT_CH_FAILED && test_preload_calls == 1);

    // 14. 触摸待处理与阅读背景：往回预排同样让路/不预绘。
    // 14. Pending touch yields and a reading background skips drawing for backward preload too.
    reset_env();
    s_chapter = 2; s_page = 0; s_back_turns = 2;
    test_touch_pending = true;
    run_idle(&ctx, 2);
    assert(s_next_ch.state == NEXT_CH_IDLE);
    test_touch_pending = false;
    test_bg = "/sdcard/bg.png";
    run_idle(&ctx, 6);
    assert(s_next_ch.state == NEXT_CH_READY && !s_next_ch.uses_next_fb && !s_next_ch.target_drawn);


    // 15. 越章保留：离开的相邻章（已解析并排完版）留作反向候选，立即折返不再解析/排版。
    // 15. Keep across a crossing: the just-left adjacent chapter (parsed and laid out) stays as the reverse candidate, so an immediate reversal needs no parse or layout.
    {
        // 往前越界：第 0 章末页采用第 1 章首页，第 0 章留作往回候选（目标页 = 它的末页）。
        // Forward crossing: chapter 0's last page adopts chapter 1 page 0, and chapter 0 stays as the backward candidate landing on its last page.
        reset_env();
        test_stage_pages = 10;
        assert(reader_prefetch_buffer());
        s_page = 4;
        run_idle(&ctx, 8);
        assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.dir == 1);
        assert(load_chapter(&ctx, 1, 0, false) && test_fallback_loads == 0 && s_chapter == 1 && s_page == 0);
        assert(s_next_ch.kept && s_next_ch.chapter == 0 && s_next_ch.dir == -1 && s_next_ch.target_page == 4);
        assert(s_next_ch.state == NEXT_CH_LAYOUT_DONE && !s_next_ch.uses_next_fb && !s_next_ch.target_drawn);
        assert(test_detach_calls == 1 && test_last_detach_ctx == &s_next_ch.dims_ctx && s_next_ch.dims_ctx.chapter == 0);
        assert(s_next_ch.text.utf8 && !strcmp(s_next_ch.text.utf8, "active") && s_next_ch.text.len == 6);
        assert(s_next_ch.layout && book_layout_stage_page_count(s_next_ch.layout) == 5);
        assert(!strcmp(s_next_ch.path, s_path) && s_next_ch.source_generation == s_layout_gen);
        assert(!strcmp(s_next_ch.title, "Ch0") && s_next_ch.heading_title[0] == 0);
        assert(!strcmp(s_text, "Preloaded chapter text") && s_layout_pages == 10);
        // 常规规划没有候选：留下的候选在边界页保留，不再解析、排版或占用 s_next_fb。
        // The normal plan wants nothing: the kept candidate stays on the boundary page and never parses, lays out or borrows s_next_fb.
        memset(s_next_fb, 0x5a, fb_bytes());
        const unsigned parse0 = test_preload_calls, begin0 = test_begin_calls, step0 = test_step_calls;
        assert(plan_target() == 0 && s_back_turns == 0);
        run_idle(&ctx, 4);
        assert(s_next_ch.kept && s_next_ch.state == NEXT_CH_LAYOUT_DONE && !s_next_ch.uses_next_fb);
        assert(test_preload_calls == parse0 && test_begin_calls == begin0 && test_step_calls == step0);
        for (size_t i = 0; i < fb_bytes(); ++i) assert(s_next_fb[i] == 0x5a);
        // 同章预绘照常占 s_next_fb，不清除只做排版的保留候选。/ Same-chapter prefetch still owns s_next_fb and does not clear the layout-only kept candidate.
        (void)kick_prep();
        assert(s_prep_page == 1 && s_next_ch.kept && s_next_ch.state == NEXT_CH_LAYOUT_DONE);
        // 立即折返：采用保留的第 0 章，不调用解析/排版，落在原末页。
        // Immediate reversal adopts chapter 0 with no parse or layout and lands on its old last page.
        s_back_turns = 1;
        assert(load_chapter(&ctx, 0, 0, true));
        assert(test_fallback_loads == 0 && test_preload_calls == parse0 && test_begin_calls == begin0 && test_step_calls == step0);
        assert(s_chapter == 0 && s_page == 4 && s_jump_page == 4 && s_layout_pages == 5 && s_chapter_display_pending);
        assert(!strcmp(s_text, "active") && s_text_len == 6 && !strcmp(s_title, "Ch0") && !s_chapter_heading_title[0]);
        assert(s_next_page == -1); // 未预绘，由调用方现画 / not pre-drawn; the caller draws it
        // 原来的第 1 章又成为往前候选（目标页 0），再次折返同样不解析/排版。
        // Chapter 1 becomes the forward candidate (landing on page 0), and reversing again also skips parse and layout.
        assert(s_next_ch.kept && s_next_ch.chapter == 1 && s_next_ch.dir == 1 && s_next_ch.target_page == 0);
        assert(!strcmp(s_next_ch.text.utf8, "Preloaded chapter text") && !strcmp(s_next_ch.heading_title, "Heading"));
        assert(book_layout_stage_page_count(s_next_ch.layout) == 10 && test_detach_calls == 2);
        s_back_turns = 0;
        assert(load_chapter(&ctx, 1, 0, false));
        assert(test_fallback_loads == 0 && test_preload_calls == parse0 && test_begin_calls == begin0 && test_step_calls == step0);
        assert(s_chapter == 1 && s_page == 0 && s_layout_pages == 10 && !strcmp(s_text, "Preloaded chapter text"));
        assert(s_next_ch.kept && s_next_ch.chapter == 0 && s_next_ch.dir == -1 && s_next_ch.target_page == 4);
    }
    {
        // 往回越界：第 2 章首页往回落在第 1 章末页，第 2 章留作往前候选（目标页 0）；末页上保留，折返不排版。
        // Backward crossing: chapter 2 page 0 lands on chapter 1's last page and stays as the forward candidate (page 0); it is retained on the last page and the reversal skips layout.
        reset_env();
        s_chapter = 2; s_page = 0; s_back_turns = 2; test_stage_pages = 4;
        run_idle(&ctx, 6);
        assert(s_next_ch.state == NEXT_CH_READY && s_next_ch.dir == -1);
        assert(load_chapter(&ctx, 1, 0, true) && s_chapter == 1 && s_page == 3);
        assert(s_next_ch.kept && s_next_ch.chapter == 2 && s_next_ch.dir == 1 && s_next_ch.target_page == 0);
        const unsigned parse0 = test_preload_calls, begin0 = test_begin_calls;
        run_idle(&ctx, 3); // 往回趋势仍在：常规规划不要候选，保留规则接手 / the backward trend still blocks forward plans; the keep rule takes over
        assert(s_next_ch.kept && s_next_ch.chapter == 2 && test_preload_calls == parse0 && test_begin_calls == begin0);
        s_back_turns = 0;
        assert(load_chapter(&ctx, 2, 0, false));
        assert(test_fallback_loads == 0 && test_preload_calls == parse0 && test_begin_calls == begin0);
        assert(s_chapter == 2 && s_page == 0 && s_layout_pages == 5 && !strcmp(s_text, "active"));
        assert(s_next_ch.kept && s_next_ch.chapter == 1 && s_next_ch.dir == -1 && s_next_ch.target_page == 3);
    }
    {
        // 确认门禁：采用保留候选后，推屏失败不确认新位置，成功后才保存。
        // Confirm gate: after adopting the kept candidate a failed present does not confirm the position; success persists it.
        reset_env();
        test_stage_pages = 10;
        assert(reader_prefetch_buffer());
        s_page = 4;
        run_idle(&ctx, 8);
        assert(load_chapter(&ctx, 1, 0, false));
        assert(present(&ctx, APP_REDRAW_AREA) && !s_chapter_display_pending);
        save_progress();
        assert(test_saved_progress.chapter == 1);
        assert(s_next_ch.kept && load_chapter(&ctx, 0, 0, true) && s_chapter_display_pending);
        test_push_err = EPD_DRAW_ERROR;
        assert(present(&ctx, APP_REDRAW_AREA) && s_chapter_display_pending);
        save_progress();
        assert(test_saved_progress.chapter == 1); // 失败时仍是旧位置 / still the old position
        test_push_err = EPD_DRAW_SUCCESS;
        assert(present(&ctx, APP_REDRAW_AREA) && !s_chapter_display_pending);
        save_progress();
        assert(test_saved_progress.chapter == 0 && test_saved_progress.byte_off == 40);
    }
    {
        // 离开边界页、规划另选候选、代次/字体/书变化、内存不足：释放保留候选。
        // Moving off the boundary page, a normal plan wanting another candidate, generation/font/book changes and low memory all release the kept candidate.
        for (int variant = 0; variant < 9; ++variant) {
            reset_env();
            test_stage_pages = 10;
            assert(reader_prefetch_buffer());
            s_page = 4;
            run_idle(&ctx, 8);
            assert(load_chapter(&ctx, 1, 0, false) && s_next_ch.kept);
            const unsigned frees0 = test_stage_frees;
            switch (variant) {
            case 0: s_page = 1; break;                                                        // 离开第 0 页 / left page 0
            case 1: s_chapter = 2; break;                                                     // 越到别的章 / another chapter
            case 2: ++s_layout_gen; break;                                                    // 重排 / reflow
            case 3: copy_text(s_cur_font, sizeof(s_cur_font), "/sdcard/other.ttf"); break;    // 换字体 / font change
            case 4: s_embedded_fonts = false; break;                                          // 书内字体开关 / embedded-font setting
            case 5: copy_text(s_path, sizeof(s_path), "/sdcard/other.epub"); break;           // 换书 / book change
            case 6: test_free_psram = BOOK_NEXT_CH_RESERVE - 1024; break;                     // 内存不足 / low memory
            case 7: s_toolbar = true; break;                                                  // 工具栏 / toolbar
            default: s_part_more = true; break;                                               // 分段流 / part stream
            }
            run_idle(&ctx, 2);
            assert(!s_next_ch.kept && s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.layout && !s_next_ch.text.utf8);
            assert(test_stage_frees == frees0 + 1 && !next_chapter_can_adopt(0, -1));
            s_part_more = false;
        }
        // 往前越界的反向保留候选同理：往回越界后离开末页即释放。
        // The same for a candidate kept after a backward crossing: leaving the last page releases it.
        reset_env();
        s_chapter = 2; s_page = 0; s_back_turns = 2; test_stage_pages = 4;
        run_idle(&ctx, 6);
        assert(load_chapter(&ctx, 1, 0, true) && s_next_ch.kept && s_next_ch.dir == 1);
        s_page = 2;
        run_idle(&ctx, 2);
        assert(!s_next_ch.kept && s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.text.utf8);
        // 短章：第 0 页常规规划就要往前候选，别的候选让位（同时只有一个）。
        // Short chapter: the normal plan already wants a forward candidate on page 0, so the kept one yields (one candidate at a time).
        reset_env();
        test_stage_pages = 3;
        assert(reader_prefetch_buffer());
        s_page = 4;
        run_idle(&ctx, 8);
        assert(load_chapter(&ctx, 1, 0, false) && s_next_ch.kept);
        run_idle(&ctx, 1); // 保留候选让位；同章下一页未预绘，新候选稍后才解析 / yields; the new candidate waits for the same-chapter prefetch
        assert(!s_next_ch.kept && s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.text.utf8);
        s_next_page = 1; s_next_generation = s_layout_gen;
        run_idle(&ctx, 1);
        assert(!s_next_ch.kept && s_next_ch.chapter == 2 && s_next_ch.dir == 1 && s_next_ch.state == NEXT_CH_PARSED);
    }
    {
        // 往回趋势成立时，保留候选可以借 s_next_fb 预绘目标末页，不重新解析。
        // With a backward trend the kept candidate may borrow s_next_fb to pre-draw its landing page, without re-parsing.
        reset_env();
        test_stage_pages = 10;
        assert(reader_prefetch_buffer());
        s_page = 4;
        run_idle(&ctx, 8);
        assert(load_chapter(&ctx, 1, 0, false) && s_next_ch.kept);
        const unsigned parse0 = test_preload_calls, begin0 = test_begin_calls;
        s_back_turns = 2;
        run_idle(&ctx, 6);
        assert(s_next_ch.kept && s_next_ch.state == NEXT_CH_READY && s_next_ch.uses_next_fb && s_next_ch.target_drawn);
        assert(test_last_draw_page == 4 && test_preload_calls == parse0 && test_begin_calls == begin0);
        assert(load_chapter(&ctx, 0, 0, true) && test_fallback_loads == 0 && s_page == 4 && s_next_page == 4);
    }
    {
        // 不保留：低内存、分段流、TXT、搬出失败；采用本身仍成功。
        // Not kept: low memory, part streams, TXT and a failed move; adoption itself still succeeds.
        for (int variant = 0; variant < 5; ++variant) {
            reset_env();
            test_stage_pages = 10;
            assert(reader_prefetch_buffer());
            if (variant == 3) test_kind = BOOK_KIND_TXT;
            s_page = 4;
            run_idle(&ctx, 8);
            assert(s_next_ch.state == NEXT_CH_READY);
            if (variant == 0) test_free_psram = BOOK_NEXT_CH_RESERVE - 1024;
            if (variant == 1) s_part_more = true;
            if (variant == 2) s_part_base = 5;
            if (variant == 4) test_detach_fail = true;
            assert(load_chapter(&ctx, 1, 0, false) && test_fallback_loads == 0 && s_chapter == 1);
            assert(!s_next_ch.kept && s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.layout && !s_next_ch.text.utf8);
            assert(test_detach_calls == 0u && s_layout_pages == 10);
        }
    }

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
    cc = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
          '-g', '-fsanitize=address,undefined']
    subprocess.run(cc + [str(c_src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

    # 冷加载路径：直接编译生产 load_chapter_at（桩只替换排版/解析后端），验证搬出→成功留存/失败回滚。
    # Cold-load path: compile the production load_chapter_at (stubs replace only the layout/parse back ends) to verify detach → keep on success / rollback on failure.
    cold_fn = function('load_chapter_at', BOOK).replace('static bool load_chapter_at(', 'static bool load_chapter_at_real(', 1)
    common = unit[:unit.index('int main(void) {')]
    cold = common + cold_fn + r'''
int main(void) {
    touch_t touch = {0};
    uint8_t fb_mem[64] = {0};
    app_ctx_t ctx = {.hl = (void*)1, .fb = fb_mem, .touch = &touch};

    // 往前冷加载：第 0 章（已排完版）留作往回候选，题头几何取调用前的旧值。
    // Forward cold load: chapter 0 (laid out) stays as the backward candidate, with the heading geometry from before the call.
    reset_env();
    assert(reader_prefetch_buffer());
    s_chapter_lead_skip = 3; s_chapter_lead_height = 99; s_chapter_lead_width = 0; s_chapter_lead_row_height = 77;
    copy_text(s_chapter_heading_title, sizeof(s_chapter_heading_title), "OldHeading");
    copy_text(s_chapter_heading_label, sizeof(s_chapter_heading_label), "OldLabel");
    assert(load_chapter_at_real(&ctx, 1, 0, false, NULL, SIZE_MAX));
    assert(test_detach_calls == 1 && test_build_calls == 1 && test_last_detach_ctx == &s_next_ch.dims_ctx);
    assert(s_chapter == 1 && s_page == 0 && s_layout_pages == 7 && !strcmp(s_text, "Cold loaded text"));
    assert(s_next_ch.kept && s_next_ch.chapter == 0 && s_next_ch.dir == -1 && s_next_ch.target_page == 4);
    assert(s_next_ch.state == NEXT_CH_LAYOUT_DONE && s_next_ch.source_generation == s_layout_gen);
    assert(!strcmp(s_next_ch.text.utf8, "active") && !strcmp(s_next_ch.path, s_path) && !strcmp(s_next_ch.title, "Ch0"));
    assert(s_next_ch.lead_skip == 3 && s_next_ch.lead_height == 99 && s_next_ch.lead_row_height == 77);
    assert(!strcmp(s_next_ch.heading_title, "OldHeading") && !strcmp(s_next_ch.heading_label, "OldLabel"));
    assert(book_layout_stage_page_count(s_next_ch.layout) == 5 && s_next_ch.dims_ctx.chapter == 0);
    // 折返：直接采用保留的第 0 章，不再加载/排版，回到原末页和原题头。
    // Reversal adopts kept chapter 0 with no load/layout and returns to the old last page and heading.
    s_back_turns = 1;
    assert(load_chapter(&ctx, 0, 0, true));
    assert(test_fallback_loads == 0 && test_build_calls == 1 && test_detach_calls == 2);
    assert(s_chapter == 0 && s_page == 4 && s_layout_pages == 5 && !strcmp(s_text, "active"));
    assert(s_chapter_lead_skip == 3 && s_chapter_lead_height == 99 && !strcmp(s_chapter_heading_title, "OldHeading"));
    assert(s_next_ch.kept && s_next_ch.chapter == 1 && s_next_ch.dir == 1 && s_next_ch.target_page == 0);
    assert(!strcmp(s_next_ch.text.utf8, "Cold loaded text") && book_layout_stage_page_count(s_next_ch.layout) == 7);

    // 往回冷加载（落末页）：第 2 章留作往前候选，目标页 0。
    // Backward cold load (last page): chapter 2 stays as the forward candidate landing on page 0.
    reset_env();
    s_chapter = 2;
    assert(load_chapter_at_real(&ctx, 1, 0, true, NULL, SIZE_MAX));
    assert(s_chapter == 1 && s_next_ch.kept && s_next_ch.chapter == 2 && s_next_ch.dir == 1 && s_next_ch.target_page == 0);

    // 不留：非相邻、带锚点/偏移、方向与落点不符、分段流、TXT、低内存、搬出失败。
    // Not kept: non-adjacent, anchors/offsets, mismatched direction/landing, part streams, TXT, low memory, failed detach.
    for (int variant = 0; variant < 10; ++variant) {
        reset_env();
        size_t target = 1; size_t offset = 0; bool last = false; const char *anchor = NULL; size_t so = SIZE_MAX;
        switch (variant) {
        case 0: target = 2; break;                    // 非相邻 / not adjacent
        case 1: anchor = "a"; break;                  // 目录/锚点跳转 / TOC or anchor jump
        case 2: offset = 10; break;                   // 带偏移 / with an offset
        case 3: last = true; break;                   // 往前却落末页 / forward but landing on the last page
        case 4: s_chapter = 2; target = 1; break;     // 往回却落首页 / backward but landing on the first page
        case 5: s_part_more = true; break;            // 旧章是分段流 / the old chapter is a part stream
        case 6: test_kind = BOOK_KIND_TXT; break;
        case 7: test_free_psram = BOOK_NEXT_CH_RESERVE - 1024; break;
        case 8: test_detach_fail = true; break;
        default: so = 5; break;                       // 源偏移跳转 / source-offset jump
        }
        assert(load_chapter_at_real(&ctx, target, offset, last, anchor, so));
        assert(!s_next_ch.kept && s_next_ch.state == NEXT_CH_IDLE && !s_next_ch.layout && !s_next_ch.text.utf8);
        assert(test_stage_frees == 0 && s_chapter == target);
    }

    // 排版失败：移出的候选释放，按旧文本重排回滚，不留候选。
    // Layout failure: the detached stage is freed, the old text is re-laid out, and nothing is kept.
    reset_env();
    test_build_fail_once = true;
    assert(!load_chapter_at_real(&ctx, 1, 0, false, NULL, SIZE_MAX));
    assert(s_chapter == 0 && !strcmp(s_text, "active") && s_layout_pages == 5 && test_stage_frees == 1);
    assert(!s_next_ch.kept && s_next_ch.state == NEXT_CH_IDLE && test_free_book_calls == 0 && test_build_calls == 2);
    // 回滚重排也失败：释放当前书，同样不留候选。/ If the rollback layout fails too the book is released, still keeping nothing.
    reset_env();
    test_build_fail_always = true;
    assert(!load_chapter_at_real(&ctx, 1, 0, false, NULL, SIZE_MAX));
    assert(test_free_book_calls == 1 && test_stage_frees == 1 && !s_next_ch.kept);
    // 加载失败（尚未搬出）：什么都不动。/ A load failure (before any detach) changes nothing.
    reset_env();
    test_preload_err = ESP_FAIL;
    assert(!load_chapter_at_real(&ctx, 1, 0, false, NULL, SIZE_MAX));
    assert(test_detach_calls == 0 && s_chapter == 0 && s_layout_pages == 5 && !s_next_ch.kept);

    next_chapter_clear();
    free(s_text); free(s_next_fb);
    puts("PASS: Pico cold-load keeps the left chapter as the reverse candidate and rolls back cleanly");
    return 0;
}
'''
    cold_src = temp / 'cold_keep_test.c'
    cold_src.write_text(cold)
    cold_exe = temp / 'cold_keep_test'
    subprocess.run(cc + [str(cold_src), '-o', str(cold_exe)], check=True)
    subprocess.run([str(cold_exe)], check=True)


def main():
    with tempfile.TemporaryDirectory(prefix='pico-chapter-preload-') as d:
        temp = Path(d)
        run_source_and_stage_test(temp)
        run_app_state_machine_test(temp)


if __name__ == '__main__':
    main()

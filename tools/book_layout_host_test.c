/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用伪字体验证布局完整性、偏移和测宽复杂度。
 * English: Verify layout completeness, offsets and measurement complexity using a fake font.
 * 冻结：只用于宿主测试。/ Frozen: Host testing only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"
int test_heap_fail_after = -1;
static char drawn[20000];
static bool capture_lines;
static char captured[128][512];
static unsigned captured_count;
static size_t measured_codepoints;
static size_t measure_calls;
static int first_draw_px, last_draw_px, first_draw_x, last_draw_x;
static int last_tracking_px;
static int fitted_target;
static int test_cjk_advance;
static int test_opener_bearing;
static int image_probe_count;
// PR9 尺寸由调用方提供；测试用图片表模拟真实解码回调。/ PR9 asks its caller for dimensions; simulate the decoded image table.
static bool fixture_image(void *ctx, int image, int *width, int *height) {
    const html_text_t *chapter = ctx;
    for (size_t i=0; i<chapter->count; ++i) if (chapter->blocks[i].image == image) {
        *width=chapter->blocks[i].image_width; *height=chapter->blocks[i].image_height;
        return *width>0 && *height>0;
    }
    return false;
}
static bool probe_image(void *ctx, int image, int *width, int *height) {
    assert(ctx == &image_probe_count && image >= 0);
    ++image_probe_count;
    *width = 80; *height = 20;
    return true;
}
int test_guide_segments;
int test_guide_first_y;
int test_guide_height;
uint8_t test_guide_gray;
// 宿主假字体：一个可配置就绪位的槽表 + 一个可配置的当前槽，用来验证按 run 换字体。
// Host stand-in font: a slot table with configurable readiness plus a current slot, enough to
// check that measuring follows the runs.
static unsigned test_slot_ready_mask = 1u;
static int test_slot;
// 每个槽每字额外加宽，用来把“量宽到底走了哪个槽”变成看得见的折行差异。
// Extra per-glyph width per slot, so which slot measured a glyph shows up as a wrap change.
static int test_slot_advance[TTF_FONT_SLOTS];
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) {
    if (runs != NULL && count > 0) ttf_font_select(runs[0].slot);
}
int ttf_font_select(int slot) {
    int previous = test_slot;
    if (slot >= 0 && slot < TTF_FONT_SLOTS) test_slot = slot;
    return previous;
}
int ttf_font_selected(void) { return test_slot; }
bool ttf_font_slot_ready(int slot) {
    return slot >= 0 && slot < TTF_FONT_SLOTS && (test_slot_ready_mask & (1u << slot)) != 0;
}
int ttf_text_width_px(int px, const char* text) {
    int n = 0, width = 0;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80) {
        n++;
        width += (unsigned char)*text >= 0x80 && test_cjk_advance ? test_cjk_advance :
                 *text == 'i' ? px / 2 : *text == 'W' ? px + px / 2 : px;
    }
    measured_codepoints += (size_t)n;
    measure_calls++;
    return width + n * test_slot_advance[test_slot];
}
int ttf_ascender_px(int px) { return px; }
int ttf_text_left_bearing_px(int px, const char* text) {
    (void)px; (void)text; return test_opener_bearing;
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)y; (void)px; (void)align;
    assert(fg <= 15 && bg <= 15);
    if (!drawn[0]) { first_draw_px = px; first_draw_x = x; }
    last_draw_px = px;
    last_draw_x = x;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
    if(capture_lines){assert(captured_count<128&&strlen(text)<512);strcpy(captured[captured_count++],text);}
}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg) {
    last_tracking_px = tracking_px;
    ttf_draw_text_px(fb, x, y, px, text, EPD_DRAW_ALIGN_LEFT, fg, bg);
}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, uint8_t fg, uint8_t bg) {
    fitted_target = target_width;
    ttf_draw_text_px_spaced(fb, x, y, px, text, tracking_px, fg, bg);
}
int main(void) {
    EpdRect r = {0, 0, 20, 30};
    const char text[] = "甲乙丙丁戊己庚辛壬癸";
    assert(book_layout_build(text, strlen(text), r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 12);
    assert(book_layout_page_for_offset(11) == 0);
    assert(book_layout_page_for_offset(12) == 1);
    assert(book_layout_page_for_offset(999) == 2);
    uint8_t fb = 0;
    for (size_t i = 0; i < book_layout_page_count(); i++) {
        assert(book_layout_page_start_offset(i) % 3 == 0);
        book_layout_draw_page(&fb, i, r, 10);
    }
    assert(strcmp(drawn, text) == 0);
    const char mixed[] = "Wi甲iiW";
    assert(book_layout_build(mixed, strlen(mixed), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 7);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(strcmp(drawn, mixed) == 0);
    assert(book_layout_build("", 0, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_build(NULL, 0, r, 10));
    assert(!book_layout_build(NULL, 1, r, 10));
    assert(book_layout_page_count() == 0);
    assert(!book_layout_build("\xe7\x94", 2, r, 10));
    assert(!book_layout_build("x\0y", 3, r, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,5,30}, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,20,5}, 10));
    char* many = malloc(4097);
    memset(many, 'x', 4097);
    r = (EpdRect){0,0,10,15};
    assert(book_layout_build(many, 4096, r, 10));
    assert(book_layout_page_count() == 4096);
    assert(!book_layout_build(many, 4097, r, 10));
    assert(book_layout_page_count() == 0);
    free(many);
    assert(book_layout_build("a\r\n\r\nb", 6, (EpdRect){0,0,20,100}, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, (EpdRect){0,0,20,100}, 10);
    assert(strcmp(drawn, "ab") == 0);
    char long_line[1024];
    memset(long_line, 'z', sizeof(long_line));
    r = (EpdRect){0,0,10000,30};
    measured_codepoints = measure_calls = 0;
    assert(book_layout_build(long_line, sizeof(long_line), r, 10));
    printf("long-line build: %zu calls, %zu measured codepoints\n", measure_calls, measured_codepoints);
    fflush(stdout);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 11);
    assert(drawn[0] == 0);
    book_layout_draw_page(&fb, 99, r, 10);
    assert(drawn[0] == 0);
    measured_codepoints = measure_calls = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(strlen(drawn) == sizeof(long_line));
    assert(memcmp(drawn, long_line, sizeof(long_line)) == 0);
    assert(book_layout_page_start_offset(99) == sizeof(long_line));
    assert(!book_layout_build("\xed\xa0\x80", 3, r, 10));
    assert(!book_layout_build("\xf4\x90\x80\x80", 4, r, 10));
    assert(!book_layout_build("\xc0\xaf", 2, r, 10));
    book_layout_free();
    assert(book_layout_page_count() == 0);
    const char styled[] = "Title\nbody";
    blk_t blocks[] = {{.offset = 0, .len = 5, .heading = true, .image = -1},
                      {.offset = 6, .len = 4, .heading = false, .image = -1}};
    r = (EpdRect){0, 0, 100, 45};
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, "Titlebody") && first_draw_px == 18 && last_draw_px == 10);
    const char chapters[] = "One\nalpha\nTwo\nbeta";
    blk_t chapter_blocks[] = {
        {.offset = 0, .len = 3, .heading = true, .chapter_start = true, .image = -1},
        {.offset = 4, .len = 5, .image = -1},
        {.offset = 10, .len = 3, .heading = true, .chapter_start = true, .image = -1},
        {.offset = 14, .len = 4, .image = -1},
    };
    EpdRect chapter_rect = {0, 0, 300, 120};
    assert(book_layout_build_blocks(chapters, strlen(chapters), chapter_blocks, 4, chapter_rect, 10));
    // PR9 不包含后来追加的块级 chapter_start 强制分页；这里验证其原始流式行为。
    // PR9 excludes the later block-level forced chapter break; verify its original flow.
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 0);
    assert(book_layout_page_for_offset(10) == 0 && book_layout_page_for_offset(14) == 0);
    drawn[0] = 0; book_layout_draw_page(&fb, 0, chapter_rect, 10);
    assert(!strcmp(drawn, "OnealphaTwobeta"));
    book_layout_set_chapter_lead(4, 30);
    assert(book_layout_build_blocks(chapters, strlen(chapters), chapter_blocks, 4, chapter_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 4);
    book_layout_set_chapter_lead(0, 0);
    const char illustrated[] = "IMG\nAA\nIMG\nBB";
    blk_t illustrated_blocks[] = {
        {.offset = 0, .len = 3, .image = 0},
        {.offset = 4, .len = 2, .image = -1},
        {.offset = 7, .len = 3, .image = 1},
        {.offset = 11, .len = 2, .image = -1},
    };
    EpdRect illustrated_rect = {0, 0, 100, 60};
    assert(book_layout_build_blocks(illustrated, strlen(illustrated), illustrated_blocks, 4,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 4);
    assert(book_layout_page_image(0) == 0 && book_layout_page_image(2) == 1);
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(illustrated, strlen(illustrated), illustrated_blocks, 4,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 4);
    assert(book_layout_page_image(0) == -1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, illustrated_rect, 10);
    assert(!strcmp(drawn, "AABB"));
    assert(book_layout_build_blocks("IMG", 3, illustrated_blocks, 1, illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) == 0);
    book_layout_set_images_visible(true);
    // 真实 HTML 的换行、分页空白、全角及零宽占位不应在插图前后产生额外页面。
    // Real HTML line/page-break whitespace and fullwidth/zero-width spacers must not create image-adjacent pages.
    const char *image_html = "<p>甲</p><p>&#10;&#12;&nbsp;</p><p>　</p>"
        "<br/><img src='image.png'/><br/><p>　&#x200b;&#xfeff;</p><p id='after'>乙<br/>丙</p>";
    html_text_t parsed = {0};size_t image_anchor = 0;
    assert(html_to_blocks_with_css_anchor(image_html, strlen(image_html), NULL, 0,
                                          "after", &image_anchor, &parsed) == ESP_OK);
    assert(parsed.count == 6 && parsed.image_count == 1);
    size_t after_image = parsed.blocks[4].offset;
    assert(image_anchor == after_image);
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_image(1) == -1);
    assert(book_layout_page_start_offset(2) == after_image && book_layout_page_for_offset(image_anchor) == 2);
    drawn[0] = 0;
    for(size_t p=0;p<3;p++)book_layout_draw_page(&fb,p,illustrated_rect,10);
    assert(!strcmp(drawn,"甲乙丙"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) < 0);
    drawn[0]=0;book_layout_draw_page(&fb,0,illustrated_rect,10);assert(!strcmp(drawn,"甲乙丙"));
    book_layout_free();html_text_free(&parsed);
    const char *only_image = "<p>　&#x200b;</p><img src='image.png'/><p>　</p>";
    assert(html_to_blocks(only_image,strlen(only_image),&parsed)==ESP_OK);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,illustrated_rect,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1);
    book_layout_set_images_visible(true);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,illustrated_rect,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1);
    book_layout_free();html_text_free(&parsed);
    const char *text_spacer="<p>甲</p><p>　</p><p>乙</p>";
    assert(html_to_blocks(text_spacer,strlen(text_spacer),&parsed)==ESP_OK);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,(EpdRect){0,0,100,20},10));
    assert(book_layout_page_count()==3);
    book_layout_free();html_text_free(&parsed);
    EpdRect balanced = book_layout_balanced_rect((EpdRect){36, 0, 612, 100}, 48, 0);
    assert(balanced.x == 54 && balanced.width == 576);
    assert(balanced.x == 684 - balanced.x - balanced.width);
    balanced = book_layout_balanced_rect((EpdRect){36, 0, 612, 100}, 48, 2);
    assert(balanced.x == 43 && balanced.width == 598);
    r = (EpdRect){0, 0, 100, 100};
    for (unsigned em = 0; em <= 3; ++em) {
        book_layout_set_first_line_indent(em);
        assert(book_layout_build("甲乙\n丙丁", strlen("甲乙\n丙丁"), r, 10));
        drawn[0] = 0;
        book_layout_draw_page(&fb, 0, r, 10);
        assert(first_draw_x == (int)em * 10);
        // 原文的段首全角/半角空格不得额外叠加一个缩进档位。
        // Source leading spaces must not add another visual indent stop.
        const char *preindented = "　甲乙\n  丙丁";
        assert(book_layout_build(preindented, strlen(preindented), r, 10));
        drawn[0] = 0;
        book_layout_draw_page(&fb, 0, r, 10);
        assert(first_draw_x == (int)em * 10);
        assert(!strcmp(drawn, "甲乙丙丁"));
    }
    // 模拟字号 48、真实字宽 33 的字体；开引号留白不得额外增加半格。
    // Simulate a 48-high font with 33-wide glyphs; an opener's bearing must not add half an indent cell.
    r = (EpdRect){36, 0, 612, 180};
    test_cjk_advance = 33;
    for (unsigned em = 0; em <= 3; ++em) {
        for (int tracking = -4; tracking <= 4; tracking += 2) {
            book_layout_set_first_line_indent(em);
            book_layout_set_typography(tracking);
            const char *cases[] = {"甲乙丙丁", "　甲乙丙丁", "\xef\xbb\xbf\xe2\x80\x8b甲乙丙丁",
                                    "　“甲乙丙丁”", "（甲乙丙丁）", "「甲乙丙丁」"};
            for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
                test_opener_bearing = i >= 3 ? 20 : 0;
                assert(book_layout_build(cases[i], strlen(cases[i]), r, 48));
                drawn[0] = 0;
                book_layout_draw_page(&fb, 0, r, 48);
                int shift = em ? test_opener_bearing : 0;
                assert(first_draw_x + shift == r.x + (int)em * (33 + tracking));
                assert(book_layout_page_start_offset(0) == 0);
            }
        }
    }
    book_layout_set_first_line_indent(2);book_layout_set_typography(0);
    test_cjk_advance=33;test_opener_bearing=12;
    for(int delta=-20;delta<=20;++delta){
        book_layout_set_first_line_indent_adjust(delta);
        const char *text="　“甲乙丙丁”";
        assert(book_layout_build(text,strlen(text),r,48));drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
        assert(first_draw_x+12==r.x+66+delta&&book_layout_page_start_offset(0)==0);
    }
    book_layout_set_first_line_indent(0);book_layout_set_first_line_indent_adjust(20);
    assert(book_layout_build("甲乙",strlen("甲乙"),r,48));drawn[0]=0;book_layout_draw_page(&fb,0,r,48);assert(first_draw_x==r.x);
    book_layout_set_first_line_indent(1);test_cjk_advance=10;test_opener_bearing=0;book_layout_set_first_line_indent_adjust(-20);
    assert(book_layout_build("甲乙",strlen("甲乙"),r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(first_draw_x==r.x);
    book_layout_set_first_line_indent_adjust(21); // Invalid values reset safely to zero.
    assert(book_layout_build("甲乙",strlen("甲乙"),r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(first_draw_x==r.x+10);
    for(int align=0;align<=2;++align){
        blk_t isolated={.offset=0,.len=strlen("甲乙"),.heading=align==0,.align=(uint8_t)align,.image=-1};
        book_layout_set_first_line_indent(2);book_layout_set_first_line_indent_adjust(0);
        assert(book_layout_build_blocks("甲乙",isolated.len,&isolated,1,r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);int original_x=first_draw_x;
        book_layout_set_first_line_indent_adjust(20);
        assert(book_layout_build_blocks("甲乙",isolated.len,&isolated,1,r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(first_draw_x==original_x);
    }
    book_layout_set_first_line_indent_adjust(0);
    test_cjk_advance = test_opener_bearing = 0;
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    const char punct[] = "甲乙，丙";
    r = (EpdRect){0, 0, 20, 15};
    assert(book_layout_build(punct, strlen(punct), r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3);
    book_layout_set_first_line_indent(0);
    r = (EpdRect){0, 0, 40, 15};
    assert(book_layout_build("甲乙丙丁，戊", strlen("甲乙丙丁，戊"), r, 10));
    assert(book_layout_page_start_offset(1) == 9);
    book_layout_set_first_line_indent(2);
    const char opener[] = "甲（乙丙";
    r.width = 20;
    assert(book_layout_build(opener, strlen(opener), r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3);
    const char aligned[] = "甲乙";
    blk_t aligned_block = {.offset = 0, .len = strlen(aligned), .image = -1,
                           .align = 1, .indent_percent = 100};
    r = (EpdRect){10, 0, 100, 30};
    assert(book_layout_build_blocks(aligned, strlen(aligned), &aligned_block, 1, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_x == 50 && last_draw_x == 50);
    r = (EpdRect){0, 0, 100, 45};
    book_layout_set_chapter_lead(6, 20);
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_page_start_offset(0) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "body"));
    book_layout_set_chapter_lead(0, 0);
    r.width = 20;
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 6);
    assert(book_layout_page_start_offset(5) == 6);
    drawn[0] = 0;
    for (size_t i = 0; i < book_layout_page_count(); ++i) book_layout_draw_page(&fb, i, r, 10);
    assert(!strcmp(drawn, "Titlebody"));
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, (EpdRect){0,0,100,20}, 10));
    blocks[1].offset = 5;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 0);
    blocks[1].offset = 6;
    blocks[1].len = SIZE_MAX;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(!book_layout_build_blocks(styled, strlen(styled), NULL, 2, r, 10));
    blk_t split_utf8[] = {{.offset = 0, .len = 1, .heading = true, .image = -1},
                          {.offset = 2, .len = 2, .heading = false, .image = -1}};
    assert(!book_layout_build_blocks("甲\nx", 5, split_utf8, 2, r, 10));
    assert(book_layout_build("body", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_px == 10);
    book_layout_set_typography(0);
    r = (EpdRect){0, 0, 35, 100};
    assert(book_layout_build("abcd", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "abcd") && first_draw_x == 20 && last_draw_x == 0);
    book_layout_set_first_line_indent(0);
    book_layout_set_typography(2);
    r = (EpdRect){0, 0, 25, 15};
    assert(book_layout_build("abcd", 4, r, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 2);
    drawn[0] = 0; last_tracking_px = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "ab") && last_tracking_px == 2);
    book_layout_set_typography(-2);
    r.width = 28;
    assert(book_layout_build("abcd", 4, r, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 3);
    drawn[0] = 0; last_tracking_px = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "abc") && last_tracking_px == -2);
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    r = (EpdRect){10, 20, 400, 120};
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    book_layout_set_reading_line(0);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments == 0);
    book_layout_set_reading_line(1);
    book_layout_draw_page(&fb, 0, r, 40);
    int dashed_segments = test_guide_segments;
    assert(dashed_segments > 0);
    assert(test_guide_first_y == 70 && test_guide_height == 2 && test_guide_gray == 0x50);
    book_layout_set_reading_line_offset(-8);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 62);
    book_layout_set_reading_line_offset(8);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 78);
    book_layout_set_reading_line_offset(0);
    book_layout_set_reading_line(2);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments > dashed_segments);
    assert(test_guide_first_y == 70 && test_guide_height == 2 && test_guide_gray == 0x50);
    book_layout_set_spacing(110, 50);
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 62);
    book_layout_set_spacing(200, 50);
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 80);
    book_layout_set_spacing(150, 50);
    book_layout_set_reading_line(0);
    // 右标点可在安全留白内悬挂，下一页必须仍从正文开始。/ Hang a closer safely without starting the next page with punctuation.
    book_layout_set_first_line_indent(0);
    r = (EpdRect){40, 0, 40, 15};
    const char hanging[] = "甲乙丙丁，戊己庚辛";
    assert(book_layout_build(hanging, strlen(hanging), r, 10));
    assert(book_layout_page_start_offset(1) == strlen("甲乙丙丁，"));
    drawn[0] = 0; fitted_target = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "甲乙丙丁，") && fitted_target == 43);
    // 居中块的首行不应带入普通段落缩进。/ A centered block must not inherit first-line body indent.
    blk_t centered = {.offset = 0, .len = strlen("甲乙丙丁戊己"), .align = 1, .indent_percent = 200, .image = -1};
    r = (EpdRect){40, 0, 30, 15};
    assert(book_layout_build_blocks("甲乙丙丁戊己", centered.len, &centered, 1, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    int first_center_x = first_draw_x;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 1, r, 10);
    assert(first_center_x == first_draw_x);
    const char *punct_cases[]={"甲乙丙丁：“戊己”庚辛", "甲乙丙丁……戊己庚辛", "甲乙丙丁——戊己庚辛", "甲乙丙丁”，戊己庚辛", "甲乙丙丁：‘戊己’庚辛"};
    book_layout_set_first_line_indent(0);
    for(unsigned c=0;c<5;++c)for(int w=20;w<=80;w+=10)for(int tracking=-4;tracking<=4;tracking+=2){
        EpdRect box={40,0,w,30};
        book_layout_set_typography(tracking);capture_lines=true;captured_count=0;drawn[0]=0;
        assert(book_layout_build(punct_cases[c],strlen(punct_cases[c]),box,10));
        for(size_t page=0;page<book_layout_page_count();++page)book_layout_draw_page(&fb,page,box,10);
        assert(!strcmp(drawn,punct_cases[c]));
        capture_lines=false;
    }
    html_text_t composed={0};
    const char *html="<p>甲乙丙丁</p><img src='small.png'/><p>戊己庚辛</p>";
    assert(html_to_blocks(html,strlen(html),&composed)==ESP_OK);
    for(size_t i=0;i<composed.count;++i)if(composed.blocks[i].image>=0){composed.blocks[i].image_width=60;composed.blocks[i].image_height=15;}
    EpdRect mixed_box={40,20,100,100};
    book_layout_set_image_dims(fixture_image,&composed);
    book_layout_set_typography(0);book_layout_set_spacing(150,25);book_layout_set_chapter_lead(0,0);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1&&book_layout_page_image_count(0)==1);
    int image_y,image_w,image_h;
    assert(book_layout_page_image_at(0,0,NULL,&image_y,&image_w,&image_h));
    assert(image_w==60&&image_h==15&&image_y>0&&image_y+image_h<mixed_box.height);
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_box,10);assert(!strcmp(drawn,"甲乙丙丁戊己庚辛"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1&&!book_layout_page_image_count(0));
    book_layout_set_images_visible(true);html_text_free(&composed);

    // 按 run 换字体：槽 2 的每个字更宽。量宽没跟着 run 走的话，三个字仍然挤在一行里。
    // Per-run faces: slot 2 is wider per glyph. If measuring ignored the runs, all three
    // glyphs would still fit on one line.
    {
        const char* runs_text = "甲甲乙";
        blk_t run_block = {.offset = 0, .len = 9, .image = -1, .run_first = 0, .run_count = 2};
        html_run_t run_spans[] = {{0, 3, 1}, {3, 6, 2}};
        EpdRect run_box = {40, 0, 80, 60};
        const int old_cjk = test_cjk_advance;
        test_cjk_advance = 10;
        test_slot_ready_mask = 0x7u;
        // 槽 2 单字 75px：一行 80px 只放得下一个，槽 1 的 10px 三个都放得下。
        // Slot 2 is 75 px per glyph: only one fits an 80 px line, whereas all three 10 px
        // glyphs of slot 1 fit easily.
        test_slot_advance[2] = 65;
        book_layout_set_spacing(150, 0);
        book_layout_set_chapter_lead(0, 0);
        book_layout_set_first_line_indent(0);
        book_layout_set_typography(0);
        book_layout_set_runs(NULL, 0);
        capture_lines = true; captured_count = 0;
        assert(book_layout_build_blocks(runs_text, 9, &run_block, 1, run_box, 10));
        for (size_t page = 0; page < book_layout_page_count(); ++page)
            book_layout_draw_page(&fb, page, run_box, 10);
        assert(captured_count == 1 && !strcmp(captured[0], "甲甲乙"));
        captured_count = 0;
        book_layout_set_runs(run_spans, 2);
        assert(book_layout_build_blocks(runs_text, 9, &run_block, 1, run_box, 10));
        for (size_t page = 0; page < book_layout_page_count(); ++page)
            book_layout_draw_page(&fb, page, run_box, 10);
        assert(captured_count == 3 && !strcmp(captured[0], "甲") &&
               !strcmp(captured[1], "甲") && !strcmp(captured[2], "乙"));
        capture_lines = false;
        book_layout_set_runs(NULL, 0);
        test_slot_ready_mask = 1u;
        test_slot_advance[2] = 0;
        test_cjk_advance = old_cjk;
    }

    // 多图同页、比例缩小与未知尺寸回退，正文前后不遗漏。
    // Cover multiple images, aspect-fit and unknown-size fallback without losing adjacent prose.
    const char *multi = "<p>甲</p><img src='a.png'/><img src='b.png'/><p>乙</p>";
    assert(html_to_blocks(multi, strlen(multi), &composed) == ESP_OK);
    for (size_t i=0;i<composed.count;++i) if (composed.blocks[i].image>=0) {
        composed.blocks[i].image_width=60; composed.blocks[i].image_height=15;
    }
    uint32_t before_generation=book_layout_generation();
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_generation()!=before_generation);
    assert(book_layout_page_count()==1 && book_layout_page_image_count(0)==2 && book_layout_page_image(0)==-1);
    int index,y,w,h,first_y;
    assert(book_layout_page_image_at(0,0,&index,&first_y,&w,&h)&&index==0&&w==60&&h==15);
    assert(book_layout_page_image_at(0,1,&index,&y,&w,&h)&&index==1&&y==first_y+20);
    assert(!book_layout_page_image_at(0,-1,NULL,NULL,NULL,NULL));
    assert(!book_layout_page_image_at(0,2,NULL,NULL,NULL,NULL)&&book_layout_page_image_count(99)==0);
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_box,10);assert(!strcmp(drawn,"甲乙"));
    // 剩余高度不足时整张图移到下一页。/ Move a whole image when remaining height is insufficient.
    EpdRect short_box={40,20,100,45};
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,short_box,10));
    assert(book_layout_page_count()==2&&book_layout_page_image_count(0)==1&&book_layout_page_image_count(1)==1);
    assert(book_layout_page_start_offset(1)==composed.blocks[2].offset);
    assert(book_layout_page_image_at(1,0,&index,&y,&w,&h)&&index==1&&y==0);
    drawn[0]=0;for(size_t i=0;i<2;++i)book_layout_draw_page(&fb,i,short_box,10);assert(!strcmp(drawn,"甲乙"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image_count(0)==0);
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_box,10);assert(!strcmp(drawn,"甲乙"));
    book_layout_set_images_visible(true);
    composed.blocks[3].chapter_start=true;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1);
    html_text_free(&composed);
    const char *large="<img src='big.png'/>";
    assert(html_to_blocks(large,strlen(large),&composed)==ESP_OK);
    composed.blocks[0].image_width=1000;composed.blocks[0].image_height=2000;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_image_at(0,0,&index,&y,&w,&h)&&w==50&&h==100&&y==0);
    assert(book_layout_page_image(0)==0);
    composed.blocks[0].image_width=composed.blocks[0].image_height=0;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_image_count(0)==0&&book_layout_page_image(0)==0);
    book_layout_set_image_dims(probe_image,&image_probe_count);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(image_probe_count==1&&book_layout_page_image_at(0,0,&index,&y,&w,&h)&&w==80&&h==20);
    book_layout_set_image_dims(fixture_image,&composed);html_text_free(&composed);
    char many_images[2000]="<p>甲</p>";
    for(int i=0;i<40;++i)strcat(many_images,"<img src='small.png'/>");
    strcat(many_images,"<p>乙</p>");
    assert(html_to_blocks(many_images,strlen(many_images),&composed)==ESP_OK);
    for(size_t i=0;i<composed.count;++i)if(composed.blocks[i].image>=0){composed.blocks[i].image_width=10;composed.blocks[i].image_height=1;}
    EpdRect many_box={0,0,100,400};
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,many_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image_count(0)==40);
    assert(book_layout_page_image_at(0,39,&index,&y,&w,&h)&&index==39);
    // 每一处分配失败后布局清空，可以安全重建。/ Allocation failures clear ownership and permit a clean rebuild.
    for(int fail=0;fail<6;++fail){
        test_heap_fail_after=fail;
        assert(!book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,many_box,10));
        assert(!book_layout_page_count()&&!book_layout_page_image_count(0));
    }
    test_heap_fail_after=-1;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,many_box,10));
    drawn[0]=0;book_layout_draw_page(&fb,0,many_box,10);assert(!strcmp(drawn,"甲乙"));
    html_text_free(&composed);
    book_layout_free();
    puts("book_layout_host_test: PASS");
}

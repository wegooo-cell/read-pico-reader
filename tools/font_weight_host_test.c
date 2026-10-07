/* SPDX-FileCopyrightText: 2026 mindreset */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * 中文：正文字重的宿主回归。静态字体没有 wght 轴，合成字重必须按档位单调加墨或减墨，
 * 不动字宽与页码，并且未设置字重时的外观与常规档完全一致。
 *
 * English: Host regression for body weight. Static fonts have no wght axis, so synthetic
 * weight must add or remove ink monotonically per step without touching advances, and the
 * untouched look must match the regular step exactly.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __APPLE__
/* Some host libc versions predate strlcpy, which ESP-IDF provides. */
size_t strlcpy(char *dst, const char *src, size_t cap) {
    size_t length = strlen(src);
    if (cap) {
        size_t copied = length < cap - 1 ? length : cap - 1;
        memcpy(dst, src, copied);
        dst[copied] = 0;
    }
    return length;
}
#endif

#include "ttf_font.h"

#define FB_WIDTH 512
#define FB_HEIGHT 160
#define FB_PIXELS ((size_t)FB_WIDTH * FB_HEIGHT)
#define ORIGIN_X 20
#define BASELINE_Y 110
#define TEST_PX 48
#define TEST_TEXT "汉字Wg"
#define NEUTRAL_INDEX 1
#define STEP_COUNT 4

static const char* const k_default_font = "main/assets/builtin.ttf";

static uint8_t s_frame[FB_PIXELS];
static uint8_t s_captured[STEP_COUNT][FB_PIXELS];

/* 字体的内建位图在设备上由链接脚本塞进 flash；宿主测试只需要符号存在。 */
/* The device links the built-in font blob from flash; the host test only needs the symbols. */
__asm__(
    ".globl _binary_builtin_ttf_start\n"
    "_binary_builtin_ttf_start:\n"
    ".byte 0\n"
    ".globl _binary_builtin_ttf_end\n"
    "_binary_builtin_ttf_end:\n"
    ".byte 0\n"
);

void* heap_caps_malloc(size_t size, int caps) { (void)caps; return malloc(size); }
void* heap_caps_calloc(size_t count, size_t size, int caps) { (void)caps; return calloc(count, size); }
void* heap_caps_realloc(void* ptr, size_t size, int caps) { (void)caps; return realloc(ptr, size); }
void heap_caps_free(void* ptr) { free(ptr); }
/// 整表映射在宿主上不成立：报 0 让字体走分块读取路径。/ Whole-table mapping never applies on the host: report 0 so the font streams in blocks.
size_t heap_caps_get_largest_free_block(int caps) { (void)caps; return 0; }
int64_t esp_timer_get_time(void) { return 0; }

static const char* s_font_path = k_default_font;
const char* app_settings_font_path(void) { return s_font_path; }
const char* app_settings_fonts_dir(void) { return "/sdcard/fonts"; }

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* framebuffer) {
    if (x < 0 || y < 0 || x >= FB_WIDTH || y >= FB_HEIGHT) return;
    framebuffer[(size_t)y * FB_WIDTH + x] = (uint8_t)(color >> 4);
}

typedef struct {
    int ink;                        /* 覆盖率总和 / total coverage */
    int dark;                       /* 近满墨像素 / near-full ink pixels */
    int left, right, top, bottom;   /* 墨迹包围盒 / ink bounding box */
} render_stats_t;

static render_stats_t render_text(uint8_t* frame) {
    memset(frame, 15, FB_PIXELS);
    ttf_draw_text_px(frame, ORIGIN_X, BASELINE_Y, TEST_PX, TEST_TEXT, EPD_DRAW_ALIGN_LEFT, 0, 15);
    render_stats_t stats = {.left = FB_WIDTH, .right = -1, .top = FB_HEIGHT, .bottom = -1};
    for (int y = 0; y < FB_HEIGHT; ++y) {
        for (int x = 0; x < FB_WIDTH; ++x) {
            const uint8_t gray = frame[(size_t)y * FB_WIDTH + x];
            const int ink = 15 - gray;
            if (ink <= 0) continue;
            stats.ink += ink;
            if (gray <= 1) ++stats.dark;
            if (x < stats.left) stats.left = x;
            if (x > stats.right) stats.right = x;
            if (y < stats.top) stats.top = y;
            if (y > stats.bottom) stats.bottom = y;
        }
    }
    return stats;
}

int main(int argc, char** argv) {
    s_font_path = argc > 1 ? argv[1] : k_default_font;
    assert(ttf_font_open(s_font_path) == ESP_OK);
    assert(ttf_font_ready());
    const int neutral_weight = ttf_get_weight();
    // 默认档必须是常规档：新增字重选项不能改变从未调整过的正文外观。
    // The default must be the regular step so an untouched reader keeps its previous look.
    assert(neutral_weight == 400);

    const render_stats_t untouched = render_text(s_frame);
    const int neutral_width = ttf_text_width_px(TEST_PX, TEST_TEXT);
    assert(neutral_width > 0);

    static const int steps[STEP_COUNT] = {300, 400, 500, 700};
    render_stats_t stats[STEP_COUNT];
    for (int i = 0; i < STEP_COUNT; ++i) {
        ttf_set_weight(steps[i]);
        assert(ttf_get_weight() == steps[i]);
        stats[i] = render_text(s_captured[i]);
        // 字宽不随字重变化，阅读页因此不需要重排。
        // Advances never depend on weight, so the reader never repaginates.
        assert(ttf_text_width_px(TEST_PX, TEST_TEXT) == neutral_width);
        assert(stats[i].ink > 0 && stats[i].right > stats[i].left);
    }

    assert(untouched.ink == stats[NEUTRAL_INDEX].ink);
    assert(untouched.left == stats[NEUTRAL_INDEX].left);
    assert(untouched.right == stats[NEUTRAL_INDEX].right);
    for (int i = 1; i < STEP_COUNT; ++i) assert(stats[i].ink > stats[i - 1].ink);
    assert(stats[3].dark > stats[NEUTRAL_INDEX].dark); /* 700 真的加粗笔画，而不是只加深边缘 / 700 thickens stems, not just edges */

    // 加粗只加墨、变细只减墨：逐像素单调，任何写错缓冲都会在这里暴露。
    // Heavier only adds and lighter only removes ink; pointwise monotonicity exposes a wrong buffer.
    for (size_t i = 0; i < FB_PIXELS; ++i) {
        assert(s_captured[3][i] <= s_captured[NEUTRAL_INDEX][i]);
        assert(s_captured[NEUTRAL_INDEX][i] <= s_captured[0][i]);
    }
    // 膨胀最多外扩一像素，字形盒不能整体移位。
    // Dilation grows the ink box by at most one pixel per side without shifting the glyph.
    assert(stats[NEUTRAL_INDEX].left - stats[3].left <= 1 && stats[3].left <= stats[NEUTRAL_INDEX].left);
    assert(stats[3].right - stats[NEUTRAL_INDEX].right <= 1 && stats[NEUTRAL_INDEX].right <= stats[3].right);
    assert(stats[0].left >= stats[NEUTRAL_INDEX].left && stats[0].right <= stats[NEUTRAL_INDEX].right);

    // 字形缓存按字重分开，切回旧档必须还原同一张位图。
    // The glyph cache is keyed by weight; returning to a step must restore the same bitmap.
    ttf_set_weight(700);
    assert(render_text(s_frame).ink == stats[3].ink);
    ttf_set_weight(300);
    assert(render_text(s_frame).ink == stats[0].ink);

    // 超出通用边界的字重夹到 300..800。
    // Weights outside the generic bounds clamp to 300..800.
    ttf_set_weight(900);
    assert(ttf_get_weight() == 800);
    ttf_set_weight(100);
    assert(ttf_get_weight() == 300);

    // 重新打开字体必须回到常规档，避免上次阅读的字重污染系统界面。
    // Reopening a font must return to the regular step so a previous reader weight cannot reach the system UI.
    ttf_font_unload();
    assert(ttf_font_open(s_font_path) == ESP_OK);
    assert(ttf_get_weight() == 400);
    assert(render_text(s_frame).ink == stats[NEUTRAL_INDEX].ink);
    ttf_font_unload();

    puts("font weight host test passed (synthetic steps, stable advances, neutral default)");
    return 0;
}

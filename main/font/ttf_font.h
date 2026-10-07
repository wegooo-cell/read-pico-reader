/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 可变 TTF 字形缓存：卡上字体或内置子集，按字重光栅化后画到 framebuffer。
 *
 * Variable TTF glyph cache: SD fonts or the built-in subset, rasterized
 * at the current weight onto the framebuffer.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epdiy.h"
#include "esp_err.h"

#define TTF_SIZE_SMALL 0
#define TTF_SIZE_LARGE 1

#define TTF_FONT_MAX 24
#define TTF_FONT_NAME_MAX 64
#define TTF_FONT_PATH_MAX 160

typedef struct {
    char name[TTF_FONT_NAME_MAX];
    char path[TTF_FONT_PATH_MAX];
} ttf_font_item_t;

typedef struct {
    uint32_t glyphs;
    uint32_t hits;
    uint32_t misses;
    int64_t read_us;
    int64_t raster_us;
    int64_t total_us;
} ttf_bench_stats_t;

#define TTF_FONT_BUILTIN "builtin"

/// 灰阶字覆盖率伽马。小于 1 抬中间覆盖率，抗锯齿边缘更深；满墨仍是 0。
/// Coverage gamma for gray glyphs. Below 1 lifts mid coverage so AA edges are darker; full ink stays 0.
#ifndef TTF_COVER_GAMMA
#define TTF_COVER_GAMMA 0.5f
#endif

esp_err_t ttf_font_init(void);
esp_err_t ttf_font_open(const char* path);
esp_err_t ttf_font_open_builtin(void);
bool ttf_font_is_builtin(void);
bool ttf_font_path_is_builtin(const char* path);
void ttf_font_unload(void);
int ttf_font_scan(void);
int ttf_font_count(void);
const ttf_font_item_t* ttf_font_item(int index);
const char* ttf_font_path(void);
const char* ttf_font_display_name(void);
/// 已知随附字体的中文名称；未知名称原样返回。/ Chinese names for bundled fonts; unknown names pass through.
const char* ttf_font_localized_name(const char* stem);
bool ttf_font_ready(void);
int ttf_ascender(int size);
int ttf_ascender_px(int pixel_height);

void ttf_draw_text(
    uint8_t* framebuffer, int x, int y, int size, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_draw_text_px(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);
/// 从左向右绘制，并在相邻字符之间加入固定像素间距。/ Draw left-aligned text with extra pixels between adjacent characters.
void ttf_draw_text_px_spaced(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, uint8_t fg, uint8_t bg
);
/// 在汉字间均摊行尾余量；标点溢出时可小幅压缩字距。/ Distribute spare width across CJK gaps or slightly compress for hanging punctuation.
void ttf_draw_text_px_fitted(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, int target_width, uint8_t fg, uint8_t bg
);
/// 覆盖率过半才落墨，像素只有 fg/bg。/ Ink only when coverage is over half; pixels are fg/bg only.
void ttf_draw_text_px_bw(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_measure_line(int size, const char* text, int* above, int* below);
void ttf_measure_line_px(int pixel_height, const char* text, int* above, int* below);
int ttf_text_width_px(int pixel_height, const char* text);
/// 首字符左侧留白，不生成位图；用于段首开标点的视觉对齐。
/// First-glyph left bearing without rasterizing; used for paragraph-opening punctuation alignment.
int ttf_text_left_bearing_px(int pixel_height, const char* text);
/// 设置当前字重，字宽不变。带 wght 轴的字体走真实变体；静态字体按与常规字重
/// 之差做覆盖率形态学，近似更粗或更细的字面，每 300 字重约一像素。
/// Set the current weight without changing advances. wght-axis fonts use real
/// variations; static fonts approximate a heavier or lighter face by coverage
/// morphology relative to the regular weight, roughly one pixel per 300 units.
void ttf_set_weight(int wght);
int ttf_get_weight(void);

void ttf_font_cache_clear(void);
void ttf_bench_begin(void);
void ttf_bench_end(ttf_bench_stats_t* out);

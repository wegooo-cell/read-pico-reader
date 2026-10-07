/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：宿主字重测试的显示契约：矩形、对齐标志与落点回调。
 * English: Display contract for the host font-weight test: rect, align flags and the pixel sink.
 *
 * 冻结：仅供测试，对齐位与 epdiy 一致。
 * Frozen: Tests only; align bits match epdiy.
 */
#pragma once

#include <stdint.h>

typedef struct {
    int x, y, width, height;
} EpdRect;

enum EpdFontFlags {
    EPD_DRAW_BACKGROUND = 0x1,
    EPD_DRAW_ALIGN_LEFT = 0x2,
    EPD_DRAW_ALIGN_RIGHT = 0x4,
    EPD_DRAW_ALIGN_CENTER = 0x8,
};

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* framebuffer);

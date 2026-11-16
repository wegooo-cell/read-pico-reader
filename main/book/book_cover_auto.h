/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：无封面书籍的确定性灰阶模板。只绘制目标尺寸，不执行屏幕抖动。
 * English: Deterministic grayscale covers for coverless books. Render at target size, without display dithering.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 缓存键的一部分：改动封面生成或封面的解码能力后必须递增，否则旧结果一直命中。
// Part of the cover cache key: bump it whenever cover rendering or the set of decodable cover
// formats changes, or stale entries keep winning.
#define BOOK_AUTO_COVER_VERSION 6u

/// 封面所选字体的缓存标识，默认字体为0。/ Cache identity of the selected cover face; zero for the default.
uint64_t book_auto_cover_font_key(void);

/// 生成 8 位灰阶封面；调用方提供 width*height 字节。/ Generate an 8-bit gray cover into caller-owned width*height bytes.
bool book_auto_cover_render(const char *title, const char *author,
                            unsigned width, unsigned height, uint8_t *out);

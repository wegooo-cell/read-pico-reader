/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：宿主字重测试的计时契约；字形缓存路径不依赖真实时钟。
 * English: Timer contract for the host font-weight test; the glyph cache path needs no real clock.
 *
 * 冻结：仅供测试。
 * Frozen: Tests only.
 */
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);

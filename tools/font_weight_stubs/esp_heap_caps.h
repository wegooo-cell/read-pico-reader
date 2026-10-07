/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：宿主字重测试的 PSRAM 分配契约，映射到宿主 malloc 以便 ASan 检查越界。
 * English: PSRAM allocation contract for the host font-weight test, mapped to host
 * malloc so ASan still reports out-of-bounds access.
 *
 * 冻结：仅供测试。
 * Frozen: Tests only.
 */
#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2

void* heap_caps_malloc(size_t size, int caps);
void* heap_caps_calloc(size_t count, size_t size, int caps);
void* heap_caps_realloc(void* ptr, size_t size, int caps);
void heap_caps_free(void* ptr);
/// 映射整表前的空闲块探测；宿主测试不需要映射，直接报 0。/ Free-block probe before whole-table mapping; the host test needs no mapping and reports 0.
size_t heap_caps_get_largest_free_block(int caps);

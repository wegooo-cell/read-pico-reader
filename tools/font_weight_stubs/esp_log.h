/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：宿主字重测试的日志契约，直接写到 stderr。
 * English: Log contract for the host font-weight test, straight to stderr.
 *
 * 冻结：仅供测试。
 * Frozen: Tests only.
 */
#pragma once

#include <stdio.h>

#define ESP_LOGE(tag, ...) do { fprintf(stderr, "E %s: ", (tag)); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGW(tag, ...) do { fprintf(stderr, "W %s: ", (tag)); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGI(tag, ...) do { fprintf(stderr, "I %s: ", (tag)); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

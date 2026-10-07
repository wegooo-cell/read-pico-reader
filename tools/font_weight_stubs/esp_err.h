/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：宿主字重测试的错误码契约。
 * English: Error-code contract for the host font-weight test.
 *
 * 冻结：仅供测试。
 * Frozen: Tests only.
 */
#pragma once

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_FOUND 0x105

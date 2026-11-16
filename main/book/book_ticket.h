/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：记录实际阅读时长和翻页，并绘制票根锁屏。
 * English: Record actual reading time and turns, and paint the ticket lock face.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "book_progress.h"
#include "esp_err.h"

/// 本地毫秒时钟；时钟无效返回 0。/ Local wall-clock milliseconds, or zero when unknown.
uint64_t book_ticket_local_time_ms(void);
esp_err_t book_ticket_record(uint32_t day, uint32_t milliseconds, uint32_t turns);
void book_ticket_format_time(char *out, size_t cap, uint64_t milliseconds);
void book_ticket_note_position(const char *path, const book_progress_t *progress,
                               const char *chapter, bool saved);
/// 近 30 天（最早到今天）的实测阅读秒数；时钟无效时返回 false 且清零。
/// Measured seconds for the last 30 days, oldest to today; false and zeroed without a valid clock.
bool book_ticket_recent_days(uint32_t seconds[30]);
/// 当前月每日真实阅读秒数，兼容旧30日记录；无有效日期返回 false。/ Current-month measured seconds with legacy 30-day migration; false without a valid date.
bool book_ticket_month_days(int year, int month, uint32_t seconds[31]);
bool book_ticket_draw(uint8_t *fb, bool reader_background);

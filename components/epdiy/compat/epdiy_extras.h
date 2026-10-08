/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * 中文：本仓库固件调用、而厂商示例 epdiy 没有的那几个函数的声明。实现在
 * epdiy_metalio_compat.c 里。例程的 epdiy.h 上没有这些名字，所以单独给一个头。
 *
 * English: declarations for the functions this repo's firmware calls that the vendor demo epdiy
 * does not define. The implementations live in epdiy_metalio_compat.c; the vendor's epdiy.h has
 * no such names, hence a separate header.
 */

#pragma once

#include <stdbool.h>

#include "epd_highlevel.h"

#ifdef __cplusplus
extern "C" {
#endif

void epd_set_leading_skip(bool enable);
void epd_lcd_set_prefill_lines(int lines);
enum EpdDrawError epd_hl_update_area_full(
    EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature, EpdRect area
);

#if defined(PICO_BOARD_METALIO_EINK4_PLUS)
// 本仓库固件按"跟随／灰阶／全刷"三档选波形，而厂商示例只有一份 MY_WAVEFORM。
// 三档在这里都指向它：画面正常，只是少了跟手专用的短波形（那是本仓库 fork 的优化）。
// This repo's firmware picks between a follow, a gray and a full waveform while the vendor demo
// ships only MY_WAVEFORM. All three map to it here: the picture is correct, only the dedicated
// short follow waveform (a local optimisation of this fork) is missing.
#include "my_waveform.h"

#define E0470_WAVEFORM MY_WAVEFORM
#define E0470_FULL_WAVEFORM MY_WAVEFORM
#define E0470_FOLLOW_WAVEFORM MY_WAVEFORM
#endif

#ifdef __cplusplus
}
#endif

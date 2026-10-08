/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：Metalio 板上连续 DU 与翻页引擎的替代声明。
 *
 * 这两个功能建立在本仓库 fork 的波形类型（EpdWaveform 布局）与波形表之上，而本板用的是
 * 厂商示例固件的 epdiy，两套波形布局不同名同构。为了先让画面出来，本板把这两个组件摘出
 * 依赖图，这里给出同名接口的空实现，上层调用点一行都不用改。
 *
 * 代价：本板没有"连续 DU 跟手"与"错相揭页"两个特性，触摸走普通刷新路径。
 *
 * English: stand-ins for the continuous-DU and page-turn APIs on the Metalio board.
 *
 * Both features are built on this repo's fork of the waveform types and tables, while this board
 * uses the vendor demo firmware's epdiy, whose waveform layout shares the names but not the
 * memory layout. To get a picture on screen first, those two components are dropped from the
 * dependency graph here and these no-op implementations keep every call site unchanged.
 *
 * The cost is that this board has neither continuous-DU finger tracking nor the staggered
 * page-turn; touch falls back to the ordinary refresh path.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epd_highlevel.h"
#include "epdiy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 连续 DU / Continuous DU ---- */

#define CONTINUOUS_DARK_PHASES 6
#define CONTINUOUS_LIGHT_PHASES 6

int continuous_du_dark_phases(void);
int continuous_du_light_phases(void);
bool continuous_du_init(void);
void continuous_du_deinit(void);
void continuous_du_reset(void);
bool continuous_du_busy(void);
void continuous_du_from_logical(int lx, int ly, int* px, int* py);
EpdRect continuous_du_rect_from_logical(EpdRect logical);
void continuous_du_mark_circle(int cx, int cy, int radius, int phases);
void continuous_du_mark_rect(EpdRect area, int phases);
void continuous_du_mark_diff(
    const uint8_t* to, const uint8_t* from, EpdRect area, int phases, bool invert
);
enum EpdDrawError continuous_du_scan(EpdiyHighlevelState* hl, EpdRect area);

/* ---- 错相揭页 / Staggered page turn ---- */

typedef enum {
    E0470_TURN_LTR = 0,
    E0470_TURN_RTL = 1,
    E0470_TURN_TTB = 2,
    E0470_TURN_BTT = 3,
} e0470_turn_dir_t;

#define E0470_TURN_DEFAULT_TICK_US 21000

const char* e0470_turn_dir_name(e0470_turn_dir_t dir);
void e0470_page_turn_set_tick_us(int us);
int e0470_page_turn_tick_us(void);
void e0470_page_turn_release(void);
enum EpdDrawError e0470_page_turn(EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir);

#ifdef __cplusplus
}
#endif

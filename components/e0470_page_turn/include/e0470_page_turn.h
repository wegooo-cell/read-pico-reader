/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 基于 MindReset Read Pico 官方 E0470 波形与刷新路径实现。
 * Built on the MindReset Read Pico E0470 waveform and refresh path.
 * 错相揭页引擎。应用只依赖本公开接口，不依赖条带或 LUT 的内部布局。
 * Staggered page-turn engine. Applications depend on this public API, not the internal band or LUT layout.
 */

#pragma once

#include "epd_highlevel.h"
#include "epdiy.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 逻辑屏幕上的揭页方向；库内按当前旋转映射到 framebuffer。/ Logical direction mapped through the current rotation.
typedef enum {
    E0470_TURN_LTR = 0,
    E0470_TURN_RTL = 1,
    E0470_TURN_TTB = 2,
    E0470_TURN_BTT = 3,
} e0470_turn_dir_t;

#define E0470_TURN_DEFAULT_TICK_US 21000
// 快档只缩短软件补等，不改变单次扫描或每像素相位。/ Fast only shortens software padding, never a scan or pixel's phases.
#define E0470_TURN_FAST_TICK_US 14000

const char* e0470_turn_dir_name(e0470_turn_dir_t dir);

/// 每拍目标时长；默认 21ms × 52 ≈ 1.1s。/ Target tick duration; default 21ms × 52 ≈ 1.1s.
void e0470_page_turn_set_tick_us(int us);
int e0470_page_turn_tick_us(void);
/// 离开阅读时释放按需分配的相位表。/ Release the lazily allocated phase table when leaving the reader.
void e0470_page_turn_release(void);

/// `area` 是逻辑坐标；无可用 GL16 时返回 `EPD_DRAW_NO_PHASES_AVAILABLE`，不刷屏。
/// `area` uses logical coordinates; unavailable GL16 phases return without scanning.
/// 调用方负责 FAST 扫描与 HV 轨保活。/ Caller owns FAST scanning and HV rail keepalive.
enum EpdDrawError e0470_page_turn(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir
);

#ifdef __cplusplus
}
#endif

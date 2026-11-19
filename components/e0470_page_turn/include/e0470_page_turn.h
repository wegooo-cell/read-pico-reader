/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 基于 MindReset Read Pico 官方 E0470 波形与刷新路径实现。
 * Built on the MindReset Read Pico E0470 waveform and refresh path.
 * 错相揭页引擎。应用只依赖本公开接口，不依赖条带或 LUT 的内部布局。
 * Staggered page-turn engine. Applications depend on this public API, not the internal band or LUT layout.
 * 主页及通用入口保留8带、11090us节拍；S31阅读由板级出口选择四带尾合并，原错开启动接口仍可用。
 * Main/generic outlets retain eight bands at11090us; the S31 reader selects four bands with a paired tail in its board outlet, retaining the spaced-launch API.
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

#define E0470_TURN_DEFAULT_TICK_US 12000
// PR17快档只缩短软件补等，不改变单次扫描或每像素相位。/ PR17 fast pacing changes only software padding, never a scan or a pixel's phases.
#define E0470_TURN_FAST_TICK_US 11090
#define E0470_TURN_FAST_BANDS 8

const char* e0470_turn_dir_name(e0470_turn_dir_t dir);

/// 每拍目标间隔；默认12ms，实际扫描更慢时不截短；不改变驱动脉冲宽度。/ Target tick interval: 12ms; never truncate a slower scan or change pulse widths.
void e0470_page_turn_set_tick_us(int us);
int e0470_page_turn_tick_us(void);
/// 离开阅读时释放按需分配的相位表。/ Release the lazily allocated phase table when leaving the reader.
void e0470_page_turn_release(void);

/// `area` 是逻辑坐标；无可用 GL16 时返回 `EPD_DRAW_NO_PHASES_AVAILABLE`，不刷屏。
/// `area` uses logical coordinates; unavailable GL16 phases return without scanning.
/// 阅读8带按一拍一带启动，37相共44拍；调用方选择11090us并负责FAST扫描与HV轨保活。
/// Reader launches 8 bands one per tick, totaling 44 ticks for 37 phases; the caller selects 11090us and owns FAST scanning and HV rail keepalive.
enum EpdDrawError e0470_page_turn(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir
);

/// 显式灰阶序列与2..32条带；阅读入口固定原GL16/8带。/ Explicit gray sequence and 2..32 bands; the reader entry uses original GL16/8 bands.
enum EpdDrawError e0470_page_turn_with_waveform(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir,
    const EpdWaveform* waveform, unsigned bands
);

/// 同步普通翻页用1带，水波纹用2..32带。/ Use one synchronous band for ordinary turns, 2..32 for ripple.
/// 显式序列的一拍一带入口；保留波形、指定条带数与当前节拍，8带37相共44拍。/ Compact explicit sequence: retain waveform, requested bands and current pacing; eight bands/37 phases take 44 ticks.
enum EpdDrawError e0470_page_turn_with_waveform_compact(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir,
    const EpdWaveform* waveform, unsigned bands
);

#if KIIKO_BOARD_METALIO47
/// S31阅读专用：尾部两带同步启动和收尾，每像素相位完整；主页/侧滑保持原条带入口。
/// S31 reader only: launch/finish the last pair together with every pixel phase intact; main pages/edge-back retain the original entry.
enum EpdDrawError e0470_reader_ripple_with_waveform(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir,
    const EpdWaveform* waveform, unsigned bands
);

/// S31普通文字单带：一次提交整轮，复用内部LUT；仅跳过实际全保持前导相，保留一相准备及所有有效/尾相。
/// S31 single-band text: batch through the internal LUT; omit only actual all-hold leading phases, retaining one setup phase and every active/tail phase.
enum EpdDrawError e0470_text_turn_with_waveform(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir, const EpdWaveform* waveform
);
enum EpdDrawError e0470_text_turn_with_prefill(
    EpdiyHighlevelState* hl, EpdRect area, const EpdWaveform* waveform
);
#endif

/// 倒置竖屏的上下柔和区与中间校准区同时逐相输出；区域须全逻辑宽、32列对齐。
/// Inverted portrait: output soft chrome and calibrated middle art in the same phases; full logical width and 32-column alignment are required.
/// S31外裁剪只需偶数边界，列选择映射向外对齐但驱动掩码仍精确；中间校准区仍32列对齐。/ S31 outer crops may be even-aligned: LUT mapping expands while drive masks stay exact; calibrated boundaries remain32-aligned.
/// S31外围也允许显式DU表；缺GL模式时才选择DU，灰阶区始终保留指定模式。
/// S31 chrome also accepts an explicit DU table when GL is absent; art retains its specified mode.
/// 不错相、不改灰阶序列；仅临时2048B PSRAM，成功后一次提交真实back，失败留给调用方恢复。
/// No staggering or sequence changes; use 2048 temporary PSRAM bytes, commit the real baseline once on success and let the caller recover failures.
enum EpdDrawError e0470_update_regions(
    EpdiyHighlevelState* hl, EpdRect area, EpdRect strong_area,
    const EpdWaveform* soft_waveform, const EpdWaveform* strong_waveform,
    enum EpdDrawMode strong_mode
);

#if KIIKO_BOARD_METALIO47
/// S31列表原厂GC48清理：精确裁剪，不扩展到题头/底栏；成功才提交真实back。
/// S31 list cleanup with raw GC48: exact crop without header/footer expansion; commit the real baseline only on success.
enum EpdDrawError e0470_update_native_area(EpdiyHighlevelState* hl,EpdRect area,const EpdWaveform* waveform);
#endif

#ifdef __cplusplus
}
#endif

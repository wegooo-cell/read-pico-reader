/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：S31独立文字水波纹实验，不改变原厂波形和电源参数。
 * English: Isolated S31 text-ripple experiment, retaining vendor tables and power parameters.
 */
#pragma once
#include <stdbool.h>
#include "epd_highlevel.h"

#define METALIO_TEXT_RIPPLE_PHASES 22
#define METALIO_TEXT_RIPPLE_BANDS 8
#define METALIO_TEXT_RIPPLE_CLEAN_PAGES 10

/// 仅量化目标区域，保留真实旧帧；失败扫描由原显示出口恢复。/ Quantize only the target crop, retaining real history and the display outlet's failure recovery.
bool metalio_text_ripple_prepare(EpdiyHighlevelState* hl, EpdRect area);
/// 独立生成的实验表；不可用或本次开机已降级时返回NULL。/ Independently generated experimental table; NULL when unavailable or disabled for this boot.
const EpdWaveform* metalio_text_ripple_waveform(void);
/// 阅读水波纹保留22相迁移，原尾保持前增加六相目标白，包含等白。/ Reader ripple retains22 transition phases, inserting six white-target phases including equal whites before settling.
const EpdWaveform* metalio_text_ripple_clean_waveform(void);
/// RC110原版六相尾部擦白，仅供显式本地回退测试。/ Original RC110 six-phase tail erase for explicit local comparison.
const EpdWaveform* metalio_text_ripple_rc110_waveform(void);
/// 扫描失败后本次开机回到完整文字波形，不写入设置。/ Return to complete text phases after a scan failure, without persisting settings.
void metalio_text_ripple_disable(void);

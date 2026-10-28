/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epdiy.h"
#include <stdbool.h>

/// 柔和文字序列：白底到中灰的少量回浅脉冲提前；具体阅读模式由板级出口选择。
/// Soft text phases advance a few gray-edge settling pulses; board output selects the reading modes that use them.
const EpdWaveform* display_reader_text_waveform(void);

/// 普通阅读差分的柔和档：厂商 GL16 原序，仅黑→黑不驱动；中灰等灰保留厂商刷新稳住抗锯齿灰边；
/// 末尾四相擦白目标白、压黑目标黑，插在定稿保持之前。
/// Ordinary reader differences: vendor GL16 order, only black-on-black held; equal midgrays
/// keep the vendor refresh so antialiased edges stay put; four trailing contrast phases
/// erase white targets and darken black targets before the settle holds.
const EpdWaveform* display_reader_soft_turn_waveform(void);

#include "epd_highlevel.h"
/// 速读复用各板原厂DU相位：Pico用MODE_DU，S31使用批量接口别名。/ Fast BW retains board-native DU phases: MODE_DU on Pico, batch-mode alias on S31.
const EpdWaveform* display_reader_fast_text_waveform(void);
/// 只归一文字区域目标，保留真实back和区域外灰阶。/ Normalize only the text target, retaining real history and outside grays.
bool display_reader_fast_text_prepare(EpdiyHighlevelState* hl,EpdRect area);

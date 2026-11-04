/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "epdiy.h"
#include <stdbool.h>

/// 柔和文字序列：白底到中灰的少量回浅脉冲提前；具体阅读模式由板级出口选择。
/// Soft text phases advance a few gray-edge settling pulses; board output selects the reading modes that use them.
const EpdWaveform* display_reader_text_waveform(void);
#if !KIIKO_BOARD_METALIO47
/// Pico reading ripple retains its gray sequence plus four changed-white phases before settling.
const EpdWaveform* display_reader_water_waveform(void);
#endif

#include "epd_highlevel.h"
/// Pico速读保留原厂DU；S31保留所选DU原序，定稿前追加四相目标白，使用批量模式别名。
/// Pico FastBW retains native DU; S31 retains the selected DU order, inserts four white-target phases before settling and aliases the batch mode.
const EpdWaveform* display_reader_fast_text_waveform(void);
/// 两板普通翻页分别派生本板波形：Pico等灰保持、变白补四相；S31沿Moku GL37补六相目标白。
/// Ordinary uses board-native profiles: Pico holds equal grays and adds four changed-white phases; S31 retains Moku GL37 plus six white-target phases.
const EpdWaveform* display_reader_soft_turn_waveform(void);
#if KIIKO_BOARD_METALIO47
/// S31纯文字翻页：白→中灰改为白侧直接推黑（0.40标定），灰边与黑白端点同段出现。
/// S31 text-only turn: white-origin midgrays push dark directly from white (0.40 calibration),
/// so gray edges appear with the black/white endpoints instead of the vendor tail.
const EpdWaveform* display_reader_text_turn_waveform(void);
#endif
/// 只归一文字区域目标，保留真实back和区域外灰阶。/ Normalize only the text target, retaining real history and outside grays.
bool display_reader_fast_text_prepare(EpdiyHighlevelState* hl,EpdRect area);

#if KIIKO_BOARD_METALIO47
/// 两个阅读设置面板的显式本地四灰实验，默认false。/ Explicit local four-gray experiment for the two primary reader settings; false by default.
bool display_reader_fast_panels_enabled(void);
/// UI目标映射0/5/10/15，近白控件底色保留浅灰，不改区域外正文/历史帧。
/// Map UI targets to0/5/10/15,retaining near-white control fills as light gray and outside body/history pixels.
bool display_reader_panel_gray4_prepare(uint8_t* target,EpdRect area);
/// 两个面板四灰实验：进入时26相统一重建中灰（含等灰），反馈16相；关闭实验返回NULL。
/// Four-gray panels:26-phase uniform midgray entry including equal pairs,16-phase feedback; NULL when disabled.
const EpdWaveform* display_reader_panel_gray4_waveform(bool erase_underlay);
/// 本地普通/水波纹共用的22相是否可用；常规构建恒false。/ Shared local Ordinary/Ripple22-phase availability; false in regular builds.
bool display_reader_fast_ripple_enabled(void);
#endif

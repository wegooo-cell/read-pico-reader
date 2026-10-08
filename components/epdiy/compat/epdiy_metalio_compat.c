/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * 中文：Metalio 板用厂商示例固件的 epdiy，而本仓库的固件调用了几个人家没有的函数。
 * 这里补齐那四个，让上层不用改：
 *   - epd_hl_update_area_full  → 转调例程的 epd_hl_update_area
 *   - epd_set_leading_skip     → 本仓库 fork 的差分刷新优化，例程没有；空实现
 *   - epd_lcd_set_prefill_lines→ 同上，空实现
 * 空实现只是少了优化，不影响画面正确性。
 *
 * English: the Metalio board uses the vendor demo firmware's epdiy, but this repo's firmware
 * calls a few functions it does not define. These four fill the gap so the layers above need no
 * changes: update_area_full forwards to the demo's epd_hl_update_area, while leading_skip and
 * prefill_lines are this fork's diff-refresh optimisations and become no-ops. Losing them costs
 * speed, not correctness.
 */

#include "epd_highlevel.h"
#include "epdiy.h"

/// 本仓库 fork 的前导相位跳过是本地优化，例程没有对应概念。
/// / The leading-phase skip is a local optimisation in this repo's fork; the demo has no
/// equivalent.
void epd_set_leading_skip(bool enable) {
    (void)enable;
}

/// 预填行数同样只影响吞吐，不影响结果。
/// / The prefill line count likewise only affects throughput, not the result.
void epd_lcd_set_prefill_lines(int lines) {
    (void)lines;
}

/// 强刷区域：例程的 epd_hl_update_area 已经是整区域刷新，语义一致。
/// / Forced area refresh: the demo's epd_hl_update_area already refreshes the whole area.
enum EpdDrawError epd_hl_update_area_full(
    EpdiyHighlevelState* state, enum EpdDrawMode mode, int temperature, EpdRect area
) {
    return epd_hl_update_area(state, mode, temperature, area);
}

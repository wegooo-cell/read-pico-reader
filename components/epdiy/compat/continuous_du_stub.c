/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：compat/continuous_du.h 那些接口的空实现。写在这里而不是做成 static inline，
 * 是为了避免"定义了但没用到"在 -Werror 下变成错误。
 *
 * English: the no-op implementations behind compat/continuous_du.h. They live in a .c rather
 * than as static inlines so that an unused definition does not become an error under -Werror.
 */

#include "continuous_du.h"

int continuous_du_dark_phases(void) {
    return CONTINUOUS_DARK_PHASES;
}

int continuous_du_light_phases(void) {
    return CONTINUOUS_LIGHT_PHASES;
}

/// 返回 false：上层据此认为"连续 DU 不可用"，走普通刷新。
/// / Returns false so the caller treats continuous DU as unavailable and uses the normal path.
bool continuous_du_init(void) {
    return false;
}

void continuous_du_deinit(void) {}

void continuous_du_reset(void) {}

bool continuous_du_busy(void) {
    return false;
}

/// 逻辑坐标与物理坐标在本板上一致（旋转由 epdiy 自己处理），所以原样透传。
/// / Logical and physical coordinates coincide here (epdiy handles rotation), so pass through.
void continuous_du_from_logical(int lx, int ly, int* px, int* py) {
    if (px != NULL) *px = lx;
    if (py != NULL) *py = ly;
}

EpdRect continuous_du_rect_from_logical(EpdRect logical) {
    return logical;
}

void continuous_du_mark_circle(int cx, int cy, int radius, int phases) {
    (void)cx;
    (void)cy;
    (void)radius;
    (void)phases;
}

void continuous_du_mark_rect(EpdRect area, int phases) {
    (void)area;
    (void)phases;
}

void continuous_du_mark_diff(
    const uint8_t* to, const uint8_t* from, EpdRect area, int phases, bool invert
) {
    (void)to;
    (void)from;
    (void)area;
    (void)phases;
    (void)invert;
}

/// 报告成功但不做任何扫描：调用方按"这一相位已完成"继续，不会卡住。
/// / Reports success without scanning, so the caller moves on instead of stalling.
enum EpdDrawError continuous_du_scan(EpdiyHighlevelState* hl, EpdRect area) {
    (void)hl;
    (void)area;
    return EPD_DRAW_SUCCESS;
}

/* ---- 错相揭页 / Staggered page turn ---- */

const char* e0470_turn_dir_name(e0470_turn_dir_t dir) {
    switch (dir) {
        case E0470_TURN_LTR: return "ltr";
        case E0470_TURN_RTL: return "rtl";
        case E0470_TURN_TTB: return "ttb";
        case E0470_TURN_BTT: return "btt";
        default: return "?";
    }
}

void e0470_page_turn_set_tick_us(int us) {
    (void)us;
}

int e0470_page_turn_tick_us(void) {
    return E0470_TURN_DEFAULT_TICK_US;
}

void e0470_page_turn_release(void) {}

/// 返回"没有可用相位"，调用方据此跳过揭页动画而不是刷一半。
/// / Reports no phases available so the caller skips the animation rather than half-refreshing.
enum EpdDrawError e0470_page_turn(EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir) {
    (void)hl;
    (void)area;
    (void)dir;
    return EPD_DRAW_NO_PHASES_AVAILABLE;
}

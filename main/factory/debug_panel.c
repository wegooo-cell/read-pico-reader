/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 调试面板。入口：开机时按住音量键（FCA9555 P1.0/P1.1 低有效）。
 * 面板提供：
 *   - VCOM 校准（进现有标定页，值存 NVS，写运行寄存器）
 *   - 防褪色重刷间隔（关 / 1 / 3 / 5 / 10 / 30 分钟，静置到点整屏 GC16 重推）
 *   - 立即整屏重刷（强光下变淡后一键把画面推回来）
 *   - 抗锯齿微调（覆盖率 gamma / 补字灰边偏置 / 补字墨量；数值用于后续固化默认）
 *   - 继续开机
 *
 * Debug panel, entered by holding a volume key while powering on. It exposes VCOM
 * calibration, the anti-fade refresh interval, a manual full-frame refresh,
 * anti-aliasing fine tuning (debug-only numbers for later baking) and continue-to-boot.
 */

#include "debug_panel.h"

#include <math.h>
#include <stdio.h>

#include "anti_fade.h"
#include "app_font_context.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "metalio_hw.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "vcom_setup.h"

#define TAG "debug_panel"

#define DEBUG_ROWS 5
#define DEBUG_ROW_VCOM 0
#define DEBUG_ROW_ANTI_FADE 1
#define DEBUG_ROW_REFRESH 2
#define DEBUG_ROW_AA 3
#define DEBUG_ROW_CONTINUE 4

static const unsigned k_anti_fade_steps[] = {0, 1, 3, 5, 10, 30};
#define ANTI_FADE_STEP_COUNT ((int)(sizeof(k_anti_fade_steps) / sizeof(k_anti_fade_steps[0])))

static EpdRect debug_row_rect(int index) {
    const int x = UI_MARGIN;
    const int width = UI_LOCK_WIDTH - 2 * UI_MARGIN;
    const int y = UI_CONTENT_TOP + 16 + index * (UI_BTN_H + UI_GAP);
    return (EpdRect){ .x = x, .y = y, .width = width, .height = UI_BTN_H };
}

static void debug_format_vcom(char* out, size_t size, int mv) {
    if (mv >= 0) {
        snprintf(out, size, "-%d.%02d V", mv / 1000, (mv % 1000) / 10);
    } else {
        snprintf(out, size, "读取失败");
    }
}

static void debug_labels(char labels[DEBUG_ROWS][72]) {
    char chip[24];
    debug_format_vcom(chip, sizeof(chip), metalio_tps_vcom_mv());
    snprintf(labels[DEBUG_ROW_VCOM], 72, "VCOM 校准  %s", chip);

    const unsigned anti_fade = anti_fade_minutes();
    if (anti_fade == 0) {
        snprintf(labels[DEBUG_ROW_ANTI_FADE], 72, "防褪色重刷  关闭");
    } else {
        snprintf(labels[DEBUG_ROW_ANTI_FADE], 72, "防褪色重刷  %u 分钟", anti_fade);
    }

    snprintf(labels[DEBUG_ROW_REFRESH], 72, "立即整屏重刷");
    snprintf(labels[DEBUG_ROW_AA], 72, "抗锯齿微调 AA");
    snprintf(labels[DEBUG_ROW_CONTINUE], 72, "继续开机 Continue");
}

static void debug_draw(uint8_t* fb) {
    ui_clear_page(fb);
    char chip[24];
    debug_format_vcom(chip, sizeof(chip), metalio_tps_vcom_mv());
    char subtitle[96];
    snprintf(
        subtitle, sizeof(subtitle), "最近回读 VCOM %s；点按下方执行，Continue 退出", chip
    );
    ui_draw_header(fb, "调试 Debug", subtitle);
    char labels[DEBUG_ROWS][72];
    debug_labels(labels);
    for (int i = 0; i < DEBUG_ROWS; i++) {
        ui_draw_button(fb, debug_row_rect(i), labels[i], false);
    }
}

static void debug_present(EpdiyHighlevelState* hl) {
    guard_draw_result(hl, update_display_full(hl));
}

static void debug_wait_release(cst836u_handle_t tp) {
    for (;;) {
        cst836u_touch_t touch;
        if (cst836u_read(tp, &touch) == ESP_OK && !touch.touched) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 间隔在 关 / 1 / 3 / 5 / 10 / 30 分钟之间循环。
// Cycle the anti-fade interval through off / 1 / 3 / 5 / 10 / 30 minutes.
static void debug_cycle_anti_fade(void) {
    const unsigned current = anti_fade_minutes();
    int index = 0;
    for (int i = 0; i < ANTI_FADE_STEP_COUNT; i++) {
        if (k_anti_fade_steps[i] == current) {
            index = i;
            break;
        }
    }
    const unsigned next = k_anti_fade_steps[(index + 1) % ANTI_FADE_STEP_COUNT];
    anti_fade_set_minutes(next);
    ESP_LOGI(TAG, "anti-fade interval -> %u min", anti_fade_minutes());
}

/* ---- 抗锯齿微调 / AA fine tuning ---- */

// 用户2026-11-12：调试页手动微调抗锯齿。三个参数：
//   覆盖率 gamma     —— 原生 TTF 字形半透明边缘的覆盖率曲线（越大边缘越深）
//   补字边缘偏置     —— 补字（PIF4）灰边整体偏移，正数变浅、负数变重
//   补字墨量         —— 补字目标墨量百分比，只缩放补字，不动原生字形
// 数值显示在屏上并打印到串口，用户回报后再固化默认。
// User2026-11-12: debug-page manual AA tuning. Three parameters: coverage gamma for native
// TTF edges, supplement (PIF4) edge bias and supplement ink percent. The page shows and logs
// the numbers so they can be reported and then baked into the defaults.
#define AA_PARAM_GAMMA 0
#define AA_PARAM_BIAS 1
#define AA_PARAM_INK 2
#define AA_PARAM_COUNT 3
#define AA_ROW_TOGGLE AA_PARAM_COUNT
#define AA_ROW_RESET (AA_PARAM_COUNT + 1)
#define AA_ROW_BACK (AA_PARAM_COUNT + 2)
#define AA_ROW_COUNT (AA_PARAM_COUNT + 3)
#define AA_SAMPLE_TOP 230
#define AA_ROWS_TOP 520

static EpdRect aa_row_rect(int index) {
    const int h = 84, gap = 10;
    return (EpdRect){UI_MARGIN, AA_ROWS_TOP + index * (h + gap),
                     UI_LOCK_WIDTH - 2 * UI_MARGIN, h};
}
static EpdRect aa_minus_rect(int index) {
    EpdRect r = aa_row_rect(index);
    return (EpdRect){r.x + r.width - 2 * 92 - 8, r.y + 4, 92, r.height - 8};
}
static EpdRect aa_plus_rect(int index) {
    EpdRect r = aa_row_rect(index);
    return (EpdRect){r.x + r.width - 92, r.y + 4, 92, r.height - 8};
}

static void aa_draw_sample(uint8_t *fb) {
    // 三档字号覆盖正文常用字号；最后一行专门用补字字形看灰边。
    // Three sizes cover common body sizes; the last line exercises supplement glyphs.
    ttf_draw_text_px(fb, UI_MARGIN, AA_SAMPLE_TOP, 28,
                     "永字八法 抗锯齿微调 灰度边缘 0123", EPD_DRAW_ALIGN_LEFT, 0, 15);
    ttf_draw_text_px(fb, UI_MARGIN, AA_SAMPLE_TOP + 52, 34,
                     "阅读正文 蹉跎龃龉 補字邊緣", EPD_DRAW_ALIGN_LEFT, 0, 15);
    ttf_draw_text_px(fb, UI_MARGIN, AA_SAMPLE_TOP + 112, 44,
                     "墨量校正 Aa 12345", EPD_DRAW_ALIGN_LEFT, 0, 15);
    ttf_draw_text_px(fb, UI_MARGIN, AA_SAMPLE_TOP + 184, 56,
                     "永字八法", EPD_DRAW_ALIGN_LEFT, 0, 15);
    ttf_draw_text_px(fb, UI_MARGIN, AA_SAMPLE_TOP + 250, 30,
                     "补字样例 㤘㧐 阅读灰边", EPD_DRAW_ALIGN_LEFT, 0, 15);
}

static void aa_draw_value_row(uint8_t *fb, int row, const char *label, const char *value) {
    EpdRect r = aa_row_rect(row);
    ui_draw_round_rect(fb, r, 12, UI_GRAY_LIGHT);
    ui_text_vc(fb, r.x + 20, r.y + r.height / 2, 26, label, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, r.x + r.width - 2 * 92 - 16, r.y + r.height / 2, 30, value,
               EPD_DRAW_ALIGN_RIGHT, false);
}

static void aa_draw(uint8_t *fb) {
    ui_clear_page(fb);
    ui_draw_header(fb, "抗锯齿微调 AA", "- / + 微调；返回后数值打印到串口");
    ui_text(fb, UI_MARGIN, 186, 22,
            "样例：正文/补字灰边（黑字白底，实际阅读墨色）", EPD_DRAW_ALIGN_LEFT, false);
    aa_draw_sample(fb);
    char value[32];
    snprintf(value, sizeof(value), "%.3f", (double)ttf_aa_tune_get_cover_gamma());
    aa_draw_value_row(fb, AA_PARAM_GAMMA, "覆盖率 gamma", value);
    snprintf(value, sizeof(value), "%d", ttf_aa_tune_get_edge_bias());
    aa_draw_value_row(fb, AA_PARAM_BIAS, "补字边缘偏置", value);
    snprintf(value, sizeof(value), "%d%%", ttf_aa_tune_get_ink_percent());
    aa_draw_value_row(fb, AA_PARAM_INK, "补字墨量", value);
    aa_draw_value_row(fb, AA_ROW_TOGGLE, "抗锯齿", ttf_get_antialias() ? "开" : "关");
    for (int i = 0; i < AA_PARAM_COUNT; i++) {
        ui_draw_button(fb, aa_minus_rect(i), "-", false);
        ui_draw_button(fb, aa_plus_rect(i), "+", false);
    }
    ui_draw_button(fb, aa_row_rect(AA_ROW_RESET), "恢复默认", false);
    ui_draw_button(fb, aa_row_rect(AA_ROW_BACK), "返回 Back", false);
}

static void aa_log_state(const char *stage) {
    ESP_LOGI(TAG, "AA TUNE %s gamma=%.3f bias=%d ink=%d aa=%d", stage,
             (double)ttf_aa_tune_get_cover_gamma(), ttf_aa_tune_get_edge_bias(),
             ttf_aa_tune_get_ink_percent(), ttf_get_antialias() ? 1 : 0);
}

static void aa_adjust(int param, int direction) {
    if (param == AA_PARAM_GAMMA) {
        int milli = (int)lroundf(ttf_aa_tune_get_cover_gamma() * 1000.f);
        milli += direction * 25;
        if (milli < 200) milli = 200;
        if (milli > 1600) milli = 1600;
        ttf_aa_tune_set_cover_gamma((float)milli / 1000.f);
    } else if (param == AA_PARAM_BIAS) {
        int bias = ttf_aa_tune_get_edge_bias() + direction * 256;
        ttf_aa_tune_set_edge_bias(bias);
    } else if (param == AA_PARAM_INK) {
        int ink = ttf_aa_tune_get_ink_percent() + direction * 2;
        ttf_aa_tune_set_ink_percent(ink);
    }
}

static void aa_tune_run(EpdiyHighlevelState *hl, uint8_t *fb, cst836u_handle_t tp) {
    // 用阅读字体渲染样例：正文缺字才会走补字灰边路径。
    // Render the sample with the reader face; only missing body glyphs use the supplement path.
    app_font_activate_reading();
    aa_log_state("enter");
    bool redraw = true;
    bool leave = false;
    while (!leave) {
        if (redraw) {
            aa_draw(fb);
            debug_present(hl);
            redraw = false;
            aa_log_state("page");
        }
        cst836u_touch_t touch;
        if (cst836u_read(tp, &touch) != ESP_OK || !touch.touched) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (cst836u_read(tp, &touch) != ESP_OK || !touch.touched) continue;
        debug_wait_release(tp);

        if (ui_rect_hit(aa_row_rect(AA_ROW_BACK), touch.x, touch.y)) {
            leave = true;
        } else if (ui_rect_hit(aa_row_rect(AA_ROW_RESET), touch.x, touch.y)) {
            ttf_aa_tune_reset();
            ttf_set_antialias(true);
            redraw = true;
        } else if (ui_rect_hit(aa_row_rect(AA_ROW_TOGGLE), touch.x, touch.y)) {
            ttf_set_antialias(!ttf_get_antialias());
            redraw = true;
        } else {
            for (int param = 0; param < AA_PARAM_COUNT; param++) {
                if (ui_rect_hit(aa_minus_rect(param), touch.x, touch.y)) {
                    aa_adjust(param, -1);
                    redraw = true;
                    break;
                }
                if (ui_rect_hit(aa_plus_rect(param), touch.x, touch.y)) {
                    aa_adjust(param, +1);
                    redraw = true;
                    break;
                }
            }
        }
    }
    aa_log_state("exit");
    app_font_activate_system();
}

void debug_panel_run(EpdiyHighlevelState* hl, uint8_t* fb, cst836u_handle_t tp) {
    // 只在需要时才重画+整屏推；空闲时只读触摸，不能每轮都整屏刷新。
    // Redraw/push only when something changed; idle iterations must not refresh.
    bool redraw = true;
    for (;;) {
        if (redraw) {
            debug_draw(fb);
            debug_present(hl);
            redraw = false;
        }

        // 等一次按下再处理，避免同一次触摸连触两下。
        // Wait for one press, then handle it; ignore the held state afterwards.
        cst836u_touch_t touch;
        if (cst836u_read(tp, &touch) != ESP_OK || !touch.touched) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (cst836u_read(tp, &touch) != ESP_OK || !touch.touched) continue;

        if (ui_rect_hit(debug_row_rect(DEBUG_ROW_VCOM), touch.x, touch.y)) {
            debug_wait_release(tp);
            vcom_setup_run(hl, fb, tp);
            debug_wait_release(tp);
            redraw = true;
            continue;
        }
        if (ui_rect_hit(debug_row_rect(DEBUG_ROW_ANTI_FADE), touch.x, touch.y)) {
            debug_wait_release(tp);
            debug_cycle_anti_fade();
            redraw = true;
            continue;
        }
        if (ui_rect_hit(debug_row_rect(DEBUG_ROW_REFRESH), touch.x, touch.y)) {
            // 整屏 GC16 全像素重刷：强光照射后点一下，检验画面能否推回。
            // Full-pixel GC16 of the current page: after strong light exposure this
            // checks whether the frame can be driven back.
            debug_wait_release(tp);
            debug_present(hl);
            continue;
        }
        if (ui_rect_hit(debug_row_rect(DEBUG_ROW_AA), touch.x, touch.y)) {
            debug_wait_release(tp);
            aa_tune_run(hl, fb, tp);
            debug_wait_release(tp);
            redraw = true;
            continue;
        }
        if (ui_rect_hit(debug_row_rect(DEBUG_ROW_CONTINUE), touch.x, touch.y)) {
            debug_wait_release(tp);
            return;
        }
    }
}

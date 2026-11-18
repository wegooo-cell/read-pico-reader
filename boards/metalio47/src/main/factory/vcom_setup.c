/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * VCOM 标定页。Metalio S31 没有 Pico PMU：值改为写 ESP NVS，写入后断电重上电让
 * TPS65185 按新值上电；开机时按住音量键进入本页。
 *
 * VCOM calibration page. The Metalio S31 board has no Pico PMU, so the value is
 * stored in ESP NVS and applied on the next rail power-up; hold a volume key
 * while powering on to enter the page.
 *
 * 面板黑度不足（整屏偏淡、全刷也回不来）时先用这里扫 VCOM：900..4000mV、10mV 一档，
 * 反复调直到黑最黑、残影最少。只改运行时寄存器与 NVS，不写 TPS65185 的永久 OTP。
 * Sweep VCOM here when the whole panel looks washed out: 900..4000 mV in 10 mV
 * steps until the black is deepest and ghosting least. Only the runtime register
 * and NVS change; the TPS65185 OTP is never programmed.
 */

#include "vcom_setup.h"
#include "metalio_hw.h"

#include <stdio.h>
#include <string.h>

#include "display.h"
#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ui_kit.h"

#define TAG "vcom_setup"

#define VCOM_MIN_MV 900
// 标定上限放宽到 4000mV（400），方便扫到面板最优点；判据是黑最深且白底/残影开始变差。
// Calibration ceiling raised to 4000 mV (digits 400) so the sweep can find the
// panel optimum; stop where black stops improving but white/ghosting worsens.
#define VCOM_MAX_MV 4000
// 用户2026-11-18指定缺省-1500mV；已保存且有效的逐机标定仍优先，不写永久OTP。
// User2026-11-18 selects a -1500mV default; valid saved per-device calibration retains priority, without permanent OTP writes.
#define VCOM_DEFAULT_MV 1500
#define VCOM_NVS_NAMESPACE "metalio_hw"
#define VCOM_NVS_KEY "vcom_mv"
#define VCOM_PAD_ROWS 4
#define VCOM_PAD_COLS 3
#define VCOM_CONFIRM_PX 168
#define VCOM_KEY_OK 11

#define VCOM_HIT_NONE (-1)
#define VCOM_HIT_DEL 10
#define VCOM_HIT_OK 11
#define VCOM_HIT_WRITE 12
#define VCOM_HIT_BACK 13

typedef struct {
    int hero_y;
    int pad_y;
    int pad_h;
    EpdRect num;
    EpdRect write;
    EpdRect back;
} vcom_geom_t;

typedef struct {
    char digits[4];
    int mv;
    bool confirm;
    bool ok_on;
} vcom_state_t;

static const char* const k_keys[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "删除 Del", "0", "确定 OK",
};
static const int k_pad_ids[12] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, VCOM_HIT_DEL, 0, VCOM_HIT_OK,
};

static vcom_geom_t vcom_geom(void) {
    const int hero_y = UI_CONTENT_TOP + 8;
    const int pad_y = UI_CONTENT_TOP + UI_PX_HERO + 72;
    const int bottom = UI_LOCK_HEIGHT - UI_MARGIN;
    const int pad_h =
        (bottom - pad_y - (VCOM_PAD_ROWS - 1) * UI_GAP) / VCOM_PAD_ROWS;
    return (vcom_geom_t){
        .hero_y = hero_y,
        .pad_y = pad_y,
        .pad_h = pad_h,
        .num = {
            .x = UI_MARGIN - 8,
            .y = hero_y - 8,
            .width = ui_content_width() + 16,
            .height = UI_PX_HERO + 40,
        },
        .write = ui_row_rect(0, 2, UI_BAR_TOP, UI_BAR_H),
        .back = ui_row_rect(1, 2, UI_BAR_TOP, UI_BAR_H),
    };
}

static EpdRect vcom_key_rect(const vcom_geom_t* g, int i) {
    return ui_grid_rect(
        i % VCOM_PAD_COLS, VCOM_PAD_COLS, i / VCOM_PAD_COLS, g->pad_y, g->pad_h
    );
}

static int vcom_typed_mv(const char* digits) {
    int typed = 0;
    if (digits == NULL) return 0;
    for (const char* p = digits; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return 0;
        typed = typed * 10 + (*p - '0');
    }
    return typed * 10;
}

static void vcom_fmt_volt(char* out, size_t n, int mv) {
    snprintf(out, n, "-%d.%02d V", mv / 1000, (mv % 1000) / 10);
}

static bool vcom_digits_valid(const char* digits, int* out_mv) {
    if (digits == NULL || digits[0] == '\0') return false;
    int n = 0;
    for (const char* p = digits; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return false;
        n++;
        if (n > 3) return false;
    }
    int mv = vcom_typed_mv(digits);
    if (mv < VCOM_MIN_MV || mv > VCOM_MAX_MV) return false;
    if (out_mv != NULL) *out_mv = mv;
    return true;
}

static bool vcom_push_digit(char* digits, int d) {
    size_t n = strlen(digits);
    if (n >= 3) return false;
    digits[n] = (char)('0' + d);
    digits[n + 1] = '\0';
    return true;
}

static bool vcom_pop_digit(char* digits) {
    size_t n = strlen(digits);
    if (n == 0) return false;
    digits[n - 1] = '\0';
    return true;
}

static void draw_number(uint8_t* fb, const vcom_geom_t* g, const char* digits) {
    char shown[16];
    if (digits == NULL) digits = "";
    if (digits[0] == '\0') {
        snprintf(shown, sizeof(shown), "- —.—— V");
    } else {
        vcom_fmt_volt(shown, sizeof(shown), vcom_typed_mv(digits));
    }
    ui_clear_rect_fast(fb, g->num);
    ui_draw_hero(fb, g->hero_y, "公共电压 Panel", shown);
}

static void present_fast(EpdiyHighlevelState* hl, EpdRect area) {
    guard_draw_result(
        hl, update_display_area_with(hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area)
    );
}

static void present_setup(EpdiyHighlevelState* hl) {
    guard_draw_result(hl, update_display_mode(hl, MODE_GC16));
}

static void present_confirm(EpdiyHighlevelState* hl) {
    guard_draw_result(hl, update_display_full(hl));
}

static void sync_ok(
    uint8_t* fb, EpdiyHighlevelState* hl, const vcom_geom_t* g, bool on, bool* drawn
) {
    if (on == *drawn) return;
    *drawn = on;
    EpdRect rect = vcom_key_rect(g, VCOM_KEY_OK);
    ui_clear_rect_fast(fb, rect);
    ui_draw_button(fb, rect, k_keys[VCOM_KEY_OK], on);
    present_fast(hl, rect);
}

static void draw_setup(uint8_t* fb, const vcom_geom_t* g, const vcom_state_t* s) {
    ui_clear_page(fb);
    const int current = vcom_setup_load_mv();
    const int chip = metalio_tps_vcom_mv();
    char hint[96];
    if (chip >= 0) {
        // 同时显示"保存值 / 最近寄存器回读"：两者不一致说明保存值没生效。
        // Show the saved value next to the chip read-back; a mismatch means the
        // saved value never reached the TPS65185.
        snprintf(
            hint, sizeof(hint), "保存 -%d.%02d V / 回读 -%d.%02d V",
            current / 1000, (current % 1000) / 10, chip / 1000, (chip % 1000) / 10
        );
    } else {
        snprintf(
            hint, sizeof(hint), "保存 -%d.%02d V / 尚无回读",
            current / 1000, (current % 1000) / 10
        );
    }
    ui_draw_header(fb, "设定 VCOM Set", hint);
    draw_number(fb, g, s->digits);
    for (int i = 0; i < 12; i++) {
        ui_draw_button(fb, vcom_key_rect(g, i), k_keys[i], i == VCOM_KEY_OK && s->ok_on);
    }
}

static void draw_confirm(uint8_t* fb, const vcom_geom_t* g, int mv) {
    char volt[16];
    char milli[24];
    vcom_fmt_volt(volt, sizeof(volt), mv);
    snprintf(milli, sizeof(milli), "%d mV", mv);

    ui_clear_page(fb);
    ui_draw_header(fb, "确认 VCOM Confirm", "请核对大号数值，再写入。");
    const int cx = epd_rotated_display_width() / 2;
    const int y0 = UI_CONTENT_TOP + 80;
    ui_text(fb, cx, y0, VCOM_CONFIRM_PX, volt, EPD_DRAW_ALIGN_CENTER, false);
    ui_text(
        fb, cx, y0 + VCOM_CONFIRM_PX + 24, UI_PX_TITLE, milli, EPD_DRAW_ALIGN_CENTER, false
    );
    ui_draw_button(fb, g->write, "写入 Write", false);
    ui_draw_button(fb, g->back, "改回 Back", false);
}

static void show_setup(
    EpdiyHighlevelState* hl, uint8_t* fb, const vcom_geom_t* g, vcom_state_t* s
) {
    s->ok_on = vcom_digits_valid(s->digits, NULL);
    draw_setup(fb, g, s);
    present_setup(hl);
}

static void show_confirm(
    EpdiyHighlevelState* hl, uint8_t* fb, const vcom_geom_t* g, int mv
) {
    draw_confirm(fb, g, mv);
    present_confirm(hl);
}

static int vcom_hit(const vcom_geom_t* g, bool confirm, uint16_t x, uint16_t y) {
    if (confirm) {
        if (ui_rect_hit(g->write, x, y)) return VCOM_HIT_WRITE;
        if (ui_rect_hit(g->back, x, y)) return VCOM_HIT_BACK;
        return VCOM_HIT_NONE;
    }
    return ui_grid_hit(
        x, y, VCOM_PAD_COLS, VCOM_PAD_ROWS, g->pad_y, g->pad_h, k_pad_ids
    );
}

static void wait_up(cst836u_handle_t tp) {
    while (true) {
        cst836u_touch_t up;
        if (cst836u_read(tp, &up) == ESP_OK && !up.touched) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void eat_touch(cst836u_handle_t tp, bool* was_touched) {
    wait_up(tp);
    *was_touched = false;
}

int vcom_setup_load_mv(void) {
    nvs_handle_t h;
    uint16_t mv = 0;
    if (nvs_open(VCOM_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return VCOM_DEFAULT_MV;
    esp_err_t err = nvs_get_u16(h, VCOM_NVS_KEY, &mv);
    nvs_close(h);
    if (err != ESP_OK || mv < VCOM_MIN_MV || mv > VCOM_MAX_MV || mv % 10) return VCOM_DEFAULT_MV;
    return (int)mv;
}

static bool persist(int mv) {
    if (mv < VCOM_MIN_MV || mv > VCOM_MAX_MV || mv % 10) return false;
    nvs_handle_t h;
    esp_err_t err = nvs_open(VCOM_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u16(h, VCOM_NVS_KEY, (uint16_t)mv);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "VCOM NVS save: %s", esp_err_to_name(err));
        return false;
    }
    // 保存确认必须区分真实回读与默认值回退（1500mV）。
    // A failed read must not masquerade as a verified save of the default.
    uint16_t verified_mv = 0;
    err = nvs_open(VCOM_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return false;
    err = nvs_get_u16(h, VCOM_NVS_KEY, &verified_mv);
    nvs_close(h);
    if (err != ESP_OK || verified_mv != mv) return false;
    epd_set_vcom((uint16_t)mv);
    // 新 VCOM 只在下一次上电写入 TPS65185：先撤轨，再让下一帧全刷重新上电。
    // The new VCOM is written on the next rail power-up: drop the rails now and
    // let the next full refresh power them on with the new value.
    epd_poweroff();
    display_request_full_refresh();
    return true;
}

static void after_digits(
    EpdiyHighlevelState* hl, uint8_t* fb, const vcom_geom_t* g, vcom_state_t* s
) {
    draw_number(fb, g, s->digits);
    present_fast(hl, g->num);
    bool ok = vcom_digits_valid(s->digits, &s->mv);
    if (strlen(s->digits) == 3 && ok) {
        s->confirm = true;
        return;
    }
    sync_ok(fb, hl, g, ok, &s->ok_on);
}

void vcom_setup_run(EpdiyHighlevelState* hl, uint8_t* fb, cst836u_handle_t tp) {
    const vcom_geom_t g = vcom_geom();
    vcom_state_t s = { 0 };

    show_setup(hl, fb, &g, &s);

    bool was_touched = false;
    for (;;) {
        cst836u_touch_t touch;
        if (cst836u_read(tp, &touch) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        bool pressed = touch.touched && !was_touched;
        was_touched = touch.touched;
        if (!pressed) {
            vTaskDelay(pdMS_TO_TICKS(8));
            continue;
        }

        int hit = vcom_hit(&g, s.confirm, touch.x, touch.y);
        if (s.confirm) {
            if (hit == VCOM_HIT_BACK) {
                s.confirm = false;
                show_setup(hl, fb, &g, &s);
                eat_touch(tp, &was_touched);
            } else if (hit == VCOM_HIT_WRITE) {
                if (persist(s.mv)) {
                    ESP_LOGI(TAG, "panel VCOM -%d mV", s.mv);
                    wait_up(tp);
                    return;
                }
                ESP_LOGE(TAG, "VCOM_SET failed");
                eat_touch(tp, &was_touched);
            }
            continue;
        }

        bool changed = false;
        if (hit >= 0 && hit <= 9) {
            changed = vcom_push_digit(s.digits, hit);
        } else if (hit == VCOM_HIT_DEL) {
            changed = vcom_pop_digit(s.digits);
        } else if (hit == VCOM_HIT_OK && vcom_digits_valid(s.digits, &s.mv)) {
            s.confirm = true;
        } else {
            continue;
        }

        if (changed) after_digits(hl, fb, &g, &s);
        if (s.confirm) {
            eat_touch(tp, &was_touched);
            show_confirm(hl, fb, &g, s.mv);
        }
    }
}

/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：Pico高电量读数稳定器；不改PMU曲线、保护阈值或充电参数。
 * English: Stabilize Pico upper-SOC readings without changing PMU curves, safety limits or charging.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint16_t samples[5],mvs[5],shown;
    uint32_t sampled_ms,changed_ms,budget_ms,hold_ms;
    uint8_t count,next;
    bool valid,powered;
} pico_soc_filter_t;
/// 输入有效的原始千分比：中位数去抖、按时间限速变化，并用电压作硬下限。
/// Feed valid raw permille: median de-noise, time-based slew limits, and voltage as a hard floor.
uint16_t pico_soc_filter_update(pico_soc_filter_t *state,uint16_t raw,uint16_t mv,
                               bool powered,bool full,bool low,uint32_t now_ms);

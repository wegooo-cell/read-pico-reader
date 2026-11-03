/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：S31 CX25601N 充电 IC 的可编程充电电流与充电监控接口。
 * English: Programmable charge current and charge monitor interface for the S31 CX25601N.
 *
 * 冻结：只配置充电寄存器；不写永久 OTP/eFuse，不碰 TPS65185 VCOM、供电顺序与面板波形。
 * Frozen: Configure charger registers only; never write permanent OTP/eFuse, touch TPS65185
 * VCOM, rail ordering or panel waveforms.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/// 标准充电电流与快充电流，驱动按芯片 80mA 步进向下量化。
/// Standard and fast charge currents; the driver quantizes down in 80mA steps.
#define METALIO_CHARGER_STD_MA  1000
#define METALIO_CHARGER_FAST_MA 1500

/// 探测 0x6B、写默认充电参数并启动监控任务；可重复调用，成功后直接返回 ESP_OK。
/// Probe 0x6B, apply default charge settings and start the monitor task; idempotent after success.
esp_err_t metalio_charger_init(void);

/// 驱动是否已初始化且监控任务可用。/ Whether the driver is initialized and its monitor is up.
bool metalio_charger_ready(void);

/// 立即设置 ICHG（按 80mA 向下量化并同步 IINDPM），写入后回读校验；
/// 未就绪时记录目标并返回 ESP_ERR_INVALID_STATE，初始化成功后自动补应用。
/// Set ICHG immediately (quantized down to 80mA steps with IINDPM synced) and verify the readback;
/// while not initialized it records the target for a post-init apply and returns ESP_ERR_INVALID_STATE.
esp_err_t metalio_charger_set_current_ma(uint32_t ma);

/// 最近一次成功写入并回读校验的 ICHG 毫安值；未初始化时为 0。
/// Last ICHG value written and verified by readback; 0 before initialization.
uint32_t metalio_charger_current_ma(void);

/// 快充开关：打开用 METALIO_CHARGER_FAST_MA，关闭用 METALIO_CHARGER_STD_MA；
/// 未就绪时记录偏好并返回 ESP_ERR_INVALID_STATE，初始化成功后自动补应用。
/// Fast-charge switch: METALIO_CHARGER_FAST_MA when on, METALIO_CHARGER_STD_MA when off;
/// while not initialized it records the preference for a post-init apply and returns
/// ESP_ERR_INVALID_STATE.
esp_err_t metalio_charger_set_fast_charge(bool on);

/// 快充开关的驱动侧状态（只反映成功切换）。/ Driver-side fast-charge state (only successful switches).
bool metalio_charger_fast_charge(void);

/// 浅睡、深睡或关机事务前挂起监控任务，避免其与电源/总线事务交叉。
/// Suspend the monitor before a light-sleep, deep-sleep or power-off transaction so it cannot
/// interleave with power/bus work.
void metalio_charger_suspend(void);

/// 唤醒或事务失败回滚后恢复监控任务；与 suspend 配对，未挂起时为空操作。
/// Resume the monitor after wake-up or a failed transaction rollback; paired with suspend, and a
/// no-op when it is not suspended.
void metalio_charger_resume(void);

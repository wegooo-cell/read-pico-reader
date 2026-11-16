/* SPDX-License-Identifier: Apache-2.0
 * S31原厂SDK浅睡修复的C入口；不修改波形或永久配置。
 * C entry points for the vendor S31 sleep fix; no waveform or permanent configuration changes.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/// 初始化原厂唤醒校准修复。/ Initialize the vendor wake calibration fix.
esp_err_t metalio_sleep_sdk_init(void);
/// 保留原睡眠参数，使用原厂兼容路径进入浅睡。/ Enter light sleep through vendor compatibility with existing sleep parameters.
esp_err_t metalio_light_sleep_start(bool *power_key_confirmed);
/// 有界重放保留的复位/睡眠阶段证据，不写TF。/ Bounded replay of retained reset/sleep evidence, without TF writes.
void metalio_sleep_report_reset(void);
#ifdef __cplusplus
}
#endif

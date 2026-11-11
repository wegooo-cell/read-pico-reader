/* SPDX-License-Identifier: MIT
 * 中文：移植官方v2.0.77的S31电压曲线、滤波和单向显示步进；仅S31编译。
 * English: C port of the vendor v2.0.77 voltage curves/filter/display steps; S31 only.
 * Source: CloudZao/Metalio-E-INK4-Plus/main/boards/common/power/bq27220_gauge.{h,cc}
 * Original MIT notice: ../../../licenses/Metalio-MIT.txt
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    float samples[60],sum;
    unsigned next;
    int displayed,last_direction;
    int64_t ready_at,last_sample,last_filter,last_step,full_since;
    bool primed,full_confirmed;
} metalio_soc_filter_t;
float metalio_soc_voltage_percent(uint16_t mv,bool charging);
/// 首次使用前状态须置零；初始化/唤醒后等待3秒，保留已确认满电的滞回。/ Wait3s after initialization/wake, preserving confirmed-full hysteresis.
void metalio_soc_filter_reset(metalio_soc_filter_t *filter,int64_t now_us);
/// 无效采样返回false，不能当成0%；仅调用线程拥有该状态。/ Invalid is unknown, never0%; one calling thread owns state.
bool metalio_soc_filter_update(metalio_soc_filter_t *filter,bool valid,uint16_t mv,int16_t ma,
                               int64_t now_us,int *level,bool *charging);
#ifdef __cplusplus
}
#endif

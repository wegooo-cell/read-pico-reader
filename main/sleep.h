/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏、浅睡等待、软睡/关机下电。
 *
 * Lock, light-sleep wait, and soft-sleep / power-off rail drop.
 */

#pragma once

#include <stdint.h>

#include "epd_highlevel.h"
#include "esp_err.h"
#include "sc7a20h.h"
#include "cst836u.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WAKE_NONE = 0,
    APP_WAKE_KEY,
    APP_WAKE_PICKUP,
    APP_WAKE_TIMEOUT,
} app_wake_source_t;

/// 已确认按键唤醒后检查有效时钟下的免密期限。/ Check grace with a valid clock only after confirmed key wake.
bool lock_pin_grace_active(void);

#if KIIKO_BOARD_METALIO47
/// 解锁后继续浅睡媒体恢复，未完成时保留挂起的字体/书籍句柄。/ Continue light-sleep media recovery after unlock, retaining pending font/book handles.
bool app_sleep_media_ready(void);
/// 卡恢复优先：有界重开失败后，由主循环释放消费者并安排重挂。/ Yield failed reopen to loop-owned card recovery.
bool app_sleep_media_failed(void);
/// 无I/O快照，阻止其他字体入口绕过恢复顺序。/ IO-free snapshot protecting the media-before-font ordering.
bool app_sleep_media_pending(void);
/// Only after closing all book/font slots: replace pending wake IO with a manual recovery transaction.
void app_sleep_discard_media_io(void);
/// 主动拼贴锁屏先灭灯并显示圆锁，保留后续短按。/ Manual collage feedback retains later wake clicks.
void app_lock_feedback(EpdiyHighlevelState *hl);
/// 定时关机必须已有成功保存的恢复点。/ Require a persisted resume checkpoint before timed lock power-off.
void app_lock_set_resume_ready(bool ready);
/// 在硬件初始化前锁存深睡电源键唤醒原因。/ Capture a deep-sleep power wake before hardware initialization.
void app_lock_capture_boot_wakeup(void);
/// 深睡保留物理锁图，卡就绪后恢复真实参考；NULL在解锁绘制前取消。
/// Retain physical lock art across deep boot and restore references when media is ready; NULL cancels before unlock drawing.
bool app_lock_resume_frame(EpdiyHighlevelState *hl);
#else
/// 登记常开供电的触摸与加速度计，并准备 CPU 浅睡保持区。/ Bind always-on touch/accelerometer and prepare CPU light-sleep retention.
void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc);
/// 深睡保持原面板画面，TF就绪后恢复真实参考；不等待挂载、不改解锁规则。
/// Retain deep-boot panel art and restore true references when media is ready; NULL cancels before application rendering.
bool app_lock_resume_frame(EpdiyHighlevelState *hl);
#endif

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc, cst836u_handle_t touch,
    bool reader_background
);
#if KIIKO_BOARD_METALIO47
#else

/// 主动拼贴在保存进度/切字体前局部GC16显示圆锁，保留其余当前画面；不适用于自动休眠、直接关机或升级/上传中。
/// Present a local GC16 lock badge before manual-collage saves/font changes, retaining the surrounding frame; not for idle sleep, shutdown or OTA/uploads.
void app_lock_feedback(EpdiyHighlevelState *hl);
#endif

/// 在消费恢复/开书前进行冷启动与深睡密码验证。/ Gate cold/deep starts before consuming resume or opening books.
void app_lock_wake_single_once(void);
void app_lock_boot_gate(EpdiyHighlevelState *hl, cst836u_handle_t touch, sc7a20h_handle_t acc);

/// 等电源键松开，避免进睡瞬间被同一下按住立刻唤醒。
/// Wait for the power key to release so the same press does not wake immediately.
void app_lock_wait_key_idle(int timeout_ms);
#if KIIKO_BOARD_METALIO47
// 十分钟交接由S31等待事务内部管理，外层超时不能被当作解锁。
// The S31 wait transaction owns the ten-minute handoff; an outer timeout is never an unlock.
#define APP_LOCK_IDLE_TIMEOUT_MS 0U
#define APP_LOCK_OFF_AFTER_US INT64_C(600000000)
/// 锁屏先浅睡十分钟后同步RTC并硬关机；USB暂缓。/ Light sleep for ten minutes, sync RTC and power off; defer for USB.
#else
/// ESP 浅睡，按键或拿起唤醒。acc 为空则只等按键。
/// ESP light sleep; wake on key or pickup. Key only when acc is NULL.
#endif
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc);
/// ESP 浅睡至按键、拿起或超时；超时以毫秒计，0 表示不设期限。
/// ESP light sleep until key, pickup, or timeout; 0 means no deadline.
app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t acc, uint32_t timeout_ms);
app_wake_source_t app_last_wake_source(void);
/// 软睡/关机前停用外设并落盘；成功不返回，交接失败恢复外设并返回错误。保留调用方的阅读/密码上下文。
/// Park peripherals and sync before host sleep/off; success does not return, failure restores peripherals and returns an error with reader/PIN context intact.
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode);
/// 将当前系统时间写入常供电 PMU RTC；主控断电后由 PMU 继续走时。
/// Sync system time to the always-on PMU RTC so it keeps ticking while the host is off.
esp_err_t app_sync_time_to_pmu(void);
/// 让 PMU 正常重启主控；先保存 RTC，调用后不会返回。
/// Ask the PMU for a normal host restart after preserving RTC; does not return.
void app_restart_host(void);
#if KIIKO_BOARD_METALIO47
/// 准备S31下载口，等待电脑发起USJ硬件复位；不关闭字体/TF/显示。
/// Prepare the S31 download port for host-triggered USJ reset; retain fonts/TF/display.
esp_err_t app_enter_download(void);
bool app_download_active(void);
void app_cancel_download(void);
#else
#endif

#ifdef __cplusplus
}
#endif

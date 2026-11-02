/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 小纸 Pico SDMMC 探测、挂载、格式化。
 *
 * Read Pico SDMMC probe, mount, and format.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool present;
    bool mounted;
    bool needs_format;
    char name[8];
    uint64_t capacity_bytes;
    // capacity 是物理卡大小；已用/可用只按挂载的文件系统统计。
    // Physical card size is separate from the mounted filesystem's usable capacity.
    uint64_t filesystem_bytes;
    uint64_t free_bytes;
    bool space_valid;
    esp_err_t space_error;
    esp_err_t error;
} read_pico_sd_info_t;

/// 首次插卡探测；失效后须先关闭消费者，再由主循环安排恢复或显式remount。
/// Probe first insertion; after invalidation, close consumers before loop recovery or explicit remount.
esp_err_t read_pico_sd_start_probe(void);
/// Pico对CD做防抖；检测通信失败保留快照。失效期间不可继续用旧句柄。
/// Pico debounces CD and retains snapshots on detect faults; stale handles must not be used after invalidation.
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info);
/// 更新空间快照，不重挂载、不写卡；勿在 render 中调用。
/// Refresh allocation statistics without remounting or writing; never call from render.
esp_err_t read_pico_sd_refresh_space(void);
/// 异步只读统计；保持已挂载快照，完成后get_info返回更新结果。/ Read-only background accounting; get_info remains mounted and later exposes the result.
esp_err_t read_pico_sd_start_space_refresh(void);
/// 主循环定期调用；后台只读检查总线，USB独占期间不可调用。
/// Poll from the UI loop for background read-only bus checks; never poll during USB ownership.
void read_pico_sd_poll_health(void);
/// 查询是否需要恢复；主动sync/USB卸载不触发自动恢复。
/// Check recovery need; intentional sync/USB unmounts do not trigger recovery.
bool read_pico_sd_needs_recovery(void);
/// 无I/O的快照门禁；挂载/卸载期间后台消费者不得重新打开TF文件。
/// Snapshot-only gate; background consumers must not reopen TF files during mount/unmount.
bool read_pico_sd_io_ready(void);
/// 消费者全部释放后调用；ESP_OK表示已排队，NOT_FINISHED表示其他生命周期操作忙。
/// Call after all consumers release handles; ESP_OK means queued, NOT_FINISHED means another lifecycle owner is busy.
esp_err_t read_pico_sd_start_recovery(void);
#if KIIKO_BOARD_METALIO47
/// 读取当前已验证的卡兼容档；未挂载时为0，不触发I/O或升档。/ Read the validated mounted profile, or0 when unavailable, without IO or clock changes.
void read_pico_sd_profile(int *khz,int *width);
/// 所有消费者关闭后标记I/O会话失效；不卸载、不写卡，忙时交由下轮重试。
/// After consumers close, invalidate the IO session without unmounting/writing; retry if busy.
esp_err_t read_pico_sd_invalidate_io(void);
/// Explicit recovery after consumers close; also handles failed initial mounts and paused wake IO.
/// ESP_OK queues an asynchronous close/remount, NOT_FINISHED leaves the current lifecycle owner intact.
esp_err_t read_pico_sd_start_manual_recovery(void);
#endif
/// 显式重挂前须关闭字体、阅读及上传句柄；Pico自动恢复由主循环另行排队。
/// Close font, reader and upload handles before explicit remount; the loop separately queues Pico automatic recovery.
esp_err_t read_pico_sd_remount(void);
/// 仅显式用户确认后调用；失效挂载必须先 remount，绝不自动建议格式化拔出的卡。
/// / Explicit user confirmation only; an invalidated mount needs remount first, never a removal-triggered format suggestion.
esp_err_t read_pico_sd_format(void);
/// 已挂载则卸载落盘。探测未完成返回 ESP_ERR_NOT_FINISHED。
/// / Unmount if mounted so writes land. ESP_ERR_NOT_FINISHED while probing.
esp_err_t read_pico_sd_sync(void);

#ifdef __cplusplus
}
#endif

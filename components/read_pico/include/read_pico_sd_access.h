/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/// S31的TF访问许可；已有事务可收尾，新访问在卸载期间返回忙。/ S31 SD admission; admitted operations drain while new access is denied during transitions.
bool read_pico_sd_access_begin(void);
void read_pico_sd_access_end(void);
/// 等待全部文件、目录及在途访问归还；失败不改变挂载。/ Drain files, directories and operations; failure preserves the mount.
bool read_pico_sd_access_transition_begin(uint32_t timeout_ms);
void read_pico_sd_access_transition_end(void);
/// 仅由持有生命周期权限的驱动发布可用状态。/ Only the lifecycle owner publishes filesystem availability.
void read_pico_sd_access_set_mounted(bool mounted);

/// 完整写入事务包含关闭后的改名和清理；自动在离开作用域时归还。/ Whole write transactions include post-close rename/cleanup and release on scope exit.
typedef struct {bool allowed, active;} read_pico_sd_access_scope_t;
read_pico_sd_access_scope_t read_pico_sd_access_scope(const char *path);
static inline void read_pico_sd_access_scope_end(read_pico_sd_access_scope_t *scope) {
    if(scope->active)read_pico_sd_access_end();
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：启动保护、开书事务与一次性深睡恢复；独立于用户设置，不清除阅读记录。
 * English: Startup protection, book-open transactions and one-shot deep-sleep resume, separate from settings and reading records.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "kiiko_board_config.h"

#define PICO_BOOT_PATH_MAX 288
typedef struct {
    uint8_t tab, reader, fullscreen;
    char path[PICO_BOOT_PATH_MAX];
} pico_resume_t;

/// 在 NVS 初始化后、硬件初始化前调用；异常或未完成启动采用本次启动保护。/ Call after NVS init and before hardware; abnormal or incomplete startup enables recovery for this boot.
void pico_boot_init(bool abnormal_reset);
/// 首个完整界面输出成功后标记启动完成。/ Mark startup complete after a successful first full presentation.
void pico_boot_ready(void);
bool pico_boot_recovery(void);
/// 只观察恢复检查点，不消费也不改写Flash。/ Inspect the resume checkpoint without consuming it or writing Flash.
bool pico_boot_has_resume(void);
#if KIIKO_BOARD_METALIO47
/// 门禁前重复复位仍保留锁定；真实解锁或成功消费恢复入口后解除。/ Retain the lock across resets before the gate; clear after verified unlock or successful resume consumption.
bool pico_boot_lock_pending(void);
#endif
/// 仅刚刚保存成功的实际锁屏可用作下次深睡启动参考。/ Only a successfully persisted actual lock frame may seed the next deep-sleep boot reference.
esp_err_t pico_boot_mark_frame_cached(void);
bool pico_boot_resume_frame_cached(void);
/// 自动封面/元数据不得重试上次中断的图书；显式开书仍允许。/ Automatic artwork/metadata must not retry interrupted books; explicit opening remains allowed.
bool pico_boot_asset_allowed(const char *path);
/// 开书开始先持久化，失败不得进入解析；结束清除事务。/ Persist before parsing; failure prevents admission. Clear the transaction on completion.
esp_err_t pico_boot_book_begin(const char *path);
void pico_boot_book_end(bool success);
/// 保存锁屏前所在主页面或阅读上下文；不保存临时弹窗。/ Save the main tab or reader context before lock, excluding transient dialogs.
esp_err_t pico_boot_save_resume(const pico_resume_t *resume);
/// 启动时先在 NVS 消费恢复入口，再交给调用方；一次失败不形成重启循环。/ Consume in NVS before returning a resume target so failures cannot loop.
bool pico_boot_take_resume(pico_resume_t *resume);
/// 密码等待期间保留启动恢复；实际开书前仍先消费。/ Hold resume during authentication; still consume before opening.
void pico_boot_hold_resume(void);
void pico_boot_clear_resume(void);
/// 仅导出本次启动发现的中断图书；不把一般异常归因给最后阅读的书。/ Export only a book interrupted on this boot, never blame the last read for a generic reset.
bool pico_boot_interrupted_book(char *path, size_t capacity);
/// 离开中断图书提示后恢复日常模式，保留故障书的自动处理隔离。
/// Restore normal operation when leaving the interrupted-book notice, preserving the book's quarantine.
void pico_boot_leave_book_recovery(void);
void pico_boot_forget_interrupted_book(void);

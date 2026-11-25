/*
 * 中文：PICO机内存储检查与备份后重建；默认检查不删除数据。
 * English: Pico storage checks and verified-backup rebuild; checks never delete data.
 * 冻结：只重建固定storage分区；备份和源读回一致且第二次确认后才可擦除，不写NVS/TF格式/VCOM。
 * Frozen: rebuild only the fixed storage partition after verified backup and second confirmation; never erase NVS/TF or change VCOM.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    STORAGE_REPAIR_IDLE, STORAGE_REPAIR_PREPARE, STORAGE_REPAIR_REMOUNT,
    STORAGE_REPAIR_CHECK, STORAGE_REPAIR_COPY, STORAGE_REPAIR_VERIFY,
    STORAGE_REPAIR_RECHECK, STORAGE_REPAIR_REBUILD,
    STORAGE_REPAIR_DONE, STORAGE_REPAIR_FAILED, STORAGE_REPAIR_BACKUP_READY,
} storage_repair_phase_t;
typedef struct {
    storage_repair_phase_t phase;
    esp_err_t error, card_error, flash_error, progress_error;
    size_t done, total;
    bool card_progress, backup_verified, erase_started;
    char backup_path[192], sha256[65];
} storage_repair_status_t;

/// 设置页已退出阅读/传输后调用；backup=true对应第一次明确确认，仅创建备份。
/// Call after leaving reader/transfers; backup=true is the first explicit confirmation and only creates a backup.
esp_err_t app_storage_repair_begin(bool backup, int64_t now_ms);
/// 第二次独立确认；重新核验原分区和备份后才进入重建。/ Second confirmation; reverify source and backup before rebuilding.
esp_err_t app_storage_repair_confirm(int64_t now_ms);
bool app_storage_repair_busy(void);
storage_repair_status_t app_storage_repair_status(void);
bool app_storage_repair_tick(int64_t now_ms);
/// 离页或锁屏撤销未执行的确认；进行中由互斥门禁保护。/ Leaving/locking revokes an unexecuted confirmation; interlocks protect active work.
void app_storage_repair_cancel(void);

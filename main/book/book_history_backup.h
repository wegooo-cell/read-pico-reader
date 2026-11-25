/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "esp_err.h"

// 阅读资料按记录流附在设置备份后，避免把大量书籍进度装入内存。
// Append reading data as a stream so a large library does not require a large allocation.
esp_err_t book_history_backup_write(FILE *file);
bool book_history_backup_validate(FILE *file);
esp_err_t book_history_backup_restore(FILE *file);
esp_err_t book_history_checkpoint_write(FILE *file,const char *settings,uint32_t generation);
esp_err_t book_history_checkpoint_restore(FILE *file,const char *settings,uint32_t *generation,bool apply);

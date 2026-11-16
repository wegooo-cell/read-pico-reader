/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"

#if KIIKO_BOARD_METALIO47
bool app_sleep_media_ready(void);
bool app_sleep_media_failed(void);
bool app_sleep_media_pending(void);
bool app_sleep_book_reopen_failed(void);
void app_sleep_discard_media_io(void);
#endif

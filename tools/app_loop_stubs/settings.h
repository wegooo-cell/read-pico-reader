/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"

bool app_settings_ble_turner(void);
void app_settings_set_ble_turner(bool enabled);

uint8_t app_settings_auto_lock_minutes(void);

uint8_t app_settings_system_contrast(void);
bool app_settings_main_fast_refresh(void);

#if KIIKO_BOARD_METALIO47
typedef enum {APP_AI_REFRESH,APP_AI_BOOKMARK,APP_AI_FRONTLIGHT,APP_AI_HOME,APP_AI_NONE,APP_AI_ACTION_COUNT}app_ai_action_t;
app_ai_action_t app_settings_ai_short(void);
esp_err_t app_settings_set_ai_short(app_ai_action_t action);
#endif

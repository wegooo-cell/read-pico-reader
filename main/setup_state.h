/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：首次引导独立持久状态；老配置缺少新标记时不强制重新引导。
 * English: Persistent setup lifecycle; missing new markers never force legacy profiles through setup.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
/// 在NVS初始化成功后、任何默认值迁移写入之前调用。/ Call after successful NVS init, before writing migrated defaults.
void app_setup_init(void);
bool app_setup_pending(void);
bool app_setup_english(void);
esp_err_t app_setup_set_english(bool english);
/// 跳过与完成都需提交成功才退出，失败保留引导。/ Both Skip and Finish leave only after a successful commit.
esp_err_t app_setup_finish(void);

/// Fresh onboarding does not adopt a previous card's bookshelf/index until explicit enrollment.
bool app_setup_library_pending(void);
/// Clear only after checked library commit; failures keep the first-import boundary.
esp_err_t app_setup_library_complete(void);

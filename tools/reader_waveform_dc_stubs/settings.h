/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：波形 DC 审计测试的设置替身，只暴露低残影开关。
 * English: Settings shim for the waveform DC-audit test; only the low-ghost switch is visible.
 * 冻结：仅供主机测试。/ Frozen: host tests only.
 */
#pragma once
#include <stdbool.h>

bool app_settings_low_ghost(void);

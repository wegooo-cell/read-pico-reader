/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：本板上"错相揭页"接口的声明在 compat/continuous_du.h 里，display.h 引的是
 * e0470_page_turn.h，这里转发一下，省得改上层。
 *
 * English: on this board the staggered page-turn declarations live in compat/continuous_du.h,
 * but display.h includes e0470_page_turn.h, so forward it rather than touch the caller.
 */

#pragma once

#include "continuous_du.h"

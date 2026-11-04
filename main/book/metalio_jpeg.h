/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/// S31基线JPEG硬解、渐进式完整软解；失败由调用者重置文件后走原解码器。
/// S31 baseline hardware / full progressive software; callers rewind before legacy fallback.
bool metalio_jpeg_gray(const uint8_t *data, size_t size, FILE *file,
                       unsigned width, unsigned height, uint8_t *out);

/// 取消正在运行的S31 JPEG；调用方在卸卡前等待其协作退出。/ Cancel S31 JPEG cooperatively and join before unmounting.
void metalio_jpeg_cancel(void);
/// 本线程上次解码是否已取消；不得再进入较慢的后备解码。/ Whether this thread was cancelled; do not enter a slower fallback.
bool metalio_jpeg_cancelled(void);

/// 已接管渐进图的失败不能再走无界后备解码。/ Claimed progressive failures must not enter an unbounded fallback.
bool metalio_jpeg_fallback_allowed(void);

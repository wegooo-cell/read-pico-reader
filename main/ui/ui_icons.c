/*
 * SPDX-FileCopyrightText: 2026 zhubeilife
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：定义 KIIKO_UI_ICONS_DEFLATE 时，把生成头里压缩存放的图标掩模表在首次取用时整表解压到 PSRAM，
 * 之后一直复用。未定义时本文件为空，掩模直接取自生成头里的原始数组。
 *
 * English: With KIIKO_UI_ICONS_DEFLATE, inflate the generated header's compressed icon-mask table into
 * PSRAM on first use and reuse it afterwards. Without it this file is empty and masks come straight from
 * the raw array in the generated header.
 *
 * 冻结：不改掩模内容与绘制方式；解压失败只返回 NULL，由调用方跳过绘制。
 * Frozen: mask content and drawing stay unchanged; a failed inflate only returns NULL and callers skip drawing.
 */
#include "../assets/ui_icons.h"

#if KIIKO_UI_ICONS_DEFLATE
#include <stdbool.h>
#include <stddef.h>
#include "esp_heap_caps.h"
#include "miniz.h"

// 解压后的整表；发布前只写一次，之后只读。/ Inflated table; written once before publication, read-only after.
static uint8_t *s_masks;

const uint8_t *ui_icon_mask(ui_icon_t icon) {
    if (icon < 0 || icon >= UI_ICON_COUNT) return NULL;
    uint8_t *masks = __atomic_load_n(&s_masks, __ATOMIC_ACQUIRE);
    if (masks == NULL) {
        const size_t total = (size_t)UI_ICON_COUNT * UI_ICON_MASK_BYTES;
        uint8_t *out = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        // 解压器也放 PSRAM；便利解压函数的大型栈变量不适合 UI 任务。
        // Keep the inflater in PSRAM too; the convenience decoder's large stack frame is unsuitable for UI tasks.
        tinfl_decompressor *inflate =
            heap_caps_malloc(sizeof(*inflate), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        bool ok = out != NULL && inflate != NULL;
        if (ok) {
            size_t input = UI_ICON_DEFLATE_BYTES, output = total;
            tinfl_init(inflate);
            const tinfl_status status = tinfl_decompress(inflate, ui_icon_deflate, &input, out, out, &output,
                TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            ok = status == TINFL_STATUS_DONE && output == total && input == UI_ICON_DEFLATE_BYTES;
        }
        heap_caps_free(inflate);
        if (!ok) {
            heap_caps_free(out);
            return NULL;
        }
        // 两个任务同时首次绘制时只保留先发布的一份，另一份释放。
        // If two tasks race on the first draw, keep whichever copy is published first and free the other.
        uint8_t *expected = NULL;
        if (__atomic_compare_exchange_n(&s_masks, &expected, out, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            masks = out;
        } else {
            heap_caps_free(out);
            masks = expected;
        }
    }
    return masks + (size_t)icon * UI_ICON_MASK_BYTES;
}
#endif

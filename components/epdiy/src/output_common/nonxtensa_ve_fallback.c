/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * 中文：Xtensa 汇编（output_common/lut.S、output_common/diff.S）在非 Xtensa 目标上的等价 C 实现。
 * 那两个 .S 只在 CONFIG_IDF_TARGET_ESP32S3 下产出符号，其余目标编成空，所以这里的三个函数
 * 补上同样的符号；语义与汇编一一对应，只在实现上取了 C。
 *
 * English: C equivalents of the Xtensa assembly in output_common/lut.S and
 * output_common/diff.S for non-Xtensa targets. Those .S files only emit symbols under
 * CONFIG_IDF_TARGET_ESP32S3 and assemble to nothing elsewhere, so these three functions supply
 * the same symbols. The semantics match the assembly one for one; only the implementation is C.
 *
 * 冻结：不得改动这三个函数的语义或签名——它们与汇编、以及 render_lcd.c / lut.c 里的调用约定
 * 必须完全一致。
 * Frozen: do not change the semantics or signatures of these three functions. They must stay
 * identical to the assembly and to the call sites in render_lcd.c and lut.c.
 *
 * 移植自 Metalio E-Ink4-Plus 示例固件（MIT，Copyright (c) 2025 Shenzhen Xinzhi Future
 * Technology Co., Ltd. 与 Project Contributors），见 licenses/METALIO-MIT.txt。
 * Portions ported from the Metalio E-Ink4-Plus demo firmware (MIT); see
 * licenses/METALIO-MIT.txt.
 */

#include <stdint.h>

#include <esp_attr.h>

// 只在没有 Xtensa 汇编可用时才编出来。
// Only compiled when the Xtensa assembly is not available.
#if !defined(__XTENSA__)

/// 交织一行 4bpp 两帧为差分行，并更新列脏标记；返回非 0 表示本行有差异。
/// Interlaces one 4bpp line of two frames into a difference line, tracking column dirtiness.
uint32_t epd_interlace_4bpp_line_VE(
    const uint8_t* to, const uint8_t* from, uint8_t* interlaced, uint8_t* col_dirtyness,
    int fb_width
) {
    uint32_t dirty = 0;
    // 两像素一字节，成对处理，省掉逐像素的 x/2 与 x%2。
    // Two pixels per byte, handled in pairs so the per-pixel x/2 and x%2 disappear.
    const int pairs = fb_width >> 1;
    for (int p = 0; p < pairs; p++) {
        const uint8_t tb = to[p];
        const uint8_t fb = from[p];
        const uint8_t t0 = (uint8_t)(tb & 0x0f);
        const uint8_t t1 = (uint8_t)(tb >> 4);
        const uint8_t f0 = (uint8_t)(fb & 0x0f);
        const uint8_t f1 = (uint8_t)(fb >> 4);
        const uint8_t d0 = (uint8_t)(t0 ^ f0);
        const uint8_t d1 = (uint8_t)(t1 ^ f1);
        col_dirtyness[p] |= (uint8_t)(d0 | (d1 << 4));
        dirty |= (uint32_t)(d0 | d1);
        interlaced[p * 2] = (uint8_t)((t0 << 4) | f0);
        interlaced[p * 2 + 1] = (uint8_t)((t1 << 4) | f1);
    }
    if (fb_width & 1) {
        const int x = fb_width - 1;
        uint8_t t = to[x / 2];
        uint8_t f = from[x / 2];
        t = (uint8_t)((x % 2) ? (t >> 4) : (t & 0x0f));
        f = (uint8_t)((x % 2) ? (f >> 4) : (f & 0x0f));
        col_dirtyness[x / 2] |= (uint8_t)((t ^ f) << (4 * (x % 2)));
        dirty |= (uint32_t)(t ^ f);
        interlaced[x] = (uint8_t)((t << 4) | f);
    }
    return dirty;
}

/// 按字节把 mask 与到 line 上（汇编是 EE.ANDQ，每轮 16 字节）。
/// Byte-wise AND of mask into line, matching the assembly's 16-byte EE.ANDQ loop.
__attribute__((optimize("O3"))) void IRAM_ATTR
epd_apply_line_mask_VE(uint8_t* line, const uint8_t* mask, int mask_len) {
    if (line == NULL || mask == NULL || mask_len <= 0) {
        return;
    }
    // 行队列与 mask 都是 16 字节对齐的，所以按字处理安全。
    // The line queue and the mask are both 16-byte aligned, so word access is safe.
    uint32_t* l32 = (uint32_t*)line;
    const uint32_t* m32 = (const uint32_t*)mask;
    const int words = mask_len >> 2;
    int i = 0;
    for (; i + 4 <= words; i += 4) {
        l32[i] &= m32[i];
        l32[i + 1] &= m32[i + 1];
        l32[i + 2] &= m32[i + 2];
        l32[i + 3] &= m32[i + 3];
    }
    for (; i < words; i++) {
        l32[i] &= m32[i];
    }
    for (i = words << 2; i < mask_len; i++) {
        line[i] &= mask[i];
    }
}

/// 1ppB / 1K VE 格式的 LUT 查表，补齐 lut.S 的对齐分支。
/// 1ppB / 1K VE-format LUT lookup, filling in lut.S's aligned branch.
__attribute__((optimize("O3"))) void IRAM_ATTR calc_epd_input_1ppB_1k_S3_VE_aligned(
    const uint32_t* ld, uint8_t* epd_input, const uint8_t* conversion_lut, uint32_t epd_width
) {
    if (ld == NULL || epd_input == NULL || conversion_lut == NULL || epd_width < 4) {
        return;
    }
    const uint8_t* lut = conversion_lut;
    const uint32_t n = epd_width >> 2;  // 输出字节数 = 像素数 / 4
    uint32_t j = 0;

    // 字节内 4 个像素的高低次序随目标的 LCD 外设而不同：ESP32-S31 沿 X 先吃低位那 2bit，
    // ESP32-S3 先吃高位。这是帧缓冲的打包约定，不是 CPU 端序，写反了整屏就是花的。
    // The order of the four pixels inside a byte follows the target's LCD peripheral: ESP32-S31
    // consumes the low 2 bits first along X, ESP32-S3 the high ones. That is the framebuffer
    // packing convention, not CPU endianness, and getting it backwards garbles the whole screen.
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#define EPD_LUT4(in)                                                                               \
    ((uint8_t)(lut[((in) & 0xFFu) << 2] << 6 | lut[(((in) >> 8) & 0xFFu) << 2] << 4 |              \
               lut[(((in) >> 16) & 0xFFu) << 2] << 2 | lut[((in) >> 24) << 2]))
#else
#define EPD_LUT4(in)                                                                               \
    ((uint8_t)(lut[((in) >> 24) << 2] << 6 | lut[(((in) >> 16) & 0xFFu) << 2] << 4 |               \
               lut[(((in) >> 8) & 0xFFu) << 2] << 2 | lut[((in) & 0xFFu) << 2]))
#endif

    for (; j + 4 <= n; j += 4) {
        const uint32_t in0 = ld[j];
        const uint32_t in1 = ld[j + 1];
        const uint32_t in2 = ld[j + 2];
        const uint32_t in3 = ld[j + 3];
        epd_input[j] = EPD_LUT4(in0);
        epd_input[j + 1] = EPD_LUT4(in1);
        epd_input[j + 2] = EPD_LUT4(in2);
        epd_input[j + 3] = EPD_LUT4(in3);
    }
    for (; j < n; j++) {
        epd_input[j] = EPD_LUT4(ld[j]);
    }
#undef EPD_LUT4
}

#endif  // !defined(__XTENSA__)

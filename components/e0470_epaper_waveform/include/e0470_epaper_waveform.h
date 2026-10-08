/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * E0470A01（684×1216，40pin）的 epdiy 波形。
 * 自行调整屏幕波形会使设备失去保修。
 *   E0470_WAVEFORM         默认：裁剪 GC16 36 相 / GL16 37 相 + 50/50 阈值 DU
 *   E0470_FULL_WAVEFORM    GC16 / GL16 各 48 相，一个温度档 0-50°C
 *   E0470_GRAY8_WAVEFORM   8 灰阶：GC16 / GL16 各 30 相，灰阶少一半换来整屏约 360ms
 *   E0470_FOLLOW_WAVEFORM  跟随 DU：8 帧（黑 7 / 白 8），用于触摸笔迹
 * 默认与 8 灰阶波形挂阈值 DU（目标 0-7→黑、8-15→白）；波形保留只驱动 0/15 的源表。
 * GL16 给 15→15 补 1 帧白推；刷新走全像素，否则差分跳过白底。
 * 开机先调一次 e0470_waveform_init()。
 *
 * epdiy waveforms for the E0470A01 (684×1216, 40-pin).
 * Changing panel waveforms voids the warranty.
 *   E0470_WAVEFORM         Default: trimmed GC16 36 / GL16 37 + 50/50 threshold DU
 *   E0470_FULL_WAVEFORM    GC16 / GL16 48 phases each, one 0–50°C temp range
 *   E0470_GRAY8_WAVEFORM   8-gray: GC16 / GL16 30 phases; half the steps, ~360 ms full
 *   E0470_FOLLOW_WAVEFORM  Follow DU: 8 frames (black 7 / white 8) for touch ink
 * Default and 8-gray hang a threshold DU (dest 0–7→black, 8–15→white); the
 * source tables still drive only 0/15. GL16 adds one white push on 15→15;
 * refresh is full-pixel or the diff skips already-white.
 * Call e0470_waveform_init() once at boot.
 */

#pragma once

#include <stdint.h>

// Metalio 板用厂商示例固件的 epdiy 与波形表，本仓库这套波形类型在那块板上不参与编译。
// 上层引的是这个头，所以在这里把"跟随／灰阶／全刷"三档都映射到厂商那一份 MY_WAVEFORM：
// 画面正常，只是少了跟手专用的短波形（本仓库 fork 的优化）。
// The Metalio board uses the vendor demo firmware's epdiy and waveform tables, and this repo's
// waveform types are not compiled there. Callers include this header, so the follow, gray and
// full selections all map to the vendor's single MY_WAVEFORM: the picture is correct, only the
// dedicated short follow waveform (a local optimisation of this fork) is missing.
#if defined(PICO_BOARD_METALIO_EINK4_PLUS)

#include "my_waveform.h"

#define E0470_WAVEFORM MY_WAVEFORM
#define E0470_FULL_WAVEFORM MY_WAVEFORM
#define E0470_FOLLOW_WAVEFORM MY_WAVEFORM

// 行几何计算要的帧数与帧周期。厂商默认表是 GC16/GL16 完整 48 相；帧周期沿用本仓库
// 在 Read Pico 上标定的值，两板同面板同 pclk 区间。
// Frame counts and the frame period the line geometry needs. The vendor's default table is a
// full 48-phase GC16/GL16; the period reuses what this repo characterised on Read Pico, since
// both boards drive the same panel over the same pixel-clock range.
#define E0470_GC16_FRAMES 48
#define E0470_GL16_FRAMES 48
#define E0470_WAVEFORM_FRAME_US 11090

/// 本板上波形表由厂商的 my_waveform_init() 装配，这里给个空壳避免上层漏改。
/// / On this board the vendor's my_waveform_init() builds the tables; this stub keeps callers
/// unchanged.
static inline void e0470_waveform_init(void) {}

#else

#include "epd_waveform.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 完整灰阶表：GC16 / GL16 各 48 相，白推动梯子 17 档、去重后 11 级可分辨灰阶。
/// DU 是 20 相，只驱动目标 0/15；E0470_FULL_WAVEFORM 原样保留对照。
/// / Full gray tables: GC16 / GL16 48 phases, 17-step white-push ladder,
/// 11 distinguishable after collapse. DU is 20 phases, dest 0/15 only;
/// E0470_FULL_WAVEFORM is kept as a reference.
#define E0470_FULL_DU_FRAMES 20
#define E0470_FULL_GC16_FRAMES 48
#define E0470_FULL_GL16_FRAMES 48
extern const EpdWaveform E0470_FULL_WAVEFORM;

/// 另一份表：只有 8 级可分辨灰阶（净推动梯子 8 档），30 相，整屏约 360ms（默认表约 430ms）。
/// 灰阶档数 = 白推动梯子的档数，在这块屏上 16 级与 30 相不可兼得，所以它是备选不是默认。
/// / Alternate table: 8 distinguishable grays (8-step net push), 30 phases,
/// ~360 ms full (default ~430 ms). Gray count is the white-push ladder; 16
/// levels and 30 phases cannot both fit this panel, so this is optional.
#define E0470_GRAY8_GC16_FRAMES 30
#define E0470_GRAY8_GL16_FRAMES 30
extern const EpdWaveform E0470_GRAY8_WAVEFORM;

/// 裁剪版保留新屏所需的 3 个尾部保持相。完整48相实测未明显改善边缘残影，因此恢复
/// 已验证的主动段裁剪，避免无收益地增加约150ms；无损前导保持跳过仍由渲染器执行。
/// / Trim keeps the 3 trailing hold phases the new panel needs. The full 48
/// did not clearly help edge ghosting, so the proven active-segment trim is
/// restored instead of adding ~150 ms for no gain. Lossless leading-hold
/// skip is still done by the renderer.
#define E0470_TRIM_ERASE_MAX 11
#define E0470_TRIM_SAT_CUT 5
#define E0470_TRIM_WHITE_SAT_CUT 0
#define E0470_TRIM_HOLD 3
#define E0470_GC16_FRAMES 36
#define E0470_GL16_FRAMES 37

/// 帧周期取算法文档第 8 节的设计点 ν≈90Hz。
/// / Frame period is the §8 design point ν≈90 Hz.
#define E0470_WAVEFORM_FRAME_US 11090
extern const EpdWaveform E0470_WAVEFORM;

/// CrossMux/FreeInk 普通文字翻页 GL16；仅配合真实 4bpp 历史帧差分使用，定期 GC16 保留。
/// / CrossMux/FreeInk text-turn GL16; use with actual 4bpp history differentials and retain
/// periodic GC16.
extern const EpdWaveform E0470_TEXTTURN_WAVEFORM;

/// 触摸笔迹跟手用的 8 帧短 DU，走 FAST 扫描（帧周期约 7ms）。
/// 推动次数按 |to-from| 比例分配，满幅迁移往黑推 7 相、往白推 8 相。
/// / 8-frame short DU for touch ink, FAST scan (~7 ms/frame). Push count
/// scales with |to-from|; full travel is 7 black / 8 white.
#define E0470_FOLLOW_BLACK_FRAMES 7
#define E0470_FOLLOW_WHITE_FRAMES 8
#define E0470_FOLLOW_FRAMES 8
extern const EpdWaveform E0470_FOLLOW_WAVEFORM;

/// 单相位执行表供水波纹的错相 GL16 扫描使用；像素动作来自逐带相位 LUT。
/// One-phase apply waveform for staggered GL16; per-band phase LUTs supply pixel actions.
extern const EpdWaveform E0470_APPLY_WAVEFORM;

/// 按上面的公式生成 frames 帧的跟随表写进 dst（容量 frames×16×4 字节）。
/// / Build a follow table of `frames` into dst (frames×16×4 bytes).
void e0470_follow_lut_build(int frames, uint8_t (*dst)[16][4]);

/// 在一条 EpdWaveform 里按模式（MODE_GC16 / MODE_GL16 / MODE_DU）找唯一温度档的相位表。
/// / Look up the single temp-range phase table for MODE_GC16 / MODE_GL16 / MODE_DU.
const EpdWaveformPhases* e0470_waveform_phases(const EpdWaveform* waveform, int mode);

/// 查 (from→to) 在第 phase 相的 2bit 动作：0 保持、1 压黑、2 擦白。
/// / 2-bit action for (from→to) at `phase`: 0 hold, 1 darken, 2 erase.
int e0470_phase_action(const EpdWaveformPhases* phases, int phase, int to, int from);

/// 生成 E0470_WAVEFORM 与 E0470_FOLLOW_WAVEFORM 的表。必须在第一次刷新之前调用一次。
/// / Build E0470_WAVEFORM and E0470_FOLLOW_WAVEFORM. Call once before the first refresh.
void e0470_waveform_init(void);

#ifdef __cplusplus
}
#endif

#endif  // PICO_BOARD_METALIO_EINK4_PLUS

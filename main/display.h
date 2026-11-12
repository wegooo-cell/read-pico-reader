/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 墨水屏刷新、HV 轨空闲超时、pclk 回退。
 *
 * EPD present, HV-rail idle timeout, and pclk fallback.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "epd_highlevel.h"
#include "epdiy.h"
#include "e0470_page_turn.h"
#include "read_pico_epd_timing.h"


#ifdef __cplusplus
extern "C" {
#endif

// LCD 像素时钟：只决定有效像素段占一行的多少，剩下的由行结束段补足——行周期被锁在
// 波形标定的帧周期上，所以调 pclk 不会让刷新变快，只影响 DMA 的供数余量。12MHz 是
// epdiy 的保守默认值，18MHz 实测稳定（前提是 PSRAM 跑在 120MHz，80MHz 下 16MHz
// 就喂不满 DMA），开机锁 18MHz 留足余量。
// LCD pixel clock: it only sets how much of the line the active pixels occupy;
// the line-end pad fills the rest. The line period is locked to the waveform
// frame time, so pclk does not make refresh faster — it only changes DMA slack.
// 12 MHz is epdiy's conservative default; 18 MHz is stable here when PSRAM runs
// at 120 MHz (at 80 MHz even 16 MHz starves DMA). Boot locks 18 MHz for margin.
#define DISPLAY_PCLK_DEFAULT_MHZ 18
// 出现供数不足（EPD_DRAW_EMPTY_LINE_QUEUE）时退回这个确定安全的频率。
// Fall back to this known-safe clock on underrun (EPD_DRAW_EMPTY_LINE_QUEUE).
#define DISPLAY_PCLK_SAFE_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
// 工程页上还能继续往上试。行消隐与 CKV 宽度会跟着频率重解。
// The lab page can still step higher. Line blanking and CKV width re-solve with the clock.
#define DISPLAY_PCLK_MIN_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
#define DISPLAY_PCLK_MAX_MHZ READ_PICO_EPD_PCLK_MAX_MHZ
#define DISPLAY_PCLK_STEP_MHZ 1

/// 保留高压轨并释放像素驱动，包括无变化刷新；不增加扫描。/ Retain HV rails and release pixel drive, including no-change updates, without another scan.
void rails_keepalive(void);
void rails_idle_check(int64_t now_ms);

/// 大量文件I/O期间增加扫描预填，调用方离开时恢复。/ Increase scan prefill during bulk file I/O; caller restores on exit.
void display_set_bulk_io(bool active);

/// 请求下一张完成绘制的画面整屏清理一次，用于解锁。/ Clean the next completed frame once after unlock.
void display_request_full_refresh(void);
enum EpdDrawError update_display_mode(EpdiyHighlevelState* hl, enum EpdDrawMode mode);
/// 普通页面切换使用真实差分 GL16；未变化像素不进入 dirty map。
/// True differential GL16 for ordinary page transitions; unchanged pixels stay clean.
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState* hl, enum EpdDrawMode mode);
/// 系统子页只推最终差分，不因操作计数全刷；显式全刷和故障恢复独立。
/// System children present final differences without count-triggered full output; explicit full and fault recovery are independent.
/// 四主页面固定底栏，仅横条局部更新；普通保留灰阶与内容区整理，快刷用完整黑白表及棋盘格亚克力。
/// All main screens retain navigation with local markers; ordinary uses soft gray differences and periodic body cleanup, fast uses monochrome/checkerboard acrylic.
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState* hl);
/// 为下一次主页面输出启用固定底栏；标记书架切出与真正切页，避免同页重绘播放动画。
/// Arm fixed navigation for the next main-screen output; tag shelf exit and actual navigation so same-page repaints do not animate.
void display_main_transition_arm(const void* owner, bool leaving_shelf, bool changing_page);
#if KIIKO_BOARD_METALIO47
/// 仅真实快刷跨主页的背景擦白许可；调用后须恢复原值。/ Scoped fast root background permission; restore after presentation.
bool display_main_fast_background_set(bool active);
/// Scope extra white erasure to an actual switch between visible main tabs; returns the previous scope.
bool display_main_switch_erase_set(bool active);
/// 仅书架切回首页的下半内容区增强擦白，返回旧作用域。/ Scope stronger lower-body white drive to shelf-to-home only; returns prior scope.
bool display_main_shelf_home_erase_set(bool active);
/// 纯绘制期间提前等待TPS唤醒，结束时取消未消费的准备；实际推屏另持Flash锁。/ Overlap TPS wake with pure paint and cancel unused preparation; actual output separately holds the Flash lock.
bool display_page_prepare_begin(void);
void display_page_prepare_end(bool held);
bool display_pipeline_enabled(void);
/// 当前设置/文件主页面按黑白输出，底栏仍从真实back保留。/ Present the armed Settings/Files root in BW, retaining navigation from actual back.
void display_main_transition_monochrome(void);
/// 只规整S31内容区像素，不改底栏、波形或显示历史。/ Normalize only S31 content pixels, without changing navigation, waveforms or display history.
void display_monochrome_content(uint8_t* fb);
/// 整个系统页二值化，不改真实back或原厂波形。/ Binarize the whole system frame without altering back history or vendor tables.
void display_monochrome_frame(uint8_t* fb);
/// 设置本次系统推屏策略，返回旧策略；离开作用域必须恢复。/ Set scoped system-output policy, returning the previous policy for restoration.
bool display_system_binary_set(bool enabled);
/// 常驻底栏的根页/子页保留灰阶，只二值化内容，返回旧值供恢复。/ Persistent root/child navigation retains gray; binarize content only and return the prior scope.
bool display_system_navigation_set(bool visible);
#endif
/// 系统侧滑用主页灰阶水波纹，独立于阅读文字序列；区域必须是逻辑坐标。
/// System edge returns use main-gray ripple independently of reader text; the area uses logical coordinates.
enum EpdDrawError update_display_main_turn(EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir);
/// 当前主页面最多三个横向灰阶条带，其余内容按黑白驱动；无入口许可时忽略。
/// Register at most three horizontal gray bands on the armed main page; other content uses black/white. Ignore without permission.
void display_main_transition_gray_bands(const EpdRect* bands, unsigned count);
/// 书架分页加强白色擦除，保持底栏边界。/ Strengthen shelf-page whitening within the fixed navigation boundary.
void display_main_transition_shelf_page(void);
/// 解除尚未使用的入口许可。/ Disarm unused entry permission.
void display_main_transition_disarm(void);
/// 取消尚未使用的入口；不打断物理波形，无延迟补刷任务。
/// Cancel unused permission without interrupting a physical waveform; no deferred completion exists.
void display_main_transition_cancel(void);
#if KIIKO_BOARD_METALIO47
/// 注册书架封面及书夹区，供独立37相实验使用；普通灰区不启用。/ Register shelf art for the isolated37-phase experiment; generic gray bands are unaffected.
void display_main_transition_cover_bands(const EpdRect* bands, unsigned count);
#endif
enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
);
/// 把前缓冲铺白再 GC16 全刷，物理屏回到白底。
/// Paint the front buffer white and GC16 the panel back to white.
enum EpdDrawError update_display_white(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_full(EpdiyHighlevelState* hl);

/// 面板修复：不看 framebuffer 的整屏盲刷，专治刷新中途断电留下的条纹。
/// Panel repair: a blind full-screen drive that ignores the framebuffer, for the stripes a power
/// cut in the middle of a refresh leaves behind.
/// 返回最终整屏输出结果；失败时调用方不能报告成功。/ Return the final full-frame result; callers must not report success on failure.
enum EpdDrawError display_panel_repair(EpdiyHighlevelState* hl);
/// 图片页清除旧光学状态，再驱动完整灰阶。/ Clear the prior optical state and drive the full gray ladder for images.
enum EpdDrawError update_display_image_gray(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
);
/// 灰阶图还在屏上时置位：菜单盖上来或离页先刷白，避免从中间灰差分。
/// Set while a gray image is still on panel: wipe to white before the menu or leave so the next update is not a mid-gray differential.
void display_hold_white_exit(bool hold);
bool display_take_white_exit(void);
enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);
/// 动画定稿只驱动真实差异，不强制把区域内所有 GL16 像素重刷。
/// Animation settle drives real differences only instead of forcing every GL16 pixel in the area.
/// 在指定区域驱动全部像素，用于局部定稿和清残影。/ Drive all pixels within an area for local settling and ghost cleanup.
/// 控件局部反馈暂存后续主页刷新入口，不提前消耗其裁剪、灰阶和底栏保护。/ Local control feedback preserves the pending main crop, gray policy and footer protection.
enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area);
enum EpdDrawError update_display_area_full_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);
enum EpdDrawError update_display_area_diff_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);
#if KIIKO_BOARD_METALIO47
/// S31列表按身份/范围累计真实滚动；每八次用原厂GC48替换当次输出，不额外补刷。
/// S31 lists accrue changed scrolls by identity/crop; every eighth output uses raw GC48 without an extra pass.
enum EpdDrawError update_display_settings_list(EpdiyHighlevelState* hl,unsigned list_id,EpdRect area);
#endif
/// area始终为旋转后逻辑坐标，勿传物理epd_full_screen。/ area always uses rotated logical coordinates, never physical epd_full_screen.
/// 阅读页可选的真实错相 GL16 翻页；缺相位时回退现有 GL16，失败时恢复基准帧。
/// 纯文字水波纹复用柔和文字灰边，8带节拍不变；缺相回退GL16，扫描失败重建真实基准。
/// Staggered text phases retain eight-band pacing, hold unchanged grays and soften edge transients; missing phases fall back and failures rebuild the baseline.
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir);
/// 同步普通文字翻页沿用水波纹的全部灰阶相位，单带同时输出并恢复原节拍。/ Ordinary text uses all ripple gray phases in one synchronous band and restores pacing.
enum EpdDrawError update_display_text_turn(EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir);
#if KIIKO_BOARD_METALIO47
/// S31图文：同色保持，实际灰阶迁移用完整灰阶，黑白目标用原厂DU20，不量化图片。
/// S31 images: hold equal pixels; retain complete gray transitions or native DU20 for actual BW changes, without quantization.
enum EpdDrawError update_display_gray_transition(EpdiyHighlevelState* hl, EpdRect area);
#endif

#if KIIKO_BOARD_METALIO47
/// 已验证未变正文上的面板：保留原厂GL48及完整目标灰阶，一次区域定稿；不借文字22相或速读黑白。
/// Panels over verified unchanged text retain vendorGL48/full target grays in one regional settle, independent of text22-phase/FastBW.
enum EpdDrawError update_display_reader_panel(EpdiyHighlevelState* hl,EpdRect area);
/// 同一底页的面板反馈显式使用本地GL37实验；普通构建/OOM/故障仍回到完整48相。
/// Same-body panel feedback opts into local GL37; normal builds,OOM or faults retain full48 phases.
enum EpdDrawError update_display_reader_panel_fast(EpdiyHighlevelState* hl,EpdRect area);
/// 两个设置面板四灰实验：进入时加强底页擦除，反馈仍16相；故障后37/48，手动/故障清理48相。
/// Four-gray settings strengthen underlay erasure on entry,retain16-phase feedback,37/48 fallback and48-phase recovery.
enum EpdDrawError update_display_reader_panel_gray4(EpdiyHighlevelState* hl,EpdRect area,bool erase_underlay);
#endif
/// 动画中间帧使用灰阶波形，但不累计周期清屏计数；动画最终帧走普通刷新出口。
/// Use grayscale for animation strips without advancing periodic cleanup; the final frame uses the normal path.

/// 当前像素时钟。/ Current pixel clock.
int display_pclk_mhz(void);
/// 出现供数不足就退回安全频率并整屏重刷，其它错误码原样忽略。
/// On underrun, drop to the safe clock and full-refresh; other error bits are ignored.
void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result);

/// 根页切换补白许可：0关闭、6普通、8涉及书架，仅在一次真实根页推屏中生效并恢复。
/// Scope board root whitening:0 off,6 ordinary,8 involving the shelf; restore the prior value after presentation.
unsigned display_main_quality_set(unsigned white_phases);

#ifdef __cplusplus
}
#endif

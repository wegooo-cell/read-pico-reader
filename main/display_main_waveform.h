/* SPDX-License-Identifier: Apache-2.0
 * 中文：主页面灰阶保持未变像素；书架退出加强同向擦白，主页水波纹端点单向推动，中灰保持原序列。
 * English: Main-page grays hold unchanged pixels; shelf exit strengthens directional whitening and main water uses directional endpoints while retaining original midgray sequences.
 */
#pragma once
#include "epdiy.h"
const EpdWaveform* display_main_gray_waveform(void);
/// 离开书架单向变白并补四相，避免GC16反向压黑；其他迁移用原DU，等值保持。/ Shelf exit whitens directionally with four extra phases, avoiding GC16 reverse darkening; other transitions use original DU and equal values hold.
const EpdWaveform* display_main_shelf_exit_waveform(void);
/// 水波纹与普通切页共用：端点单向驱动，中灰变色保留完整序列，等灰保持；调用者决定是否错相。
/// Shared by water and ordinary transitions: directional endpoints, complete changed midgray sequences and held equal grays; callers choose staggering.
const EpdWaveform* display_main_water_waveform(void);
/// 快刷首帧保留原DU并保持所有等值像素，亚克力首帧不驱动。/ Fast first pass retains original DU while holding every equal pair, including skipped acrylic.
const EpdWaveform* display_main_bw_waveform(void);
#if !KIIKO_BOARD_METALIO47
/// Pico列表滑动专用12相：保留FOLLOW8黑推，变白补四相，所有等值保持；仅列表变化像素输出。
/// Pico list-only12 phases retain FOLLOW8 black drives, add four changed-white drives and hold all equals; changed list pixels only.
extern const EpdWaveform PICO_SETTINGS_MOTION_WAVEFORM;
const EpdWaveform* display_settings_motion_waveform(void);
#endif

/// 普通模式灰边交错，端点/相数/推动数量不变；OOM回退原柔和灰阶。
/// Ordinary gray-edge interleaving keeps endpoints/phase counts/pulse counts; OOM falls back to original soft grays.
const EpdWaveform* display_main_normal_waveform(void);

/// 板级根页清残影：保留原动作并补变白及释放，分配失败回退原表；各板使用各自相位预算。
/// Board root cleanup retains original actions and adds white/release phases within each board's own budget, with safe allocation fallback.
const EpdWaveform* display_main_clean_waveform(const EpdWaveform* source, enum EpdDrawMode mode);

/// 板级根页按来源/目标选择六或八相变白；缓存区分力度且不叠加，分配失败保留原表。
/// Board roots select six/eight changed-white phases; strength-keyed bounded caches never stack; OOM retains the source.
const EpdWaveform* display_main_clean_waveform_with_phases(
    const EpdWaveform* source, enum EpdDrawMode mode, unsigned white_phases
);

#if KIIKO_BOARD_METALIO47
/// 用户批准的快刷跨根页背景维护：独立于实验开关，六/八相全目标白及两相释放。
/// Approved board-native fast root maintenance: six/eight white-target drives and two holds, independent of experiment flags.
const EpdWaveform* display_main_fast_background_waveform_with_phases(
    const EpdWaveform* source,enum EpdDrawMode mode,unsigned white_phases);
/// S31快刷/水波纹根页同轮维护所有目标白；六/八相预算不变，等黑/等灰不新增推动。
/// S31 fast/ripple roots maintain all white targets in the same six/eight-phase budget without new equal-black/gray drives.
const EpdWaveform* display_main_background_waveform_with_phases(
    const EpdWaveform* source,enum EpdDrawMode mode,unsigned white_phases);

#include <stdbool.h>
/// 仅主页切换及阅读图文出口显式调用：变化为白的像素增加两拍，保留原序列与释放尾段。
/// Explicit root-switch/reader-image outlets add two white drives on changed white targets, retaining original actions and release phases.
/// Mode, gray pulses and equal-pixel maintenance are unchanged; OOM/unsupported sources return the original waveform.
const EpdWaveform* display_main_switch_waveform(const EpdWaveform* source, enum EpdDrawMode mode);
/// 本地书架实验：封面/外围均官方37相16灰；关闭或OOM返回NULL，调用者保留48相。
/// Local shelf experiment: art/chrome share vendor37-phase16-gray LUTs; NULL on disabled/OOM requires48-phase fallback.
const EpdWaveform* display_main_shelf37_waveform(bool art);
/// 本地显式实验的16灰GL37面板表；未启用、源不兼容或分配失败返回NULL，调用方回退48相。
/// Local opt-in16-gray GL37 panel profile; NULL on disabled/incompatible/OOM builds requires a48-phase fallback.
const EpdWaveform* display_reader_panel37_waveform(void);
/// 用原DU20相位供已有条带动画读取；仅适配mode元数据。/ Expose original DU20 phases to the existing band engine; adapt mode metadata only.
const EpdWaveform* display_main_bw_turn_waveform(void);
/// S31十四相黑白维护：等白末三相补白，其他等值保持，迁移及总相数不变；未启用返回NULL。
/// S31 DU14 binary maintenance: maintain equal whites in the last three phases, hold other equals, retain changed transitions/count; NULL when disabled.
const EpdWaveform* display_du14_white_waveform(void);
/// 底栏灰底轻量恢复：原DU20端点、白底FOLLOW灰推，非白先擦白，等灰保持；不改原厂表。
/// Quiet navigation restore: vendor DU20 endpoints, FOLLOW grays from white, whiten nonwhite origins first, hold equals; vendor tables remain intact.
const EpdWaveform* display_main_navigation_waveform(void);
/// 书架普通切页柔和题头/分页；使用本机DU20端点，中灰GL48保持等值，无额外相位。
/// Soft shelf chrome uses this panel's DU20 endpoints and equal-held GL48 midgrays, without extra phases.
const EpdWaveform* display_main_shelf_soft_waveform(void);
/// 封面/材质保留完整本机GL/GC48，白色留白沿用柔和擦白；手动全刷不使用此派生表。
/// Art retains complete local GL/GC48; white space uses soft erase. Manual full refresh keeps the original tables.
#else
/// 书架封面/材质保留校准灰阶，白色留白只向白推动；只供普通切页，手动全刷保留原表。
/// Shelf art keeps calibrated grays while white space drives only white; ordinary transitions only, preserving vendor manual full refresh.
#endif
const EpdWaveform* display_main_shelf_waveform(enum EpdDrawMode mode);
#if KIIKO_BOARD_METALIO47

/// 整页输出保留15→15补白；固定底栏及局部控件继续使用原等值保持表。
/// Page profiles retain white-to-white maintenance; frozen navigation and local controls use equal-held profiles.
const EpdWaveform* display_main_page_gray_waveform(void);
const EpdWaveform* display_main_page_bw_waveform(void);
#else
#endif

/// 设置滚动用各板原生灰阶加强擦白：S31为48+8+2，Pico为37+8；保留字缘和头像灰阶。
/// Board-owned list cleanup retains glyph/avatar grays: S31 uses48+8+2, Pico uses37+8.
const EpdWaveform* display_settings_scroll_waveform(void);
#if KIIKO_BOARD_METALIO47
/// S31列表下半区十相背景擦白及两相释放。/ S31 lower list: ten white-background drives and two release phases.
const EpdWaveform* display_settings_scroll_lower_waveform(void);
#endif
#if !KIIKO_BOARD_METALIO47
/// 系统底色恢复原完整柔和灰阶，白底同向擦除；底栏由调用方裁剪保护，不改原始波形。
/// Restore complete original soft system grays with directional white erasure; callers crop navigation and vendor tables remain intact.
const EpdWaveform* display_main_ui_waveform(void);
#endif

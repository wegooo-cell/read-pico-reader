/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：小纸 Pico 的编译身份与硬件能力。板型在构建前选定，禁止运行时猜硬件或接受另一板型的升级包。
 * English: Read Pico build identity and hardware capabilities. The board is fixed before compilation;
 * never guess hardware at runtime or accept another board's update.
 *
 * 冻结：本文件只放常量与能力宏，不放代码；跨板共用逻辑一律按这里的宏或 components/kiiko_board 的接口分叉。
 * Frozen: constants and capability macros only. Shared code branches on these macros or on the
 * components/kiiko_board interface.
 */
#pragma once

#ifndef KIIKO_BOARD_METALIO47
#define KIIKO_BOARD_METALIO47 0
#endif
#if KIIKO_BOARD_METALIO47 != 0 && KIIKO_BOARD_METALIO47 != 1
#error "KIIKO_BOARD_METALIO47 must be 0 or 1"
#endif

/* ---- 身份 / Identity ---- */
#define KIIKO_BOARD_ID "pico"
#define KIIKO_BOARD_PROJECT "Read_Pico"
/// ESP32-S3 的 esp_image 头 chip_id。/ esp_image header chip id for the ESP32-S3.
#define KIIKO_BOARD_IMAGE_CHIP_ID 0x0009
#define KIIKO_BOARD_SETTINGS_NAMESPACE "read_pico"
#define KIIKO_BOARD_LOCK_NAMESPACE "pico_lock"
#define KIIKO_BOARD_STARTUP_NAMESPACE "pico_startup"

/* ---- 面板 / Panel ---- */
/// 两块板同为 1216x684 单色面板，尺寸一致；驱动路径不同。
/// Both boards carry the same 1216x684 mono panel; only the driving path differs.
#define KIIKO_BOARD_PANEL_WIDTH 1216
#define KIIKO_BOARD_PANEL_HEIGHT 684
/// VCOM 存在 CW32 PMU 里，主机只读；Metalio 没有 PMU，标定值存本机 NVS。
/// VCOM lives in the CW32 PMU and is read-only for the host; Metalio has no PMU and keeps
/// its calibrated value in local NVS.
#define KIIKO_BOARD_VCOM_IN_NVS 0
#define KIIKO_BOARD_VCOM_DEFAULT_MV 0

/* ---- 硬件能力 / Hardware capabilities ---- */
/// 前光与振动：Pico 没有，Metalio 有。/ Front light and haptics: Metalio only.
#define KIIKO_BOARD_HAS_FRONTLIGHT 0
#define KIIKO_BOARD_HAS_HAPTIC 0
/// 侧边音量键：Metalio 的 FCA9555 P1.0/P1.1。/ Volume keys: Metalio's FCA9555 P1.0/P1.1.
#define KIIKO_BOARD_HAS_VOLUME_KEYS 0
/// TPS65185 电源轨与 VCOM 回读：Metalio。/ TPS65185 rails and VCOM readback: Metalio.
#define KIIKO_BOARD_HAS_TPS65185 0
/// 板上 USJ/UTMI 切换与下载交接：Metalio（GPIO36）。/ Board USJ/UTMI switch: Metalio (GPIO36).
#define KIIKO_BOARD_HAS_USB_ROUTE 0
/// 防淡墨（定时全刷）与 PSRAM 封面缓存：Metalio 上实现。/ Anti fade and PSRAM cover cache: Metalio.
#define KIIKO_BOARD_HAS_ANTI_FADE 0
#define KIIKO_BOARD_HAS_COVER_CACHE 0
/// 本板专属缓存目录，与 Metalio 的 `/sdcard/.metalio_s31` 分开，避免两块板的缓存互相污染。
/// Board-private cache directory, separate from Metalio's `/sdcard/.metalio_s31`.
#define KIIKO_BOARD_CACHE_DIR "/sdcard/.readpico"
/// CW32L010 PMU 协议主机。/ CW32L010 PMU protocol host.
#define KIIKO_BOARD_HAS_PMU 1
/// 锁屏待机方式（浅睡 / 深睡 / 关机）三档：Metalio 独有；Pico 的待机由 PMU 决定。
/// Lock-face standby policy (light/deep/off): Metalio only; the Pico PMU decides standby.
#define KIIKO_BOARD_HAS_LOCK_SLEEP_POLICY 0
/// 两板各自实现普通、水波纹、速读黑白。/ Both boards implement ordinary, ripple and Fast BW through native drivers.
#define KIIKO_BOARD_READER_TURN_EFFECT_MAX 2

/* ---- 备份与设备名 / Backup and device name ---- */
/// 设置备份文件名前缀与默认设备名；升级后要与本机旧固件的取值一致，否则备份认不出来。
/// Settings-backup filename prefix and default device name; must match what this board's
/// previous firmware wrote or existing backups stop being recognised.
#define KIIKO_BOARD_BACKUP_PREFIX "Pico-settings"
#define KIIKO_BOARD_PRODUCT_NAME "kiikoread"

/* ---- 显示策略 / Display policy ---- */
/// 面板修复用盲刷多轮（Pico 独有）；Metalio 用 epd_clear_checked 一次白底基线。
/// Panel repair by blind repeated pushes (Pico only); Metalio uses one epd_clear_checked baseline.
#define KIIKO_BOARD_HAS_PANEL_BLIND_REPAIR 1
/// 屏轨空闲多久自动断电：Pico 8s；Metalio 每次输出后立即断电（0 表示不做空闲延时）。
/// Rail idle timeout: 8 s here; Metalio powers down right after every output (0 disables it).
#define KIIKO_BOARD_RAILS_IDLE_TIMEOUT_MS 8000
/// 是否使用裁剪过的 GL48 波形表（Pico 的柔和调序）；Metalio 用厂家 2.0.76 完整波形。
/// Whether to trim the GL48 table (the Pico soft ordering); Metalio ships the vendor's full
/// 2.0.76 waveform.
#define KIIKO_BOARD_WAVEFORM_TRIM_GL48 1
/// 文字翻页是否做相序调序（Pico 的灰边收敛）；Metalio 交给派生表处理。
/// Whether reader text turns re-order phases (the Pico gray-edge tuning); Metalio defers to
/// the derived tables.
#define KIIKO_BOARD_READER_TEXT_PHASE_TUNE 1
/// 全像素快刷后是否需要一次白 tick（Metalio 用来修强光下发灰）。
/// Whether a full-pixel fast update needs one white tick (Metalio's fix for grey whites in
/// strong light).
#define KIIKO_BOARD_PANEL_WHITE_TICK 0

/* ---- 缓存与内存预算 / Cache and memory budgets ---- */
/// 封面缓存目录；两块板用各自的目录，互不污染。
/// Cover-cache directory; each board keeps its own so the two never share entries.
#define KIIKO_BOARD_COVER_CACHE_DIR KIIKO_BOARD_CACHE_DIR "/covers"
/// Pico 没有 PSRAM 封面缓存，封面仍走卡上文件缓存。
/// The Pico has no RAM cover cache; covers stay on the card cache.
#define KIIKO_BOARD_RAM_COVER_CACHE 0
/// 以下数值单位 KiB；pico 保持 rc99 的保守取值，S31 有 16MB PSRAM 才放大。
/// KiB units below; these are the rc99 conservative values, only the S31 (16 MB PSRAM) scales up.
#define KIIKO_BOARD_COVER_RAM_SLOTS 64
#define KIIKO_BOARD_COVER_RAM_BUDGET_KB 2048
#define KIIKO_BOARD_CACHE_PSRAM_RESERVE_KB 2048
#define KIIKO_BOARD_CACHE_BLOCK_RESERVE_KB 1024
#define KIIKO_BOARD_TTF_CACHE_LIMIT_KB 320
#define KIIKO_BOARD_TTF_CACHE_MID_KB 320
#define KIIKO_BOARD_TTF_CACHE_PRESSURE_KB 320
#define KIIKO_BOARD_PAGE_CACHE_SLOTS 2
#define KIIKO_BOARD_PAGE_CACHE_MAX_KB 512
/// 关闭 ZIP 预取：pico 的 PSRAM 余量留给字体与渲染。
/// ZIP prefetch stays off here; the Pico's PSRAM headroom belongs to fonts and rendering.
#define KIIKO_BOARD_ZIP_PREFETCH_KB 0

/* ---- 书库 NVS 命名空间 / Shelf NVS namespaces ---- */
/// 与各板已发布固件的取值保持一致，改名等于清空用户的书架、收藏与书签。
/// Keep these equal to each board's shipped firmware: renaming clears the user's shelf,
/// favorites and bookmarks.
#define KIIKO_BOARD_FAVORITES_NS "rp_favs"
#define KIIKO_BOARD_SHELF_NS "rp_shelf"
#define KIIKO_BOARD_MARKS_NS "rp_marks"

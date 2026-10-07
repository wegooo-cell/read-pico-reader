/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 里的用户设置：睡眠档、字体路径、上次唤醒/开机原因、拿起唤醒开关。
 *
 * User settings in NVS: sleep mode, font path, last wake/boot reason,
 * pickup-wake switch.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/// 默认深睡。浅睡：按键回原页；拿起唤醒默认关。软睡：SOFT_SLEEP 拉低 EN，再短按开机。关机：EN=0，长按开机。
/// Default is deep. Light: key returns to the page; pickup-wake defaults off. Soft sleep: SOFT_SLEEP drops EN, then a short press boots. Off: EN=0, long-press to boot.
typedef enum {
    APP_SLEEP_LIGHT = 0,
    APP_SLEEP_DEEP = 1,
    APP_SLEEP_OFF = 2,
} app_sleep_mode_t;

void app_settings_init(void);
app_sleep_mode_t app_settings_sleep_mode(void);
void app_settings_set_sleep_mode(app_sleep_mode_t mode);
const char* app_sleep_mode_name(app_sleep_mode_t mode);
/// 电源菜单“关机”：false 为彻底断电，true 为先浅睡 10 分钟再由 PMU 软睡断电。
/// Power-menu shutdown: false powers fully off; true light-sleeps for 10 minutes, then PMU soft-sleeps.
bool app_settings_staged_shutdown(void);
void app_settings_set_staged_shutdown(bool staged);
/// 首页进入时强制全刷；默认关闭。/ Full refresh when entering Home; off by default.
bool app_settings_home_full_refresh(void);
void app_settings_set_home_full_refresh(bool enabled);
/// 无操作自动锁屏分钟数：0 关闭，1/5/10。/ Idle lock timeout: off, 1/5/10 minutes.
uint8_t app_settings_auto_lock_minutes(void);
void app_settings_set_auto_lock_minutes(uint8_t minutes);
const char *app_settings_device_name(void);
void app_settings_set_device_name(const char *name);
const char *app_settings_avatar_path(void);
void app_settings_set_avatar_path(const char *path);
const char *app_settings_status_signature(void);
void app_settings_set_status_signature(const char *signature);
/// 空路径表示固件内建字体；非空为 SD 上的 TTF。/ Empty path is the built-in font; non-empty is a TTF on the SD card.
const char* app_settings_font_path(void);
void app_settings_set_font_path(const char* path);
/// 系统界面字体；空值使用内建思源黑体并从 TF 卡补字。/ UI font; empty uses built-in Source Han Sans with SD fallback.
const char* app_settings_system_font_path(void);
void app_settings_set_system_font_path(const char* path);
/// 系统字号 100..200%，每档 10%。/ System UI size 100..200%, in 10% steps.
uint8_t app_settings_system_font_size(void);
void app_settings_set_system_font_size(uint8_t percent);
/// 系统界面灰阶对比度，100..140%。/ System UI grayscale contrast, 100..140%.
uint8_t app_settings_system_contrast(void);
void app_settings_set_system_contrast(uint8_t percent);
/// 锁屏样式和壁纸路径。/ Lock style and wallpaper image path.
uint8_t app_settings_lock_style(void);
void app_settings_set_lock_style(uint8_t style);
const char* app_settings_wallpaper_path(void);
void app_settings_set_wallpaper_path(const char* path);
/// 上次浅睡唤醒源（app_wake_source_t），掉电也保留。/ Last light-sleep wake source (app_wake_source_t); kept across power loss.
uint8_t app_settings_last_wake(void);
void app_settings_set_last_wake(uint8_t src);
/// 最近一次非 0 的 PMU wake_reason。STATUS 报 0 时用这个回显。/ Last non-zero PMU wake_reason. Used when STATUS reports 0.
uint8_t app_settings_last_boot(void);
void app_settings_set_last_boot(uint8_t reason);
/// 浅睡拿起唤醒。默认关；有加速度计也不会自动开。/ Light-sleep pickup wake. Defaults off; an accelerometer does not turn it on.
bool app_settings_pickup_wake(void);
void app_settings_set_pickup_wake(bool on);
/// 阅读默认字号，36..72，默认 48。/ Default reading size, 36..72, initially 48.
uint8_t app_settings_book_px(void);
/// 无效字号恢复 48。/ Invalid sizes fall back to 48.
void app_settings_set_book_px(uint8_t px);
/// 实验性晃动翻页，默认关闭。/ Experimental shake page turn, off by default.
bool app_settings_book_shake(void);
/// 保存实验性晃动翻页开关。/ Persist the experimental shake page-turn switch.
void app_settings_set_book_shake(bool on);
/// 阅读时每 5/10/15/30 页全刷；0 关闭周期全刷，手动全刷仍可用。
/// Full-screen cleanup every 5/10/15/30 turns; 0 disables periodic cleanup only.
uint8_t app_settings_reader_full_pages(void);
void app_settings_set_reader_full_pages(uint8_t pages);
/// 阅读翻页效果：0 默认，1 水波纹；初始为默认。/ Reader turn effect: 0 default, 1 water ripple; initially default.
uint8_t app_settings_reader_turn_effect(void);
void app_settings_set_reader_turn_effect(uint8_t effect);
/// 阅读正文里短按电源键翻下一页；默认关闭。/ Short power press turns forward only in reader body; off by default.
bool app_settings_reader_power_turn(void);
void app_settings_set_reader_power_turn(bool on);
/// 沉浸全屏时隐藏阅读状态栏。/ Hide the reader status bar in immersive full screen.
bool app_settings_reader_immersive(void);
void app_settings_set_reader_immersive(bool on);
/// 阅读时跳过书内插图页；原图仍保留在 EPUB 中。/ Skip inline illustrations while reading without changing the EPUB.
bool app_settings_reader_hide_images(void);
void app_settings_set_reader_hide_images(bool on);
/// 阅读时长按中间圆圈键全刷；关闭时返回首页，默认关闭。
/// Refresh the entire screen on a reader middle-key hold; off returns Home, default off.
bool app_settings_reader_hold_refresh(void);
void app_settings_set_reader_hold_refresh(bool on);
/// 上中下轻点分区；默认左右翻页。/ Top/middle/bottom tap regions; defaults to horizontal.
bool app_settings_reader_vertical_turn(void);
void app_settings_set_reader_vertical_turn(bool on);
/// 书架排序偏好重启后保持。/ Persist the shelf's recent-reading sort across restarts.
bool app_settings_shelf_recent_sort(void);
void app_settings_set_shelf_recent_sort(bool on);
/// 蓝牙翻页器：启用 BLE HID 主机，好让蓝牙翻页器连上并翻页；默认关闭。
/// 关掉会把整个 BLE 栈拆掉，把 NimBLE 占的内存还回去。
/// Bluetooth page-turner: bring up the BLE HID host so a remote can pair and turn pages; off
/// by default. Turning it off tears the whole stack down and returns NimBLE's memory.
bool app_settings_ble_turner(void);
void app_settings_set_ble_turner(bool on);
/// 字间距档位 0..4，默认 2 居中；对应 -4/-2/0/+2/+4 像素。/ Tracking index 0..4, centered default 2; maps to -4/-2/0/+2/+4 pixels.
uint8_t app_settings_book_tracking(void);
void app_settings_set_book_tracking(uint8_t index);
/// 正文字体粗细档位：常规（400）、中等（500）、粗体（700），默认常规；带 wght 轴的
/// 字体走真实变体，静态字体按覆盖率形态学近似，未调整时外观与字体本身一致。
/// Body weight steps: regular (400), medium (500) and bold (700), default regular;
/// wght-axis fonts use real variations while static fonts approximate the face by
/// coverage morphology, so the untouched look matches the font itself.
#define APP_BOOK_WEIGHT_COUNT 3
#define APP_BOOK_WEIGHT_DEFAULT 400
#define APP_BOOK_WEIGHT_DEFAULT_INDEX 0
extern const uint16_t app_settings_book_weights[APP_BOOK_WEIGHT_COUNT];
/// 当前档位下标；越界返回默认档。/ Current step index; out-of-range returns the default step.
uint8_t app_settings_book_weight_index(void);
/// 当前正文字体粗细（400/500/700）。/ Current body weight (400, 500 or 700).
uint16_t app_settings_book_weight(void);
/// 只接受档位表内的字重，其他值忽略。/ Only a weight from the step table is accepted; other values are ignored.
void app_settings_set_book_weight(uint16_t wght);
/// 普通正文首行缩进 0..3 字，默认两字。/ First-line indent: 0..3 em, default 2.
uint8_t app_settings_book_indent(void);
void app_settings_set_book_indent(uint8_t em);
/// 正文阅读线：0 无，1 虚线，2 点线。/ Body reading guides: none, dashed or dotted.
uint8_t app_settings_book_reading_line(void);
void app_settings_set_book_reading_line(uint8_t style);
/// 阅读线相对默认位置 -8..+8 像素，2 像素一步，默认 0。/ Guide offset in 2 px steps.
int8_t app_settings_book_reading_line_offset(void);
void app_settings_set_book_reading_line_offset(int8_t offset_px);
/// 阅读行高百分比，110..150。/ Reader line-height percentage: 110..150.
uint8_t app_settings_book_line_spacing(void);
void app_settings_set_book_line_spacing(uint8_t percent);
/// 阅读页左右边距，24..60 像素。/ Reader horizontal margin: 24..60 pixels.
uint8_t app_settings_book_margin(void);
void app_settings_set_book_margin(uint8_t px);
/// 段后距离百分比，0/25/50/75。/ Paragraph-gap percentage: 0/25/50/75.
uint8_t app_settings_book_paragraph_spacing(void);
void app_settings_set_book_paragraph_spacing(uint8_t percent);
/// 书架样式：1 深色书轨、2 亚克力挡板、3 半透明书袋、4 封面与书脊。/ Shelf style: rail, acrylic, pocket, or covers and spines.
uint8_t app_settings_shelf_style(void);
void app_settings_set_shelf_style(uint8_t style);
/// 当前 TF 卡书籍目录和字体目录；默认分别为 /sdcard/books、/sdcard/fonts。
const char* app_settings_books_dir(void);
const char* app_settings_fonts_dir(void);
/// 仅允许已挂载 TF 卡内的有界绝对目录；调用者负责确认目录存在。
bool app_settings_set_books_dir(const char* path);
bool app_settings_set_fonts_dir(const char* path);

/// 将设置、阅读记录和 WiFi 凭据保存到 TF 卡根目录；备份含明文密码，需妥善保管。
/// Save settings, reading records and WiFi credentials to TF root; keep the plaintext-secret backup private.
esp_err_t app_settings_backup_save(void);
/// 校验备份后恢复 NVS；缺失的外部字体或壁纸回退到内建资源。
/// Validate before restoring NVS; missing external assets fall back safely.
esp_err_t app_settings_backup_restore(void);

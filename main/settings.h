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
#if KIIKO_BOARD_METALIO47
typedef enum {APP_AI_REFRESH,APP_AI_BOOKMARK,APP_AI_FRONTLIGHT,APP_AI_HOME,APP_AI_NONE,APP_AI_ACTION_COUNT} app_ai_action_t;
/// AI键短按独立保存在S31设置；仅成功提交才改变当前映射。/ Persist S31 AI taps, changing the live mapping only after a successful commit.
app_ai_action_t app_settings_ai_short(void);
esp_err_t app_settings_set_ai_short(app_ai_action_t action);
#else
#endif
void app_settings_set_sleep_mode(app_sleep_mode_t mode);
const char* app_sleep_mode_name(app_sleep_mode_t mode);
#if KIIKO_BOARD_METALIO47
typedef enum {APP_LOCK_SLEEP_TIMED, APP_LOCK_SLEEP_OFF, APP_LOCK_SLEEP_KEEP_LIGHT} app_lock_sleep_mode_t;
/// S31锁屏策略；保持浅睡不设置自动断电截止，手动关机独立。/ S31 lock policy; keep-light has no automatic power-off deadline, independent of manual off.
app_lock_sleep_mode_t app_settings_lock_sleep_mode(void);
esp_err_t app_settings_set_lock_sleep_mode(app_lock_sleep_mode_t mode);
bool app_settings_keep_light_sleep(void);
/// S31关机：默认浅睡十分钟后同步RTC并硬关机；false立即断电，保存后生效。/ S31 syncs RTC and powers off after ten minutes of light sleep; false powers off immediately, effective after saving.
#else
/// 电源菜单“关机”：false 为彻底断电，true 为先浅睡 10 分钟再由 PMU 软睡断电。
/// Power-menu shutdown: false powers fully off; true light-sleeps for 10 minutes, then PMU soft-sleeps.
#endif
bool app_settings_staged_shutdown(void);
void app_settings_set_staged_shutdown(bool staged);
/// 电源键开机次数：1 单击开机，2 双击开机；默认 1，已保存的选择继续保留。
/// Power-key boot presses: 1 boots on a single press, 2 needs a double click; defaults to 1 and retains an explicit saved choice.
uint8_t app_settings_wake_presses(void);
void app_settings_set_wake_presses(uint8_t presses);
/// 可选密码宽限期0/5/60/480/1440分钟，默认关闭。/ Optional PIN grace0/5/60/480/1440min, off by default.
uint8_t app_settings_pin_grace_code(void);
void app_settings_set_pin_grace_code(uint8_t code);
unsigned app_settings_pin_grace_minutes(void);
uint32_t app_settings_pin_locked_at(void);
void app_settings_record_pin_lock(uint32_t unix_seconds);
/// 首页进入时强制全刷；无配置时默认开启，保留已保存的选择。/ Full refresh when entering Home; on when unconfigured, retaining saved choices.
bool app_settings_home_full_refresh(void);
void app_settings_set_home_full_refresh(bool enabled);
/// 首页组件无配置时默认Moon Clock；选择与每日目标掉电保存。/ Default to Moon Clock when unconfigured; persist the widget and daily goal.
typedef enum {
    APP_HOME_FOLDER = 0, APP_HOME_CLOCK = 1, APP_HOME_ASTERISK = 2,
    // 3为旧组件的保留编号，不能重排已有配置。/ ID 3 is retired; preserve existing persisted IDs.
    APP_HOME_MOON = 4, APP_HOME_KEHAI = 5, APP_HOME_DALI = 6, APP_HOME_CAT = 7, APP_HOME_HOUSE = 8, APP_HOME_COMPONENT_COUNT = 9
} app_home_component_t;
app_home_component_t app_settings_home_component(void);
esp_err_t app_settings_set_home_component(app_home_component_t component);
uint8_t app_settings_home_goal_minutes(void);
esp_err_t app_settings_set_home_goal_minutes(uint8_t minutes);
/// 主页刷新模式，只控制四个主页面；默认普通。/ Main-tab refresh mode; ordinary by default.
typedef enum {
    APP_MAIN_REFRESH_NORMAL = 0, ///< 普通灰阶 / Ordinary grayscale
    APP_MAIN_REFRESH_FAST = 1,   ///< 黑白快刷与灰阶亚克力 / Monochrome fast with gray acrylic
    APP_MAIN_REFRESH_WATER = 2,  ///< 水波纹灰阶 / Staggered grayscale
} app_main_refresh_mode_t;
app_main_refresh_mode_t app_settings_main_refresh_mode(void);
void app_settings_set_main_refresh_mode(app_main_refresh_mode_t mode);
/// 绘制主页面黑白资源时使用，其他两种模式保留灰阶。/ Used to render monochrome resources; the other modes retain grayscale.
bool app_settings_main_fast_refresh(void);
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
/// 锁屏：0阅读票根、1壁纸、2书架拼贴、3透明书封。/ Lock: ticket (0), wallpaper (1), collage (2), PNG cover (3).
uint8_t app_settings_lock_style(void);
void app_settings_set_lock_style(uint8_t style);
const char* app_settings_bookmark_path(void);
void app_settings_set_bookmark_path(const char *path);
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
/// 阅读默认字号，18..90，默认 48。/ Default reading size, 18..90, initially 48.
/// Reading stroke weight -2..2: thin, lighter, native, heavier, bold.
int8_t app_settings_book_weight(void);
#define APP_BOOK_WEIGHT_SUBSTEPS 16
#define APP_BOOK_WEIGHT_FINE_MAX 32
/// 一档为1/16像素，旧像素值继续兼容。/ One step is 1/16 pixel; legacy pixel values remain compatible.
int8_t app_settings_book_weight_fine(void);
bool app_settings_set_book_weight_fine(int8_t steps);
bool app_settings_set_book_weight(int8_t level);
/// 阅读抗锯齿默认开启；关闭只改变字形边缘，不改变排版或图片。/ Reading AA defaults on; disabling affects glyph edges, not layout or images.
bool app_settings_book_antialias(void);
bool app_settings_set_book_antialias(bool enabled);
uint8_t app_settings_book_px(void);
/// 无效字号恢复 48。/ Invalid sizes fall back to 48.
void app_settings_set_book_px(uint8_t px);
/// Reading backgrounds are selected from /sdcard/pictures; empty means white.
const char *app_settings_book_background_path(void);
bool app_settings_set_book_background_path(const char *path);
/// 阅读时每 5/10/15/30 页全刷；0 关闭周期全刷，手动全刷仍可用。
/// Full-screen cleanup every 5/10/15/30 turns; 0 disables periodic cleanup only.
/// S31另支持20页，所有翻页效果均遵循所选间隔。/ S31 also supports20; every turn effect respects the selected interval.
uint8_t app_settings_reader_full_pages(void);
void app_settings_set_reader_full_pages(uint8_t pages);
/// 阅读翻页效果：0普通，1水波纹，2速读黑白；旧0/1配置不变。/ Reader effect:0 ordinary,1 ripple,2 fast BW; preserve legacy0/1 settings.
uint8_t app_settings_reader_turn_effect(void);
void app_settings_set_reader_turn_effect(uint8_t effect);
/// 阅读正文里短按电源键翻下一页；默认关闭。/ Short power press turns forward only in reader body; off by default.
bool app_settings_reader_power_turn(void);
void app_settings_set_reader_power_turn(bool on);
/// 新开书使用的全屏偏好；锁屏恢复遵从记录。/ Fresh-entry preference; lock/resume keeps its recorded state.
bool app_settings_reader_default_fullscreen(void);
/// 保存成功后才更新偏好。/ Apply the preference only after a successful save.
bool app_settings_set_reader_default_fullscreen(bool on);
/// 沉浸全屏时隐藏阅读状态栏。/ Hide the reader status bar in immersive full screen.
bool app_settings_reader_immersive(void);
void app_settings_set_reader_immersive(bool on);
/// 阅读时跳过书内插图页；原图仍保留在 EPUB 中。/ Skip inline illustrations while reading without changing the EPUB.
bool app_settings_reader_hide_images(void);
void app_settings_set_reader_hide_images(bool on);
/// 使用书内自带 TTF（EPUB @font-face 或包内字体）；默认开启，关掉后正文回到系统字体。
/// Use embedded book TTFs (CSS @font-face or a packaged face); on by default. Turning it off
/// puts body text back on the system face.
bool app_settings_book_fonts(void);
void app_settings_set_book_fonts(bool on);
/// 竖排正文（自上而下成列、列序自右向左）；默认关闭，只影响正文排版与插图分页。
/// Vertical body text (columns run down, ordered right to left); off by default, and only the
/// body layout and illustration pagination are affected.
bool app_settings_book_vertical(void);
void app_settings_set_book_vertical(bool on);
typedef enum {
    APP_READER_KEY_PREV, ///< 上一页 / Previous page
    APP_READER_KEY_NEXT, ///< 下一页 / Next page
    APP_READER_KEY_TOOLS, ///< 阅读工具栏 / Reader toolbar
    APP_READER_KEY_FULLSCREEN, ///< 切换全屏 / Toggle full screen
    APP_READER_KEY_HOME, ///< 返回主页（本次开书来源） / Return to the opening origin
    APP_READER_KEY_REFRESH, ///< 手动全刷 / Manual full refresh
    APP_READER_KEY_NONE, ///< 不操作 / No action
    APP_READER_KEY_COUNT
} app_reader_key_action_t;
/// 三个短按功能，0/1/2对应左/中/右；长按独立配置。/ Short actions indexed left/middle/right, with a separate hold action.
app_reader_key_action_t app_settings_reader_key_action(unsigned key);
/// 仅验证按键与动作范围，不强制工具栏映射；3代表中键长按。/ Validate key/action ranges without requiring a toolbar mapping; 3 means middle hold.
bool app_settings_reader_key_action_allowed(unsigned key, app_reader_key_action_t action);
/// 返回是否成功保存；失败不改变映射。/ Return successful persistence, leaving the mapping unchanged on failure.
bool app_settings_set_reader_key_action(unsigned key, app_reader_key_action_t action);
/// 中键长按，默认手动全刷；与短按独立保存。/ Middle hold defaults to full refresh and persists independently.
app_reader_key_action_t app_settings_reader_hold_action(void);
bool app_settings_set_reader_hold_action(app_reader_key_action_t action);
/// 一次提交整套推荐设置，1为翻页，2为首页/全屏/工具栏。/ Commit preset 1 (page turns) or 2 (Home/full screen/tools) together.
void app_settings_set_reader_key_preset(unsigned preset);
/// 旧长按设置API保留用于历史备份兼容。/ Retained legacy hold APIs for historical backup compatibility.
bool app_settings_reader_hold_refresh(void);
void app_settings_set_reader_hold_refresh(bool on);
/// 上1/3与下2/3轻点分区；默认左右各半。/ Upper 1/3 and lower 2/3 tap regions; default horizontal halves.
bool app_settings_reader_vertical_turn(void);
void app_settings_set_reader_vertical_turn(bool on);
/// 书架排序偏好重启后保持。/ Persist the shelf's recent-reading sort across restarts.
/// 顺序编码保留旧0/1；2名称降、3最近升、4添加升、5添加降。/ Legacy0/1, then name-desc/recent-asc/added-asc/added-desc.
uint8_t app_settings_shelf_order(void);
void app_settings_set_shelf_order(uint8_t order);
bool app_settings_shelf_recent_sort(void);
void app_settings_set_shelf_recent_sort(bool on);
/// 蓝牙翻页器：启用 BLE HID 主机，好让蓝牙翻页器连上并翻页；默认关闭。
/// 关掉会把整个 BLE 栈拆掉，把 NimBLE 占的内存还回去。
/// Bluetooth page-turner: bring up the BLE HID host so a remote can pair and turn pages; off
/// by default. Turning it off tears the whole stack down and returns NimBLE's memory.
bool app_settings_ble_turner(void);
void app_settings_set_ble_turner(bool on);
/// S31快充：默认开启；关闭回到标准充电电流，切换立即生效并掉电保存。/ S31 fast charge: on by default; off returns to the standard current, and the choice persists.
bool app_settings_fast_charge(void);
void app_settings_set_fast_charge(bool on);
/// 字间距默认0px，31档以0居中。/ Default 0px, centered among 31 tracking stops.
#define APP_BOOK_TRACK_MIN (-15)
#define APP_BOOK_TRACK_MAX 15
#define APP_BOOK_TRACK_COUNT (APP_BOOK_TRACK_MAX-APP_BOOK_TRACK_MIN+1)
/// 字间距索引0..30对应-15..+15px，每档1px。/ Tracking indices 0..30 map to -15..+15px, in 1px steps.
uint8_t app_settings_book_tracking(void);
void app_settings_set_book_tracking(uint8_t index);
/// 普通正文首行缩进 0..3 字，默认两字。/ First-line indent: 0..3 em, default 2.
uint8_t app_settings_book_indent(void);
void app_settings_set_book_indent(uint8_t em);
/// 正文首行缩进微调，-100..100像素，默认0；无缩进时不生效。/ First-line adjustment in pixels, -100..100 (default 0); ignored with no indent.
int8_t app_settings_book_indent_adjust(void);
void app_settings_set_book_indent_adjust(int8_t px);
/// 正文阅读线：0 无，1 虚线，2 点线。/ Body reading guides: none, dashed or dotted.
uint8_t app_settings_book_reading_line(void);
void app_settings_set_book_reading_line(uint8_t style);
/// 阅读线范围，false 跟随文字，true 整行/整列；提交成功才生效。/ Guide span, applied after commit.
bool app_settings_book_reading_line_full(void);
bool app_settings_set_book_reading_line_full(bool full);
/// 阅读线相对默认位置 -8..+8 像素，2 像素一步，默认 0。/ Guide offset in 2 px steps.
int8_t app_settings_book_reading_line_offset(void);
void app_settings_set_book_reading_line_offset(int8_t offset_px);
/// 阅读行高百分比，110..150。/ Reader line-height percentage: 110..150.
uint8_t app_settings_book_line_spacing(void);
void app_settings_set_book_line_spacing(uint8_t percent);
/// 阅读页左右边距，0..48 像素。/ Reader horizontal margin: 0..48 pixels.
uint8_t app_settings_book_margin(void);
void app_settings_set_book_margin(uint8_t px);
/// 段距11档，25%居中，0..100%。/ Eleven paragraph-gap stops centered on 25%, spanning 0..100%.
#define APP_BOOK_PARAGRAPH_COUNT 11
uint8_t app_settings_book_paragraph_value(uint8_t level);
uint8_t app_settings_book_paragraph_level(uint8_t percent);
uint8_t app_settings_book_paragraph_spacing(void);
void app_settings_set_book_paragraph_spacing(uint8_t percent);
/// 书架样式：1 深色书轨、2 亚克力、5 列表、6 液态玻璃；旧3/4安全回退2。/ Shelf styles: rail (1), acrylic (2), list (5), liquid glass (6); retired IDs 3/4 fall back to 2.
uint8_t app_settings_shelf_style(void);
void app_settings_set_shelf_style(uint8_t style);
/// 固定 TF 根目录：/sdcard/books 与 /sdcard/fonts；旧配置/备份不能修改。/ Fixed TF roots, unaffected by legacy settings/backups.
const char* app_settings_books_dir(void);
const char* app_settings_fonts_dir(void);
/// TF卡内自定义目录，持久化成功后才更改当前路径。/ Custom SD roots change only after persistence succeeds.
bool app_settings_set_books_dir(const char* path);
bool app_settings_set_fonts_dir(const char* path);

/// 将设置、阅读记录和 WiFi 凭据保存到 TF 卡根目录；备份含明文密码，需妥善保管。
/// Save settings, reading records and WiFi credentials to TF root; keep the plaintext-secret backup private.
esp_err_t app_settings_backup_save(void);
/// 校验备份后恢复 NVS；缺失的外部字体或壁纸回退到内建资源。
/// Validate before restoring NVS; missing external assets fall back safely.
esp_err_t app_settings_backup_restore(void);

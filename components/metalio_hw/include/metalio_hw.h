/* SPDX-License-Identifier: Apache-2.0
 * 中文：Metalio V1.5 硬件；总线、扩展口与电源轨由此独占。
 * English: Metalio V1.5 hardware owns the shared bus, expander and rails.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "fca9555.h"
#include "esp_err.h"

esp_err_t metalio_hw_init(void);
i2c_master_bus_handle_t metalio_bus(void);
fca9555_handle_t metalio_ioe(void);
esp_err_t metalio_read(uint8_t address, uint8_t reg, void *data, size_t size);
esp_err_t metalio_write(uint8_t address, uint8_t reg, const void *data, size_t size);
esp_err_t metalio_write8(uint8_t address, uint8_t reg, uint8_t data);
esp_err_t metalio_io_set(unsigned pin, bool high);
esp_err_t metalio_io_read(uint16_t *inputs);
/// 原厂休眠掩码：非按键中断隔离，仅 P17 电源键保留输入；失败后调用恢复。
/// Reference sleep mask: isolate non-key IRQs, keeping only P17 as an input; restore after a failed admission.
esp_err_t metalio_prepare_key_wake(void);
esp_err_t metalio_restore_key_io(void);
bool metalio_key_down(void);
/// 两次有效P17低电平才确认电源键；共享GPIO2中断或读失败不能确认。
/// Confirm power only after two valid low P17 samples; shared GPIO2 or failed reads are insufficient.
bool metalio_confirm_power_key(void);
/// 浅睡返回后立即读P17，两次低电平才确认。/ On light-sleep return, require two immediate P17 low samples.
bool metalio_confirm_light_wake_key(void);
/// 深睡启动硬件初始化时确认的真实按键。/ Real key confirmed early during deep-boot hardware initialization.
bool metalio_boot_power_key(void);
/// Consume optional one-use bootloader P17 evidence before loading UI/media.
void metalio_capture_boot_power_key(void);
int metalio_take_volume(void);
/// 官方AI/BOOT输入GPIO61，25ms去抖、500ms长按；长按抬手不触发短按。
/// Vendor AI/BOOT input GPIO61,25ms debounce and500ms hold; release after a hold never produces a tap.
enum { METALIO_AI_NONE, METALIO_AI_SHORT, METALIO_AI_LONG };
void metalio_ai_sample(bool down,int64_t now_ms);
int metalio_take_ai(void);
/// 丢弃跨锁屏/休眠输入；重新稳定释放后才接收。/ Discard cross-lock/sleep input until a stable release.
void metalio_ai_clear(void);
void metalio_volume_clear(void);
void metalio_volume_sample(uint16_t inputs,int64_t now_ms);
esp_err_t metalio_tps_on(int vcom_mv);
/// 仅提前唤醒TPS，PWRUP/VCOM保持低；取消或失败仍下电。/ Wake only TPS ahead of paint, holding PWRUP/VCOM low; cancel/failure powers down.
esp_err_t metalio_tps_prepare(void);
void metalio_tps_cancel_prepare(void);
void metalio_tps_off(void);
/// P00–P02 与 PGOOD 全低才确认断电，TPS 已被重新启用时返回 ESP_ERR_INVALID_STATE。
/// Confirm power-off from P00–P02 and PGOOD; return ESP_ERR_INVALID_STATE if TPS is active or preparing.
esp_err_t metalio_tps_verify_off(void);
bool metalio_tps_ready(void);
/// 读 TPS65185 的 VCOM 寄存值；断电后返回最近成功回读，失败 -1。这不是电压表测量。
/// Read VCOM registers (10mV/LSB); retain the last successful read while powered off, -1 on failure. Not a voltage measurement.
int metalio_tps_vcom_mv(void);
void metalio_frontlight(unsigned brightness, unsigned warmth);
unsigned metalio_brightness(void);
unsigned metalio_warmth(void);
bool metalio_frontlight_enabled(void);
#define METALIO_FRONTLIGHT_RANGE 240
unsigned metalio_frontlight_position(bool warmth);
void metalio_frontlight_preview_position(unsigned brightness, unsigned warmth);
void metalio_frontlight_restore(void);
void metalio_frontlight_save(unsigned brightness, unsigned warmth);
void metalio_frontlight_preview(unsigned brightness, unsigned warmth);
void metalio_frontlight_toggle(void);
void metalio_frontlight_commit(void);
bool metalio_haptic_enabled(void);
void metalio_haptic_set_enabled(bool enabled);
esp_err_t metalio_haptic_click(void);
void metalio_haptic_stop(void);
/// 失败关机恢复按键反馈。/ Resume feedback after a failed power-off.
void metalio_haptic_resume(void);
void metalio_hold_for_deep_sleep(void);
void metalio_sd_release_hold(void);
/// 卸载后将单线模式不用的D1～D3恢复高阻上拉，避免沿用睡眠低电平。
/// After unmount, release unusedD1–D3 to pulled-up inputs before a single-line mount.
esp_err_t metalio_sd_single_line_idle(void);
/// 官方浅睡引脚与电源轨事务；不触碰 GPIO36 下载通路。
/// Vendor light-sleep pin/rail transaction; retain the GPIO36 download route.
esp_err_t metalio_hold_for_light_sleep(void);
esp_err_t metalio_prepare_light_sleep_pins(void);
void metalio_release_light_sleep_pins(void);
esp_err_t metalio_sleep_cut_rails(uint16_t *saved);
esp_err_t metalio_sleep_restore_rails(uint16_t saved);
void metalio_ana_sw(bool high);
/// S31独立USJ与UTMI，以下仅恢复USJ及板上GPIO36通路。
/// S31 has separate USJ/UTMI PHYs; these recover only USJ and the board GPIO36 route.
/// 正常串口高保持；仅读卡显式释放到UTMI，失败须停止交接。
/// Retain the normal serial route; only explicit MSC may release it to UTMI.
esp_err_t metalio_usb_route_serial(bool serial);
void metalio_usb_serial_startup(void);
void metalio_usb_serial_reconnect(void);
/// 串口连接/外部VBUS/枚举宽限/充电器读取失败时阻止手动睡眠。
/// Block manual sleep for USJ, external VBUS, enumeration grace or an unknown charger state.
bool metalio_usb_blocks_sleep(void);
void metalio_clear_download_request(void);
/// 下载交接：确认USJ通路并单独保持SEL；准备失败不得重启。
/// Download handoff: verify USJ and retain SEL alone; never restart after a failed preparation.
esp_err_t metalio_download_prepare(void);
void metalio_download_cancel(void);
/// 断开板上USB通路500ms，按S31 HAL复位USJ并恢复驱动中断，再等待电脑进入ROM。
/// Disconnect the board USB route for 500ms, reset USJ with the S31 HAL, restore driver interrupts, then await host ROM entry.
esp_err_t metalio_request_download(void);
/// 只表示检测到USB SOF，不能据此宣称已进入ROM。/ USB SOF presence, not proof of ROM download.
bool metalio_download_connected(void);
/// 只读USB状态快照；供串口失效时写入TF诊断，不表示ROM握手成功。
/// Read-only USB snapshot for TF diagnostics when CDC is unavailable; not proof of ROM handshake.
void metalio_usb_describe(char *out,size_t size);
void metalio_poweroff(void);

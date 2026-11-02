/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
#pragma once
#include "esp_err.h"
#include "sdmmc_cmd.h"
/// S31 USB独占原始卡；复用挂载总线配置和内部DMA，调用前关闭文件。/ S31 raw USB ownership reuses mount bus setup/internal DMA; close files first.
esp_err_t read_pico_sd_usb_acquire(sdmmc_card_t **out);
/// TinyUSB完全停止后释放原始卡；失败保留资源供重试。/ Release raw storage after TinyUSB stops; failures retain resources for retry.
esp_err_t read_pico_sd_usb_release(void);

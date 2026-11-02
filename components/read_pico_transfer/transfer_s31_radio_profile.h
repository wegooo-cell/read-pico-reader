/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：按Metalio公开网络文档统一S31运行期策略；不存取网络凭据。
 * English: Apply the documented Metalio S31 runtime profile without touching credentials.
 * Reference: CloudZao/Metalio-E-INK4-Plus@ad8edf4 docs/ap-file-transfer.md (MIT).
 */
#pragma once
#include <stdbool.h>
#include "esp_wifi.h"
#define TRANSFER_S31_MAX_TX_POWER 48
static inline esp_err_t transfer_s31_radio_profile(wifi_interface_t interface) {
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) err = esp_wifi_set_protocol(interface,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (err == ESP_OK) err = esp_wifi_set_max_tx_power(TRANSFER_S31_MAX_TX_POWER);
    return err;
}

// SDK: AMPDU接收池应覆盖BA窗口；只给独占传书补齐，最大16槽，日常STA/BLE不增占用。
// SDK: cover the BA window with static RX buffers; bulk-only, capped at16, retaining normal STA/BLE memory.
static inline void transfer_s31_bulk_receive_config(wifi_init_config_t *init, bool bulk) {
    if (!init || !bulk || !init->ampdu_rx_enable || init->rx_ba_win <= 0 ||
        init->rx_ba_win > 16 ||
        (init->dynamic_rx_buf_num && init->dynamic_rx_buf_num < init->rx_ba_win)) return;
    int window=init->rx_ba_win;
#ifdef CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP
    // 独占传书统一到SDK建议的16槽；原静态池已经够大也不能跳过BA设置。
    // Bulk PSRAM reception uses the SDK's16-slot recommendation even when the static pool was already large.
    if (!init->dynamic_rx_buf_num || init->dynamic_rx_buf_num >= 16)window=16;
#endif
    init->rx_ba_win=window;
    if(init->static_rx_buf_num<window)init->static_rx_buf_num=window;
}

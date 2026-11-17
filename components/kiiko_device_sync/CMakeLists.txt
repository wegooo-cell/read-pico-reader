# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：设置页专用的 ESP-NOW 近场同步；不存凭据，不碰分区。/ English: Settings-page ESP-NOW sync; no credentials, no partitions.
idf_component_register(
    SRCS "kiiko_device_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif esp_timer esp_hw_support esp_system ble_page_turner
)
# 设置页同步是偶发操作，按体积优化；Pico 的应用分区已经贴着上限。
# Settings sync runs rarely, so optimise it for size; the Pico app partition is already at its ceiling.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)

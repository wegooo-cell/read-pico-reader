include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
if(KIIKO_BOARD_METALIO47)
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 独立热点传书组件与嵌入页面。/ Standalone hotspot transfer component and embedded page.
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "read_pico_transfer.c" "transfer_http_time.c" "transfer_ntp_time.c")
idf_component_register(SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES read_pico kiiko_locale esp_app_format ble_page_turner read_pico_search esp_wifi esp_netif esp_event esp_http_server esp_http_client mbedtls lwip nvs_flash fatfs esp_timer espressif__cjson
    EMBED_TXTFILES "upload.html")
option(METALIO_NETWORK_PROBE "Local S31 read-only radio diagnostics" OFF)
if(METALIO_NETWORK_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_LOCAL_NETWORK_PROBE=1)
endif()

else()
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 独立热点传书组件与嵌入页面。/ Standalone hotspot transfer component and embedded page.
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "read_pico_transfer.c")
idf_component_register(SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES kiiko_locale ble_page_turner read_pico_search esp_wifi esp_netif esp_event esp_http_server nvs_flash fatfs esp_timer espressif__cjson
    EMBED_TXTFILES "upload.html")

endif()

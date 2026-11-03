include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
if(NOT KIIKO_BOARD_METALIO47)
    idf_component_register()
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "metalio_hw.c" "metalio_charger.c" "metalio_usb.c" "metalio_ai_key.c")
idf_component_register(SRCS ${kiiko_selected_sources} INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_driver_i2c esp_driver_gpio esp_driver_ledc fca9555
    PRIV_REQUIRES esp_hal_wdt riscv esp_hw_support esp_system freertos esp_timer esp_rom soc efuse esp_driver_usb_serial_jtag esp_hal_usb)
if(CONFIG_METALIO_EARLY_POWER_KEY)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u metalio_early_key_app_desc")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/s31_sleep/s31_sleep_sdk.cmake")

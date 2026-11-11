include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
if(KIIKO_BOARD_METALIO47)
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "metalio_power_adapter.c" "metalio_soc_filter.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c metalio_hw esp_timer
    PRIV_REQUIRES freertos esp_rom
)

else()
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "read_pico_pmu.c" "pico_soc_filter.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES freertos esp_rom esp_timer
)

endif()

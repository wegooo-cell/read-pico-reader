include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
if(KIIKO_BOARD_METALIO47)
set(read_pico_requires metalio_hw epdiy e0470_epaper_waveform sc7a20h cst836u read_pico_pmu fatfs fca9555 sy7636a
                   esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt esp_driver_sdmmc)

kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "read_pico_init.c" "read_pico_board.c" "read_pico_epd_timing.c" "metalio_haptic.c" "read_pico_sd.c" "read_pico_sd_access.c" "read_pico_crashlog.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${read_pico_requires}
    PRIV_REQUIRES bootloader_support spi_flash freertos esp_timer esp_mm
    # flash_hpm_zb.c 只定义了一个覆盖 IDF weak 符号的表，没有任何
    # 代码引用它，不整包链接的话这个目标文件会被丢掉，覆盖就不生效。
    WHOLE_ARCHIVE
)

# TF生命周期只拦S31的标准文件入口，Pico保持原链接。/ Guard S31 SD standard file entry points only; retain Pico linkage.
foreach(sd_fn _open_r _close_r _stat_r _unlink_r _rename_r _link_r mkdir rmdir access utime truncate opendir closedir esp_vfs_fat_info)
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${sd_fn}")
endforeach()

else()
set(read_pico_requires epdiy e0470_epaper_waveform sc7a20h cst836u read_pico_pmu fatfs sy7636a fca9555
                   esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt esp_driver_sdmmc pwm_audio)

kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "read_pico_init.c" "read_pico_board.c" "read_pico_epd_timing.c" "read_pico_buzzer.c" "buzzer_score.c" "buzzer_duty_map.c" "buzzer_1bit.c" "read_pico_sd.c" "read_pico_flash_hpm.c" "read_pico_crashlog.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${read_pico_requires}
    PRIV_REQUIRES bootloader_support spi_flash freertos esp_timer
    # flash_hpm_zb.c 只定义了一个覆盖 IDF weak 符号的表，没有任何
    # 代码引用它，不整包链接的话这个目标文件会被丢掉，覆盖就不生效。
    WHOLE_ARCHIVE
)

endif()

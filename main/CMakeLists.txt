include("${CMAKE_CURRENT_LIST_DIR}/../cmake/board.cmake")
if(KIIKO_BOARD_METALIO47)
set(main_requires
    kiiko_locale
    kiiko_board
    read_pico
    metalio_hw
    read_pico_transfer
    read_pico_search
    espressif__qrcode
    espressif__esp_jpeg
    esp_driver_jpeg
    read_pico_webp
    espressif__libjpeg-turbo
    # 渐进式 JPEG；基线仍走 esp_jpeg。/ Progressive JPEG; baseline stays on esp_jpeg.
    jpegdec
    espressif__libpng
    espressif__zlib
    espressif__esp_tinyusb
    # BLE HID 主机（蓝牙翻页器）。本仓库开了 MINIMAL_BUILD，组件不进依赖图的话连它的
    # Kconfig 都不会加载，sdkconfig.defaults 里的 CONFIG_BT_* 会被判为未知符号而丢弃。
    # BLE HID host (Bluetooth page-turner). This tree sets MINIMAL_BUILD, so a component
    # outside the dependency graph does not even load its Kconfig and the CONFIG_BT_*
    # entries in sdkconfig.defaults are dropped as unknown symbols.
    bt
    # 蓝牙翻页器本体。/ The page-turner itself.
    ble_page_turner
    continuous_du
    e0470_epaper_waveform
    e0470_page_turn
    read_pico_pmu
    fatfs
    wear_levelling
    bootloader_support
    spi_flash
    heap
    nvs_flash
    esp_hw_support
    esp_rom
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    esp_app_format
    app_update
    esp_http_client
    mbedtls
    espressif__cjson
)
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "app_main.c"
        "boot_state.c"
        "apps/app_boot_recovery.c"
        "ota_update.c"
        "ota_online.c"
        "ota_release.c"
        "display.c"
        "anti_fade.c"
        "display_main_waveform.c"
        "display_reader_waveform.c"
        "metalio_text_ripple.c"
        "sleep.c"
        "time_service.c"
        "lock_pin.c"
        "lock_pin_flow.c"
        "lock_screen.c"
        "settings.c"
        "setup_state.c"
        "apps/app_onboarding.c"
        "factory_reset.c"
        "app/app_loop.c"
        "app/sd_recovery.c"
        "app/crash_note.c"
        "app/app_registry.c"
        "ui/ui_kit.c"
        "ui/ui_text_edit.c"
        "ui/ui_text_input.c"
        "ui/ui_ime.c"
        "ui/ui_keyboard.c"
        "ui/ui_gesture.c"
        "ui/ui_glass.c"
        "ui/ui_pinpad.c"
        "ui/ui_power_dialog.c"
        "ui/ui_quick_menu.c"
        "ui/ui_ai_menu.c"
        "ui/ui_lock_feedback.c"
        "ui/ui_quick_menu_input.c"
        "ui/ui_click_feedback.c"
        "ui/ui_wifi_qr.c"
        "ui/datamatrix.c"
        "ui/ui_menu.c"
        "ui/ui_nav.c"
        "ui/ui_home_widget.c"
        "ui/ui_home_clock.c"
        "ui/ui_image_dither.c"
        "ui/ui_wallpaper.c"
        "ui/ui_avatar.c"
        "ui/ui_reader_background.c"
        "apps/app_book.c"
        "apps/book_toc.c"
        "apps/app_dashboard.c"
        "apps/app_files.c"
        "apps/file_tree.c"
        "apps/file_search.c"
        "apps/app_device_settings.c"
        "apps/app_image.c"
        "apps/app_transfer.c"
        "apps/usb_storage.c"
        "book/book_store.c"
        "book/book_progress.c"
        "book/book_history_backup.c"
        "book/book_title.c"
        "book/book_added.c"
        "book/book_ticket.c"
        "book/book_excerpt.c"
        "book/book_lock_collage.c"
        "book/book_lock_bookmark.c"
        "book/book_source.c"
        "book/book_index_cache.c"
        "book/book_txt.c"
        "book/book_epub.c"
        "book/book_cover.c"
        "book/metalio_jpeg.c"
        "book/book_cover_jobs.c"
        "book/metalio_cover_cache.c"
        "book/reader_page_cache.c"
        "book/book_image_header.c"
        "book/book_cover_auto.c"
        "book/zip_reader.c"
        "book/html_text.c"
        "book/book_layout.c"
        "book/gbk.c"
        "font/ttf_font.c"
        "font/app_font_context.c"
        "font/online_font.c"
        "font/online_font_store.c"
        "ui/ui_online_fonts.c"
        "font/ui_font.c"
        "font/ui_hanzi.c"
        "factory/pmu_selftest.c"
        "factory/vcom_setup.c"
        "factory/debug_panel.c")
idf_component_register(
    LDFRAGMENTS "metalio_crash.lf"
    SRCS ${kiiko_selected_sources}
    REQUIRES ${main_requires}
    INCLUDE_DIRS "." "app" "apps" "font" "ui" "factory" "book"
    EMBED_FILES
        "assets/builtin.ttf"
        "assets/ui-hanzi.bin"
        "assets/display-test.png"
)

# 包住 IDF 的 panic 处理器：崩溃现场在重启前落进 RTC，没有串口时那是唯一的现场。
# 链接选项挂在 main 上，INTERFACE 会带到最终链接；写在 project() 之后的全局属性则太晚。
# Wrap the IDF panic handler so the crash scene reaches RTC before a restart; without a console
# that is the only place it survives. The option hangs off main, whose INTERFACE carries it into
# the final link; a global property set after project() is too late for the linker to see it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")

# 本地显式测时构建才接收 USB 测试命令；交付构建默认关闭。
# Only explicit local benchmark builds accept USB probe commands; delivery defaults off.
option(METALIO_LOCAL_LATENCY "Local USB latency probe; never enable for delivery" OFF)
if(METALIO_LOCAL_LATENCY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_LOCAL_LATENCY=1)
endif()
option(METALIO_LOCAL_TRANSFER_START "Unattended local WiFi transfer test; never deliver" OFF)
if(METALIO_LOCAL_TRANSFER_START)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_LOCAL_TRANSFER_START=1)
endif()

# 仅显式本地实验包启用；普通构建不继承缓存中的实验值。/ Explicit local packages only; normal builds must not inherit cached experiment flags.
option(METALIO_MAIN_CLEAN_EXPERIMENT "Local S31 stronger root-switch white erasure" OFF)
if(METALIO_MAIN_CLEAN_EXPERIMENT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_MAIN_CLEAN=1)
endif()
if(METALIO_DU14_EXPERIMENT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_DU14=1)
endif()
option(METALIO_FAST_READER_PANELS "Local four-gray font/reading panels" OFF)
if(METALIO_FAST_READER_PANELS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_FAST_READER_PANELS=1)
endif()
option(METALIO_READER_PANEL37_EXPERIMENT "Local vendor-derived S31 GL37 reader panels" OFF)
if(METALIO_READER_PANEL37_EXPERIMENT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_READER_PANEL37=1)
endif()
option(METALIO_SHELF37_EXPERIMENT "Local vendor-derived S31 GL37 shelf art/chrome" OFF)
if(METALIO_SHELF37_EXPERIMENT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_SHELF37=1)
endif()
option(METALIO_TEXT_RIPPLE_EXPERIMENT "Local four-level reader ripple experiment" OFF)
if(METALIO_TEXT_RIPPLE_EXPERIMENT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_TEXT_RIPPLE=1)
endif()

else()
set(main_requires
    kiiko_locale
    kiiko_board
    read_pico
    read_pico_transfer
    read_pico_search
    espressif__qrcode
    espressif__esp_jpeg
    # 渐进式 JPEG；基线仍走 esp_jpeg。/ Progressive JPEG; baseline stays on esp_jpeg.
    jpegdec
    defensiblelogic__lowzag
    # WebP 插图与封面的解码器（libwebp 解码子集）。/ WebP decoder subset for illustrations and covers.
    read_pico_webp
    espressif__libpng
    espressif__zlib
    espressif__esp_tinyusb
    # BLE HID 主机（蓝牙翻页器）。本仓库开了 MINIMAL_BUILD，组件不进依赖图的话连它的
    # Kconfig 都不会加载，sdkconfig.defaults 里的 CONFIG_BT_* 会被判为未知符号而丢弃。
    # BLE HID host (Bluetooth page-turner). This tree sets MINIMAL_BUILD, so a component
    # outside the dependency graph does not even load its Kconfig and the CONFIG_BT_*
    # entries in sdkconfig.defaults are dropped as unknown symbols.
    bt
    # 蓝牙翻页器本体。/ The page-turner itself.
    ble_page_turner
    continuous_du
    e0470_epaper_waveform
    e0470_page_turn
    read_pico_pmu
    fatfs
    wear_levelling
    bootloader_support
    spi_flash
    heap
    nvs_flash
    esp_hw_support
    esp_rom
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    esp_app_format
    app_update
    esp_http_client
    mbedtls
    espressif__cjson
)
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "app_main.c"
        "boot_state.c"
        "apps/app_boot_recovery.c"
        "ota_update.c"
        "ota_online.c"
        "ota_release.c"
        "display.c"
        "display_main_waveform.c"
        "display_reader_waveform.c"
        "sleep.c"
        "time_service.c"
        "lock_pin.c"
        "lock_pin_flow.c"
        "lock_screen.c"
        "settings.c"
        "setup_state.c"
        "apps/app_onboarding.c"
        "factory_reset.c"
        "app/app_loop.c"
        "app/crash_note.c"
        "app/app_registry.c"
        "ui/ui_kit.c"
        "ui/ui_text_edit.c"
        "ui/ui_text_input.c"
        "ui/ui_ime.c"
        "ui/ui_keyboard.c"
        "ui/ui_gesture.c"
        "ui/ui_glass.c"
        "ui/ui_pinpad.c"
        "ui/ui_power_dialog.c"
        "ui/ui_quick_menu.c"
        "ui/ui_lock_feedback.c"
        "ui/ui_quick_menu_input.c"
        "ui/ui_click_feedback.c"
        "ui/ui_wifi_qr.c"
        "ui/datamatrix.c"
        "ui/ui_menu.c"
        "ui/ui_nav.c"
        "ui/ui_home_widget.c"
        "ui/ui_home_clock.c"
        "ui/ui_image_dither.c"
        "ui/ui_wallpaper.c"
        "ui/ui_avatar.c"
        "ui/ui_reader_background.c"
        "apps/app_book.c"
        "apps/book_toc.c"
        "apps/app_dashboard.c"
        "apps/app_files.c"
        "apps/file_tree.c"
        "apps/file_search.c"
        "apps/app_device_settings.c"
        "apps/app_image.c"
        "apps/app_transfer.c"
        "apps/usb_storage.c"
        "book/book_store.c"
        "book/book_progress.c"
        "book/book_history_backup.c"
        "book/book_title.c"
        "book/book_added.c"
        "book/book_ticket.c"
        "book/book_excerpt.c"
        "book/book_lock_collage.c"
        "book/book_lock_bookmark.c"
        "book/book_source.c"
        "book/book_index_cache.c"
        "book/book_txt.c"
        "book/book_epub.c"
        "book/book_cover.c"
        "book/book_cover_jobs.c"
        "book/book_image_header.c"
        "book/book_cover_auto.c"
        "book/zip_reader.c"
        "book/html_text.c"
        "book/book_layout.c"
        "book/gbk.c"
        "font/ttf_font.c"
        "font/online_font.c"
        "font/online_font_store.c"
        "ui/ui_online_fonts.c"
        "font/app_font_context.c"
        "font/ui_font.c"
        "font/ui_hanzi.c"
        "factory/pmu_selftest.c"
        "factory/vcom_setup.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    REQUIRES ${main_requires}
    INCLUDE_DIRS "." "app" "apps" "font" "ui" "factory" "book"
    EMBED_FILES
        "assets/loading_4bpp.bin"
        "assets/builtin.ttf"
        "assets/ui-hanzi.bin"
        "assets/display-test.png"
)

# 包住 IDF 的 panic 处理器：崩溃现场在重启前落进 RTC，没有串口时那是唯一的现场。
# 链接选项挂在 main 上，INTERFACE 会带到最终链接；写在 project() 之后的全局属性则太晚。
# Wrap the IDF panic handler so the crash scene reaches RTC before a restart; without a console
# that is the only place it survives. The option hangs off main, whose INTERFACE carries it into
# the final link; a global property set after project() is too late for the linker to see it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")

# 本地 USB 测时入口，普通构建不包含。/ Local USB timing probe, omitted from ordinary product builds.
if("$ENV{KIIKO_LOCAL_LATENCY}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_LOCAL_LATENCY=1)
endif()

endif()

if(NOT KIIKO_BOARD_METALIO47)
    option(PICO_MAIN_CLEAN_EXPERIMENT "Local Pico stronger root-switch white erasure" OFF)
    if(PICO_MAIN_CLEAN_EXPERIMENT)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_PICO_MAIN_CLEAN=1)
    endif()
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_UI_LOCALIZATION=1)

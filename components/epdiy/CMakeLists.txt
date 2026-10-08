# 两块板用两套 epdiy，按 PICO_BOARD 二选一：
#   Read Pico   -> src/    本仓库裁剪过的 fork（按列脏裁剪、前导相位跳过等本地扩展）
#   Metalio     -> metalio/ 厂商示例固件的实现，原样使用，只补 4 个我们固件调用而例程没有的函数
# 供应商的这套是给同一颗芯片（ESP32-S31）、同一块面板（ED047TC2 1216x684）写的，
# 之前逐条对齐我们的 fork 反而漏掉了 CKV 上电这类关键步骤。
# Two epdiy trees, chosen by PICO_BOARD. Read Pico keeps this repo's trimmed fork (column
# dirtiness, leading-phase skip and other local additions); the Metalio board uses the vendor
# demo firmware's implementation as-is, plus a shim for the four functions our firmware calls
# and the demo does not define. That demo targets the same chip (ESP32-S31) and the same panel
# (ED047TC2 1216x684); aligning our fork piecemeal is what missed things like the CKV power-up.

if(PICO_BOARD STREQUAL "metalio_eink4_plus")
    set(epdiy_impl "metalio")
else()
    set(epdiy_impl "src")
endif()
message(STATUS "epdiy implementation: ${epdiy_impl}")

if(PICO_BOARD STREQUAL "metalio_eink4_plus")
    # 例程的 .S 是 Xtensa 汇编，在 S31 上编不了；它对应的 RISC-V C 实现是
    # epdiy_riscv_ve_stub.c，已经一并复制过来。
    # The demo's .S files are Xtensa assembly and cannot assemble on S31; their RISC-V C
    # equivalents live in epdiy_riscv_ve_stub.c, which is copied alongside.
    file(GLOB_RECURSE app_sources "${CMAKE_CURRENT_SOURCE_DIR}/metalio/*.c")
    list(APPEND app_sources
         "${CMAKE_CURRENT_SOURCE_DIR}/compat/epdiy_metalio_compat.c"
         # 连续 DU 与翻页引擎在本板上是空实现，见 compat/continuous_du.h 的说明。
         # Continuous DU and the page-turn engine are no-ops here; see compat/continuous_du.h.
         "${CMAKE_CURRENT_SOURCE_DIR}/compat/continuous_du_stub.c")
    # 厂商在 S3 上才需要的那份汇编等价实现，S31 用不到（我们用文件内的 __XTENSA__ 守卫）。
    # The vendor's assembly-equivalent C file is only needed where the .S files emit nothing.
    set(epdiy_include_dirs
        "${CMAKE_CURRENT_SOURCE_DIR}/metalio"
        "${CMAKE_CURRENT_SOURCE_DIR}/metalio/output_common"
        "${CMAKE_CURRENT_SOURCE_DIR}/metalio/output_lcd"
        "${CMAKE_CURRENT_SOURCE_DIR}/metalio/waveforms"
        # 本仓库自己的公开头（epd_lcd.h / epd_waveform.h）。epd_lcd.h 在 Metalio 板上会转发到
        # 例程的 lcd_driver.h，所以两者不会重复定义同名类型。
        # This repo's own public headers (epd_lcd.h / epd_waveform.h). On the Metalio board
        # epd_lcd.h forwards to the demo's lcd_driver.h, so the types are never defined twice.
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/compat")
else()
    set(app_sources "src/epdiy.c"
                    "src/render.c"
                    "src/output_lcd/render_lcd.c"
                    "src/output_lcd/lcd_driver.c"
                    "src/output_common/rmt_compat.c"
                    "src/output_common/lut.c"
                    "src/output_common/lut.S"
                    # 非 Xtensa 目标下补上 lut.S / diff.S 的符号；Xtensa 上这个文件编成空的。
                    # Supplies lut.S / diff.S symbols on non-Xtensa targets; compiles to nothing
                    # on Xtensa.
                    "src/output_common/nonxtensa_ve_fallback.c"
                    "src/output_common/line_queue.c"
                    "src/output_common/render_context.c"
                    "src/font.c"
                    "src/displays.c"
                    "src/diff.S"
                    "src/highlevel.c"
                    "src/board/epd_board.c"
    )
    set(epdiy_include_dirs "${CMAKE_CURRENT_SOURCE_DIR}/src/" "${CMAKE_CURRENT_SOURCE_DIR}/include"
        # compat/ 里的声明两块板共用（读 Pico 上与本 fork 自己的声明重复但完全一致）。
        # compat/ is shared by both boards; on Read Pico its declarations duplicate the fork's own,
        # identically.
        "${CMAKE_CURRENT_SOURCE_DIR}/compat")
endif()

if (${IDF_VERSION_MAJOR} LESS 5)
    message(FATAL_ERROR "epdiy requires ESP-IDF 5.x or newer")
endif()

set(epdiy_requires driver esp_timer esp_adc esp_lcd)
if (${IDF_VERSION_MAJOR} GREATER 5 OR (${IDF_VERSION_MAJOR} EQUAL 5 AND ${IDF_VERSION_MINOR} GREATER 2))
    list(APPEND epdiy_requires esp_driver_i2c)
endif()
if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND epdiy_requires esp_driver_gpio esp_driver_rmt esp_driver_dma)
endif()

set(epdiy_private_requires hal soc esp_hw_support esp_rom)
if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND epdiy_private_requires esp_hal_dma esp_hal_rmt esp_hal_lcd)
    # 例程的 lcd_driver.c 引 hal/lcd_periph.h，那个头又拉 hal/i2s_ll.h，所以 i2s 也得在依赖里。
    # The demo's lcd_driver.c includes hal/lcd_periph.h, which pulls in hal/i2s_ll.h, so the i2s
    # HAL has to be a dependency too.
    list(APPEND epdiy_private_requires esp_hal_i2s)
    # esp_cache_msync 在 esp_mm 里：S31 的回弹缓冲必须做 C2M 同步。
    # esp_cache_msync lives in esp_mm: the S31 bounce buffers need a C2M sync.
    list(APPEND epdiy_private_requires esp_mm)
endif()

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${epdiy_include_dirs}
    REQUIRES ${epdiy_requires}
    PRIV_REQUIRES ${epdiy_private_requires}
)

# formatting specifiers maybe incompatible between idf versions because of different int definitions
component_compile_options(-Wno-error=format= -Wno-format)
# PSRAM 缓存规避补丁是 Xtensa 专有选项，RISC-V 上会直接报 unrecognized option。
# The PSRAM cache-issue workaround is Xtensa-only; RISC-V rejects the flag outright.
if(CONFIG_IDF_TARGET_ESP32S3)
    set_source_files_properties("src/output_common/lut.c" PROPERTIES COMPILE_OPTIONS -mno-fix-esp32-psram-cache-issue)
endif()

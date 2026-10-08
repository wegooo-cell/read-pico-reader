# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# 本地测试沿用当前正式版号；仅正式发布或明确的 OTA 验证指定新版本。
# Local tests keep the current release version; override only for publication or explicit OTA validation.
if(DEFINED ENV{PICO_PROJECT_VER} AND NOT "$ENV{PICO_PROJECT_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{PICO_PROJECT_VER}")
else()
    set(PROJECT_VER "0.3.3-rc86")
endif()
# 目标设备：configure 时用 -DPICO_BOARD=<名字> 指定，默认 read_pico。
# 每块板自带 boards/<名字>/sdkconfig.defaults，追加在根 sdkconfig.defaults 之后，所以板级值优先；
# 并且各自用独立的 sdkconfig.<名字>，否则两块板会互相改写对方的配置。
# Target board, chosen at configure time with -DPICO_BOARD=<name>, defaulting to read_pico. Each
# board carries boards/<name>/sdkconfig.defaults, appended after the root defaults so board values
# win, and gets its own sdkconfig.<name> - a shared one would have the two boards reconfiguring
# each other on every switch.
set(PICO_BOARD "read_pico" CACHE STRING "Target board (see boards/)")
set_property(CACHE PICO_BOARD PROPERTY STRINGS read_pico metalio_eink4_plus)

set(PICO_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}")

# 芯片目标跟着板子走：Read Pico 是 Xtensa 的 esp32s3，Metalio 是 RISC-V 的 esp32s31。
# 光换引脚和 sdkconfig 不够——工具链、PSRAM 时序、epdiy 的输出路径全都不一样。
# 对应地也要用独立的 build 目录（见 tools/build_board.ps1），否则两块板会互相重配。
# The chip target follows the board: Read Pico is esp32s3 (Xtensa), Metalio is esp32s31
# (RISC-V). Changing pins and sdkconfig alone is not enough - the toolchain, PSRAM timing and
# epdiy's output path all differ. It also needs its own build directory (see
# tools/build_board.ps1), or the two boards reconfigure each other.
if(EXISTS "${PICO_BOARD_DIR}/target")
    file(STRINGS "${PICO_BOARD_DIR}/target" PICO_BOARD_TARGET LIMIT_COUNT 1)
    string(STRIP "${PICO_BOARD_TARGET}" PICO_BOARD_TARGET)
    if(PICO_BOARD_TARGET)
        set(IDF_TARGET "${PICO_BOARD_TARGET}" CACHE STRING "IDF target for ${PICO_BOARD}" FORCE)
        message(STATUS "PICO_BOARD target: ${IDF_TARGET}")
    endif()
endif()
if(NOT EXISTS "${PICO_BOARD_DIR}/sdkconfig.defaults")
    file(GLOB PICO_BOARD_CHOICES RELATIVE "${CMAKE_CURRENT_LIST_DIR}/boards" "${CMAKE_CURRENT_LIST_DIR}/boards/*")
    string(REPLACE ";" " " PICO_BOARD_CHOICES "${PICO_BOARD_CHOICES}")
    message(FATAL_ERROR "未知设备 '${PICO_BOARD}'。boards/ 下有：${PICO_BOARD_CHOICES}")
endif()
message(STATUS "PICO_BOARD: ${PICO_BOARD}")

if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
string(REPLACE ";" ";" SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS}")
list(APPEND SDKCONFIG_DEFAULTS "${PICO_BOARD_DIR}/sdkconfig.defaults")
list(REMOVE_DUPLICATES SDKCONFIG_DEFAULTS)
set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${PICO_BOARD}" CACHE STRING "sdkconfig file for this board")

# 让 C 代码能 #if 分支。板级驱动都挂在这个宏上。
# Exposed to C so board-specific code can #if on it.
string(TOUPPER "${PICO_BOARD}" PICO_BOARD_UPPER)
add_compile_definitions(PICO_BOARD_${PICO_BOARD_UPPER}=1)

project(Read_Pico)

# 固件描述时间统一为 UTC，与概览页标识一致；保留已有编译缓存启动器。
# Compile app-description timestamps in UTC to match Overview; preserve any compiler-cache launcher.
idf_component_get_property(app_desc_lib esp_app_format COMPONENT_LIB)
get_target_property(app_desc_launcher ${app_desc_lib} C_COMPILER_LAUNCHER)
if(NOT app_desc_launcher)
    set(app_desc_launcher "")
endif()
set(app_desc_utc_launcher "${CMAKE_COMMAND}" -E env TZ=UTC)
if(app_desc_launcher)
    list(APPEND app_desc_utc_launcher ${app_desc_launcher})
endif()
set_property(TARGET ${app_desc_lib} PROPERTY C_COMPILER_LAUNCHER "${app_desc_utc_launcher}")

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/board.cmake")
set(KIIKO_BOARD "${KIIKO_BOARD}" CACHE STRING "Firmware device: pico or metalio47")
set_property(CACHE KIIKO_BOARD PROPERTY STRINGS pico metalio47)
if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL KIIKO_IDF_TARGET)
    message(FATAL_ERROR "${KIIKO_BOARD} requires ${KIIKO_IDF_TARGET}; use a separate build/config directory")
endif()
if(DEFINED ENV{IDF_TARGET} AND NOT "$ENV{IDF_TARGET}" STREQUAL "${KIIKO_IDF_TARGET}")
    message(FATAL_ERROR "IDF_TARGET environment conflicts with ${KIIKO_BOARD}")
endif()
set(IDF_TARGET "${KIIKO_IDF_TARGET}" CACHE STRING "Selected chip")
set(ENV{KIIKO_BOARD} "${KIIKO_BOARD}")
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${KIIKO_BOARD_ROOT}/sdkconfig.defaults")
endif()
# S31专用本地波形实验；板级与正式构建开关隔离。/ S31-only local waveform experiment, isolated from other boards and release defaults.
option(METALIO_DU14_EXPERIMENT "Local S31 vendor DU14 black/white experiment" OFF)
if(METALIO_DU14_EXPERIMENT AND NOT KIIKO_BOARD_METALIO47)
    message(FATAL_ERROR "DU14 experiment requires Metalio S31")
endif()
add_compile_definitions(KIIKO_BOARD_METALIO47=${KIIKO_BOARD_METALIO47})
# S31 波形回退开关：默认用官方完整48相；置ON可回退到2026-11-01的裁剪37相档做A/B。
# / S31 waveform rollback switch: vendor full 48 phases by default; ON restores the 2026-11-01
#   trimmed 37-phase profile for A/B.
option(METALIO_TRIMMED_WAVEFORM "Local S31 rollback to the trimmed 36/37-phase waveform" OFF)
if(METALIO_TRIMMED_WAVEFORM AND NOT KIIKO_BOARD_METALIO47)
    message(FATAL_ERROR "Trimmed waveform rollback requires Metalio S31")
endif()
if(METALIO_TRIMMED_WAVEFORM)
    add_compile_definitions(E0470_WAVEFORM_TRIM=1)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# 本地测试沿用当前正式版号；仅正式发布或明确的 OTA 验证指定新版本。
# Local tests keep the current release version; override only for publication or explicit OTA validation.
if(DEFINED ENV{KIIKO_PROJECT_VER} AND NOT "$ENV{KIIKO_PROJECT_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{KIIKO_PROJECT_VER}")
elseif(DEFINED ENV{PICO_PROJECT_VER} AND NOT "$ENV{PICO_PROJECT_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{PICO_PROJECT_VER}")
else()
    set(PROJECT_VER "${KIIKO_PROJECT_VER}")
endif()
project(${KIIKO_PROJECT_NAME})

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

# panic 包装器的链接选项在 main/CMakeLists.txt 里挂到 main 组件上：写在 project() 之后
# 用 idf_build_set_property 设全局链接选项已经太晚，链接器看不到 --wrap。
# The panic wrapper's link option hangs off the main component in main/CMakeLists.txt: setting a
# global link option after project() is too late for the linker to see --wrap.

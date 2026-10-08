# Metalio 板用厂商示例固件的 epdiy，其波形布局与本仓库 fork 不同名同构。
# 本组件建立在本仓库那套波形类型上，所以在这一块板上不编任何源文件；
# 上层调用点由 epdiy/compat/ 里的同名空实现接住。
# The Metalio board uses the vendor demo firmware's epdiy, whose waveform layout shares the
# names but not the memory layout of this repo's fork. This component is built on the fork's
# types, so on that board it compiles nothing; epdiy/compat/ carries same-named no-ops for
# the call sites.
if(PICO_BOARD STREQUAL "metalio_eink4_plus")

    idf_component_register(INCLUDE_DIRS "include")
else()
    idf_component_register(
    SRCS "e0470_epaper_waveform.c" "e0470_waveform_trim.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "waveforms"
    REQUIRES epdiy
)
endif()

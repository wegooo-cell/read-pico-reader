include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
if(KIIKO_BOARD_METALIO47)
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "e0470_epaper_waveform.c" "e0470_waveform_trim.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "metalio" "include"
    PRIV_INCLUDE_DIRS "metalio" "waveforms"
    REQUIRES epdiy
)

if(METALIO_DU14_EXPERIMENT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_DU14=1)
endif()
option(METALIO_SETTINGS_SHORT_MOTION "Local S31 settings-motion-only14-phase profile" OFF)
if(METALIO_SETTINGS_SHORT_MOTION)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_S31_SETTINGS_SHORT_MOTION=1)
endif()

else()
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "e0470_epaper_waveform.c" "e0470_waveform_trim.c")
idf_component_register(
    SRCS ${kiiko_selected_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "waveforms"
    REQUIRES epdiy
)

endif()

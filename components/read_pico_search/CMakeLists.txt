include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake")
# 离线搜索无需额外运行时依赖。/ Offline search has no extra runtime dependencies.
kiiko_board_sources(kiiko_selected_sources "${CMAKE_CURRENT_LIST_DIR}" "read_pico_search.c")
idf_component_register(SRCS ${kiiko_selected_sources} INCLUDE_DIRS "include")

# Reuse the search dictionary for infrequent name sorting within Pico's fixed app slot.
if(KIIKO_BOARD STREQUAL "pico")
    target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
endif()

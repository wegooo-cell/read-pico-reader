idf_component_register(
    SRCS "platform/HalStorage.cpp" "platform/WeReadHttpClient.cpp" "weread_service.cpp"
         "weread_notes.cpp"
         "vendor/StreamingJsonParser.cpp" "vendor/WeReadProtocol.cpp" "vendor/WeReadStore.cpp"
         "vendor/WeReadBrowse.cpp" "vendor/WeReadXhtmlCodec.cpp" "vendor/WeReadClient.cpp"
    INCLUDE_DIRS "include" "platform" "vendor"
    REQUIRES esp_http_client mbedtls nvs_flash esp_timer esp_wifi read_pico_transfer cjson
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ENABLE_CHINESE_VERSION=1 ENABLE_SERIAL_LOG=1 BOARD_HAS_PSRAM=1)

file(GLOB_RECURSE BADGE_SOURCES "*.cpp" "*.c")
idf_component_register(SRCS ${BADGE_SOURCES}
  INCLUDE_DIRS "." "../../devices_badge"
  REQUIRES M5GFX lvgl smooth_ui_toolkit mooncake mooncake_log ArduinoJson
    M5IOE1 M5PM1 BMI270_BMM150_Sensor i2c_bus
    esp_timer esp_wifi esp_netif esp_event esp_http_server esp_http_client nvs_flash
    esp_partition spi_flash esp_psram esp_system esp_driver_usb_serial_jtag
    mbedtls littlefs json)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# One release version: ESP-IDF stamps version.txt into the app descriptor, and
# the readiness protocol reports it as the build ID ("v" + version).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../version.txt" BADGE_VERSION LIMIT_COUNT 1)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_RELEASE="v${BADGE_VERSION}")

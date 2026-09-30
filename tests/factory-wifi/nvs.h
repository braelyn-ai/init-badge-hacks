#pragma once
#include <cstddef>
using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr int NVS_READONLY = 0, NVS_READWRITE = 1;
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
esp_err_t nvs_get_str(nvs_handle_t, const char*, char*, size_t*);
esp_err_t nvs_set_str(nvs_handle_t, const char*, const char*);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);

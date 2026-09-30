// Production Wi-Fi storage against a bounded in-memory NVS namespace.
#include "wifi_config.h"
#include "nvs.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {
std::map<std::string, std::string> store;
bool exists = false;
}
esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* handle) {
    assert(!strcmp(name, "badge_wifi"));
    if (mode == NVS_READONLY && !exists) return ESP_FAIL;
    exists = true; *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t, const char* key, char* out, size_t* size) {
    auto found = store.find(key);
    if (found == store.end() || found->second.size() + 1 > *size) return ESP_FAIL;
    memcpy(out, found->second.c_str(), found->second.size() + 1); *size = found->second.size() + 1;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t, const char* key, const char* value) { store[key] = value; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t) { store.clear(); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
void nvs_close(nvs_handle_t) {}

using badge::WifiCredentials;
void expect_event() {
    WifiCredentials loaded{"stale", "stale-password"};
    assert(badge::wifi_credentials_load(loaded));
    assert(loaded.ssid == "init() attendee" && loaded.password.empty());
    assert(!badge::wifi_credentials_custom());
}

int main() {
    assert(badge::wifi_credentials_valid(badge::wifi_event_credentials()));
    expect_event(); // A fresh badge has the compiled event network.

    WifiCredentials other{"Other network", "correct horse"};
    assert(badge::wifi_credentials_save(other));
    WifiCredentials loaded;
    assert(badge::wifi_credentials_load(loaded) && loaded.ssid == other.ssid && loaded.password == other.password);
    assert(badge::wifi_credentials_custom());

    assert(badge::wifi_credentials_forget()); // Forget and Reset badge restore the event network.
    expect_event();
    assert(store.empty());

    assert(badge::wifi_credentials_save(other));
    assert(badge::wifi_credentials_save(badge::wifi_event_credentials()));
    expect_event();
    assert(store.empty()); // Saving the event network clears the override.

    assert(!badge::wifi_credentials_save({"Short", "1234567"}));
    assert(!badge::wifi_credentials_save({"", ""}));
    expect_event();

    store = {{"ssid", "Damaged"}, {"pass", "short"}}; // Invalid records never block the event network.
    expect_event();
    store = {{"ssid", "Partial"}};
    expect_event();
    puts("Wi-Fi storage: built-in event default, runtime-only override, forget/reset restoration, "
         "event save dedupe and damaged-record fallback passed");
}

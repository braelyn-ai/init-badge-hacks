#include "wifi_config.h"
#include <nvs.h>
#include <array>
#include <utility>

namespace badge {
namespace {
constexpr char kNamespace[] = "badge_wifi";

bool printable(const std::string& value) {
  for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return false;
  return true;
}
}

WifiCredentials wifi_event_credentials() { return {kEventWifiSsid, ""}; }

bool wifi_credentials_valid(const WifiCredentials& credentials) {
  return !credentials.ssid.empty() && credentials.ssid.size() <= 32 && printable(credentials.ssid) &&
    (credentials.password.empty() || (credentials.password.size() >= 8 && credentials.password.size() <= 63 && printable(credentials.password)));
}

namespace {
bool load_override(WifiCredentials& credentials) {
  credentials = {};
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  std::array<char, 33> ssid{};
  std::array<char, 64> password{};
  size_t ssid_size = ssid.size(), password_size = password.size();
  const bool ok = nvs_get_str(handle, "ssid", ssid.data(), &ssid_size) == ESP_OK &&
    nvs_get_str(handle, "pass", password.data(), &password_size) == ESP_OK;
  nvs_close(handle);
  if (!ok) return false;
  WifiCredentials loaded{ssid.data(), password.data()};
  if (!wifi_credentials_valid(loaded)) return false;
  credentials = std::move(loaded);
  return true;
}
bool is_event(const WifiCredentials& credentials) {
  return credentials.ssid == kEventWifiSsid && credentials.password.empty();
}
}

bool wifi_credentials_load(WifiCredentials& credentials) {
  if (!load_override(credentials)) credentials = wifi_event_credentials();
  return true;
}

bool wifi_credentials_custom() {
  WifiCredentials saved;
  return load_override(saved) && !is_event(saved);
}

bool wifi_credentials_save(const WifiCredentials& credentials) {
  if (!wifi_credentials_valid(credentials)) return false;
  if (is_event(credentials)) return wifi_credentials_forget();
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool ok = nvs_set_str(handle, "ssid", credentials.ssid.c_str()) == ESP_OK &&
    nvs_set_str(handle, "pass", credentials.password.c_str()) == ESP_OK &&
    nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool wifi_credentials_forget() {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool ok = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}
} // namespace badge

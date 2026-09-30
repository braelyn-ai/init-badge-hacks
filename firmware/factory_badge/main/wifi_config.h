#pragma once
#include <string>

namespace badge {
struct WifiCredentials {
  std::string ssid;
  std::string password;
};

bool wifi_credentials_valid(const WifiCredentials& credentials);
bool wifi_credentials_load(WifiCredentials& credentials);
bool wifi_credentials_save(const WifiCredentials& credentials);
bool wifi_credentials_forget();
} // namespace badge

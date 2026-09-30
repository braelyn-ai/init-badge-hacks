#pragma once
#include <string>

namespace badge {
struct WifiCredentials {
  std::string ssid;
  std::string password;
};

// The confirmed open conference network is compiled in so every badge can
// refresh at the event. Any other network exists only as a runtime NVS
// override; personal SSIDs and passwords never belong in firmware.
inline constexpr char kEventWifiSsid[] = "init() attendee";
WifiCredentials wifi_event_credentials();

bool wifi_credentials_valid(const WifiCredentials& credentials);
// Loads the saved override, falling back to the event network.
bool wifi_credentials_load(WifiCredentials& credentials);
// True only while a valid non-event override is saved.
bool wifi_credentials_custom();
// Saving the event network clears any override instead of duplicating it.
bool wifi_credentials_save(const WifiCredentials& credentials);
// Removes the override; the badge returns to the event network.
bool wifi_credentials_forget();
} // namespace badge

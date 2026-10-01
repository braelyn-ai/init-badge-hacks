#pragma once
#include "clock_types.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace badge {
struct WifiCredentials;
// One social account: an index into badge_social::Networks (or -1) and its handle.
struct Profile { std::string name; int network = -1; std::string handle; std::string company; };
struct ProfileSnapshot {
  Profile profile;
  // Native little-endian RGB565, 160 x 160; immutable across UI frames.
  std::shared_ptr<const std::vector<uint16_t>> avatar;
  uint32_t revision = 0;
  bool ready = false;
  std::string error;
};
enum class ProfileResetState : uint8_t { Idle, Pending, Running, Succeeded, Failed };
struct ProfileResetSnapshot {
  ProfileResetState state = ProfileResetState::Idle;
  std::string error;
};
enum class PortalOutcome : uint8_t { None, Saved, Cancelled, TimedOut, Error };
struct PortalSnapshot {
  bool active = false, starting = false;
  std::string ssid, password, error;
  PortalOutcome outcome = PortalOutcome::None;
  uint8_t clients = 0;
};
enum class WifiFetchState : uint8_t { Idle, Pending, Running, Succeeded, Failed };
struct WifiFetchSnapshot {
  WifiFetchState state = WifiFetchState::Idle;
  int http_status = 0;
  unsigned bytes = 0;
  int photo_network = -1; // badge_social::Networks index when replacing the photo.
  std::string error;      // Short attendee-facing reason for a failed photo.
};
// One explicit, bounded HTTPS read. The service worker owns the entire station
// lifecycle and returns to Wi-Fi off before reporting a terminal state.
// temporary is used only for a local diagnostic; it is never saved to NVS.
bool wifi_fetch_request(const WifiCredentials* temporary = nullptr);
// Queues one avatar.chan.dev photo fetch for a social account; it starts only
// after setup Wi-Fi has stopped, uses the same bounded join/read/disconnect,
// and replaces just the photo after Wi-Fi is off. handle defaults to the saved
// profile's (USB diagnostics may pass a public test handle).
bool photo_fetch_request(int network, const WifiCredentials* temporary = nullptr, const std::string* handle = nullptr);
WifiFetchSnapshot wifi_fetch_snapshot();
// Callback runs on the HTTP task; board/clock implementation must serialize RTC
// access and return success only after RTC, system clock and NVS readback.
using PhoneClockSync = std::function<bool(int64_t, int, ClockSnapshot&, std::string&)>;
bool services_init(PhoneClockSync clock_sync);
ProfileSnapshot profile_snapshot();
// Asynchronous AP lifecycle: inspect starting/active/error in portal_snapshot().
bool portal_start(const char* test_password = nullptr);
void portal_stop();
void portal_tick(); // Compatibility hook; lifecycle is owned by a service task.
PortalSnapshot portal_snapshot();
// Explicitly confirmed reset only. A successful request queues one atomic
// empty-profile write on the existing worker; it does not mean the write passed.
// Inspect the terminal snapshot before resetting main-owned preferences. A failed
// write retains the previous profile and requires another explicit request.
// Setup must be fully stopped; clock, NVS and legacy records are untouched.
bool profile_reset_request(std::string& error);
ProfileResetSnapshot profile_reset_snapshot();
bool profile_clear(); // Explicit diagnostic only; never called by normal startup.
// Destructive batch opt-in only: caller must validate the exact confirmation
// token and fresh nonce. Preserves mounted stores; never invoked implicitly.
bool profile_initialize_for_conference();
} // namespace badge

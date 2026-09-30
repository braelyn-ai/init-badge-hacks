#include "badge_ui.h"
#include "services.h"
#include "conference_settings.h"
#include "schedule_bookmarks.h"
#include "after_dark_unlock.h"
#include "wifi_config.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace badge {
ProfileSnapshot current;
ProfileResetSnapshot result;
bool accept_request = true;
unsigned requests = 0;
ProfileSnapshot profile_snapshot() { return current; }
ProfileResetSnapshot profile_reset_snapshot() { return result; }
bool profile_reset_request(std::string& error) {
    ++requests;
    if (!accept_request) { error = "Synthetic unavailable worker"; return false; }
    error.clear(); result = {ProfileResetState::Pending, {}}; return true;
}
unsigned wifi_forgets = 0;
bool wifi_credentials_forget() { ++wifi_forgets; return true; }
}
namespace board {
int applied_brightness = -1;
unsigned brightness_calls = 0;
void setBrightness(uint8_t value) { applied_brightness = value; ++brightness_calls; }
}
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0;
std::map<std::string, uint32_t> durable;
std::vector<std::string> writes;
unsigned operation = 0, fail_operation = 0;
int set_value(const char* key, uint32_t value) {
    writes.emplace_back(key);
    // The pinned IDF writes individual entries before nvs_commit(). Model that
    // boundary faithfully; a later failing key cannot undo earlier saved keys.
    if (++operation == fail_operation) return -1;
    durable[key] = value;
    return ESP_OK;
}
int nvs_set_u32(nvs_handle_t, const char* key, uint32_t value) { return set_value(key, value); }
int nvs_set_u8(nvs_handle_t, const char* key, uint8_t value) { return set_value(key, value); }
int nvs_commit(nvs_handle_t) {
    writes.emplace_back("commit");
    return ++operation == fail_operation ? -1 : ESP_OK;
}
ConferenceSettings settings;
badge_schedule::Bookmarks bookmarks;
badge_after_dark::Unlock afterDark;
badge::UiModel model;
nvs_handle_t preferences = 1;
bool preferencesReady = true, rotationPending = false, setupRequested = false;
bool resetRequested = false, resetNeedsPreferences = false;
uint32_t resetProfileRevision = 0, preferenceWrites = 0, networkSaveAt = 0;
unsigned refreshes = 0;
void refreshModel() { ++refreshes; }

// PRODUCTION_RESET_COORDINATOR

void baseline(bool dirty = false) {
    settings.restore(0xc701021e); // 30%, fixed 180 degrees.
    bookmarks.restore(badge_schedule::Bookmarks::Version | 0x105);
    afterDark.restore(badge_after_dark::Unlock::SavedUnlocked);
    model = {}; model.selected_network = 2;
    badge::current = {}; badge::current.ready = true; badge::current.revision = 9;
    badge::current.profile.name = "Synthetic attendee";
    badge::current.profile.company = "Synthetic company";
    badge::current.profile.urls[0] = "https://github.com/example";
    badge::result = {}; badge::accept_request = true; badge::requests = 0;
    preferencesReady = true; rotationPending = setupRequested = false;
    resetRequested = resetNeedsPreferences = false;
    resetProfileRevision = preferenceWrites = networkSaveAt = 0;
    refreshes = operation = fail_operation = 0;
    board::applied_brightness = -1; board::brightness_calls = 0;
    writes.clear(); durable = {{"prefs", settings.encoded()}, {"network", 2},
        {"agenda_saved", bookmarks.encoded()}, {"after_dark_v1", afterDark.encoded()},
        {"clock-offset", 1234}, {"legacy-auth", 5678}};
    if (dirty) {
        settings.setBrightness(40, 100);
        bookmarks.toggle(1, 100);
        networkSaveAt = 1300;
    }
}
void worker_succeeds() {
    badge::current.profile = {}; badge::current.avatar.reset(); ++badge::current.revision;
    badge::result = {badge::ProfileResetState::Succeeded, {}};
}
void assert_unrelated_preserved() {
    assert(durable.at("clock-offset") == 1234 && durable.at("legacy-auth") == 5678);
}
void assert_defaults() {
    ConferenceSettings defaults;
    badge_schedule::Bookmarks empty;
    assert(settings.encoded() == defaults.encoded() && !settings.pending());
    assert(settings.orientation == ConferenceOrientationMode::Default && settings.fixedRotation() == 0);
    assert(bookmarks.mask() == 0 && !bookmarks.pending());
    assert(durable.at("prefs") == defaults.encoded());
    assert(durable.at("network") == 0 && durable.at("agenda_saved") == empty.encoded());
    assert(model.selected_network == 0 && networkSaveAt == 0);
    assert(badge::wifi_forgets > 0);
    assert(board::applied_brightness == 60 && rotationPending);
    assert(model.reset_state == badge::ResetState::Complete && !resetRequested && !resetNeedsPreferences);
    assert(!afterDark.unlocked() && !afterDark.pending() && !afterDark.saveDue(10000));
    assert(durable.at("after_dark_v1") == badge_after_dark::Unlock::SavedLocked);
    assert_unrelated_preserved();
}
int main() {
    baseline(true);
    requestReset();
    assert(resetRequested && badge::requests == 1 && model.reset_state == badge::ResetState::Working);
    requestReset(); assert(badge::requests == 1); // Duplicate contact cannot dispatch twice.
    pollReset(); persist(2000); assert(writes.empty());
    badge::result.state = badge::ProfileResetState::Running;
    pollReset(); persist(2000); assert(writes.empty());
    worker_succeeds(); pollReset();
    assert_defaults();
    assert(writes == std::vector<std::string>({"prefs", "network", "agenda_saved", "after_dark_v1", "commit"}));
    const auto saved_writes = writes.size(); pollReset(); persist(4000);
    assert(writes.size() == saved_writes && badge::requests == 1);

    // A successful reset also cancels an unlock awaiting its delayed NVS retry.
    // It must not reappear when the old save deadline passes.
    baseline(true);
    afterDark.restore(badge_after_dark::Unlock::SavedLocked);
    durable["after_dark_v1"] = badge_after_dark::Unlock::SavedLocked;
    assert(afterDark.unlock(100)); afterDark.saveFailed(100);
    assert(afterDark.pending() && afterDark.saveDue(5100));
    requestReset(); persist(6000); assert(writes.empty());
    worker_succeeds(); pollReset(); assert_defaults();
    const auto reset_writes = writes.size(); persist(10000);
    assert(writes.size() == reset_writes);
    assert_defaults();

    // A rejected request or worker failure cannot alter preferences. Previously
    // pending user preferences resume their ordinary save path after failure.
    baseline(true); badge::accept_request = false; requestReset();
    assert(!resetRequested && model.reset_state == badge::ResetState::Failed && writes.empty());
    baseline(true); requestReset(); badge::result = {badge::ProfileResetState::Failed, "Synthetic failure"}; pollReset();
    assert(!resetRequested && !resetNeedsPreferences && model.reset_state == badge::ResetState::Failed);
    assert(settings.brightness == 40 && bookmarks.mask() == 0x107 && model.selected_network == 2);
    assert(badge::current.profile.name == "Synthetic attendee" && writes.empty());
    persist(2000);
    assert(!settings.pending() && !bookmarks.pending() && !networkSaveAt);
    assert(durable.at("prefs") == settings.encoded() && durable.at("agenda_saved") == bookmarks.encoded());
    assert(afterDark.unlocked() && !afterDark.pending());
    assert(durable.at("after_dark_v1") == badge_after_dark::Unlock::SavedUnlocked);
    assert_unrelated_preserved();
    baseline(); setupRequested = true; requestReset(); assert(!resetRequested && badge::requests == 0);

    for (unsigned failed = 1; failed <= 5; ++failed) {
        baseline(); const auto old_settings = settings.encoded(); const auto old_marks = bookmarks.mask();
        requestReset(); worker_succeeds(); fail_operation = failed; pollReset();
        assert(!resetRequested && resetNeedsPreferences && model.reset_state == badge::ResetState::SettingsFailed);
        assert(badge::current.profile.name.empty() && badge::current.revision == resetProfileRevision);
        assert(settings.encoded() == old_settings && bookmarks.mask() == old_marks && model.selected_network == 2);
        assert(board::brightness_calls == 0 && !rotationPending && !preferenceWrites);
        assert(writes.size() == failed); // Short-circuit after the failed operation.
        assert(afterDark.unlocked() && !afterDark.pending());
        assert(durable.at("after_dark_v1") == (failed == 5
            ? badge_after_dark::Unlock::SavedLocked : badge_after_dark::Unlock::SavedUnlocked));
        assert_unrelated_preserved();
        // No auto-retry or replay of the destructive worker request.
        pollReset(); persist(2000); assert(writes.size() == failed && badge::requests == 1);
        fail_operation = 0; writes.clear(); requestReset();
        assert(resetRequested && badge::requests == 1); // Retry only incomplete preferences.
        pollReset(); assert_defaults(); assert(badge::requests == 1);
    }
    baseline(); requestReset(); worker_succeeds(); preferencesReady = false; pollReset();
    assert(model.reset_state == badge::ResetState::SettingsFailed && writes.empty());
    preferencesReady = true; requestReset(); pollReset(); assert_defaults();

    // Configuring a new profile after a partial reset invalidates the narrow
    // preference-only retry. A new confirmed attempt must clear that revision.
    baseline(); requestReset(); worker_succeeds(); fail_operation = 2; pollReset();
    const auto failed_revision = resetProfileRevision;
    badge::current.profile.name = "New attendee"; ++badge::current.revision;
    fail_operation = 0; writes.clear(); requestReset();
    assert(resetRequested && !resetNeedsPreferences && badge::requests == 2);
    assert(badge::current.revision != failed_revision);
    pollReset(); assert(writes.empty());
    worker_succeeds(); pollReset(); assert_defaults();
    std::puts("Reset coordinator: worker gating, partial individual NVS writes/commit failures, explicit retry, new profile guard, defaults, unlock reset, pending unlock cancellation and preserved clock/legacy state passed");
}

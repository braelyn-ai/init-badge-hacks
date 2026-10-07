#include "board.h"
#include "touch_mapping.h"
#include "badge_ui.h"
#include "clock_service.h"
#include "services.h"
#include "wifi_config.h"
#include "social_networks.h"
#include "conference_settings.h"
#include "schedule.h"
#include "schedule_bookmarks.h"
#include "orientation_filter.h"
#include "button_gesture.h"
#include "after_dark_unlock.h"
#include "touch_observation.h"
#include <ArduinoJson.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_flash.h>
#include <esp_psram.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <array>

namespace {
constexpr char Build[] = BADGE_RELEASE; // From ../version.txt, e.g. "v1.0.0".
ConferenceSettings settings;
OrientationFilter orientation;
BadgeButtonGesture buttons;
badge_after_dark::Unlock afterDark;
badge_touch::Observation touchObservation;
uint32_t observedTouchSequence = 0;
badge_schedule::Bookmarks bookmarks;
badge::UiModel model;
badge::ProfileSnapshot profile;
nvs_handle_t preferences;
bool preferencesReady = false, rotationPending = false;
bool setupRequested = false;
bool resetRequested = false, resetNeedsPreferences = false;
uint32_t resetProfileRevision = 0;
uint32_t preferenceWrites = 0, lastImu = 0, lastModel = 0;
uint32_t loopCount = 0, inputCount = 0, maxLoopGap = 0, lastLoop = 0;
std::string serialLine;
bool serialOverflow = false;

bool writeBytes(const void* bytes, size_t count, uint32_t timeoutMs = 1000) {
    const auto* data = static_cast<const uint8_t*>(bytes);
    uint32_t started = board::millis();
    while (count && uint32_t(board::millis() - started) < timeoutMs) {
        int sent = usb_serial_jtag_write_bytes(data, count, pdMS_TO_TICKS(20));
        if (sent > 0) { data += sent; count -= sent; }
    }
    return count == 0;
}
void line(const char* text) { writeBytes(text, strlen(text)); writeBytes("\n", 1); }
void reply(const char* prefix, const JsonDocument& json) {
    std::string body;
    serializeJson(json, body);
    writeBytes(prefix, strlen(prefix)); writeBytes(body.data(), body.size()); writeBytes("\n", 1);
}
void startSetup(const char* password = nullptr) {
    if (setupRequested || badge::ui_touch_test_active() || badge::ui_reset_active()) return;
    if (badge::portal_start(password)) {
        setupRequested = true;
        badge::ui_show_setup("", "", "192.168.4.1", "Starting setup...");
    }
}
void closeSetup() { badge::portal_stop(); }
void applyButton(BadgeButtonAction action) {
    if (action == BadgeButtonAction::NONE) return;
    ++inputCount;
    bool both = action == BadgeButtonAction::SETTINGS;
    int delta = action == BadgeButtonAction::BLUE ? 1 : -1;
    if (board::rotation() == 2) delta = -delta;
    badge::ui_button(both, delta);
}
touch_mapping::Session calibration;
bool calibrationRequested = false;
uint32_t calibrationSequence = 0;
void refreshModel() {
    model.touch_calibrated = touch_mapping::available();
    const auto stage = calibration.stage();
    model.calibration_target_visible = stage == touch_mapping::Session::Stage::Fit || stage == touch_mapping::Session::Stage::Verify;
    auto target = calibration.target();
    model.calibration_x = int(target.x); model.calibration_y = int(target.y);
    model.calibration_title = stage == touch_mapping::Session::Stage::Fit ? "Calibrate " + std::to_string(calibration.index()+1) + " / 9" : stage == touch_mapping::Session::Stage::Verify ? "Check " + std::to_string(calibration.index()+1) + " / 5" : "Touch calibration";
    model.calibration_message = calibration.message();
    auto previousAvatar = profile.avatar; // Keep previous image alive through widget update.
    profile = badge::profile_snapshot();
    model.name = profile.profile.name;
    model.company = profile.profile.company;
    model.schedule_bookmarks = bookmarks.mask();
    model.social_network = profile.profile.network;
    model.social_url = badge_social::url(profile.profile.network, profile.profile.handle);
    model.social_label = badge_social::photo_source(profile.profile.network, profile.profile.handle).label;
    model.avatar = profile.avatar && !profile.avatar->empty() ? profile.avatar->data() : nullptr;
    model.avatar_width = model.avatar_height = model.avatar ? 160 : 0;
    model.profile_revision = profile.revision;
    auto power = board::battery();
    model.battery_percent = power.valid ? power.percent : -1;
    model.brightness_percent = settings.brightness;
    model.orientation = static_cast<badge::Orientation>(settings.orientation);
    model.settings_pending = settings.pending() || bookmarks.pending();
    {
        // Progress, then the outcome for a few seconds, on every page.
        static badge::WifiFetchState shown = badge::WifiFetchState::Idle;
        static uint32_t finishedAt = 0;
        const auto fetch = badge::wifi_fetch_snapshot();
        const bool terminal = fetch.state == badge::WifiFetchState::Succeeded || fetch.state == badge::WifiFetchState::Failed;
        if (terminal && shown != fetch.state) finishedAt = board::millis();
        shown = fetch.state;
        model.photo_status.clear();
        if (fetch.photo_network >= 0) {
            if (!terminal && fetch.state != badge::WifiFetchState::Idle) model.photo_status = "Getting your photo...";
            else if (terminal && uint32_t(board::millis() - finishedAt) < 6000)
                model.photo_status = fetch.state == badge::WifiFetchState::Succeeded ? "Photo updated" : fetch.error;
        }
    }
    model.clock_text = badge_clock::timeText();
    model.date_text = badge_clock::dateText();
    model.clock_valid = badge_clock::valid();
    model.clock_epoch = badge_clock::epoch();
    model.utc_offset_minutes = badge_clock::offset();
    model.schedule_minute = badge_schedule::localMinute(
        badge_clock::epoch(), badge_clock::offset(), model.clock_valid);
    model.schedule_current = badge_schedule::current(model.schedule_minute);
    afterDark.updateClock(badge_clock::epoch(), badge_clock::offset(), model.clock_valid, board::millis());
    model.after_dark_unlocked = afterDark.unlocked();
    const auto radar = badge::radar_snapshot();
    model.radar_scanning = radar.scanning;
    if (model.radar_revision != radar.revision) {
        model.radar_revision = radar.revision;
        model.radar.clear();
        for (const auto& heard : radar.contacts) {
            badge::RadarContact contact;
            std::copy(std::begin(heard.bssid), std::end(heard.bssid), contact.bssid.begin());
            contact.rssi = heard.rssi;
            model.radar.push_back(contact);
        }
    }
    badge::ui_update(model);
}
void status(const char* nonce) {
    auto saved = badge::profile_snapshot();
    auto portal = badge::portal_snapshot();
    uint32_t flashSize = 0;
    esp_flash_get_size(nullptr, &flashSize);
    wifi_mode_t wifi = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifi);
    // Bit per network index; at most one is ever set.
    const int mask = saved.profile.network >= 0 ? 1 << saved.profile.network : 0;
    JsonDocument data;
    data["build"] = Build; data["framework"] = "ESP-IDF/LVGL/Smooth/Mooncake";
    data["page_count"] = badge::ui_page_count(); data["page"] = badge::ui_page_index();
    data["after_dark_unlocked"] = afterDark.unlocked();
    data["after_dark_save_pending"] = afterDark.pending();
    data["vibration_available"] = board::inputVibrationAvailable();
    data["vibration_active"] = board::inputVibrationActive();
    data["brightness_percent"] = settings.brightness;
    data["orientation_mode"] = settings.orientationName();
    data["rotation"] = board::rotation(); data["preferences_pending"] = settings.pending();
    data["preference_writes"] = preferenceWrites;
    data["social_network"] = saved.profile.network >= 0 ? badge_social::Networks[saved.profile.network].key : "";
    data["configured_mask"] = mask; data["avatar"] = bool(saved.avatar);
    data["company_present"] = !saved.profile.company.empty();
    data["schedule_bookmarks"] = bookmarks.mask();
    data["bookmarks_pending"] = bookmarks.pending();
    data["design"] = "init-2026";
    data["reset_active"] = badge::ui_reset_active();
    data["reset_state"] = int(model.reset_state);
    data["name_present"] = !saved.profile.name.empty(); data["store_ready"] = saved.ready;
    data["setup"] = setupRequested || portal.active || portal.starting;
    data["wifi_mode"] = int(wifi); data["wifi_custom"] = badge::wifi_credentials_custom();
    auto fetch = badge::wifi_fetch_snapshot();
    data["wifi_fetch_state"] = int(fetch.state); data["wifi_fetch_http"] = fetch.http_status;
    data["wifi_fetch_bytes"] = fetch.bytes;
    data["photo_network"] = fetch.photo_network; data["photo_error"] = fetch.error;
    data["bluetooth"] = 0; data["ap_clients"] = portal.clients;
    data["touch_calibration_version"] = touch_mapping::version();
    data["calibration_active"] = badge::ui_calibration_active();
    data["calibration_message"] = calibration.message();
    data["calibration_step"] = calibration.index();
    data["clock_valid"] = badge_clock::valid(); data["rtc"] = board::rtcAvailable();
    data["flash_bytes"] = flashSize; data["psram_bytes"] = esp_psram_get_size();
    data["board"] = 30; data["display_width"] = board::width(); data["display_height"] = board::height();
    data["schedule_current"] = model.schedule_current;
    data["schedule_minute"] = model.schedule_minute;
    data["ui_ticks"] = loopCount; data["inputs"] = inputCount; data["max_loop_gap_ms"] = maxLoopGap;
    if (badge_clock::validNonce(nonce)) data["nonce"] = nonce;
    reply("CONFERENCE_STATUS ", data);
}
void touchStatus(const char* nonce) {
    auto touch = badge::ui_touch_state();
    JsonDocument data;
    data["active"] = badge::ui_touch_test_active(); data["pressed"] = touch.pressed;
    data["sample"] = touch.sample; data["sensor"] = touch.sensor;
    data["x"] = touch.x; data["y"] = touch.y;
    data["raw_x"] = touch.raw_x; data["raw_y"] = touch.raw_y;
    data["rotation"] = board::rotation(); data["scale_trial"] = false;
    data["touch_model"] = touch_mapping::available() ? "esptember-calibrated" : "factory-native";
    data["calibration_version"] = touch_mapping::version();
    if (badge_clock::validNonce(nonce)) data["nonce"] = nonce;
    reply("TOUCH_TEST_STATUS ", data);
}
void observeTaps(JsonDocument& command) {
    const char* nonce = command["nonce"] | "";
    const char* action = command["action"] | "read";
    if (!badge_clock::validNonce(nonce)) { line("COMMAND_REJECTED"); return; }
    const uint32_t now = board::millis();
    if (!strcmp(action, "start")) {
        if (!command["duration_ms"].is<uint32_t>() || command["duration_ms"].as<uint32_t>() < 1000 ||
            command["duration_ms"].as<uint32_t>() > badge_touch::Observation::MaxDurationMs) {
            line("COMMAND_REJECTED"); return;
        }
        touchObservation.start(now, command["duration_ms"].as<uint32_t>());
        observedTouchSequence = board::touch().sequence;
    } else if (!strcmp(action, "stop")) touchObservation.stop(now);
    else if (strcmp(action, "read")) { line("COMMAND_REJECTED"); return; }
    JsonDocument result;
    result["active"] = touchObservation.active(now);
    result["dropped"] = touchObservation.dropped();
    result["page"] = badge::ui_page_index(); result["current_rotation"] = board::rotation();
    result["after_dark_unlocked"] = afterDark.unlocked();
    result["after_dark_save_pending"] = afterDark.pending();
    result["nonce"] = nonce;
    auto records = result["contacts"].to<JsonArray>();
    badge_touch::Observation::Contact contact;
    // Drain bounded batches only on request. No serial I/O occurs on touch edges.
    for (int i = 0; i < 8 && touchObservation.pop(contact); ++i) {
        auto row = records.add<JsonObject>();
        row["press_ms"] = contact.press_ms; row["release_ms"] = contact.release_ms;
        row["duration_ms"] = contact.duration_ms;
        if (contact.has_previous) row["gap_before_ms"] = contact.gap_before_ms;
        row["start_x"] = contact.start_x; row["start_y"] = contact.start_y;
        row["end_x"] = contact.end_x; row["end_y"] = contact.end_y;
        row["max_dx"] = contact.max_dx; row["max_dy"] = contact.max_dy;
        row["end"] = contact.reason();
    }
    result["remaining"] = touchObservation.count();
    reply("TOUCH_OBSERVATION ", result);
}
void clockCommand(JsonDocument& command) {
    const char* op = command["op"] | "";
    const char* nonce = command["nonce"] | "";
    std::string error;
    if (!badge_clock::validNonce(nonce)) error = "invalid_nonce";
    else if (strcmp(op, "clock_set") == 0) {
        if (!command["epoch"].is<int64_t>() || !command["offset_minutes"].is<int>()) error = "invalid_value";
        else badge_clock::set(command["epoch"].as<int64_t>(), command["offset_minutes"].as<int>(), "computer", error);
    }
    int64_t rtc = 0;
    bool validRtc = board::readRtcUtc(rtc);
    bool valid = badge_clock::valid() && validRtc;
    if (valid && llabs(badge_clock::epoch() - rtc) > 1) { valid = false; error = "clock_verify_failed"; }
    if (error.empty() && !valid) error = "clock_unset";
    JsonDocument data;
    data["protocol"] = 1; data["ok"] = error.empty() && valid; data["valid"] = valid;
    data["source"] = badge_clock::source(); data["epoch"] = badge_clock::epoch();
    data["rtc_epoch"] = validRtc ? rtc : 0; data["offset_minutes"] = badge_clock::offset();
    if (!error.empty()) data["error"] = error;
    if (badge_clock::validNonce(nonce)) data["nonce"] = nonce;
    reply(strcmp(op, "clock_set") == 0 ? "CLOCK_ACK " : "CLOCK_STATUS ", data);
}
void capture() {
    if (setupRequested || badge::ui_setup_active()) { line("CAPTURE_REJECTED"); return; }
    refreshModel(); badge::ui_tick(board::millis());
    lv_refr_now(board::display());
    char header[64]; snprintf(header, sizeof(header), "BADGE_CAPTURE %d %d", board::width(), board::height());
    line(header);
    std::array<uint8_t, 468 * 3> row;
    uint32_t start = board::millis();
    for (int y = 0; y < board::height(); ++y) {
        size_t bytes = size_t(board::width()) * 3;
        if (board::millis() - start > 5000 || !board::readFrameRow(y, row.data(), bytes) || !writeBytes(row.data(), bytes, 200)) {
            line("\nBADGE_CAPTURE_ABORTED"); return;
        }
    }
    line("\nBADGE_CAPTURE_END");
}
void command(JsonDocument& data) {
    const char* op = data["op"] | "";
    const char* nonce = data["nonce"] | "";
    if (!strcmp(op, "clock_set") || !strcmp(op, "clock_status")) clockCommand(data);
    else if (!strcmp(op, "status")) { if (data["reset_metrics"] | false) maxLoopGap = 0; status(nonce); }
    else if (!strcmp(op, "wifi_config") && badge_clock::validNonce(nonce)) {
        // A local USB diagnostic for provisioning/test. Never return a password.
        const char* ssid = data["ssid"] | ""; const char* password = data["password"] | "";
        badge::WifiCredentials credentials{ssid, password};
        const bool ok = !setupRequested && !badge::ui_reset_active() &&
            badge::wifi_credentials_save(credentials);
        JsonDocument result; result["ok"] = ok; result["nonce"] = nonce;
        reply("WIFI_CONFIG_ACK ", result);
    }
    else if (!strcmp(op, "wifi_fetch") && badge_clock::validNonce(nonce)) {
        badge::WifiCredentials temporary;
        const bool provided = data["ssid"].is<const char*>() && data["password"].is<const char*>();
        if (provided) {
            temporary.ssid = data["ssid"].as<std::string>();
            temporary.password = data["password"].as<std::string>();
        }
        const bool ok = !setupRequested && badge::wifi_fetch_request(provided ? &temporary : nullptr);
        std::fill(temporary.password.begin(), temporary.password.end(), '\0');
        JsonDocument result; result["ok"] = ok; result["nonce"] = nonce;
        reply("WIFI_FETCH_ACK ", result);
    }
    else if (!strcmp(op, "photo_fetch") && badge_clock::validNonce(nonce)) {
        // USB diagnostic: optional public test handle and RAM-only test Wi-Fi.
        badge::WifiCredentials temporary;
        const bool provided = data["ssid"].is<const char*>() && data["password"].is<const char*>();
        if (provided) {
            temporary.ssid = data["ssid"].as<std::string>();
            temporary.password = data["password"].as<std::string>();
        }
        const bool named = data["handle"].is<const char*>();
        const std::string handle = named ? data["handle"].as<std::string>() : std::string();
        const int network = data["network"].is<const char*>() ? badge_social::find(data["network"].as<std::string>()) : (data["network"] | -1);
        const bool ok = !setupRequested && badge::photo_fetch_request(network,
            provided ? &temporary : nullptr, named ? &handle : nullptr);
        std::fill(temporary.password.begin(), temporary.password.end(), '\0');
        JsonDocument result; result["ok"] = ok; result["nonce"] = nonce;
        reply("PHOTO_FETCH_ACK ", result);
    }
    else if (!strcmp(op, "touch_test_status")) touchStatus(nonce);
    else if (!strcmp(op, "calibrate_touch")) {
        if (!badge_clock::validNonce(nonce) || setupRequested || badge::ui_setup_active() || badge::ui_touch_test_active() || badge::ui_reset_active()) { line("COMMAND_REJECTED"); return; }
        calibrationRequested = true; status(nonce);
    }
    else if (!strcmp(op, "observe_taps")) observeTaps(data);
    else if (!strcmp(op, "page") && data["step"].is<int>() && abs(data["step"].as<int>()) == 1) {
        badge::ui_page(data["step"].as<int>()); status(nonce);
    } else if (!strcmp(op, "button")) {
        const char* value = data["value"] | "";
        if (!strcmp(value, "blue")) applyButton(BadgeButtonAction::BLUE);
        else if (!strcmp(value, "yellow")) applyButton(BadgeButtonAction::YELLOW);
        else if (!strcmp(value, "both")) applyButton(BadgeButtonAction::SETTINGS);
        else { line("COMMAND_REJECTED"); return; }
        status(nonce);
    } else if (!strcmp(op, "touch")) {
        const char* phase = data["phase"] | "";
        int x = data["x"] | -1, y = data["y"] | -1;
        if (x < 0 || y < 0 || x >= board::width() || y >= board::height() ||
            (strcmp(phase, "begin") && strcmp(phase, "move") && strcmp(phase, "end"))) { line("COMMAND_REJECTED"); return; }
        bool simulatedContact = strcmp(phase, "end") != 0;
        if (!board::injectTouch(x, y, simulatedContact)) { line("COMMAND_REJECTED"); return; }
        lv_indev_read(board::pointer());
        if (badge::ui_touch_test_active()) badge::ui_touch_sample(x, y, x, y, simulatedContact, board::rotation(), false);
        status(nonce);
    } else if (!strcmp(op, "setup_test")) {
        const char* password = data["password"] | "";
        size_t size = strlen(password);
        bool accepted = size >= 12 && size <= 32;
        for (size_t i = 0; i < size; ++i) if (!isalnum(static_cast<unsigned char>(password[i]))) accepted = false;
        if (!accepted || setupRequested || badge::ui_touch_test_active() || badge::ui_reset_active()) { line("COMMAND_REJECTED"); return; }
        startSetup(password);
        line("CONFERENCE_SETUP {\"starting\":true}");
    } else if (!strcmp(op, "portal_status")) {
        auto portal = badge::portal_snapshot(); JsonDocument result;
        result["active"] = portal.active; result["starting"] = portal.starting;
        result["clients"] = portal.clients; result["outcome"] = int(portal.outcome);
        if (!portal.error.empty()) result["error"] = portal.error;
        reply("PORTAL_STATUS ", result);
    } else if (!strcmp(op, "initialize_conference_storage")) {
        bool confirmed = !strcmp(data["confirm"] | "", "ERASE_FFAT_FOR_CONFERENCE");
        bool success = confirmed && badge_clock::validNonce(nonce) && !setupRequested && badge::profile_initialize_for_conference();
        JsonDocument result; result["ok"] = success; result["store_ready"] = badge::profile_snapshot().ready;
        if (badge_clock::validNonce(nonce)) result["nonce"] = nonce;
        reply("STORAGE_ACK ", result);
    } else if (!strcmp(op, "clear_manual_profile") && data["confirm"].is<bool>() && data["confirm"].as<bool>()) {
        if (setupRequested) { line("COMMAND_REJECTED"); return; }
        JsonDocument result; result["success"] = badge::profile_clear(); reply("CONFERENCE_CLEAR ", result);
    } else if (!strcmp(op, "after_dark_reset")) {
        // Narrow USB test reset; unlike Reset badge, this leaves the profile
        // and all other saved preferences intact.
        std::string error;
        badge_after_dark::Unlock locked;
        if (!data["confirm"].is<bool>() || !data["confirm"].as<bool>() || !badge_clock::validNonce(nonce))
            error = "confirmation_required";
        else if (setupRequested || resetRequested || badge::ui_setup_active() ||
                 badge::ui_touch_test_active() || badge::ui_reset_active()) error = "busy";
        else if (locked.updateClock(badge_clock::epoch(), badge_clock::offset(), badge_clock::valid(), board::millis()))
            error = "timed_reveal_active";
        else if (!preferencesReady ||
                 nvs_set_u8(preferences, "after_dark_v1", badge_after_dark::Unlock::SavedLocked) != ESP_OK ||
                 nvs_commit(preferences) != ESP_OK) error = "storage_write_failed";
        if (error.empty()) {
            afterDark.restore(badge_after_dark::Unlock::SavedLocked);
            ++preferenceWrites;
            refreshModel();
        }
        JsonDocument result;
        result["ok"] = error.empty(); result["after_dark_unlocked"] = afterDark.unlocked();
        if (!error.empty()) result["error"] = error;
        if (badge_clock::validNonce(nonce)) result["nonce"] = nonce;
        reply("AFTER_DARK_RESET ", result);
    } else if (!strcmp(op, "capture_badge") || !strcmp(op, "capture")) capture();
    else if (!strcmp(op, "reboot")) { line("CONFERENCE_REBOOT"); vTaskDelay(pdMS_TO_TICKS(50)); esp_restart(); }
    else line("COMMAND_REJECTED");
}
void pollCommands() {
    uint8_t bytes[256];
    int count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
    for (int i = 0; i < count; ++i) {
        char c = bytes[i];
        if (c == '\r') continue;
        if (c != '\n') {
            if (!serialOverflow && serialLine.size() < 1024) serialLine += c;
            else serialOverflow = true;
            continue;
        }
        JsonDocument data;
        if (serialOverflow || deserializeJson(data, serialLine)) line("COMMAND_REJECTED");
        else command(data);
        serialLine.clear(); serialOverflow = false;
    }
}
void persist(uint32_t now) {
    // A reset writes its own defaults after the profile worker succeeds.
    // Do not interleave an older debounced preference write with that commit.
    if (resetRequested) return;
    if (bookmarks.saveDue(now)) {
        if (preferencesReady && nvs_set_u32(preferences, "agenda_saved", bookmarks.encoded()) == ESP_OK && nvs_commit(preferences) == ESP_OK) {
            ++preferenceWrites; bookmarks.saved();
        } else bookmarks.saveFailed(now);
    }
    if (afterDark.saveDue(now)) {
        if (preferencesReady && nvs_set_u8(preferences, "after_dark_v1", afterDark.encoded()) == ESP_OK && nvs_commit(preferences) == ESP_OK) {
            ++preferenceWrites; afterDark.saved();
        } else afterDark.saveFailed(now);
    }
    if (settings.saveDue(now)) {
        if (preferencesReady && nvs_set_u32(preferences, "prefs", settings.encoded()) == ESP_OK && nvs_commit(preferences) == ESP_OK) {
            ++preferenceWrites; settings.saved();
        } else settings.saveFailed(now);
    }
}
void requestReset() {
    if (resetRequested || setupRequested) return;
    model.reset_state = badge::ResetState::Working;
    // Retry only the unfinished settings step while the cleared profile is
    // unchanged. A newly configured profile requires a new confirmed reset.
    if (resetNeedsPreferences && badge::profile_snapshot().revision == resetProfileRevision) {
        resetRequested = true;
    } else {
        resetNeedsPreferences = false;
        std::string error;
        resetRequested = badge::profile_reset_request(error);
        if (!resetRequested) model.reset_state = badge::ResetState::Failed;
    }
    refreshModel();
}
void pollReset() {
    if (!resetRequested) return;
    if (!resetNeedsPreferences) {
        auto result = badge::profile_reset_snapshot();
        if (result.state == badge::ProfileResetState::Pending || result.state == badge::ProfileResetState::Running) return;
        if (result.state != badge::ProfileResetState::Succeeded) {
            resetRequested = false;
            model.reset_state = badge::ResetState::Failed;
            refreshModel();
            return;
        }
        resetNeedsPreferences = true;
        resetProfileRevision = badge::profile_snapshot().revision;
    }
    ConferenceSettings defaults;
    badge_schedule::Bookmarks emptyBookmarks;
    const bool saved = preferencesReady &&
        nvs_set_u32(preferences, "prefs", defaults.encoded()) == ESP_OK &&
        nvs_set_u32(preferences, "agenda_saved", emptyBookmarks.encoded()) == ESP_OK &&
        nvs_set_u8(preferences, "after_dark_v1", badge_after_dark::Unlock::SavedLocked) == ESP_OK &&
        nvs_commit(preferences) == ESP_OK;
    resetRequested = false;
    if (saved && badge::wifi_credentials_forget()) {
        ++preferenceWrites;
        settings = defaults;
        bookmarks = emptyBookmarks;
        afterDark.restore(badge_after_dark::Unlock::SavedLocked);
        // Best effort: game progress is not part of the reset's success.
        model.labyrinth_finished = 0;
        nvs_erase_key(preferences, "hack_maze"); nvs_commit(preferences);
        board::setBrightness(settings.brightness);
        rotationPending = true;
        resetNeedsPreferences = false;
        model.reset_state = badge::ResetState::Complete;
    } else {
        // The profile is already cleared. Keep that partial outcome explicit;
        // do not claim success or automatically repeat a destructive request.
        model.reset_state = badge::ResetState::SettingsFailed;
    }
    refreshModel();
}
// Eyes: the reading is the support force along the unrotated display's
// right (native Y) and down (native X) axes; gravity is its opposite.
void pollMotion() {
    static uint32_t lastMotion = 0;
    auto imu = board::acceleration();
    if (!imu.valid || imu.sampledAtMs == lastMotion) return;
    lastMotion = imu.sampledAtMs;
    const float right = -imu.y, down = -imu.x;
    switch (board::rotation()) {
        case 1: badge::ui_motion(down, -right); break;
        case 2: badge::ui_motion(-right, -down); break;
        case 3: badge::ui_motion(-down, right); break;
        default: badge::ui_motion(right, down); break;
    }
}
void pollOrientation(uint32_t now) {
    if (badge::ui_touch_test_active() || badge::ui_reset_active()) return;
    bool touching = board::touch().pressed;
    if (rotationPending && !touching) {
        uint8_t next = settings.automatic() ? board::rotation() : settings.fixedRotation();
        if (board::setRotation(next)) {
            orientation.reset(next); rotationPending = false; badge::ui_rotation_changed();
        }
    }
    if (!settings.automatic() || rotationPending) return;
    auto imu = board::acceleration();
    if (imu.sampledAtMs == lastImu) return;
    lastImu = imu.sampledAtMs;
    if (!imu.valid) { orientation.invalidate(); return; }
    if (orientation.update(imu.y, imu.x, imu.z, now, touching) && board::setRotation(orientation.rotation()))
        badge::ui_rotation_changed();
}
}

extern "C" void app_main() {
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 8192, .rx_buffer_size = 2048};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    usb_serial_jtag_vfs_use_driver();
    // Never erase an attendee's NVS on initialization errors.
    esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK || !board::init()) { line("CONFERENCE_BOOT_FAILED"); return; }
    preferencesReady = nvs_open("conference_ui", NVS_READWRITE, &preferences) == ESP_OK;
    uint32_t value = 0, savedBookmarks = 0; uint8_t savedUnlock = 0;
    if (preferencesReady) {
        nvs_get_u32(preferences, "prefs", &value);
        nvs_get_u8(preferences, "after_dark_v1", &savedUnlock);
        nvs_get_u32(preferences, "agenda_saved", &savedBookmarks);
        uint8_t savedMaze = 0;
        if (nvs_get_u8(preferences, "hack_maze", &savedMaze) == ESP_OK) model.labyrinth_finished = savedMaze;
    }
    afterDark.restore(savedUnlock);
    bookmarks.restore(savedBookmarks);
    settings.restore(value);
    // A finger held during boot can defer rotation. Keep the filter aligned
    // with the actual display and retry the saved fixed mode after release.
    rotationPending = !board::setRotation(settings.automatic() ? 2 : settings.fixedRotation());
    orientation.reset(board::rotation());
    board::setBrightness(settings.brightness);
    badge_clock::init();
    badge::services_init(badge_clock::setFromPhone);
    badge::UiCallbacks callbacks;
    callbacks.calibrate_touch = [] { calibrationRequested = true; };
    callbacks.cancel_calibration = [] { calibration.cancel(); calibrationRequested = false; };
    callbacks.request_setup = []{ startSetup(); };
    callbacks.close_setup = closeSetup;
    callbacks.brightness = [](int value) {
        settings.setBrightness(value, board::millis());
        board::setBrightness(settings.brightness); refreshModel();
    };
    callbacks.orientation = [](badge::Orientation mode) {
        if (settings.setOrientation(static_cast<ConferenceOrientationMode>(mode), board::millis())) rotationPending = true;
        refreshModel();
    };
    callbacks.bookmark = [](int index) {
        if (bookmarks.toggle(index, board::millis())) refreshModel();
    };
    callbacks.reset_badge = requestReset;
    callbacks.radar_scan = [] { badge::radar_scan_request(); };
    callbacks.labyrinth_finished = [](int level) {
        // Levels are finished minutes apart, so each one is written at once.
        model.labyrinth_finished = std::clamp(level, 0, 255);
        if (preferencesReady && nvs_set_u8(preferences, "hack_maze", uint8_t(model.labyrinth_finished)) == ESP_OK &&
            nvs_commit(preferences) == ESP_OK) ++preferenceWrites;
    };
    callbacks.unlock_after_dark = [] {
        if (afterDark.unlock(board::millis())) refreshModel();
    };
    badge::ui_init(board::display(), std::move(callbacks));
    refreshModel();
    line("CONFERENCE_READY"); status(nullptr);
    while (true) {
        uint32_t now = board::millis();
        if (lastLoop) maxLoopGap = std::max(maxLoopGap, now - lastLoop);
        lastLoop = now; ++loopCount;
        board::poll(); badge_clock::poll(); pollCommands();
        auto keys = board::buttons();
        applyButton(buttons.update(keys.yellow, keys.blue, now));
        auto contact = board::touch();
        if (calibrationRequested && !contact.pressed && board::setRotation(0)) {
            calibrationRequested = false; calibration.begin(); calibrationSequence = contact.sequence;
            badge::ui_show_calibration(); refreshModel();
        }
        if (badge::ui_calibration_active() && contact.sequence != calibrationSequence) {
            calibrationSequence = contact.sequence;
            calibration.sample(contact.valid, contact.sensor, contact.pressed, contact.rawX, contact.rawY, contact.sampledAtMs);
            if (calibration.stage() == touch_mapping::Session::Stage::Ready) {
                calibration.saved(touch_mapping::save(calibration.affine()));
                lv_indev_reset(board::pointer(), nullptr);
            }
            refreshModel();
        }
        touchObservation.active(board::millis());
        if (contact.sequence != observedTouchSequence) {
            observedTouchSequence = contact.sequence;
            const bool scope = badge::ui_page_index() == 2 && !setupRequested &&
                !badge::ui_setup_active() && !badge::ui_touch_test_active() && !badge::ui_reset_active();
            touchObservation.observe(scope, contact.valid, contact.pressed, contact.sensor,
                                     contact.x, contact.y, contact.sampledAtMs);
        }
        if (badge::ui_touch_test_active() && contact.valid && contact.sequence &&
            (contact.pressed || badge::ui_touch_state().sample))
            badge::ui_touch_sample(contact.rawX, contact.rawY, contact.x, contact.y, contact.pressed, board::rotation(), contact.sensor);
        pollOrientation(now); pollMotion(); pollReset(); persist(now);
        auto portal = badge::portal_snapshot();
        if (setupRequested) {
            if (portal.active || portal.starting) badge::ui_show_setup(portal.ssid, portal.password, "192.168.4.1",
                portal.outcome == badge::PortalOutcome::Saved ? "Saved / closing setup" : portal.outcome == badge::PortalOutcome::Cancelled ? "Cancelled / closing setup" : portal.starting ? "Starting setup..." : "");
            else {
                setupRequested = false;
                badge::ui_close_setup(portal.outcome == badge::PortalOutcome::Saved);
                refreshModel();
            }
        }
        if (uint32_t(now - lastModel) >= 100) { lastModel = now; refreshModel(); }
        badge::ui_tick(now); lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

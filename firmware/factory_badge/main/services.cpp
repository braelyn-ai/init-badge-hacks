#include "services.h"
#include "portal_page.h"
#include "wifi_config.h"
#include "../../devices_badge/avatar_decode.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_littlefs.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/sha256.h"

namespace badge {
namespace {
constexpr size_t kHeader = 64, kMetadata = 768, kAvatarSide = 160;
constexpr size_t kAvatarBytes = kAvatarSide * kAvatarSide * 2, kImageLimit = 128 * 1024;
constexpr char kRoot[] = "/badgefs", kRecord[] = "/badgefs/conference-manual-v1.bin";
constexpr char kTemporary[] = "/badgefs/conference-manual-v1.tmp";
constexpr uint8_t kMagic[8] = {'I','N','I','T','C','F','0','1'};
std::mutex data_mutex, portal_mutex, write_mutex, sockets_mutex;
struct TrackedSocket { int fd = -1; int64_t opened_at = 0; };
TrackedSocket tracked_sockets[3];
ProfileSnapshot stored;
PortalSnapshot portal;
WifiFetchSnapshot fetch_state;
WifiCredentials fetch_credentials;
// The queued photo target (portal_mutex). from_profile photos are saved only if
// the profile still has that handle when the download finishes.
int fetch_photo_network = -1;
std::string fetch_photo_handle;
bool fetch_photo_from_profile = false;
constexpr size_t kPhotoMaxBytes = 128 * 1024; // The decoder's JPEG limit.
ProfileResetSnapshot reset_state; // Protected by portal_mutex with AP lifecycle.
PhoneClockSync sync_clock;
std::atomic<bool> requested{false}, running{false};
TaskHandle_t service_task = nullptr;
esp_netif_t* ap_netif = nullptr;
esp_netif_t* station_netif = nullptr;
httpd_handle_t server = nullptr;
int dns_socket = -1;
std::string session_nonce, staged_token;
std::vector<uint8_t> staged_image;
int64_t expires_at = 0, closes_at = 0;

int64_t monotonic_ms() { return esp_timer_get_time() / 1000; }
bool alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
std::string trim(const std::string& s) {
  auto begin = s.find_first_not_of(' '); if (begin == std::string::npos) return {};
  return s.substr(begin, s.find_last_not_of(' ') - begin + 1);
}
bool name_valid(const std::string& input, std::string& result) {
  if (input.size() > 160) return false;
  auto s = trim(input); if (s.size() > 120) return false;
  unsigned count = 0;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i++]; uint32_t cp; unsigned extra;
    if (c < 128) { cp = c; extra = 0; }
    else if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; }
    else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; }
    else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; }
    else return false;
    if (i + extra > s.size()) return false;
    for (unsigned n = 0; n < extra; ++n) { uint8_t next = s[i++]; if ((next & 0xc0) != 0x80) return false; cp = (cp << 6) | (next & 63); }
    if ((extra == 1 && cp < 128) || (extra == 2 && cp < 2048) || (extra == 3 && cp < 65536) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || (cp >= 127 && cp <= 159) || cp == 0x2028 || cp == 0x2029 ||
        (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069) || ++count > 60) return false;
  }
  result = s; return true;
}
bool social_url(unsigned network, const std::string& input, std::string& result) {
  result.clear(); if (network > 2 || input.size() > 180) return false;
  auto value = trim(input); if (value.empty()) return true;
  auto handle = value;
  static const char* prefixes[3][4] = {
    {"https://github.com/", "https://www.github.com/", nullptr, nullptr},
    {"https://x.com/", "https://twitter.com/", "https://www.x.com/", "https://www.twitter.com/"},
    {"https://www.linkedin.com/in/", "https://linkedin.com/in/", nullptr, nullptr}
  };
  bool full = false;
  for (auto prefix : prefixes[network]) if (prefix && value.rfind(prefix, 0) == 0) { handle = value.substr(strlen(prefix)); full = true; break; }
  if (full && !handle.empty() && handle.back() == '/') handle.pop_back();
  if (!full && !handle.empty() && handle.front() == '@') handle.erase(0, 1);
  if (handle.empty() || handle.size() > (network == 0 ? 39u : network == 1 ? 15u : 100u)) return false;
  for (size_t i = 0; i < handle.size(); ++i) {
    char c = handle[i];
    if (!alnum(c) && !(c == '_' && network != 0) && !(c == '-' && network != 1)) return false;
    if (network == 0 && c == '-' && (i == 0 || i + 1 == handle.size() || handle[i - 1] == '-')) return false;
  }
  result = std::string(network == 0 ? "https://github.com/" : network == 1 ? "https://x.com/" : "https://www.linkedin.com/in/") + handle + (network == 2 ? "/" : "");
  return true;
}
// Network keys shared by the setup page, photo relay paths and profile slots.
constexpr const char* kPhotoNetworks[3] = {"github", "x", "linkedin"};
// Canonical profile URL (from social_url) back to its bare handle.
std::string social_handle(unsigned network, const std::string& url) {
  static const char* prefixes[3] = {"https://github.com/", "https://x.com/", "https://www.linkedin.com/in/"};
  if (network > 2 || url.rfind(prefixes[network], 0) != 0) return {};
  auto handle = url.substr(strlen(prefixes[network]));
  if (!handle.empty() && handle.back() == '/') handle.pop_back();
  return handle;
}
bool profile_valid(const Profile& p) {
  std::string value; if (!name_valid(p.name, value) || value != p.name) return false;
  if (!name_valid(p.company, value) || value != p.company) return false;
  for (unsigned i = 0; i < 3; ++i) if (!social_url(i, p.urls[i], value) || value != p.urls[i]) return false;
  return true;
}
uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void put32(uint8_t* p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = n >> (i * 8); }
struct File {
  FILE* value;
  File(const char* path, const char* mode) : value(fopen(path, mode)) {}
  ~File() { if (value) fclose(value); }
  bool close() { if (!value) return false; FILE* f = value; value = nullptr; return fclose(f) == 0; }
};
bool digest(const uint8_t* header, const uint8_t* metadata, size_t length, const uint16_t* pixels, uint8_t* output) {
  mbedtls_sha256_context hash; mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0 && mbedtls_sha256_update(&hash, header, 32) == 0 &&
    mbedtls_sha256_update(&hash, metadata, length) == 0;
  if (ok && pixels) ok = mbedtls_sha256_update(&hash, reinterpret_cast<const uint8_t*>(pixels), kAvatarBytes) == 0;
  if (ok) ok = mbedtls_sha256_finish(&hash, output) == 0;
  mbedtls_sha256_free(&hash); return ok;
}
size_t encode(const Profile& p, uint8_t* bytes, uint16_t version) {
  if (version != 1 && version != 2) return 0;
  if (version == 1 && !p.company.empty()) return 0;
  const std::string* fields[] = {&p.name, &p.urls[0], &p.urls[1], &p.urls[2], &p.company}; size_t used = 0;
  for (unsigned i = 0; i < (version == 1 ? 4u : 5u); ++i) { auto field = fields[i];
    if (used + 2 + field->size() > kMetadata) return 0;
    put16(bytes + used, field->size()); used += 2;
    memcpy(bytes + used, field->data(), field->size()); used += field->size();
  }
  return used;
}
bool decode(const uint8_t* bytes, size_t length, uint16_t version, Profile& p) {
  if ((version != 1 && version != 2) || length > kMetadata) return false;
  Profile candidate;
  std::string* fields[] = {&candidate.name, &candidate.urls[0], &candidate.urls[1], &candidate.urls[2], &candidate.company}; size_t used = 0;
  for (unsigned i = 0; i < (version == 1 ? 4u : 5u); ++i) { auto field = fields[i];
    if (used + 2 > length) return false;
    size_t n = u16(bytes + used); used += 2;
    if (n > length - used || memchr(bytes + used, 0, n)) return false;
    field->assign(reinterpret_cast<const char*>(bytes + used), n); used += n; }
  if (used != length || !profile_valid(candidate)) return false;
  p = std::move(candidate); return true;
}
bool read_record(const char* path, ProfileSnapshot& result, uint8_t* digest_out = nullptr) {
  File file(path, "rb"); if (!file.value) return false;
  struct stat info; uint8_t header[kHeader], metadata[kMetadata];
  if (fstat(fileno(file.value), &info) || !S_ISREG(info.st_mode) || info.st_size < int64_t(kHeader) ||
      info.st_size > int64_t(kHeader + kMetadata + kAvatarBytes) || fread(header, 1, kHeader, file.value) != kHeader) return false;
  size_t meta_size = u32(header + 16), image_size = u32(header + 20); uint16_t version = u16(header + 8);
  if (memcmp(header, kMagic, 8) || (version != 1 && version != 2) || u16(header + 10) != kHeader || u16(header + 12) != kAvatarSide ||
      u16(header + 14) != kAvatarSide || !meta_size || meta_size > kMetadata || (image_size && image_size != kAvatarBytes) ||
      u32(header + 24) || u32(header + 28) || uint64_t(info.st_size) != kHeader + meta_size + image_size) return false;
  Profile profile;
  if (fread(metadata, 1, meta_size, file.value) != meta_size || !decode(metadata, meta_size, version, profile)) return false;
  std::shared_ptr<std::vector<uint16_t>> pixels;
  if (image_size) { pixels = std::make_shared<std::vector<uint16_t>>(kAvatarSide * kAvatarSide); if (fread(pixels->data(), 1, image_size, file.value) != image_size) return false; }
  if (ferror(file.value) || !file.close()) return false;
  uint8_t sum[32]; if (!digest(header, metadata, meta_size, pixels ? pixels->data() : nullptr, sum) || memcmp(sum, header + 32, 32)) return false;
  if (digest_out) memcpy(digest_out, sum, 32);
  result.profile = std::move(profile); result.avatar = std::move(pixels); return true;
}
bool save_record(const Profile& profile, const uint8_t* jpeg, size_t jpeg_size, bool replace, std::string& error) {
  std::lock_guard<std::mutex> writing(write_mutex);
  auto previous = profile_snapshot();
  if (!previous.ready) { error = "Storage unavailable"; return false; }
  if (!profile_valid(profile)) { error = "Check the name, company and social accounts"; return false; }
  ProfileSnapshot next; next.profile = profile; next.ready = true; next.avatar = replace ? nullptr : previous.avatar;
  if (replace && jpeg_size) {
    int width = 0, height = 0; uint8_t* rgb = badgeDecodeAvatarJpeg(jpeg, jpeg_size, width, height);
    if (!rgb) { error = "Use a JPEG up to 512 by 512 and 128 KiB"; return false; }
    auto pixels = std::make_shared<std::vector<uint16_t>>(kAvatarSide * kAvatarSide);
    int side = std::min(width, height), left = (width - side) / 2, top = (height - side) / 2;
    for (unsigned y = 0; y < kAvatarSide; ++y) for (unsigned x = 0; x < kAvatarSide; ++x) {
      const uint8_t* p = rgb + ((top + y * side / kAvatarSide) * width + left + x * side / kAvatarSide) * 3;
      (*pixels)[y * kAvatarSide + x] = uint16_t((p[0] & 0xf8) << 8) | uint16_t((p[1] & 0xfc) << 3) | (p[2] >> 3);
    }
    badgeFreeAvatarPixels(rgb); next.avatar = std::move(pixels);
  }
  // Keep empty-company records readable by the previous firmware. Version 2
  // appends one bounded field; the path, pixels, hash and atomic commit stay the same.
  const uint16_t version = profile.company.empty() ? 1 : 2;
  uint8_t header[kHeader] = {}, metadata[kMetadata]; size_t length = encode(profile, metadata, version);
  if (!length) { error = "Badge too large"; return false; }
  memcpy(header, kMagic, 8); put16(header + 8, version); put16(header + 10, kHeader); put16(header + 12, kAvatarSide); put16(header + 14, kAvatarSide);
  put32(header + 16, length); put32(header + 20, next.avatar ? kAvatarBytes : 0);
  if (!digest(header, metadata, length, next.avatar ? next.avatar->data() : nullptr, header + 32)) { error = "Badge hash failed"; return false; }
  File file(kTemporary, "wb"); if (!file.value) { error = "Could not open badge storage"; return false; }
  bool written = fwrite(header, 1, kHeader, file.value) == kHeader && fwrite(metadata, 1, length, file.value) == length;
  if (written && next.avatar) written = fwrite(next.avatar->data(), 1, kAvatarBytes, file.value) == kAvatarBytes;
  if (written) written = !ferror(file.value) && fflush(file.value) == 0 && fsync(fileno(file.value)) == 0;
  bool closed = file.close(); ProfileSnapshot verified; uint8_t verified_digest[32];
  if (!written || !closed || !read_record(kTemporary, verified, verified_digest) || memcmp(verified_digest, header + 32, 32)) {
    unlink(kTemporary); error = "Badge save failed; previous badge kept"; return false;
  }
  if (rename(kTemporary, kRecord) != 0) { unlink(kTemporary); error = "Badge commit failed; previous badge kept"; return false; }
  next.revision = previous.revision + 1;
  { std::lock_guard<std::mutex> lock(data_mutex); stored = std::move(next); }
  return true;
}
bool reset_pending() {
  return reset_state.state == ProfileResetState::Pending || reset_state.state == ProfileResetState::Running;
}
bool reset_setup_busy() {
  // Call only under portal_mutex. Short-circuit before inspecting staging while
  // HTTP may own it. An inactive portal has joined HTTP and cleared staging.
  return portal.active || portal.starting || requested || running ||
         !staged_image.empty() || !staged_token.empty();
}
bool queue_profile_reset(std::string& error) {
  std::lock_guard<std::mutex> lock(portal_mutex);
  error.clear();
  if (reset_pending()) { error = "Reset already in progress"; return false; }
  if (reset_setup_busy()) { error = "Close phone setup before resetting the badge"; return false; }
  if (!service_task) { error = "Reset service unavailable; restart and try again"; return false; }
  if (!profile_snapshot().ready) { error = "Storage unavailable; saved badge was not changed"; return false; }
  reset_state = {ProfileResetState::Pending, {}};
  return true;
}
void process_profile_reset() {
  {
    std::lock_guard<std::mutex> lock(portal_mutex);
    if (reset_state.state != ProfileResetState::Pending) return;
    if (reset_setup_busy()) {
      reset_state = {ProfileResetState::Failed, "Phone setup is busy; saved badge was not changed"};
      return;
    }
    reset_state.state = ProfileResetState::Running;
  }
  // This runs on the service worker. The existing verified temp-file/rename
  // transaction changes only the current manual record and its in-memory model.
  // No NVS erase, filesystem format, clock write or legacy-file removal occurs.
  std::string error;
  const bool saved = save_record({}, nullptr, 0, true, error);
  std::lock_guard<std::mutex> lock(portal_mutex);
  reset_state.state = saved ? ProfileResetState::Succeeded : ProfileResetState::Failed;
  reset_state.error = saved ? "" : error.empty() ? "Reset failed; previous badge kept" : error;
}
std::string random_hex(unsigned bytes) {
  uint8_t random[16]; esp_fill_random(random, sizeof(random)); char text[33] = {};
  for (unsigned i = 0; i < std::min(bytes, 16u); ++i) snprintf(text + i * 2, 3, "%02x", random[i]);
  return text;
}
std::string escape(const std::string& input) {
  std::string output;
  for (char c : input) switch (c) { case '&': output += "&amp;"; break; case '<': output += "&lt;"; break;
    case '>': output += "&gt;"; break; case '"': output += "&quot;"; break; case '\'': output += "&#39;"; break; default: output += c; }
  return output;
}
void replace_token(std::string& s, const char* token, const std::string& value) { auto pos = s.find(token); if (pos != std::string::npos) s.replace(pos, strlen(token), value); }
std::string page() {
  auto current = profile_snapshot(); std::string html = BADGE_PORTAL_HTML;
  WifiCredentials wifi; wifi_credentials_load(wifi); const bool custom = wifi_credentials_custom();
  // One social account: the first saved network and its bare handle.
  int network = 0;
  while (network < 2 && current.profile.urls[network].empty()) ++network;
  if (current.profile.urls[network].empty()) network = 0;
  // Replace tokens in reverse document order so attendee text inserted earlier
  // in the page can never be matched as a later token.
  replace_token(html, "{{NONCE}}", session_nonce);
  replace_token(html, "{{PHOTO}}", current.avatar ? "Your saved photo will be kept unless you replace or remove it." : "No saved photo. A placeholder will appear until you add one.");
  replace_token(html, "{{PHOTO_CHOICE}}", current.avatar ? "keep" : "network");
  replace_token(html, "{{HANDLE}}", escape(social_handle(network, current.profile.urls[network])));
  replace_token(html, "{{NETWORK}}", kPhotoNetworks[network]);
  replace_token(html, "{{COMPANY}}", escape(current.profile.company));
  replace_token(html, "{{NAME}}", escape(current.profile.name));
  replace_token(html, "{{WIFI_STATUS}}", custom ? "This badge has a different network saved." : "Using the built-in event Wi-Fi.");
  replace_token(html, "{{WIFI_SSID}}", escape(custom ? wifi.ssid : std::string()));
  replace_token(html, "{{WIFI_CHOICE}}", custom ? "other" : "event");
  return html;
}
const char* status_text(int status) { switch (status) { case 200: return "200 OK"; case 400: return "400 Bad Request"; case 403: return "403 Forbidden";
  case 408: return "408 Request Timeout"; case 413: return "413 Payload Too Large"; case 415: return "415 Unsupported Media Type";
  case 500: return "500 Internal Server Error"; default: return "503 Service Unavailable"; } }
esp_err_t json_reply(httpd_req_t* req, int status, cJSON* object) {
  char* serialized = cJSON_PrintUnformatted(object); if (!serialized) return ESP_FAIL;
  httpd_resp_set_status(req, status_text(status)); httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store"); httpd_resp_set_hdr(req, "Connection", "close");
  auto result = httpd_resp_send(req, serialized, strlen(serialized)); cJSON_free(serialized); return result;
}
esp_err_t message(httpd_req_t* req, int status, const std::string& text) {
  cJSON* object = cJSON_CreateObject(); if (!object) return ESP_FAIL;
  cJSON_AddStringToObject(object, "message", text.c_str()); auto result = json_reply(req, status, object); cJSON_Delete(object); return result;
}
bool header(httpd_req_t* req, const char* key, std::string& value, size_t limit) {
  size_t n = httpd_req_get_hdr_value_len(req, key); if (!n || n > limit) return false;
  std::vector<char> bytes(n + 1); if (httpd_req_get_hdr_value_str(req, key, bytes.data(), bytes.size()) != ESP_OK) return false;
  value.assign(bytes.data(), n); return true;
}
bool session_open() { std::lock_guard<std::mutex> lock(portal_mutex); return requested && portal.active && portal.outcome == PortalOutcome::None && monotonic_ms() < expires_at; }
bool json_fields(cJSON* root, const std::vector<const char*>& allowed) {
  for (cJSON* field = root->child; field; field = field->next) {
    if (!field->string) return false;
    bool found = false; for (auto key : allowed) if (!strcmp(key, field->string)) found = true;
    if (!found) return false;
    for (cJSON* earlier = root->child; earlier != field; earlier = earlier->next) if (!strcmp(earlier->string, field->string)) return false;
  }
  return true;
}
bool profile_form(cJSON* root, const Profile& previous, Profile& candidate, std::string& error) {
  const char* fields[] = {"name", "github", "x", "linkedin", "image", "imageToken"};
  if (!json_fields(root, {fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], "company", "ssid", "password"})) {
    error = "Unexpected form field."; return false;
  }
  for (auto field : fields) if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root, field))) {
    error = "Incomplete form. Reload and try again."; return false;
  }
  Profile next;
  if (!name_valid(cJSON_GetObjectItemCaseSensitive(root, "name")->valuestring, next.name)) {
    error = "Use a name of at most 60 characters / 120 UTF-8 bytes, without control characters."; return false;
  }
  auto company = cJSON_GetObjectItemCaseSensitive(root, "company");
  // A setup page opened on older firmware must not silently clear this field.
  next.company = previous.company;
  if (company && (!cJSON_IsString(company) || !name_valid(company->valuestring, next.company))) {
    error = "Use a company of at most 60 characters / 120 UTF-8 bytes, without control characters."; return false;
  }
  for (unsigned i = 0; i < 3; ++i) if (!social_url(i, cJSON_GetObjectItemCaseSensitive(root, fields[i + 1])->valuestring, next.urls[i])) {
    error = "Check the social handles. Use a profile handle or its HTTPS profile URL."; return false;
  }
  candidate = std::move(next); return true;
}
void finish_session(PortalOutcome result) {
  std::lock_guard<std::mutex> lock(portal_mutex); portal.outcome = result; closes_at = monotonic_ms() + 3000;
}
esp_err_t socket_open(httpd_handle_t, int fd) {
  std::lock_guard<std::mutex> lock(sockets_mutex);
  for (auto& socket : tracked_sockets) if (socket.fd < 0) { socket = {fd, monotonic_ms()}; return ESP_OK; }
  return ESP_FAIL;
}
void socket_close(httpd_handle_t, int fd) {
  std::lock_guard<std::mutex> lock(sockets_mutex);
  for (auto& socket : tracked_sockets) if (socket.fd == fd) socket.fd = -1;
  close(fd);
}
void expire_sockets(bool all = false) {
  // The service task enforces an absolute connection lifetime independently of
  // HTTP parsing. SO_RCVTIMEO alone permits an indefinitely trickled header.
  std::lock_guard<std::mutex> lock(sockets_mutex);
  for (auto& socket : tracked_sockets) if (socket.fd >= 0 && (all || monotonic_ms() - socket.opened_at >= 15000)) shutdown(socket.fd, SHUT_RDWR);
}
esp_err_t handle_get(httpd_req_t* req) {
  if (!session_open()) return message(req, 403, "Open the current badge setup page.");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store"); httpd_resp_set_hdr(req, "Connection", "close");
  if (strcmp(req->uri, "/")) { httpd_resp_set_status(req, "302 Found"); httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/"); return httpd_resp_send(req, "", 0); }
  httpd_resp_set_type(req, "text/html; charset=utf-8"); auto html = page(); return httpd_resp_send(req, html.data(), html.size());
}
// photo_network < 0 is the 1 KiB connectivity probe; otherwise the complete
// avatar JPEG (at most kPhotoMaxBytes) is returned in body for saving after
// Wi-Fi is off.
WifiFetchSnapshot fetch_once(const WifiCredentials& credentials, int photo_network,
                             const std::string& handle, std::vector<uint8_t>& body) {
  WifiFetchSnapshot result;
  result.state = WifiFetchState::Failed;
  result.photo_network = photo_network;
  result.error = "Couldn't join Wi-Fi";
  if (!station_netif) station_netif = esp_netif_create_default_wifi_sta();
  if (!station_netif) return result;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) {
    esp_netif_destroy_default_wifi(station_netif);
    station_netif = nullptr;
    return result;
  }
  bool started = false;
  do {
    wifi_config_t config = {};
    memcpy(config.sta.ssid, credentials.ssid.data(), credentials.ssid.size());
    memcpy(config.sta.password, credentials.password.data(), credentials.password.size());
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK ||
        esp_wifi_start() != ESP_OK) break;
    started = true;
    if (esp_wifi_connect() != ESP_OK) break;
    const int64_t deadline = monotonic_ms() + 10000;
    bool joined = false;
    while (monotonic_ms() < deadline) {
      wifi_ap_record_t access = {};
      esp_netif_ip_info_t ip = {};
      if (esp_wifi_sta_get_ap_info(&access) == ESP_OK &&
          esp_netif_get_ip_info(station_netif, &ip) == ESP_OK && ip.ip.addr != 0) {
        joined = true; break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!joined) break;
    result.error = "Couldn't reach photo service";
    const std::string url = photo_network < 0 ? std::string("https://workos.com/init")
      : "https://avatar.chan.dev/v1/" + std::string(kPhotoNetworks[photo_network]) + "/" + handle;
    esp_http_client_config_t http = {};
    http.url = url.c_str();
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.timeout_ms = 7000;
    http.disable_auto_redirect = true;
    http.user_agent = "init-badge/1";
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client) break;
    if (esp_http_client_open(client, 0) == ESP_OK) {
      esp_http_client_fetch_headers(client);
      result.http_status = esp_http_client_get_status_code(client);
      if (photo_network < 0) {
        uint8_t data[1024];
        int bytes = esp_http_client_read(client, reinterpret_cast<char*>(data), sizeof(data));
        if (bytes > 0) result.bytes = bytes;
        if (result.http_status == 200 && result.bytes > 0) result.state = WifiFetchState::Succeeded;
      } else if (result.http_status == 200) {
        body.clear();
        uint8_t data[2048];
        bool complete = false;
        for (;;) {
          int bytes = esp_http_client_read(client, reinterpret_cast<char*>(data), sizeof(data));
          if (bytes < 0) break;
          if (bytes == 0) { complete = esp_http_client_is_complete_data_received(client); break; }
          if (body.size() + size_t(bytes) > kPhotoMaxBytes) { body.clear(); break; }
          body.insert(body.end(), data, data + bytes);
        }
        result.bytes = body.size();
        if (complete && !body.empty()) { result.state = WifiFetchState::Succeeded; result.error.clear(); }
        else { body.clear(); result.error = "Couldn't download photo"; }
      } else {
        result.error = result.http_status == 404 ? "No public photo found"
          : result.http_status == 429 ? "Photo service busy, try later" : "Photo service unavailable";
      }
    }
    esp_http_client_cleanup(client);
  } while (false);
  if (started) esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_destroy_default_wifi(station_netif);
  station_netif = nullptr;
  return result;
}
esp_err_t handle_post(httpd_req_t* req) {
  const bool image = !strcmp(req->uri, "/image"), clock = !strcmp(req->uri, "/clock"), cancel = !strcmp(req->uri, "/cancel"), save = !strcmp(req->uri, "/save");
  if (!(image || clock || cancel || save)) return message(req, 400, "Unknown request.");
  std::string nonce, type;
  if (!session_open() || !header(req, "X-Conference-Nonce", nonce, 32) || nonce != session_nonce) return message(req, 403, "Open the current badge setup page.");
  if (httpd_req_get_hdr_value_len(req, "Transfer-Encoding") || req->content_len == 0 || req->content_len > (image ? kImageLimit : 2048)) return message(req, 413, "Request is too large or unsupported.");
  if (!header(req, "Content-Type", type, 80) || (image ? type != "image/jpeg" : type != "application/json")) return message(req, 415, "Unsupported request format.");
  std::unique_ptr<uint8_t, decltype(&heap_caps_free)> bytes(static_cast<uint8_t*>(heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), heap_caps_free);
  if (!bytes) return message(req, 503, "Not enough memory. Try a smaller image.");
  const int64_t deadline = monotonic_ms() + 10000; size_t used = 0;
  while (used < req->content_len) {
    if (!session_open() || monotonic_ms() >= deadline) return ESP_FAIL;
    int n = httpd_req_recv(req, reinterpret_cast<char*>(bytes.get() + used), std::min<size_t>(2048, req->content_len - used));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return ESP_FAIL;
    used += n;
  }
  bytes.get()[used] = 0;
  if (image) {
    staged_image.clear(); staged_token.clear(); int w = 0, h = 0;
    auto rgb = badgeDecodeAvatarJpeg(bytes.get(), used, w, h);
    if (!rgb) return message(req, 400, "Use a JPEG up to 512 by 512 and 128 KiB.");
    badgeFreeAvatarPixels(rgb); staged_image.assign(bytes.get(), bytes.get() + used); staged_token = random_hex(8);
    cJSON* result = cJSON_CreateObject(); if (!result) return ESP_FAIL;
    cJSON_AddStringToObject(result, "message", "Photo ready. Save badge to keep it."); cJSON_AddStringToObject(result, "imageToken", staged_token.c_str());
    auto sent = json_reply(req, 200, result); cJSON_Delete(result); return sent;
  }
  // cJSON uses NUL-terminated strings. Refuse embedded NUL encodings before
  // parsing so fields cannot silently acquire truncated values.
  if (memchr(bytes.get(), 0, used) || strstr(reinterpret_cast<char*>(bytes.get()), "\\u0000")) return message(req, 400, "Invalid form characters.");
  const char* parsed_end = nullptr;
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(cJSON_ParseWithLengthOpts(reinterpret_cast<char*>(bytes.get()), used + 1, &parsed_end, true), cJSON_Delete);
  if (!json || !cJSON_IsObject(json.get())) return message(req, 400, "Invalid form. Reload and try again.");
  if (cancel) {
    if (json->child) return message(req, 400, "Unexpected cancel field.");
    auto result = message(req, 200, "Profile changes cancelled. Any successful clock sync is kept. You may close this page.");
    finish_session(PortalOutcome::Cancelled); return result;
  }
  if (clock) {
    if (!json_fields(json.get(), {"epoch", "offset_minutes", "timezone"})) return message(req, 400, "Unexpected clock field.");
    auto epoch = cJSON_GetObjectItemCaseSensitive(json.get(), "epoch"), offset = cJSON_GetObjectItemCaseSensitive(json.get(), "offset_minutes"), zone = cJSON_GetObjectItemCaseSensitive(json.get(), "timezone");
    if (!cJSON_IsNumber(epoch) || !cJSON_IsNumber(offset) || !std::isfinite(epoch->valuedouble) || !std::isfinite(offset->valuedouble) ||
        epoch->valuedouble != std::floor(epoch->valuedouble) || offset->valuedouble != std::floor(offset->valuedouble) || epoch->valuedouble < 1704067200.0 || epoch->valuedouble >= 4102444800.0 || offset->valuedouble < -840 || offset->valuedouble > 840) return message(req, 400, "Invalid clock values.");
    if (zone) { if (!cJSON_IsString(zone) || !zone->valuestring || !strlen(zone->valuestring) || strlen(zone->valuestring) > 64) return message(req, 400, "Invalid timezone label.");
      for (const char* c = zone->valuestring; *c; ++c) if (!alnum(*c) && *c != '/' && *c != '_' && *c != '-' && *c != '+') return message(req, 400, "Invalid timezone label."); }
    ClockSnapshot snapshot;
    std::string error; bool ok = sync_clock && sync_clock(static_cast<int64_t>(epoch->valuedouble), static_cast<int>(offset->valuedouble), snapshot, error);
    ok = ok && snapshot.valid;
    cJSON* result = cJSON_CreateObject(); if (!result) return ESP_FAIL;
    cJSON_AddBoolToObject(result, "ok", ok); cJSON_AddBoolToObject(result, "valid", snapshot.valid); cJSON_AddStringToObject(result, "source", snapshot.source.c_str());
    cJSON_AddNumberToObject(result, "epoch", static_cast<double>(snapshot.epoch));
    cJSON_AddNumberToObject(result, "rtc_epoch", static_cast<double>(snapshot.rtc_epoch));
    cJSON_AddNumberToObject(result, "offset_minutes", snapshot.offset_minutes);
    if (!ok) cJSON_AddStringToObject(result, "error", error.empty() ? "clock_sync_failed" : error.c_str());
    cJSON_AddStringToObject(result, "message", ok ? "Clock synchronized from this browser. Profile edits are saved separately." : "Clock sync could not be verified. Check your device time and try again.");
    auto sent = json_reply(req, ok ? 200 : 400, result); cJSON_Delete(result); return sent;
  }
  Profile candidate; std::string error;
  if (!profile_form(json.get(), profile_snapshot().profile, candidate, error)) return message(req, 400, error);
  auto value = [&](const char* key) { return cJSON_GetObjectItemCaseSensitive(json.get(), key)->valuestring; };
  // Wi-Fi is saved with the badge. The page never receives the saved password,
  // so an unchanged network name with a blank password keeps the saved network.
  WifiCredentials wifi; wifi_credentials_load(wifi);
  bool wifi_changed = false;
  auto ssid = cJSON_GetObjectItemCaseSensitive(json.get(), "ssid"), password = cJSON_GetObjectItemCaseSensitive(json.get(), "password");
  if (ssid || password) {
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) return message(req, 400, "Enter a Wi-Fi network name and optional password.");
    WifiCredentials next{trim(ssid->valuestring), password->valuestring};
    if (next.ssid != wifi.ssid || !next.password.empty()) {
      if (!wifi_credentials_valid(next)) return message(req, 400, "Use a Wi-Fi name of 1–32 bytes and either no password or 8–63 characters.");
      wifi = std::move(next); wifi_changed = true;
    }
  }
  std::string action = value("image"); bool replace = false; const uint8_t* jpeg = nullptr; size_t jpeg_size = 0;
  int source = -1;
  for (int i = 0; i < 3; ++i) if (action == kPhotoNetworks[i]) source = i;
  if (action == "auto") {
    // Automatic: without a saved photo, use the first handle (GitHub, X, LinkedIn).
    action = "keep";
    if (!profile_snapshot().avatar) for (int i = 0; i < 3 && source < 0; ++i) if (!candidate.urls[i].empty()) source = i;
  }
  static const char* names[3] = {"GitHub", "X", "LinkedIn"};
  if (source >= 0) {
    if (candidate.urls[source].empty()) return message(req, 400, std::string("Add your ") + names[source] + " handle to use its photo.");
  } else if (action == "remove") replace = true;
  else if (action == "staged") { if (staged_image.empty() || staged_token != value("imageToken")) return message(req, 400, "Upload your photo again before saving."); replace = true; jpeg = staged_image.data(); jpeg_size = staged_image.size(); }
  else if (action != "keep") return message(req, 400, "Invalid photo choice.");
  if (wifi_changed && !wifi_credentials_save(wifi)) return message(req, 500, "Could not save Wi-Fi. Try again.");
  if (!save_record(candidate, jpeg, jpeg_size, replace, error)) return message(req, 500, error);
  // The current photo stays until the new one downloads after setup closes.
  const bool queued = source >= 0 && photo_fetch_request(source);
  auto result = message(req, 200, source < 0 ? "Saved on your badge. You may close this page; setup Wi-Fi will turn off."
    : queued ? "Saved. When setup closes, the badge joins " + wifi.ssid + ", gets your " + names[source] + " photo, then turns Wi-Fi off."
    : "Saved, but the photo download could not be queued. Open setup again to retry.");
  staged_image.clear(); staged_token.clear(); finish_session(PortalOutcome::Saved); return result;
}
bool start_network() {
  PortalSnapshot state; { std::lock_guard<std::mutex> lock(portal_mutex); state = portal; }
  if (!ap_netif) ap_netif = esp_netif_create_default_wifi_ap();
  if (!ap_netif) return false;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) return false;
  wifi_config_t config = {}; memcpy(config.ap.ssid, state.ssid.data(), state.ssid.size()); config.ap.ssid_len = state.ssid.size();
  memcpy(config.ap.password, state.password.data(), state.password.size()); config.ap.channel = 1; config.ap.max_connection = 2;
  config.ap.authmode = WIFI_AUTH_WPA2_PSK; config.ap.pmf_cfg.required = false;
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK || esp_wifi_start() != ESP_OK) return false;
  dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (dns_socket < 0) return false;
  sockaddr_in address = {}; address.sin_family = AF_INET; address.sin_port = htons(53); address.sin_addr.s_addr = htonl(INADDR_ANY);
  timeval timeout = {0, 50000}; setsockopt(dns_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (bind(dns_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return false;
  httpd_config_t http = HTTPD_DEFAULT_CONFIG(); http.max_uri_handlers = 2; http.max_open_sockets = 3; http.lru_purge_enable = true;
  http.stack_size = 12288; http.recv_wait_timeout = 1; http.send_wait_timeout = 2; http.uri_match_fn = httpd_uri_match_wildcard;
  http.open_fn = socket_open; http.close_fn = socket_close;
  if (httpd_start(&server, &http) != ESP_OK) return false;
  httpd_uri_t get = {}; get.uri = "/*"; get.method = HTTP_GET; get.handler = handle_get;
  httpd_uri_t post = {}; post.uri = "/*"; post.method = HTTP_POST; post.handler = handle_post;
  return httpd_register_uri_handler(server, &get) == ESP_OK && httpd_register_uri_handler(server, &post) == ESP_OK;
}
void stop_network() {
  // HTTP shutdown joins its task before touching session-owned profile staging.
  expire_sockets(true);
  if (server) { httpd_stop(server); server = nullptr; }
  if (dns_socket >= 0) { close(dns_socket); dns_socket = -1; }
  esp_wifi_stop(); esp_wifi_deinit();
  staged_image.clear(); staged_image.shrink_to_fit(); staged_token.clear(); session_nonce.clear();
}
void dns_tick() {
  uint8_t packet[512]; sockaddr_in client = {}; socklen_t length = sizeof(client);
  int n = recvfrom(dns_socket, packet, sizeof(packet) - 16, 0, reinterpret_cast<sockaddr*>(&client), &length);
  if (n < 12 || (packet[2] & 0x80) || packet[4] || packet[5] != 1) return;
  size_t position = 12;
  while (position < size_t(n) && packet[position]) { uint8_t count = packet[position++]; if (count > 63 || position + count >= size_t(n)) return; position += count; }
  if (position + 5 != size_t(n)) return;
  uint16_t type = (packet[position + 1] << 8) | packet[position + 2];
  if (packet[position + 3] != 0 || packet[position + 4] != 1) return;
  packet[2] = 0x81; packet[3] = 0x80; packet[6] = 0; packet[7] = type == 1 ? 1 : 0; packet[8] = packet[9] = packet[10] = packet[11] = 0;
  if (type == 1) { const uint8_t answer[] = {0xc0,0x0c,0,1,0,1,0,0,0,0,0,4,192,168,4,1}; memcpy(packet + n, answer, sizeof(answer)); n += sizeof(answer); }
  sendto(dns_socket, packet, n, 0, reinterpret_cast<sockaddr*>(&client), length);
}
void service_loop(void*) {
  for (;;) {
    process_profile_reset();
    bool fetch = false, from_profile = false;
    int photo_network = -1;
    std::string photo_handle;
    WifiCredentials credentials;
    {
      std::lock_guard<std::mutex> lock(portal_mutex);
      if (fetch_state.state == WifiFetchState::Pending && !requested && !running) {
        fetch_state.state = WifiFetchState::Running;
        credentials = std::move(fetch_credentials);
        photo_network = fetch_photo_network; photo_handle = std::move(fetch_photo_handle);
        from_profile = fetch_photo_from_profile;
        fetch_photo_network = -1; fetch_photo_handle.clear();
        fetch = true;
      }
    }
    if (fetch) {
      std::vector<uint8_t> body;
      auto result = fetch_once(credentials, photo_network, photo_handle, body);
      std::fill(credentials.password.begin(), credentials.password.end(), '\0');
      credentials = {};
      // Wi-Fi is already off. Replace only the photo, and only for the account
      // that was requested if it came from the saved profile.
      if (photo_network >= 0 && result.state == WifiFetchState::Succeeded) {
        auto current = profile_snapshot();
        std::string error;
        if (from_profile && social_handle(photo_network, current.profile.urls[photo_network]) != photo_handle) {
          result.state = WifiFetchState::Failed; result.error = "Profile changed, photo skipped";
        } else if (!save_record(current.profile, body.data(), body.size(), true, error)) {
          result.state = WifiFetchState::Failed; result.error = "Couldn't use that photo";
        }
      }
      std::lock_guard<std::mutex> lock(portal_mutex);
      fetch_state = result;
    }
    if (requested && !running) {
      bool ok = start_network();
      if (!ok) { requested = false; stop_network(); }
      { std::lock_guard<std::mutex> lock(portal_mutex); portal.starting = false; portal.active = ok;
        if (!ok) { portal.error = "Could not start setup Wi-Fi"; portal.outcome = PortalOutcome::Error; portal.password.clear(); }
        else { expires_at = monotonic_ms() + 600000; closes_at = 0; } }
      running = ok;
    }
    if (!requested && !running) {
      std::lock_guard<std::mutex> lock(portal_mutex);
      if (portal.starting) { portal.starting = false; portal.password.clear(); portal.outcome = PortalOutcome::Cancelled; }
    }
    if (running) {
      expire_sockets();
      { std::lock_guard<std::mutex> lock(portal_mutex);
        if (portal.outcome == PortalOutcome::None && monotonic_ms() >= expires_at) { portal.outcome = PortalOutcome::TimedOut; requested = false; }
        if (closes_at && monotonic_ms() >= closes_at) requested = false;
      }
      if (!requested) {
        stop_network(); running = false;
        std::lock_guard<std::mutex> lock(portal_mutex); portal.active = portal.starting = false; portal.password.clear(); portal.clients = 0;
        if (portal.outcome == PortalOutcome::None) portal.outcome = PortalOutcome::Cancelled;
      } else {
        dns_tick(); wifi_sta_list_t clients = {}; if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) { std::lock_guard<std::mutex> lock(portal_mutex); portal.clients = clients.num; }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
} // namespace

ProfileSnapshot profile_snapshot() { std::lock_guard<std::mutex> lock(data_mutex); return stored; }
PortalSnapshot portal_snapshot() { std::lock_guard<std::mutex> lock(portal_mutex); return portal; }
WifiFetchSnapshot wifi_fetch_snapshot() { std::lock_guard<std::mutex> lock(portal_mutex); return fetch_state; }
bool wifi_fetch_request(const WifiCredentials* temporary) {
  std::lock_guard<std::mutex> lock(portal_mutex);
  if (portal.active || portal.starting || requested || running || reset_pending() ||
      fetch_state.state == WifiFetchState::Pending || fetch_state.state == WifiFetchState::Running) return false;
  WifiCredentials credentials;
  if (temporary) {
    if (!wifi_credentials_valid(*temporary)) return false;
    credentials = *temporary;
  } else if (!wifi_credentials_load(credentials)) return false;
  fetch_credentials = std::move(credentials);
  fetch_photo_network = -1; fetch_photo_handle.clear(); fetch_photo_from_profile = false;
  fetch_state = {WifiFetchState::Pending, 0, 0, -1, {}};
  return true;
}
bool photo_fetch_request(int network, const WifiCredentials* temporary, const std::string* handle) {
  if (network < 0 || network > 2) return false;
  std::string target;
  const bool from_profile = !handle;
  if (handle) {
    std::string url;
    if (!social_url(network, *handle, url) || url.empty()) return false;
    target = social_handle(network, url);
  } else {
    target = social_handle(network, profile_snapshot().profile.urls[network]);
  }
  if (target.empty()) return false;
  WifiCredentials credentials;
  if (temporary) {
    if (!wifi_credentials_valid(*temporary)) return false;
    credentials = *temporary;
  } else if (!wifi_credentials_load(credentials)) return false;
  std::lock_guard<std::mutex> lock(portal_mutex);
  // Unlike the probe this may be queued from an open setup page; the service
  // loop starts it only after the setup AP has fully stopped.
  if (reset_pending() || fetch_state.state == WifiFetchState::Pending || fetch_state.state == WifiFetchState::Running) return false;
  fetch_credentials = std::move(credentials);
  fetch_photo_network = network; fetch_photo_handle = std::move(target); fetch_photo_from_profile = from_profile;
  fetch_state = {WifiFetchState::Pending, 0, 0, network, {}};
  return true;
}
bool profile_reset_request(std::string& error) { return queue_profile_reset(error); }
ProfileResetSnapshot profile_reset_snapshot() { std::lock_guard<std::mutex> lock(portal_mutex); return reset_state; }
bool services_init(PhoneClockSync clock_sync) {
  sync_clock = std::move(clock_sync);
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat");
  ProfileSnapshot initial;
  if (!partition || partition->address != 0x610000 || partition->size != 0x9E0000) initial.error = "Unexpected storage partition";
  else {
    esp_vfs_littlefs_conf_t config = {}; config.base_path = kRoot; config.partition_label = "ffat"; config.format_if_mount_failed = false; config.dont_mount = false;
    if (esp_vfs_littlefs_register(&config) != ESP_OK) initial.error = "Storage unavailable; provisioning required";
    else { initial.ready = true; struct stat info;
      if (!stat(kRecord, &info)) {
        ProfileSnapshot loaded;
        if (!read_record(kRecord, loaded)) initial.error = "Saved badge is damaged; configure to replace";
        else { initial.profile = std::move(loaded.profile); initial.avatar = std::move(loaded.avatar); initial.revision = 1; }
      }
      else if (errno != ENOENT) { initial.ready = false; initial.error = "Could not inspect saved badge"; }
    }
  }
  { std::lock_guard<std::mutex> lock(data_mutex); stored = std::move(initial); }
  auto net = esp_netif_init(); if (net != ESP_OK && net != ESP_ERR_INVALID_STATE) return false;
  auto event = esp_event_loop_create_default(); if (event != ESP_OK && event != ESP_ERR_INVALID_STATE) return false;
  if (!service_task && xTaskCreate(service_loop, "badge_services", 12288, nullptr, 3, &service_task) != pdPASS) return false;
  return profile_snapshot().ready;
}
bool portal_start(const char* test_password) {
  std::lock_guard<std::mutex> lock(portal_mutex);
  if (reset_pending() || fetch_state.state == WifiFetchState::Pending || fetch_state.state == WifiFetchState::Running) return false;
  if (portal.active || portal.starting || running) return true;
  portal = {}; auto current = profile_snapshot();
  if (!current.ready || !service_task) { portal.error = current.error.empty() ? "Setup is unavailable" : current.error; portal.outcome = PortalOutcome::Error; return false; }
  portal.password = test_password ? test_password : random_hex(6);
  if (portal.password.size() < 12 || portal.password.size() > 32 || !std::all_of(portal.password.begin(), portal.password.end(), alnum)) {
    portal.password.clear(); portal.error = "Invalid temporary setup password"; portal.outcome = PortalOutcome::Error; return false;
  }
  uint8_t mac[6] = {}; esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP); char suffix[13];
  snprintf(suffix, sizeof(suffix), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  portal.ssid = std::string("init-badge-") + suffix; session_nonce = random_hex(16); portal.starting = true; requested = true; return true;
}
void portal_stop() { requested = false; }
void portal_tick() {}
bool profile_clear() {
  // Keep the retained explicit USB diagnostic excluded from setup/reset work.
  std::lock_guard<std::mutex> lock(portal_mutex);
  if (reset_pending() || reset_setup_busy()) return false;
  std::string error; return save_record({}, nullptr, 0, true, error);
}
bool profile_initialize_for_conference() {
  // Keep setup/start and profile writes excluded throughout explicit formatting.
  std::scoped_lock locks(portal_mutex, write_mutex);
  if (reset_pending()) return false;
  if (profile_snapshot().ready) return true;
  if (portal.active || portal.starting || requested || running) return false;
  struct Expected { const char* label; uint8_t type, subtype; uint32_t address, size; };
  static constexpr Expected expected[] = {
    {"nvs", 1, 2, 0x9000, 0x5000}, {"otadata", 1, 0, 0xe000, 0x2000},
    {"app0", 0, 0x10, 0x10000, 0x300000}, {"app1", 0, 0x11, 0x310000, 0x300000},
    {"ffat", 1, 0x81, 0x610000, 0x9e0000}, {"coredump", 1, 3, 0xff0000, 0x10000}
  };
  unsigned count = 0, seen = 0;
  auto it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it) {
    const auto* p = esp_partition_get(it); bool matched = false;
    for (unsigned i = 0; i < 6; ++i) {
      const auto& e = expected[i];
      if (!strcmp(p->label, e.label) && p->type == e.type && p->subtype == e.subtype && p->address == e.address && p->size == e.size && !p->encrypted && !(seen & (1u << i))) {
        seen |= 1u << i; matched = true; break;
      }
    }
    if (!matched) { esp_partition_iterator_release(it); return false; }
    ++count; it = esp_partition_next(it);
  }
  if (count != 6 || seen != 0x3f) return false;
  // An unavailable store receives one final non-destructive mount attempt.
  // A repaired/already prepared filesystem must never be formatted by retry.
  esp_vfs_littlefs_conf_t config = {}; config.base_path = kRoot; config.partition_label = "ffat";
  config.format_if_mount_failed = false; config.dont_mount = false;
  bool mounted = esp_littlefs_mounted("ffat");
  if (!mounted) mounted = esp_vfs_littlefs_register(&config) == ESP_OK;
  if (!mounted) {
    esp_vfs_littlefs_unregister("ffat");
    if (esp_littlefs_format("ffat") != ESP_OK || esp_vfs_littlefs_register(&config) != ESP_OK) return false;
  }
  ProfileSnapshot restored; restored.ready = true; struct stat info;
  if (!stat(kRecord, &info)) {
    ProfileSnapshot loaded;
    if (!read_record(kRecord, loaded)) restored.error = "Saved badge is damaged; configure to replace";
    else { restored.profile = std::move(loaded.profile); restored.avatar = std::move(loaded.avatar); restored.revision = 1; }
  } else if (errno != ENOENT) return false;
  { std::lock_guard<std::mutex> lock(data_mutex); stored = std::move(restored); }
  return true;
}
} // namespace badge

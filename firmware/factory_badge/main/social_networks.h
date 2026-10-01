// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstring>
#include <string>

// The badge holds one social account: a selectable network and its handle, or
// "Other (URL)" holding a profile link. The QR opens the profile URL. Photos
// come from avatar.chan.dev/unavatar providers: the named networks directly,
// and an Other link only when it matches a supported provider's profile form.
// Keep services/avatar's route table equal to Providers.
namespace badge_social {
enum class Rule : unsigned char { LinkedIn, X, GitHub, HuggingFace, YouTube, Url };
struct Network {
    const char* key;   // Stored in the profile record; the provider key for named networks.
    const char* label;
    const char* prefix;
    Rule rule;
    const char* inputs[4]; // Accepted pasted profile-URL prefixes.
};
inline constexpr Network Networks[] = {
    {"linkedin", "LinkedIn", "https://linkedin.com/in/", Rule::LinkedIn, {"https://www.linkedin.com/in/", "https://linkedin.com/in/", "linkedin.com/in/", "www.linkedin.com/in/"}},
    {"x", "X", "https://x.com/", Rule::X, {"https://x.com/", "https://twitter.com/", "https://www.x.com/", "https://www.twitter.com/"}},
    {"github", "GitHub", "https://github.com/", Rule::GitHub, {"https://github.com/", "https://www.github.com/", "github.com/", nullptr}},
    {"huggingface", "Hugging Face", "https://huggingface.co/", Rule::HuggingFace, {"https://huggingface.co/", "huggingface.co/", nullptr, nullptr}},
    {"youtube", "YouTube", "https://youtube.com/@", Rule::YouTube, {"https://www.youtube.com/@", "https://youtube.com/@", "youtube.com/@", "www.youtube.com/@"}},
    {"url", "Other (URL)", "", Rule::Url, {nullptr, nullptr, nullptr, nullptr}},
};
inline constexpr int Count = int(sizeof(Networks) / sizeof(Networks[0]));
// Photo providers the relay serves (unavatar paths), named or recognized in links.
inline constexpr const char* Providers[] = {"linkedin", "x", "github", "huggingface", "youtube",
    "bluesky", "gitlab", "substack", "dribbble", "threads"};
inline constexpr size_t MaxUrl = 180; // Keeps Other QR codes scannable.

inline int find(const std::string& key) {
    for (int i = 0; i < Count; ++i) if (key == Networks[i].key) return i;
    return -1;
}
inline bool alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
inline bool lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
inline std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }
inline bool starts(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
inline bool chars(const std::string& s, size_t min, size_t max, const char* extra, bool alnum_first) {
    if (s.size() < min || s.size() > max || (alnum_first && !alnum(s[0]))) return false;
    for (char c : s) if (!alnum(c) && !std::strchr(extra, c)) return false;
    return true;
}
// A lowercase DNS name: dot-separated labels of letters, digits and inner hyphens.
inline bool domain(const std::string& s, size_t max) {
    if (s.empty() || s.size() > max || s.find('.') == std::string::npos) return false;
    for (size_t start = 0; start <= s.size();) {
        size_t end = s.find('.', start);
        if (end == std::string::npos) end = s.size();
        if (end == start || end - start > 63 || s[start] == '-' || s[end - 1] == '-') return false;
        for (size_t i = start; i < end; ++i) if (!lower_alnum(s[i]) && s[i] != '-') return false;
        start = end + 1;
    }
    return true;
}
inline bool github(const std::string& h) { return chars(h, 1, 39, "-", true) && h.back() != '-' && h.find("--") == std::string::npos; }
inline bool x_handle(const std::string& h) { return chars(h, 1, 15, "_", false); }
inline bool linkedin(const std::string& h) { return chars(h, 1, 100, "-_", false); }
inline bool huggingface(const std::string& h) { return chars(h, 1, 96, "-_.", true); }
inline bool youtube(const std::string& h) { return chars(h, 3, 30, "-_.", false); }
// An Other link: https, a real host, printable ASCII without spaces, quotes or
// angle brackets, and short enough for a readable QR.
inline bool link(const std::string& u) {
    if (!starts(u, "https://") || u.size() > MaxUrl) return false;
    for (unsigned char c : u) if (c <= ' ' || c >= 127 || std::strchr("\"'<>\\`", c)) return false;
    const auto host_end = u.find_first_of("/?#", 8);
    return domain(lower(u.substr(8, host_end == std::string::npos ? std::string::npos : host_end - 8)), 253);
}
inline bool valid(int network, const std::string& h) {
    if (network < 0 || network >= Count) return false;
    switch (Networks[network].rule) {
    case Rule::LinkedIn: return linkedin(h);
    case Rule::X: return x_handle(h);
    case Rule::GitHub: return github(h);
    case Rule::HuggingFace: return huggingface(h);
    case Rule::YouTube: return youtube(h);
    case Rule::Url: return link(h);
    }
    return false;
}
inline std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
// Accepts a bare handle, @handle or one of the network's profile URLs (or, for
// Other, any https link; a bare host gains https://). Returns the stored value,
// or empty when the input is not valid for that network.
inline std::string handle(int network, const std::string& input) {
    if (network < 0 || network >= Count || input.size() > 260) return {};
    std::string h = trim(input);
    const auto& n = Networks[network];
    if (n.rule == Rule::Url) {
        if (starts(lower(h), "http://")) h = "https://" + h.substr(7);
        else if (starts(lower(h), "https://")) h = "https://" + h.substr(8);
        else if (!h.empty()) h = "https://" + h;
        return link(h) ? h : std::string();
    }
    for (auto prefix : n.inputs) if (prefix && starts(h, prefix)) { h.erase(0, std::strlen(prefix)); break; }
    const auto query = h.find_first_of("?#");
    if (query != std::string::npos) h.erase(query);
    if (!h.empty() && h.back() == '/') h.pop_back();
    if (!h.empty() && h.front() == '@') h.erase(0, 1);
    return valid(network, h) ? h : std::string();
}
inline std::string url(int network, const std::string& h) {
    if (!valid(network, h)) return {};
    return Networks[network].rule == Rule::Url ? h : std::string(Networks[network].prefix) + h;
}

// Where a photo can come from: an unavatar provider key, its handle, and a
// label for the QR. An unrecognized Other link has no provider (QR only).
struct PhotoSource { const char* provider = nullptr; std::string handle; std::string label; };
inline PhotoSource recognize(const std::string& link_url) {
    PhotoSource none;
    if (!link(link_url)) return none;
    std::string rest = link_url.substr(8);
    const auto cut = rest.find_first_of("?#");
    if (cut != std::string::npos) rest.erase(cut);
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    const auto slash = rest.find('/');
    std::string host = lower(rest.substr(0, slash));
    const std::string path = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
    if (starts(host, "www.")) host.erase(0, 4);
    none.label = host;
    auto single = [&](const char* prefix) -> std::string { // One path segment after prefix.
        if (!starts(path, prefix)) return {};
        auto h = path.substr(std::strlen(prefix));
        return h.find('/') == std::string::npos ? h : std::string();
    };
    auto source = [](const char* provider, std::string h, const char* label) { return PhotoSource{provider, std::move(h), label}; };
    std::string h;
    if (host == "linkedin.com" && linkedin(h = single("in/"))) return source("linkedin", h, "LinkedIn");
    if ((host == "x.com" || host == "twitter.com") && x_handle(h = single(""))) return source("x", h, "X");
    if (host == "github.com" && github(h = single(""))) return source("github", h, "GitHub");
    if (host == "huggingface.co" && huggingface(h = single(""))) return source("huggingface", h, "Hugging Face");
    if (host == "youtube.com" && youtube(h = single("@"))) return source("youtube", h, "YouTube");
    if (host == "bsky.app" && domain(lower(h = single("profile/")), 253)) return source("bluesky", lower(h), "Bluesky");
    if (host == "gitlab.com" && chars(h = single(""), 2, 64, "-_.", true)) return source("gitlab", h, "GitLab");
    if (host == "dribbble.com" && chars(h = single(""), 2, 32, "-_", true)) return source("dribbble", h, "Dribbble");
    if ((host == "threads.net" || host == "threads.com") && chars(h = single("@"), 1, 30, "._", false)) return source("threads", h, "Threads");
    const std::string tail = ".substack.com";
    if (host.size() > tail.size() && host.compare(host.size() - tail.size(), tail.size(), tail) == 0) {
        h = host.substr(0, host.size() - tail.size());
        if (h.find('.') == std::string::npos && domain(host, 253)) return source("substack", h, "Substack");
    }
    return none;
}
inline PhotoSource photo_source(int network, const std::string& h) {
    if (!valid(network, h)) return {};
    if (Networks[network].rule == Rule::Url) return recognize(h);
    return {Networks[network].key, h, Networks[network].label};
}
} // namespace badge_social

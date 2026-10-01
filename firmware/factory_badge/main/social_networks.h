// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstring>
#include <string>

// The badge holds one social account: a network from this table and a handle.
// The key is also the avatar.chan.dev/unavatar path; the badge QR opens the
// profile URL built from prefix + handle + suffix. Keep services/avatar's route
// table in step with these handle rules.
namespace badge_social {
enum class Rule : unsigned char { GitHub, X, LinkedIn, Bluesky, HuggingFace, YouTube, GitLab, Substack, Dribbble, Threads };
struct Network {
    const char* key;
    const char* label;
    const char* prefix;
    const char* suffix;
    Rule rule;
    const char* inputs[4]; // Accepted pasted profile-URL prefixes.
};
inline constexpr Network Networks[] = {
    {"github", "GitHub", "https://github.com/", "", Rule::GitHub, {"https://github.com/", "https://www.github.com/", "github.com/", nullptr}},
    {"x", "X", "https://x.com/", "", Rule::X, {"https://x.com/", "https://twitter.com/", "https://www.x.com/", "https://www.twitter.com/"}},
    {"linkedin", "LinkedIn", "https://linkedin.com/in/", "", Rule::LinkedIn, {"https://www.linkedin.com/in/", "https://linkedin.com/in/", "linkedin.com/in/", "www.linkedin.com/in/"}},
    {"bluesky", "Bluesky", "https://bsky.app/profile/", "", Rule::Bluesky, {"https://bsky.app/profile/", "bsky.app/profile/", nullptr, nullptr}},
    {"huggingface", "Hugging Face", "https://huggingface.co/", "", Rule::HuggingFace, {"https://huggingface.co/", "huggingface.co/", nullptr, nullptr}},
    {"youtube", "YouTube", "https://youtube.com/@", "", Rule::YouTube, {"https://www.youtube.com/@", "https://youtube.com/@", "youtube.com/@", "www.youtube.com/@"}},
    {"gitlab", "GitLab", "https://gitlab.com/", "", Rule::GitLab, {"https://gitlab.com/", "gitlab.com/", nullptr, nullptr}},
    {"substack", "Substack", "https://", ".substack.com", Rule::Substack, {"https://", nullptr, nullptr, nullptr}},
    {"dribbble", "Dribbble", "https://dribbble.com/", "", Rule::Dribbble, {"https://dribbble.com/", "dribbble.com/", nullptr, nullptr}},
    {"threads", "Threads", "https://threads.net/@", "", Rule::Threads, {"https://www.threads.net/@", "https://threads.net/@", "threads.net/@", "https://www.threads.com/@"}},
};
inline constexpr int Count = int(sizeof(Networks) / sizeof(Networks[0]));

inline int find(const std::string& key) {
    for (int i = 0; i < Count; ++i) if (key == Networks[i].key) return i;
    return -1;
}
inline bool alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
inline bool lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
inline std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }
inline bool chars(const std::string& s, size_t min, size_t max, const char* extra, bool alnum_first) {
    if (s.size() < min || s.size() > max || (alnum_first && !alnum(s[0]))) return false;
    for (char c : s) if (!alnum(c) && !std::strchr(extra, c)) return false;
    return true;
}
// A lowercase DNS name: dot-separated labels of letters, digits and inner hyphens.
inline bool domain(const std::string& s, size_t max) {
    if (s.empty() || s.size() > max || s.find('.') == std::string::npos) return false;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t end = s.find('.', start) == std::string::npos ? s.size() : s.find('.', start);
        const size_t length = end - start;
        if (!length || length > 63 || s[start] == '-' || s[end - 1] == '-') return false;
        for (size_t i = start; i < end; ++i) if (!lower_alnum(s[i]) && s[i] != '-') return false;
        start = end + 1;
    }
    return true;
}
inline bool valid(int network, const std::string& h) {
    if (network < 0 || network >= Count) return false;
    switch (Networks[network].rule) {
    case Rule::GitHub:
        if (!chars(h, 1, 39, "-", true) || h.back() == '-') return false;
        return h.find("--") == std::string::npos;
    case Rule::X: return chars(h, 1, 15, "_", false);
    case Rule::LinkedIn: return chars(h, 1, 100, "-_", false);
    case Rule::Bluesky: return domain(h, 253);
    case Rule::HuggingFace: return chars(h, 1, 96, "-_.", true);
    case Rule::YouTube: return chars(h, 3, 30, "-_.", false);
    case Rule::GitLab: return chars(h, 2, 64, "-_.", true);
    case Rule::Substack: return h.find('.') == std::string::npos && domain(h + ".substack.com", 253);
    case Rule::Dribbble: return chars(h, 2, 32, "-_", true);
    case Rule::Threads: return chars(h, 1, 30, "._", false);
    }
    return false;
}
// Accepts a bare handle, @handle or one of the network's profile URLs; returns
// the stored handle, or empty when the input is not a valid account.
inline std::string handle(int network, const std::string& input) {
    if (network < 0 || network >= Count || input.size() > 260) return {};
    std::string h = input;
    while (!h.empty() && h.front() == ' ') h.erase(0, 1);
    while (!h.empty() && h.back() == ' ') h.pop_back();
    const auto& n = Networks[network];
    for (auto prefix : n.inputs) if (prefix && h.rfind(prefix, 0) == 0) { h.erase(0, std::strlen(prefix)); break; }
    const auto query = h.find_first_of("?#");
    if (query != std::string::npos) h.erase(query);
    if (!h.empty() && h.back() == '/') h.pop_back();
    if (!h.empty() && h.front() == '@') h.erase(0, 1);
    if (n.rule == Rule::Bluesky) {
        h = lower(h);
        if (!h.empty() && h.find('.') == std::string::npos) h += ".bsky.social"; // Default PDS handle.
    }
    if (n.rule == Rule::Substack) {
        h = lower(h);
        const std::string tail = ".substack.com";
        if (h.size() > tail.size() && h.compare(h.size() - tail.size(), tail.size(), tail) == 0) h.erase(h.size() - tail.size());
    }
    return valid(network, h) ? h : std::string();
}
inline std::string url(int network, const std::string& h) {
    if (!valid(network, h)) return {};
    return std::string(Networks[network].prefix) + h + Networks[network].suffix;
}
} // namespace badge_social

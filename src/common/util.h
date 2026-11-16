// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace mz {

inline uint64_t now_ms()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

inline uint32_t now_s()
{
    return static_cast<uint32_t>(time(nullptr));
}

constexpr uint32_t kBroadcast = 0xffffffffu;

inline std::string node_id_str(uint32_t num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "!%08x", num);
    return buf;
}

std::string base64_encode(const uint8_t* data, size_t len, bool url_safe = false);
bool base64_decode(const std::string& text, std::vector<uint8_t>& out);

// Escapes tab/newline/backslash so a string fits in one tab-separated field.
std::string escape_field(const std::string& text);
std::string unescape_field(const std::string& text);

std::vector<std::string> split(const std::string& text, char sep);
std::string trim(const std::string& text);

// Truncates to at most max_bytes without cutting a UTF-8 sequence in half.
std::string utf8_truncate(const std::string& text, size_t max_bytes);

// "12s", "5m", "3h", "2d" relative age; "-" when unknown.
std::string age_str(uint32_t then_s, uint32_t now);

// Great-circle distance in metres and initial bearing in degrees.
double geo_distance_m(double lat1, double lon1, double lat2, double lon2);
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);
std::string distance_str(double metres);
const char* compass_point(double bearing_deg);

}  // namespace mz

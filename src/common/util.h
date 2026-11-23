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

// Compares dotted version numbers such as "0.1.10" and "0.2": below zero,
// zero or above zero as a is older than, the same as or newer than b. A part
// that is missing counts as 0; an empty string is older than everything.
int version_compare(const std::string& a, const std::string& b);

// The version named by "tag_name" in a release description as GitHub's
// releases API gives it ("v0.1.2" becomes "0.1.2"). Empty when there is none
// or it is not made of digits and dots.
std::string release_version(const std::string& json);

// Where this user's own state is kept (what has been read, and the like).
std::string user_state_dir();
// Creates a directory and the ones above it.
void make_dirs(const std::string& path);

// Truncates to at most max_bytes without cutting a UTF-8 sequence in half.
std::string utf8_truncate(const std::string& text, size_t max_bytes);

// "12s", "5m", "3h", "2d" relative age; "-" when unknown.
std::string age_str(uint32_t then_s, uint32_t now);

// Great-circle distance in metres and initial bearing in degrees.
double geo_distance_m(double lat1, double lon1, double lat2, double lon2);
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);
// "850m", "12.3km"; or, in feet and miles, "500ft", "7.67mi".
std::string distance_str(double metres, bool imperial = false);
// A height above sea level, given in metres: "152 m" or "499 ft".
std::string altitude_str(int metres, bool imperial = false);
constexpr double kFootM = 0.3048;
constexpr double kMileM = 1609.344;

// Web Mercator, the projection map tiles use. A position becomes a point in
// the world at a zoom level, in pixels: 256 per tile, x east from 180 W and y
// south from about 85 N. Latitudes beyond the projection's edge are held at it.
constexpr int kTilePx = 256;
void geo_to_world_px(double lat, double lon, int zoom, double& x, double& y);
void world_px_to_geo(double x, double y, int zoom, double& lat, double& lon);
// How many metres one pixel covers at a latitude and zoom.
double metres_per_px(double lat, int zoom);
const char* compass_point(double bearing_deg);

// The charge of this device's battery in percent, as the kernel reports it
// under `root`, or -1 when it has none. Batteries of accessories, such as a
// game controller, are not this device's. `charging` says whether it is charging.
int battery_percent(bool& charging, const std::string& root = "/sys/class/power_supply");

}  // namespace mz

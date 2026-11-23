// SPDX-License-Identifier: GPL-3.0-only
#include "common/util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include <dirent.h>
#include <sys/stat.h>

namespace mz {

std::string base64_encode(const uint8_t* data, size_t len, bool url_safe)
{
    static const char* std_tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char* url_tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* tbl = url_safe ? url_tbl : std_tbl;
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
        const uint32_t v  = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        if (i + 1 < len) {
            out.push_back(tbl[(v >> 6) & 63]);
        } else if (!url_safe) {
            out.push_back('=');
        }
        if (i + 2 < len) {
            out.push_back(tbl[v & 63]);
        } else if (!url_safe) {
            out.push_back('=');
        }
    }
    return out;
}

bool base64_decode(const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t acc = 0;
    int bits     = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') {
            v = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            v = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            v = c - '0' + 52;
        } else if (c == '+' || c == '-') {
            v = 62;
        } else if (c == '/' || c == '_') {
            v = 63;
        } else if (c == '=' || c == ' ' || c == '\n' || c == '\r') {
            continue;
        } else {
            return false;
        }
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
        }
    }
    return true;
}

std::string escape_field(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

std::string unescape_field(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            out.push_back(n == 't' ? '\t' : n == 'n' ? '\n' : n);
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char sep)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t pos = text.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string trim(const std::string& text)
{
    size_t a = 0;
    size_t b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\n' || text[a] == '\r')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\n' || text[b - 1] == '\r')) --b;
    return text.substr(a, b - a);
}

int version_compare(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return a.empty() == b.empty() ? 0 : a.empty() ? -1 : 1;
    const std::vector<std::string> pa = split(a, '.'), pb = split(b, '.');
    for (size_t i = 0; i < pa.size() || i < pb.size(); ++i) {
        const unsigned long x = i < pa.size() ? strtoul(pa[i].c_str(), nullptr, 10) : 0;
        const unsigned long y = i < pb.size() ? strtoul(pb[i].c_str(), nullptr, 10) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::string release_version(const std::string& json)
{
    const size_t key = json.find("\"tag_name\"");
    if (key == std::string::npos) return {};
    const size_t open = json.find('"', json.find(':', key) == std::string::npos ? key : json.find(':', key));
    if (open == std::string::npos) return {};
    const size_t close = json.find('"', open + 1);
    if (close == std::string::npos) return {};
    std::string tag = json.substr(open + 1, close - open - 1);
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    // Shown on screen and kept in a file: digits and dots only.
    if (tag.empty() || tag.size() > 20 || tag.find_first_not_of("0123456789.") != std::string::npos) return {};
    return tag;
}

std::string user_state_dir()
{
    std::string base;
    if (const char* x = getenv("XDG_STATE_HOME"); x && x[0]) {
        base = x;
    } else if (const char* h = getenv("HOME"); h && h[0]) {
        base = std::string(h) + "/.local/state";
    } else {
        base = "/tmp";
    }
    return base + "/meshzero";
}

void make_dirs(const std::string& path)
{
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
    }
}

std::string utf8_truncate(const std::string& text, size_t max_bytes)
{
    if (text.size() <= max_bytes) return text;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<uint8_t>(text[end]) & 0xC0) == 0x80) --end;
    return text.substr(0, end);
}

std::string age_str(uint32_t then_s, uint32_t now)
{
    if (then_s == 0) return "-";
    const uint32_t d = now > then_s ? now - then_s : 0;
    char buf[16];
    if (d < 60) {
        snprintf(buf, sizeof(buf), "%us", d);
    } else if (d < 3600) {
        snprintf(buf, sizeof(buf), "%um", d / 60);
    } else if (d < 86400) {
        snprintf(buf, sizeof(buf), "%uh", d / 3600);
    } else {
        snprintf(buf, sizeof(buf), "%ud", d / 86400);
    }
    return buf;
}

static double rad(double deg)
{
    return deg * M_PI / 180.0;
}

double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double dlat = rad(lat2 - lat1);
    const double dlon = rad(lon2 - lon1);
    const double a =
        std::sin(dlat / 2) * std::sin(dlat / 2) + std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 6371000.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    const double dlon = rad(lon2 - lon1);
    const double y    = std::sin(dlon) * std::cos(rad(lat2));
    const double x    = std::cos(rad(lat1)) * std::sin(rad(lat2)) - std::sin(rad(lat1)) * std::cos(rad(lat2)) * std::cos(dlon);
    double deg        = std::atan2(y, x) * 180.0 / M_PI;
    if (deg < 0) deg += 360.0;
    return deg;
}

void geo_to_world_px(double lat, double lon, int zoom, double& x, double& y)
{
    const double size = static_cast<double>(kTilePx) * static_cast<double>(1u << zoom);
    lat               = std::max(-85.05112878, std::min(85.05112878, lat));
    const double s    = std::sin(lat * M_PI / 180.0);
    x                 = (lon + 180.0) / 360.0 * size;
    y                 = (0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * size;
}

void world_px_to_geo(double x, double y, int zoom, double& lat, double& lon)
{
    const double size = static_cast<double>(kTilePx) * static_cast<double>(1u << zoom);
    lon               = x / size * 360.0 - 180.0;
    lat               = std::atan(std::sinh(M_PI * (1 - 2 * y / size))) * 180.0 / M_PI;
}

double metres_per_px(double lat, int zoom)
{
    return 156543.03392804097 * std::cos(lat * M_PI / 180.0) / static_cast<double>(1u << zoom);
}

bool tilt_heading(const double mag[3], const double acc[3], double& deg)
{
    const auto cross = [](const double a[3], const double b[3], double out[3]) {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    };
    const auto unit = [](double v[3]) {
        const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 1e-9) return false;
        for (int i = 0; i < 3; ++i) v[i] /= len;
        return true;
    };
    double up[3] = {acc[0], acc[1], acc[2]};
    double east[3], north[3];
    if (!unit(up)) return false;
    // The field points north and into the ground (or out of it): across it and
    // up lies east, and across up and east lies north.
    cross(mag, up, east);
    const double field = std::sqrt(mag[0] * mag[0] + mag[1] * mag[1] + mag[2] * mag[2]);
    const double level = std::sqrt(east[0] * east[0] + east[1] * east[1] + east[2] * east[2]);
    if (field < 1e-9 || level < 0.05 * field || !unit(east)) return false;
    cross(up, east, north);
    // The top of the screen, unless that points at the sky or the ground: then
    // the way the back of the device faces.
    if (std::fabs(up[1]) < 0.8) {
        deg = std::atan2(east[1], north[1]) * 180.0 / M_PI;
    } else {
        deg = std::atan2(-east[2], -north[2]) * 180.0 / M_PI;
    }
    if (deg < 0) deg += 360.0;
    return true;
}

std::string altitude_str(int metres, bool imperial)
{
    if (!imperial) return std::to_string(metres) + " m";
    return std::to_string(std::lround(metres / kFootM)) + " ft";
}

std::string distance_str(double metres, bool imperial)
{
    char buf[24];
    if (imperial) {
        const double miles = metres / kMileM;
        if (metres < 0.25 * kMileM - 0.5) {
            snprintf(buf, sizeof(buf), "%.0fft", metres / kFootM);
        } else if (miles < 10) {
            // Up to two decimals, without zeros at the end: "0.25mi", "2.5mi", "5mi".
            snprintf(buf, sizeof(buf), "%.2f", miles);
            std::string text = buf;
            while (text.back() == '0') text.pop_back();
            if (text.back() == '.') text.pop_back();
            return text + "mi";
        } else if (miles < 100) {
            snprintf(buf, sizeof(buf), "%.1fmi", miles);
        } else {
            snprintf(buf, sizeof(buf), "%.0fmi", miles);
        }
        return buf;
    }
    if (metres < 1000) {
        snprintf(buf, sizeof(buf), "%.0fm", metres);
    } else if (metres < 100000) {
        snprintf(buf, sizeof(buf), "%.1fkm", metres / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%.0fkm", metres / 1000.0);
    }
    return buf;
}

const char* compass_point(double bearing_deg)
{
    static const char* pts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return pts[static_cast<int>((bearing_deg + 22.5) / 45.0) & 7];
}

int battery_percent(bool& charging, const std::string& root)
{
    charging = false;
    DIR* dir = opendir(root.c_str());
    if (!dir) return -1;
    std::vector<std::string> names;
    while (const dirent* e = readdir(dir)) {
        if (e->d_name[0] != '.') names.push_back(e->d_name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    const auto read = [&root](const std::string& name, const char* file) {
        std::ifstream in(root + "/" + name + "/" + file);
        std::string line;
        std::getline(in, line);
        return trim(line);
    };
    for (const std::string& name : names) {
        if (read(name, "type") != "Battery" || read(name, "scope") == "Device") continue;
        const std::string text = read(name, "capacity");
        char* end              = nullptr;
        const long percent     = strtol(text.c_str(), &end, 10);
        if (text.empty() || *end != 0 || percent < 0 || percent > 100) continue;
        charging = read(name, "status") == "Charging";
        return static_cast<int>(percent);
    }
    return -1;
}

}  // namespace mz

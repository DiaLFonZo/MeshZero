// SPDX-License-Identifier: GPL-3.0-only
#include "common/util.h"

#include <cmath>

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

std::string distance_str(double metres)
{
    char buf[24];
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

}  // namespace mz

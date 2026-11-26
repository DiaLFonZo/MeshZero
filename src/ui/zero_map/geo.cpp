// SPDX-License-Identifier: GPL-3.0-only
#include "ui/zero_map/geo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mz {

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

}  // namespace mz

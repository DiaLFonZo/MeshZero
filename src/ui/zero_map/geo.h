// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>

namespace mz {
constexpr double kFootM = 0.3048;
constexpr double kMileM = 1609.344;
constexpr int kTilePx = 256;
void geo_to_world_px(double lat, double lon, int zoom, double& x, double& y);
void world_px_to_geo(double x, double y, int zoom, double& lat, double& lon);
double metres_per_px(double lat, int zoom);
bool tilt_heading(const double mag[3], const double acc[3], double& deg);
std::string distance_str(double metres, bool imperial);
}  // namespace mz

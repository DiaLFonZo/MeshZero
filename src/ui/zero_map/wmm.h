// SPDX-License-Identifier: GPL-3.0-only
// The World Magnetic Model: which way a compass needle points, anywhere.
#pragma once

namespace mz {

struct WmmRow {
    int n, m;
    double g, h, dg, dh;
};
extern const double kWmmEpoch;
extern const int kWmmDegree;
extern const WmmRow kWmm[];
extern const int kWmmRows;

// How far east of true north magnetic north lies at a place, in degrees
// (negative: west), at a time given as a year with decimals. The model covers
// five years from kWmmEpoch; a time outside them is held at the nearest end.
double magnetic_declination(double lat, double lon, double year, double altitude_km = 0);

// The year with decimals of the system clock, as magnetic_declination takes it.
double decimal_year();

}  // namespace mz

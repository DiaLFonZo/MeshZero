// SPDX-License-Identifier: GPL-3.0-only
#include "ui/zero_map/wmm.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace mz {

double magnetic_declination(double lat, double lon, double year, double altitude_km)
{
    constexpr int kMax    = 13;
    constexpr double kA   = 6378.137;      // WGS 84 equatorial radius, km
    constexpr double kB   = 6356.7523142;  // and polar
    constexpr double kRef = 6371.2;        // the model's reference radius
    if (kWmmDegree >= kMax) return 0;
    const double dt = std::max(0.0, std::min(5.0, year - kWmmEpoch));
    lat             = std::max(-89.99, std::min(89.99, lat));  // the sums divide by the cosine

    // From the position on the ellipsoid to one on a sphere.
    const double phi = lat * M_PI / 180.0, lambda = lon * M_PI / 180.0;
    const double e2  = 1 - (kB * kB) / (kA * kA);
    const double nu  = kA / std::sqrt(1 - e2 * std::sin(phi) * std::sin(phi));
    const double p   = (nu + altitude_km) * std::cos(phi);
    const double z   = (nu * (1 - e2) + altitude_km) * std::sin(phi);
    const double r   = std::sqrt(p * p + z * z);
    const double phi_c = std::asin(z / r);
    const double ct = std::sin(phi_c), st = std::cos(phi_c);  // of the colatitude

    // Schmidt semi-normalised associated Legendre functions and their
    // derivatives with respect to colatitude.
    double P[kMax][kMax] = {}, dP[kMax][kMax] = {};
    P[0][0] = 1;
    for (int n = 1; n <= kWmmDegree; ++n) {
        const double k = n == 1 ? 1.0 : std::sqrt((2.0 * n - 1) / (2.0 * n));
        P[n][n]        = k * st * P[n - 1][n - 1];
        dP[n][n]       = k * (st * dP[n - 1][n - 1] + ct * P[n - 1][n - 1]);
        for (int m = 0; m < n; ++m) {
            const double before  = n >= 2 ? std::sqrt((n - 1.0) * (n - 1.0) - m * m) : 0;
            const double scale   = std::sqrt(static_cast<double>(n) * n - m * m);
            const double p2      = n >= 2 ? P[n - 2][m] : 0;
            const double dp2     = n >= 2 ? dP[n - 2][m] : 0;
            P[n][m]  = ((2 * n - 1) * ct * P[n - 1][m] - before * p2) / scale;
            dP[n][m] = ((2 * n - 1) * (ct * dP[n - 1][m] - st * P[n - 1][m]) - before * dp2) / scale;
        }
    }

    // The field on the sphere: north, east and down.
    double north = 0, east = 0, down = 0;
    for (int i = 0; i < kWmmRows; ++i) {
        const WmmRow& c    = kWmm[i];
        const double g     = c.g + c.dg * dt, h = c.h + c.dh * dt;
        const double reach = std::pow(kRef / r, c.n + 2);
        const double cm = std::cos(c.m * lambda), sm = std::sin(c.m * lambda);
        north += reach * (g * cm + h * sm) * dP[c.n][c.m];
        east += reach * c.m * (g * sm - h * cm) * P[c.n][c.m] / st;
        down -= reach * (c.n + 1) * (g * cm + h * sm) * P[c.n][c.m];
    }
    // Back to the ellipsoid, where north is tilted by the difference in latitude.
    const double psi = phi_c - phi;
    const double x   = north * std::cos(psi) - down * std::sin(psi);
    return std::atan2(east, x) * 180.0 / M_PI;
}

double decimal_year()
{
    const time_t now = time(nullptr);
    tm t{};
    gmtime_r(&now, &t);
    const int year  = t.tm_year + 1900;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return year + (t.tm_yday + t.tm_hour / 24.0) / (leap ? 366.0 : 365.0);
}

}  // namespace mz

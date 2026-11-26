// SPDX-License-Identifier: GPL-3.0-only
#include "ui/zero_map/compass.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <dirent.h>
#include <unistd.h>

#include "common/util.h"
#include "ui/zero_map/geo.h"

namespace mz::compass {
namespace {

constexpr int kSampleMs = 80;
// How much of the newest reading goes into the heading that is given out.
constexpr double kSmooth = 0.35;
// Turned every way, each direction of the magnetometer swings between plus and
// minus the strength of the earth's field, which is 0.22 gauss at its weakest.
constexpr double kFullSwingGauss = 0.4;

// How the sensors sit in the CardputerZero: for the device's x (right of the
// screen), y (its top) and z (out of it), which of the sensor's directions
// that is, 0 to 2, and whether it points the other way. Measured on a device:
// the magnetometer is turned half round against the accelerometer.
// MESHZERO_COMPASS_AXES overrides both for trying out, as "+y+x-z +y-x+z":
// magnetometer first.
struct Axes {
    int from[3];
    int sign[3];
};
Axes g_mag_axes = {{1, 0, 2}, {1, 1, -1}};
Axes g_acc_axes = {{1, 0, 2}, {1, -1, 1}};

bool parse_axes(const std::string& text, Axes& out)
{
    if (text.size() != 6) return false;
    Axes a{};
    for (int i = 0; i < 3; ++i) {
        const char s = text[static_cast<size_t>(i) * 2], c = text[static_cast<size_t>(i) * 2 + 1];
        if ((s != '+' && s != '-') || c < 'x' || c > 'z') return false;
        a.sign[i] = s == '+' ? 1 : -1;
        a.from[i] = c - 'x';
    }
    out = a;
    return true;
}

std::mutex g_mutex;
bool g_found_looked = false;
std::string g_mag_dir, g_acc_dir;  // empty: no such sensor
double g_mag_scale = 1;            // gauss per count

bool g_loaded     = false;
bool g_calibrated = false;
bool g_follow     = true;
double g_offset[3] = {0, 0, 0};  // in counts, taken off
double g_gain[3]   = {1, 1, 1};  // then multiplied by

bool g_calibrating = false;
double g_low[3], g_high[3];
bool g_any = false;

bool g_have     = false;  // a heading has been worked out
double g_sin = 0, g_cos = 0;

std::atomic<int> g_run{0};  // goes up with each start and stop; odd while running

bool read_number(const std::string& path, double& out)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char buf[64] = {};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;
    char* end = nullptr;
    out       = strtod(buf, &end);
    return end != buf;
}

bool read_axes(const std::string& dir, const char* kind, double out[3])
{
    static const char* const names[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        if (!read_number(dir + "/in_" + kind + "_" + names[i] + "_raw", out[i])) return false;
    }
    return true;
}

void find_locked()
{
    if (g_found_looked) return;
    g_found_looked = true;
    // MESHZERO_IIO_DIR points at a stand-in for the kernel's folder, for trying out.
    const char* env        = getenv("MESHZERO_IIO_DIR");
    const std::string root = env && env[0] ? env : "/sys/bus/iio/devices";
    if (DIR* d = opendir(root.c_str())) {
        while (const dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            const std::string dir = root + "/" + e->d_name;
            double v[3];
            if (g_mag_dir.empty() && read_axes(dir, "magn", v)) g_mag_dir = dir;
            if (g_acc_dir.empty() && read_axes(dir, "accel", v)) g_acc_dir = dir;
        }
        closedir(d);
    }
    if (!g_mag_dir.empty() && (!read_number(g_mag_dir + "/in_magn_scale", g_mag_scale) || g_mag_scale <= 0)) {
        g_mag_scale = 1;
    }
    if (const char* axes = getenv("MESHZERO_COMPASS_AXES"); axes && axes[0]) {
        std::istringstream in(axes);
        std::string mag, acc;
        in >> mag >> acc;
        parse_axes(mag, g_mag_axes);
        parse_axes(acc, g_acc_axes);
    }
}

std::string state_file()
{
    return user_state_dir() + "/compass";
}

void load_locked()
{
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream in(state_file());
    std::string key;
    while (in >> key) {
        if (key == "follow") {
            int v = 1;
            in >> v;
            g_follow = v != 0;
        } else if (key == "offset") {
            in >> g_offset[0] >> g_offset[1] >> g_offset[2];
            g_calibrated = !in.fail();
        } else if (key == "gain") {
            in >> g_gain[0] >> g_gain[1] >> g_gain[2];
        } else {
            break;
        }
    }
    for (double& g : g_gain) {
        if (!(g > 0.2 && g < 5)) g = 1;
    }
}

void save_locked()
{
    make_dirs(user_state_dir());
    std::ofstream out(state_file() + ".tmp", std::ios::trunc);
    if (!out) return;
    out << "follow " << (g_follow ? 1 : 0) << '\n';
    if (g_calibrated) {
        out << "offset " << g_offset[0] << ' ' << g_offset[1] << ' ' << g_offset[2] << '\n'
            << "gain " << g_gain[0] << ' ' << g_gain[1] << ' ' << g_gain[2] << '\n';
    }
    out.close();
    rename((state_file() + ".tmp").c_str(), state_file().c_str());
}

int progress_locked()
{
    if (!g_any) return 0;
    double least = 1e9;
    for (int i = 0; i < 3; ++i) least = std::min(least, (g_high[i] - g_low[i]) * g_mag_scale);
    return static_cast<int>(std::max(0.0, std::min(100.0, least / kFullSwingGauss * 100.0)));
}

void sample()
{
    std::string mag_dir, acc_dir;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        mag_dir = g_mag_dir;
        acc_dir = g_acc_dir;
    }
    double raw_mag[3], raw_acc[3];
    if (!read_axes(mag_dir, "magn", raw_mag) || !read_axes(acc_dir, "accel", raw_acc)) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_calibrating) {
        for (int i = 0; i < 3; ++i) {
            g_low[i]  = g_any ? std::min(g_low[i], raw_mag[i]) : raw_mag[i];
            g_high[i] = g_any ? std::max(g_high[i], raw_mag[i]) : raw_mag[i];
        }
        g_any = true;
    }
    if (!g_calibrated) return;
    double mag[3], acc[3], fixed[3];
    for (int i = 0; i < 3; ++i) fixed[i] = (raw_mag[i] - g_offset[i]) * g_gain[i];
    for (int i = 0; i < 3; ++i) {
        mag[i] = fixed[g_mag_axes.from[i]] * g_mag_axes.sign[i];
        acc[i] = raw_acc[g_acc_axes.from[i]] * g_acc_axes.sign[i];
    }
    double deg = 0;
    if (!tilt_heading(mag, acc, deg)) return;
    const double s = std::sin(deg * M_PI / 180.0), c = std::cos(deg * M_PI / 180.0);
    g_sin  = g_have ? g_sin + (s - g_sin) * kSmooth : s;
    g_cos  = g_have ? g_cos + (c - g_cos) * kSmooth : c;
    g_have = true;
}

void run(int mine)
{
    while (g_run.load() == mine) {
        sample();
        usleep(kSampleMs * 1000);
    }
}

}  // namespace

bool available()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    find_locked();
    return !g_mag_dir.empty() && !g_acc_dir.empty();
}

void start()
{
    if (!available() || (g_run.load() & 1)) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        load_locked();
        g_have = false;
    }
    std::thread(run, ++g_run).detach();
}

void stop()
{
    if (g_run.load() & 1) ++g_run;
}

bool heading(double& deg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_have || !g_calibrated) return false;
    deg = std::atan2(g_sin, g_cos) * 180.0 / M_PI;
    if (deg < 0) deg += 360.0;
    return true;
}

bool calibrated()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_calibrated;
}

void calibrate_begin()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_calibrating = true;
        g_any         = false;
    }
    start();
}

int calibrate_progress()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_calibrating ? progress_locked() : 0;
}

bool calibrate_end()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool done = g_calibrating && progress_locked() >= 100;
    g_calibrating   = false;
    if (!done) return false;
    load_locked();
    double swing[3], mean = 0;
    for (int i = 0; i < 3; ++i) {
        g_offset[i] = (g_low[i] + g_high[i]) / 2;
        swing[i]    = g_high[i] - g_low[i];
        mean += swing[i] / 3;
    }
    // Metal nearby stretches one direction more than another; evened out here.
    for (int i = 0; i < 3; ++i) g_gain[i] = mean / swing[i];
    g_calibrated = true;
    g_have       = false;
    save_locked();
    return true;
}

void calibrate_cancel()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_calibrating = false;
}

bool follow()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_follow;
}

void set_follow(bool on)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    g_follow = on;
    save_locked();
}

}  // namespace mz::compass

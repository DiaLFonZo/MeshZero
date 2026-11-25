// SPDX-License-Identifier: GPL-3.0-only
// Battery level and time left.
//
// Zero: the bq27220 fuel gauge measures the real current and gives its own
// time-to-empty, so that is used as is. A board with only a percentage (the
// ADV) gets an estimate from how fast the percentage drops: samples taken
// while running on battery, over the last hour at most, reset when charging.
#include "ui/battery.h"

#include <deque>
#include <fstream>

#include "common/util.h"
#include "lvgl.h"

#ifndef MESHCARD_ESP32
#include <dirent.h>
#endif

namespace mz {
namespace {

constexpr uint64_t kReadEveryMs  = 1000;  // also how fast plugging in wakes the screen
constexpr uint64_t kSampleMs     = 60 * 1000;
constexpr uint64_t kWindowMs     = 60 * 60 * 1000;
constexpr uint64_t kMinSpanMs    = 8 * 60 * 1000;  // before that: no estimate
constexpr int kMinDropPercent    = 2;

struct Raw {
    bool ok       = false;
    int percent   = 0;
    bool charging = false;
    long empty_s  = -1;  // from a fuel gauge, -1 when it has none
};

#ifndef MESHCARD_ESP32
bool read_long(const std::string& path, long& out)
{
    std::ifstream in(path);
    return static_cast<bool>(in >> out);
}

// The whole first line: a status can be two words ("Not charging", plugged in and full);
// reading one word gave "Not", so a full Zero never counted as plugged (2026-11-20).
std::string read_word(const std::string& path)
{
    std::ifstream in(path);
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// First power_supply of type "Battery" (the Zero's is bq27220-0).
std::string find_battery()
{
    const char* base = "/sys/class/power_supply";
    DIR* d = opendir(base);
    if (!d) return "";
    std::string found;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string dir = std::string(base) + "/" + e->d_name;
        if (read_word(dir + "/type") == "Battery") {
            found = dir;
            break;
        }
    }
    closedir(d);
    return found;
}

Raw read_raw()
{
    static std::string dir;
    static uint64_t looked_ms = 0;
    Raw r;
    if (dir.empty()) {
        const uint64_t now = now_ms();
        if (looked_ms != 0 && now - looked_ms < 60000) return r;
        looked_ms = now;
        dir       = find_battery();
        if (dir.empty()) return r;
    }
    long cap = -1;
    if (!read_long(dir + "/capacity", cap) || cap < 0) return r;
    const std::string status = read_word(dir + "/status");
    r.ok       = true;
    r.percent  = cap > 100 ? 100 : static_cast<int>(cap);
    // Plugged in and full reads "Not charging" on the Zero, so it counts as plugged too.
    r.charging = status == "Charging" || status == "Full" || status == "Not charging";
    long tte   = -1;
    if (!r.charging && read_long(dir + "/time_to_empty_now", tte) && tte > 0) r.empty_s = tte;
    return r;
}
#else
}  // namespace
// The ESP32 build supplies the board's battery reading (percent 0-100).
extern "C" __attribute__((weak)) bool mc_platform_battery(int* percent, bool* charging)
{
    (void)percent;
    (void)charging;
    return false;
}
namespace {
Raw read_raw()
{
    Raw r;
    int p   = 0;
    bool ch = false;
    if (mc_platform_battery(&p, &ch)) {
        r.ok       = true;
        r.percent  = p < 0 ? 0 : p > 100 ? 100 : p;
        r.charging = ch;
    }
    return r;
}
#endif

struct Sample {
    uint64_t ms;
    int percent;
};

// The drain rate learned on earlier runs (percent per minute), kept on disk so
// the time left shows as soon as it comes off the charger or restarts, instead
// of only after a new count of several minutes.
constexpr uint64_t kLearnSpanMs  = 20 * 60 * 1000;
constexpr int kLearnDropPercent  = 3;
constexpr uint64_t kSaveEveryMs  = 10 * 60 * 1000;

std::string rate_path()
{
    return user_state_dir() + "/battery.rate";
}

double& saved_rate()
{
    static double rate  = -1;
    static bool loaded  = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in(rate_path());
        if (!(in >> rate) || rate <= 0 || rate > 5) rate = -1;
    }
    return rate;
}

void learn_rate(double per_min, uint64_t now)
{
    static uint64_t saved_ms = 0;
    double& rate = saved_rate();
    if (saved_ms != 0 && now - saved_ms < kSaveEveryMs) return;
    saved_ms = now;
    rate     = rate > 0 ? 0.5 * rate + 0.5 * per_min : per_min;
    make_dirs(user_state_dir());
    std::ofstream out(rate_path(), std::ios::trunc);
    out << rate << '\n';
}

}  // namespace

BatteryInfo battery_info()
{
    static BatteryInfo last;
    static uint64_t read_ms = 0;
    static std::deque<Sample> samples;
    const uint64_t now = now_ms();
    if (read_ms != 0 && now - read_ms < kReadEveryMs) return last;
    read_ms = now;

    const Raw r = read_raw();
    BatteryInfo b;
    b.known    = r.ok;
    b.percent  = r.percent;
    b.charging = r.charging;
    if (!r.ok || r.charging) {
        samples.clear();  // a new count starts when it comes off the charger
    } else if (r.empty_s > 0) {
        b.minutes_left = static_cast<int>(r.empty_s / 60);
    } else {
        if (samples.empty() || now - samples.back().ms >= kSampleMs) samples.push_back({now, r.percent});
        while (!samples.empty() && now - samples.front().ms > kWindowMs) samples.pop_front();
        if (samples.size() >= 2) {
            const Sample& a = samples.front();
            const uint64_t span = now - a.ms;
            const int drop      = a.percent - r.percent;
            if (span >= kMinSpanMs && drop >= kMinDropPercent) {
                const double per_min = drop / (span / 60000.0);
                b.minutes_left       = static_cast<int>(r.percent / per_min);
                if (span >= kLearnSpanMs && drop >= kLearnDropPercent) learn_rate(per_min, now);
            }
        }
        // No count of its own yet: use what earlier runs measured.
        if (b.minutes_left < 0 && saved_rate() > 0) b.minutes_left = static_cast<int>(r.percent / saved_rate());
    }
    last = b;
    return b;
}

std::string battery_text(const BatteryInfo& b)
{
    if (!b.known) return "";
    std::string s;
    if (b.charging) {
        s = LV_SYMBOL_CHARGE;
    } else {
        s = b.percent > 80   ? LV_SYMBOL_BATTERY_FULL
            : b.percent > 55 ? LV_SYMBOL_BATTERY_3
            : b.percent > 30 ? LV_SYMBOL_BATTERY_2
            : b.percent > 10 ? LV_SYMBOL_BATTERY_1
                             : LV_SYMBOL_BATTERY_EMPTY;
    }
    s += std::to_string(b.percent) + "%";
    if (!b.charging && b.minutes_left >= 0) {
        const int h = b.minutes_left / 60, m = b.minutes_left % 60;
        s += " \xE2\x86\x93";  // ↓ = time left
        s += h > 0 ? std::to_string(h) + "h" + (m < 10 ? "0" : "") + std::to_string(m) + "m" : std::to_string(m) + "m";
    }
    return s;
}

}  // namespace mz

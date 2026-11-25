// SPDX-License-Identifier: GPL-3.0-only
#include "common/alert.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if !defined(MESHCARD_ESP32)
#include <sys/wait.h>
#else
#include <Arduino.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

namespace mz {
namespace {

constexpr int kRate = 22050;

const char* const kNames[kAlertSoundCount] = {"Chirp", "Beeps", "Bell", "Siren"};

void tone(std::vector<int16_t>& out, double f0, double f1, double secs, double amp, bool decay = false)
{
    const int n    = static_cast<int>(secs * kRate);
    const int fade = kRate / 200;  // 5 ms edges, no clicks
    double phase   = 0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double f = f0 + (f1 - f0) * t;
        phase += 2 * M_PI * f / kRate;
        double a = amp;
        if (decay) a *= std::exp(-4.0 * t);
        if (i < fade) a *= static_cast<double>(i) / fade;
        if (n - i < fade) a *= static_cast<double>(n - i) / fade;
        double v = std::sin(phase);
        if (decay) v = 0.7 * v + 0.3 * std::sin(2 * phase);
        out.push_back(static_cast<int16_t>(v * a * 32767));
    }
}

void gap(std::vector<int16_t>& out, double secs)
{
    out.insert(out.end(), static_cast<size_t>(secs * kRate), 0);
}

std::vector<int16_t> make_sound(int sound)
{
    std::vector<int16_t> s;
    switch (sound) {
        case 1:  // Beeps: three short high beeps, carry well from a small speaker
            for (int i = 0; i < 3; ++i) {
                tone(s, 2000, 2000, 0.10, 0.9);
                gap(s, 0.08);
            }
            break;
        case 2:  // Bell: one ding that fades out
            tone(s, 880, 880, 0.9, 0.9, true);
            break;
        case 3:  // Siren: two rising sweeps, the loudest choice
            tone(s, 800, 2200, 0.45, 0.95);
            tone(s, 800, 2200, 0.45, 0.95);
            break;
        default:  // Chirp: two rising notes
            tone(s, 1200, 1200, 0.12, 0.85);
            gap(s, 0.05);
            tone(s, 1800, 1800, 0.14, 0.85);
            break;
    }
    return s;
}

void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

bool write_wav(const std::string& path, const std::vector<int16_t>& pcm)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t data = static_cast<uint32_t>(pcm.size() * 2);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, kRate);
    put32(f, kRate * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, data);
    fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
    return true;
}

}  // namespace

const char* alert_sound_name(int index)
{
    return index >= 0 && index < kAlertSoundCount ? kNames[index] : kNames[0];
}

std::string alert_conf_path()
{
#if defined(MESHCARD_ESP32)
    return "/littlefs/mc/cfg/alert.conf";
#endif
    const char* home = getenv("HOME");
    return std::string(home && *home ? home : "/home/pi") + "/.config/meshcard/alert.conf";
}

std::string alert_open_chat_path()
{
    return "/tmp/meshcard-open-chat";
}

bool alert_chat_is_open(bool dm, uint32_t id)
{
    FILE* f = fopen(alert_open_chat_path().c_str(), "r");
    if (!f) return false;
    int pid = 0, fdm = 0;
    unsigned fid = 0;
    const bool got = fscanf(f, "%d %d %u", &pid, &fdm, &fid) == 3;
    fclose(f);
    if (!got || pid <= 0) return false;
    char proc[32];
    snprintf(proc, sizeof(proc), "/proc/%d", pid);
    struct stat st{};
    if (stat(proc, &st) != 0) return false;  // the app has closed
    return (fdm != 0) == dm && fid == id;
}

AlertConf alert_conf_load()
{
    AlertConf c;
    FILE* f = fopen(alert_conf_path().c_str(), "r");
    if (!f) return c;
    char key[32];
    int value;
    while (fscanf(f, " %31[^=]=%d", key, &value) == 2) {
        if (!strcmp(key, "on")) c.on = value != 0;
        if (!strcmp(key, "all")) c.all = value != 0;
        if (!strcmp(key, "volume")) c.volume = value < 10 ? 10 : value > 100 ? 100 : value;
        if (!strcmp(key, "sound")) c.sound = value >= 0 && value < kAlertSoundCount ? value : 0;
        if (!strcmp(key, "repeat")) c.repeat = value != 0;
    }
    fclose(f);
    return c;
}

bool alert_conf_save(const AlertConf& c)
{
    const std::string path = alert_conf_path();
    const std::string dir  = path.substr(0, path.rfind('/'));
    mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "on=%d\nall=%d\nvolume=%d\nsound=%d\nrepeat=%d\n", c.on ? 1 : 0, c.all ? 1 : 0, c.volume, c.sound,
            c.repeat ? 1 : 0);
    fclose(f);
    return true;
}

#if defined(MESHCARD_ESP32)
}  // namespace mz
// Cardputer ADV: the firmware plays it on the speaker (meshtastic-firmware/src/meshcard/LocalUiAPI.cpp).
extern "C" void mc_platform_ring(int sound, int volume);
namespace mz {
void alert_ring(const AlertConf& c)
{
    (void)&write_wav;
    (void)&make_sound;
    mc_platform_ring(c.sound, c.volume);
}

// Same timing as meshcard-alertd's Alarm (meshcard/src/alert/main.cpp).
namespace {
constexpr uint32_t kRingEveryMs = 5000;
constexpr uint32_t kGiveUpMs    = 3 * 60 * 1000;
struct EspAlarm {
    bool active      = false;
    uint32_t started = 0;
    uint32_t next    = 0;
    AlertConf conf;
} g_alarm;
}  // namespace

void alert_message(bool dm)
{
    const AlertConf c = alert_conf_load();
    if (!c.on || !(dm || c.all)) return;
    const uint32_t t = millis();
    if (!g_alarm.active) g_alarm.started = t;
    g_alarm.active = true;
    g_alarm.conf   = c;
    g_alarm.next   = t;  // ring now, also for a second message during a ring
}

void alert_key()
{
    g_alarm.active = false;
}

void alert_tick()
{
    if (!g_alarm.active) return;
    const uint32_t t = millis();
    if (t - g_alarm.started > kGiveUpMs) {
        g_alarm.active = false;
        return;
    }
    if (static_cast<int32_t>(t - g_alarm.next) >= 0) {
        alert_ring(g_alarm.conf);
        g_alarm.next = t + kRingEveryMs;
        if (!g_alarm.conf.repeat) g_alarm.active = false;
    }
}
#else
void alert_ring(const AlertConf& c)
{
    char wav[64];
    snprintf(wav, sizeof(wav), "/tmp/meshcard-alert-%d.wav", c.sound);
    struct stat st{};
    if (stat(wav, &st) != 0 && !write_wav(wav, make_sound(c.sound))) return;
    char cmd[512];
    // Through PipeWire, so it mixes with whatever else uses the speaker.
    snprintf(cmd, sizeof(cmd),
             "OLD=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null | cut -d' ' -f2); "
             "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%% 2>/dev/null; pw-play %s >/dev/null 2>&1; "
             "[ -n \"$OLD\" ] && wpctl set-volume @DEFAULT_AUDIO_SINK@ $OLD 2>/dev/null",
             c.volume, wav);
    // Double fork: the ring runs on its own and leaves no zombie behind.
    const pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", cmd, static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
}
#endif

}  // namespace mz

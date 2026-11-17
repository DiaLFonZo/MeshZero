// SPDX-License-Identifier: GPL-3.0-only
#include "ui/update.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>

#include "common/util.h"

namespace mz::update {
namespace {

constexpr uint32_t kCheckEveryS = 3600;
constexpr char kDefaultUrl[]    = "https://api.github.com/repos/s0liton/MeshZero/releases/latest";

struct State {
    bool enabled      = true;
    uint32_t checked  = 0;  // when the releases were last read, 0 if never
    std::string latest;     // newest release seen
    std::string notified;   // newest release the user has been told about
    bool running      = false;
};

std::mutex g_mutex;
State g_state;
bool g_loaded = false;

std::string state_file()
{
    return user_state_dir() + "/updates";
}

void load_locked()
{
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream in(state_file());
    std::string key, value;
    while (in >> key >> value) {
        if (key == "enabled") g_state.enabled = value != "0";
        if (key == "checked") g_state.checked = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
        if (key == "latest" && value != "-") g_state.latest = value;
        if (key == "notified" && value != "-") g_state.notified = value;
    }
    if (const char* env = getenv("MESHZERO_UPDATE_CHECK"); env && env[0] == '0') g_state.enabled = false;
}

void save_locked()
{
    const std::string dir = user_state_dir();
    make_dirs(dir);
    std::ofstream out(state_file() + ".tmp", std::ios::trunc);
    if (!out) return;
    out << "enabled " << (g_state.enabled ? 1 : 0) << '\n'
        << "checked " << g_state.checked << '\n'
        << "latest " << (g_state.latest.empty() ? "-" : g_state.latest) << '\n'
        << "notified " << (g_state.notified.empty() ? "-" : g_state.notified) << '\n';
    out.close();
    rename((state_file() + ".tmp").c_str(), state_file().c_str());
}

void fetch()
{
    // MESHZERO_UPDATE_URL points the check somewhere else, for trying it out.
    const char* env       = getenv("MESHZERO_UPDATE_URL");
    const std::string url = env && env[0] ? env : kDefaultUrl;
    std::string json;
    // The address is put in quotes; one that could end them is not used.
    if (url.find_first_of("'\\") == std::string::npos) {
        const std::string cmd = "curl -fsSL --max-time 15 -H 'Accept: application/vnd.github+json' -A 'meshzero/" +
                                std::string(MESHZERO_VERSION) + "' '" + url + "' 2>/dev/null";
        if (FILE* p = popen(cmd.c_str(), "r")) {
            char buf[4096];
            size_t n = 0;
            while (json.size() < 256 * 1024 && (n = fread(buf, 1, sizeof(buf), p)) > 0) json.append(buf, n);
            pclose(p);
        }
    }
    const std::string latest = release_version(json);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.running = false;
    if (latest.empty()) return;  // offline, no curl, or nothing released: look again next time
    g_state.latest  = latest;
    g_state.checked = now_s();
    save_locked();
}

}  // namespace

void start()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (!g_state.enabled || g_state.running) return;
    const uint32_t now = now_s();
    if (g_state.checked != 0 && now >= g_state.checked && now - g_state.checked < kCheckEveryS) return;
    g_state.running = true;
    std::thread(fetch).detach();
}

std::string available()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_state.enabled && version_compare(g_state.latest, MESHZERO_VERSION) > 0 ? g_state.latest : std::string();
}

bool take_notice()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (!g_state.enabled || version_compare(g_state.latest, MESHZERO_VERSION) <= 0) return false;
    if (g_state.notified == g_state.latest) return false;
    g_state.notified = g_state.latest;
    save_locked();
    return true;
}

bool checked()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_state.checked != 0;
}

bool enabled()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_state.enabled;
}

void set_enabled(bool on)
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        load_locked();
        g_state.enabled = on;
        if (on) g_state.checked = 0;  // look right away
        save_locked();
    }
    if (on) start();
}

}  // namespace mz::update

// SPDX-License-Identifier: GPL-3.0-only
// MeshZero: a Meshtastic client for the M5Stack CardputerZero.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

#include "common/util.h"
#include "lvgl.h"
#include "ui/app.h"
#include "ui/battery.h"
#include "ui/camera.h"
#include "ui/display.h"
#include "ui/keys.h"
#include "ui/mesh.h"
#include "ui/update.h"

using namespace mz;

namespace {

volatile sig_atomic_t g_signal = 0;

void on_signal(int)
{
    g_signal = 1;
}

uint32_t tick_cb()
{
    return static_cast<uint32_t>(now_ms());
}

void usage()
{
    printf("MeshZero %s - Meshtastic client for the CardputerZero\n\n"
           "  meshzero [options]\n\n"
           "  --host HOST[:PORT]   API endpoint to connect to (default 127.0.0.1:4403)\n"
           "  --display KIND       fbdev, sdl or headless (default: fbdev on the device)\n"
           "  --fb PATH            framebuffer device (default: the built-in LCD)\n"
           "  --zoom N             SDL window scale (default 3)\n"
           "  --script FILE        headless automation: 'wait MS', 'key NAME', 'hold NAME MS', 'type TEXT',\n"
           "                       'shot FILE.png', 'quit' (one command per line)\n"
           "  --version\n",
           MESHZERO_VERSION);
}

// Screen light: dims after 30 s without a key, goes dark after 60 s.
// Any key or a new message lights it again; the key that wakes a dark
// screen does nothing else.
class ScreenLight {
public:
    static constexpr uint64_t kDimMs = 30000;
    static constexpr uint64_t kOffMs = 60000;
    static constexpr int kFull = 100, kDim = 15, kOff = 0;

    static std::string light_path()
    {
        const char* env = getenv("MESHZERO_BACKLIGHT");
        return env && env[0] ? env : "/sys/class/backlight/backlight/brightness";
    }
    // A screen light we may write (the Zero's; none on a PC).
    static bool available() { return access(light_path().c_str(), W_OK) == 0; }

    void enable()
    {
        path_ = light_path();
        on_   = true;
        set(kFull);
        last_ = now_ms();
    }
    ~ScreenLight() { if (on_) set(kFull); }

    // Returns false when the key must be dropped (it only woke the screen).
    bool key(const Key& k)
    {
        if (!on_) return true;
        const uint64_t now = now_ms();
        last_ = now;
        if (eating_) {
            if (k.release || now > eat_until_) eating_ = false;
            if (now <= eat_until_) return false;
        }
        if (level_ == kOff && !k.release) {
            set(kFull);
            forced_off_ = false;
            eating_     = true;
            eat_until_  = now + 1000;
            return false;
        }
        if (!forced_off_) set(kFull);
        return true;
    }

    // Esc on Home: dark now, until the next key or message.
    void turn_off()
    {
        if (!on_) return;
        forced_off_ = true;
        set(kOff);
    }

    // Plugged in: stays fully lit (unless turned off with Esc).
    void tick(uint32_t wake_count, bool plugged)
    {
        if (!on_) return;
        const uint64_t now = now_ms();
        // A new message, or plugging in / unplugging, lights the screen again.
        if (wake_count != seen_wake_ || plugged != was_plugged_) {
            seen_wake_   = wake_count;
            was_plugged_ = plugged;
            last_        = now;
            forced_off_  = false;
        }
        if (forced_off_) return set(kOff);
        if (plugged) {
            // The Zero's launcher keeps running behind us and turns the light off on its own
            // timer; level_ would still say "full". Check the real value every second.
            if (now - checked_ms_ >= 1000) {
                checked_ms_ = now;
                if (FILE* f = fopen(path_.c_str(), "r")) {
                    int real = -1;
                    if (fscanf(f, "%d", &real) == 1 && real == 0) level_ = -1;  // forces the write below
                    fclose(f);
                }
            }
            return set(kFull);
        }
        const uint64_t idle = now - last_;
        set(idle >= kOffMs ? kOff : idle >= kDimMs ? kDim : kFull);
    }

private:
    void set(int level)
    {
        if (level == level_) return;
        level_ = level;
        if (FILE* f = fopen(path_.c_str(), "w")) {
            fprintf(f, "%d\n", level);
            fclose(f);
        }
    }

    std::string path_;
    bool on_ = false, eating_ = false, forced_off_ = false, was_plugged_ = false;
    int level_ = -1;
    uint64_t last_ = 0, eat_until_ = 0, checked_ms_ = 0;
    uint32_t seen_wake_ = 0;
};

ScreenLight g_light;

// Runs the main loop for the given time; returns false when asked to stop.
bool pump(App& app, Mesh& mesh, Keypad* keypad, uint64_t duration_ms)
{
    const uint64_t until = now_ms() + duration_ms;
    std::deque<Key> keys;
    do {
        mesh.poll();
        if (keypad) keypad->poll(keys);
        display_poll_keys(keys);
        while (!keys.empty()) {
            const Key k = keys.front();
            keys.pop_front();
            if (g_light.key(k)) app.handle_key(k);
        }
        app.tick();
        if (app.take_screen_off_request()) g_light.turn_off();
        g_light.tick(app.wake_count(), battery_info().charging);
        const uint32_t idle = lv_timer_handler();
        if (g_signal || app.quit_requested() || display_quit_requested()) return false;
        usleep((idle < 5 ? 5 : idle > 20 ? 20 : idle) * 1000);
    } while (now_ms() < until);
    return true;
}

int run_script(const std::string& path, App& app, Mesh& mesh)
{
    std::ifstream in(path);
    if (!in) {
        fprintf(stderr, "meshzero: cannot read script %s\n", path.c_str());
        return 2;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        std::string arg;
        std::getline(ss, arg);
        arg = trim(arg);
        if (cmd == "wait") {
            if (!pump(app, mesh, nullptr, static_cast<uint64_t>(atoi(arg.c_str())))) return 0;
        } else if (cmd == "key") {
            std::istringstream names(arg);
            std::string name;
            while (names >> name) {
                Key key;
                if (!key_from_name(name, key)) {
                    fprintf(stderr, "meshzero: unknown key '%s'\n", name.c_str());
                    return 2;
                }
                app.handle_key(key);
                pump(app, mesh, nullptr, 30);
                if (key.k == K::Esc) {
                    key.release = true;
                    app.handle_key(key);
                }
            }
        } else if (cmd == "hold") {
            // hold esc 2500: keeps Esc down for that many milliseconds.
            std::istringstream parts(arg);
            std::string name;
            int ms = 0;
            std::string shot;  // optional: screenshot taken while the key is still down
            parts >> name >> ms >> shot;
            Key key;
            if (!key_from_name(name, key)) {
                fprintf(stderr, "meshzero: unknown key '%s'\n", name.c_str());
                return 2;
            }
            app.handle_key(key);
            const bool alive = pump(app, mesh, nullptr, static_cast<uint64_t>(ms));
            if (alive && !shot.empty()) display_screenshot(shot);
            key.release      = true;
            app.handle_key(key);
            if (!alive) return 0;
        } else if (cmd == "type") {
            for (const char c : arg) {
                Key key;
                key.k  = K::Char;
                key.ch = static_cast<uint8_t>(c);
                app.handle_key(key);
            }
            pump(app, mesh, nullptr, 30);
        } else if (cmd == "shot") {
            pump(app, mesh, nullptr, 60);
            if (!display_screenshot(arg)) {
                fprintf(stderr, "meshzero: screenshot %s failed\n", arg.c_str());
                return 2;
            }
        } else if (cmd == "quit") {
            return 0;
        } else {
            fprintf(stderr, "meshzero: unknown script command '%s'\n", cmd.c_str());
            return 2;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string host = "127.0.0.1";
    int port         = kDefaultApiPort;
    std::string script;
    DisplayOptions display;

    if (const char* env = getenv("MESHZERO_HOST"); env && env[0]) host = env;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next     = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--host") {
            host = next();
        } else if (a == "--display") {
            const std::string kind = next();
            display.kind = kind == "sdl" ? DisplayKind::Sdl : kind == "headless" ? DisplayKind::Headless : DisplayKind::Fbdev;
        } else if (a == "--fb") {
            display.fb_device = next();
        } else if (a == "--zoom") {
            display.zoom = atoi(next().c_str());
        } else if (a == "--script") {
            script       = next();
            display.kind = DisplayKind::Headless;
        } else if (a == "--version") {
            printf("%s\n", MESHZERO_VERSION);
            return 0;
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        port = atoi(host.substr(colon + 1).c_str());
        host = host.substr(0, colon);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    lv_init();
    lv_tick_set_cb(tick_cb);
    if (!display_init(display)) {
        fprintf(stderr, "meshzero: could not initialise the display\n");
        return 1;
    }

    Camera::probe();

    int rc = 0;
    {
        Mesh mesh;
        mesh.configure(host, static_cast<uint16_t>(port));
        App app(mesh);
        app.start();

        if (!script.empty()) {
            // Scripted runs stay off the network unless told where to look.
            if (getenv("MESHZERO_UPDATE_URL")) update::start();
            rc = run_script(script, app, mesh);
        } else {
            update::start();
            Keypad keypad;
            const bool device_keys = display_kind() == DisplayKind::Fbdev;
            if (device_keys && !keypad.open_default()) fprintf(stderr, "meshzero: no keyboard found\n");
            // The screen light is ours whenever the device has one, also when MeshCard runs in
            // the Zero's desktop (not fbdev): before, it was left to the launcher there, which
            // turned it off while plugged in (2026-11-20).
            if (device_keys || g_light.available()) g_light.enable();
            while (pump(app, mesh, device_keys ? &keypad : nullptr, 1000)) {
            }
        }
    }
    display_shutdown();
    return rc;
}

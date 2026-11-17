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
            app.handle_key(k);
        }
        app.tick();
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
            parts >> name >> ms;
            Key key;
            if (!key_from_name(name, key)) {
                fprintf(stderr, "meshzero: unknown key '%s'\n", name.c_str());
                return 2;
            }
            app.handle_key(key);
            const bool alive = pump(app, mesh, nullptr, static_cast<uint64_t>(ms));
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
            while (pump(app, mesh, device_keys ? &keypad : nullptr, 1000)) {
            }
        }
    }
    display_shutdown();
    return rc;
}

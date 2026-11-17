// SPDX-License-Identifier: GPL-3.0-only
#include "ui/display.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include <unistd.h>

#if LV_USE_SDL
#include <SDL2/SDL.h>
#endif

namespace mz {
namespace {

DisplayKind g_kind    = DisplayKind::Auto;
lv_display_t* g_disp  = nullptr;
bool g_quit           = false;
std::vector<uint8_t> g_frame;  // headless: RGB565 framebuffer
std::deque<Key> g_backend_keys;

// The built-in LCD is not always fb0 (HDMI can take that slot).
std::string detect_framebuffer()
{
    if (const char* env = getenv("LV_LINUX_FBDEV_DEVICE"); env && env[0]) return env;
    std::ifstream in("/proc/fb");
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("st7789") != std::string::npos) return "/dev/fb" + std::to_string(atoi(line.c_str()));
    }
    return "/dev/fb0";
}

void headless_flush(lv_display_t* disp, const lv_area_t*, uint8_t*)
{
    lv_display_flush_ready(disp);
}

// ---- minimal PNG encoder (stored deflate blocks, no dependencies) ----

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

void put_be32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void put_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    put_be32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = crc32_update(0xffffffffu, out.data() + start, out.size() - start) ^ 0xffffffffu;
    put_be32(out, crc);
}

bool write_png(const std::string& path, const uint8_t* rgb565, int w, int h)
{
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const size_t i   = (static_cast<size_t>(y) * w + x) * 2;
            const uint16_t p = static_cast<uint16_t>(rgb565[i] | (rgb565[i + 1] << 8));
            const uint8_t r5 = (p >> 11) & 0x1f, g6 = (p >> 5) & 0x3f, b5 = p & 0x1f;
            raw.push_back(static_cast<uint8_t>((r5 << 3) | (r5 >> 2)));
            raw.push_back(static_cast<uint8_t>((g6 << 2) | (g6 >> 4)));
            raw.push_back(static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (const uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xff));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xff));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
        pos += n;
    }
    put_be32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(w));
    put_be32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    put_chunk(out, "IHDR", ihdr);
    put_chunk(out, "IDAT", z);
    put_chunk(out, "IEND", {});
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

#if LV_USE_SDL
int sdl_event_watch(void*, SDL_Event* e)
{
    if (e->type == SDL_QUIT) {
        g_quit = true;
    } else if (e->type == SDL_TEXTINPUT) {
        for (const char* c = e->text.text; *c; ++c) {
            if (static_cast<uint8_t>(*c) >= 0x20 && static_cast<uint8_t>(*c) < 0x7f) {
                Key key;
                key.k  = K::Char;
                key.ch = static_cast<uint8_t>(*c);
                g_backend_keys.push_back(key);
            }
        }
    } else if (e->type == SDL_KEYUP && e->key.keysym.sym == SDLK_ESCAPE) {
        Key key;
        key.k       = K::Esc;
        key.release = true;
        g_backend_keys.push_back(key);
    } else if (e->type == SDL_KEYDOWN) {
        Key key;
        key.repeat = e->key.repeat != 0;
        key.ctrl   = (e->key.keysym.mod & KMOD_CTRL) != 0;
        switch (e->key.keysym.sym) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                key.k = K::Enter;
                break;
            case SDLK_ESCAPE:
                key.k = K::Esc;
                break;
            case SDLK_BACKSPACE:
                key.k = K::Backspace;
                break;
            case SDLK_DELETE:
                key.k = K::Delete;
                break;
            case SDLK_UP:
                key.k = K::Up;
                break;
            case SDLK_DOWN:
                key.k = K::Down;
                break;
            case SDLK_LEFT:
                key.k = K::Left;
                break;
            case SDLK_RIGHT:
                key.k = K::Right;
                break;
            case SDLK_TAB:
                key.k = K::Tab;
                break;
            case SDLK_HOME:
                key.k = K::Home;
                break;
            case SDLK_END:
                key.k = K::End;
                break;
            case SDLK_PAGEUP:
                key.k = K::PageUp;
                break;
            case SDLK_PAGEDOWN:
                key.k = K::PageDown;
                break;
            default:
                break;
        }
        if (key.k != K::None) g_backend_keys.push_back(key);
    }
    return 1;
}
#endif

}  // namespace

DisplayKind display_kind()
{
    return g_kind;
}

bool display_quit_requested()
{
    return g_quit;
}

bool display_init(const DisplayOptions& options)
{
    DisplayKind kind = options.kind;
    if (kind == DisplayKind::Auto) {
#if LV_USE_SDL
        kind = (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")) ? DisplayKind::Sdl : DisplayKind::Fbdev;
#else
        kind = DisplayKind::Fbdev;
#endif
    }

    if (kind == DisplayKind::Headless) {
        g_disp = lv_display_create(kScreenW, kScreenH);
        g_frame.assign(static_cast<size_t>(kScreenW) * kScreenH * 2, 0);
        lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(g_disp, g_frame.data(), nullptr, static_cast<uint32_t>(g_frame.size()),
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(g_disp, headless_flush);
    } else if (kind == DisplayKind::Sdl) {
#if LV_USE_SDL
        g_disp = lv_sdl_window_create(kScreenW, kScreenH);
        if (!g_disp) return false;
        lv_sdl_window_set_zoom(g_disp, static_cast<float>(options.zoom));
        lv_sdl_window_set_title(g_disp, "MeshZero");
        SDL_StartTextInput();
        SDL_AddEventWatch(sdl_event_watch, nullptr);
#else
        fprintf(stderr, "meshzero: this build has no SDL support\n");
        return false;
#endif
    } else {
        const std::string device = options.fb_device.empty() ? detect_framebuffer() : options.fb_device;
        if (access(device.c_str(), R_OK | W_OK) != 0) {
            fprintf(stderr, "meshzero: cannot open framebuffer %s\n", device.c_str());
            return false;
        }
        g_disp = lv_linux_fbdev_create();
        if (!g_disp) return false;
        lv_linux_fbdev_set_file(g_disp, device.c_str());
    }
    g_kind = kind;
    return g_disp != nullptr;
}

void display_shutdown()
{
    if (g_disp) lv_display_delete(g_disp);
    g_disp = nullptr;
#if LV_USE_SDL
    if (g_kind == DisplayKind::Sdl) lv_sdl_quit();
#endif
}

void display_poll_keys(std::deque<Key>& out)
{
    while (!g_backend_keys.empty()) {
        out.push_back(g_backend_keys.front());
        g_backend_keys.pop_front();
    }
}

bool display_screenshot(const std::string& path)
{
    if (g_kind != DisplayKind::Headless) return false;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(g_disp);
    return write_png(path, g_frame.data(), kScreenW, kScreenH);
}

}  // namespace mz

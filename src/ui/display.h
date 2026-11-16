// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <deque>
#include <string>

#include "lvgl.h"
#include "ui/keys.h"

namespace mz {

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;

enum class DisplayKind { Auto, Fbdev, Sdl, Headless };

struct DisplayOptions {
    DisplayKind kind = DisplayKind::Auto;
    std::string fb_device;  // empty: detect the built-in LCD
    int zoom = 3;           // SDL window scale
};

// Creates the LVGL display (and, for SDL, its keyboard source).
bool display_init(const DisplayOptions& options);
void display_shutdown();
DisplayKind display_kind();
bool display_quit_requested();

// Keys produced by the display backend itself (SDL window). Device keys come
// from Keypad instead.
void display_poll_keys(std::deque<Key>& out);

// Renders the current frame and writes it as a PNG. Headless backend only.
bool display_screenshot(const std::string& path);

}  // namespace mz

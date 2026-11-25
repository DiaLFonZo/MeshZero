// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <deque>
#include <string>

#include "lvgl.h"
#include "ui/keys.h"

namespace mz {

#ifndef MC_SCREEN_W
#define MC_SCREEN_W 320
#endif
#ifndef MC_SCREEN_H
#define MC_SCREEN_H 170
#endif
constexpr int kScreenW = MC_SCREEN_W;
constexpr int kScreenH = MC_SCREEN_H;
// Small screen = Cardputer ADV (240x135): tighter layout, smaller text.
constexpr bool kSmall = kScreenW < 300;
// Scales a Zero pixel size for this screen (4/5 on the ADV, like the text).
constexpr int S(int v) { return kSmall ? (v * 4 + (v >= 0 ? 2 : -2)) / 5 : v; }

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

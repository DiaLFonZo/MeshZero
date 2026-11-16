// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace mz {

enum class K { None, Char, Enter, Esc, Backspace, Delete, Up, Down, Left, Right, Tab, Home, End, PageUp, PageDown };

struct Key {
    K k         = K::None;
    uint32_t ch = 0;  // Unicode code point when k == Char
    bool ctrl   = false;
    bool repeat = false;

    bool is_char(char c) const { return k == K::Char && !ctrl && ch == static_cast<uint32_t>(c); }
};

// Reads the CardputerZero keyboard (or any Linux keyboard) through evdev.
// The kernel driver already resolves the Fn and Sym layers into key codes, so
// only Shift and Ctrl need to be tracked here.
class Keypad {
public:
    ~Keypad();
    bool open_default();
    bool open_device(const std::string& path, bool require_keys);
    void poll(std::deque<Key>& out);

private:
    void handle(uint16_t code, int32_t value, std::deque<Key>& out);
    std::vector<int> fds_;
    bool shift_l_ = false, shift_r_ = false, ctrl_l_ = false, ctrl_r_ = false, caps_ = false;
};

// Parses a key name used by scripted runs: "enter", "esc", "up", "tab", "a", ...
bool key_from_name(const std::string& name, Key& out);

}  // namespace mz

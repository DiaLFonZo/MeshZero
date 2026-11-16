// SPDX-License-Identifier: GPL-3.0-only
#include "ui/keys.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace mz {
namespace {

// Key codes the TCA8418 keypad driver emits for its Sym layer. They reuse
// unrelated Linux key codes, so they must be checked before the generic map.
// Mirrors /usr/share/keymaps/tca8418_keypad_m5stack_keymap.map on the device.
struct SymEntry {
    uint16_t code;
    char text;
};
constexpr SymEntry kSymLayer[] = {
    {26, '!'}, {27, '@'}, {39, '#'},  {40, '$'}, {41, '%'}, {43, '^'}, {51, '&'},  {52, '*'},
    {53, '('}, {94, ')'}, {55, '~'},  {69, '`'}, {70, '_'}, {71, '-'}, {72, '+'},  {73, '='},
    {74, '['}, {75, ']'}, {76, '{'},  {77, '}'}, {79, ';'}, {80, ':'}, {81, '\''}, {82, '"'},
    {83, '<'}, {85, '>'}, {86, '\\'}, {89, '|'}, {90, ','}, {91, '.'}, {92, '/'},  {93, '?'},
};

bool g_m5_keymap = false;

char sym_char(uint16_t code)
{
    if (!g_m5_keymap) return 0;
    for (const auto& e : kSymLayer) {
        if (e.code == code) return e.text;
    }
    return 0;
}

struct Plain {
    uint16_t code;
    char normal;
    char shifted;
};
constexpr Plain kPlain[] = {
    {KEY_1, '1', '!'},          {KEY_2, '2', '@'},          {KEY_3, '3', '#'},         {KEY_4, '4', '$'},
    {KEY_5, '5', '%'},          {KEY_6, '6', '^'},          {KEY_7, '7', '&'},         {KEY_8, '8', '*'},
    {KEY_9, '9', '('},          {KEY_0, '0', ')'},          {KEY_MINUS, '-', '_'},     {KEY_EQUAL, '=', '+'},
    {KEY_LEFTBRACE, '[', '{'},  {KEY_RIGHTBRACE, ']', '}'}, {KEY_BACKSLASH, '\\', '|'}, {KEY_SEMICOLON, ';', ':'},
    {KEY_APOSTROPHE, '\'', '"'}, {KEY_GRAVE, '`', '~'},     {KEY_COMMA, ',', '<'},     {KEY_DOT, '.', '>'},
    {KEY_SLASH, '/', '?'},      {KEY_SPACE, ' ', ' '},
};

char letter(uint16_t code)
{
    static const struct {
        uint16_t first;
        const char* row;
    } rows[] = {{KEY_Q, "qwertyuiop"}, {KEY_A, "asdfghjkl"}, {KEY_Z, "zxcvbnm"}};
    for (const auto& r : rows) {
        const size_t n = strlen(r.row);
        if (code >= r.first && code < r.first + n) return r.row[code - r.first];
    }
    return 0;
}

bool has_typing_keys(int fd)
{
    std::array<unsigned long, (KEY_MAX / (sizeof(unsigned long) * 8)) + 1> bits{};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits.data()) < 0) return false;
    const auto test = [&bits](unsigned bit) {
        return (bits[bit / (sizeof(unsigned long) * 8)] >> (bit % (sizeof(unsigned long) * 8))) & 1UL;
    };
    return test(KEY_ENTER) && test(KEY_A) && test(KEY_Z);
}

}  // namespace

Keypad::~Keypad()
{
    for (const int fd : fds_) close(fd);
}

bool Keypad::open_device(const std::string& path, bool require_keys)
{
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    if (require_keys && !has_typing_keys(fd)) {
        close(fd);
        return false;
    }
    fds_.push_back(fd);
    return true;
}

bool Keypad::open_default()
{
    for (const char* name : {"MESHZERO_KEYBOARD_DEVICE", "APPLAUNCH_LINUX_KEYBOARD_DEVICE", "LV_LINUX_KEYBOARD_DEVICE"}) {
        const char* path = getenv(name);
        if (path && path[0] && open_device(path, false)) {
            g_m5_keymap = true;
            return true;
        }
    }
    // The built-in keyboard of the CardputerZero.
    if (open_device("/dev/input/by-path/platform-3f804000.i2c-event", false)) {
        g_m5_keymap = true;
        return true;
    }
    bool any = false;
    for (int i = 0; i < 32; ++i) any = open_device("/dev/input/event" + std::to_string(i), true) || any;
    return any;
}

void Keypad::poll(std::deque<Key>& out)
{
    for (const int fd : fds_) {
        while (true) {
            input_event ev{};
            const ssize_t n = read(fd, &ev, sizeof(ev));
            if (n != sizeof(ev)) break;
            if (ev.type == EV_KEY) handle(ev.code, ev.value, out);
        }
    }
}

void Keypad::handle(uint16_t code, int32_t value, std::deque<Key>& out)
{
    switch (code) {
        case KEY_LEFTSHIFT:
            shift_l_ = value != 0;
            return;
        case KEY_RIGHTSHIFT:
            shift_r_ = value != 0;
            return;
        case KEY_LEFTCTRL:
            ctrl_l_ = value != 0;
            return;
        case KEY_RIGHTCTRL:
            ctrl_r_ = value != 0;
            return;
        case KEY_CAPSLOCK:
            if (value == 1) caps_ = !caps_;
            return;
        default:
            break;
    }
    if (value == 0) return;  // releases carry no information for us

    Key key;
    key.repeat = value == 2;
    key.ctrl   = ctrl_l_ || ctrl_r_;
    const bool shift = shift_l_ || shift_r_;

    if (const char s = sym_char(code)) {
        key.k  = K::Char;
        key.ch = static_cast<uint8_t>(s);
        out.push_back(key);
        return;
    }
    switch (code) {
        case KEY_ENTER:
        case KEY_KPENTER:
            key.k = K::Enter;
            break;
        case KEY_ESC:
            key.k = K::Esc;
            break;
        case KEY_BACKSPACE:
            key.k = K::Backspace;
            break;
        case KEY_DELETE:
            key.k = K::Delete;
            break;
        case KEY_UP:
            key.k = K::Up;
            break;
        case KEY_DOWN:
            key.k = K::Down;
            break;
        case KEY_LEFT:
            key.k = K::Left;
            break;
        case KEY_RIGHT:
            key.k = K::Right;
            break;
        case KEY_TAB:
            key.k = K::Tab;
            break;
        case KEY_HOME:
            key.k = K::Home;
            break;
        case KEY_END:
            key.k = K::End;
            break;
        case KEY_PAGEUP:
            key.k = K::PageUp;
            break;
        case KEY_PAGEDOWN:
            key.k = K::PageDown;
            break;
        default:
            if (const char l = letter(code)) {
                key.k  = K::Char;
                key.ch = static_cast<uint8_t>((shift != caps_) ? l - 'a' + 'A' : l);
                break;
            }
            for (const auto& p : kPlain) {
                if (p.code == code) {
                    key.k  = K::Char;
                    key.ch = static_cast<uint8_t>(shift ? p.shifted : p.normal);
                    break;
                }
            }
            break;
    }
    if (key.k != K::None) out.push_back(key);
}

bool key_from_name(const std::string& name, Key& out)
{
    static const struct {
        const char* name;
        K k;
    } names[] = {{"enter", K::Enter},   {"esc", K::Esc},   {"backspace", K::Backspace}, {"delete", K::Delete},
                 {"up", K::Up},         {"down", K::Down}, {"left", K::Left},           {"right", K::Right},
                 {"tab", K::Tab},       {"home", K::Home}, {"end", K::End},             {"pgup", K::PageUp},
                 {"pgdn", K::PageDown}};
    out = Key{};
    for (const auto& n : names) {
        if (name == n.name) {
            out.k = n.k;
            return true;
        }
    }
    if (name == "space") {
        out.k  = K::Char;
        out.ch = ' ';
        return true;
    }
    if (name.size() == 1) {
        out.k  = K::Char;
        out.ch = static_cast<uint8_t>(name[0]);
        return true;
    }
    return false;
}

}  // namespace mz

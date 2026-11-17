// SPDX-License-Identifier: GPL-3.0-only
// Table-driven settings editor: a page is a list of typed fields bound to a
// working copy of a config struct, plus a function that sends it to the radio.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/app.h"

namespace mz {

struct Opt {
    int64_t value;
    std::string label;
};

struct Field {
    enum Type { Bool, Int, Enum, Text, Action, Info, Header } type = Info;
    std::string label;
    std::string help;
    std::string unit;
    std::function<int64_t()> get;
    std::function<void(int64_t)> set;
    std::function<std::string()> get_text;
    // Returns an empty string on success, otherwise the reason it was rejected.
    std::function<std::string(const std::string&)> set_text;
    std::function<void()> run;
    std::vector<Opt> opts;
    int64_t min    = 0;
    int64_t max    = 0;
    size_t max_len = 0;
    bool secret    = false;
    // Optional: something to add after the value shown (Enum), and what an
    // empty Text field reads as instead of "(empty)".
    std::function<std::string()> note;
    std::string empty_text;
    // Action rows normally leave the page clean; set when the action edits the working copy.
    bool marks_dirty = false;
};

Field f_header(const std::string& label);
Field f_info(const std::string& label, std::function<std::string()> get_text);
Field f_bool(const std::string& label, std::function<int64_t()> get, std::function<void(int64_t)> set,
             const std::string& help = "");
Field f_int(const std::string& label, std::function<int64_t()> get, std::function<void(int64_t)> set, int64_t min,
            int64_t max, const std::string& unit = "", const std::string& help = "");
Field f_enum(const std::string& label, std::function<int64_t()> get, std::function<void(int64_t)> set,
             std::vector<Opt> opts, const std::string& help = "");
Field f_text(const std::string& label, std::function<std::string()> get,
             std::function<std::string(const std::string&)> set, size_t max_len, const std::string& help = "",
             bool secret = false);
Field f_action(const std::string& label, std::function<void()> run, const std::string& help = "",
               bool marks_dirty = false);

// `save` is called once when the user confirms; it should send the working copy.
std::unique_ptr<Screen> make_editor(App& app, const std::string& title, std::vector<Field> fields,
                                    std::function<void()> save, const std::string& saved_note = "");

// Binds a field to a member of a shared working copy named `cfg`.
#define MZ_BOOL(label, member, help) \
    f_bool(label, [cfg]() { return static_cast<int64_t>(cfg->member); }, [cfg](int64_t v) { cfg->member = v != 0; }, help)
#define MZ_INT(label, member, lo, hi, unit, help)                 \
    f_int(label, [cfg]() { return static_cast<int64_t>(cfg->member); }, \
          [cfg](int64_t v) { cfg->member = static_cast<decltype(cfg->member)>(v); }, lo, hi, unit, help)
#define MZ_ENUM(label, member, opts, help)                         \
    f_enum(label, [cfg]() { return static_cast<int64_t>(cfg->member); }, \
           [cfg](int64_t v) { cfg->member = static_cast<decltype(cfg->member)>(v); }, opts, help)
#define MZ_TEXT(label, member, help, secret)                                     \
    f_text(label, [cfg]() { return std::string(cfg->member); },                  \
           [cfg](const std::string& s) {                                         \
               snprintf(cfg->member, sizeof(cfg->member), "%s", s.c_str());      \
               return std::string();                                             \
           },                                                                    \
           sizeof(cfg->member) - 1, help, secret)

}  // namespace mz

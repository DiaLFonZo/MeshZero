// SPDX-License-Identifier: GPL-3.0-only
// Theme, text helpers and the two keyboard-driven widgets every screen uses.
#pragma once

#include <string>
#include <vector>

#include "lvgl.h"
#include "ui/keys.h"

namespace mz::ui {

// ---- palette -------------------------------------------------------------
lv_color_t c_bg();
lv_color_t c_bar();
lv_color_t c_text();
lv_color_t c_dim();
lv_color_t c_accent();
lv_color_t c_select();
lv_color_t c_warn();
lv_color_t c_error();
lv_color_t c_me();
lv_color_t node_color(uint32_t num);

// The accent colours to choose from, in rainbow order. All are light enough
// for dark text on them, as on the selected tab.
struct Accent {
    const char* name;  // shown, and how the choice is saved
    uint32_t rgb;
};
const std::vector<Accent>& accents();
// The accent in use, as an index into accents(); saved for the next start.
size_t accent_index();
void set_accent(size_t index);

// Whether the Map page works in feet and miles instead of metres and
// kilometres: its distances, and which topographic map it shows. Saved for
// the next start.
bool imperial();
void set_imperial(bool on);

const lv_font_t* font();
const lv_font_t* font_small();

constexpr int kRowH = 17;

// ---- builders ------------------------------------------------------------
// A bare container: no padding, border, radius, background or scrolling.
lv_obj_t* box(lv_obj_t* parent);
lv_obj_t* label(lv_obj_t* parent, const std::string& text, lv_color_t color, const lv_font_t* f = nullptr);
void set_text(lv_obj_t* label, const std::string& text);
void set_bg(lv_obj_t* obj, lv_color_t color);

// Small arrows at the top and bottom edge of a scrolling area, shown while
// there is more to see in that direction. They follow scrolling by
// themselves; call update_scroll_hints when the content changed instead.
// After lv_obj_clean on the area they have to be added again.
void add_scroll_hints(lv_obj_t* scrollable);
void update_scroll_hints(lv_obj_t* scrollable);

// Makes arbitrary mesh text safe for the built-in fonts: emoji and other
// glyphs the font lacks become readable ASCII stand-ins.
std::string clean(const std::string& text, bool single_line = false);

// ---- list ----------------------------------------------------------------
struct ListItem {
    std::string prefix;  // fixed-width leading cell (e.g. a node's short name)
    lv_color_t prefix_color{};
    std::string text;
    std::string right;
    lv_color_t color{};
    lv_color_t right_color{};
    bool header   = false;  // section title, not selectable
    bool has_color = false;
    bool has_prefix_color = false;
    bool has_right_color = false;
};

class ListView {
public:
    ListView(lv_obj_t* parent, int x, int y, int w, int h);
    void set_prefix_width(int w) { prefix_w_ = w; }
    void set_items(const std::vector<ListItem>& items);
    int selected() const { return selected_; }
    void select(int index);
    size_t count() const { return items_.size(); }
    bool handle_key(const Key& key);
    lv_obj_t* obj() const { return cont_; }

private:
    struct Row {
        lv_obj_t* row    = nullptr;
        lv_obj_t* prefix = nullptr;
        lv_obj_t* text   = nullptr;
        lv_obj_t* right  = nullptr;
    };
    void rebuild();
    void apply(size_t index);
    void move(int delta);
    bool selectable(int index) const;

    lv_obj_t* cont_ = nullptr;
    int w_          = 0;
    int h_          = 0;
    int prefix_w_   = 0;
    int selected_   = 0;
    std::vector<ListItem> items_;
    std::vector<Row> rows_;
};

// ---- single-line text entry -------------------------------------------------
class TextField {
public:
    TextField(lv_obj_t* parent, int x, int y, int w, const std::string& placeholder = "");
    bool handle_key(const Key& key);
    std::string text() const;
    void set_text(const std::string& text);
    // Adds text at the cursor. False when it would not fit.
    bool insert(const std::string& text);
    void set_max_bytes(size_t n) { max_bytes_ = n; }
    void set_accept(const char* chars);
    lv_obj_t* obj() const { return ta_; }

private:
    lv_obj_t* ta_     = nullptr;
    size_t max_bytes_ = 200;
};

}  // namespace mz::ui

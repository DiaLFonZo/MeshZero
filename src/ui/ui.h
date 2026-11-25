// SPDX-License-Identifier: GPL-3.0-only
// Theme, text helpers and the two keyboard-driven widgets every screen uses.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "lvgl.h"
#include "ui/display.h"
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

const lv_font_t* font();
const lv_font_t* font_small();
const lv_font_t* font_bold();

// MeshCard: 14 px text in 17 px cards with a 2 px gap (7 rows on the Zero).
constexpr int kRowH   = S(19);
constexpr int kRowGap = S(2);

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
    std::string sub;     // MeshCard: second line, shown under the text when selected
    lv_color_t color{};
    lv_color_t right_color{};
    bool header   = false;  // section title, not selectable
    bool has_color = false;
    bool has_prefix_color = false;
    bool has_right_color = false;
    bool right_badge = false;  // right cell drawn as a round count badge
};

// MeshCard: a deque grows in small blocks; a long list in one block needed more
// unbroken memory than the ADV has (Settings crashed while building its list).
using ListItems = std::deque<ListItem>;

class ListView {
public:
    ListView(lv_obj_t* parent, int x, int y, int w, int h);
    void set_prefix_width(int w) { prefix_w_ = w; }
    void set_items(const ListItems& items);
    int selected() const { return selected_; }
    void select(int index);
    size_t count() const { return items_.size(); }
    bool handle_key(const Key& key);
    lv_obj_t* obj() const { return cont_; }
    // MeshCard: an inactive list shows no highlighted row (focus is elsewhere).
    void set_active(bool on);
    bool at_top() const;
    // Change the list's height.
    void set_height(int h);
    // Paints the rows in view (called by the screen's draw event).
    void draw(lv_layer_t* layer);

private:
    // MeshCard: the list is ONE screen object that paints its rows itself. A
    // label object per row cost ~450 bytes each; the ADV (no PSRAM) ran out of
    // memory on Settings plus a popup. Now a list costs the same at any length.
    void move(int delta);
    bool selectable(int index) const;
    int y_of(size_t index) const;
    int row_h(size_t index) const;
    int total_h() const;
    bool two_line_selected() const;
    void keep_in_view();  // scrolls just enough to show the selection (and its header)
    void redraw();

    lv_obj_t* cont_ = nullptr;
    int w_          = 0;
    int h_          = 0;
    int prefix_w_   = 0;
    int selected_   = 0;
    int scroll_     = 0;  // pixels scrolled from the top
    bool active_    = true;
    ListItems items_;
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

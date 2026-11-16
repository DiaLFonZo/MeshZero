// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ui.h"

#include <cstring>
#include <unordered_map>

#include "ui/fonts/fonts.h"

namespace mz::ui {

lv_color_t c_bg()
{
    return lv_color_hex(0x0b0f14);
}
lv_color_t c_bar()
{
    return lv_color_hex(0x18212b);
}
lv_color_t c_text()
{
    return lv_color_hex(0xe6edf3);
}
lv_color_t c_dim()
{
    return lv_color_hex(0x8b98a5);
}
lv_color_t c_accent()
{
    return lv_color_hex(0x67ea94);
}
lv_color_t c_select()
{
    return lv_color_hex(0x1f3a5a);
}
lv_color_t c_warn()
{
    return lv_color_hex(0xf0b429);
}
lv_color_t c_error()
{
    return lv_color_hex(0xff6b6b);
}
lv_color_t c_me()
{
    return lv_color_hex(0x6cb6ff);
}

lv_color_t node_color(uint32_t num)
{
    static const uint32_t palette[] = {0xffa657, 0x7ee787, 0xd2a8ff, 0x79c0ff, 0xf778ba,
                                       0xe3b341, 0x56d4dd, 0xffa198, 0xa5d6ff, 0xb4f1b4};
    const uint32_t h = (num ^ (num >> 11) ^ (num >> 21)) * 2654435761u;
    return lv_color_hex(palette[(h >> 8) % (sizeof(palette) / sizeof(palette[0]))]);
}

const lv_font_t* font()
{
    return mz_font_main();
}

const lv_font_t* font_small()
{
    return mz_font_small();
}

lv_obj_t* box(lv_obj_t* parent)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const std::string& text, lv_color_t color, const lv_font_t* f)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f ? f : font(), 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text.c_str());
    return l;
}

void set_text(lv_obj_t* l, const std::string& text)
{
    const char* current = lv_label_get_text(l);
    if (current && text == current) return;
    lv_label_set_text(l, text.c_str());
}

void set_bg(lv_obj_t* obj, lv_color_t color)
{
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

// ------------------------------------------------------------- text cleanup

namespace {

struct Replacement {
    uint32_t cp;
    const char* text;
};
constexpr Replacement kReplacements[] = {
    {0x2018, "'"},     {0x2019, "'"},     {0x201C, "\""},   {0x201D, "\""},     {0x2013, "-"},      {0x2014, "-"},
    {0x2026, "..."},   {0x00A0, " "},     {0x2764, "<3"},   {0x1F44D, "+1"},    {0x1F44E, "-1"},    {0x1F602, ":'D"},
    {0x1F923, ":'D"},  {0x1F600, ":D"},   {0x1F603, ":D"},  {0x1F604, ":D"},    {0x1F601, ":D"},    {0x1F642, ":)"},
    {0x1F60A, ":)"},   {0x1F609, ";)"},   {0x1F622, ":'("}, {0x1F62D, ":'("},   {0x1F641, ":("},    {0x1F61E, ":("},
    {0x1F621, ">:("},  {0x1F62E, ":o"},   {0x1F914, ":?"},  {0x1F44B, "o/"},    {0x1F64F, "_/\\_"}, {0x1F389, "\\o/"},
    {0x1F525, "(fire)"}, {0x2705, "[v]"}, {0x274C, "[x]"},  {0x2753, "?"},      {0x2757, "!"},      {0x1F4E1, "(ant)"},
    {0x1F4CD, "(pin)"}, {0x1F50B, "(bat)"}, {0x26A1, "(zap)"}, {0x1F680, "(rocket)"}, {0x1F440, "(eyes)"}, {0x1F4AF, "100"},
    {0x1F60D, "<3"},   {0x1F970, "<3"},   {0x1F499, "<3"},  {0x1F49A, "<3"},    {0x1F60E, "B)"},    {0x1F31E, "(sun)"},
    {0x2600, "(sun)"}, {0x1F327, "(rain)"}, {0x2744, "(snow)"}, {0x1F37B, "(beer)"}, {0x1F37A, "(beer)"}, {0x2615, "(coffee)"},
};

// Asking the TrueType fallbacks for a glyph they lack is slow and noisy, so
// each code point is looked up once.
bool font_has(uint32_t cp)
{
    static std::unordered_map<uint32_t, bool> known;
    const auto it = known.find(cp);
    if (it != known.end()) return it->second;
    lv_font_glyph_dsc_t dsc;
    const bool has = lv_font_get_glyph_dsc(font(), &dsc, cp, 0) && !dsc.is_placeholder;
    if (known.size() < 4096) known[cp] = has;
    return has;
}

void append_utf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

std::string clean(const std::string& text, bool single_line)
{
    std::string out;
    out.reserve(text.size());
    const auto* s = reinterpret_cast<const uint8_t*>(text.data());
    const size_t n = text.size();
    for (size_t i = 0; i < n;) {
        uint32_t cp = s[i];
        size_t len  = 1;
        if (cp >= 0xF0 && i + 3 < n) {
            cp  = ((cp & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            len = 4;
        } else if (cp >= 0xE0 && i + 2 < n) {
            cp  = ((cp & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
        } else if (cp >= 0xC0 && i + 1 < n) {
            cp  = ((cp & 0x1F) << 6) | (s[i + 1] & 0x3F);
            len = 2;
        } else if (cp >= 0x80) {
            cp = '?';
        }
        i += len;

        if (cp == '\n' || cp == '\r' || cp == '\t') {
            if (cp == '\r') continue;
            out.push_back(single_line || cp == '\t' ? ' ' : '\n');
            continue;
        }
        if (cp < 0x20) continue;
        // Invisible modifiers that only make sense next to a colour emoji.
        if (cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x200B || (cp >= 0x1F3FB && cp <= 0x1F3FF)) continue;
        if (cp < 0x7F || font_has(cp)) {
            append_utf8(out, cp);
            continue;
        }
        const char* replacement = nullptr;
        for (const auto& r : kReplacements) {
            if (r.cp == cp) {
                replacement = r.text;
                break;
            }
        }
        out += replacement ? replacement : (cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) ? "(*)" : "?");
    }
    return out;
}

// ------------------------------------------------------------------- list

ListView::ListView(lv_obj_t* parent, int x, int y, int w, int h) : w_(w), h_(h)
{
    cont_ = lv_obj_create(parent);
    lv_obj_remove_style_all(cont_);
    lv_obj_set_pos(cont_, x, y);
    lv_obj_set_size(cont_, w, h);
    lv_obj_set_scroll_dir(cont_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(cont_, c_dim(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(cont_, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(cont_, 2, LV_PART_SCROLLBAR);
    lv_obj_remove_flag(cont_, LV_OBJ_FLAG_CLICKABLE);
}

bool ListView::selectable(int index) const
{
    return index >= 0 && index < static_cast<int>(items_.size()) && !items_[static_cast<size_t>(index)].header;
}

void ListView::rebuild()
{
    lv_obj_clean(cont_);
    rows_.clear();
    rows_.resize(items_.size());
    for (size_t i = 0; i < items_.size(); ++i) {
        Row& r = rows_[i];
        r.row  = box(cont_);
        lv_obj_set_pos(r.row, 0, static_cast<int>(i) * kRowH);
        lv_obj_set_size(r.row, w_, kRowH);
        int x = 4;
        if (prefix_w_ > 0) {
            r.prefix = label(r.row, "", c_text());
            lv_obj_set_pos(r.prefix, x, 1);
            lv_obj_set_width(r.prefix, prefix_w_);
            lv_label_set_long_mode(r.prefix, LV_LABEL_LONG_CLIP);
            x += prefix_w_ + 4;
        }
        r.text = label(r.row, "", c_text());
        lv_obj_set_pos(r.text, x, 1);
        lv_label_set_long_mode(r.text, LV_LABEL_LONG_DOT);
        lv_obj_set_height(r.text, kRowH - 1);
        r.right = label(r.row, "", c_dim());
        lv_obj_align(r.right, LV_ALIGN_TOP_RIGHT, -5, 1);
    }
}

void ListView::apply(size_t i)
{
    const ListItem& it = items_[i];
    Row& r             = rows_[i];
    const bool sel     = static_cast<int>(i) == selected_ && !it.header;
    if (sel) {
        set_bg(r.row, c_select());
    } else if (it.header) {
        set_bg(r.row, c_bar());
    } else {
        lv_obj_set_style_bg_opa(r.row, LV_OPA_TRANSP, 0);
    }
    if (r.prefix) {
        set_text(r.prefix, it.prefix);
        lv_obj_set_style_text_color(r.prefix, it.has_prefix_color ? it.prefix_color : c_text(), 0);
    }
    set_text(r.text, it.text);
    lv_obj_set_style_text_color(r.text, it.header ? c_accent() : (it.has_color ? it.color : c_text()), 0);
    lv_obj_set_style_text_font(r.text, font(), 0);
    set_text(r.right, it.right);
    lv_obj_set_style_text_color(r.right, it.has_right_color ? it.right_color : c_dim(), 0);
    // Leave room for the right-hand cell so the two never overlap.
    lv_obj_update_layout(r.right);
    const int right_w = it.right.empty() ? 0 : lv_obj_get_width(r.right) + 8;
    const int x       = lv_obj_get_x(r.text);
    lv_obj_set_width(r.text, w_ - x - right_w - 6);
    lv_obj_align(r.right, LV_ALIGN_TOP_RIGHT, -5, 1);
}

void ListView::set_items(const std::vector<ListItem>& items)
{
    const bool resize = items.size() != items_.size();
    items_            = items;
    if (resize) rebuild();
    if (selected_ >= static_cast<int>(items_.size())) selected_ = static_cast<int>(items_.size()) - 1;
    if (selected_ < 0) selected_ = 0;
    if (!selectable(selected_)) {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            if (selectable(i)) {
                selected_ = i;
                break;
            }
        }
    }
    for (size_t i = 0; i < items_.size(); ++i) apply(i);
    if (resize) select(selected_);
}

void ListView::select(int index)
{
    if (items_.empty()) {
        selected_ = 0;
        return;
    }
    if (index < 0) index = 0;
    if (index >= static_cast<int>(items_.size())) index = static_cast<int>(items_.size()) - 1;
    const int previous = selected_;
    selected_          = index;
    if (previous >= 0 && previous < static_cast<int>(rows_.size())) apply(static_cast<size_t>(previous));
    apply(static_cast<size_t>(selected_));
    // Keep a section header visible when its first entry is selected.
    const int top = selected_ > 0 && items_[static_cast<size_t>(selected_ - 1)].header ? selected_ - 1 : selected_;
    lv_obj_scroll_to_view(rows_[static_cast<size_t>(top)].row, LV_ANIM_OFF);
    lv_obj_scroll_to_view(rows_[static_cast<size_t>(selected_)].row, LV_ANIM_OFF);
}

void ListView::move(int delta)
{
    if (items_.empty()) return;
    const int step = delta > 0 ? 1 : -1;
    int target     = selected_;
    int remaining  = delta > 0 ? delta : -delta;
    int probe      = selected_;
    while (remaining > 0) {
        probe += step;
        if (probe < 0 || probe >= static_cast<int>(items_.size())) break;
        if (selectable(probe)) {
            target = probe;
            --remaining;
        }
    }
    if (target != selected_) {
        select(target);
    } else if (step < 0) {
        lv_obj_scroll_to_y(cont_, 0, LV_ANIM_OFF);
    }
}

bool ListView::handle_key(const Key& key)
{
    const int page = h_ / kRowH - 1;
    switch (key.k) {
        case K::Up:
            move(-1);
            return true;
        case K::Down:
            move(1);
            return true;
        case K::PageUp:
            move(-page);
            return true;
        case K::PageDown:
            move(page);
            return true;
        case K::Home:
            move(-100000);
            return true;
        case K::End:
            move(100000);
            return true;
        default:
            return false;
    }
}

// ------------------------------------------------------------- text field

TextField::TextField(lv_obj_t* parent, int x, int y, int w, const std::string& placeholder)
{
    ta_ = lv_textarea_create(parent);
    lv_obj_remove_style_all(ta_);
    lv_obj_set_pos(ta_, x, y);
    lv_obj_set_size(ta_, w, kRowH + 1);
    lv_textarea_set_one_line(ta_, true);
    lv_textarea_set_placeholder_text(ta_, placeholder.c_str());
    lv_obj_set_style_text_font(ta_, font(), 0);
    lv_obj_set_style_text_color(ta_, c_text(), 0);
    lv_obj_set_style_text_color(ta_, c_dim(), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_pad_left(ta_, 4, 0);
    lv_obj_set_style_pad_right(ta_, 4, 0);
    lv_obj_set_style_pad_top(ta_, 1, 0);
    set_bg(ta_, c_bar());
    // A steady block cursor: the default one blinks through an animation timer.
    lv_obj_set_style_bg_color(ta_, c_accent(), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta_, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_width(ta_, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(ta_, 0, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_state(ta_, LV_STATE_FOCUSED);
    lv_obj_remove_flag(ta_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(ta_, LV_SCROLLBAR_MODE_OFF);
}

void TextField::set_accept(const char* chars)
{
    lv_textarea_set_accepted_chars(ta_, chars);
}

std::string TextField::text() const
{
    return lv_textarea_get_text(ta_);
}

void TextField::set_text(const std::string& text)
{
    lv_textarea_set_text(ta_, text.c_str());
    lv_textarea_set_cursor_pos(ta_, LV_TEXTAREA_CURSOR_LAST);
}

bool TextField::handle_key(const Key& key)
{
    switch (key.k) {
        case K::Char: {
            if (key.ctrl) return false;
            if (strlen(lv_textarea_get_text(ta_)) >= max_bytes_) return true;
            char buf[5] = {};
            if (key.ch < 0x80) {
                buf[0] = static_cast<char>(key.ch);
            } else {
                return true;
            }
            lv_textarea_add_text(ta_, buf);
            return true;
        }
        case K::Backspace:
            lv_textarea_delete_char(ta_);
            return true;
        case K::Delete:
            lv_textarea_delete_char_forward(ta_);
            return true;
        case K::Left:
            lv_textarea_cursor_left(ta_);
            return true;
        case K::Right:
            lv_textarea_cursor_right(ta_);
            return true;
        case K::Home:
            lv_textarea_set_cursor_pos(ta_, 0);
            return true;
        case K::End:
            lv_textarea_set_cursor_pos(ta_, LV_TEXTAREA_CURSOR_LAST);
            return true;
        default:
            return false;
    }
}

}  // namespace mz::ui

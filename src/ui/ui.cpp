// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ui.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "ui/fonts/fonts.h"

namespace mz::ui {

lv_color_t c_bg()
{
    return lv_color_hex(0x0d1117);
}
lv_color_t c_bar()
{
    return lv_color_hex(0x1a2029);
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
    return lv_color_hex(0x67ea94);
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

const lv_font_t* font_bold()
{
    return mz_font_bold();
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

// ------------------------------------------------------------ scroll hints

namespace {

// Tell the two arrows apart from the area's other children.
char g_hint_up, g_hint_down;

void on_scrolled(lv_event_t* e)
{
    update_scroll_hints(static_cast<lv_obj_t*>(lv_event_get_target(e)));
}

lv_obj_t* make_hint(lv_obj_t* parent, const char* symbol, void* tag, lv_align_t where, int dy)
{
    lv_obj_t* l = label(parent, symbol, c_accent(), font_small());
    lv_obj_set_user_data(l, tag);
    // Stays put while the rest scrolls, and does not count as content.
    lv_obj_add_flag(l, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_hor(l, 3, 0);
    lv_obj_set_style_radius(l, 6, 0);
    set_bg(l, c_bar());
    lv_obj_align(l, where, 0, dy);
    return l;
}

}  // namespace

void add_scroll_hints(lv_obj_t* obj)
{
    bool watched = false;
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); ++i) {
        watched = watched || lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == on_scrolled;
    }
    if (!watched) lv_obj_add_event_cb(obj, on_scrolled, LV_EVENT_SCROLL, nullptr);
    make_hint(obj, LV_SYMBOL_UP, &g_hint_up, LV_ALIGN_TOP_MID, 0);
    make_hint(obj, LV_SYMBOL_DOWN, &g_hint_down, LV_ALIGN_BOTTOM_MID, 0);
    update_scroll_hints(obj);
}

void update_scroll_hints(lv_obj_t* obj)
{
    lv_obj_update_layout(obj);
    lv_obj_t* up   = nullptr;
    lv_obj_t* down = nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        if (lv_obj_get_user_data(child) == &g_hint_up) up = child;
        if (lv_obj_get_user_data(child) == &g_hint_down) down = child;
    }
    const auto show = [](lv_obj_t* hint, bool on) {
        if (!hint) return;
        if (on == lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN)) {
            if (on) {
                lv_obj_remove_flag(hint, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (on) lv_obj_move_foreground(hint);
    };
    show(up, lv_obj_get_scroll_top(obj) > 2);
    show(down, lv_obj_get_scroll_bottom(obj) > 2);
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
// MeshCard: one screen object paints the rows in view (see ui.h). Same look as
// the old label-per-row list: rounded cards, green selected card, headers,
// prefix cell, right cell or count badge, second line under the selected row.

namespace {

void list_draw_cb(lv_event_t* e)
{
    static_cast<ListView*>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e));
}

// Cuts text to fit a width, ending in "..." (or just cut, without dots).
std::string fit(const std::string& s, const lv_font_t* f, int w, bool dots)
{
    if (w <= 0 || s.empty()) return "";
    if (lv_text_get_width(s.c_str(), static_cast<uint32_t>(s.size()), f, 0) <= w) return s;
    const int ew = dots ? lv_text_get_width("...", 3, f, 0) : 0;
    size_t keep  = 0;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + n > s.size()) break;
        if (lv_text_get_width(s.c_str(), static_cast<uint32_t>(i + n), f, 0) + ew > w) break;
        i += n;
        keep = i;
    }
    return s.substr(0, keep) + (dots ? "..." : "");
}

void text_at(lv_layer_t* layer, const std::string& s, const lv_font_t* f, lv_color_t c, int x, int y, int w)
{
    if (s.empty() || w <= 0) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font       = f;
    d.color      = c;
    d.text       = s.c_str();
    d.text_local = 1;  // drawn later; keep a copy
    d.flag       = LV_TEXT_FLAG_EXPAND;
    lv_area_t a{x, y, x + w - 1, y + lv_font_get_line_height(f) - 1};
    lv_draw_label(layer, &d, &a);
}

void box_at(lv_layer_t* layer, lv_color_t c, lv_opa_t opa, int radius, int x1, int y1, int x2, int y2)
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color     = c;
    r.bg_opa       = opa;
    r.radius       = radius;
    r.border_width = 0;
    lv_area_t a{x1, y1, x2, y2};
    lv_draw_rect(layer, &r, &a);
}

int text_w(const std::string& s, const lv_font_t* f)
{
    return s.empty() ? 0 : lv_text_get_width(s.c_str(), static_cast<uint32_t>(s.size()), f, 0);
}

}  // namespace

ListView::ListView(lv_obj_t* parent, int x, int y, int w, int h) : w_(w), h_(h)
{
    cont_ = lv_obj_create(parent);
    lv_obj_remove_style_all(cont_);
    lv_obj_set_pos(cont_, x, y);
    lv_obj_set_size(cont_, w, h);
    lv_obj_remove_flag(cont_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont_, list_draw_cb, LV_EVENT_DRAW_MAIN, this);
}

bool ListView::selectable(int index) const
{
    return index >= 0 && index < static_cast<int>(items_.size()) && !items_[static_cast<size_t>(index)].header;
}

bool ListView::two_line_selected() const
{
    return active_ && selectable(selected_) && !items_[static_cast<size_t>(selected_)].sub.empty();
}

// Top of an item inside the list; the selected row may be two lines tall.
int ListView::y_of(size_t index) const
{
    int y = static_cast<int>(index) * kRowH;
    if (two_line_selected() && static_cast<int>(index) > selected_) y += kRowH - 3;
    return y;
}

int ListView::row_h(size_t index) const
{
    const bool two = two_line_selected() && static_cast<int>(index) == selected_;
    return two ? 2 * kRowH - kRowGap - 3 : kRowH - kRowGap;
}

int ListView::total_h() const
{
    return items_.empty() ? 0 : y_of(items_.size()) - kRowGap;
}

void ListView::redraw()
{
    lv_obj_invalidate(cont_);
}

void ListView::keep_in_view()
{
    if (active_ && !items_.empty() && selected_ >= 0 && selected_ < static_cast<int>(items_.size())) {
        int lo = selected_;
        if (lo > 0 && items_[static_cast<size_t>(lo - 1)].header) --lo;  // keep its section title in view
        const int top    = y_of(static_cast<size_t>(lo));
        const int bottom = y_of(static_cast<size_t>(selected_)) + row_h(static_cast<size_t>(selected_));
        if (bottom > scroll_ + h_) scroll_ = bottom - h_;
        if (top < scroll_) scroll_ = top;
    }
    const int max_scroll = total_h() - h_;
    if (scroll_ > max_scroll) scroll_ = max_scroll;
    if (scroll_ < 0) scroll_ = 0;
}

void ListView::draw(lv_layer_t* layer)
{
    lv_area_t a;
    lv_obj_get_coords(cont_, &a);
    const lv_color_t on_sel = c_bg();
    const int line          = lv_font_get_line_height(font());
    for (size_t i = 0; i < items_.size(); ++i) {
        const int y0 = y_of(i) - scroll_;
        const int rh = row_h(i);
        if (y0 + rh <= 0) continue;
        if (y0 >= h_) break;
        const ListItem& it = items_[i];
        const bool sel     = active_ && static_cast<int>(i) == selected_ && !it.header;
        const bool two     = sel && !it.sub.empty();
        const int rx1 = a.x1 + 2, rx2 = a.x1 + w_ - 3;
        const int ry1 = a.y1 + y0, ry2 = ry1 + rh - 1;
        if (sel) {
            box_at(layer, c_select(), LV_OPA_COVER, 5, rx1, ry1, rx2, ry2);
        } else if (!it.header) {
            box_at(layer, c_bar(), LV_OPA_COVER, 5, rx1, ry1, rx2, ry2);
        }
        const int ty = ry1 + (kRowH - kRowGap - line) / 2;
        int x        = 6;
        if (prefix_w_ > 0) {
            const lv_color_t pc = sel ? on_sel : (it.has_prefix_color ? it.prefix_color : c_text());
            text_at(layer, fit(it.prefix, font_bold(), prefix_w_, false), font_bold(), pc, rx1 + x, ty, prefix_w_);
            x += prefix_w_ + 4;
        }
        // Right cell (or round count badge): measured first so the text stops short of it.
        int right_w = 0;
        if (!it.right.empty()) {
            const bool badge     = it.right_badge;
            const lv_font_t* rf  = badge ? font_bold() : font();
            const int tw         = text_w(it.right, rf);
            const int cell_w     = tw + (badge ? 12 : 0);
            const int cx2        = rx2 - 6;
            const int cx1        = cx2 - cell_w + 1;
            const int cy         = two ? ry1 + 1 : ty;
            if (badge) {
                box_at(layer, sel ? c_bg() : c_accent(), LV_OPA_COVER, 8, cx1, cy, cx2,
                       cy + lv_font_get_line_height(rf) - 1);
                text_at(layer, it.right, rf, sel ? c_accent() : c_bg(), cx1 + 6, cy, tw + 1);
            } else {
                const lv_color_t rc = sel ? on_sel : (it.has_right_color ? it.right_color : c_dim());
                text_at(layer, it.right, rf, rc, cx1, cy, tw + 1);
            }
            right_w = cell_w + 8;
        }
        const int text_w_px      = w_ - x - right_w - 6;
        const lv_font_t* tf      = it.header || sel ? font_bold() : font();
        const lv_color_t tc      = sel ? on_sel : it.header ? c_accent() : (it.has_color ? it.color : c_text());
        const std::string& body  = two || it.sub.empty() ? it.text : it.text + "  " + it.sub;
        text_at(layer, fit(body, tf, text_w_px, true), tf, tc, rx1 + x, ty, text_w_px);
        if (two) {
            text_at(layer, fit(it.sub, font(), text_w_px, true), font(), on_sel, rx1 + x, ry1 + kRowH - kRowGap - 2,
                    text_w_px);
        }
    }
    // Thin scrollbar and the two "more this way" arrows.
    const int total = total_h();
    if (total > h_) {
        int len = h_ * h_ / total;
        if (len < 8) len = 8;
        const int pos = (h_ - len) * scroll_ / (total - h_);
        box_at(layer, c_dim(), LV_OPA_70, 1, a.x2 - 1, a.y1 + pos, a.x2, a.y1 + pos + len - 1);
        const lv_font_t* hf = font_small();
        const int hh        = lv_font_get_line_height(hf);
        const auto arrow    = [&](const char* sym, int y) {
            const int sw = text_w(sym, hf) + 6;
            const int x1 = a.x1 + (w_ - sw) / 2;
            box_at(layer, c_bar(), LV_OPA_COVER, 6, x1, y, x1 + sw - 1, y + hh - 1);
            text_at(layer, sym, hf, c_accent(), x1 + 3, y, sw);
        };
        if (scroll_ > 2) arrow(LV_SYMBOL_UP, a.y1);
        if (scroll_ < total - h_ - 2) arrow(LV_SYMBOL_DOWN, a.y1 + h_ - hh);
    }
}

void ListView::set_height(int h)
{
    if (h == h_) return;
    h_ = h;
    lv_obj_set_height(cont_, h);
    keep_in_view();
    redraw();
}

void ListView::set_active(bool on)
{
    if (active_ == on) return;
    active_ = on;
    // Inactive lists show their top; the active one keeps the selection in view.
    if (!on) scroll_ = 0;
    keep_in_view();
    redraw();
}

// True when no selectable row sits above the selected one.
bool ListView::at_top() const
{
    for (int i = selected_ - 1; i >= 0; --i) {
        if (selectable(i)) return false;
    }
    return true;
}

void ListView::set_items(const ListItems& items)
{
    items_ = items;
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
    keep_in_view();
    redraw();
}

void ListView::select(int index)
{
    if (items_.empty()) {
        selected_ = 0;
        return;
    }
    if (index < 0) index = 0;
    if (index >= static_cast<int>(items_.size())) index = static_cast<int>(items_.size()) - 1;
    selected_ = index;
    keep_in_view();
    redraw();
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
    } else if (step < 0 && scroll_ != 0) {
        // Already on the first entry: show what is above it (a header).
        scroll_ = 0;
        redraw();
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
    lv_obj_set_style_pad_left(ta_, 8, 0);
    lv_obj_set_style_pad_right(ta_, 8, 0);
    lv_obj_set_style_pad_top(ta_, 1, 0);
    lv_obj_set_style_radius(ta_, 8, 0);  // MeshCard: rounded field
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

bool TextField::insert(const std::string& text)
{
    if (strlen(lv_textarea_get_text(ta_)) + text.size() > max_bytes_) return false;
    lv_textarea_add_text(ta_, text.c_str());
    return true;
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

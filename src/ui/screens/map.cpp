// SPDX-License-Identifier: GPL-3.0-only
// Radar-style map: nodes with a known position plotted around this device.
#include <algorithm>
#include <cmath>

#include "common/util.h"
#include "ui/app.h"

namespace mz {
namespace {

constexpr int kCx     = kContentW / 2;
constexpr int kCy     = kContentH / 2;
constexpr int kRadius = kContentH / 2 - 6;

// Ring distances the map snaps to, in metres.
const double kScales[] = {100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000};
constexpr int kScaleCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));

class MapScreen : public Screen {
public:
    explicit MapScreen(App& a) : Screen(a)
    {
        plot_ = ui::box(root);
        lv_obj_set_size(plot_, kContentW, kContentH);
        lv_obj_add_event_cb(
            plot_, [](lv_event_t* e) { static_cast<MapScreen*>(lv_event_get_user_data(e))->draw(lv_event_get_layer(e)); },
            LV_EVENT_DRAW_MAIN, this);
        // MeshCard: the keys and scale that used to be in the hint bar.
        info_ = ui::label(root, "", ui::c_dim(), ui::font_small());
        lv_obj_align(info_, LV_ALIGN_BOTTOM_RIGHT, -5, -3);
        note_ = ui::label(root, "", ui::c_dim());
        lv_obj_set_width(note_, kContentW - 30);
        lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
    }
    int tab() const override { return kTabMap; }
    std::string hint() const override
    {
        return "+/- zoom   0 auto   ring " + distance_str(scale_m_) + "   " + std::to_string(plotted_) + " nodes";
    }

    void refresh() override
    {
        if (mesh.rev_nodes == seen_rev_ && zoom_ == seen_zoom_ && built_) return;
        seen_rev_  = mesh.rev_nodes;
        seen_zoom_ = zoom_;
        built_     = true;
        plotted_   = 0;

        double my_lat = 0, my_lon = 0;
        has_pos_ = mesh.my_position(my_lat, my_lon);
        lv_obj_invalidate(plot_);
        if (!has_pos_) {
            lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
            ui::set_text(note_, "No position yet\n\nTurn on GPS in Settings > Position & GPS.");
            ui::set_text(info_, "");
            return;
        }
        lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

        struct Pos {
            uint32_t num;
            double dist;
            double bearing;
        };
        std::vector<Pos> found;
        double farthest = 0;
        for (const auto& [num, n] : mesh.nodes) {
            double lat, lon;
            if (num == mesh.my_num || !Mesh::node_position(n, lat, lon)) continue;
            const double d = geo_distance_m(my_lat, my_lon, lat, lon);
            found.push_back({num, d, geo_bearing_deg(my_lat, my_lon, lat, lon)});
            farthest = std::max(farthest, d);
        }
        int auto_scale = 0;
        while (auto_scale < kScaleCount - 1 && kScales[auto_scale] < farthest) ++auto_scale;
        const int index = zoom_ < 0 ? auto_scale : std::min(zoom_, kScaleCount - 1);
        scale_index_    = index;
        scale_m_        = kScales[index];

        // Far nodes first so near ones are drawn on top.
        std::sort(found.begin(), found.end(), [](const Pos& a, const Pos& b) { return a.dist > b.dist; });
        dots_.clear();
        for (const Pos& d : found) {
            const bool outside = d.dist > scale_m_;
            const double r     = outside ? kRadius + 3 : d.dist / scale_m_ * kRadius;
            const double rad   = d.bearing * M_PI / 180.0;
            Dot dot;
            dot.x       = kCx + static_cast<int>(std::lround(std::sin(rad) * r));
            dot.y       = kCy - static_cast<int>(std::lround(std::cos(rad) * r));
            dot.outside = outside;
            dot.color   = outside ? ui::c_dim() : ui::node_color(d.num);
            if (!outside) {
                dot.name = ui::clean(mesh.short_name(d.num), true);
                ++plotted_;
            }
            dots_.push_back(std::move(dot));
        }
        scale_text_ = distance_str(scale_m_) + "\n" + distance_str(scale_m_ / 2) + " inner";
        none_       = found.empty();
        ui::set_text(info_, std::to_string(plotted_) + (plotted_ == 1 ? " node" : " nodes") + "\nUp/Down zoom");
        lv_obj_align(info_, LV_ALIGN_BOTTOM_RIGHT, -5, -3);
        lv_obj_move_foreground(info_);
        lv_obj_invalidate(plot_);
    }

    // MeshCard: the map is ONE screen object that paints itself (like the lists), so it
    // costs the same with 3 nodes or 80. A dot and a name object per node could run the
    // ADV out of memory with many nodes on the map.
    void draw(lv_layer_t* layer)
    {
        if (!has_pos_) return;
        lv_area_t a;
        lv_obj_get_coords(plot_, &a);
        const int ox = a.x1, oy = a.y1;
        const auto circle = [&](int cx, int cy, int r, lv_color_t fill, bool filled, lv_color_t border, int bw) {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.radius       = LV_RADIUS_CIRCLE;
            d.bg_color     = fill;
            d.bg_opa       = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
            d.border_color = border;
            d.border_width = bw;
            d.border_opa   = bw ? LV_OPA_COVER : LV_OPA_TRANSP;
            lv_area_t r_a{ox + cx - r, oy + cy - r, ox + cx - r + 2 * r - 1, oy + cy - r + 2 * r - 1};
            lv_draw_rect(layer, &d, &r_a);
        };
        const auto text = [&](const std::string& s, lv_color_t c, int x, int y) {
            if (s.empty()) return;
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font       = ui::font_small();
            d.color      = c;
            d.text       = s.c_str();
            d.text_local = 1;  // drawn later; keep a copy
            lv_area_t t_a{ox + x, oy + y, ox + kContentW - 1, oy + y + 2 * lv_font_get_line_height(d.font)};
            lv_draw_label(layer, &d, &t_a);
        };
        for (const int r : {kRadius, kRadius / 2}) circle(kCx, kCy, r, ui::c_bg(), false, lv_color_hex(0x2a3948), 1);
        text("N", ui::c_dim(), kCx - 3, kCy - kRadius - 1);
        for (const Dot& d : dots_) {
            const int r = d.outside ? 1 : 2;
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.radius   = LV_RADIUS_CIRCLE;
            rd.bg_color = d.color;
            rd.bg_opa   = LV_OPA_COVER;
            lv_area_t d_a{ox + d.x - 2, oy + d.y - 2, ox + d.x - 2 + 2 * r, oy + d.y - 2 + 2 * r};
            lv_draw_rect(layer, &rd, &d_a);
            if (!d.outside) text(d.name, d.color, d.x + 4, d.y - 6);
        }
        circle(kCx, kCy, 3, ui::c_me(), true, ui::c_text(), 1);
        text(scale_text_, ui::c_dim(), 5, 3);
        if (none_) text("No other positions yet", ui::c_dim(), 5, kContentH - 3 - lv_font_get_line_height(ui::font_small()));
    }

    bool on_key(const Key& key) override
    {
        if (key.is_char('+') || key.is_char('=') || key.k == K::Up) {
            zoom_ = std::max(0, scale_index_ - 1);
            return true;
        }
        if (key.is_char('-') || key.is_char('_') || key.k == K::Down) {
            zoom_ = std::min(kScaleCount - 1, scale_index_ + 1);
            return true;
        }
        if (key.is_char('0')) {
            zoom_ = -1;
            return true;
        }
        return false;
    }

private:
    lv_obj_t* plot_      = nullptr;
    lv_obj_t* note_      = nullptr;
    lv_obj_t* info_      = nullptr;
    uint32_t seen_rev_   = 0;
    int seen_zoom_       = -2;
    int zoom_            = -1;  // -1: fit all nodes
    int scale_index_     = 0;
    double scale_m_      = 1000;
    int plotted_         = 0;
    bool built_          = false;
    struct Dot {
        int x = 0, y = 0;
        bool outside = false;
        lv_color_t color{};
        std::string name;  // a short name: fits in the string itself, no extra memory
    };
    std::vector<Dot> dots_;
    std::string scale_text_;
    bool none_    = false;
    bool has_pos_ = false;
};

}  // namespace

std::unique_ptr<Screen> make_map(App& app)
{
    return std::make_unique<MapScreen>(app);
}

}  // namespace mz

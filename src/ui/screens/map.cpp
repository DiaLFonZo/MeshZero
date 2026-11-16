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
        lv_obj_clean(plot_);
        plotted_ = 0;

        double my_lat = 0, my_lon = 0;
        if (!mesh.my_position(my_lat, my_lon)) {
            lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
            ui::set_text(note_, "No position yet\n\nEnable GPS in Setup > Position & GPS.");
            return;
        }
        lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

        struct Dot {
            uint32_t num;
            double dist;
            double bearing;
        };
        std::vector<Dot> dots;
        double farthest = 0;
        for (const auto& [num, n] : mesh.nodes) {
            double lat, lon;
            if (num == mesh.my_num || !Mesh::node_position(n, lat, lon)) continue;
            const double d = geo_distance_m(my_lat, my_lon, lat, lon);
            dots.push_back({num, d, geo_bearing_deg(my_lat, my_lon, lat, lon)});
            farthest = std::max(farthest, d);
        }
        int auto_scale = 0;
        while (auto_scale < kScaleCount - 1 && kScales[auto_scale] < farthest) ++auto_scale;
        const int index = zoom_ < 0 ? auto_scale : std::min(zoom_, kScaleCount - 1);
        scale_index_    = index;
        scale_m_        = kScales[index];

        for (const int r : {kRadius, kRadius / 2}) {
            lv_obj_t* ring = ui::box(plot_);
            lv_obj_set_size(ring, r * 2, r * 2);
            lv_obj_set_pos(ring, kCx - r, kCy - r);
            lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(ring, 1, 0);
            lv_obj_set_style_border_color(ring, lv_color_hex(0x2a3948), 0);
        }
        lv_obj_t* north = ui::label(plot_, "N", ui::c_dim(), ui::font_small());
        lv_obj_set_pos(north, kCx - 3, kCy - kRadius - 1);

        // Far nodes first so near ones are drawn on top.
        std::sort(dots.begin(), dots.end(), [](const Dot& a, const Dot& b) { return a.dist > b.dist; });
        for (const Dot& d : dots) {
            const bool outside = d.dist > scale_m_;
            const double r     = outside ? kRadius + 3 : d.dist / scale_m_ * kRadius;
            const double rad   = d.bearing * M_PI / 180.0;
            const int x        = kCx + static_cast<int>(std::lround(std::sin(rad) * r));
            const int y        = kCy - static_cast<int>(std::lround(std::cos(rad) * r));
            const lv_color_t color = outside ? ui::c_dim() : ui::node_color(d.num);
            lv_obj_t* dot = ui::box(plot_);
            lv_obj_set_size(dot, outside ? 3 : 5, outside ? 3 : 5);
            lv_obj_set_pos(dot, x - 2, y - 2);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            ui::set_bg(dot, color);
            if (!outside) {
                lv_obj_t* name = ui::label(plot_, ui::clean(mesh.short_name(d.num), true), color, ui::font_small());
                lv_obj_set_pos(name, x + 4, y - 6);
                ++plotted_;
            }
        }
        lv_obj_t* me = ui::box(plot_);
        lv_obj_set_size(me, 7, 7);
        lv_obj_set_pos(me, kCx - 3, kCy - 3);
        lv_obj_set_style_radius(me, LV_RADIUS_CIRCLE, 0);
        ui::set_bg(me, ui::c_me());
        lv_obj_set_style_border_width(me, 1, 0);
        lv_obj_set_style_border_color(me, ui::c_text(), 0);

        lv_obj_t* scale = ui::label(plot_, distance_str(scale_m_) + "\n" + distance_str(scale_m_ / 2) + " inner", ui::c_dim(),
                                    ui::font_small());
        lv_obj_set_pos(scale, 5, 3);
        if (dots.empty()) {
            lv_obj_t* none = ui::label(plot_, "No other positions yet", ui::c_dim(), ui::font_small());
            lv_obj_align(none, LV_ALIGN_BOTTOM_LEFT, 5, -3);
        }
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
    uint32_t seen_rev_   = 0;
    int seen_zoom_       = -2;
    int zoom_            = -1;  // -1: fit all nodes
    int scale_index_     = 0;
    double scale_m_      = 1000;
    int plotted_         = 0;
    bool built_          = false;
};

}  // namespace

std::unique_ptr<Screen> make_map(App& app)
{
    return std::make_unique<MapScreen>(app);
}

}  // namespace mz

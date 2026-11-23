// SPDX-License-Identifier: GPL-3.0-only
// The nodes with a known position, either on a radar around this device or
// pinned on a street or topographic map.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/util.h"
#include "ui/app.h"
#include "ui/tiles.h"

namespace mz {
namespace {

constexpr int kCx     = kContentW / 2;
constexpr int kCy     = kContentH / 2;
constexpr int kRadius = kContentH / 2 - 6;

// Ring distances the map snaps to, in metres.
const double kScales[] = {100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000};
constexpr int kScaleCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
// The same number of them in feet and miles.
const double kScalesImperial[kScaleCount] = {250 * kFootM, 500 * kFootM, 1000 * kFootM, 0.5 * kMileM, 1 * kMileM,
                                             2.5 * kMileM, 5 * kMileM,   10 * kMileM,   25 * kMileM,  50 * kMileM,
                                             100 * kMileM, 250 * kMileM, 500 * kMileM};

// ---- tile map ----------------------------------------------------------------
constexpr int kFitZoomMax   = 16;  // as close as fitting the nodes goes by itself
constexpr int kAloneZoom    = 13;  // with nobody else to fit
constexpr int kZoomMin      = 2;
constexpr int kStandIn      = 5;   // zoom levels up to look for a tile to enlarge
constexpr uint16_t kNoTile  = 0x18e4;  // where there is no map, a dark grey
constexpr uint16_t kNoTileLine = 0x2969;

// Lengths the scale bar snaps to, in metres.
const double kBars[] = {10,    20,    50,    100,    200,    500,    1000,    2000,
                        5000,  10000, 20000, 50000,  100000, 200000, 500000,  1000000};

// The same in feet and miles: up to 1000 ft, then from a quarter mile.
const double kBarsImperial[] = {50 * kFootM,   100 * kFootM, 200 * kFootM, 500 * kFootM, 1000 * kFootM, 0.25 * kMileM,
                                0.5 * kMileM,  1 * kMileM,   2 * kMileM,   5 * kMileM,   10 * kMileM,   20 * kMileM,
                                50 * kMileM,   100 * kMileM, 200 * kMileM, 500 * kMileM};

lv_color_t c_ink()
{
    return lv_color_hex(0x0d1117);
}

// A label on a dark plate, readable on any map.
lv_obj_t* plate(lv_obj_t* parent, const std::string& text, lv_color_t color)
{
    lv_obj_t* l = ui::label(parent, text, color, ui::font_small());
    lv_obj_set_style_bg_color(l, c_ink(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(l, 2, 0);
    lv_obj_set_style_pad_bottom(l, 1, 0);
    lv_obj_set_style_radius(l, 2, 0);
    return l;
}

class MapScreen : public Screen {
public:
    explicit MapScreen(App& a) : Screen(a)
    {
        plot_ = ui::box(root);
        lv_obj_set_size(plot_, kContentW, kContentH);
        pixels_.assign(static_cast<size_t>(kContentW) * kContentH, kNoTile);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_buffer(canvas_, pixels_.data(), kContentW, kContentH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(canvas_, 0, 0);
        pins_ = ui::box(root);
        lv_obj_set_size(pins_, kContentW, kContentH);
        layer_ = tiles::saved_layer();
        note_ = ui::label(root, "", ui::c_dim());
        lv_obj_set_width(note_, kContentW - 30);
        lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
    }
    int tab() const override { return kTabMap; }
    std::string hint() const override
    {
        if (layer_ < 0) {
            return "+/- zoom   0 auto   m map   ring " + distance_str(scale_m_, ui::imperial());
        }
        if (moving_) return "arrows move   +/- zoom   0 fit   enter done";
        std::string state;
        if (missing_ > 0) {
            state = !tiles::downloads()    ? "  no tiles"
                    : tiles::failing()     ? "  offline"
                    : tiles::pending() > 0 ? "  loading"
                                           : "  no map here";
        }
        return std::string("+/- zoom   0 fit   m ") + tiles::layer(layer_).name + "   enter move" + state;
    }

    void refresh() override
    {
        const bool map = layer_ >= 0;
        if (ui::imperial() != seen_imperial_) {
            seen_imperial_ = ui::imperial();
            dirty_         = true;
        }
        if (map != showing_map_ || !built_) {
            showing_map_ = map;
            dirty_       = true;
            const auto show = [](lv_obj_t* o, bool on) {
                if (on) {
                    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
                }
            };
            show(plot_, !map);
            show(canvas_, map);
            show(pins_, map);
            lv_obj_move_foreground(note_);
        }
        if (map) {
            refresh_map();
        } else {
            refresh_radar();
        }
        built_ = true;
        dirty_ = false;
    }

    bool on_key(const Key& key) override
    {
        if (key.is_char('m') || key.is_char('M')) {
            layer_  = layer_ + 1 > tiles::kTopo ? -1 : layer_ + 1;
            moving_ = false;
            dirty_  = true;
            if (layer_ >= 0) zoom_level_ = std::min(zoom_level_, tiles::layer(source()).max_zoom);
            tiles::save_layer(layer_);
            return true;
        }
        return layer_ < 0 ? radar_key(key) : map_key(key);
    }

private:
    // ---- tile map ------------------------------------------------------------

    // Where the tiles come from. The topographic map is the one in feet when
    // the map's units are imperial.
    int source() const { return layer_ == tiles::kTopo && ui::imperial() ? tiles::kUsTopo : layer_; }

    static std::string count_str(int nodes) { return std::to_string(nodes) + (nodes == 1 ? " node" : " nodes"); }

    bool map_key(const Key& key)
    {
        const int max_zoom = tiles::layer(source()).max_zoom;
        const bool in      = key.is_char('+') || key.is_char('=');
        const bool out     = key.is_char('-') || key.is_char('_');
        if (in || out || (!moving_ && (key.k == K::Up || key.k == K::Down))) {
            const bool closer = in || key.k == K::Up;
            zoom_level_       = std::max(kZoomMin, std::min(max_zoom, zoom_level_ + (closer ? 1 : -1)));
            fit_              = false;
            dirty_            = true;
            return true;
        }
        if (key.is_char('0')) {
            fit_ = follow_ = dirty_ = true;
            moving_                 = false;
            return true;
        }
        if (key.k == K::Enter && !key.repeat) {
            moving_ = !moving_;
            return true;
        }
        if (!moving_) return false;
        if (key.k == K::Esc) {
            moving_ = false;
            return true;
        }
        int dx = 0, dy = 0;
        if (key.k == K::Left) dx = -kContentW / 4;
        if (key.k == K::Right) dx = kContentW / 4;
        if (key.k == K::Up) dy = -kContentH / 4;
        if (key.k == K::Down) dy = kContentH / 4;
        if (dx == 0 && dy == 0) return key.k == K::Tab;  // stay on the map while moving it
        double x = 0, y = 0;
        geo_to_world_px(lat_, lon_, zoom_level_, x, y);
        const double size = world_size(zoom_level_);
        x                 = std::fmod(x + dx + size, size);
        y                 = std::max(0.0, std::min(size - 1, y + dy));
        world_px_to_geo(x, y, zoom_level_, lat_, lon_);
        follow_ = fit_ = false;
        dirty_         = true;
        return true;
    }

    static double world_size(int zoom) { return static_cast<double>(kTilePx) * static_cast<double>(1u << zoom); }

    struct Pin {
        uint32_t num;
        double lat, lon;
    };

    void refresh_map()
    {
        if (!dirty_ && mesh.rev_nodes == seen_rev_ && tiles::revision() == seen_tiles_ &&
            (missing_ == 0 || now_ms() < retry_ms_)) {
            return;
        }
        seen_rev_   = mesh.rev_nodes;
        seen_tiles_ = tiles::revision();
        // Tiles that could not be had are asked for again now and then.
        retry_ms_ = now_ms() + 5000;

        double my_lat = 0, my_lon = 0;
        const bool have_me = mesh.my_position(my_lat, my_lon);
        std::vector<Pin> pins;
        for (const auto& [num, n] : mesh.nodes) {
            double lat, lon;
            if (num != mesh.my_num && Mesh::node_position(n, lat, lon)) pins.push_back({num, lat, lon});
        }
        if (!have_me && pins.empty() && !placed_) {
            lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clean(pins_);
            ui::set_text(note_, "No position yet\n\nEnable GPS in Setup > Position & GPS.");
            plotted_ = missing_ = 0;
            return;
        }
        lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);

        if (follow_ && (have_me || !pins.empty())) {
            if (have_me) {
                lat_ = my_lat;
                lon_ = my_lon;
            } else {
                // Without a position of our own: the middle of the others.
                double lat_lo = 90, lat_hi = -90, lon_lo = 180, lon_hi = -180;
                for (const Pin& p : pins) {
                    lat_lo = std::min(lat_lo, p.lat);
                    lat_hi = std::max(lat_hi, p.lat);
                    lon_lo = std::min(lon_lo, p.lon);
                    lon_hi = std::max(lon_hi, p.lon);
                }
                lat_ = (lat_lo + lat_hi) / 2;
                lon_ = (lon_lo + lon_hi) / 2;
            }
            placed_ = true;
        }
        const int max_zoom = tiles::layer(source()).max_zoom;
        if (fit_) {
            // The closest zoom that still has every node on screen.
            int z = pins.empty() ? kAloneZoom : std::min(kFitZoomMax, max_zoom);
            for (; z > kZoomMin && !pins.empty(); --z) {
                double cx = 0, cy = 0;
                geo_to_world_px(lat_, lon_, z, cx, cy);
                bool all = true;
                for (const Pin& p : pins) {
                    double x = 0, y = 0;
                    geo_to_world_px(p.lat, p.lon, z, x, y);
                    if (std::fabs(wrap(x - cx, z)) > kContentW / 2 - 18 || std::fabs(y - cy) > kContentH / 2 - 10) {
                        all = false;
                        break;
                    }
                }
                if (all) break;
            }
            zoom_level_ = z;
        }
        zoom_level_ = std::max(kZoomMin, std::min(max_zoom, zoom_level_));

        double cx = 0, cy = 0;
        geo_to_world_px(lat_, lon_, zoom_level_, cx, cy);
        const int64_t ox = static_cast<int64_t>(std::llround(cx)) - kContentW / 2;
        const int64_t oy = static_cast<int64_t>(std::llround(cy)) - kContentH / 2;
        draw_tiles(ox, oy);

        lv_obj_clean(pins_);
        plotted_ = 0;
        // Far from the middle first, so what is being looked at ends up on top.
        std::sort(pins.begin(), pins.end(), [this](const Pin& a, const Pin& b) {
            return geo_distance_m(lat_, lon_, a.lat, a.lon) > geo_distance_m(lat_, lon_, b.lat, b.lon);
        });
        for (const Pin& p : pins) {
            int x = 0, y = 0;
            if (!on_screen(p.lat, p.lon, cx, cy, x, y)) continue;
            const lv_color_t color = ui::node_color(p.num);
            pin(x, y, 7, color, c_ink());
            lv_obj_t* name = plate(pins_, ui::clean(mesh.short_name(p.num), true), color);
            lv_obj_set_pos(name, x + 5, y - 6);
            ++plotted_;
        }
        int x = 0, y = 0;
        if (have_me && on_screen(my_lat, my_lon, cx, cy, x, y)) pin(x, y, 9, ui::c_me(), lv_color_white());

        // Scale bar: the longest round distance that fits in about a quarter of the width.
        const double per_px = metres_per_px(lat_, zoom_level_);
        const bool feet     = ui::imperial();
        double bar_m        = feet ? kBarsImperial[0] : kBars[0];
        for (const double m : feet ? kBarsImperial : kBars) {
            if (m / per_px <= kContentW / 4) bar_m = m;
        }
        const int bar_w = std::max(2, static_cast<int>(std::lround(bar_m / per_px)));
        lv_obj_t* bar   = ui::box(pins_);
        lv_obj_set_size(bar, bar_w, 4);
        lv_obj_set_pos(bar, 4, kContentH - 7);
        ui::set_bg(bar, c_ink());
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_white(), 0);
        lv_obj_t* length = plate(pins_, distance_str(bar_m, ui::imperial()), ui::c_text());
        lv_obj_set_pos(length, 4, kContentH - 21);
        // The maps are free to use on the condition that their makers are named.
        const char* by = used_other_ && used_source_ ? "(c) OpenStreetMap, SRTM, OpenTopoMap, USGS"
                         : used_other_               ? tiles::layer(tiles::kTopo).credit
                                                     : tiles::layer(source()).credit;
        lv_obj_t* credit = plate(pins_, by, ui::c_dim());
        lv_obj_align(credit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_t* count = plate(pins_, count_str(plotted_), ui::c_text());
        lv_obj_align(count, LV_ALIGN_TOP_RIGHT, -2, 2);
        lv_obj_invalidate(canvas_);
    }

    // The shorter way round the world between two x positions.
    static double wrap(double dx, int zoom)
    {
        const double size = world_size(zoom);
        if (dx > size / 2) dx -= size;
        if (dx < -size / 2) dx += size;
        return dx;
    }

    bool on_screen(double lat, double lon, double cx, double cy, int& x, int& y) const
    {
        double wx = 0, wy = 0;
        geo_to_world_px(lat, lon, zoom_level_, wx, wy);
        x = kContentW / 2 + static_cast<int>(std::lround(wrap(wx - cx, zoom_level_)));
        y = kContentH / 2 + static_cast<int>(std::lround(wy - cy));
        return x >= 0 && x < kContentW && y >= 0 && y < kContentH;
    }

    void pin(int x, int y, int size, lv_color_t color, lv_color_t edge)
    {
        lv_obj_t* dot = ui::box(pins_);
        lv_obj_set_size(dot, size, size);
        lv_obj_set_pos(dot, x - size / 2, y - size / 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        ui::set_bg(dot, color);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, edge, 0);
    }

    // Fills the picture with the tiles under it; ox, oy is its top left corner
    // in world pixels. A tile that is not there is stood in for by the part of
    // a tile from further out that covers it, enlarged, when there is one.
    // The map in feet ends at the borders of the United States: where it has
    // nothing, the one that covers the world is shown instead.
    void draw_tiles(int64_t ox, int64_t oy)
    {
        const int64_t count = int64_t{1} << zoom_level_;
        const auto floor_div = [](int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        const int from  = source();
        const int other = from == tiles::kUsTopo ? tiles::kTopo : -1;
        missing_        = 0;
        used_source_ = used_other_ = false;
        for (int64_t ty = floor_div(oy, kTilePx); ty * kTilePx < oy + kContentH; ++ty) {
            for (int64_t tx = floor_div(ox, kTilePx); tx * kTilePx < ox + kContentW; ++tx) {
                const int x0 = static_cast<int>(std::max<int64_t>(tx * kTilePx, ox) - ox);
                const int x1 = static_cast<int>(std::min<int64_t>((tx + 1) * kTilePx, ox + kContentW) - ox);
                const int y0 = static_cast<int>(std::max<int64_t>(ty * kTilePx, oy) - oy);
                const int y1 = static_cast<int>(std::min<int64_t>((ty + 1) * kTilePx, oy + kContentH) - oy);
                const int sx0 = static_cast<int>(x0 + ox - tx * kTilePx);  // where in the tile that is
                const int sy0 = static_cast<int>(y0 + oy - ty * kTilePx);
                const int wx  = static_cast<int>(((tx % count) + count) % count);
                const uint16_t* src = nullptr;
                int shift = 0, bx = 0, by = 0;
                if (ty >= 0 && ty < count) {
                    const int y = static_cast<int>(ty);
                    bool second = false;
                    src = tiles::get(from, zoom_level_, wx, y, true);
                    if (!src && other >= 0) {
                        // Asked for only once it is known that the first map has nothing here.
                        src    = tiles::get(other, zoom_level_, wx, y, tiles::absent(from, zoom_level_, wx, y));
                        second = src != nullptr;
                    }
                    if (!src) ++missing_;
                    for (int d = 1; !src && d <= kStandIn && d <= zoom_level_; ++d) {
                        src = tiles::get(from, zoom_level_ - d, wx >> d, y >> d, false);
                        if (!src && other >= 0) {
                            src    = tiles::get(other, zoom_level_ - d, wx >> d, y >> d, false);
                            second = src != nullptr;
                        }
                        if (!src) continue;
                        shift = d;
                        bx    = (wx & ((1 << d) - 1)) * (kTilePx >> d);
                        by    = (y & ((1 << d) - 1)) * (kTilePx >> d);
                    }
                    if (src) (second ? used_other_ : used_source_) = true;
                }
                for (int y = y0; y < y1; ++y) {
                    uint16_t* dst = &pixels_[static_cast<size_t>(y) * kContentW + x0];
                    const int sy  = sy0 + (y - y0);
                    if (!src) {
                        for (int x = x0; x < x1; ++x) {
                            const int sx = sx0 + (x - x0);
                            *dst++       = sx == 0 || sy == 0 ? kNoTileLine : kNoTile;
                        }
                    } else if (shift == 0) {
                        memcpy(dst, src + static_cast<size_t>(sy) * kTilePx + sx0, static_cast<size_t>(x1 - x0) * 2);
                    } else {
                        const uint16_t* row = src + static_cast<size_t>(by + (sy >> shift)) * kTilePx + bx;
                        for (int x = x0; x < x1; ++x) *dst++ = row[(sx0 + (x - x0)) >> shift];
                    }
                }
            }
        }
    }

    // ---- radar ---------------------------------------------------------------

    void refresh_radar()
    {
        if (!dirty_ && mesh.rev_nodes == seen_rev_ && zoom_ == seen_zoom_) return;
        seen_rev_  = mesh.rev_nodes;
        seen_zoom_ = zoom_;
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
        const double* scales = ui::imperial() ? kScalesImperial : kScales;
        while (auto_scale < kScaleCount - 1 && scales[auto_scale] < farthest) ++auto_scale;
        const int index = zoom_ < 0 ? auto_scale : std::min(zoom_, kScaleCount - 1);
        scale_index_    = index;
        scale_m_        = scales[index];

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

        lv_obj_t* scale = ui::label(plot_, distance_str(scale_m_, ui::imperial()) + "\n" + distance_str(scale_m_ / 2, ui::imperial()) + " inner", ui::c_dim(),
                                    ui::font_small());
        lv_obj_set_pos(scale, 5, 3);
        lv_obj_t* count = ui::label(plot_, count_str(plotted_), ui::c_dim(), ui::font_small());
        lv_obj_align(count, LV_ALIGN_TOP_RIGHT, -5, 3);
        if (dots.empty()) {
            lv_obj_t* none = ui::label(plot_, "No other positions yet", ui::c_dim(), ui::font_small());
            lv_obj_align(none, LV_ALIGN_BOTTOM_LEFT, 5, -3);
        }
    }

    bool radar_key(const Key& key)
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

    lv_obj_t* plot_      = nullptr;  // radar
    lv_obj_t* canvas_    = nullptr;  // map picture
    lv_obj_t* pins_      = nullptr;  // what is drawn on the map
    lv_obj_t* note_      = nullptr;
    std::vector<uint16_t> pixels_;
    int layer_           = -1;     // -1: radar, else tiles::kStreet or tiles::kTopo
    bool seen_imperial_  = false;
    bool used_source_    = false;  // tiles of the layer's own map are on screen
    bool used_other_     = false;  // and tiles of the one that fills in for it
    bool showing_map_    = false;
    bool dirty_          = true;
    bool follow_         = true;   // the middle stays on this device
    bool fit_            = true;   // the zoom keeps every node in view
    bool moving_         = false;  // arrows move the map
    bool placed_         = false;  // lat_, lon_ have been set
    double lat_          = 0;
    double lon_          = 0;
    int zoom_level_      = kAloneZoom;
    int missing_         = 0;      // tiles on screen that are not on disk
    uint32_t seen_tiles_ = 0;
    uint64_t retry_ms_   = 0;
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

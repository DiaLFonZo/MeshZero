// SPDX-License-Identifier: GPL-3.0-only
// Node list (home tab) and the per-node detail/actions screen.
#include <algorithm>
#include <cmath>

#include "common/util.h"
#include "ui/fonts/fonts.h"
#include "ui/app.h"

namespace mz {
namespace {

constexpr uint32_t kOnlineWindowS = 2 * 3600;

enum class Sort { Heard, Name, Hops, Distance, Count };
const char* sort_name(Sort s)
{
    switch (s) {
        case Sort::Heard:
            return "last heard";
        case Sort::Name:
            return "name";
        case Sort::Hops:
            return "hops";
        default:
            return "distance";
    }
}

std::string uptime_str(uint32_t s)
{
    char buf[24];
    if (s >= 86400) {
        snprintf(buf, sizeof(buf), "%ud %uh", s / 86400, (s % 86400) / 3600);
    } else if (s >= 3600) {
        snprintf(buf, sizeof(buf), "%uh %um", s / 3600, (s % 3600) / 60);
    } else {
        snprintf(buf, sizeof(buf), "%um", s / 60);
    }
    return buf;
}

std::string fmt(const char* format, double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), format, value);
    return buf;
}

// --------------------------------------------------------------- node list

class NodesScreen : public Screen {
public:
    explicit NodesScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        list_->set_prefix_width(42);
    }
    int tab() const override { return kTabNodes; }
    std::string hint() const override
    {
        return "Enter open   S sort: " + std::string(sort_name(sort_)) + "   " + std::to_string(online_) + "/" +
               std::to_string(total_) + " online";
    }

    void refresh() override
    {
#ifdef MESHCARD_ESP32
        // Rebuilding the whole list 4 times a second briefly needs a lot of memory on the
        // ADV: only when the nodes or the sort changed, else every 5 s for the "heard" ages.
        const uint64_t now_ms_v = now_ms();
        if (built_ms_ != 0 && mesh.rev_nodes == built_rev_ && sort_ == built_sort_ && now_ms_v - built_ms_ < 5000) return;
        built_ms_   = now_ms_v;
        built_rev_  = mesh.rev_nodes;
        built_sort_ = sort_;
#endif
        const uint32_t now = now_s();
        double my_lat = 0, my_lon = 0;
        const bool have_me = mesh.my_position(my_lat, my_lon);

        // Only what the list shows, copied per node: the full node record is 300+ bytes, and
        // on the ADV the nodes are read one at a time from the firmware (no pointers to keep).
        struct Entry {
            uint32_t num        = 0;
            uint32_t last_heard = 0;
            float snr           = 0;
            double dist         = -1;
            uint8_t hops_away   = 0;
            bool has_hops_away  = false;
            bool is_favorite    = false;
            bool is_ignored     = false;
            bool via_mqtt       = false;
            char long_name[sizeof(meshtastic_User::long_name)] = "";
        };
        std::vector<Entry> entries;
        entries.reserve(mesh.nodes.size());
        online_ = 0;
        for (const auto& [num, n] : mesh.nodes) {
            if (num == 0) continue;  // ADV: the list shrank while being read
            Entry e;
            double lat, lon;
            if (have_me && num != mesh.my_num && Mesh::node_position(n, lat, lon)) {
                e.dist = geo_distance_m(my_lat, my_lon, lat, lon);
            }
            e.num           = num;
            e.last_heard    = n.last_heard;
            e.snr           = n.snr;
            e.hops_away     = static_cast<uint8_t>(n.hops_away);
            e.has_hops_away = n.has_hops_away;
            e.is_favorite   = n.is_favorite;
            e.is_ignored    = n.is_ignored;
            e.via_mqtt      = n.via_mqtt;
            if (n.has_user) snprintf(e.long_name, sizeof(e.long_name), "%s", n.user.long_name);
            entries.push_back(e);
            if (num == mesh.my_num || (n.last_heard && now - n.last_heard < kOnlineWindowS)) ++online_;
        }
        total_ = static_cast<int>(entries.size());

        const uint32_t me = mesh.my_num;
        const Sort sort   = sort_;
        std::sort(entries.begin(), entries.end(), [me, sort](const Entry& a, const Entry& b) {
            if ((a.num == me) != (b.num == me)) return a.num == me;
            if (a.is_favorite != b.is_favorite) return a.is_favorite;
            switch (sort) {
                case Sort::Name: {
                    const int c = strcasecmp(a.long_name, b.long_name);
                    if (c != 0) return c < 0;
                    break;
                }
                case Sort::Hops: {
                    const int ha = a.has_hops_away ? a.hops_away : 99;
                    const int hb = b.has_hops_away ? b.hops_away : 99;
                    if (ha != hb) return ha < hb;
                    break;
                }
                case Sort::Distance: {
                    const double da = a.dist < 0 ? 1e12 : a.dist;
                    const double db = b.dist < 0 ? 1e12 : b.dist;
                    if (da != db) return da < db;
                    break;
                }
                default:
                    break;
            }
            if (a.last_heard != b.last_heard) return a.last_heard > b.last_heard;
            return a.num < b.num;
        });

        // Keep the cursor on the same node while the list reorders underneath.
        const uint32_t selected_num =
            static_cast<size_t>(list_->selected()) < nums_.size() ? nums_[static_cast<size_t>(list_->selected())] : 0;

        ui::ListItems items;
        nums_.clear();
        for (const Entry& e : entries) {
            const Entry& n = e;
            ui::ListItem it;
            it.prefix           = ui::clean(mesh.short_name(n.num), true);
            it.has_prefix_color = true;
            it.prefix_color     = n.num == me ? ui::c_me() : ui::node_color(n.num);
            it.text             = ui::clean((n.is_favorite && n.num != me ? "* " : "") + mesh.long_name(n.num), true);
            if (n.num == me) {
                it.right = "this device";
            } else {
                const bool online = n.last_heard && now - n.last_heard < kOnlineWindowS;
                std::string right;
                if (sort_ == Sort::Distance && e.dist >= 0) {
                    right = distance_str(e.dist) + "  ";
                } else if (n.via_mqtt) {
                    right = "mqtt  ";
                } else if (n.has_hops_away && n.hops_away > 0) {
                    right = std::to_string(n.hops_away) + (n.hops_away == 1 ? " hop  " : " hops  ");
                } else if (n.snr != 0) {
                    right = fmt("%.1fdB  ", n.snr);
                }
                right += age_str(n.last_heard, now);
                it.right = right;
                if (!online) {
                    it.has_color = true;
                    it.color     = ui::c_dim();
                }
                if (n.is_ignored) it.text = "(ignored) " + it.text;
            }
            items.push_back(std::move(it));
            nums_.push_back(n.num);
        }
        list_->set_items(items);
        if (selected_num != 0) {
            for (size_t i = 0; i < nums_.size(); ++i) {
                if (nums_[i] == selected_num && static_cast<int>(i) != list_->selected()) {
                    list_->select(static_cast<int>(i));
                    break;
                }
            }
        }
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        const size_t i = static_cast<size_t>(list_->selected());
        if (key.k == K::Enter && i < nums_.size()) {
            app.push(make_node_detail(app, nums_[i]));
            return true;
        }
        if (key.is_char('s') || key.is_char('S')) {
            sort_ = static_cast<Sort>((static_cast<int>(sort_) + 1) % static_cast<int>(Sort::Count));
            return true;
        }
        return false;
    }

private:
    std::unique_ptr<ui::ListView> list_;
    std::vector<uint32_t> nums_;
    Sort sort_  = Sort::Heard;
    int online_ = 0;
#ifdef MESHCARD_ESP32
    uint64_t built_ms_  = 0;
    uint32_t built_rev_ = 0;
    Sort built_sort_    = Sort::Heard;
#endif
    int total_  = 0;
};

// ------------------------------------------------------------- node detail

class NodeDetailScreen : public Screen {
public:
    // MeshCard layout: summary card, then [Message] [Trace route] buttons,
    // then the other actions and the details as a list.
    NodeDetailScreen(App& a, uint32_t num) : Screen(a), num_(num)
    {
        const bool self = num_ == mesh.my_num;
        card_ = ui::box(root);
        lv_obj_set_pos(card_, 2, 1);
        lv_obj_set_size(card_, kContentW - 4, kCardH);
        lv_obj_set_style_radius(card_, 6, 0);
        ui::set_bg(card_, ui::c_bar());
        name_ = ui::label(card_, "", ui::c_text(), ui::font_bold());
        lv_obj_set_pos(name_, 8, 1);
        lv_obj_set_width(name_, kContentW - 20);
        lv_label_set_long_mode(name_, LV_LABEL_LONG_DOT);
        facts_ = ui::label(card_, "", ui::c_dim(), mz_font(12, false));
        lv_obj_set_pos(facts_, 8, 19);
        lv_obj_set_width(facts_, kContentW - 20);
        lv_label_set_long_mode(facts_, LV_LABEL_LONG_DOT);

        int y = kCardH + 3;
        if (!self) {
            const int bw = (kContentW - 4 - 4) / 2;
            const char* names[2] = {LV_SYMBOL_ENVELOPE "  Message", LV_SYMBOL_SHUFFLE "  Trace route"};
            for (int i = 0; i < 2; ++i) {
                btn_[i] = ui::label(root, names[i], ui::c_text(), ui::font_bold());
                lv_obj_set_pos(btn_[i], 2 + i * (bw + 4), y);
                lv_obj_set_size(btn_[i], bw, kBtnH);
                lv_obj_set_style_radius(btn_[i], 6, 0);
                lv_obj_set_style_text_align(btn_[i], LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_pad_top(btn_[i], 2, 0);
            }
            y += kBtnH + 3;
            focus_buttons_ = true;
        }
        list_ = std::make_unique<ui::ListView>(root, 0, y, kContentW, kContentH - y);
        list_->set_prefix_width(16);
        list_->set_active(!focus_buttons_);
        paint_buttons();
    }
    std::string title() const override { return LV_SYMBOL_LEFT " Node"; }
    std::string hint() const override { return "Up/Down move   Enter select   Esc back"; }

    void refresh() override
    {
        const meshtastic_NodeInfo* n = mesh.node(num_);
        ui::ListItems items;
        actions_.clear();
        const auto action = [&](const char* icon, const std::string& text, std::function<void()> fn) {
            ui::ListItem it;
            it.prefix           = icon;
            it.has_prefix_color = true;
            it.prefix_color     = ui::c_accent();
            it.text             = text;
            it.has_color        = true;
            it.color            = ui::c_accent();
            items.push_back(it);
            actions_.push_back(std::move(fn));
        };
        const auto info = [&](const std::string& name, const std::string& value) {
            ui::ListItem it;
            it.text      = name;
            it.right     = ui::clean(value, true);
            it.has_color = true;
            it.color     = ui::c_dim();
            it.has_right_color = true;
            it.right_color     = ui::c_text();
            items.push_back(it);
            actions_.push_back(nullptr);
        };
        const auto header = [&](const std::string& text) {
            ui::ListItem it;
            it.text   = text;
            it.header = true;
            items.push_back(it);
            actions_.push_back(nullptr);
        };
        if (!n) {
            info("Node removed", "");
            list_->set_items(items);
            return;
        }
        const bool self   = num_ == mesh.my_num;
        const uint32_t id = num_;
        unmessagable_     = n->has_user && n->user.has_is_unmessagable && n->user.is_unmessagable;
        {
            ui::set_text(name_, ui::clean(mesh.short_name(num_) + "  " + mesh.long_name(num_), true));
            std::string f = node_id_str(num_);
            const auto add = [&f](const std::string& x) { f += "   " + x; };
            if (self) {
                add("this device");
            } else {
                if (n->has_hops_away) add(n->hops_away == 0 ? "direct" : std::to_string(n->hops_away) + " hops");
                if (n->snr != 0) add(fmt("%.1f dB", n->snr));
                if (n->last_heard) add(age_str(n->last_heard, now_s()) + " ago");
            }
            if (n->has_device_metrics && n->device_metrics.has_battery_level) {
                const uint32_t b = n->device_metrics.battery_level;
                add(b > 100 ? std::string(LV_SYMBOL_CHARGE) : std::string(LV_SYMBOL_BATTERY_FULL " ") + std::to_string(b) + "%");
            }
            ui::set_text(facts_, ui::clean(f, true));
        }
        paint_buttons();
        if (!self) {
            header("More");
            action(LV_SYMBOL_GPS, "Request position", [this, id]() { mesh.request_position(id); });
            action(LV_SYMBOL_CHARGE, "Request telemetry", [this, id]() { mesh.request_telemetry(id); });
            action(LV_SYMBOL_REFRESH, "Request node info", [this, id]() { mesh.request_nodeinfo(id); });
            const bool fav = n->is_favorite;
            action(fav ? LV_SYMBOL_MINUS : LV_SYMBOL_PLUS, fav ? "Remove from favorites" : "Add to favorites",
                   [this, id, fav]() { mesh.set_favorite(id, !fav); });
            const bool ign = n->is_ignored;
            action(ign ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE, ign ? "Stop ignoring" : "Ignore this node",
                   [this, id, ign]() { mesh.set_ignored(id, !ign); });
            action(LV_SYMBOL_TRASH, "Remove from node list", [this, id]() {
                app.push(make_confirm(app, "Remove node", "Remove " + mesh.long_name(id) + "?",
                                      {"Cancel", "Remove"}, [this, id](int choice) {
                                          if (choice == 1) {
                                              mesh.remove_node(id);
                                              app.pop();
                                          }
                                      }));
            });
        }

        header("Node");
        if (n->has_user) {
            info("Hardware", hw_model_name(n->user.hw_model));
            info("Role", role_name(n->user.role));
            info("Public key", n->user.public_key.size > 0 ? "yes" : "no");
            if (n->user.is_licensed) info("Licensed operator", "yes");
        }
        const uint32_t now = now_s();
        if (!self) {
            info("Last heard", n->last_heard ? age_str(n->last_heard, now) + " ago" : "never");
            if (n->via_mqtt) info("Heard via", "MQTT");
            if (n->has_hops_away) info("Hops away", n->hops_away == 0 ? "direct" : std::to_string(n->hops_away));
            if (n->snr != 0) info("SNR", fmt("%.2f dB", n->snr));
        }
        if (n->has_device_metrics) {
            const meshtastic_DeviceMetrics& d = n->device_metrics;
            header("Telemetry");
            if (d.has_battery_level) {
                info("Battery", d.battery_level > 100 ? "powered" : std::to_string(d.battery_level) + "%");
            }
            if (d.has_voltage && d.voltage > 0) info("Voltage", fmt("%.2f V", d.voltage));
            if (d.has_channel_utilization) info("Channel utilization", fmt("%.1f%%", d.channel_utilization));
            if (d.has_air_util_tx) info("Air time (TX)", fmt("%.1f%%", d.air_util_tx));
            if (d.has_uptime_seconds) info("Uptime", uptime_str(d.uptime_seconds));
        }
        double lat, lon;
        if (Mesh::node_position(*n, lat, lon)) {
            header("Position");
            char buf[48];
            snprintf(buf, sizeof(buf), "%.5f, %.5f", lat, lon);
            info("Coordinates", buf);
            if (n->position.has_altitude) info("Altitude", std::to_string(n->position.altitude) + " m");
            double my_lat, my_lon;
            if (!self && mesh.my_position(my_lat, my_lon)) {
                const double bearing = geo_bearing_deg(my_lat, my_lon, lat, lon);
                info("Distance", distance_str(geo_distance_m(my_lat, my_lon, lat, lon)));
                info("Bearing", fmt("%.0f deg ", bearing) + compass_point(bearing));
            }
            if (n->position.sats_in_view) info("Satellites", std::to_string(n->position.sats_in_view));
            if (n->position.time) info("Fix age", age_str(n->position.time, now));
        }
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (focus_buttons_) {
            switch (key.k) {
                case K::Left:
                case K::Right:
                    btn_sel_ = 1 - btn_sel_;
                    paint_buttons();
                    return true;
                case K::Down:
                    focus_buttons_ = false;
                    list_->set_active(true);
                    paint_buttons();
                    return true;
                case K::Enter:
                    if (btn_sel_ == 0) {
                        if (unmessagable_) {
                            app.toast("This node does not take messages", true);
                        } else {
                            app.open_chat(ChatKey{true, num_});
                        }
                    } else {
                        mesh.traceroute(num_);
                    }
                    return true;
                default:
                    return false;
            }
        }
        if (btn_[0] && key.k == K::Up && list_->at_top()) {
            focus_buttons_ = true;
            list_->set_active(false);
            paint_buttons();
            return true;
        }
        if (list_->handle_key(key)) return true;
        if (key.k == K::Enter) {
            const size_t i = static_cast<size_t>(list_->selected());
            if (i < actions_.size() && actions_[i]) {
                auto fn = actions_[i];
                fn();
            }
            return true;
        }
        return false;
    }

private:
    static constexpr int kCardH = 36;
    static constexpr int kBtnH  = 21;

    // Selected button: green with dark text; the other a dark card.
    void paint_buttons()
    {
        for (int i = 0; i < 2; ++i) {
            if (!btn_[i]) continue;
            const bool on  = focus_buttons_ && i == btn_sel_;
            const bool off = i == 0 && unmessagable_;
            ui::set_bg(btn_[i], on ? ui::c_accent() : ui::c_bar());
            lv_obj_set_style_text_color(btn_[i], on ? ui::c_bg() : off ? ui::c_dim() : ui::c_text(), 0);
        }
    }

    uint32_t num_;
    lv_obj_t* card_  = nullptr;
    lv_obj_t* name_  = nullptr;
    lv_obj_t* facts_ = nullptr;
    lv_obj_t* btn_[2]{};
    int btn_sel_        = 0;
    bool focus_buttons_ = false;
    bool unmessagable_  = false;
    std::unique_ptr<ui::ListView> list_;
    std::vector<std::function<void()>> actions_;
};

}  // namespace

std::unique_ptr<Screen> make_nodes(App& app)
{
    return std::make_unique<NodesScreen>(app);
}

std::unique_ptr<Screen> make_node_detail(App& app, uint32_t num)
{
    return std::make_unique<NodeDetailScreen>(app, num);
}

}  // namespace mz

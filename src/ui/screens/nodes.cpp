// SPDX-License-Identifier: GPL-3.0-only
// Node list (home tab) and the per-node detail/actions screen.
#include <algorithm>
#include <cmath>

#include "common/util.h"
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
        const uint32_t now = now_s();
        double my_lat = 0, my_lon = 0;
        const bool have_me = mesh.my_position(my_lat, my_lon);

        struct Entry {
            const meshtastic_NodeInfo* n;
            double dist;
        };
        std::vector<Entry> entries;
        online_ = 0;
        for (const auto& [num, n] : mesh.nodes) {
            double lat, lon, dist = -1;
            if (have_me && num != mesh.my_num && Mesh::node_position(n, lat, lon)) {
                dist = geo_distance_m(my_lat, my_lon, lat, lon);
            }
            entries.push_back({&n, dist});
            if (num == mesh.my_num || (n.last_heard && now - n.last_heard < kOnlineWindowS)) ++online_;
        }
        total_ = static_cast<int>(entries.size());

        const uint32_t me = mesh.my_num;
        const Sort sort   = sort_;
        std::sort(entries.begin(), entries.end(), [me, sort](const Entry& a, const Entry& b) {
            if ((a.n->num == me) != (b.n->num == me)) return a.n->num == me;
            if (a.n->is_favorite != b.n->is_favorite) return a.n->is_favorite;
            switch (sort) {
                case Sort::Name: {
                    const int c = strcasecmp(a.n->user.long_name, b.n->user.long_name);
                    if (c != 0) return c < 0;
                    break;
                }
                case Sort::Hops: {
                    const int ha = a.n->has_hops_away ? a.n->hops_away : 99;
                    const int hb = b.n->has_hops_away ? b.n->hops_away : 99;
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
            if (a.n->last_heard != b.n->last_heard) return a.n->last_heard > b.n->last_heard;
            return a.n->num < b.n->num;
        });

        // Keep the cursor on the same node while the list reorders underneath.
        const uint32_t selected_num =
            static_cast<size_t>(list_->selected()) < nums_.size() ? nums_[static_cast<size_t>(list_->selected())] : 0;

        std::vector<ui::ListItem> items;
        nums_.clear();
        for (const Entry& e : entries) {
            const meshtastic_NodeInfo& n = *e.n;
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
                    right = distance_str(e.dist, ui::imperial()) + "  ";
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
    int total_  = 0;
};

// ------------------------------------------------------------- node detail

class NodeDetailScreen : public Screen {
public:
    NodeDetailScreen(App& a, uint32_t num) : Screen(a), num_(num)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        list_->set_prefix_width(16);
    }
    std::string title() const override { return LV_SYMBOL_LEFT " " + mesh.short_name(num_) + "  " + mesh.long_name(num_); }
    std::string hint() const override { return "Up/Down move   Enter select   Esc back"; }
    void on_show() override { mesh.want_info(num_, false, true); }

    void refresh() override
    {
        const meshtastic_NodeInfo* n = mesh.node(num_);
        std::vector<ui::ListItem> items;
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
        if (!self) {
            header("Actions");
            const bool unmessagable = n->has_user && n->user.has_is_unmessagable && n->user.is_unmessagable;
            if (!unmessagable) {
                action(LV_SYMBOL_ENVELOPE, "Send direct message", [this, id]() { app.open_chat(ChatKey{true, id}); });
            }
            action(LV_SYMBOL_SHUFFLE, "Trace route", [this, id]() { mesh.traceroute(id); });
            action(LV_SYMBOL_GPS, "Request position", [this, id]() { mesh.request_position(id); });
            action(LV_SYMBOL_CHARGE, "Request telemetry", [this, id]() {
                app.push(make_picker(app, LV_SYMBOL_LEFT " Request telemetry",
                                     {"Device (battery, uptime)", "Environment sensors", "Power sensors",
                                      "Air quality sensors"},
                                     -1, [this, id](int choice) {
                                         if (choice < 0 || choice > 3) return;
                                         mesh.request_telemetry(id, static_cast<TelemetryKind>(choice));
                                     }));
            });
            action(LV_SYMBOL_REFRESH, "Request node info", [this, id]() { mesh.request_nodeinfo(id); });
            const bool fav = n->is_favorite;
            action(fav ? LV_SYMBOL_MINUS : LV_SYMBOL_PLUS, fav ? "Remove from favorites" : "Add to favorites",
                   [this, id, fav]() { mesh.set_favorite(id, !fav); });
            const bool ign = n->is_ignored;
            action(ign ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE, ign ? "Stop ignoring" : "Ignore this node",
                   [this, id, ign]() { mesh.set_ignored(id, !ign); });
            const bool muted = n->is_muted;
            action(muted ? LV_SYMBOL_BELL : LV_SYMBOL_MUTE,
                   muted ? "Unmute direct messages" : "Mute direct messages",
                   [this, id, muted]() { mesh.set_muted(ChatKey{true, id}, !muted); });
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
        info("ID", node_id_str(num_));
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
        const auto found = mesh.sensors.find(num_);
        if (found != mesh.sensors.end()) {
            const Sensors& s    = found->second;
            const bool imperial = ui::imperial();
            const auto temp     = [imperial](double c) {
                return imperial ? fmt("%.1f \xC2\xB0" "F", c * 9 / 5 + 32) : fmt("%.1f \xC2\xB0" "C", c);
            };
            const auto wind = [imperial](double ms) { return imperial ? fmt("%.1f mph", ms * 2.23694) : fmt("%.1f m/s", ms); };
            const auto rain = [imperial](double mm) { return imperial ? fmt("%.2f in", mm / 25.4) : fmt("%.1f mm", mm); };
            if (s.has_env) {
                const meshtastic_EnvironmentMetrics& e = s.env;
                header("Environment");
                info("Received", age_str(s.env_ts, now) + " ago");
                if (e.has_temperature) info("Temperature", temp(e.temperature));
                if (e.has_relative_humidity) info("Humidity", fmt("%.0f%%", e.relative_humidity));
                if (e.has_barometric_pressure) info("Pressure", fmt("%.1f hPa", e.barometric_pressure));
                if (e.has_gas_resistance) info("Gas resistance", fmt("%.2f MOhm", e.gas_resistance));
                if (e.has_iaq) info("Air quality index (IAQ)", std::to_string(e.iaq));
                if (e.has_voltage) info("Voltage", fmt("%.2f V", e.voltage));
                if (e.has_current) info("Current", fmt("%.1f mA", e.current));
                if (e.has_lux) info("Light", fmt("%.0f lx", e.lux));
                if (e.has_white_lux) info("White light", fmt("%.0f lx", e.white_lux));
                if (e.has_ir_lux) info("Infrared", fmt("%.0f lx", e.ir_lux));
                if (e.has_uv_lux) info("Ultraviolet", fmt("%.0f lx", e.uv_lux));
                if (e.has_wind_speed) info("Wind", wind(e.wind_speed));
                if (e.has_wind_direction) {
                    info("Wind from", std::to_string(e.wind_direction) + " deg " + compass_point(e.wind_direction));
                }
                if (e.has_wind_gust) info("Wind gust", wind(e.wind_gust));
                if (e.has_wind_lull) info("Wind lull", wind(e.wind_lull));
                if (e.has_rainfall_1h) info("Rain, last hour", rain(e.rainfall_1h));
                if (e.has_rainfall_24h) info("Rain, last 24 hours", rain(e.rainfall_24h));
                if (e.has_soil_moisture) info("Soil moisture", std::to_string(e.soil_moisture) + "%");
                if (e.has_soil_temperature) info("Soil temperature", temp(e.soil_temperature));
                if (e.has_distance) info("Distance sensor", fmt("%.0f mm", e.distance));
                if (e.has_weight) info("Weight", fmt("%.2f kg", e.weight));
                if (e.has_radiation) info("Radiation", fmt("%.2f uR/h", e.radiation));
                for (pb_size_t i = 0; i < e.one_wire_temperature_count; ++i) {
                    info("Probe " + std::to_string(i + 1), temp(e.one_wire_temperature[i]));
                }
            }
            if (s.has_power) {
                const meshtastic_PowerMetrics& p = s.power;
                header("Power");
                info("Received", age_str(s.power_ts, now) + " ago");
                const auto channel = [&](int index, bool has_v, float v, bool has_c, float c) {
                    if (!has_v && !has_c) return;
                    std::string value;
                    if (has_v) value = fmt("%.2f V", v);
                    if (has_c) value += (value.empty() ? "" : "  ") + fmt("%.1f mA", c);
                    info("Channel " + std::to_string(index), value);
                };
                channel(1, p.has_ch1_voltage, p.ch1_voltage, p.has_ch1_current, p.ch1_current);
                channel(2, p.has_ch2_voltage, p.ch2_voltage, p.has_ch2_current, p.ch2_current);
                channel(3, p.has_ch3_voltage, p.ch3_voltage, p.has_ch3_current, p.ch3_current);
                channel(4, p.has_ch4_voltage, p.ch4_voltage, p.has_ch4_current, p.ch4_current);
                channel(5, p.has_ch5_voltage, p.ch5_voltage, p.has_ch5_current, p.ch5_current);
                channel(6, p.has_ch6_voltage, p.ch6_voltage, p.has_ch6_current, p.ch6_current);
                channel(7, p.has_ch7_voltage, p.ch7_voltage, p.has_ch7_current, p.ch7_current);
                channel(8, p.has_ch8_voltage, p.ch8_voltage, p.has_ch8_current, p.ch8_current);
            }
            if (s.has_air) {
                const meshtastic_AirQualityMetrics& q = s.air;
                header("Air quality");
                info("Received", age_str(s.air_ts, now) + " ago");
                const auto dust = [&](const char* name, uint32_t value) { info(name, std::to_string(value) + " ug/m3"); };
                if (q.has_pm10_standard) dust("PM1.0", q.pm10_standard);
                if (q.has_pm25_standard) dust("PM2.5", q.pm25_standard);
                if (q.has_pm40_standard) dust("PM4.0", q.pm40_standard);
                if (q.has_pm100_standard) dust("PM10", q.pm100_standard);
                if (q.has_co2) info("CO2", std::to_string(q.co2) + " ppm");
                if (q.has_co2_temperature) info("Temperature", temp(q.co2_temperature));
                if (q.has_co2_humidity) info("Humidity", fmt("%.0f%%", q.co2_humidity));
                if (q.has_pm_voc_idx) info("VOC index", fmt("%.0f", q.pm_voc_idx));
                if (q.has_pm_nox_idx) info("NOx index", fmt("%.0f", q.pm_nox_idx));
                if (q.has_form_formaldehyde) info("Formaldehyde", fmt("%.0f ppb", q.form_formaldehyde));
            }
        }
        double lat, lon;
        if (Mesh::node_position(*n, lat, lon)) {
            header("Position");
            char buf[48];
            snprintf(buf, sizeof(buf), "%.5f, %.5f", lat, lon);
            info("Coordinates", buf);
            if (n->position.has_altitude) info("Altitude", altitude_str(n->position.altitude, ui::imperial()));
            double my_lat, my_lon;
            if (!self && mesh.my_position(my_lat, my_lon)) {
                const double bearing = geo_bearing_deg(my_lat, my_lon, lat, lon);
                info("Distance", distance_str(geo_distance_m(my_lat, my_lon, lat, lon), ui::imperial()));
                info("Bearing", fmt("%.0f deg ", bearing) + compass_point(bearing));
            }
            if (n->position.sats_in_view) info("Satellites", std::to_string(n->position.sats_in_view));
            if (n->position.time) info("Fix age", age_str(n->position.time, now));
        }
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
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
    uint32_t num_;
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

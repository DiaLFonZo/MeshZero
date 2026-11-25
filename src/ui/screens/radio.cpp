// SPDX-License-Identifier: GPL-3.0-only
// Radio status tab, packet log, help text and the tab factory.
#include <ctime>

#if !defined(MESHCARD_ESP32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

#include "common/util.h"
#include "ui/app.h"
#include "ui/fonts/fonts.h"

namespace mz {
namespace {

std::vector<std::string> local_addresses()
{
    std::vector<std::string> out;
#if defined(MESHCARD_ESP32)
    return out;  // Cardputer ADV: no network sharing from the UI
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
        // Container and virtual bridges are not reachable from other devices.
        const std::string name = i->ifa_name ? i->ifa_name : "";
        if (name.rfind("docker", 0) == 0 || name.rfind("veth", 0) == 0 || name.rfind("br-", 0) == 0) continue;
        char buf[32] = {};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof(buf));
        out.emplace_back(buf);
    }
    freeifaddrs(list);
    return out;
#endif
}

std::string fmt(const char* format, double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), format, value);
    return buf;
}

// Frequency the radio uses, in MHz, worked out the way the firmware does
// (region band, preset bandwidth, channel slot). 0 when unknown.
double radio_frequency_mhz(const Mesh& mesh)
{
    const auto& l = mesh.config.lora;
    if (l.override_frequency > 0) return l.override_frequency + l.frequency_offset;
    struct Band {
        int region;
        double start, end;
    };
    static const Band bands[] = {
        {meshtastic_Config_LoRaConfig_RegionCode_US, 902.0, 928.0},
        {meshtastic_Config_LoRaConfig_RegionCode_EU_433, 433.0, 434.0},
        {meshtastic_Config_LoRaConfig_RegionCode_EU_868, 869.4, 869.65},
        {meshtastic_Config_LoRaConfig_RegionCode_CN, 470.0, 510.0},
        {meshtastic_Config_LoRaConfig_RegionCode_JP, 920.5, 923.5},
        {meshtastic_Config_LoRaConfig_RegionCode_ANZ, 915.0, 928.0},
        {meshtastic_Config_LoRaConfig_RegionCode_KR, 920.0, 923.0},
        {meshtastic_Config_LoRaConfig_RegionCode_TW, 920.0, 925.0},
        {meshtastic_Config_LoRaConfig_RegionCode_RU, 868.7, 869.2},
        {meshtastic_Config_LoRaConfig_RegionCode_IN, 865.0, 867.0},
        {meshtastic_Config_LoRaConfig_RegionCode_NZ_865, 864.0, 868.0},
        {meshtastic_Config_LoRaConfig_RegionCode_TH, 920.0, 925.0},
        {meshtastic_Config_LoRaConfig_RegionCode_UA_433, 433.0, 434.7},
        {meshtastic_Config_LoRaConfig_RegionCode_UA_868, 868.0, 868.6},
        {meshtastic_Config_LoRaConfig_RegionCode_MY_433, 433.0, 435.0},
        {meshtastic_Config_LoRaConfig_RegionCode_MY_919, 919.0, 924.0},
        {meshtastic_Config_LoRaConfig_RegionCode_SG_923, 917.0, 925.0},
        {meshtastic_Config_LoRaConfig_RegionCode_LORA_24, 2400.0, 2483.5},
    };
    const Band* band = nullptr;
    for (const Band& b : bands)
        if (b.region == l.region) band = &b;
    if (!band) return 0;
    double bw_khz = 250;
    if (!l.use_preset) {
        bw_khz = l.bandwidth == 31 ? 31.25 : l.bandwidth == 62 ? 62.5 : l.bandwidth == 200 ? 203.125
                 : l.bandwidth == 400 ? 406.25 : l.bandwidth ? l.bandwidth : 250;
    } else {
        switch (l.modem_preset) {
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
                bw_khz = 125;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
                bw_khz = 62.5;
                break;
            case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
            case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
                bw_khz = 500;
                break;
            default:
                bw_khz = 250;
                break;
        }
    }
    const double bw  = bw_khz / 1000.0;
    const int slots  = static_cast<int>((band->end - band->start) / bw);
    if (slots <= 0) return 0;
    int slot;
    if (l.channel_num > 0) {
        slot = (l.channel_num - 1) % slots;
    } else {
        // Same hash as the firmware (djb2) of the primary channel name.
        std::string name = mesh.channel_name(0);
        if (name.empty() || name == "Primary") name = mesh.preset_name();
        uint32_t h = 5381;
        for (const unsigned char c : name) h = ((h << 5) + h) + c;
        slot = static_cast<int>(h % static_cast<uint32_t>(slots));
    }
    return band->start + bw / 2 + slot * bw + l.frequency_offset;
}

class RadioScreen : public Screen {
public:
    // MeshCard: six tiles with the key figures, the details below.
    explicit RadioScreen(App& a) : Screen(a)
    {
        const int w = (kContentW - 4 - 2 * kGap) / 3;
        for (int i = 0; i < 6; ++i) {
            Tile& t = tiles_[i];
            t.box   = ui::box(root);
            lv_obj_set_pos(t.box, 2 + (i % 3) * (w + kGap), 1 + (i / 3) * (kTileH + kGap));
            lv_obj_set_size(t.box, w, kTileH);
            lv_obj_set_style_radius(t.box, 6, 0);
            ui::set_bg(t.box, ui::c_bar());
            t.value = ui::label(t.box, "", ui::c_text(), ui::font_bold());
            lv_obj_set_pos(t.value, 7, 1);
            lv_obj_set_width(t.value, w - 10);
            lv_label_set_long_mode(t.value, LV_LABEL_LONG_CLIP);
            t.name = ui::label(t.box, "", ui::c_dim(), mz_font(12, false));
            lv_obj_set_pos(t.name, 7, 17);
        }
        const int y = 1 + 2 * (kTileH + kGap);
        list_       = std::make_unique<ui::ListView>(root, 0, y, kContentW, kContentH - y);
    }
    int tab() const override { return kTabRadio; }
    std::string hint() const override { return "Up/Down scroll   Tab next page"; }

    void refresh() override
    {
        ui::ListItems items;
        log_row_ = -1;
        const auto header = [&](const std::string& text) {
            ui::ListItem it;
            it.text   = text;
            it.header = true;
            items.push_back(it);
        };
        const auto row = [&](const std::string& name, const std::string& value, bool warn = false) {
            ui::ListItem it;
            it.text            = name;
            it.has_color       = true;
            it.color           = ui::c_dim();
            it.right           = ui::clean(value, true);
            it.has_right_color = true;
            it.right_color     = warn ? ui::c_warn() : ui::c_text();
            items.push_back(it);
        };

        {
            const auto& lora = mesh.config.lora;
            const bool up    = mesh.ever_ready();
            const double mhz = up ? radio_frequency_mhz(mesh) : 0;
            const meshtastic_NodeInfo* me = mesh.me();
            std::string cu = "-", air = "-", batt = "-";
            if (me && me->has_device_metrics) {
                const auto& d = me->device_metrics;
                if (d.has_channel_utilization) cu = fmt("%.1f%%", d.channel_utilization);
                if (d.has_air_util_tx) air = fmt("%.1f%%", d.air_util_tx);
                if (d.has_battery_level) batt = d.battery_level > 100 ? "USB" : std::to_string(d.battery_level) + "%";
            }
            set_tile(0, up && !mesh.region_unset() ? region_label(lora.region) : "not set", "Region");
            set_tile(1, mhz > 0 ? fmt("%.3f", mhz) : "-", "MHz");
            set_tile(2, up ? mesh.preset_name() : "-", "Preset");
            set_tile(3, cu, "Channel use");
            set_tile(4, air, "Airtime");
            set_tile(5, batt, "Battery");
        }

        header("Link");
        switch (mesh.state()) {
            case LinkState::Ready:
                row("Radio", "connected");
                break;
            case LinkState::Syncing:
                row("Radio", "starting...", true);
                break;
            default:
                row("Radio", mesh.offline_text(), true);
                break;
        }
        if (mesh.network_access != 1) {
            row("Network access", "off");
        } else {
            const std::vector<std::string> addrs = local_addresses();
            if (addrs.empty()) row("Network access", "no network", true);
            for (const auto& a : addrs) row("Network access", a + ":4403");
        }

        if (mesh.ever_ready()) {
            header("This node");
            row("Name", mesh.long_name(mesh.my_num) + " (" + mesh.short_name(mesh.my_num) + ")");
            row("ID", node_id_str(mesh.my_num));
            if (mesh.has_metadata) row("Firmware", mesh.metadata.firmware_version);
            row("Role", role_name(mesh.config.device.role));

            header("LoRa");
            const auto& lora = mesh.config.lora;
            if (mesh.region_unset()) {
                row("Region", "not set", true);
            } else {
                row("Region", std::to_string(lora.region) == "0" ? "UNSET" : region_label(lora.region));
            }
            row("Modem preset", mesh.preset_name());
            row("Hop limit", std::to_string(lora.hop_limit));
            row("Transmit", !lora.tx_enabled ? "disabled" : lora.tx_power == 0 ? "max for region" : std::to_string(lora.tx_power) + " dBm",
                !lora.tx_enabled);
            int channels = 0;
            for (int i = 0; i < kMaxChannels; ++i) channels += mesh.channel_enabled(i) ? 1 : 0;
            row("Channels", std::to_string(channels) + "  (primary: " + mesh.channel_name(0) + ")");

            const meshtastic_NodeInfo* self = mesh.me();
            header("Activity");
            if (self && self->has_device_metrics) {
                const auto& d = self->device_metrics;
                if (d.has_channel_utilization) row("Channel utilization", fmt("%.1f%%", d.channel_utilization));
                if (d.has_air_util_tx) row("Air time (TX)", fmt("%.2f%%", d.air_util_tx));
            }
            if (mesh.has_local_stats) {
                const auto& s = mesh.local_stats;
                row("Packets received", std::to_string(s.num_packets_rx) + "  (" + std::to_string(s.num_packets_rx_bad) + " bad)");
                row("Packets sent", std::to_string(s.num_packets_tx) + "  (" + std::to_string(s.num_tx_relay) + " relayed)");
                row("Nodes online", std::to_string(s.num_online_nodes) + " of " + std::to_string(s.num_total_nodes));
                if (s.noise_floor != 0) row("Noise floor", std::to_string(s.noise_floor) + " dBm");
            } else {
                row("Nodes known", std::to_string(mesh.nodes.size()));
            }
            log_row_ = static_cast<int>(items.size());
            {
                ui::ListItem it;
                it.text      = "Packet log";
                it.has_color = true;
                it.color     = ui::c_accent();
                it.right     = std::to_string(mesh.packet_log.size()) + "  " LV_SYMBOL_RIGHT;
                items.push_back(it);
            }

            header("GPS");
            const auto mode = mesh.config.position.gps_mode;
            if (mesh.config.position.fixed_position) {
                row("Mode", "fixed position");
            } else if (mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
                row("Mode", "off", true);
            } else {
                row("Mode", "enabled");
            }
            double lat, lon;
            if (self && Mesh::node_position(*self, lat, lon)) {
                char buf[48];
                snprintf(buf, sizeof(buf), "%.5f, %.5f", lat, lon);
                row("Position", buf);
                if (self->position.has_altitude) row("Altitude", std::to_string(self->position.altitude) + " m");
                if (self->position.sats_in_view) row("Satellites", std::to_string(self->position.sats_in_view));
                if (self->position.time) row("Fix age", age_str(self->position.time, now_s()));
            } else if (mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
                row("Position", "waiting for a fix", true);
            }
        }
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) {
            show_tiles(list_->at_top());
            return true;
        }
        if (key.k == K::Enter) {
            if (list_->selected() == log_row_) app.push(make_packet_log(app));
            return true;
        }
        return false;
    }

private:
    static constexpr int kTileH = 33;
    static constexpr int kGap   = 3;
    struct Tile {
        lv_obj_t* box   = nullptr;
        lv_obj_t* value = nullptr;
        lv_obj_t* name  = nullptr;
    };
    void set_tile(int i, const std::string& value, const char* name)
    {
        ui::set_text(tiles_[i].value, ui::clean(value, true));
        ui::set_text(tiles_[i].name, name);
    }
    // The tiles show only while the first row is selected; lower down the
    // list takes the whole page.
    void show_tiles(bool on)
    {
        if (on == tiles_on_) return;
        tiles_on_ = on;
        for (Tile& t : tiles_) {
            if (on) lv_obj_remove_flag(t.box, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(t.box, LV_OBJ_FLAG_HIDDEN);
        }
        const int y = on ? kListY : 0;
        lv_obj_set_pos(list_->obj(), 0, y);
        list_->set_height(kContentH - y);
        list_->select(list_->selected());  // keep the picked row in view
    }
    static constexpr int kListY = 1 + 2 * (kTileH + kGap);
    Tile tiles_[6];
    bool tiles_on_ = true;

    static std::string region_label(int region)
    {
        // Names follow the protobuf enum; kept short for the status row.
        static const char* names[] = {"UNSET",  "US",     "EU_433", "EU_868", "CN",      "JP",      "ANZ",     "KR",     "TW",
                                      "RU",     "IN",     "NZ_865", "TH",     "LORA_24", "UA_433",  "UA_868",  "MY_433", "MY_919",
                                      "SG_923", "PH_433", "PH_868", "PH_915", "ANZ_433", "KZ_433",  "KZ_863",  "NP_865", "BR_902",
                                      "ITU1_2M", "ITU2_2M", "EU_866", "EU_874", "EU_917", "EU_N_868", "ITU3_2M"};
        if (region >= 0 && region < static_cast<int>(sizeof(names) / sizeof(names[0]))) return names[region];
        return std::to_string(region);
    }

    std::unique_ptr<ui::ListView> list_;
    int log_row_ = -1;
};

class PacketLogScreen : public Screen {
public:
    explicit PacketLogScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        list_->set_prefix_width(34);
    }
    std::string title() const override { return LV_SYMBOL_LEFT " Packet log"; }
    std::string hint() const override { return follow_ ? "Up/Down browse   Esc back" : "Paused   Home: live   Esc back"; }

    void refresh() override
    {
        if (seen_rev_ == mesh.rev_log && built_) return;
        if (!follow_ && built_) return;
        seen_rev_ = mesh.rev_log;
        built_    = true;
        ui::ListItems items;
        for (auto e = mesh.packet_log.rbegin(); e != mesh.packet_log.rend(); ++e) {
            ui::ListItem it;
            const time_t t = e->ts;
            tm local{};
            localtime_r(&t, &local);
            char clock[8];
            strftime(clock, sizeof(clock), "%H:%M", &local);
            it.prefix           = clock;
            it.has_prefix_color = true;
            it.prefix_color     = ui::c_dim();
            const std::string to = e->to == kBroadcast ? "all" : mesh.short_name(e->to);
            std::string what     = e->portnum < 0 ? "encrypted" : portnum_name(e->portnum);
            for (auto& c : what) c = static_cast<char>(tolower(c));
            it.text = (e->outgoing ? "me" : mesh.short_name(e->from)) + " " LV_SYMBOL_RIGHT " " + to + "  " + what;
            if (!e->summary.empty()) it.text += ": " + e->summary;
            it.text = ui::clean(it.text, true);
            if (e->outgoing) {
                it.has_color = true;
                it.color     = ui::c_me();
            } else {
                char sig[32] = "";
                if (e->hops > 0) {
                    snprintf(sig, sizeof(sig), "%dh", e->hops);
                } else if (e->snr != 0 || e->rssi != 0) {
                    snprintf(sig, sizeof(sig), "%.0f/%d", e->snr, e->rssi);
                }
                it.right = sig;
            }
            items.push_back(std::move(it));
        }
        if (items.empty()) {
            ui::ListItem it;
            it.text      = "Nothing heard yet";
            it.has_color = true;
            it.color     = ui::c_dim();
            items.push_back(it);
        }
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (key.k == K::Home) {
            follow_ = true;
            built_  = false;
            list_->select(0);
            return true;
        }
        if (list_->handle_key(key)) {
            follow_ = list_->selected() == 0;
            return true;
        }
        return false;
    }

private:
    std::unique_ptr<ui::ListView> list_;
    uint32_t seen_rev_ = 0;
    bool built_        = false;
    bool follow_       = true;
};

const char* const kHelp =
    "KEYS\n"
    "Arrows: move (hold Fn only while typing)\n"
    "Enter: open, edit, send\n"
    "Esc: back, then Home. On Home, hold it 3 s to exit\n"
    "Tab or Left/Right: next page\n"
    "Settings: Esc asks to save\n\n"
    "MESSAGES\n"
    "Green tick: delivered. Grey tick: relayed. Cross: failed.\n"
    "To message someone, open them in Nodes.\n\n"
    "OTHER APPS\n"
    "The radio keeps running after you exit. To use it from MeshMonitor or a phone, turn on Network access in "
    "Setup. The address is on the Radio page.\n\n"
    "RADIO OFFLINE\n"
    "Power off, check the cap, power on. For details run:\n"
    "sudo -u meshzero meshzerod doctor\n\n"
    "ABOUT\n"
    "MeshZero " MESHZERO_VERSION;

}  // namespace

std::unique_ptr<Screen> make_radio(App& app)
{
    return std::make_unique<RadioScreen>(app);
}

std::unique_ptr<Screen> make_packet_log(App& app)
{
    return std::make_unique<PacketLogScreen>(app);
}

std::unique_ptr<Screen> make_help(App& app)
{
    return make_about_view(app, LV_SYMBOL_LEFT " Help", kHelp, "https://github.com/s0liton/MeshZero");
}

std::unique_ptr<Screen> make_tab(App& app, int tab)
{
    switch (tab) {
        case kTabNodes:
            return make_nodes(app);
        case kTabMap:
            return make_map(app);
        case kTabRadio:
            return make_radio(app);
        case kTabSetup:
            return make_setup(app);
        case kTabHome:
            return make_home(app);
        default:
            return make_chats(app);
    }
}

}  // namespace mz

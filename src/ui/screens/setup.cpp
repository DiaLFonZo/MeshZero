// SPDX-License-Identifier: GPL-3.0-only
// Setup tab: radio, node, channel and module configuration.
#include <cstring>
#include <random>
#if defined(MESHCARD_ESP32)
#include <esp_random.h>
#endif

#include "common/alert.h"
#include "common/util.h"
#include "meshtastic/apponly.pb.h"
#include "ui/camera.h"
#include "ui/screens/editor.h"
#include "ui/update.h"

namespace mz {
namespace {

using Region = meshtastic_Config_LoRaConfig_RegionCode;

// The Cap LoRa-1262 front end covers roughly 850-930 MHz.
struct RegionInfo {
    int value;
    const char* name;
    const char* band;
    bool supported;
};
const RegionInfo kRegions[] = {
    {meshtastic_Config_LoRaConfig_RegionCode_US, "US", "United States 902-928 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_868, "EU_868", "Europe 869.4-869.65 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ, "ANZ", "Australia / NZ 915-928 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_NZ_865, "NZ_865", "New Zealand 864-868 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_JP, "JP", "Japan 920.8-927.8 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_KR, "KR", "Korea 920-923 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_TW, "TW", "Taiwan 920-925 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_RU, "RU", "Russia 868.7-869.2 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_IN, "IN", "India 865-867 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_TH, "TH", "Thailand 920-925 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_UA_868, "UA_868", "Ukraine 868-868.6 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_919, "MY_919", "Malaysia 919-924 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_SG_923, "SG_923", "Singapore 917-925 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_868, "PH_868", "Philippines 868-869.4 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_915, "PH_915", "Philippines 915-918 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_KZ_863, "KZ_863", "Kazakhstan 863-868 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_NP_865, "NP_865", "Nepal 865-868 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_BR_902, "BR_902", "Brazil 902-907.5 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_866, "EU_866", "Europe 865.6-867.6 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_874, "EU_874", "Europe 874-874.4 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_917, "EU_917", "Europe 917.4-919.4 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_N_868, "EU_N_868", "Europe narrowband 868 MHz", true},
    {meshtastic_Config_LoRaConfig_RegionCode_CN, "CN", "China 470-510 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_433, "EU_433", "Europe 433-434 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_UA_433, "UA_433", "Ukraine 433-434.7 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_433, "MY_433", "Malaysia 433-435 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_433, "PH_433", "Philippines 433-434.7 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ_433, "ANZ_433", "Australia / NZ 433-434.8 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_KZ_433, "KZ_433", "Kazakhstan 433-434.8 MHz", false},
    {meshtastic_Config_LoRaConfig_RegionCode_LORA_24, "LORA_24", "2.4 GHz worldwide", false},
    {meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M, "ITU1_2M", "2 m amateur band, ITU region 1", false},
    {meshtastic_Config_LoRaConfig_RegionCode_ITU2_2M, "ITU2_2M", "2 m amateur band, ITU region 2", false},
    {meshtastic_Config_LoRaConfig_RegionCode_ITU3_2M, "ITU3_2M", "2 m amateur band, ITU region 3", false},
};

std::string region_name(int value)
{
    if (value == meshtastic_Config_LoRaConfig_RegionCode_UNSET) return "UNSET";
    for (const auto& r : kRegions) {
        if (r.value == value) return r.name;
    }
    return std::to_string(value);
}

// The band of a region in MHz, read from its description above. False for
// regions described without one.
bool region_band(int value, double& low, double& high)
{
    for (const auto& r : kRegions) {
        if (r.value != value) continue;
        for (const char* p = r.band; *p; ++p) {
            if (*p < '0' || *p > '9') continue;
            float a = 0, b = 0;
            if (sscanf(p, "%f-%f MHz", &a, &b) == 2 && b >= a) {
                low  = a;
                high = b;
                return true;
            }
            while ((*p >= '0' && *p <= '9') || *p == '.') ++p;
            if (!*p) break;
        }
    }
    return false;
}

std::vector<Opt> region_opts()
{
    std::vector<Opt> opts;
    opts.push_back({meshtastic_Config_LoRaConfig_RegionCode_UNSET, "UNSET"});
    for (const auto& r : kRegions) {
        opts.push_back({r.value, std::string(r.name) + "  " + r.band + (r.supported ? "" : " (unsupported)")});
    }
    return opts;
}

// The presets the radio daemon shipped with MeshZero knows, with what each
// stands for: bandwidth in kHz, spreading factor, coding rate 4/x. Taken from
// modemPresetToParams() in the firmware's MeshRadio.h for that version. A
// preset it does not know runs as LongFast there, so none of those (Lite,
// Narrow, VeryLongSlow) are offered.
struct PresetInfo {
    int value;
    const char* name;
    int bw, sf, cr;
};
const PresetInfo kPresets[] = {
    {meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, "LongFast", 250, 11, 5},
    {meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, "LongModerate", 125, 11, 8},
    {meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, "LongSlow", 125, 12, 8},
    {meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO, "LongTurbo", 500, 11, 8},
    {meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, "MediumFast", 250, 9, 5},
    {meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW, "MediumSlow", 250, 10, 5},
    {meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST, "ShortFast", 250, 7, 5},
    {meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW, "ShortSlow", 250, 8, 5},
    {meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, "ShortTurbo", 500, 7, 5},
};
constexpr int kCustomModem = -1;

// What a preset stands for; LongFast for one the radio does not know, as there.
const PresetInfo& preset_info(int preset)
{
    for (const PresetInfo& p : kPresets) {
        if (p.value == preset) return p;
    }
    return kPresets[0];
}

std::vector<Opt> modem_opts()
{
    std::vector<Opt> opts;
    for (const PresetInfo& p : kPresets) {
        opts.push_back({p.value, std::string(p.name) + (p.value == kPresets[0].value ? "  (default)" : "")});
    }
    opts.push_back({kCustomModem, "Custom"});
    return opts;
}

// Moves a working copy from a preset to modem values of its own, starting
// from the ones the preset stood for.
void make_custom(meshtastic_Config_LoRaConfig& l)
{
    if (!l.use_preset) return;
    const PresetInfo& p = preset_info(l.modem_preset);
    l.bandwidth         = static_cast<decltype(l.bandwidth)>(p.bw);
    l.spread_factor     = static_cast<decltype(l.spread_factor)>(p.sf);
    l.coding_rate       = static_cast<decltype(l.coding_rate)>(p.cr);
    l.use_preset        = false;
}

// The protocol rounds a position to a grid; these are the cell sizes other
// Meshtastic apps offer. 0 stops position being sent on the channel at all.
std::vector<Opt> precision_opts()
{
    return {{0, "Off"},          {10, "Within 23 km"}, {11, "Within 12 km"}, {12, "Within 5.8 km"},
            {13, "Within 2.9 km"}, {14, "Within 1.5 km"}, {15, "Within 700 m"}, {16, "Within 350 m"},
            {17, "Within 180 m"}, {18, "Within 90 m"},  {19, "Within 45 m"},  {32, "Exact"}};
}

const char* const kRestartNote = "Saved. Radio restarting.";

void send_config(Mesh& mesh, const meshtastic_Config& c)
{
    mesh.begin_edit();
    mesh.set_config(c);
    mesh.commit_edit();
}

void send_module(Mesh& mesh, const meshtastic_ModuleConfig& c)
{
    mesh.begin_edit();
    mesh.set_module_config(c);
    mesh.commit_edit();
}

// ------------------------------------------------------------------ pages

std::unique_ptr<Screen> page_user(App& app)
{
    auto cfg = std::make_shared<meshtastic_User>();
    if (const meshtastic_NodeInfo* self = app.mesh.me(); self && self->has_user) *cfg = self->user;
    std::vector<Field> f;
    f.push_back(MZ_TEXT("Long name", long_name, "", false));
    f.push_back(f_text(
        "Short name", [cfg]() { return std::string(cfg->short_name); },
        [cfg](const std::string& s) {
            if (s.empty() || s.size() > 4) return std::string("Short name: 1-4 characters");
            snprintf(cfg->short_name, sizeof(cfg->short_name), "%s", s.c_str());
            return std::string();
        },
        4, "1-4 characters"));
    f.push_back(MZ_BOOL("Licensed operator (ham)", is_licensed,
                        "Turns encryption off"));
    f.push_back(f_bool(
        "Accept direct messages", [cfg]() { return static_cast<int64_t>(!(cfg->has_is_unmessagable && cfg->is_unmessagable)); },
        [cfg](int64_t v) {
            cfg->has_is_unmessagable = true;
            cfg->is_unmessagable     = v == 0;
        },
        ""));
    f.push_back(f_info("Node ID", [&app]() { return node_id_str(app.mesh.my_num); }));
    return make_editor(app, "Your node", std::move(f), [&app, cfg]() { app.mesh.set_owner(*cfg); });
}

// Leftovers of a custom setup must not survive a move to another region or
// to a preset: a frequency fixed for one band is wrong, and may be illegal,
// in another, and custom modem values next to a preset only mislead. What the
// user set in the same edit is theirs and stays.
meshtastic_Config_LoRaConfig lora_cleaned(const meshtastic_Config_LoRaConfig& before, meshtastic_Config_LoRaConfig l)
{
    const bool region_changed = l.region != before.region;
    const bool to_preset      = l.use_preset && (!before.use_preset || l.modem_preset != before.modem_preset);
    if (l.use_preset) {
        l.bandwidth     = 0;
        l.spread_factor = 0;
        l.coding_rate   = 0;
    }
    if (region_changed || to_preset) {
        if (l.override_frequency == before.override_frequency) l.override_frequency = 0;
        if (l.frequency_offset == before.frequency_offset) l.frequency_offset = 0;
    }
    if (region_changed && l.channel_num == before.channel_num) l.channel_num = 0;
    // Whatever the history, a fixed frequency outside the region's band does not go to the radio.
    double low = 0, high = 0;
    if (l.override_frequency != 0 && region_band(l.region, low, high) &&
        (l.override_frequency < low - 0.001 || l.override_frequency > high + 0.001)) {
        l.override_frequency = 0;
    }
    return l;
}

std::unique_ptr<Screen> page_lora(App& app)
{
    auto cfg    = std::make_shared<meshtastic_Config_LoRaConfig>(app.mesh.config.lora);
    auto before = std::make_shared<meshtastic_Config_LoRaConfig>(app.mesh.config.lora);
    std::vector<Field> f;
    f.push_back(MZ_ENUM("Region", region, region_opts(),
                        "Required to transmit"));
    // One row says what the radio runs on: a preset, or values of its own.
    f.push_back(f_enum(
        "Modem", [cfg]() { return static_cast<int64_t>(cfg->use_preset ? preset_info(cfg->modem_preset).value : kCustomModem); },
        [cfg](int64_t v) {
            if (v == kCustomModem) {
                make_custom(*cfg);
                return;
            }
            cfg->modem_preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(v);
            cfg->use_preset   = true;
        },
        modem_opts(), "Must match the rest of the mesh"));
    // A fixed frequency is set far down the page; say so where the modem is named.
    f.back().note = [cfg]() { return std::string(cfg->override_frequency != 0 ? ", fixed freq" : ""); };
    f.push_back(MZ_INT("Hop limit", hop_limit, 1, 7, "hops", "Default 3"));
    f.push_back(MZ_BOOL("Transmit enabled", tx_enabled, ""));
    f.push_back(MZ_INT("Transmit power", tx_power, 0, 30, "dBm", "0 = region maximum"));
    f.push_back(MZ_INT("Frequency slot", channel_num, 0, 250, "", "0 = automatic"));
    f.push_back(MZ_BOOL("RX boosted gain", sx126x_rx_boosted_gain, ""));
    f.push_back(MZ_BOOL("Override duty cycle", override_duty_cycle, "May be illegal in your region"));
    f.push_back(MZ_BOOL("OK to MQTT", config_ok_to_mqtt, "Let gateways uplink your packets"));
    f.push_back(MZ_BOOL("Ignore MQTT", ignore_mqtt, "Drop packets that arrived via MQTT"));
    // These show what is in use, the preset's values included. Changing one
    // makes the modem Custom, starting from what was there.
    f.push_back(f_header("Modem values"));
    f.push_back(f_int(
        "Bandwidth", [cfg]() { return static_cast<int64_t>(cfg->use_preset ? preset_info(cfg->modem_preset).bw : cfg->bandwidth); },
        [cfg](int64_t v) {
            make_custom(*cfg);
            cfg->bandwidth = static_cast<decltype(cfg->bandwidth)>(v);
        },
        7, 1600, "kHz", "Changing it makes the modem Custom. 62 = 62.5, 31 = 31.25"));
    f.push_back(f_int(
        "Spreading factor",
        [cfg]() { return static_cast<int64_t>(cfg->use_preset ? preset_info(cfg->modem_preset).sf : cfg->spread_factor); },
        [cfg](int64_t v) {
            make_custom(*cfg);
            cfg->spread_factor = static_cast<decltype(cfg->spread_factor)>(v);
        },
        5, 12, "", "Changing it makes the modem Custom. 5-12"));
    f.push_back(f_int(
        "Coding rate", [cfg]() { return static_cast<int64_t>(cfg->use_preset ? preset_info(cfg->modem_preset).cr : cfg->coding_rate); },
        [cfg](int64_t v) {
            make_custom(*cfg);
            cfg->coding_rate = static_cast<decltype(cfg->coding_rate)>(v);
        },
        5, 8, "", "Changing it makes the modem Custom. 5-8 (4/5 to 4/8)"));
    f.push_back(f_text(
        "Fixed frequency (MHz)",
        [cfg]() {
            if (cfg->override_frequency == 0) return std::string();
            char buf[24];
            snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(cfg->override_frequency));
            std::string text = buf;
            while (text.back() == '0') text.pop_back();
            if (text.back() == '.') text.pop_back();
            return text;
        },
        [cfg](const std::string& s) {
            const std::string text = trim(s);
            if (text.empty()) {
                cfg->override_frequency = 0;
                return std::string();
            }
            char* end          = nullptr;
            const double value = strtod(text.c_str(), &end);
            if (end == text.c_str() || *end != 0 || value < 137 || value > 3000) return std::string("A frequency in MHz, e.g. 869.618");
            double low = 0, high = 0;
            if (region_band(cfg->region, low, high) && (value < low || value > high)) {
                char range[64];
                snprintf(range, sizeof(range), "%s allows %g to %g MHz", region_name(cfg->region).c_str(), low, high);
                return std::string(range);
            }
            cfg->override_frequency = static_cast<float>(value);
            return std::string();
        },
        12, "Empty = automatic. Must be legal in your region"));
    f.back().empty_text = "automatic";
    return make_editor(
        app, "LoRa radio", std::move(f),
        [&app, cfg, before]() {
            meshtastic_Config c     = meshtastic_Config_init_zero;
            c.which_payload_variant = meshtastic_Config_lora_tag;
            c.payload_variant.lora  = lora_cleaned(*before, *cfg);
            send_config(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_device(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_DeviceConfig>(app.mesh.config.device);
    std::vector<Field> f;
    f.push_back(MZ_ENUM("Role", role,
                        (std::vector<Opt>{
                            {meshtastic_Config_DeviceConfig_Role_CLIENT, "Client"},
                            {meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE, "Client mute"},
                            {meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN, "Client hidden"},
                            {meshtastic_Config_DeviceConfig_Role_CLIENT_BASE, "Client base"},
                            {meshtastic_Config_DeviceConfig_Role_TRACKER, "Tracker"},
                            {meshtastic_Config_DeviceConfig_Role_SENSOR, "Sensor"},
                            {meshtastic_Config_DeviceConfig_Role_TAK, "TAK"},
                            {meshtastic_Config_DeviceConfig_Role_TAK_TRACKER, "TAK tracker"},
                            {meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND, "Lost and found"},
                            {meshtastic_Config_DeviceConfig_Role_ROUTER, "Router"},
                            {meshtastic_Config_DeviceConfig_Role_ROUTER_LATE, "Router late"},
                            {meshtastic_Config_DeviceConfig_Role_REPEATER, "Repeater"},
                        }),
                        ""));
    f.push_back(MZ_ENUM("Rebroadcast", rebroadcast_mode,
                        (std::vector<Opt>{
                            {meshtastic_Config_DeviceConfig_RebroadcastMode_ALL, "All"},
                            {meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING, "All, skip decoding"},
                            {meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY, "Local only"},
                            {meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY, "Known only"},
                            {meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY, "Core portnums only"},
                            {meshtastic_Config_DeviceConfig_RebroadcastMode_NONE, "None"},
                        }),
                        ""));
    f.push_back(MZ_INT("Node info interval", node_info_broadcast_secs, 0, 86400, "s",
                       "0 = default (3 h)"));
    f.push_back(MZ_TEXT("Time zone (POSIX)", tzdef, "POSIX TZ string", false));
    return make_editor(
        app, "Device", std::move(f),
        [&app, cfg]() {
            meshtastic_Config c       = meshtastic_Config_init_zero;
            c.which_payload_variant   = meshtastic_Config_device_tag;
            c.payload_variant.device  = *cfg;
            send_config(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_position(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_PositionConfig>(app.mesh.config.position);
    // Accuracy is a per-channel setting; this page edits it for the primary channel.
    auto primary         = std::make_shared<meshtastic_Channel>(app.mesh.channels[0]);
    auto primary_changed = std::make_shared<bool>(false);
    App* a               = &app;
    std::vector<Field> f;
    f.push_back(MZ_ENUM("GPS", gps_mode,
                        (std::vector<Opt>{
                            {meshtastic_Config_PositionConfig_GpsMode_ENABLED, "Enabled"},
                            {meshtastic_Config_PositionConfig_GpsMode_DISABLED, "Disabled"},
                            {meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT, "Not present"},
                        }),
                        ""));
    f.push_back(f_enum(
        "Shared accuracy", [primary]() { return static_cast<int64_t>(primary->settings.module_settings.position_precision); },
        [primary, primary_changed](int64_t v) {
            primary->settings.has_module_settings                = true;
            primary->settings.module_settings.position_precision = static_cast<uint32_t>(v);
            *primary_changed                                     = true;
        },
        precision_opts(), "Primary channel. Others: Setup > Channels"));
    f.push_back(MZ_INT("Broadcast interval", position_broadcast_secs, 0, 86400, "s",
                       "0 = default (15 min)"));
    f.push_back(MZ_BOOL("Smart broadcast", position_broadcast_smart_enabled,
                        "Send sooner when moving"));
    f.push_back(MZ_INT("Smart min distance", broadcast_smart_minimum_distance, 0, 100000, "m",
                       "0 = default (100 m)"));
    f.push_back(MZ_INT("Smart min interval", broadcast_smart_minimum_interval_secs, 0, 86400, "s",
                       "0 = default (30 s)"));
    f.push_back(MZ_INT("GPS update interval", gps_update_interval, 0, 86400, "s",
                       "0 = default (2 min)"));
    f.push_back(f_header("Fixed position"));
    f.push_back(MZ_BOOL("Use fixed position", fixed_position, ""));
    f.push_back(f_action(
        "Set fixed position...",
        [a]() {
            a->push(make_input(*a, "Fixed position", "", 48,
                               "lat, lon, alt (m)\ne.g. 52.3731, 4.8922, 5",
                               [a](const std::string& text) {
                                   double lat = 0, lon = 0, alt = 0;
                                   const int n = sscanf(text.c_str(), "%lf , %lf , %lf", &lat, &lon, &alt);
                                   if (n < 2 || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
                                       a->toast("Use: lat, lon, alt", true);
                                       return false;
                                   }
                                   a->mesh.set_fixed_position(lat, lon, static_cast<int>(alt));
                                   a->toast("Fixed position set");
                                   return true;
                               },
                               "-0123456789., "));
        },
        ""));
    f.push_back(f_action(
        "Clear fixed position",
        [a]() {
            a->mesh.remove_fixed_position();
            a->toast("Fixed position cleared");
        },
        ""));
    return make_editor(
        app, "Position & GPS", std::move(f),
        [&app, cfg, primary, primary_changed]() {
            meshtastic_Config c        = meshtastic_Config_init_zero;
            c.which_payload_variant    = meshtastic_Config_position_tag;
            c.payload_variant.position = *cfg;
            app.mesh.begin_edit();
            app.mesh.set_config(c);
            if (*primary_changed) {
                primary->index        = 0;
                primary->has_settings = true;
                primary->role         = meshtastic_Channel_Role_PRIMARY;
                app.mesh.set_channel(*primary);
            }
            app.mesh.commit_edit();
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_power(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_PowerConfig>(app.mesh.config.power);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Power saving", is_power_saving, ""));
    f.push_back(MZ_INT("Shutdown on battery after", on_battery_shutdown_after_secs, 0, 604800, "s", "0 = never"));
    return make_editor(
        app, "Power", std::move(f),
        [&app, cfg]() {
            meshtastic_Config c     = meshtastic_Config_init_zero;
            c.which_payload_variant = meshtastic_Config_power_tag;
            c.payload_variant.power = *cfg;
            send_config(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_network(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_NetworkConfig>(app.mesh.config.network);
    std::vector<Field> f;
    f.push_back(f_info("Wi-Fi", []() { return std::string("managed by system"); }));
    f.push_back(f_bool(
        "Mesh over LAN (UDP)",
        [cfg]() { return static_cast<int64_t>(cfg->enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST); },
        [cfg](int64_t v) {
            if (v) {
                cfg->enabled_protocols |= meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST;
            } else {
                cfg->enabled_protocols &= ~static_cast<uint32_t>(meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST);
            }
        },
        ""));
    f.push_back(MZ_TEXT("NTP server", ntp_server, "", false));
    f.push_back(MZ_TEXT("Syslog server", rsyslog_server, "host:port", false));
    return make_editor(
        app, "Network", std::move(f),
        [&app, cfg]() {
            meshtastic_Config c       = meshtastic_Config_init_zero;
            c.which_payload_variant   = meshtastic_Config_network_tag;
            c.payload_variant.network = *cfg;
            send_config(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_security(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_SecurityConfig>(app.mesh.config.security);
    std::vector<Field> f;
    f.push_back(f_info("Public key", [cfg]() {
        return cfg->public_key.size ? base64_encode(cfg->public_key.bytes, cfg->public_key.size).substr(0, 20) + ".." : "none";
    }));
    f.push_back(f_action(
        "Show full public key",
        [&app, cfg]() {
            app.push(make_text_view(app, "Public key",
                                    cfg->public_key.size ? base64_encode(cfg->public_key.bytes, cfg->public_key.size)
                                                         : "No key yet"));
        },
        ""));
    f.push_back(MZ_BOOL("Managed mode", is_managed, "Blocks local config changes"));
    f.push_back(MZ_BOOL("Legacy admin channel", admin_channel_enabled, "Insecure"));
    f.push_back(MZ_BOOL("Debug log over API", debug_log_api_enabled, ""));
    return make_editor(
        app, "Security", std::move(f),
        [&app, cfg]() {
            meshtastic_Config c        = meshtastic_Config_init_zero;
            c.which_payload_variant    = meshtastic_Config_security_tag;
            c.payload_variant.security = *cfg;
            send_config(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_mqtt(App& app)
{
    auto cfg = std::make_shared<meshtastic_ModuleConfig_MQTTConfig>(app.mesh.modules.mqtt);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Enabled", enabled, ""));
    f.push_back(MZ_TEXT("Server", address, "Empty = mqtt.meshtastic.org", false));
    f.push_back(MZ_TEXT("Username", username, "", false));
    f.push_back(MZ_TEXT("Password", password, "", true));
    f.push_back(MZ_BOOL("Encryption", encryption_enabled, ""));
    f.push_back(MZ_BOOL("JSON output", json_enabled, ""));
    f.push_back(MZ_BOOL("TLS", tls_enabled, ""));
    f.push_back(MZ_TEXT("Root topic", root, "e.g. msh/EU_868", false));
    f.push_back(f_header("Map reporting"));
    f.push_back(MZ_BOOL("Report to map", map_reporting_enabled, ""));
    f.push_back(f_bool(
        "Share location", [cfg]() { return static_cast<int64_t>(cfg->map_report_settings.should_report_location); },
        [cfg](int64_t v) {
            cfg->has_map_report_settings                    = true;
            cfg->map_report_settings.should_report_location = v != 0;
        },
        "Makes your position public"));
    f.push_back(f_int(
        "Map report interval", [cfg]() { return static_cast<int64_t>(cfg->map_report_settings.publish_interval_secs); },
        [cfg](int64_t v) {
            cfg->has_map_report_settings                   = true;
            cfg->map_report_settings.publish_interval_secs = static_cast<uint32_t>(v);
        },
        0, 86400, "s", "0 = default (1 h)"));
    return make_editor(
        app, "MQTT", std::move(f),
        [&app, cfg]() {
            meshtastic_ModuleConfig c = meshtastic_ModuleConfig_init_zero;
            c.which_payload_variant   = meshtastic_ModuleConfig_mqtt_tag;
            c.payload_variant.mqtt    = *cfg;
            send_module(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_telemetry(App& app)
{
    auto cfg = std::make_shared<meshtastic_ModuleConfig_TelemetryConfig>(app.mesh.modules.telemetry);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Send device metrics", device_telemetry_enabled, ""));
    f.push_back(MZ_INT("Device metrics interval", device_update_interval, 0, 86400, "s", "0 = default (30 min)"));
    f.push_back(MZ_BOOL("Environment sensors", environment_measurement_enabled, ""));
    f.push_back(MZ_INT("Environment interval", environment_update_interval, 0, 86400, "s", "0 = default (30 min)"));
    f.push_back(MZ_BOOL("Power sensors", power_measurement_enabled, ""));
    f.push_back(MZ_INT("Power interval", power_update_interval, 0, 86400, "s", "0 = default"));
    f.push_back(MZ_BOOL("Air quality sensors", air_quality_enabled, ""));
    f.push_back(MZ_INT("Air quality interval", air_quality_interval, 0, 86400, "s", "0 = default"));
    return make_editor(
        app, "Telemetry", std::move(f),
        [&app, cfg]() {
            meshtastic_ModuleConfig c   = meshtastic_ModuleConfig_init_zero;
            c.which_payload_variant     = meshtastic_ModuleConfig_telemetry_tag;
            c.payload_variant.telemetry = *cfg;
            send_module(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_neighbor(App& app)
{
    auto cfg = std::make_shared<meshtastic_ModuleConfig_NeighborInfoConfig>(app.mesh.modules.neighbor_info);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Enabled", enabled, ""));
    f.push_back(MZ_INT("Update interval", update_interval, 0, 86400, "s", "0 = default (6 h)"));
    f.push_back(MZ_BOOL("Send over LoRa", transmit_over_lora, ""));
    return make_editor(
        app, "Neighbor info", std::move(f),
        [&app, cfg]() {
            meshtastic_ModuleConfig c       = meshtastic_ModuleConfig_init_zero;
            c.which_payload_variant         = meshtastic_ModuleConfig_neighbor_info_tag;
            c.payload_variant.neighbor_info = *cfg;
            send_module(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_store_forward(App& app)
{
    auto cfg = std::make_shared<meshtastic_ModuleConfig_StoreForwardConfig>(app.mesh.modules.store_forward);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Enabled", enabled, ""));
    f.push_back(MZ_BOOL("Act as server", is_server, ""));
    f.push_back(MZ_BOOL("Heartbeat", heartbeat, ""));
    f.push_back(MZ_INT("Records kept", records, 0, 100000, "", "0 = default"));
    f.push_back(MZ_INT("Max messages returned", history_return_max, 0, 1000, "", "0 = default (25)"));
    f.push_back(MZ_INT("History window", history_return_window, 0, 100000, "min", "0 = default (4 h)"));
    return make_editor(
        app, "Store & forward", std::move(f),
        [&app, cfg]() {
            meshtastic_ModuleConfig c       = meshtastic_ModuleConfig_init_zero;
            c.which_payload_variant         = meshtastic_ModuleConfig_store_forward_tag;
            c.payload_variant.store_forward = *cfg;
            send_module(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_range_test(App& app)
{
    auto cfg = std::make_shared<meshtastic_ModuleConfig_RangeTestConfig>(app.mesh.modules.range_test);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Enabled", enabled, ""));
    f.push_back(MZ_INT("Send interval", sender, 0, 86400, "s", "0 = receive only"));
    f.push_back(MZ_BOOL("Save results", save, ""));
    return make_editor(
        app, "Range test", std::move(f),
        [&app, cfg]() {
            meshtastic_ModuleConfig c    = meshtastic_ModuleConfig_init_zero;
            c.which_payload_variant      = meshtastic_ModuleConfig_range_test_tag;
            c.payload_variant.range_test = *cfg;
            send_module(app.mesh, c);
        },
        kRestartNote);
}

// --------------------------------------------------------------- channels

constexpr uint8_t kDefaultPskIndex = 1;

std::string psk_summary(const meshtastic_ChannelSettings& s)
{
    if (s.psk.size == 0) return "no encryption";
    if (s.psk.size == 1) return s.psk.bytes[0] == kDefaultPskIndex ? "default key" : "simple key " + std::to_string(s.psk.bytes[0]);
    return s.psk.size == 16 ? "AES-128" : "AES-256";
}

std::string channel_url(const Mesh& mesh, int only_index)
{
    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!mesh.channel_enabled(i)) continue;
        if (only_index >= 0 && i != only_index) continue;
        set.settings[set.settings_count++] = mesh.channels[i].settings;
    }
    set.has_lora_config = true;
    set.lora_config     = mesh.config.lora;
    uint8_t buffer[meshtastic_ChannelSet_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, meshtastic_ChannelSet_fields, &set)) return {};
    return std::string("https://meshtastic.org/e/") + (only_index >= 0 ? "?add=true" : "") + "#" +
           base64_encode(buffer, stream.bytes_written, true);
}

// Reads the channels out of a shared URL. Returns an empty string, or what is wrong with it.
std::string parse_channel_url(const std::string& url, meshtastic_ChannelSet& set)
{
    const size_t hash = url.find('#');
    std::vector<uint8_t> bytes;
    if (hash == std::string::npos || !base64_decode(url.substr(hash + 1), bytes) || bytes.empty()) {
        return "Not a channel URL";
    }
    set = meshtastic_ChannelSet_init_zero;
    if (!pb_decode_msg(meshtastic_ChannelSet_fields, bytes.data(), bytes.size(), set) || set.settings_count == 0) {
        return "Invalid channel URL";
    }
    return {};
}

// Adds the channels of a shared URL to free slots. Returns a message for the user.
std::string add_channels_from_url(Mesh& mesh, const std::string& url)
{
    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
    if (const std::string error = parse_channel_url(url, set); !error.empty()) return error;
    int added = 0;
    mesh.begin_edit();
    for (pb_size_t s = 0; s < set.settings_count; ++s) {
        const meshtastic_ChannelSettings& incoming = set.settings[s];
        bool known = false;
        int free_slot = -1;
        for (int i = 0; i < kMaxChannels; ++i) {
            if (mesh.channel_enabled(i)) {
                const meshtastic_ChannelSettings& have = mesh.channels[i].settings;
                if (strcmp(have.name, incoming.name) == 0 && have.psk.size == incoming.psk.size &&
                    memcmp(have.psk.bytes, incoming.psk.bytes, have.psk.size) == 0) {
                    known = true;
                }
            } else if (free_slot < 0 && i > 0) {
                free_slot = i;
            }
        }
        if (known || free_slot < 0) continue;
        meshtastic_Channel c = meshtastic_Channel_init_zero;
        c.index              = static_cast<int8_t>(free_slot);
        c.has_settings       = true;
        c.settings           = incoming;
        c.role               = meshtastic_Channel_Role_SECONDARY;
        mesh.set_channel(c);
        ++added;
    }
    mesh.commit_edit();
    if (added == 0) return "No channels added";
    return "Added " + std::to_string(added) + (added == 1 ? " channel" : " channels");
}

std::string preset_label(int preset)
{
    for (const PresetInfo& p : kPresets) {
        if (p.value == preset) return p.name;
    }
    return "preset " + std::to_string(preset) + " (runs as LongFast)";
}

// The radio settings a shared link carries, in a few words.
std::string lora_summary(const meshtastic_Config_LoRaConfig& l)
{
    std::string out = l.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET ? "" : region_name(l.region) + ", ";
    if (l.use_preset) {
        out += preset_label(l.modem_preset);
    } else {
        char modem[48];
        snprintf(modem, sizeof(modem), "SF%u / %s kHz / CR 4/%u", static_cast<unsigned>(l.spread_factor),
                 l.bandwidth == 62 ? "62.5" : l.bandwidth == 31 ? "31.25" : std::to_string(l.bandwidth).c_str(),
                 static_cast<unsigned>(l.coding_rate));
        out += modem;
    }
    if (l.override_frequency != 0) {
        char mhz[24];
        snprintf(mhz, sizeof(mhz), ", %.3f MHz", static_cast<double>(l.override_frequency));
        out += mhz;
    } else if (l.channel_num != 0) {
        out += ", slot " + std::to_string(l.channel_num);
    }
    return out;
}

// What of this node's radio settings a shared link would change: the part
// that decides which other nodes can be heard.
meshtastic_Config_LoRaConfig lora_with(const meshtastic_Config_LoRaConfig& mine, const meshtastic_Config_LoRaConfig& theirs)
{
    meshtastic_Config_LoRaConfig l = mine;
    l.use_preset         = theirs.use_preset;
    l.modem_preset       = theirs.modem_preset;
    l.bandwidth          = theirs.bandwidth;
    l.spread_factor      = theirs.spread_factor;
    l.coding_rate        = theirs.coding_rate;
    l.channel_num        = theirs.channel_num;
    l.override_frequency = theirs.override_frequency;
    l.frequency_offset   = theirs.frequency_offset;
    if (theirs.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) l.region = theirs.region;
    if (theirs.hop_limit != 0) l.hop_limit = theirs.hop_limit;
    return l;
}

bool lora_differs(const meshtastic_Config_LoRaConfig& a, const meshtastic_Config_LoRaConfig& b)
{
    if (a.region != b.region || a.use_preset != b.use_preset || a.channel_num != b.channel_num ||
        a.override_frequency != b.override_frequency) {
        return true;
    }
    if (a.use_preset) return a.modem_preset != b.modem_preset;
    return a.bandwidth != b.bandwidth || a.spread_factor != b.spread_factor || a.coding_rate != b.coding_rate;
}

// Makes this node's channels and radio settings those of a shared link, the
// way the phone apps do when a link is opened: its first channel becomes the
// primary one and channels it does not name are switched off.
std::string replace_from_url(Mesh& mesh, const std::string& url)
{
    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
    if (const std::string error = parse_channel_url(url, set); !error.empty()) return error;
    mesh.begin_edit();
    for (int i = 0; i < kMaxChannels; ++i) {
        meshtastic_Channel c = meshtastic_Channel_init_zero;
        c.index              = static_cast<int8_t>(i);
        if (i < static_cast<int>(set.settings_count)) {
            c.has_settings = true;
            c.settings     = set.settings[i];
            c.role         = i == 0 ? meshtastic_Channel_Role_PRIMARY : meshtastic_Channel_Role_SECONDARY;
        } else if (!mesh.channel_enabled(i)) {
            continue;
        }
        mesh.set_channel(c);
    }
    if (set.has_lora_config) {
        meshtastic_Config c     = meshtastic_Config_init_zero;
        c.which_payload_variant = meshtastic_Config_lora_tag;
        c.payload_variant.lora  = lora_with(mesh.config.lora, set.lora_config);
        mesh.set_config(c);
    }
    mesh.commit_edit();
    return "Switched. Radio restarting.";
}

// Says what a shared link contains and asks what to do with it.
void offer_channels(App* a, const std::string& url)
{
    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
    if (!parse_channel_url(url, set).empty()) return;
    std::string names;
    for (pb_size_t s = 0; s < set.settings_count; ++s) {
        const meshtastic_ChannelSettings& c = set.settings[s];
        names += (s ? "\n" : "") + std::string(c.name[0] ? c.name : "(default name)") + "  -  " + psk_summary(c);
    }
    const bool radio = set.has_lora_config && lora_differs(a->mesh.config.lora, lora_with(a->mesh.config.lora, set.lora_config));
    const auto add   = [a, url]() {
        const std::string result = add_channels_from_url(a->mesh, url);
        a->toast(result, result.rfind("Added", 0) != 0);
    };
    if (!radio) {
        const std::string question =
            set.settings_count == 1 ? "Add this channel?" : "Add these " + std::to_string(set.settings_count) + " channels?";
        a->push(make_confirm(*a, "Add channels", question + "\n\n" + ui::clean(names), {"Cancel", "Add"}, [add](int choice) {
            if (choice == 1) add();
        }));
        return;
    }
    // The link is for a mesh that runs on other radio settings than this node.
    // Kept short: three choices leave room for five lines.
    std::string list = names;
    for (char& ch : list) ch = ch == '\n' ? ',' : ch;
    const std::string text = ui::clean(list, true) + "\nRadio: " + lora_summary(set.lora_config) +
                             "\nSwitching replaces your channels and radio settings.";
    a->push(make_confirm(*a, "Channels and radio", text, {"Cancel", "Add the channels only", "Switch to this setup"},
                         [a, url, add](int choice) {
                             if (choice == 1) add();
                             if (choice == 2) a->toast(replace_from_url(a->mesh, url));
                         }));
}

// Setups that local communities have agreed on, which are not among the
// official presets. Each is the link its community hands out, so it can be
// checked against the source and replaced when they change it.
struct CommunitySetup {
    const char* name;
    const char* where;
    const char* url;
};
const CommunitySetup kCommunitySetups[] = {
    // https://meshtastic.es/docs/generador-configuracion/
    {"SFNarrow", "Spain", "https://meshtastic.org/e/#ChESAQEaCFNGTmFycm93KAEwARIYCAAYPiAHKAU4A0ADSAFoAXWNZ1lEyAYB"},
};

void pick_community_setup(App* a)
{
    std::vector<std::string> labels;
    for (const CommunitySetup& s : kCommunitySetups) labels.push_back(std::string(s.name) + " (" + s.where + ")");
    a->push(make_picker(*a, LV_SYMBOL_LEFT " Community setups", labels, -1, [a](int choice) {
        if (choice < 0 || static_cast<size_t>(choice) >= sizeof(kCommunitySetups) / sizeof(kCommunitySetups[0])) return;
        offer_channels(a, kCommunitySetups[choice].url);
    }));
}

std::unique_ptr<Screen> page_channel(App& app, int index)
{
    auto cfg = std::make_shared<meshtastic_Channel>(app.mesh.channels[index]);
    cfg->index        = static_cast<int8_t>(index);
    cfg->has_settings = true;
    if (index == 0) cfg->role = meshtastic_Channel_Role_PRIMARY;
    App* a = &app;
    std::vector<Field> f;
    if (index > 0) {
        f.push_back(f_bool(
            "Enabled", [cfg]() { return static_cast<int64_t>(cfg->role != meshtastic_Channel_Role_DISABLED); },
            [cfg](int64_t v) {
                const bool fresh = cfg->role == meshtastic_Channel_Role_DISABLED && v;
                cfg->role        = v ? meshtastic_Channel_Role_SECONDARY : meshtastic_Channel_Role_DISABLED;
                if (fresh && cfg->settings.psk.size == 0) {
                    cfg->settings.psk.size     = 1;
                    cfg->settings.psk.bytes[0] = kDefaultPskIndex;
                }
            },
            ""));
    }
    f.push_back(f_text(
        "Name", [cfg]() { return std::string(cfg->settings.name); },
        [cfg](const std::string& s) {
            if (s.size() > 11) return std::string("Name: max 11 characters");
            snprintf(cfg->settings.name, sizeof(cfg->settings.name), "%s", s.c_str());
            return std::string();
        },
        11, "Empty = default channel"));
    f.push_back(f_info("Encryption", [cfg]() { return psk_summary(cfg->settings); }));
    f.push_back(f_text(
        "Key (base64)", [cfg]() { return base64_encode(cfg->settings.psk.bytes, cfg->settings.psk.size); },
        [cfg](const std::string& s) {
            std::vector<uint8_t> key;
            if (!base64_decode(s, key)) return std::string("Not valid base64");
            if (key.size() != 0 && key.size() != 1 && key.size() != 16 && key.size() != 32) {
                return std::string("Key must be 1, 16 or 32 bytes");
            }
            cfg->settings.psk.size = static_cast<pb_size_t>(key.size());
            memcpy(cfg->settings.psk.bytes, key.data(), key.size());
            return std::string();
        },
        48, "AQ== is the default key"));
    f.push_back(f_action(
        "Generate new key",
        [cfg, a]() {
#if defined(MESHCARD_ESP32)
            esp_fill_random(cfg->settings.psk.bytes, 32);
#else
            std::random_device rd;
            for (auto& b : cfg->settings.psk.bytes) b = static_cast<uint8_t>(rd());
#endif
            cfg->settings.psk.size = 32;
            a->toast("New key generated");
        },
        "", true));
    f.push_back(f_action(
        "Use default key",
        [cfg]() {
            cfg->settings.psk.size     = 1;
            cfg->settings.psk.bytes[0] = kDefaultPskIndex;
        },
        "Not private", true));
    f.push_back(f_enum(
        "Location accuracy", [cfg]() { return static_cast<int64_t>(cfg->settings.module_settings.position_precision); },
        [cfg](int64_t v) {
            cfg->settings.has_module_settings               = true;
            cfg->settings.module_settings.position_precision = static_cast<uint32_t>(v);
        },
        precision_opts(), ""));
    f.push_back(f_bool(
        "Muted", [cfg]() { return static_cast<int64_t>(cfg->settings.module_settings.is_muted); },
        [cfg](int64_t v) {
            cfg->settings.has_module_settings      = true;
            cfg->settings.module_settings.is_muted = v != 0;
        },
        ""));
    f.push_back(f_bool(
        "MQTT uplink", [cfg]() { return static_cast<int64_t>(cfg->settings.uplink_enabled); },
        [cfg](int64_t v) { cfg->settings.uplink_enabled = v != 0; }, ""));
    f.push_back(f_bool(
        "MQTT downlink", [cfg]() { return static_cast<int64_t>(cfg->settings.downlink_enabled); },
        [cfg](int64_t v) { cfg->settings.downlink_enabled = v != 0; }, ""));
    f.push_back(f_action(
        "Share (QR code)",
        [a, index]() {
            if (!a->mesh.channel_enabled(index)) {
                a->toast("Save the channel first", true);
                return;
            }
            a->push(make_qr(*a, "Share channel", channel_url(a->mesh, index)));
        },
        ""));
    return make_editor(app, "Channel " + std::to_string(index), std::move(f), [a, cfg]() {
        a->mesh.begin_edit();
        a->mesh.set_channel(*cfg);
        a->mesh.commit_edit();
    });
}

class ChannelsScreen : public Screen {
public:
    explicit ChannelsScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        list_->set_prefix_width(12);
    }
    std::string title() const override { return LV_SYMBOL_LEFT " Channels"; }
    std::string hint() const override { return "Enter edit   Esc back"; }

    void refresh() override
    {
        ui::ListItems items;
        for (int i = 0; i < kMaxChannels; ++i) {
            const meshtastic_Channel& c = mesh.channels[i];
            ui::ListItem it;
            it.prefix = std::to_string(i);
            if (mesh.channel_enabled(i)) {
                it.text  = ui::clean(mesh.channel_name(i), true) + (i == 0 ? "  (primary)" : "");
                it.right = psk_summary(c.settings);
            } else {
                it.text      = "empty slot";
                it.has_color = true;
                it.color     = ui::c_dim();
            }
            items.push_back(it);
        }
        const auto action = [&items](const std::string& text) {
            ui::ListItem it;
            it.text      = text;
            it.has_color = true;
            it.color     = ui::c_accent();
            items.push_back(it);
        };
        action(LV_SYMBOL_IMAGE "  Share all (QR code)");
        action(LV_SYMBOL_PLUS "  Add from URL");
        // Only on models that have a camera.
        scan_index_ = -1;
        if (Camera::available()) {
            scan_index_ = static_cast<int>(items.size());
            action(LV_SYMBOL_EYE_OPEN "  Scan QR code");
        }
        community_index_ = static_cast<int>(items.size());
        action(LV_SYMBOL_LIST "  Community setups (experimental)");
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k != K::Enter) return false;
        const int i = list_->selected();
        if (i < kMaxChannels) {
            app.push(page_channel(app, i));
        } else if (i == kMaxChannels) {
            app.push(make_qr(app, "Share channels", channel_url(mesh, -1)));
        } else if (i == scan_index_) {
            scan();
        } else if (i == community_index_) {
            pick_community_setup(&app);
        } else {
            App* a = &app;
            app.push(make_input(app, "Add channels", "", 400,
                                "Enter a meshtastic.org/e/# link",
                                [a](const std::string& text) {
                                    meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
                                    const std::string error   = parse_channel_url(trim(text), set);
                                    if (!error.empty()) {
                                        a->toast(error, true);
                                        return false;
                                    }
                                    a->pop();  // this page; the question takes its place
                                    offer_channels(a, trim(text));
                                    return false;
                                }));
        }
        return true;
    }

private:
    // Opens the camera, and asks before adding what a channel code contains.
    void scan()
    {
        App* a = &app;
        app.push(make_scanner(app, LV_SYMBOL_LEFT " Scan channel code", [a](const std::string& text) {
            meshtastic_ChannelSet set = meshtastic_ChannelSet_init_zero;
            if (!parse_channel_url(text, set).empty()) {
                a->toast("Not a channel code", true);
                return false;
            }
            a->pop();  // the camera view
            offer_channels(a, text);
            return true;
        }));
    }

    std::unique_ptr<ui::ListView> list_;
    int scan_index_      = -1;
    int community_index_ = -1;
};

// ---------------------------------------------------------- region picker

class RegionScreen : public Screen {
public:
    RegionScreen(App& a, bool first_run) : Screen(a), first_run_(first_run)
    {
        int y = 0;
        if (first_run) {
            lv_obj_t* l = ui::label(root, "Select your region to enable transmitting.",
                                    ui::c_warn(), ui::font_small());
            lv_obj_set_pos(l, 5, 2);
            lv_obj_set_width(l, kContentW - 10);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            y = 16;
        }
        list_ = std::make_unique<ui::ListView>(root, 0, y, kContentW, kContentH - y);
        list_->set_prefix_width(58);
        ui::ListItems items;
        int current = 0;
        for (size_t i = 0; i < sizeof(kRegions) / sizeof(kRegions[0]); ++i) {
            const RegionInfo& r = kRegions[i];
            ui::ListItem it;
            it.prefix           = r.name;
            it.has_prefix_color = true;
            it.prefix_color     = r.supported ? ui::c_accent() : ui::c_dim();
            it.text             = r.band;
            it.right            = r.supported ? "" : "unsupported";
            it.has_color        = !r.supported;
            it.color            = ui::c_dim();
            items.push_back(it);
            if (mesh.config.lora.region == r.value) current = static_cast<int>(i);
        }
        list_->set_items(items);
        list_->select(current);
    }
    std::string title() const override { return first_run_ ? "Select region" : LV_SYMBOL_LEFT " Region"; }
    std::string hint() const override { return "Up/Down choose   Enter select   Esc later"; }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k != K::Enter) return false;
        const RegionInfo& r = kRegions[static_cast<size_t>(list_->selected())];
        if (!mesh.ready()) {
            app.toast("Radio offline", true);
            return true;
        }
        meshtastic_Config c     = meshtastic_Config_init_zero;
        c.which_payload_variant = meshtastic_Config_lora_tag;
        c.payload_variant.lora  = mesh.config.lora;
        c.payload_variant.lora.region     = static_cast<Region>(r.value);
        c.payload_variant.lora.tx_enabled = true;
        c.payload_variant.lora            = lora_cleaned(mesh.config.lora, c.payload_variant.lora);
        send_config(mesh, c);
        app.toast(std::string("Region set to ") + r.name + ". Radio restarting.");
        app.pop();
        return true;
    }

private:
    bool first_run_;
    std::unique_ptr<ui::ListView> list_;
};

// -------------------------------------------------------------- setup tab

class SetupScreen : public Screen {
public:
    explicit SetupScreen(App& a) : Screen(a) { list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH); }
    int tab() const override { return kTabSetup; }
    std::string hint() const override { return "Enter open   Tab next page   ? help   Hold Esc exit"; }

    void refresh() override
    {
        ui::ListItems items;
        entries_.clear();
        const auto header = [&](const std::string& text) {
            ui::ListItem it;
            it.text   = text;
            it.header = true;
            items.push_back(it);
            entries_.push_back(nullptr);
        };
        const auto entry = [&](const std::string& text, const std::string& right, std::function<void()> fn) {
            ui::ListItem it;
            it.text  = text;
            it.right = ui::clean(right, true);
            items.push_back(it);
            entries_.push_back(std::move(fn));
        };
        App* a   = &app;
        Mesh* m  = &mesh;
        const auto open = [a](std::unique_ptr<Screen> (*page)(App&)) {
            return [a, page]() {
                if (!a->mesh.ever_ready()) {
                    a->toast("Radio offline", true);
                    return;
                }
                a->push(page(*a));
            };
        };

        header("Radio");
        entry("Region", region_name(mesh.config.lora.region), [a]() { a->push(make_region_picker(*a, false)); });
        entry("LoRa radio", mesh.preset_name() + (mesh.config.lora.override_frequency != 0 ? ", fixed freq" : ""),
              open(page_lora));
        int enabled = 0;
        for (int i = 0; i < kMaxChannels; ++i) enabled += mesh.channel_enabled(i) ? 1 : 0;
        entry("Channels", std::to_string(enabled) + " of 8", [a]() { a->push(make_channels(*a)); });
        // MeshCard: message alarm (rings even when MeshCard is closed).
        {
            const AlertConf al = alert_conf_load();
            header("Alarm");
            const auto save = [](AlertConf c, bool ring) {
                alert_conf_save(c);
                if (ring && c.on) alert_ring(c);
            };
            entry("Alarm", al.on ? "on" : "off", [a, al, save]() {
                a->push(make_picker(*a, LV_SYMBOL_LEFT " Alarm", {"On", "Off"}, al.on ? 0 : 1, [al, save](int c) {
                    AlertConf n = al;
                    n.on        = c == 0;
                    save(n, false);
                }));
            });
            entry("Ring for", al.all ? "all messages" : "direct messages", [a, al, save]() {
                a->push(make_picker(*a, LV_SYMBOL_LEFT " Ring for", {"Direct messages", "All messages"}, al.all ? 1 : 0,
                                    [al, save](int c) {
                                        AlertConf n = al;
                                        n.all       = c == 1;
                                        save(n, false);
                                    }));
            });
            entry("Sound", alert_sound_name(al.sound), [a, al, save]() {
                std::vector<std::string> names;
                for (int i = 0; i < kAlertSoundCount; ++i) names.push_back(alert_sound_name(i));
                a->push(make_picker(*a, LV_SYMBOL_LEFT " Sound", names, al.sound, [al, save](int c) {
                    AlertConf n = al;
                    n.sound     = c;
                    save(n, true);  // plays it, so you hear what you picked
                }));
            });
            entry("Volume", std::to_string(al.volume) + "%", [a, al, save]() {
                static const int levels[] = {25, 50, 75, 100};
                int cur = 3;
                for (int i = 0; i < 4; ++i)
                    if (al.volume == levels[i]) cur = i;
                a->push(make_picker(*a, LV_SYMBOL_LEFT " Volume", {"25%", "50%", "75%", "100%"}, cur, [al, save](int c) {
                    AlertConf n = al;
                    n.volume    = levels[c < 0 ? 0 : c > 3 ? 3 : c];
                    save(n, true);
                }));
            });
            entry("Repeat", al.repeat ? "until a key" : "once", [a, al, save]() {
                a->push(make_picker(*a, LV_SYMBOL_LEFT " Repeat", {"Until a key is pressed", "Ring once"},
                                    al.repeat ? 0 : 1, [al, save](int c) {
                                        AlertConf n = al;
                                        n.repeat    = c == 0;
                                        save(n, false);
                                    }));
            });
            entry("Test alarm", "", [a, al]() {
                AlertConf t = al;
                t.on        = true;
                alert_ring(t);
                a->toast("Ringing once");
            });
        }
        header("This node");
        entry("Your node", mesh.long_name(mesh.my_num), open(page_user));
        entry("Device", role_name(mesh.config.device.role), open(page_device));
        entry("Position & GPS",
              mesh.config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED ? "GPS on" : "GPS off",
              open(page_position));
        entry("Power", "", open(page_power));
        entry("Network", "", open(page_network));
        entry("Security", "", open(page_security));
#if !defined(MESHCARD_ESP32)
        // Zero only: they talk to meshzerod, which the ADV does not have (its radio is always on).
        entry("Run in background", mesh.background == 0 ? "off" : "on", [a, m]() {
            if (m->background < 0) {
                a->toast("Radio offline", true);
                return;
            }
            a->push(make_confirm(*a, "Run in background",
                                 "Keep the radio on when MeshZero is closed, to receive messages and relay for the "
                                 "mesh. Turn off to free the LoRa cap for other apps.",
                                 {"On", "Off"}, [m](int c) { m->set_background(c == 0); }));
        });
        entry("Network access", mesh.network_access == 1 ? "on" : "off", [a, m]() {
            if (m->network_access < 0) {
                a->toast("Radio offline", true);
                return;
            }
            a->push(make_confirm(*a, "Network access",
                                 "Let other devices on your network use this radio, for MeshMonitor or a phone app. "
                                 "There is no password.",
                                 {"Off", "On"}, [m](int c) { m->set_network_access(c == 1); }));
        });
#endif
        header("Modules");
        entry("MQTT", mesh.modules.mqtt.enabled ? "on" : "off", open(page_mqtt));
        entry("Telemetry", "", open(page_telemetry));
        entry("Neighbor info", mesh.modules.neighbor_info.enabled ? "on" : "off", open(page_neighbor));
        entry("Store & forward", mesh.modules.store_forward.enabled ? "on" : "off", open(page_store_forward));
        entry("Range test", mesh.modules.range_test.enabled ? "on" : "off", open(page_range_test));
        header("Maintenance");
        entry("Restart radio", "", [a, m]() {
            a->push(make_confirm(*a, "Restart radio", "Restart the radio?",
                                 {"Cancel", "Restart"}, [a, m](int c) {
                                     if (c == 1) {
                                         m->reboot();
                                         a->toast("Radio restarting");
                                     }
                                 }));
        });
        entry("Reset node list", "", [a, m]() {
            a->push(make_confirm(*a, "Reset node list", "Clear the node list?",
                                 {"Cancel", "Clear"}, [a, m](int c) {
                                     if (c == 1) {
                                         m->nodedb_reset();
                                         a->toast("Node list cleared");
                                     }
                                 }));
        });
        entry("Factory reset", "", [a, m]() {
            a->push(make_confirm(*a, "Factory reset",
                                 "Erase settings and channels? Everything also erases this node's keys.",
                                 {"Cancel", "Reset settings", "Reset everything"}, [a, m](int c) {
                                     if (c >= 1) {
                                         m->factory_reset(c == 2);
                                         a->toast("Factory reset. Radio restarting.");
                                     }
                                 }));
        });
#if !defined(MESHCARD_ESP32)
        // Zero only: the ADV has no internet link to look for updates.
        const std::string newer = update::available();
        entry("Updates",
              !newer.empty() ? newer + " available" : !update::enabled() ? "off" : update::checked() ? "up to date" : "",
              [a, newer]() {
                  const bool on    = update::enabled();
                  std::string text = "This is MeshZero " MESHZERO_VERSION ".";
                  if (!newer.empty()) {
                      text += " Version " + newer + " is available: update from the store, or get the package from "
                              "github.com/s0liton/MeshZero/releases.";
                  }
                  if (!on) text += "\n\nUpdate checks are turned off.";
                  a->push(make_confirm(*a, "Updates", text,
                                       {"Back", on ? "Turn off update checks" : "Turn on update checks"},
                                       [on](int c) {
                                           if (c == 1) update::set_enabled(!on);
                                       }));
              });
#endif
        entry("Help", "", [a]() { a->push(make_help(*a)); });
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k == K::Enter) {
            const size_t i = static_cast<size_t>(list_->selected());
            if (i < entries_.size() && entries_[i]) {
                auto fn = entries_[i];
                fn();
            }
            return true;
        }
        return false;
    }

private:
    std::unique_ptr<ui::ListView> list_;
    std::vector<std::function<void()>> entries_;
};

}  // namespace

std::unique_ptr<Screen> make_setup(App& app)
{
    return std::make_unique<SetupScreen>(app);
}

std::unique_ptr<Screen> make_channels(App& app)
{
    return std::make_unique<ChannelsScreen>(app);
}

std::unique_ptr<Screen> make_region_picker(App& app, bool first_run)
{
    return std::make_unique<RegionScreen>(app, first_run);
}

}  // namespace mz

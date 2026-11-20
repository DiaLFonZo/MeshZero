// SPDX-License-Identifier: GPL-3.0-only
// Setup tab: radio, node, channel and module configuration.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <random>

#include <unistd.h>

#include "common/util.h"
#include "meshtastic/apponly.pb.h"
#include "ui/camera.h"
#include "ui/screens/editor.h"
#include "ui/update.h"

namespace mz {
namespace {

using Region = meshtastic_Config_LoRaConfig_RegionCode;

// The Cap LoRa-1262 front end covers roughly 850-930 MHz. Only supported
// regions are offered; the others are here so that a radio already set to
// one shows its name.
struct RegionInfo {
    int value;
    const char* name;
    const char* band;
    // The radio daemon's own row for the region (firmware 2.7.26,
    // RadioInterface.cpp): its band in MHz and power limit in dBm.
    float start, end;
    int power;
    bool supported;  // the cap and the radio daemon's firmware can use it
};
const RegionInfo kRegions[] = {
    {meshtastic_Config_LoRaConfig_RegionCode_US, "US", "United States 902-928 MHz", 902.0f, 928.0f, 30, true},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_868, "EU_868", "Europe 869.4-869.65 MHz", 869.4f, 869.65f, 27, true},
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ, "ANZ", "Australia / NZ 915-928 MHz", 915.0f, 928.0f, 30, true},
    {meshtastic_Config_LoRaConfig_RegionCode_NZ_865, "NZ_865", "New Zealand 864-868 MHz", 864.0f, 868.0f, 36, true},
    {meshtastic_Config_LoRaConfig_RegionCode_JP, "JP", "Japan 920.5-923.5 MHz", 920.5f, 923.5f, 13, true},
    {meshtastic_Config_LoRaConfig_RegionCode_KR, "KR", "Korea 920-923 MHz", 920.0f, 923.0f, 23, true},
    {meshtastic_Config_LoRaConfig_RegionCode_TW, "TW", "Taiwan 920-925 MHz", 920.0f, 925.0f, 27, true},
    {meshtastic_Config_LoRaConfig_RegionCode_RU, "RU", "Russia 868.7-869.2 MHz", 868.7f, 869.2f, 20, true},
    {meshtastic_Config_LoRaConfig_RegionCode_IN, "IN", "India 865-867 MHz", 865.0f, 867.0f, 30, true},
    {meshtastic_Config_LoRaConfig_RegionCode_TH, "TH", "Thailand 920-925 MHz", 920.0f, 925.0f, 27, true},
    {meshtastic_Config_LoRaConfig_RegionCode_UA_868, "UA_868", "Ukraine 868-868.6 MHz", 868.0f, 868.6f, 14, true},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_919, "MY_919", "Malaysia 919-924 MHz", 919.0f, 924.0f, 27, true},
    {meshtastic_Config_LoRaConfig_RegionCode_SG_923, "SG_923", "Singapore 917-925 MHz", 917.0f, 925.0f, 20, true},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_868, "PH_868", "Philippines 868-869.4 MHz", 868.0f, 869.4f, 14, true},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_915, "PH_915", "Philippines 915-918 MHz", 915.0f, 918.0f, 24, true},
    {meshtastic_Config_LoRaConfig_RegionCode_KZ_863, "KZ_863", "Kazakhstan 863-868 MHz", 863.0f, 868.0f, 30, true},
    {meshtastic_Config_LoRaConfig_RegionCode_NP_865, "NP_865", "Nepal 865-868 MHz", 865.0f, 868.0f, 30, true},
    {meshtastic_Config_LoRaConfig_RegionCode_BR_902, "BR_902", "Brazil 902-907.5 MHz", 902.0f, 907.5f, 30, true},
    // Not in the radio daemon's region table (firmware 2.7.26): it falls back
    // to its row for UNSET, 902-928 MHz, and keeps transmitting.
    {meshtastic_Config_LoRaConfig_RegionCode_EU_866, "EU_866", "Europe 865.6-867.6 MHz", 902.0f, 928.0f, 30, false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_874, "EU_874", "Europe 874-874.4 MHz", 902.0f, 928.0f, 30, false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_917, "EU_917", "Europe 917.4-919.4 MHz", 902.0f, 928.0f, 30, false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_N_868, "EU_N_868", "Europe narrowband 868 MHz", 902.0f, 928.0f, 30,
     false},
    {meshtastic_Config_LoRaConfig_RegionCode_CN, "CN", "China 470-510 MHz", 470.0f, 510.0f, 19, false},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_433, "EU_433", "Europe 433-434 MHz", 433.0f, 434.0f, 10, false},
    {meshtastic_Config_LoRaConfig_RegionCode_UA_433, "UA_433", "Ukraine 433-434.7 MHz", 433.0f, 434.7f, 10, false},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_433, "MY_433", "Malaysia 433-435 MHz", 433.0f, 435.0f, 20, false},
    {meshtastic_Config_LoRaConfig_RegionCode_PH_433, "PH_433", "Philippines 433-434.7 MHz", 433.0f, 434.7f, 10, false},
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ_433, "ANZ_433", "Australia / NZ 433-434.8 MHz", 433.05f, 434.79f, 14,
     false},
    {meshtastic_Config_LoRaConfig_RegionCode_KZ_433, "KZ_433", "Kazakhstan 433-434.8 MHz", 433.075f, 434.775f, 10,
     false},
    {meshtastic_Config_LoRaConfig_RegionCode_LORA_24, "LORA_24", "2.4 GHz worldwide", 2400.0f, 2483.5f, 10, false},
    {meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M, "ITU1_2M", "2 m amateur band, ITU region 1", 902.0f, 928.0f, 30,
     false},
    {meshtastic_Config_LoRaConfig_RegionCode_ITU2_2M, "ITU2_2M", "2 m amateur band, ITU region 2", 902.0f, 928.0f, 30,
     false},
    {meshtastic_Config_LoRaConfig_RegionCode_ITU3_2M, "ITU3_2M", "2 m amateur band, ITU region 3", 902.0f, 928.0f, 30,
     false},
};

// What ? says about the region, on the LoRa page, in Setup and in the picker.
const char* const kRegionAbout =
    "The country or area you're in. It sets the frequencies and power the radio may use there, and the radio "
    "won't transmit until one is set.";

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
            double a = 0, b = 0;
            if (sscanf(p, "%lf-%lf MHz", &a, &b) == 2 && b >= a) {
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

// What the radio daemon can tune the SX1262 to, in MHz; RadioLib refuses
// anything else. A fixed frequency may be set anywhere in it.
constexpr double kRadioMinMHz = 150.0;
constexpr double kRadioMaxMHz = 960.0;

// Whether a frequency is in a region's band, edges included. The radio keeps
// it as a float, a few Hz from what was typed, so the edges get a little
// slack. True for a region whose band is not known.
bool in_band(int region, double mhz)
{
    double low = 0, high = 0;
    if (!region_band(region, low, high)) return true;
    return mhz >= low - 0.0001 && mhz <= high + 0.0001;
}

// A frequency in MHz as typed: up to 0.1 kHz, no trailing zeros.
std::string mhz_text(double mhz)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%.4f", mhz);
    std::string text = buf;
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    return text;
}

// A fixed frequency typed in, in MHz, or 0 for none (automatic). False when
// it is not a number the radio can tune to.
bool parse_mhz(const std::string& s, double& mhz)
{
    const std::string text = trim(s);
    mhz                    = 0;
    if (text.empty()) return true;
    char* end = nullptr;
    mhz       = strtod(text.c_str(), &end);
    return end != text.c_str() && *end == 0 && mhz >= kRadioMinMHz && mhz <= kRadioMaxMHz;
}

// The supported regions, and `current` when the radio is set to one that is
// not, so that it shows by name.
std::vector<Opt> region_opts(int current)
{
    std::vector<Opt> opts;
    opts.push_back({meshtastic_Config_LoRaConfig_RegionCode_UNSET, "UNSET"});
    for (const auto& r : kRegions) {
        if (r.supported || r.value == current) opts.push_back({r.value, std::string(r.name) + "  " + r.band});
    }
    return opts;
}

// The presets the radio daemon shipped with MeshZero knows, with what each
// stands for: bandwidth in kHz, spreading factor, coding rate 4/x. Taken from
// modemPresetToParams() in the firmware's MeshRadio.h for that version. It
// runs a preset it doesn't know (Very long, Lite, Narrow) as LongFast, under
// the name "Invalid", so none of those are offered.
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

bool known_preset(int preset)
{
    for (const PresetInfo& p : kPresets) {
        if (p.value == preset) return true;
    }
    return false;
}

// What a preset stands for; LongFast for one the radio does not know, as there.
const PresetInfo& preset_info(int preset)
{
    for (const PresetInfo& p : kPresets) {
        if (p.value == preset) return p;
    }
    return kPresets[0];
}

// ---- what the radio daemon makes of the LoRa settings (firmware 2.7.26,
// RadioInterface.cpp and MeshRadio.h), worked out the same way

// Its row for a region: UNSET's for UNSET, and for a region it lacks.
const RegionInfo& radio_region(int region)
{
    static const RegionInfo unset = {meshtastic_Config_LoRaConfig_RegionCode_UNSET, "UNSET", "", 902.0f, 928.0f, 30,
                                     false};
    for (const auto& r : kRegions) {
        if (r.value == region) return r;
    }
    return unset;
}

// A bandwidth as stored, in kHz: bwCodeToKHz().
float bw_khz(int code)
{
    switch (code) {
        case 31: return 31.25f;
        case 62: return 62.5f;
        case 200: return 203.125f;
        case 400: return 406.25f;
        case 800: return 812.5f;
        case 1600: return 1625.0f;
        default: return static_cast<float>(code);
    }
}

// The bandwidth a LoRa config runs on, in kHz.
float lora_bw_khz(const meshtastic_Config_LoRaConfig& l)
{
    return l.use_preset ? static_cast<float>(preset_info(l.modem_preset).bw) : bw_khz(l.bandwidth);
}

// Whether a region's band holds a bandwidth. When it doesn't, the radio
// falls back to LongFast.
bool bw_fits(int region, float khz)
{
    const RegionInfo& r = radio_region(region);
    return !(r.end - r.start < khz / 1000);
}

// How many frequency slots a region has at a bandwidth, in the radio's single
// precision; at least 1.
uint32_t slot_count(int region, float khz)
{
    if (khz <= 0 || !bw_fits(region, khz)) khz = 250.0f;  // LongFast, as the radio falls back to
    const RegionInfo& r = radio_region(region);
    const float n       = std::floor((r.end - r.start) / (0.0f + khz / 1000));
    return n < 1 ? 1 : static_cast<uint32_t>(n);
}

// A slot past the last one wraps round to another; the radio is set to pick one itself instead.
void fit_slot(meshtastic_Config_LoRaConfig& l)
{
    if (l.channel_num > slot_count(l.region, lora_bw_khz(l))) l.channel_num = 0;
}

// What the radio transmits with at most, in dBm, for an operator who is or
// isn't licensed: the region's limit, which licensed operators may go past,
// and never more than the SX1262's 22 dBm (SX126X_MAX_POWER, which MeshZero's
// radio.yaml leaves at its default). It can go down to -9 dBm.
constexpr int kRadioMinDbm = -9;
constexpr int kRadioMaxDbm = 22;
int power_cap(int region, bool licensed)
{
    return licensed ? kRadioMaxDbm : std::min(kRadioMaxDbm, radio_region(region).power);
}

// The bandwidths the SX1262 takes (RadioLib's SX126x::setBandwidth()), as
// stored and as shown. With any other the radio is left set up wrong.
struct Bandwidth {
    int code;
    const char* label;
};
const Bandwidth kBandwidths[] = {{7, "7.8 kHz"},   {10, "10.4 kHz"}, {15, "15.6 kHz"}, {20, "20.8 kHz"},
                                 {31, "31.25 kHz"}, {41, "41.7 kHz"}, {62, "62.5 kHz"}, {125, "125 kHz"},
                                 {250, "250 kHz"},  {500, "500 kHz"}};

// The bandwidths the region holds, and the one in use when it isn't among
// them, so that it shows by name.
std::vector<Opt> bandwidth_opts(const meshtastic_Config_LoRaConfig& l)
{
    std::vector<Opt> opts;
    const int current = l.use_preset ? preset_info(l.modem_preset).bw : l.bandwidth;
    bool listed       = false;
    for (const Bandwidth& b : kBandwidths) {
        if (!bw_fits(l.region, bw_khz(b.code))) continue;
        opts.push_back({b.code, b.label});
        listed = listed || b.code == current;
    }
    if (!listed) {
        std::string label = mhz_text(bw_khz(current)) + " kHz  not one this radio can use";
        for (const Bandwidth& b : kBandwidths) {
            if (b.code == current) label = std::string(b.label) + "  too wide for " + region_name(l.region);
        }
        opts.push_back({current, label});
    }
    return opts;
}

// The presets the region holds, the one in use, and Custom.
std::vector<Opt> modem_opts(const meshtastic_Config_LoRaConfig& l)
{
    std::vector<Opt> opts;
    for (const PresetInfo& p : kPresets) {
        const bool fits = bw_fits(l.region, static_cast<float>(p.bw));
        if (!fits && !(l.use_preset && l.modem_preset == p.value)) continue;
        std::string label = p.name;
        if (p.value == kPresets[0].value) label += "  (default)";
        if (!fits) label += "  too wide for " + region_name(l.region);
        opts.push_back({p.value, label});
    }
    if (l.use_preset && !known_preset(l.modem_preset)) {
        opts.push_back({l.modem_preset, modem_preset_name(l.modem_preset) + "  runs as LongFast"});
    }
    opts.push_back({kCustomModem, "Custom"});
    return opts;
}

// The coding rate a preset runs on, given what the radio has. The radio
// uses a coding rate stored next to a preset, when it is 5-8, instead of the
// preset's own (RadioInterface::applyModemConfig()); saving sends the
// preset's own, so that holds only for the preset the radio is on.
int preset_cr(const meshtastic_Config_LoRaConfig& l, const meshtastic_Config_LoRaConfig& radio)
{
    const bool same = radio.use_preset && radio.modem_preset == l.modem_preset;
    if (same && radio.coding_rate >= 5 && radio.coding_rate <= 8) return radio.coding_rate;
    return preset_info(l.modem_preset).cr;
}

// Moves a working copy from a preset to modem values of its own, starting
// from the ones in use.
void make_custom(meshtastic_Config_LoRaConfig& l, const meshtastic_Config_LoRaConfig& radio)
{
    if (!l.use_preset) return;
    const PresetInfo& p = preset_info(l.modem_preset);
    l.bandwidth         = static_cast<decltype(l.bandwidth)>(p.bw);
    l.spread_factor     = static_cast<decltype(l.spread_factor)>(p.sf);
    l.coding_rate       = static_cast<decltype(l.coding_rate)>(preset_cr(l, radio));
    l.use_preset        = false;
}

// The longest interval the radio firmware keeps, in seconds. It stands for
// never there (MAX_INTERVAL), though it counts it as about 24 days.
constexpr int64_t kMaxInterval = 2147483647;

bool router_role(int role)
{
    return role == meshtastic_Config_DeviceConfig_Role_ROUTER || role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
}

// An interval in a few words: "1 h", "30 min".
std::string dur_text(int64_t secs)
{
    if (secs % 3600 == 0) return std::to_string(secs / 3600) + " h";
    if (secs % 60 == 0) return std::to_string(secs / 60) + " min";
    return std::to_string(secs) + " s";
}

// Options with the current value added when it isn't among them, so that it shows by name.
std::vector<Opt> with_current(std::vector<Opt> opts, int64_t current, const std::string& label)
{
    for (const Opt& o : opts) {
        if (o.value == current) return opts;
    }
    opts.push_back({current, label});
    return opts;
}

// The default key, AQ== in base64.
constexpr uint8_t kDefaultPskIndex = 1;

// Channels::isDefaultChannel() in the radio firmware: the default key, and no
// name ("Default" counts as none) or the name of the preset in use.
bool default_channel(const meshtastic_Config_LoRaConfig& lora, const meshtastic_Channel& c)
{
    const meshtastic_ChannelSettings& s = c.settings;
    if (!c.has_settings || s.psk.size != 1 || s.psk.bytes[0] != kDefaultPskIndex) return false;
    const std::string preset = lora.use_preset ? modem_preset_name(lora.modem_preset) : "Custom";
    return !s.name[0] || strcmp(s.name, "Default") == 0 || s.name == preset;
}

// Whether the radio firmware holds an interval to its minimum for a public
// channel. It does so when it starts (NodeDB.cpp), replacing a shorter one,
// for two kinds of interval, each with a check of its own:
//  - Position: the broadcast and smart minimum intervals, when the first
//    channel that shares position is a default channel (isDefaultChannel).
//  - Telemetry: all telemetry intervals, when any channel is a default
//    channel, on a preset without a fixed frequency (hasDefaultChannel). Its
//    test for the default frequency slot always passes then, as the radio
//    isn't set up yet.
// `primary`, when given, stands in for channel 0 as it is being edited.
enum class MinimumFor { Position, Telemetry };
bool public_minimum(const Mesh& mesh, MinimumFor what, const meshtastic_Channel* primary = nullptr)
{
    const meshtastic_Config_LoRaConfig& lora = mesh.config.lora;
    const auto channel = [&](int i) -> const meshtastic_Channel& {
        return i == 0 && primary ? *primary : mesh.channels[i];
    };
    if (what == MinimumFor::Position) {
        for (int i = 0; i < kMaxChannels; ++i) {
            const meshtastic_ChannelSettings& s = channel(i).settings;
            if (s.has_module_settings && s.module_settings.position_precision != 0) {
                return default_channel(lora, channel(i));
            }
        }
        return false;
    }
    if (!lora.use_preset || lora.override_frequency != 0) return false;
    for (int i = 0; i < kMaxChannels; ++i) {
        if (default_channel(lora, channel(i))) return true;
    }
    return false;
}

// Makes an interval field take 0, the firmware's default, and refuse what
// the firmware would raise to `minimum` on a public channel.
void public_interval(Field& f, std::function<bool()> applies, std::function<int64_t()> minimum,
                     std::function<std::string()> default_text)
{
    f.zero_ok   = true;
    f.bounds    = [applies, minimum](int64_t& lo, int64_t&) {
        if (applies()) lo = minimum();
    };
    f.help_text = [applies, minimum, default_text]() {
        std::string text = "0 = default (" + default_text() + ")";
        if (applies()) text += ". At least " + dur_text(minimum()) + " on a public channel";
        return text;
    };
}

// The protocol rounds a position to a grid; these are the cell sizes other
// Meshtastic apps offer, as the radio firmware's own menu words them. 0
// stops position being sent on the channel at all. A value set elsewhere
// shows as the number of bits kept.
std::vector<Opt> precision_opts(uint32_t current)
{
    return with_current({{0, "Off"},
                         {10, "Within 23 km"},
                         {11, "Within 12 km"},
                         {12, "Within 5.8 km"},
                         {13, "Within 2.9 km"},
                         {14, "Within 1.5 km"},
                         {15, "Within 700 m"},
                         {16, "Within 350 m"},
                         {17, "Within 200 m"},
                         {18, "Within 90 m"},
                         {19, "Within 50 m"},
                         {32, "Precise"}},
                        current, current > 32 ? "Precise" : std::to_string(current) + " bits");
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
    // The radio firmware keeps the old name when it is sent an empty one.
    f.push_back(f_text(
        "Long name", [cfg]() { return std::string(cfg->long_name); },
        [cfg](const std::string& s) {
            if (s.empty()) return std::string("Long name can't be empty");
            snprintf(cfg->long_name, sizeof(cfg->long_name), "%s", s.c_str());
            return std::string();
        },
        sizeof(cfg->long_name) - 1));
    f.back().about = "Your node's name, as others see it in their node lists and chats.";
    f.push_back(f_text(
        "Short name", [cfg]() { return std::string(cfg->short_name); },
        [cfg](const std::string& s) {
            if (s.empty() || s.size() > 4) return std::string("Short name: 1-4 characters");
            snprintf(cfg->short_name, sizeof(cfg->short_name), "%s", s.c_str());
            return std::string();
        },
        4, "1-4 characters"));
    f.back().about = "A few characters that stand for you where there's little room, such as on the map and in node "
                     "lists.";
    f.push_back(MZ_BOOL("Licensed operator (ham)", is_licensed,
                        "Turns encryption off"));
    f.back().about = "For licensed ham radio operators. Removes encryption from your channels and direct messages, as "
                     "ham rules require, and lifts the region's power limit. Use your call sign as the long name.";
    f.push_back(f_bool(
        "Accept direct messages", [cfg]() { return static_cast<int64_t>(!(cfg->has_is_unmessagable && cfg->is_unmessagable)); },
        [cfg](int64_t v) {
            cfg->has_is_unmessagable = true;
            cfg->is_unmessagable     = v == 0;
        },
        ""));
    f.back().about = "Tells other nodes whether you take direct messages. When off, apps that honor it, MeshZero "
                     "included, don't offer to message you.";
    f.push_back(f_info("Node ID", [&app]() { return node_id_str(app.mesh.my_num); }));
    return make_editor(app, "Your node", std::move(f), [&app, cfg]() { app.mesh.set_owner(*cfg); });
}

// Leftovers of a custom setup must not survive a move to another region or
// to a preset: a frequency fixed for one band is wrong, and may be illegal,
// in another, and custom modem values next to a preset only mislead. A fixed
// frequency slot goes too, so that the new preset's public channel is on its
// own slot. What the user set in the same edit is theirs and stays.
meshtastic_Config_LoRaConfig lora_cleaned(const meshtastic_Config_LoRaConfig& before, meshtastic_Config_LoRaConfig l)
{
    // A bandwidth wider than the region's band makes the radio fall back to LongFast.
    if (!bw_fits(l.region, lora_bw_khz(l))) {
        l.use_preset   = true;
        l.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    }
    const bool region_changed = l.region != before.region;
    const bool to_preset      = l.use_preset && (!before.use_preset || l.modem_preset != before.modem_preset);
    if (l.use_preset) {
        // The preset's own values, as the radio fills them in when it starts.
        // The coding rate matters: the radio would save 0 as 5, and then run a
        // preset on a stored 5-8 instead of its own rate.
        const PresetInfo& p = preset_info(l.modem_preset);
        l.bandwidth         = static_cast<decltype(l.bandwidth)>(p.bw);
        l.spread_factor     = static_cast<decltype(l.spread_factor)>(p.sf);
        l.coding_rate       = static_cast<decltype(l.coding_rate)>(p.cr);
    }
    if (region_changed || to_preset) {
        if (l.override_frequency == before.override_frequency) l.override_frequency = 0;
        if (l.frequency_offset == before.frequency_offset) l.frequency_offset = 0;
    }
    if ((region_changed || to_preset) && l.channel_num == before.channel_num) l.channel_num = 0;
    fit_slot(l);
    // One outside the region's band stays: the user agreed to it when they set
    // it, or the radio already runs on it. One the radio cannot tune to does not go to it.
    if (l.override_frequency != 0 && (l.override_frequency < kRadioMinMHz || l.override_frequency > kRadioMaxMHz)) {
        l.override_frequency = 0;
    }
    return l;
}

// What the user is told, and has to accept, before a fixed frequency outside
// the region's band is taken.
std::string out_of_band_warning(int region, double mhz)
{
    double low = 0, high = 0;
    region_band(region, low, high);
    return mhz_text(mhz) + " MHz is outside the " + region_name(region) + " range of " + mhz_text(low) + "-" +
           mhz_text(high) +
           " MHz.\nTransmitting there may be against the law where you are, and the radio may not work as "
           "expected or reach other nodes.";
}

// The primary channel, made to follow the preset again, when it is the
// public channel of the preset being left with that preset's name written
// out, as links and setups do: unnamed, it takes the new preset's name, and
// with it that preset's frequency slot. Empty when it is a channel of its
// own, which stays as it is, or when the preset does not change.
std::optional<meshtastic_Channel> public_channel_moved(const Mesh& mesh, const meshtastic_Config_LoRaConfig& before,
                                                       const meshtastic_Config_LoRaConfig& after)
{
    if (!before.use_preset || !after.use_preset || before.modem_preset == after.modem_preset) return std::nullopt;
    meshtastic_Channel c = mesh.channels[0];
    const auto& s        = c.settings;
    const bool public_key = s.psk.size == 1 && s.psk.bytes[0] == 1;
    if (!c.has_settings || !public_key || modem_preset_name(before.modem_preset) != s.name) return std::nullopt;
    c.settings.name[0] = 0;
    return c;
}

std::unique_ptr<Screen> page_lora(App& app)
{
    auto cfg    = std::make_shared<meshtastic_Config_LoRaConfig>(app.mesh.config.lora);
    auto before = std::make_shared<meshtastic_Config_LoRaConfig>(app.mesh.config.lora);
    const meshtastic_NodeInfo* self = app.mesh.me();
    const bool licensed             = self && self->has_user && self->user.is_licensed;
    std::vector<Field> f;
    f.push_back(f_enum(
        "Region", [cfg]() { return static_cast<int64_t>(cfg->region); },
        [&app, cfg](int64_t v) {
            const auto region = static_cast<Region>(v);
            if (region == cfg->region) return;
            cfg->region = region;
            std::string changed;
            // A fixed frequency outside the new band was never agreed to for this region.
            if (cfg->override_frequency != 0 && !in_band(region, cfg->override_frequency)) {
                cfg->override_frequency = 0;
                changed = "Fixed frequency cleared";
            }
            // The radio would fall back to LongFast; show it here.
            if (!bw_fits(region, lora_bw_khz(*cfg))) {
                cfg->use_preset   = true;
                cfg->modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
                changed += std::string(changed.empty() ? "" : ", ") + "modem set to LongFast";
            }
            fit_slot(*cfg);
            if (!changed.empty()) {
                changed[0] = static_cast<char>(toupper(static_cast<unsigned char>(changed[0])));
                app.toast(changed + " for " + region_name(region));
            }
        },
        region_opts(cfg->region), "Required to transmit"));
    f.back().about = kRegionAbout;
    // One row says what the radio runs on: a preset, or values of its own.
    f.push_back(f_enum(
        "Modem", [cfg]() { return cfg->use_preset ? static_cast<int64_t>(cfg->modem_preset) : kCustomModem; },
        [cfg, before](int64_t v) {
            if (v == kCustomModem) {
                make_custom(*cfg, *before);
                return;
            }
            cfg->modem_preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(v);
            cfg->use_preset   = true;
            fit_slot(*cfg);
        },
        {}, "Must match the rest of the mesh"));
    f.back().opts_of = [cfg]() { return modem_opts(*cfg); };
    // A fixed frequency is set far down the page; say so where the modem is named.
    f.back().note  = [cfg]() { return std::string(cfg->override_frequency != 0 ? ", fixed freq" : ""); };
    f.back().about = "How the radio trades speed for range. Slower presets reach further but take longer to send. "
                     "Nodes only hear each other on the same one. Custom uses the values below. Presets too wide "
                     "for the region's band aren't offered.";
    f.push_back(MZ_INT("Hop limit", hop_limit, 0, 7, "hops", "Default 3. 0 = direct neighbors only"));
    f.back().about = "How many times your messages may be relayed on their way. More reaches further but adds "
                     "traffic to the mesh. 3 suits most meshes; 0 reaches only nodes in direct range.";
    f.push_back(MZ_BOOL("Transmit enabled", tx_enabled, ""));
    f.back().about = "When off, the radio only listens: it doesn't send your messages or relay other nodes'.";
    f.push_back(MZ_INT("Transmit power", tx_power, kRadioMinDbm, kRadioMaxDbm, "dBm", ""));
    f.back().bounds = [cfg, licensed](int64_t&, int64_t& hi) { hi = power_cap(cfg->region, licensed); };
    // The radio saves what it makes of 0 when it starts, the region's limit,
    // which reads back as that or more.
    f.back().value_text = [cfg, licensed](int64_t v) {
        const int region_max = power_cap(cfg->region, false);
        const int cap        = power_cap(cfg->region, licensed);
        if (v == 0 && region_max < cap) return "region max (" + std::to_string(region_max) + " dBm)";
        if (v == 0 || v >= cap) return "max (" + std::to_string(cap) + " dBm)";
        return std::string();
    };
    f.back().help_text = [cfg]() {
        return "0 = region maximum (" + std::to_string(power_cap(cfg->region, false)) + " dBm)";
    };
    f.back().about = "How strongly the radio transmits, from -9 dBm up to the region's limit, and at most 22 dBm, "
                     "this radio's maximum. 0 uses the most allowed. Licensed operators may go past the region's "
                     "limit.";
    f.push_back(MZ_INT("Frequency slot", channel_num, 0, 0, "", ""));
    f.back().bounds    = [cfg](int64_t&, int64_t& hi) { hi = slot_count(cfg->region, lora_bw_khz(*cfg)); };
    f.back().help_text = [cfg]() {
        const uint32_t n = slot_count(cfg->region, lora_bw_khz(*cfg));
        return "0 = automatic, " + (n == 1 ? std::string("1") : "1-" + std::to_string(n));
    };
    f.back().about = "Which frequency in the region's band the radio uses. 0 picks it from the primary channel's "
                     "name, as other nodes do. Set a number only to match a mesh that uses one. How many there are "
                     "depends on the region and bandwidth.";
    f.push_back(MZ_BOOL("RX boosted gain", sx126x_rx_boosted_gain, ""));
    f.back().about = "Makes the receiver more sensitive, to hear weaker signals, for slightly more power use.";
    f.push_back(MZ_BOOL("Override duty cycle", override_duty_cycle, "May be illegal in your region"));
    f.back().about = "Some regions, like EU_868, let a radio transmit for only part of each hour. On ignores that "
                     "limit, which may be against the law where you are.";
    f.push_back(MZ_BOOL("OK to MQTT", config_ok_to_mqtt, "Let gateways uplink your packets"));
    f.back().about = "Lets internet gateways that hear your packets upload them to public MQTT servers. When off, "
                     "they don't.";
    f.push_back(MZ_BOOL("Ignore MQTT", ignore_mqtt, "Drop packets that arrived via MQTT"));
    f.back().about = "Ignores packets that crossed the internet through MQTT on their way here, so you only see "
                     "what travels by radio.";
    // These show what is in use, the preset's values included. Changing one
    // makes the modem Custom, starting from what was there.
    f.push_back(f_header("Modem values"));
    f.push_back(f_enum(
        "Bandwidth", [cfg]() { return static_cast<int64_t>(cfg->use_preset ? preset_info(cfg->modem_preset).bw : cfg->bandwidth); },
        [cfg, before](int64_t v) {
            make_custom(*cfg, *before);
            cfg->bandwidth = static_cast<decltype(cfg->bandwidth)>(v);
            fit_slot(*cfg);
        },
        {}, "Changing it makes the modem Custom"));
    f.back().opts_of = [cfg]() { return bandwidth_opts(*cfg); };
    f.back().about   = "How wide a slice of the band each message takes. Wider sends faster but reaches less far. "
                       "Every node must use the same. Only widths this radio takes and the region's band holds are "
                       "offered.";
    f.push_back(f_int(
        "Spreading factor",
        [cfg]() { return static_cast<int64_t>(cfg->use_preset ? preset_info(cfg->modem_preset).sf : cfg->spread_factor); },
        [cfg, before](int64_t v) {
            make_custom(*cfg, *before);
            cfg->spread_factor = static_cast<decltype(cfg->spread_factor)>(v);
        },
        5, 12, "", "Changing it makes the modem Custom. 5-12"));
    f.back().about = "How drawn out each message is. Higher reaches further but takes longer to send. Every node "
                     "must use the same.";
    f.push_back(f_int(
        "Coding rate",
        [cfg, before]() { return static_cast<int64_t>(cfg->use_preset ? preset_cr(*cfg, *before) : cfg->coding_rate); },
        [cfg, before](int64_t v) {
            make_custom(*cfg, *before);
            cfg->coding_rate = static_cast<decltype(cfg->coding_rate)>(v);
        },
        5, 8, "", ""));
    f.back().help_text = [cfg, before]() {
        const int own = preset_info(cfg->modem_preset).cr;
        if (cfg->use_preset && preset_cr(*cfg, *before) != own) {
            return "The radio uses 4/" + std::to_string(preset_cr(*cfg, *before)) + "; saving sets the preset's 4/" +
                   std::to_string(own);
        }
        return std::string("Changing it makes the modem Custom. 5-8 (4/5 to 4/8)");
    };
    f.back().about = "How much error correction each message carries. Higher copes better with interference but "
                     "takes longer to send.";
    // The region's band is what is suggested; outside it the user is warned
    // and may go ahead, as far as the radio can tune.
    f.push_back(f_text(
        "Fixed frequency (MHz)",
        [cfg]() { return cfg->override_frequency == 0 ? std::string() : mhz_text(cfg->override_frequency); },
        [cfg](const std::string& s) {
            double mhz = 0;
            if (!parse_mhz(s, mhz)) {
                return "A frequency from " + mhz_text(kRadioMinMHz) + " to " + mhz_text(kRadioMaxMHz) +
                       " MHz, e.g. 869.618";
            }
            cfg->override_frequency = static_cast<float>(mhz);
            return std::string();
        },
        12, "Empty = automatic"));
    f.back().empty_text = "automatic";
    f.back().about      = "Sends and listens on this exact frequency instead of the one the region and frequency "
                          "slot give. Every node must use the same. Outside the region's band you're warned first.";
    f.back().help_text  = [cfg]() {
        double low = 0, high = 0;
        if (!region_band(cfg->region, low, high)) return std::string("Empty = automatic");
        return "Empty = automatic. " + region_name(cfg->region) + " range: " + mhz_text(low) + "-" + mhz_text(high) +
               " MHz";
    };
    // Nothing to ask about a value refused anyway, automatic, in the band, or already set.
    f.back().warn_text = [cfg](const std::string& s) {
        double mhz = 0;
        if (!parse_mhz(s, mhz) || mhz == 0 || in_band(cfg->region, mhz) ||
            static_cast<float>(mhz) == cfg->override_frequency) {
            return std::string();
        }
        return out_of_band_warning(cfg->region, mhz);
    };
    return make_editor(
        app, "LoRa radio", std::move(f),
        [&app, cfg, before]() {
            meshtastic_Config c     = meshtastic_Config_init_zero;
            c.which_payload_variant = meshtastic_Config_lora_tag;
            c.payload_variant.lora  = lora_cleaned(*before, *cfg);
            app.mesh.begin_edit();
            app.mesh.set_config(c);
            if (const auto primary = public_channel_moved(app.mesh, *before, c.payload_variant.lora)) {
                app.mesh.set_channel(*primary);
            }
            app.mesh.commit_edit();
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_device(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_DeviceConfig>(app.mesh.config.device);
    std::vector<Field> f;
    // The radio firmware saves Repeater, and Router client before it, as Client.
    f.push_back(f_enum(
        "Role", [cfg]() { return static_cast<int64_t>(cfg->role); },
        [cfg](int64_t v) {
            cfg->role = static_cast<meshtastic_Config_DeviceConfig_Role>(v);
            // The radio saves None as All for these roles.
            if (router_role(cfg->role) && cfg->rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_NONE) {
                cfg->rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
            }
        },
        {}, ""));
    f.back().opts_of = [cfg]() {
        return with_current(
            {
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
            },
            cfg->role,
            cfg->role == meshtastic_Config_DeviceConfig_Role_REPEATER        ? "Repeater  saved as Client"
            : cfg->role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT ? "Router client  saved as Client"
                                                                             : role_name(cfg->role));
    };
    f.back().about = "How this node takes part in the mesh. Client suits most people; Client mute doesn't relay for "
                     "others. Router roles are for well-placed fixed nodes. A new role also resets some intervals "
                     "to suit it, and Router and Client hidden set Rebroadcast too.";
    f.push_back(f_enum(
        "Rebroadcast", [cfg]() { return static_cast<int64_t>(cfg->rebroadcast_mode); },
        [cfg](int64_t v) { cfg->rebroadcast_mode = static_cast<meshtastic_Config_DeviceConfig_RebroadcastMode>(v); },
        {}, ""));
    // All, skip decoding was for the Repeater role; it works as All now.
    f.back().opts_of = [cfg]() {
        std::vector<Opt> opts = {
            {meshtastic_Config_DeviceConfig_RebroadcastMode_ALL, "All"},
            {meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY, "Local only"},
            {meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY, "Known only"},
            {meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY, "Core portnums only"},
        };
        if (!router_role(cfg->role)) opts.push_back({meshtastic_Config_DeviceConfig_RebroadcastMode_NONE, "None"});
        const auto mode = cfg->rebroadcast_mode;
        return with_current(opts, mode,
                            mode == meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING ? "All, skip decoding  works as All"
                            : mode == meshtastic_Config_DeviceConfig_RebroadcastMode_NONE ? "None  saved as All for routers"
                                                                                           : std::to_string(mode));
    };
    f.back().about = "Which packets this node relays for others. All: everything it hears. Local only: only on its "
                     "own channels. Known only: also only from nodes it knows. None: nothing; routers can't use it.";
    f.push_back(MZ_INT("Node info interval", node_info_broadcast_secs, 3600, kMaxInterval, "s",
                       "Default 3 h, 1 h at least"));
    f.back().about = "How often this node announces its name and key to the mesh, so others can list it and "
                     "message it.";
    f.push_back(MZ_TEXT("Time zone (POSIX)", tzdef, "POSIX TZ string", false));
    f.back().about = "The time zone the radio uses for local times, as a POSIX rule such as "
                     "CET-1CEST,M3.5.0,M10.5.0/3. MeshZero's clock follows the system's time zone.";
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
    f.back().about = "Turns the GPS receiver on or off. Not present tells the radio there is none. Without GPS, set "
                     "a fixed position below to share where you are.";
    f.push_back(f_enum(
        "Shared accuracy", [primary]() { return static_cast<int64_t>(primary->settings.module_settings.position_precision); },
        [primary, primary_changed](int64_t v) {
            primary->settings.has_module_settings                = true;
            primary->settings.module_settings.position_precision = static_cast<uint32_t>(v);
            *primary_changed                                     = true;
        },
        precision_opts(primary->settings.module_settings.position_precision), "Primary channel. Others: Setup > Channels"));
    f.back().about = "How precisely your position is shared on the primary channel; Off shares none there. Other "
                     "channels have their own setting in Setup > Channels.";
    const Mesh* m        = &app.mesh;
    const auto on_public = [m, primary]() { return public_minimum(*m, MinimumFor::Position, primary.get()); };
    f.push_back(MZ_INT("Broadcast interval", position_broadcast_secs, 1, kMaxInterval, "s", ""));
    public_interval(
        f.back(), on_public, [m]() -> int64_t { return router_role(m->config.device.role) ? 43200 : 3600; },
        [m]() { return std::string(router_role(m->config.device.role) ? "12 h" : "1 h"); });
    f.back().about = "How often your position is sent to the mesh. On a public channel the firmware won't send it "
                     "more often than once an hour, or 12 hours for a router.";
    f.push_back(MZ_BOOL("Smart broadcast", position_broadcast_smart_enabled,
                        "Send sooner when moving"));
    f.back().about = "Also sends your position sooner when you move, as set by the two smart settings below.";
    f.push_back(MZ_INT("Smart min distance", broadcast_smart_minimum_distance, 0, kMaxInterval, "m",
                       "0 = default (100 m)"));
    f.back().about = "How far you must move before smart broadcast sends your position again.";
    f.push_back(MZ_INT("Smart min interval", broadcast_smart_minimum_interval_secs, 1, kMaxInterval, "s", ""));
    public_interval(
        f.back(), on_public, []() -> int64_t { return 300; }, []() { return std::string("5 min"); });
    f.back().about = "The least time between two smart broadcasts. On a public channel the firmware won't go below "
                     "5 minutes.";
    // The radio firmware takes its first fix one interval after it starts,
    // which, with running in the background off, is when MeshZero opens.
    f.push_back(MZ_INT("GPS update interval", gps_update_interval, 0, kMaxInterval, "s", ""));
    f.back().value_text = [](int64_t v) { return std::string(v >= kMaxInterval ? "at boot only" : ""); };
    f.back().help_text  = [m]() {
        return std::string("0 = default (") + (router_role(m->config.device.role) ? "24 h" : "2 min") +
               "). First fix after opening waits this long";
    };
    f.back().about = "How often the GPS takes a new fix. Shorter keeps your position fresher but uses more power.";
    f.push_back(f_header("Fixed position"));
    f.push_back(MZ_BOOL("Use fixed position", fixed_position, ""));
    f.back().about = "Uses the position you set below instead of the GPS, for a node that stays in one place.";
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
    f.back().about = "Type in this node's latitude, longitude and altitude. It takes effect right away, without "
                     "saving the page: Use fixed position turns on and the position goes out to the mesh.";
    f.push_back(f_action(
        "Clear fixed position",
        [a]() {
            a->mesh.remove_fixed_position();
            a->toast("Fixed position cleared");
        },
        ""));
    f.back().about = "Removes the fixed position and turns Use fixed position off, right away. Your position then "
                     "comes from the GPS, if it's on.";
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
    f.back().about = "Lets the radio sleep as much as it can to save power. The firmware advises against it on a node "
                     "used with an app, like this one.";
    return make_editor(
        app, "Power", std::move(f),
        [&app, cfg]() {
            meshtastic_Config c     = meshtastic_Config_init_zero;
            c.which_payload_variant = meshtastic_Config_power_tag;
            c.payload_variant.power = *cfg;
            // The radio can't tell on this hardware whether it has external
            // power, so it would take itself for on battery and stop, after
            // any time set here, again each time it is restarted.
            c.payload_variant.power.on_battery_shutdown_after_secs = 0;
            send_config(app.mesh, c);
        },
        kRestartNote);
}

std::unique_ptr<Screen> page_network(App& app)
{
    auto cfg = std::make_shared<meshtastic_Config_NetworkConfig>(app.mesh.config.network);
    std::vector<Field> f;
    f.push_back(f_info("Wi-Fi", []() { return std::string("managed by system"); }));
    f.back().about = "Wi-Fi is set up in the CardputerZero's system, not in MeshZero. The radio uses whatever network "
                     "the system is on.";
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
    f.back().about = "Shares mesh packets with other Meshtastic nodes on the same local network, so they reach each "
                     "other over the network as well as by radio.";
    // The radio firmware for Linux leaves out its Wi-Fi and Ethernet code,
    // which is what reads these two.
    f.push_back(MZ_TEXT("NTP server", ntp_server, "", false));
    f.back().about = "A time server for the radio's clock. MeshZero's radio takes the time from the system instead, "
                     "so this isn't used.";
    f.push_back(MZ_TEXT("Syslog server", rsyslog_server, "host:port", false));
    f.back().about = "A server on your network to send the radio's log to. MeshZero's radio doesn't support this, so "
                     "it isn't used.";
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
    f.back().about = "This node's public key. Other nodes use it to encrypt direct messages that only this node can "
                     "read.";
    f.push_back(f_action(
        "Show full public key",
        [&app, cfg]() {
            app.push(make_text_view(app, "Public key",
                                    cfg->public_key.size ? base64_encode(cfg->public_key.bytes, cfg->public_key.size)
                                                         : "No key yet"));
        },
        ""));
    f.back().about = "Shows the whole key, to compare it with the one another node has for you.";
    f.push_back(MZ_BOOL("Managed mode", is_managed, "Blocks local config changes"));
    f.back().about = "Hands this node to a remote admin. The radio then ignores all setting changes from MeshZero, "
                     "this one included. It needs an admin key, set from another app, to turn on.";
    f.push_back(MZ_BOOL("Legacy admin channel", admin_channel_enabled, "Insecure"));
    f.back().about = "Lets nodes on a channel named admin change this node's settings remotely. It's the old, "
                     "insecure way of doing that; leave it off unless you need it.";
    f.push_back(MZ_BOOL("Debug log over API", debug_log_api_enabled, ""));
    f.back().about = "Sends the radio's live debug log to the apps connected to it, for troubleshooting.";
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
    f.back().about = "Connects the radio to an MQTT server over the internet, to link channels with other meshes and "
                     "report to online maps. What each channel sends and takes in is set in Setup > Channels.";
    f.push_back(MZ_TEXT("Server", address, "Empty = mqtt.meshtastic.org", false));
    f.back().about = "The MQTT server to connect to, as a host name or host:port.";
    // With no server named, the radio firmware logs in to the default one with
    // its public login and leaves these two out.
    f.push_back(MZ_TEXT("Username", username, "Not used when Server is empty", false));
    f.back().about = "Your user name on the server set above. With no server set, the default server's public login "
                     "is used.";
    f.push_back(MZ_TEXT("Password", password, "Not used when Server is empty", true));
    f.back().about = "Your password on the server set above. With no server set, the default server's public login "
                     "is used.";
    f.push_back(MZ_BOOL("Encryption", encryption_enabled, ""));
    f.back().about = "Sends packets to the server still encrypted, as they travel on the mesh. Off sends them "
                     "decrypted, so your own tools can read them.";
    f.push_back(MZ_BOOL("JSON output", json_enabled, ""));
    f.back().about = "Also publishes packets as plain JSON on a topic of their own, for home automation and other "
                     "tools.";
    f.push_back(MZ_BOOL("TLS", tls_enabled, ""));
    f.back().about = "Connects to the server over an encrypted TLS connection. The server must support it.";
    f.push_back(MZ_TEXT("Root topic", root, "e.g. msh/EU_868", false));
    f.back().about = "Where on the server packets go; empty means msh. Nodes only see each other's packets through "
                     "the server when they use the same root topic.";
    f.push_back(f_header("Map reporting"));
    f.push_back(MZ_BOOL("Report to map", map_reporting_enabled, ""));
    f.back().about = "Publishes this node's name, role, radio settings and rough position for online Meshtastic "
                     "maps, once per report interval. Nothing is sent unless Share location is on too.";
    f.push_back(f_bool(
        "Share location", [cfg]() { return static_cast<int64_t>(cfg->map_report_settings.should_report_location); },
        [cfg](int64_t v) {
            cfg->has_map_report_settings                    = true;
            cfg->map_report_settings.should_report_location = v != 0;
        },
        "Makes your position public"));
    f.back().about = "Lets map reports include your position, rounded to within about 1.5 km. The firmware sends no "
                     "map report without it.";
    f.push_back(f_int(
        "Map report interval", [cfg]() { return static_cast<int64_t>(cfg->map_report_settings.publish_interval_secs); },
        [cfg](int64_t v) {
            cfg->has_map_report_settings                   = true;
            cfg->map_report_settings.publish_interval_secs = static_cast<uint32_t>(v);
        },
        3600, kMaxInterval, "s", "0 = default (1 h), or 1 h at least"));
    f.back().zero_ok = true;
    f.back().about   = "How often the map report is sent. The firmware won't send it more often than once an hour.";
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
    const Mesh* m          = &app.mesh;
    const auto on_public   = [m]() { return public_minimum(*m, MinimumFor::Telemetry); };
    const auto minimum     = [m]() -> int64_t { return router_role(m->config.device.role) ? 43200 : 1800; };
    const auto default_for = [m]() { return std::string(router_role(m->config.device.role) ? "12 h" : "1 h"); };
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Send device metrics", device_telemetry_enabled, ""));
    f.back().about = "Sends this node's battery level, uptime and how busy the radio channel is to the mesh, so "
                     "others can see how it's doing.";
    f.push_back(MZ_INT("Device metrics interval", device_update_interval, 1, kMaxInterval, "s", ""));
    public_interval(f.back(), on_public, minimum, default_for);
    f.back().about = "How often device metrics are sent. On a public channel the firmware won't go below 30 minutes, "
                     "or 12 hours for a router.";
    f.push_back(MZ_BOOL("Environment sensors", environment_measurement_enabled, ""));
    f.back().about = "Sends readings from a temperature, humidity or pressure sensor connected to the radio. Without "
                     "one there's nothing to send.";
    f.push_back(MZ_INT("Environment interval", environment_update_interval, 1, kMaxInterval, "s", ""));
    public_interval(f.back(), on_public, minimum, default_for);
    f.back().about = "How often environment readings are sent. On a public channel the firmware won't go below 30 "
                     "minutes, or 12 hours for a router.";
    f.push_back(MZ_BOOL("Power sensors", power_measurement_enabled, ""));
    f.back().about = "Sends readings from a voltage and current sensor connected to the radio. Without one there's "
                     "nothing to send.";
    f.push_back(MZ_INT("Power interval", power_update_interval, 1, kMaxInterval, "s", ""));
    public_interval(f.back(), on_public, minimum, default_for);
    f.back().about = "How often power readings are sent. On a public channel the firmware won't go below 30 minutes, "
                     "or 12 hours for a router.";
    f.push_back(MZ_BOOL("Air quality sensors", air_quality_enabled, ""));
    f.back().about = "Sends readings from an air quality (particle) sensor connected to the radio. Without one there's "
                     "nothing to send.";
    f.push_back(MZ_INT("Air quality interval", air_quality_interval, 1, kMaxInterval, "s", ""));
    public_interval(f.back(), on_public, minimum, default_for);
    f.back().about = "How often air quality readings are sent. On a public channel the firmware won't go below 30 "
                     "minutes, or 12 hours for a router.";
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
    // The radio firmware saves anything under 4 h, 0 included, as 6 h. The
    // page starts from 4 h instead, without counting as a change, so turning
    // the module on saves that.
    if (cfg->update_interval < 14400) cfg->update_interval = 14400;
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Enabled", enabled, ""));
    f.back().about = "Reports which nodes this node hears directly, and how well, for mesh mapping tools.";
    f.push_back(MZ_INT("Update interval", update_interval, 14400, kMaxInterval, "s", "4 h at least"));
    f.back().about = "How often the report is made. The firmware won't go below 4 hours.";
    f.push_back(MZ_BOOL("Send over LoRa", transmit_over_lora, ""));
    f.back().about = "Also sends the report over the radio to other nodes. Never on the default public channel: there "
                     "it only goes to apps connected to this node.";
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

// The radio firmware has no limit of its own on the messages it keeps, but
// sets room for all of them aside when it starts, and fails if it can't. So
// as many as half the system's memory holds, at a record's 272 bytes there.
int64_t max_records()
{
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long size  = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || size <= 0) return UINT32_MAX;
    return std::min<int64_t>(static_cast<int64_t>(pages) * size / 2 / 272, UINT32_MAX);
}

std::unique_ptr<Screen> page_store_forward(App& app)
{
    auto cfg = std::make_shared<meshtastic_ModuleConfig_StoreForwardConfig>(app.mesh.modules.store_forward);
    std::vector<Field> f;
    f.push_back(MZ_BOOL("Enabled", enabled, ""));
    f.back().about = "Lets nodes catch up on text messages they missed: a server keeps recent ones and resends them on "
                     "request. On, but not as a server, this node is a client.";
    f.push_back(MZ_BOOL("Act as server", is_server, ""));
    f.back().about = "Keeps the text messages this node hears and resends them to nodes that ask, though not on a "
                     "public channel. Best on a node that is always on.";
    f.push_back(MZ_BOOL("Heartbeat", heartbeat, ""));
    f.back().about = "Has the server announce itself every 15 minutes, so nodes nearby know it's there.";
    f.push_back(MZ_INT("Records kept", records, 0, max_records(), "", "0 = automatic"));
    f.back().about = "How many messages the server keeps; when full, it starts over from the oldest. 0 lets the "
                     "radio choose. At most, as many as half the system's memory holds.";
    f.push_back(MZ_INT("Max messages returned", history_return_max, 0, UINT32_MAX, "", "0 = default (25)"));
    f.back().about = "The most messages the server sends back to a node per request.";
    // The radio works the window out in seconds, in 32 bits.
    f.push_back(MZ_INT("History window", history_return_window, 0, UINT32_MAX / 60, "min", "0 = default (4 h)"));
    f.back().about = "How far back the server looks when a node asks for the messages it missed.";
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
    f.back().about = "Turns on range test: a sending node transmits numbered messages, and the numbers that arrive "
                     "show how far the radio reaches.";
    f.push_back(MZ_INT("Send interval", sender, 0, 28800, "s", "0 = receive only. Stops by itself after 8 h"));
    f.back().about = "How often this node sends a numbered test message. Sending stops by itself after 8 hours.";
    f.push_back(MZ_BOOL("Save results", save, ""));
    f.back().about = "Saves the test messages received to a file on the radio. Only ESP32 radios support this, so it "
                     "does nothing here.";
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

// How a channel is encrypted, as the radio firmware reads its key
// (Channels::getKey()). A secondary channel without a key uses the
// primary's; a short key is padded with zeros.
std::string psk_summary(const meshtastic_ChannelSettings& s, bool secondary)
{
    if (s.psk.size == 0) return secondary ? "primary's key" : "no encryption";
    if (s.psk.size == 1) {
        if (s.psk.bytes[0] == 0) return "no encryption";
        return s.psk.bytes[0] == kDefaultPskIndex ? "default key" : "simple key " + std::to_string(s.psk.bytes[0]);
    }
    return s.psk.size <= 16 ? "AES-128" : "AES-256";
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
    return modem_preset_name(preset) + " (runs as LongFast)";
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
        names += (s ? "\n" : "") + std::string(c.name[0] ? c.name : "(default name)") + "  -  " + psk_summary(c, s > 0);
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

// Asks for a channel link, and what to do with what it contains.
void add_from_link(App* a)
{
    a->push(make_input(*a, "Add channels", "", 400, "Enter a meshtastic.org/e/# link", [a](const std::string& text) {
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

// Opens the camera, and asks before adding what a channel code contains.
void scan_channel_code(App* a)
{
    a->push(make_scanner(*a, LV_SYMBOL_LEFT " Scan channel code", [a](const std::string& text) {
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
        f.back().about = "Turns this channel on. It shares the primary channel's frequency; its own name and key keep "
                         "it apart.";
    }
    f.push_back(f_text(
        "Name", [cfg]() { return std::string(cfg->settings.name); },
        [cfg](const std::string& s) {
            if (s.size() > 11) return std::string("Name: max 11 characters");
            snprintf(cfg->settings.name, sizeof(cfg->settings.name), "%s", s.c_str());
            return std::string();
        },
        11, "Empty = default channel"));
    f.back().about = index == 0 ? "Nodes need the same name and key to share a channel. The primary channel's name "
                                  "also picks the frequency slot. Empty, with the default key, it's the preset's "
                                  "public channel."
                                : "Nodes need the same name and key to share a channel.";
    f.push_back(f_info("Encryption", [cfg, index]() { return psk_summary(cfg->settings, index > 0); }));
    f.back().about = std::string("How this channel is encrypted, going by its key: not at all, with the public "
                                 "default key, or with a private AES-128 or AES-256 key.") +
                     (index > 0 ? " Without a key of its own it uses the primary channel's." : "");
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
    f.back().about = "The channel's encryption key. Everyone on the channel needs the same one; share it only with "
                     "people who should read the channel.";
    f.push_back(f_action(
        "Generate new key",
        [cfg, a]() {
            std::random_device rd;
            for (auto& b : cfg->settings.psk.bytes) b = static_cast<uint8_t>(rd());
            cfg->settings.psk.size = 32;
            a->toast("New key generated");
        },
        "", true));
    f.back().about = "Makes a new random private key, so only the nodes you share the channel with can read it.";
    f.push_back(f_action(
        "Use default key",
        [cfg]() {
            cfg->settings.psk.size     = 1;
            cfg->settings.psk.bytes[0] = kDefaultPskIndex;
        },
        "Not private", true));
    f.back().about = "Switches to the default key. It is public, so anyone on a channel with the same name can read "
                     "it.";
    f.push_back(f_enum(
        "Location accuracy", [cfg]() { return static_cast<int64_t>(cfg->settings.module_settings.position_precision); },
        [cfg](int64_t v) {
            cfg->settings.has_module_settings               = true;
            cfg->settings.module_settings.position_precision = static_cast<uint32_t>(v);
        },
        precision_opts(cfg->settings.module_settings.position_precision), ""));
    f.back().about = "How precisely your position is shared on this channel; Off shares none. Regular position "
                     "updates go on the first channel in the list that doesn't have it off.";
    // MeshZero itself still notifies; the flag is for the radio and other apps.
    f.push_back(f_bool(
        "Muted", [cfg]() { return static_cast<int64_t>(cfg->settings.module_settings.is_muted); },
        [cfg](int64_t v) {
            cfg->settings.has_module_settings      = true;
            cfg->settings.module_settings.is_muted = v != 0;
        },
        ""));
    f.back().about = "Marks the channel muted, for a busy channel you want to keep. Apps that honor it stop alerting "
                     "you to its messages; MeshZero still shows them.";
    f.push_back(f_bool(
        "MQTT uplink", [cfg]() { return static_cast<int64_t>(cfg->settings.uplink_enabled); },
        [cfg](int64_t v) { cfg->settings.uplink_enabled = v != 0; }, ""));
    f.back().about = "With MQTT on, this node sends the messages it hears on this channel to the MQTT server.";
    f.push_back(f_bool(
        "MQTT downlink", [cfg]() { return static_cast<int64_t>(cfg->settings.downlink_enabled); },
        [cfg](int64_t v) { cfg->settings.downlink_enabled = v != 0; }, ""));
    f.back().about = "With MQTT on, this node also sends this channel's messages from the MQTT server out over the "
                     "radio.";
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
    f.back().about = "Shows a QR code another node can scan to add this channel. It also carries your LoRa settings. "
                     "Save the channel first.";
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
    std::string hint() const override { return "Enter edit   ? explain   Esc back"; }

    void refresh() override
    {
        std::vector<ui::ListItem> items;
        for (int i = 0; i < kMaxChannels; ++i) {
            const meshtastic_Channel& c = mesh.channels[i];
            ui::ListItem it;
            it.prefix = std::to_string(i);
            if (mesh.channel_enabled(i)) {
                it.text  = ui::clean(mesh.channel_name(i), true) + (i == 0 ? "  (primary)" : "");
                it.right = psk_summary(c.settings, i > 0);
            } else {
                it.text      = "empty slot";
                it.has_color = true;
                it.color     = ui::c_dim();
            }
            items.push_back(it);
        }
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        const int i = list_->selected();
        if (key.is_char('?')) {
            app.push(make_text_view(
                app, "Channel " + std::to_string(i),
                i == 0 ? "The primary channel. Its name also picks the radio's frequency slot, unless one is fixed. "
                         "With no name and the default key, it's the preset's public channel."
                       : "A secondary channel: a name and key of its own, on the primary channel's frequency. Turn "
                         "on an empty slot to add one, or add channels from a link in Setup."));
            return true;
        }
        if (key.k != K::Enter) return false;
        if (i < kMaxChannels) app.push(page_channel(app, i));
        return true;
    }

private:
    std::unique_ptr<ui::ListView> list_;
};

// ---------------------------------------------------------- region picker

class RegionScreen : public Screen {
public:
    RegionScreen(App& a, bool first_run) : Screen(a), first_run_(first_run)
    {
        int y            = 0;
        const int region = mesh.config.lora.region;
        if (first_run) {
            const std::string why = region == meshtastic_Config_LoRaConfig_RegionCode_UNSET
                                        ? "Select your region to enable transmitting."
                                        : region_name(region) + " can't be used with MeshZero. Select your region.";
            lv_obj_t* l = ui::label(root, why, ui::c_warn(), ui::font_small());
            lv_obj_set_pos(l, 5, 2);
            lv_obj_set_width(l, kContentW - 10);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            y = 16;
        }
        list_ = std::make_unique<ui::ListView>(root, 0, y, kContentW, kContentH - y);
        list_->set_prefix_width(58);
        std::vector<ui::ListItem> items;
        int current = 0;
        for (const RegionInfo& r : kRegions) {
            if (!r.supported) continue;
            ui::ListItem it;
            it.prefix           = r.name;
            it.has_prefix_color = true;
            it.prefix_color     = ui::c_accent();
            it.text             = r.band;
            if (region == r.value) current = static_cast<int>(items.size());
            items.push_back(it);
            shown_.push_back(&r);
        }
        list_->set_items(items);
        list_->select(current);
    }
    std::string title() const override { return first_run_ ? "Select region" : LV_SYMBOL_LEFT " Region"; }
    std::string hint() const override { return "Up/Down choose   Enter select   ? explain   Esc later"; }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.is_char('?')) {
            app.push(make_text_view(app, "Region", kRegionAbout));
            return true;
        }
        if (key.k != K::Enter) return false;
        const RegionInfo& r = *shown_[static_cast<size_t>(list_->selected())];
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
    std::vector<const RegionInfo*> shown_;  // what each row stands for
};

// ------------------------------------------------------------ accent color

// Each colour is shown in itself. Pages take a new accent when they are next
// drawn, which for Setup is right away.
class AccentScreen : public Screen {
public:
    explicit AccentScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        std::vector<ui::ListItem> items;
        for (size_t i = 0; i < ui::accents().size(); ++i) {
            const ui::Accent& c = ui::accents()[i];
            ui::ListItem it;
            it.text      = c.name;
            it.has_color = true;
            it.color     = lv_color_hex(c.rgb);
            if (i == ui::accent_index()) {
                it.right           = LV_SYMBOL_OK;
                it.has_right_color = true;
                it.right_color     = it.color;
            }
            items.push_back(it);
        }
        list_->set_items(items);
        list_->select(static_cast<int>(ui::accent_index()));
    }
    std::string title() const override { return LV_SYMBOL_LEFT " Accent color"; }
    std::string hint() const override { return "Up/Down choose   Enter select   Esc back"; }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k != K::Enter) return false;
        ui::set_accent(static_cast<size_t>(list_->selected()));
        app.pop();
        return true;
    }

private:
    std::unique_ptr<ui::ListView> list_;
};

// -------------------------------------------------------------- setup tab

class SetupScreen : public Screen {
public:
    explicit SetupScreen(App& a) : Screen(a) { list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH); }
    int tab() const override { return kTabSetup; }
    std::string hint() const override { return "Enter open   ? explain   Tab next page   Hold Esc exit"; }

    void refresh() override
    {
        std::vector<ui::ListItem> items;
        entries_.clear();
        const auto header = [&](const std::string& text) {
            ui::ListItem it;
            it.text   = text;
            it.header = true;
            items.push_back(it);
            entries_.push_back({});
        };
        const auto entry = [&](const std::string& text, const std::string& right, const std::string& about,
                               std::function<void()> fn) {
            ui::ListItem it;
            it.text  = text;
            it.right = ui::clean(right, true);
            items.push_back(it);
            entries_.push_back({text, about, std::move(fn)});
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
        const auto when_ready = [a](std::function<void()> fn) {
            return [a, fn]() {
                if (!a->mesh.ever_ready()) {
                    a->toast("Radio offline", true);
                    return;
                }
                fn();
            };
        };

        header("Radio");
        entry("Region", region_name(mesh.config.lora.region), kRegionAbout,
              [a]() { a->push(make_region_picker(*a, false)); });
        entry("LoRa radio", mesh.preset_name() + (mesh.config.lora.override_frequency != 0 ? ", fixed freq" : ""),
              "The radio's settings: modem preset, hop limit, power, frequency and more. The preset and frequency "
              "must match the mesh you want to join.",
              open(page_lora));
        int enabled = 0;
        for (int i = 0; i < kMaxChannels; ++i) enabled += mesh.channel_enabled(i) ? 1 : 0;
        entry("Channels", std::to_string(enabled) + " of 8",
              "Your eight channel slots, each with a name and key of its own. The first is the primary channel; the "
              "others share its frequency.",
              [a]() { a->push(make_channels(*a)); });
        entry("Share channels (QR code)", "",
              "Shows a QR code with all your channels and LoRa settings, so another node can copy your setup by "
              "scanning it.",
              when_ready([a]() { a->push(make_qr(*a, "Share channels", channel_url(a->mesh, -1))); }));
        entry("Add channels from URL", "",
              "Type in a meshtastic.org/e/# link to add its channels, or to switch to the setup it carries. You're "
              "asked before anything changes.",
              when_ready([a]() { add_from_link(a); }));
        // Only on models that have a camera.
        if (Camera::available()) {
            entry("Scan channel QR code", "",
                  "Scans a channel QR code with the camera to add its channels, or to switch to its setup. You're "
                  "asked before anything changes.",
                  when_ready([a]() { scan_channel_code(a); }));
        }
        entry("Community setups", "experimental",
              "Setups that local communities use instead of an official preset. Picking one shows what it holds and "
              "asks before anything changes.",
              when_ready([a]() { pick_community_setup(a); }));
        header("This node");
        entry("Your node", mesh.long_name(mesh.my_num),
              "Your node's names, and whether you're a licensed operator or take direct messages.", open(page_user));
        entry("Device", role_name(mesh.config.device.role),
              "The node's role on the mesh, which packets it relays, how often it announces itself and its time "
              "zone.",
              open(page_device));
        entry("Position & GPS",
              mesh.config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED ? "GPS on" : "GPS off",
              "The GPS, how precisely and how often your position is shared, and a fixed position for a node without "
              "GPS.",
              open(page_position));
        entry("Power", "", "Power saving for the radio.", open(page_power));
        entry("Network", "",
              "Mesh over the local network, and the radio's time and log servers. Wi-Fi is set up in the system.",
              open(page_network));
        entry("Security", "", "This node's public key, remote admin options and debug logging.", open(page_security));
        entry("Run in background", mesh.background == 1 ? "on" : "off",
              "Keeps the radio on when MeshZero is closed, to receive messages and relay for the mesh. While it's "
              "on, other LoRa apps can't use the cap.",
              [a, m]() {
                  if (m->background < 0) {
                      a->toast("Radio offline", true);
                      return;
                  }
                  a->push(make_confirm(*a, "Run in background",
                                       "Keep the radio on when MeshZero is closed, to receive messages and relay for "
                                       "the mesh. While it is on, other LoRa apps can't use the cap.",
                                       {"Off", "On"}, [m](int c) { m->set_background(c == 1); }));
              });
        entry("Network Access (LAN)", mesh.network_access == 1 ? "on" : "off",
              "Lets other devices on your local network use this radio through port 4403, for MeshMonitor or a phone "
              "app. There's no password, so use it only on networks you trust.",
              [a, m]() {
                  if (m->network_access < 0) {
                      a->toast("Radio offline", true);
                      return;
                  }
                  a->push(make_confirm(*a, "Network Access (LAN)",
                                       "Let other devices on your network use this radio, for MeshMonitor or a phone "
                                       "app. There is no password.",
                                       {"Off", "On"}, [m](int c) { m->set_network_access(c == 1); }));
              });
        header("Modules");
        entry("MQTT", mesh.modules.mqtt.enabled ? "on" : "off",
              "Connects the radio to an MQTT server over the internet, to link channels with other meshes and report "
              "to online maps.",
              open(page_mqtt));
        entry("Telemetry", "",
              "Sends battery, uptime and readings from attached sensors to the mesh, and sets how often.",
              open(page_telemetry));
        entry("Neighbor info", mesh.modules.neighbor_info.enabled ? "on" : "off",
              "Reports which nodes this node hears directly, for mesh mapping tools.", open(page_neighbor));
        entry("Store & forward", mesh.modules.store_forward.enabled ? "on" : "off",
              "Lets nodes catch up on text messages they missed: a server keeps recent ones and resends them on "
              "request.",
              open(page_store_forward));
        entry("Range test", mesh.modules.range_test.enabled ? "on" : "off",
              "Sends numbered test messages to find out how far your radio reaches.", open(page_range_test));
        header("Appearance");
        entry("Accent color", ui::accents()[ui::accent_index()].name, "",
              [a]() { a->push(std::make_unique<AccentScreen>(*a)); });
        header("Maintenance");
        // Plain actions like this one need no explanation; ? does nothing on them.
        entry("Restart radio", "", "",
              [a, m]() {
                  a->push(make_confirm(*a, "Restart radio", "Restart the radio?",
                                       {"Cancel", "Restart"}, [a, m](int c) {
                                           if (c == 1) {
                                               m->reboot();
                                               a->toast("Radio restarting");
                                           }
                                       }));
              });
        entry("Delete all messages", "", "",
              [a, m]() {
                  a->push(make_confirm(*a, "Delete all messages",
                                       "Delete the messages of every channel and direct chat? This can't be undone.",
                                       {"Cancel", "Delete"}, [a, m](int c) {
                                           if (c != 1) return;
                                           m->delete_all_history();
                                           a->toast("All messages deleted");
                                       }));
              });
        // The radio keeps favorites when MeshZero asks it to clear its nodes.
        entry("Reset node list", "",
              "Clears the nodes the radio knows, except favorites, and restarts it. Nodes come back as they're heard "
              "again.",
              [a, m]() {
                  a->push(make_confirm(*a, "Reset node list", "Clear the node list?",
                                       {"Cancel", "Clear"}, [a, m](int c) {
                                           if (c == 1) {
                                               m->nodedb_reset();
                                               a->toast("Node list cleared");
                                           }
                                       }));
              });
        entry("Factory reset", "",
              "Erases the radio's settings, channels and node list. Reset settings keeps this node's keys; Reset "
              "everything erases them too.",
              [a, m]() {
                  a->push(make_confirm(*a, "Factory reset",
                                       "Erase settings and channels? Everything also erases this node's keys.",
                                       {"Cancel", "Reset settings", "Reset everything"}, [a, m](int c) {
                                           if (c >= 1) {
                                               m->factory_reset(c == 2);
                                               a->toast("Factory reset. Radio restarting.");
                                           }
                                       }));
              });
        const std::string newer = update::available();
        entry("Updates",
              !newer.empty() ? newer + " available" : !update::enabled() ? "off" : update::checked() ? "up to date" : "",
              "Shows your MeshZero version. When it opens, MeshZero checks GitHub for a newer release, at most once "
              "an hour, and tells you. It never installs anything; the check can be turned off.",
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
        entry("Help", "", "",
              [a]() { a->push(make_help(*a)); });
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        const size_t i = static_cast<size_t>(list_->selected());
        if (key.is_char('?')) {
            if (i < entries_.size() && !entries_[i].about.empty()) {
                app.push(make_text_view(app, entries_[i].label, entries_[i].about));
            }
            return true;
        }
        if (key.k == K::Enter) {
            if (i < entries_.size() && entries_[i].open) {
                auto fn = entries_[i].open;
                fn();
            }
            return true;
        }
        return false;
    }

private:
    // A row of the list: what ? says about it and what Enter does. Headers have neither.
    struct Entry {
        std::string label;
        std::string about;
        std::function<void()> open;
    };

    std::unique_ptr<ui::ListView> list_;
    std::vector<Entry> entries_;
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

bool region_supported(int region)
{
    for (const auto& r : kRegions) {
        if (r.value == region) return r.supported;
    }
    return false;
}

bool repair_coding_rate(App& app)
{
    // A preset with a coding rate of 5-8 stored next to it that isn't its own
    // runs on that rate (RadioInterface::applyModemConfig()). That is almost
    // always the radio having saved a 0 sent with the preset as 5, as from
    // MeshZero 0.2.0 or another app: the official apps don't show the coding
    // rate while a preset is in use.
    const meshtastic_Config_LoRaConfig& l = app.mesh.config.lora;
    if (!app.mesh.config.has_lora || !l.use_preset || !known_preset(l.modem_preset)) return false;
    const PresetInfo& p = preset_info(l.modem_preset);
    if (l.coding_rate < 5 || l.coding_rate > 8 || l.coding_rate == p.cr) return false;
    meshtastic_Config c                = meshtastic_Config_init_zero;
    c.which_payload_variant            = meshtastic_Config_lora_tag;
    c.payload_variant.lora             = l;
    c.payload_variant.lora.coding_rate = static_cast<uint8_t>(p.cr);
    send_config(app.mesh, c);
    app.toast("Coding rate set back to 4/" + std::to_string(p.cr) + " for " + p.name + ". Radio restarting.");
    return true;
}

}  // namespace mz

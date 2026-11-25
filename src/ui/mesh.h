// SPDX-License-Identifier: GPL-3.0-only
// Client-side model of the mesh: talks the stream API over TCP and keeps the
// node database, channels, configuration and conversations the UI renders.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "common/history.h"
#include "common/proto.h"

namespace mz {

constexpr int kMaxChannels   = 8;
constexpr size_t kMaxTextLen = 200;

struct ChatKey {
    bool dm     = false;
    uint32_t id = 0;  // channel index, or peer node number for direct messages
    bool operator<(const ChatKey& o) const { return dm != o.dm ? dm < o.dm : id < o.id; }
    bool operator==(const ChatKey& o) const { return dm == o.dm && id == o.id; }
    bool operator!=(const ChatKey& o) const { return !(*this == o); }
};

struct Chat {
    ChatKey key;
    std::vector<StoredMsg> msgs;
    int unread         = 0;
    uint32_t last_ts   = 0;
    uint32_t last_read = 0;
};

struct PacketLogEntry {
    uint32_t ts   = 0;
    uint32_t from = 0;
    uint32_t to   = 0;
    int portnum   = 0;
    float snr     = 0;
    int rssi      = 0;
    int hops      = -1;
    bool outgoing = false;
    std::string summary;
};

struct TraceResult {
    uint32_t dest = 0;
    std::vector<uint32_t> towards;  // intermediate hops on the way out
    std::vector<int> snr_towards;   // in quarter dB, one more entry than hops when complete
    std::vector<uint32_t> back;
    std::vector<int> snr_back;
};

struct UiEvent {
    enum Type { NewMessage, Toast, Trace, LinkChanged } type = Toast;
    ChatKey chat;
    std::string text;
    TraceResult trace;
    // NewMessage: who sent it, the message alone, and whether it is a reaction.
    uint32_t from = 0;
    std::string body;
    bool tapback = false;
};

enum class LinkState { Disconnected, Connecting, Syncing, Ready };

#if defined(MESHCARD_ESP32)
// Cardputer ADV: the node list is the firmware's own (a small fixed table it keeps for the
// screens, see LocalUiAPI.cpp), read one node at a time. Nothing is copied into a map here.
// Same use as a std::map: size(), and range-for giving {num, node} pairs.
class NodeTable {
public:
    using value_type = std::pair<uint32_t, meshtastic_NodeInfo>;
    class iterator {
    public:
        iterator(size_t i) : i_(i) {}
        value_type operator*() const;
        iterator& operator++()
        {
            ++i_;
            return *this;
        }
        bool operator!=(const iterator& o) const { return i_ != o.i_; }

    private:
        size_t i_;
    };
    size_t size() const;
    bool empty() const { return size() == 0; }
    iterator begin() const { return iterator(0); }
    iterator end() const { return iterator(size()); }
};
#endif

class Mesh {
public:
    Mesh();
    ~Mesh();

    void configure(const std::string& host, uint16_t port);
    void poll();
    LinkState state() const { return state_; }
    bool ready() const { return state_ == LinkState::Ready; }
    // True between the first successful sync and now, even across reconnects.
    bool ever_ready() const { return ever_ready_; }
    // Why the radio is down, as reported by the service: "nocap", "starting",
    // "stopped", or empty when unknown.
    std::string radio_note();
    bool cap_missing() { return radio_note() == "nocap"; }
    // Short text for the state above.
    std::string offline_text();
    void resync();

    // ---- state -------------------------------------------------------
    uint32_t my_num = 0;
    meshtastic_MyNodeInfo my_info{};
    meshtastic_DeviceMetadata metadata{};
    bool has_metadata = false;
#if defined(MESHCARD_ESP32)
    NodeTable nodes;
#else
    std::map<uint32_t, meshtastic_NodeInfo> nodes;
#endif
    meshtastic_Channel channels[kMaxChannels]{};
    meshtastic_LocalConfig config{};
    meshtastic_LocalModuleConfig modules{};
    meshtastic_LocalStats local_stats{};
    bool has_local_stats = false;
    std::map<ChatKey, Chat> chats;
    std::deque<PacketLogEntry> packet_log;
    std::deque<UiEvent> events;

    // Bumped whenever the matching part of the state changes.
    uint32_t rev_nodes  = 1;
    uint32_t rev_chats  = 1;
    uint32_t rev_config = 1;
    uint32_t rev_link   = 1;
    uint32_t rev_log    = 1;

    // ---- lookups -----------------------------------------------------
    const meshtastic_NodeInfo* node(uint32_t num) const;
    const meshtastic_NodeInfo* me() const { return node(my_num); }
    std::string short_name(uint32_t num) const;
    std::string long_name(uint32_t num) const;
    std::string channel_name(int index) const;
    std::string chat_title(const ChatKey& key) const;
    std::string preset_name() const;
    bool channel_enabled(int index) const;
    int total_unread() const;
    bool region_unset() const;
    bool my_position(double& lat, double& lon) const;
    static bool node_position(const meshtastic_NodeInfo& n, double& lat, double& lon);

    // ---- conversations ----------------------------------------------
    Chat& chat(const ChatKey& key);
    void set_active_chat(const ChatKey* key);
    void mark_read(const ChatKey& key);
    bool send_text(const ChatKey& key, const std::string& text, uint32_t reply_id = 0);
    // Reacts to the message with this packet id with an emoji.
    bool send_tapback(const ChatKey& key, uint32_t target_id, const std::string& emoji);
    // Archive: hides a chat's messages up to now (kept in the history file, nothing deleted);
    // unarchive shows them again.
    void archive_chat(const ChatKey& key);
    void unarchive_chat(const ChatKey& key);
    bool chat_archived(const ChatKey& key) const { return archived_.count(key) != 0; }

    // ---- requests ----------------------------------------------------
    void traceroute(uint32_t dest);
    void request_position(uint32_t dest);
    void request_nodeinfo(uint32_t dest);
    bool has_key(uint32_t num) const;
    bool awaiting_key(uint32_t num) const;
    void ensure_key(uint32_t dest);
    void request_telemetry(uint32_t dest);

    // ---- administration (local node) ---------------------------------
    void admin(meshtastic_AdminMessage& msg, bool want_response = false);
    void begin_edit();
    void commit_edit();
    void set_owner(const meshtastic_User& user);
    void set_config(const meshtastic_Config& cfg);
    void set_module_config(const meshtastic_ModuleConfig& cfg);
    void set_channel(const meshtastic_Channel& channel);
    void set_favorite(uint32_t num, bool favorite);
    void set_ignored(uint32_t num, bool ignored);
    void remove_node(uint32_t num);
    void set_fixed_position(double lat, double lon, int alt);
    void remove_fixed_position();
    void reboot(int seconds = 2);
    void shutdown_radio(int seconds = 2);
    void factory_reset(bool full);
    void nodedb_reset();

    // ---- service ----------------------------------------------------
    // Whether other devices on the network may use the API: -1 unknown, 0, 1.
    int network_access = -1;
    void set_network_access(bool on);
    // Whether the radio keeps running with the app closed: -1 unknown, 0, 1.
    int background = -1;
    void set_background(bool on);

    void toast(const std::string& text);

private:
    void disconnect(const char* why);
    void start_connect();
    void send_to_radio(const meshtastic_ToRadio& msg);
    void request_config();
    void send_control(const std::string& request);
    uint32_t send_packet(uint32_t to, uint8_t channel, meshtastic_PortNum port, const uint8_t* payload, size_t len,
                         bool want_ack, bool want_response);
    bool send_message(const ChatKey& key, const std::string& text, uint32_t reply_id, bool tapback);
    void on_frame(const std::string& payload);
    void on_packet(const meshtastic_MeshPacket& p);
    void on_text(const meshtastic_MeshPacket& p);
    void on_routing(const meshtastic_MeshPacket& p);
    void add_message(const ChatKey& key, StoredMsg msg, bool live);
    void log_packet(const meshtastic_MeshPacket& p, bool outgoing);
    void ensure_channel_chats();
    void load_history(const ChatKey* only = nullptr);
    void load_read_state();
    void save_read_state();
    void set_state(LinkState s);
    uint32_t next_id();

    std::string host_ = "127.0.0.1";
    uint16_t port_    = kDefaultApiPort;
    int fd_           = -1;
    LinkState state_  = LinkState::Disconnected;
    bool ever_ready_  = false;
    FrameParser parser_;
    std::string out_;
    uint64_t next_connect_ms_ = 0;
    uint64_t sync_start_ms_   = 0;
    uint64_t last_rx_ms_      = 0;
    uint64_t last_hb_ms_      = 0;
    uint32_t nonce_           = 0;
    uint32_t id_seq_          = 0;
    bool history_loaded_      = false;
    std::map<ChatKey, uint32_t> archived_;  // chat -> its messages up to this time are hidden
    void save_archived();
    std::string radio_note_;
    uint64_t cap_checked_ms_  = 0;
    bool has_active_          = false;
    ChatKey active_;
    std::map<uint32_t, ChatKey> pending_acks_;  // packet id -> conversation
    std::map<uint32_t, uint32_t> trace_ids_;    // request id -> destination
    struct KeyAsk {
        uint64_t at = 0;  // when it was last asked for
        int tries   = 0;
    };
    std::map<uint32_t, KeyAsk> key_asks_;  // node -> requests for its public key
#if defined(MESHCARD_ESP32)
    void refresh_settings();
    void trim_all_chats();
    uint64_t settings_at_ms_ = 0;
    uint32_t fw_nodes_rev_   = 0;
    // node() hands out pointers: a few recent lookups stay valid here.
    static constexpr int kNodeSlots = 4;
    mutable meshtastic_NodeInfo slot_[kNodeSlots]{};
    mutable uint32_t slot_used_[kNodeSlots]{};
    mutable uint32_t slot_tick_ = 0;
#endif
};

const char* portnum_name(int portnum);
const char* routing_error_name(int error);
// Why a message was not delivered, in plain words.
std::string delivery_error_text(int error);
const char* hw_model_name(int model);
const char* role_name(int role);

}  // namespace mz

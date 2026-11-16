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
};

enum class LinkState { Disconnected, Connecting, Syncing, Ready };

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
    void resync();

    // ---- state -------------------------------------------------------
    uint32_t my_num = 0;
    meshtastic_MyNodeInfo my_info{};
    meshtastic_DeviceMetadata metadata{};
    bool has_metadata = false;
    std::map<uint32_t, meshtastic_NodeInfo> nodes;
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

    // ---- requests ----------------------------------------------------
    void traceroute(uint32_t dest);
    void request_position(uint32_t dest);
    void request_nodeinfo(uint32_t dest);
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

    void toast(const std::string& text);

private:
    void disconnect(const char* why);
    void start_connect();
    void send_to_radio(const meshtastic_ToRadio& msg);
    void request_config();
    uint32_t send_packet(uint32_t to, uint8_t channel, meshtastic_PortNum port, const uint8_t* payload, size_t len,
                         bool want_ack, bool want_response);
    void on_frame(const std::string& payload);
    void on_packet(const meshtastic_MeshPacket& p);
    void on_text(const meshtastic_MeshPacket& p);
    void on_routing(const meshtastic_MeshPacket& p);
    void add_message(const ChatKey& key, StoredMsg msg, bool live);
    void log_packet(const meshtastic_MeshPacket& p, bool outgoing);
    void ensure_channel_chats();
    void load_history();
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
    bool has_active_          = false;
    ChatKey active_;
    std::map<uint32_t, ChatKey> pending_acks_;  // packet id -> conversation
    std::map<uint32_t, uint32_t> trace_ids_;    // request id -> destination
};

const char* portnum_name(int portnum);
const char* routing_error_name(int error);
// Why a message was not delivered, in plain words.
std::string delivery_error_text(int error);
const char* hw_model_name(int model);
const char* role_name(int role);

}  // namespace mz

// SPDX-License-Identifier: GPL-3.0-only
// Text message log shared between the daemon (writer) and the UI (reader).
// One tab-separated record per line so the file survives partial writes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mz {

enum class AckState : uint8_t {
    None     = 0,  // incoming message, nothing to track
    Pending  = 1,
    Acked    = 2,  // acknowledged by the destination
    Implicit = 3,  // relayed by another node (broadcasts) or acked on the destination's behalf
    Failed   = 4,
};

struct StoredMsg {
    uint32_t ts       = 0;
    uint32_t id       = 0;
    uint32_t from     = 0;
    uint32_t to       = 0;
    uint8_t channel   = 0;
    bool outgoing     = false;
    bool pki          = false;
    bool emoji        = false;
    uint32_t reply_id = 0;
    float snr         = 0;
    int rssi          = 0;
    int hops          = -1;
    AckState ack      = AckState::None;
    int ack_error     = 0;
    std::string text;
};

std::string history_dir();
std::string history_path();

class HistoryWriter {
public:
    explicit HistoryWriter(std::string path = history_path(), size_t max_bytes = 2 * 1024 * 1024);
    void append(const StoredMsg& msg);
    void append_ack(uint32_t id, AckState state, int error);

private:
    void write_line(const std::string& line);
    std::string path_;
    size_t max_bytes_;
};

// Loads the rotated file followed by the current one, applying ack records.
std::vector<StoredMsg> history_load(const std::string& path = history_path());

}  // namespace mz

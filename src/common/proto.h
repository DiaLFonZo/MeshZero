// SPDX-License-Identifier: GPL-3.0-only
// Protobuf helpers and the client-API stream framing (0x94 0xC3 + 16-bit length).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "pb_decode.h"
#include "pb_encode.h"

#include "meshtastic/admin.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/module_config.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/telemetry.pb.h"

namespace mz {

constexpr uint8_t kStart1          = 0x94;
constexpr uint8_t kStart2          = 0xC3;
constexpr size_t kMaxFramePayload  = 512;
constexpr uint16_t kDefaultApiPort = 4403;

template <class T>
bool pb_decode_msg(const pb_msgdesc_t* fields, const uint8_t* data, size_t len, T& out)
{
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    return pb_decode(&stream, fields, &out);
}

template <class T>
bool pb_decode_msg(const pb_msgdesc_t* fields, const std::string& bytes, T& out)
{
    return pb_decode_msg(fields, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out);
}

template <class T>
std::string pb_encode_msg(const pb_msgdesc_t* fields, const T& msg)
{
    uint8_t buffer[kMaxFramePayload];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, fields, &msg)) return {};
    return std::string(reinterpret_cast<const char*>(buffer), stream.bytes_written);
}

// Encodes a message into a nanopb bytes field ({size, bytes[]}).
template <class B, class T>
bool pb_encode_into(B& dest, const pb_msgdesc_t* fields, const T& msg)
{
    pb_ostream_t stream = pb_ostream_from_buffer(dest.bytes, sizeof(dest.bytes));
    if (!pb_encode(&stream, fields, &msg)) return false;
    dest.size = static_cast<decltype(dest.size)>(stream.bytes_written);
    return true;
}

// Wraps one encoded ToRadio/FromRadio payload in the stream header.
std::string frame_wrap(const std::string& payload);

// Incremental stream parser. Bytes outside a frame (debug console text) are dropped.
class FrameParser {
public:
    using Callback = std::function<void(const std::string& payload)>;
    void feed(const uint8_t* data, size_t len, const Callback& on_frame);
    void reset();

private:
    enum class State { Start1, Start2, LenHi, LenLo, Payload };
    State state_   = State::Start1;
    size_t length_ = 0;
    std::string payload_;
};

}  // namespace mz

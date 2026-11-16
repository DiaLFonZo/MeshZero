// SPDX-License-Identifier: GPL-3.0-only
#include "common/proto.h"

namespace mz {

std::string frame_wrap(const std::string& payload)
{
    std::string out;
    out.reserve(payload.size() + 4);
    out.push_back(static_cast<char>(kStart1));
    out.push_back(static_cast<char>(kStart2));
    out.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
    out.push_back(static_cast<char>(payload.size() & 0xff));
    out += payload;
    return out;
}

void FrameParser::reset()
{
    state_  = State::Start1;
    length_ = 0;
    payload_.clear();
}

void FrameParser::feed(const uint8_t* data, size_t len, const Callback& on_frame)
{
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        switch (state_) {
            case State::Start1:
                if (b == kStart1) state_ = State::Start2;
                break;
            case State::Start2:
                state_ = b == kStart2 ? State::LenHi : (b == kStart1 ? State::Start2 : State::Start1);
                break;
            case State::LenHi:
                length_ = static_cast<size_t>(b) << 8;
                state_  = State::LenLo;
                break;
            case State::LenLo:
                length_ |= b;
                payload_.clear();
                if (length_ > kMaxFramePayload) {
                    state_ = State::Start1;
                } else if (length_ == 0) {
                    state_ = State::Start1;
                    on_frame(payload_);
                } else {
                    state_ = State::Payload;
                }
                break;
            case State::Payload:
                payload_.push_back(static_cast<char>(b));
                if (payload_.size() == length_) {
                    state_ = State::Start1;
                    on_frame(payload_);
                }
                break;
        }
    }
}

}  // namespace mz

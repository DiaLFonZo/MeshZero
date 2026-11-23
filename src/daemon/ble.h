// SPDX-License-Identifier: GPL-3.0-only
// The Meshtastic Bluetooth service, offered through the system's Bluetooth
// daemon (BlueZ): the phone apps find and use this device the way they do a
// radio with Bluetooth of its own.
//
// A phone writes one ToRadio message at a time and reads FromRadio messages
// one at a time until it gets an empty one; a counter it is notified of tells
// it when there is more to read.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mz {

class Ble {
public:
    struct Handlers {
        // A message from the phone. `device` names the phone for as long as it is connected.
        std::function<void(const std::string& device, const std::string& payload)> on_write;
        // The next message for the phone, empty when there is none.
        std::function<std::string(const std::string& device)> on_read;
        std::function<void(const std::string& device)> on_disconnect;
        // The six digits to type on the phone while it pairs; empty once that is over.
        std::function<void(const std::string& passkey)> on_passkey;
    };

    Ble();
    ~Ble();
    Ble(const Ble&)            = delete;
    Ble& operator=(const Ble&) = delete;

    // Whether this build can do Bluetooth at all.
    static bool supported();

    // Starts offering the service under this name. With `pin`, a phone has to
    // pair with a code shown here before it can use it. False when the
    // system's Bluetooth cannot be reached; why() then says more.
    bool start(const std::string& name, bool pin, Handlers handlers);
    void stop();
    bool running() const;
    // Offered and accepted by the Bluetooth daemon: phones can find it.
    bool advertising() const;
    const std::string& why() const;

    // For the caller's poll loop: the descriptor to wait on (-1 when stopped),
    // what to wait for, how long at most, and the work to do afterwards.
    int fd() const;
    short events() const;
    int timeout_ms() const;
    void process();

    // Tells the phones there is something new to read.
    void notify();
    // Ends a phone's connection, as a radio restarting would.
    void disconnect(const std::string& device);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mz

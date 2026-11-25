// SPDX-License-Identifier: GPL-3.0-only
// Battery level and time left, for the top bar.
#pragma once

#include <string>

namespace mz {

struct BatteryInfo {
    bool known     = false;  // no battery found: show nothing
    int percent    = 0;
    bool charging  = false;
    int minutes_left = -1;   // -1: no estimate yet
};

// Read at most every few seconds; cheap to call every frame.
BatteryInfo battery_info();

// Top bar text: "<icon>83% ↓3h20m", "<plug>83%" while charging, "" when unknown.
std::string battery_text(const BatteryInfo& b);

}  // namespace mz

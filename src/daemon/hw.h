// SPDX-License-Identifier: GPL-3.0-only
// Power and antenna-switch control for the Cap LoRa-1262 on a CardputerZero.
#pragma once

namespace mz {

// Turns the cap's 5 V rail on/off and drives the RF switch enable (P0 of the
// PI4IOE5V6408 expander). Returns false when a required step failed. If a switch
// not detected, we don't fail because that could be the legacy cap module.
bool hw_power(bool on, bool has_switch = true);

// Whether a LoRa cap is attached. The current model is known by its IO
// expander answering. The older model has none, so the radio chip itself is
// asked over SPI, which is only done when radio_running is false: the radio
// daemon must not be talked over. Unknown means it could not be checked (no
// access to the bus or the rail, or MESHZERO_CAP_DETECT=0), and callers then
// carry on as if it were there.
enum class Cap { Present, Legacy, Absent, Unknown };
Cap hw_cap_probe(bool patient, bool radio_running = true);
// The same for a command line, which does not know whether the service has
// the radio running: an open radio port settles it, and while the service is
// starting the radio this waits instead of using SPI.
Cap hw_cap_check(int radio_port);
const char* hw_cap_name(Cap cap);

// Prints the current rail and expander state. Returns a process exit code.
int hw_status();

// Checks devices, services and ports. Returns a process exit code.
int hw_doctor(int radio_port, int api_port);

}  // namespace mz

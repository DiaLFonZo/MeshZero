// SPDX-License-Identifier: GPL-3.0-only
// Power and antenna-switch control for the Cap LoRa-1262 on a CardputerZero.
#pragma once

namespace mz {

// Turns the cap's 5 V rail on/off and drives the RF switch enable (P0 of the
// PI4IOE5V6408 expander). Returns false when a required step failed.
bool hw_power(bool on);

// Whether a LoRa cap is attached, judged by its IO expander answering.
// Unknown means it could not be checked (no access to the bus or the rail, or
// MESHZERO_CAP_DETECT=0), and callers then carry on as if it were there.
enum class Cap { Present, Absent, Unknown };
Cap hw_cap_probe(bool patient);
const char* hw_cap_name(Cap cap);

// Prints the current rail and expander state. Returns a process exit code.
int hw_status();

// Checks devices, services and ports. Returns a process exit code.
int hw_doctor(int radio_port, int api_port);

}  // namespace mz

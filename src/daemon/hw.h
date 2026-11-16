// SPDX-License-Identifier: GPL-3.0-only
// Power and antenna-switch control for the Cap LoRa-1262 on a CardputerZero.
#pragma once

namespace mz {

// Turns the cap's 5 V rail on/off and drives the RF switch enable (P0 of the
// PI4IOE5V6408 expander). Returns false when a required step failed.
bool hw_power(bool on);

// Prints the current rail and expander state. Returns a process exit code.
int hw_status();

// Checks devices, services and ports. Returns a process exit code.
int hw_doctor(int radio_port, int api_port);

}  // namespace mz

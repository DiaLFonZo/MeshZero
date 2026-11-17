// SPDX-License-Identifier: GPL-3.0-only
// Looks for a newer MeshZero release when the app starts, at most once an
// hour, without holding up the interface. Needs the system's curl and a
// network; without either it simply finds nothing.
#pragma once

#include <string>

namespace mz::update {

// Reads what is known from earlier runs and, when checking is on and the last
// look was long enough ago, asks for the latest release in the background.
void start();

// The newer version that was found, or empty.
std::string available();
// True once for each newer version, so it is announced a single time.
bool take_notice();
// Whether a look at the releases has succeeded at some point.
bool checked();

bool enabled();
void set_enabled(bool on);

}  // namespace mz::update

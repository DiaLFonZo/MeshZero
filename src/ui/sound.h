// SPDX-License-Identifier: GPL-3.0-only
// The two sounds the app makes: a short one for a new message and a longer,
// more insistent one for a message that carries an alert bell.
#pragma once

namespace mz::sound {

enum class Kind { Message, Alert };

// Starts the sound and returns at once. A message sound is left out while
// another sound is still playing; an alert cuts a message sound short.
void play(Kind kind);

// Scripted runs make no sound unless a player is named in MESHZERO_SOUND_CMD.
void set_enabled(bool on);

}  // namespace mz::sound

// SPDX-License-Identifier: GPL-3.0-only
// MeshCard message alarm: settings shared by the Settings page and the
// background alarm service (meshcard-alertd), and the alarm sounds.
#pragma once

#include <cstdint>
#include <string>

namespace mz {

struct AlertConf {
    bool on     = true;
    bool all    = false;  // false: direct messages only
    int volume  = 80;     // percent, speaker volume while ringing
    int sound   = 0;      // index into alert_sound_names()
    bool repeat = true;   // ring every few seconds until a key is pressed
};

constexpr int kAlertSoundCount = 4;
const char* alert_sound_name(int index);

// ~/.config/meshcard/alert.conf
std::string alert_conf_path();
AlertConf alert_conf_load();
bool alert_conf_save(const AlertConf& c);

// The chat MeshCard has open on screen, written by the app ("pid dm id"), read by
// meshcard-alertd: no ring for a message in that chat while the app runs.
std::string alert_open_chat_path();
bool alert_chat_is_open(bool dm, uint32_t id);

// Rings once in the background (does not wait): sets the speaker volume,
// plays the sound, puts the volume back.
void alert_ring(const AlertConf& c);

#if defined(MESHCARD_ESP32)
// Cardputer ADV (no meshcard-alertd): the same alarm as the Zero's, run by the screen task.
// A message came in (dm: a direct message to us) for a chat not on screen: ring if the settings say so.
void alert_message(bool dm);
// Any key stops the ring.
void alert_key();
// Called often: rings every 5 s while the alarm is on, stops by itself after 3 minutes.
void alert_tick();
#endif

}  // namespace mz

# Changelog

## 0.2.1

**Fixed**

- **Run in background is opt-in (#9):** it's off unless switched on. The update turns it off for everyone, and asks once at launch anyone who had it on whether to keep it.
- **Frequency outside the region (#8):** any frequency the radio can tune to (150–960 MHz) can be set. If outside of region's legal frequency, a warning is shown.
- **Short names (#6):** nodes heard without a name are asked for their info in the background.
- **Coding rate:** LongSlow, LongModerate and LongTurbo defaults were being saved at 4/5 instead of 4/8. The app now sends the right rate, and corrects radios already affected once, with a toast.
- **Regions:** JP's range now matches the firmware. Regions the cap or firmware can't use are hidden, and a radio already on one is asked to pick another.
- **Settings match firmware 2.7.26:** no values the firmware ignores or replaces. Bandwidth is a list of the 10 supported values, and transmit power, frequency slot and interval limits are enforced. Repeater, "All, skip decoding", the Turbo presets on EU_868 and Shutdown on battery after are removed.
- **Preset switching:** you stay on the new preset's public channel.
- **Intervals:** an interval set to "never" no longer shows as 2147483647 s.

**Added**

- ? key in Setup menu explains the highlighted settings
- Battery percentage in the top bar.
- Accent color choice.
- Held arrow keys repeat.
- Delete a chat's history, or all messages.

**Changed**

- QR share, Add from URL, Scan and Community setups moved to the main Setup page.
- "Network Access (LAN)" is the new name for Network access.
- The GPS interval's help line says the first fix waits one interval.
- The Help page is updated for all of the above.

## 0.2.0
What was meant to be a couple of bug fixes became an entire minor release with new features and abilities. All thanks to getting sick as a dog and having a day in bed.

**Added**

- **Tapbacks:** React to messages with emojis.
- **Replies:** Reply to specific messages.
- **Emoji typing:** Click "tab" during a chat to open the selector.
- **Message menu:** Click Up (Fn + F) to enter select messages and select one to see actions.
- **Message details:** Sender, time, hops, signal, encryption, delivery status, reactions, etc.
- **QR scanning:** Add channels by scanning a code with the camera. Compatible with most config generators, the official app, etc.
- **Radio settings from links:** A channel link or QR code that carries radio settings can be applied as a whole.
- **Community setups (experimental):** Allows meshtastic communities to submit their
custom radio/channel configurations to the app so you can have a one click setup experience.
- **Update check:** Looks for a newer release at launch.
- **Scroll arrows:** Lists and text pages show when there is more above or below.

**Fixed**
- **iOS app dropped the connection** Would disconnect every so many seconds due to
missing heartbeat (issue #1)
- **Older cap support:** Properly detects the older model of LoRa cap. (issue #2)
- **Android app could not connect** Repeatedly connecting and failing during node retrieval (issue #3).
- **Automatic key requests:** Direct messages ask the other node for its public key, backing off when unanswered instead of waiting around for it.

**Changed**

- **Exit:** hold Esc for 2 seconds on the main pages instead of a single press.
- **LoRa page:** one Modem row replaces the "Use modem preset" switch; the modem values always show what is in use, and editing one makes the modem Custom.
- **Tab in a direct chat** opens the emoji grid; the node's details moved to the message menu.
- **Enter on a picked message** opens the menu, so a tapback is one more key press than when tapbacks were first built.
- **Scanner memory:** the QR reader gives its work buffers back when you leave it.

## 0.1.0

First release.

- Channel chat and direct messages with delivery status. History is kept
  while the app is closed.
- Node list, node details, traceroute, position and telemetry requests,
  favourites and ignore.
- Radar-style map of nearby nodes.
- Radio status page with a packet log.
- Settings for region, modem preset, channels and keys, node name and role,
  position (including how precisely your location is shared), power, network,
  security, MQTT, telemetry and other modules.
- Meshtastic TCP API on port 4403 for MeshMonitor, the `meshtastic` CLI and
  the phone apps. More than one client can connect at once. Off by default,
  turn it on under Setup > Network access.
- Emoji in messages are displayed (single colour).
- The radio does not run without the cap attached, and can be set to run only
  while the app is open (Setup > Run in background).
- Ships `meshtasticd` 2.7.26 as the radio backend.

Known gaps:

- No waypoints, replies, tapbacks or remote admin yet.
- Emoji can be read but not typed.
- No Bluetooth. Phones connect over Wi-Fi.
- The interface only handles the Cap LoRa-1262. Other radios are not set up.

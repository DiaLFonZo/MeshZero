# Changelog

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

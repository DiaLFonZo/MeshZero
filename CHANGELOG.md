# Changelog

## 0.2.0

- Lists and text pages show a small arrow at the bottom, or at the top, while
  there is more to scroll to in that direction.
- Replies. Pick a message with Up, press Enter and choose Reply. Replies show
  the message they answer on a line above them.
- Emoji can be typed: Tab in a chat opens a grid to pick from. In a direct
  chat Tab used to open the node's details; those are now under Enter on one
  of its messages, "About the sender".
- Message details: who, when, hops, signal, encryption, delivery status and
  reactions.
- A message that was not delivered can be sent again from the same menu.
- Channel links and QR codes that carry radio settings can be applied as a
  whole (channels and radio), not only added as channels.
- Setup > Channels > Community setups, starting with SFNarrow (Spain). Marked
  experimental: it is new and still being worked on.
- Setup > LoRa radio is simpler. One Modem row names the preset in use, or
  Custom. Bandwidth, spreading factor and coding rate always show what is in
  use, and changing one of them makes the modem Custom. The separate "Use
  modem preset" switch is gone.
- A fixed frequency can be set there for custom modems. It has to lie in the
  region's band, and it is cleared when the region changes or a preset is
  chosen.
- The modem list only offers presets the bundled radio daemon knows.
  VeryLongSlow, LiteFast, LiteSlow, NarrowFast and NarrowSlow were listed
  before but ran as LongFast.
- Leaving the app takes holding Esc for 2 seconds, so a stray press no longer
  closes it.
- The app tells you when a newer release is out: a line at the top the first
  time it is seen, and Setup > Updates from then on. It looks when it starts,
  at most once an hour, and only asks GitHub for the latest release number.
  Turn it off under Setup > Updates.
- Tapbacks. In a chat, Up picks a message and Enter offers a set of emoji to
  react with. Reactions are shown under the message they belong to, with who
  sent them, instead of as messages of their own.
- Fixed the Meshtastic phone app giving up with "Could not establish a stable
  connection after repeated attempts". The app fetches the node list in a
  second step, and the answer to it started with a packet that made the app
  begin again.
- Fixed the Meshtastic iOS app dropping the connection after about 24 seconds
  on a quiet mesh. The app expects an answer to its heartbeat within 5
  seconds, and heartbeats were not passed on to the radio. They are now, so
  clients get the same answer as from any other node.
- The older Cap LoRa-1262 without the IO expander is recognised by asking the
  radio chip itself, and runs without the antenna switch step.
- Direct messages fetch the other node's public key on their own. Opening a
  chat with a node whose key is missing, or sending to it, asks that node for
  its key, and asks again after 6 minutes while the chat stays open, then
  less and less often.
- The chat shows when it is still waiting for a key.
- Channels can be added by scanning a shared QR code with the camera
  (Setup > Channels > Scan QR code). The app lists what the code contains and
  asks before adding it. The entry only appears on models with a camera, and
  needs the system's `rpicam-vid`.

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

# Changelog

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
  the phone apps. More than one client can connect at once.
- Ships `meshtasticd` 2.7.26 as the radio backend.

Known gaps:

- No waypoints, replies, tapbacks or remote admin yet.
- Emoji are shown as text.
- No Bluetooth. Phones connect over Wi-Fi.
- The interface only handles the Cap LoRa-1262. Other radios are not set up.

# MeshZero

A Meshtastic client for the M5Stack CardputerZero with the Cap LoRa-1262.

The Cardputer Zero needed a fully featured meshtastic client, but the only
available app was their built in LoRa app, which is a basic implementation.
The goal with this project was to implement everything that the meshtastid
daemon supports, and bring the Cardputer up with full Meshtastic support.

My long term goal is to expand the app's abilities into Meshcore and potentially
other mesh networking technologies.

![Chats](screenshots/1-chats.png) ![Nodes](screenshots/3-nodes.png) ![Map](screenshots/5-map.png)

## What it does

- Channel chat and DMs with delivery status and unread counts.
- Tapbacks: react to any message with an emoji, and see who reacted to what.
- Background service keeps meshtastic running even when off app.
- Node listing with details, actions, and all supported functions.
- A simple radar-style map of the nodes around you.
- A radio page with link state, counters, GPS fix and a live packet log.
- Settings for most of what the phone apps cover: region, modem preset,
  channels and keys (add by URL or by scanning a QR code with the camera,
  share by QR), node name and role, position,
  power, network, security, MQTT, telemetry and a few other modules.
- The Meshtastic TCP API on port 4403, for MeshMonitor, the `meshtastic` CLI
  or the phone apps over Wi-Fi. Enable that in settings.

Potentially Roadmap:
- Replies

## You need

- CardputerZero on the official 64-bit Debian 13 image.
- Cap LoRa-1262 with an appropriate antenna for your region.

### Important
Power the device off before you attach or remove the cap. It is not
hot-pluggable. And don't transmit without an antenna.

## Install

Get it from the CardputerZero store, or download the `.deb` from the releases
page and install it yourself:

```sh
sudo apt install ./meshzero_0.1.1_arm64.deb
```

## Keys

| Key | What it does |
| --- | --- |
| Arrows (F, X, Z, C) | Move and scroll. No Fn needed, except while typing text |
| Enter | Open, edit, send |
| Up, in a chat | Pick a message to react to. Up and Down move between messages, Enter chooses the emoji |
| Esc | Back. On a changed settings page it asks whether to save |
| Tab, Left, Right | Switch between Chats, Nodes, Map, Radio and Setup |
| + and - | Zoom the map, 0 to fit everything |
| ? | Help |

## Running in the background

The radio stays on after you close the app, so messages keep arriving and your
node keeps relaying. While it runs it owns the LoRa cap and the GPS, so other
apps can't use them. To free the cap, turn off Setup > Run in background. The
radio then starts when you open MeshZero and stops about ten seconds after you
close it.

Without the cap attached the radio does not run at all. It starts once a cap
is found or the app is opened.

## Using it from other software

Turn on Setup > Network access first. It is off by default. The Radio page
then shows the device's IP address. Point a Meshtastic TCP client at it on
port 4403:

```sh
meshtastic --host 192.168.1.50 --info
```

For MeshMonitor, set `MESHTASTIC_NODE_IP` to that address.
The API has no authentication, same as any Meshtastic node with networking
turned on, so while network access is on anyone on your network can use the
radio.

## Building

On a desktop, for development:

```sh
cmake -S . -B build/host -G Ninja      # add -DMESHZERO_SDL=ON for a window (needs libsdl2-dev)
cmake --build build/host
ctest --test-dir build/host            # the proxy test also wants: pip install meshtastic
```

You don't need the hardware to work on it. `tools/fakeradio.py` pretends to be
a radio with a handful of nodes:

```sh
pip install meshtastic
python3 tools/fakeradio.py &
MESHZERO_STATE_DIR=/tmp/mz build/host/meshzerod serve &
MESHZERO_STATE_DIR=/tmp/mz build/host/meshzero --display sdl
```

The camera can be stood in for as well. `MESHZERO_CAMERA_CMD` names a program
that writes raw 1920x1080 YUV 4:2:0 frames, for example a still image through
ffmpeg:

```sh
export MESHZERO_CAMERA_CMD="ffmpeg -loglevel quiet -re -loop 1 -i code.png \
  -vf scale=300:300,pad=1920:1080:-1:-1:gray -pix_fmt yuv420p -r 10 -f rawvideo -"
```

`meshzero --script FILE` runs the interface with no display and saves
screenshots. That is how the ones in `screenshots/` are made, see
`tools/uitest.sh`.

The arm64 package is cross-built in Docker:

```sh
packaging/build-deb.sh                 # writes dist/meshzero_<version>_arm64.deb
```

## License

GPL-3.0-only. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
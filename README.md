# MeshZero

![Social Preview of App](assets/social-preview.png)

A Meshtastic client for the M5Stack CardputerZero with the Cap LoRa-1262.

The Cardputer Zero needed a fully featured meshtastic client, but the only
available app was their built in LoRa app, which is a basic implementation.
The goal with this project was to implement everything that the meshtastid
daemon supports, and bring the Cardputer up with full Meshtastic support.

My long term goal is to expand the app's abilities into Meshcore and potentially
other mesh networking technologies.

## A little taste...

![Chats](screenshots/1-chats.png) ![Nodes](screenshots/3-nodes.png) ![Map](screenshots/5-map.png)

## What it does

- Channel chat and DMs with delivery status and unread counts.
- Tapbacks, replies and emoji: react to any message, answer one in
  particular, pick emoji from a grid while writing.
- Message details (signal, hops, encryption, delivery) and sending a failed
  message again.
- Optional background mode that keeps the radio running after you close the app.
- Node listing with details, actions, and all supported functions.
- A map of the nodes around you: a simple radar, or pins on a street or
  topographic map. Map tiles are downloaded as you look at them and kept, so
  places you have seen before work without a network.
- Metric or imperial units. With imperial, distances are in feet and miles and
  the topographic map has its heights in feet inside the United States.
- A radio page with link state, counters, GPS fix and a live packet log.
- Settings for most of what the phone apps cover: region, modem preset,
  channels and keys (add by URL or by scanning a QR code with the camera,
  share by QR), custom modem settings, node name and role, position,
  power, network, security, MQTT, telemetry and a few other modules.
- The Meshtastic TCP API on port 4403, for MeshMonitor, the `meshtastic` CLI
  or the phone apps over Wi-Fi. Enable that in settings.
- Supports the new and old model Cap Lora 1262 modules.
- Community Configs allow meshtastic groups/regions around the world to submit their
  preferred radio and channel configurations for easy setup.

## You need

- CardputerZero on the official 64-bit Debian 13 image.
- Cap LoRa-1262 with an appropriate antenna for your region.


> [!IMPORTANT]
> Power the device off before you attach or remove the cap. It is not
hotswappable and can cause damage. Avoid using without an antenna attached.


## Install

### Store Install

Get it directly from the built-in store: [MeshZero on Cardputer.cc](https://cardputer.cc/#/apps/MSH0) 

### APT

```sh
sudo apt install ./meshzero_<version>_arm64.deb
```

### DPKG
Install the dependencies first:

```sh
sudo apt install adduser libgpiod3 libi2c0 libinput10 liborcania2.3 libsdl2-2.0-0 libssl3t64 libulfius2.7t64 libusb-1.0-0 libuv1t64 libx11-6 libxkbcommon0 libyaml-cpp0.8
```

then install:
``` sh
sudo dpkg -i ./meshzero_<version>_arm64.deb
```

## Community Configurations

Some local meshes do not use one of the official modem presets, and have
agreed upon radio and channel configurations for their area. With community
configurations, I want to enable communities around the world to submit
their preferred settings so that you can set them on your Cardputer with just
a click.

I will start by adding the most common configurations, but hope the community
will reach out to add their settings as this app becomes more popular (hopefully).

## Keys

| Key | What it does |
| --- | --- |
| Arrows (F, X, Z, C) | Move and scroll. No Fn needed, except while typing text |
| Enter | Open, edit, send |
| Up, in a chat | Pick a message. Up and Down move between messages, Enter offers what to do with it: react, reply, details, send again |
| Tab, in a chat | Emoji to put into the message being written |
| Esc | Back. On a changed settings page it asks whether to save. On the main pages, hold it for 2 seconds to leave the app |
| Tab, Left, Right | Switch between Chats, Nodes, Map, Radio and Setup |
| + and - | Zoom the map, 0 to fit everything |
| M, on the map | Switch between the radar, the street map and the topographic map |
| Enter, on the map | Lets the arrows move the street or topographic map. Enter again when done |
| ? | Help |

## Running in the background

The radio stops about 10 seconds after you close the app, so other apps can use
the LoRa cap. To keep receiving messages and relaying for the mesh while it's
closed, turn on Setup > Run in background. While it's on, MeshZero owns the LoRa
cap and the GPS, so other apps can't use them.

## Using it from other software

Turn on Setup > Network Access (LAN) first. It is off by default. The Radio page
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

The arm64 package is cross-built in Docker:

```sh
packaging/build-deb.sh                 # writes dist/meshzero_<version>_arm64.deb
```

## License

GPL-3.0-only. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

# MeshZero

A Meshtastic client for the M5Stack CardputerZero with the Cap LoRa-1262.

I wanted to use the Cardputer as a standalone Meshtastic node, with a real
keyboard, and still be able to reach the same radio from MeshMonitor on my
home network. MeshZero does both. The radio side is the official Meshtastic
Linux daemon, so on the air it behaves like any other node. What this project
adds is the on-device interface and a small proxy that lets several clients
share the radio.

![Chats](screenshots/1-chats.png) ![Nodes](screenshots/3-nodes.png) ![Map](screenshots/5-map.png)

## What it does

- Channel chat and direct messages, with delivery status and unread counts.
  Messages that arrive while the app is closed are still there when you open it.
- Node list with signal, hops, battery, distance and bearing. From a node you
  can send a DM, run a traceroute, ask for its position or telemetry, and
  favourite, ignore or remove it.
- A simple radar-style map of the nodes around you.
- A radio page with link state, counters, GPS fix and a live packet log.
- Settings for most of what the phone apps cover: region, modem preset,
  channels and keys (add by URL, share by QR), node name and role, position,
  power, network, security, MQTT, telemetry and a few other modules.
- The Meshtastic TCP API on port 4403, for MeshMonitor, the `meshtastic` CLI
  or the phone apps over Wi-Fi. Several can be connected at the same time.

Not there yet: waypoints, replies and tapbacks, remote admin of other nodes,
and proper emoji (they show up as text for now). There is no Bluetooth,
because the Linux daemon doesn't have it. Phones connect over Wi-Fi instead.

## You need

- A CardputerZero on the official 64-bit Debian 13 image.
- The Cap LoRa-1262 with an antenna on it.

Power the device off before you attach or remove the cap. It is not
hot-pluggable. And don't transmit without an antenna.

## Install

Get it from the CardputerZero store, or download the `.deb` from the releases
page and install it yourself:

```sh
sudo apt install ./meshzero_0.1.0_arm64.deb
```

Open MeshZero from the launcher. The first time, it asks for your region. The
radio will not transmit until you pick one.

## Keys

| Key | What it does |
| --- | --- |
| Arrows (F, X, Z, C) | Move and scroll. No Fn needed, except while typing text |
| Enter | Open, edit, send |
| Esc | Back. On a changed settings page it asks whether to save |
| Tab, Left, Right | Switch between Chats, Nodes, Map, Radio and Setup |
| + and - | Zoom the map, 0 to fit everything |
| ? | Help |

In a chat, type and press Enter. A green tick next to your message means the
recipient acknowledged it, a grey tick means the mesh relayed it, and a cross
means it wasn't delivered (the reason is shown under the message).

To DM someone, open them on the Nodes page and pick *Send direct message*.

If a first DM to a node fails with "they did not have your key yet", send it
again. Your radio has already sent them the key by then.

## Using it from other software

The Radio page shows the device's IP address. Point a Meshtastic TCP client at
it on port 4403:

```sh
meshtastic --host 192.168.1.50 --info
```

For MeshMonitor, set `MESHTASTIC_NODE_IP` to that address. In the Android app,
add the device by IP address on the connection screen.

The API has no authentication, same as any Meshtastic node with networking
turned on, so anyone on your network can use the radio. If you don't want
that, set `MESHZERO_API_BIND=127.0.0.1` in `/etc/meshzero/meshzero.conf` and
restart `meshzero.service`.

## How it fits together

```
 LoRa cap ── meshzero-radio.service ── meshzero.service ──┬─ meshzero (the app)
              meshtasticd                meshzerod         ├─ MeshMonitor
              127.0.0.1:4404             0.0.0.0:4403      └─ CLI, phone apps
```

`meshtasticd` is the Meshtastic project's Linux build. An unmodified copy is
shipped in the package and it does all the radio and mesh work.

`meshzerod` is there because `meshtasticd` only accepts one client at a time,
and a new connection kicks the previous one off. It keeps the single
connection to the radio and hands each client its own, including the config
download at connect time. It also keeps the message history for the app and
turns on the cap's 5 V rail and antenna switch before the radio starts.

`meshzero` is the interface, written with LVGL. It is just another API client
talking to `meshzerod` on localhost.

Both services run as a `meshzero` system user, not root.

| Path | What's in it |
| --- | --- |
| `/etc/meshzero/radio.yaml` | Pin and bus configuration for `meshtasticd` |
| `/etc/meshzero/meshzero.conf` | API address and port, option to use a system `meshtasticd` |
| `/var/lib/meshzero/` | Node database, keys, message history |

How the cap is wired, for reference:

| Function | On the CardputerZero |
| --- | --- |
| SX1262 SPI | `/dev/spidev0.1` |
| DIO1 / RESET / BUSY | GPIO 23 / 26 / 22 |
| TCXO and RF switch | DIO3 at 3.0 V, DIO2 |
| Antenna switch enable | PI4IOE5V6408 at I2C 0x43 on `/dev/i2c-1`, pin P0 high |
| 5 V rail | `/sys/class/leds/ext_5v_out/brightness` |
| GNSS | `/dev/ttyS0` at 115200 |

## When something is wrong

```sh
sudo -u meshzero meshzerod doctor
systemctl status meshzero meshzero-radio
journalctl -u meshzero-radio -n 50
```

`doctor` checks the devices, permissions, the cap and both services, and tells
you which one is the problem.

- "radio offline" in the top bar: the radio service isn't running. Most often
  the cap isn't seated or wasn't detected.
- No GPS position: turn GPS on under Setup > Position and give it a few
  minutes outside for the first fix.
- Nobody hears you: check the region, and that you are on the same modem
  preset and channel as everyone else.

If you want a newer `meshtasticd` than the one in the package, install the
upstream package, set `MESHZERO_USE_SYSTEM_MESHTASTICD=1` in
`/etc/meshzero/meshzero.conf` and restart `meshzero-radio`. Leave the upstream
`meshtasticd.service` disabled, since only one process can own the radio.

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
tools/fakeradio.py &
MESHZERO_STATE_DIR=/tmp/mz build/host/meshzerod serve &
MESHZERO_STATE_DIR=/tmp/mz build/host/meshzero --display sdl
```

`meshzero --script FILE` runs the interface with no display and saves
screenshots. That is how the ones in `screenshots/` are made, see
`tools/uitest.sh`.

The arm64 package is cross-built in Docker:

```sh
packaging/build-deb.sh                 # writes dist/meshzero_<version>_arm64.deb
```

Where things live:

| Path | Contents |
| --- | --- |
| `src/common` | Stream framing, protobuf helpers, message history |
| `src/daemon` | `meshzerod`: the proxy and the cap's power and antenna control |
| `src/ui` | `meshzero`: the mesh model, the app shell and the screens |
| `src/proto/gen` | nanopb code generated from the Meshtastic protobufs |
| `packaging` | systemd units, radio config, Debian packaging |
| `tools` | Fake radio, UI test script, icon and name-table generators |

Bug reports and patches are welcome. If something misbehaves on your device,
the output of `meshzerod doctor` and the radio log help a lot.

## License

GPL-3.0-only. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

MeshZero is an independent project and is not affiliated with or endorsed by
the Meshtastic project. Meshtastic® is a registered trademark of Meshtastic LLC.

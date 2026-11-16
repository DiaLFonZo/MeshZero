# Third-party notices

MeshZero itself is licensed under the GNU General Public License v3.0 only
(see `LICENSE`). It includes or is distributed with the following components.

## Meshtastic firmware (`meshtasticd`)

The package ships an unmodified `meshtasticd` binary built by the Meshtastic
project for Debian 13 (arm64). The exact version is recorded in
`/usr/lib/meshzero/meshtasticd.version` and pinned, with its checksum, in
`packaging/fetch-radio.sh`.

- License: GPL-3.0
- Source: https://github.com/meshtastic/firmware (the tag matching the version above)
- Binary packages: https://download.opensuse.org/repositories/network:/Meshtastic:/beta/

## Meshtastic protobuf definitions

The files under `src/proto/gen/` are generated from the Meshtastic protocol
definitions.

- License: GPL-3.0
- Source: https://github.com/meshtastic/protobufs

## nanopb

Protocol buffer runtime in `third_party/nanopb/`.

- License: zlib (see `third_party/nanopb/LICENSE.txt`)
- Source: https://github.com/nanopb/nanopb

## LVGL

Graphics library, downloaded at build time and linked statically.

- License: MIT
- Source: https://github.com/lvgl/lvgl

LVGL bundles the Montserrat font (SIL Open Font License 1.1) and a subset of
Font Awesome Free icons (SIL Open Font License 1.1), which MeshZero uses for
its text and symbols.

## Fonts

`Montserrat-Medium.ttf` (SIL Open Font License 1.1) and `DejaVuSans.ttf`
(Bitstream Vera / DejaVu license) are installed in `/usr/share/meshzero/fonts`
to render characters outside ASCII. Their license texts are installed in
`/usr/share/doc/meshzero/font-licenses`.

## Trademark

Meshtastic® is a registered trademark of Meshtastic LLC. MeshZero is an
independent project and is not affiliated with or endorsed by the Meshtastic
project.

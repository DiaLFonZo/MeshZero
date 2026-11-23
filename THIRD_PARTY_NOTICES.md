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

## quirc

QR code reader in `third_party/quirc/` (version 1.2), used to scan channel
codes with the camera.

- License: ISC (see `third_party/quirc/LICENSE`)
- Source: https://github.com/dlbeer/quirc

    Copyright (C) 2010-2012 Daniel Beer <dlbeer@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for
    any purpose with or without fee is hereby granted, provided that the
    above copyright notice and this permission notice appear in all
    copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
    WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
    AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
    PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
    TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
    PERFORMANCE OF THIS SOFTWARE.

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
to render characters outside ASCII. `NotoEmoji-Medium.ttf` (SIL Open Font
License 1.1, Copyright 2013 Google LLC) is installed next to them for emoji.
It is a fixed-weight copy (weight 500) made from the variable Noto Emoji font
at https://github.com/google/fonts/tree/main/ofl/notoemoji with fontTools, and
is kept in `assets/fonts` together with its license. The license texts are
installed in `/usr/share/doc/meshzero/font-licenses`.

## Map tiles

The street and topographic maps are not part of the package. The tiles a map
shows are downloaded while it is looked at, when map downloads are on, and
kept on the device.

- Street: © OpenStreetMap contributors, available under the Open Database
  License (https://www.openstreetmap.org/copyright). Tiles from
  tile.openstreetmap.org, under its tile usage policy
  (https://operations.osmfoundation.org/policies/tiles/).
- Topo: map data © OpenStreetMap contributors and SRTM; map style
  © OpenTopoMap (CC-BY-SA, https://opentopomap.org/about).

## Trademark

Meshtastic® is a registered trademark of Meshtastic LLC. MeshZero is an
independent project and is not affiliated with or endorsed by the Meshtastic
project.

#!/bin/sh
# Downloads the official Meshtastic Linux daemon (arm64, built for Debian 13)
# and unpacks the binary MeshZero ships. The checksum pins the exact build.
set -eu

VERSION="2.7.26.61~obs54e0d8d~beta"
SHA256="53d3bfe23f428c79949f18e083114fe0a3ef2f3aac3a90c0b00b8e33c1b333a7"
URL="https://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/arm64/meshtasticd_${VERSION}_arm64.deb"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/third_party/meshtasticd/arm64"
if [ -x "$OUT/meshtasticd" ] && [ "$(cat "$OUT/VERSION" 2>/dev/null)" = "$VERSION" ]; then
    echo "meshtasticd $VERSION already present"
    exit 0
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
echo "Downloading meshtasticd $VERSION"
curl -fL --retry 3 -o "$WORK/meshtasticd.deb" "$URL"
echo "$SHA256  $WORK/meshtasticd.deb" | sha256sum -c -
dpkg-deb -x "$WORK/meshtasticd.deb" "$WORK/root"
mkdir -p "$OUT"
install -m 755 "$WORK/root/usr/bin/meshtasticd" "$OUT/meshtasticd"
echo "$VERSION" > "$OUT/VERSION"
echo "Installed $OUT/meshtasticd"

#!/bin/sh
# Builds dist/meshzero_<version>_arm64.deb in a container.
#
#   packaging/build-deb.sh            # needs Docker
#   NATIVE=1 packaging/build-deb.sh   # on a Debian 13 host with the arm64 cross toolchain
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

if [ "${NATIVE:-0}" != "1" ] && [ ! -f /.dockerenv ]; then
    docker build -q -t meshzero-build packaging/docker >/dev/null
    exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT:/src" meshzero-build packaging/build-deb.sh
fi

packaging/fetch-radio.sh
cmake -S . -B build/arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DMESHZERO_BUILD_TESTS=OFF \
    -DMESHZERO_REQUIRE_RADIO=ON \
    -DMESHZERO_REQUIRE_BLUETOOTH=ON
cmake --build build/arm64
(cd build/arm64 && cpack -G DEB)
ls -l dist/*.deb

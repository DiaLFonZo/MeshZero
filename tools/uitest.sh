#!/bin/sh
# Runs a UI script (see `meshzero --help`) against a simulated mesh, with no
# hardware and no display:
#
#   tools/uitest.sh tools/screenshots.txt [fakeradio options]
#
# Needs a host build in build/host and `pip install meshtastic` for the fake
# radio. Set PYTHON to choose the interpreter.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SCRIPT=$1
shift
PYTHON=${PYTHON:-python3}
RADIO_PORT=${RADIO_PORT:-4415}
API_PORT=${API_PORT:-4416}
BIN=${BUILD:-$ROOT/build/host}

WORK=$(mktemp -d)
cleanup() {
    [ -n "${PROXY_PID:-}" ] && kill "$PROXY_PID" 2>/dev/null || true
    [ -n "${RADIO_PID:-}" ] && kill "$RADIO_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

"$PYTHON" "$ROOT/tools/fakeradio.py" --port "$RADIO_PORT" "$@" >"$WORK/radio.log" 2>&1 &
RADIO_PID=$!
sleep 1
MESHZERO_STATE_DIR="$WORK/state" "$BIN/meshzerod" serve --listen 127.0.0.1 \
    --port "$API_PORT" --radio-port "$RADIO_PORT" >"$WORK/proxy.log" 2>&1 &
PROXY_PID=$!
sleep 1

cd "$ROOT"
XDG_STATE_HOME="$WORK/ui" MESHZERO_STATE_DIR="$WORK/state" \
    "$BIN/meshzero" --host "127.0.0.1:$API_PORT" --script "$SCRIPT"

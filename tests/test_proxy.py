#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""End-to-end test of meshzerod against the fake radio, using the reference
Python client as the API consumer.

    pip install meshtastic
    tests/test_proxy.py build/host/meshzerod
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from meshtastic.protobuf import channel_pb2, mesh_pb2
from meshtastic.tcp_interface import TCPInterface
from pubsub import pub

ROOT = Path(__file__).resolve().parent.parent
RADIO_PORT = 4425
API_PORT = 4426
MY_NUM = 0x4D5A0001
NETWORK_ADDR = "127.0.0.2"  # stands in for the address other devices would use


def wait_for(predicate, timeout=10.0, what="condition"):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError("timed out waiting for " + what)


def can_connect(host):
    try:
        socket.create_connection((host, API_PORT), timeout=1).close()
        return True
    except OSError:
        return False


def handshake(nonce, heartbeat=False):
    """Asks for a configuration the way the phone apps do, over a plain socket.
    Returns the kinds of the frames up to config_complete, and whether the
    heartbeat sent afterwards was answered."""
    sock = socket.create_connection(("127.0.0.1", API_PORT), timeout=5)

    def send(msg):
        data = msg.SerializeToString()
        sock.sendall(b"\x94\xc3" + struct.pack(">H", len(data)) + data)

    def frames():
        buffer = b""
        while True:
            while len(buffer) >= 4 and len(buffer) >= 4 + struct.unpack(">H", buffer[2:4])[0]:
                length = struct.unpack(">H", buffer[2:4])[0]
                msg = mesh_pb2.FromRadio()
                msg.ParseFromString(buffer[4:4 + length])
                buffer = buffer[4 + length:]
                yield msg
            chunk = sock.recv(4096)
            if not chunk:
                return
            buffer += chunk

    try:
        incoming = frames()
        send(mesh_pb2.ToRadio(want_config_id=nonce))
        kinds = []
        for msg in incoming:
            if msg.WhichOneof("payload_variant") == "config_complete_id":
                assert msg.config_complete_id == nonce
                break
            kinds.append(msg.WhichOneof("payload_variant"))
        answered = False
        if heartbeat:
            send(mesh_pb2.ToRadio(heartbeat=mesh_pb2.Heartbeat()))
            answered = any(msg.WhichOneof("payload_variant") == "queueStatus" for msg in incoming)
        return kinds, answered
    except socket.timeout:
        raise AssertionError("no answer to want_config %d" % nonce)
    finally:
        sock.close()


def main():
    daemon = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build/host/meshzerod")
    state = tempfile.mkdtemp(prefix="mzproxy")
    radio = subprocess.Popen([sys.executable, str(ROOT / "tools/fakeradio.py"), "--port", str(RADIO_PORT), "--quiet-mesh"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    proxy = subprocess.Popen([daemon, "serve", "--listen", NETWORK_ADDR, "--port", str(API_PORT), "--radio-port",
                              str(RADIO_PORT)], env=dict(os.environ, MESHZERO_STATE_DIR=state),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    clients = []
    try:
        received = []

        def on_text(packet, interface):  # pubsub keeps only a weak reference, so this must be a named function
            received.append((interface, packet))

        pub.subscribe(on_text, "meshtastic.receive.text")

        # Two clients at once: the radio alone would drop the first one.
        a = TCPInterface("127.0.0.1", portNumber=API_PORT)
        b = TCPInterface("127.0.0.1", portNumber=API_PORT)
        clients += [a, b]
        for client in (a, b):
            assert client.myInfo.my_node_num == MY_NUM, "wrong node number"
            assert len(client.nodes) == 9, "expected 9 nodes, got %d" % len(client.nodes)
            assert client.localNode.channels[1].settings.name == "Friends"
            assert client.localNode.localConfig.lora.hop_limit == 3
        print("ok: two simultaneous clients received the full configuration")

        # The phone apps fetch the configuration and the nodes in two rounds.
        # The second must not start with my_info, or they begin again.
        kinds, _ = handshake(69420)
        assert kinds[0] == "my_info" and "channel" in kinds and "config" in kinds, kinds
        assert kinds.count("node_info") == 1, "config-only round listed other nodes"
        kinds, answered = handshake(69421, heartbeat=True)
        assert kinds == ["node_info"] * 9, kinds
        assert answered, "heartbeat got no queue status"
        print("ok: two-round handshake as the phone apps do it, heartbeat answered")

        b.sendText("ping from b")
        wait_for(lambda: any(i is a and p["decoded"]["text"] == "ping from b" for i, p in received), what="echo to client a")
        assert not any(i is b and p["decoded"]["text"] == "ping from b" for i, p in received), "sender got its own echo"
        echoed = next(p for i, p in received if i is a)
        assert echoed["from"] == MY_NUM
        print("ok: text sent by one client is echoed to the other only")

        # ... and recorded with its acknowledgement for the on-device UI.
        log = Path(state) / "messages.log"
        wait_for(lambda: log.exists() and "\nA\t" in log.read_text(), what="ack in history")
        lines = log.read_text().splitlines()
        message = next(line.split("\t") for line in lines if line.startswith("M\t"))
        assert message[-1] == "ping from b" and message[6] == "1", message
        ack = next(line.split("\t") for line in lines if line.startswith("A\t"))
        assert ack[1] == message[2] and ack[2] == "3", ack  # implicit ack for a broadcast
        print("ok: message and acknowledgement recorded in the history")

        # A settings change through one client reaches clients that connect later.
        channel = b.localNode.channels[2]
        channel.role = channel_pb2.Channel.Role.SECONDARY
        channel.settings.name = "Hiking"
        channel.settings.psk = bytes(range(32))
        b.localNode.writeChannel(2)
        time.sleep(3.0)
        c = TCPInterface("127.0.0.1", portNumber=API_PORT)
        clients.append(c)
        assert c.localNode.channels[2].settings.name == "Hiking", "late client did not see the new channel"
        print("ok: configuration cache refreshed after a settings change")

        # Other devices are kept out until network access is turned on.
        assert not can_connect(NETWORK_ADDR), "API reachable from the network by default"
        a.sendData(b"mz:net=1", destinationId=0, portNum=256)
        wait_for(lambda: can_connect(NETWORK_ADDR), what="network access to open")
        assert can_connect("127.0.0.1"), "local access lost when network access was turned on"
        remote = TCPInterface(NETWORK_ADDR, portNumber=API_PORT)
        clients.append(remote)
        assert remote.myInfo.my_node_num == MY_NUM
        # A client that is not on this device cannot change the setting.
        remote.sendData(b"mz:net=0", destinationId=0, portNum=256)
        time.sleep(1.5)
        assert can_connect(NETWORK_ADDR), "a remote client turned network access off"
        subprocess.run([daemon, "network", "off"], env=dict(os.environ, MESHZERO_STATE_DIR=state), check=True,
                       stdout=subprocess.DEVNULL)
        wait_for(lambda: not can_connect(NETWORK_ADDR), what="network access to close")
        assert can_connect("127.0.0.1")
        clients.remove(remote)
        remote.close()
        print("ok: network access is off by default and can be switched on and off")

        # Clients come and go without disturbing the others.
        b.close()
        clients.remove(b)
        a.sendText("still here")
        wait_for(lambda: any(i is c and p["decoded"]["text"] == "still here" for i, p in received), what="echo after a client left")
        print("ok: remaining clients keep working after one disconnects")
        print("all proxy tests passed")
        return 0
    finally:
        for client in clients:
            try:
                client.close()
            except Exception:  # noqa: BLE001 - best effort during teardown
                pass
        proxy.terminate()
        radio.terminate()


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A scripted stand-in for the radio daemon, for developing the UI without hardware.

It speaks the client stream API on a TCP port (default 4404, where meshzerod
expects the radio) and simulates a small mesh: a handful of nodes with
positions, channel chatter, acknowledgements, replies to direct messages and
traceroute answers. Configuration changes are remembered for the session.

    pip install meshtastic
    tools/fakeradio.py [--port 4404] [--quiet-mesh] [--no-region]
"""
import argparse
import random
import socket
import struct
import threading
import time

from meshtastic.protobuf import admin_pb2, channel_pb2, config_pb2, mesh_pb2, module_config_pb2, portnums_pb2, telemetry_pb2

BROADCAST = 0xFFFFFFFF
MY_NUM = 0x4D5A0001
HOME = (52.3731, 4.8922)

NODES = [
    # num, short, long, hw model, (dlat, dlon), hops, snr, battery, has key
    (0xA1B2C301, "ANNA", "Anna Rooftop", "HELTEC_V3", (0.012, 0.020), 0, 6.5, 101, True),
    (0xA1B2C302, "BRAM", "Bram Mobile", "T_DECK", (-0.004, 0.011), 0, -3.25, 78, True),
    (0xA1B2C303, "RTR1", "Dune Router", "RAK4631", (0.051, -0.083), 1, 0, 100, True),
    (0xA1B2C304, "CLEO", "Cleo", "T_ECHO", (-0.031, -0.040), 1, 0, 54, True),
    (0xA1B2C305, "DIRK", "Dirk van Dam", "TBEAM", (0.090, 0.150), 2, 0, 33, True),
    (0xA1B2C306, "WX01", "Harbour Weather", "RAK4631", (-0.070, 0.095), 2, 0, 96, False),
    (0xA1B2C307, "EVA", "Eva Base", "STATION_G2", (0.002, -0.006), 0, 9.75, 101, True),
    (0xA1B2C308, "F0X", "Fox Hunter", "SEEED_XIAO_S3", None, 3, 0, 12, True),
]

CHATTER = [
    (0xA1B2C301, "Good morning mesh!"),
    (0xA1B2C302, "Morning Anna. Anyone near the harbour today?"),
    (0xA1B2C307, "Base station is back up after the storm."),
    (0xA1B2C304, "Copy that, signal is great from the dunes."),
    (0xA1B2C305, "Testing from 14 km out, two hops."),
    (0xA1B2C301, "Loud and clear Dirk \U0001F4E1\U0001F600 \u2764\ufe0f\U0001F525\u2600\ufe0f\U0001F37B"),
    (0xA1B2C306, "Gr\u00fc\u00dfe vom Hafen, se\u00f1al perfecta \U0001F44D \u041f\u0440\u0438\u0432\u0435\u0442!"),
]

REPLIES = ["Got it, thanks!", "On my way.", "Reading you fine here.", "Can you repeat that?"]


class Radio:
    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.conn = None
        self.owner_long = "MeshZero Dev"
        self.owner_short = "MZ01"
        self.packet_id = random.randrange(1, 1 << 30)

        self.lora = config_pb2.Config.LoRaConfig(
            use_preset=True, hop_limit=3, tx_enabled=True, tx_power=0,
            region=config_pb2.Config.LoRaConfig.UNSET if args.no_region else config_pb2.Config.LoRaConfig.EU_868)
        self.configs = {
            "device": config_pb2.Config.DeviceConfig(node_info_broadcast_secs=10800),
            "position": config_pb2.Config.PositionConfig(
                gps_mode=config_pb2.Config.PositionConfig.ENABLED, position_broadcast_secs=900,
                position_broadcast_smart_enabled=True),
            "power": config_pb2.Config.PowerConfig(),
            "network": config_pb2.Config.NetworkConfig(ntp_server="meshtastic.pool.ntp.org"),
            "display": config_pb2.Config.DisplayConfig(),
            "lora": self.lora,
            "bluetooth": config_pb2.Config.BluetoothConfig(),
            "security": config_pb2.Config.SecurityConfig(public_key=bytes(range(32))),
        }
        self.modules = {
            "mqtt": module_config_pb2.ModuleConfig.MQTTConfig(address="mqtt.meshtastic.org", root="msh/EU_868"),
            "telemetry": module_config_pb2.ModuleConfig.TelemetryConfig(device_update_interval=1800),
            "neighbor_info": module_config_pb2.ModuleConfig.NeighborInfoConfig(),
            "store_forward": module_config_pb2.ModuleConfig.StoreForwardConfig(),
            "range_test": module_config_pb2.ModuleConfig.RangeTestConfig(),
        }
        self.channels = []
        for i in range(8):
            ch = channel_pb2.Channel(index=i)
            if i == 0:
                ch.role = channel_pb2.Channel.PRIMARY
                ch.settings.psk = b"\x01"
            elif i == 1:
                ch.role = channel_pb2.Channel.SECONDARY
                ch.settings.name = "Friends"
                ch.settings.psk = bytes(random.randrange(256) for _ in range(32))
            self.channels.append(ch)

    # ------------------------------------------------------------ framing
    def send(self, from_radio):
        data = from_radio.SerializeToString()
        with self.lock:
            if self.conn is None:
                return
            try:
                self.conn.sendall(b"\x94\xc3" + struct.pack(">H", len(data)) + data)
            except OSError:
                pass

    def next_id(self):
        self.packet_id += 1
        return self.packet_id

    # ------------------------------------------------------------- nodes
    def user(self, num, short, long_name, hw, has_key=True):
        user = mesh_pb2.User(id="!%08x" % num, long_name=long_name, short_name=short,
                             hw_model=mesh_pb2.HardwareModel.Value(hw))
        if has_key:
            user.public_key = bytes([num & 0xFF] * 32)
        return user

    def my_node(self):
        info = mesh_pb2.NodeInfo(num=MY_NUM, is_favorite=True)
        info.user.CopyFrom(self.user(MY_NUM, self.owner_short, self.owner_long, "PORTDUINO"))
        info.position.latitude_i = int(HOME[0] * 1e7)
        info.position.longitude_i = int(HOME[1] * 1e7)
        info.position.altitude = 4
        info.position.sats_in_view = 9
        info.position.time = int(time.time())
        info.device_metrics.battery_level = 101
        info.device_metrics.channel_utilization = 7.4
        info.device_metrics.air_util_tx = 0.61
        info.device_metrics.uptime_seconds = 5400
        return info

    def other_nodes(self):
        now = int(time.time())
        out = []
        for i, (num, short, long_name, hw, offset, hops, snr, battery, has_key) in enumerate(NODES):
            info = mesh_pb2.NodeInfo(num=num, snr=snr, last_heard=now - (40 + i * 510), hops_away=hops)
            info.user.CopyFrom(self.user(num, short, long_name, hw, has_key))
            if offset:
                info.position.latitude_i = int((HOME[0] + offset[0]) * 1e7)
                info.position.longitude_i = int((HOME[1] + offset[1]) * 1e7)
                info.position.altitude = 3 + i * 4
                info.position.time = now - 300
            info.device_metrics.battery_level = battery
            info.device_metrics.voltage = 3.3 + battery / 120.0
            info.device_metrics.channel_utilization = 4.0 + i
            info.device_metrics.air_util_tx = 0.2 * (i + 1)
            info.device_metrics.uptime_seconds = 3600 * (i + 2)
            info.is_favorite = i == 0
            out.append(info)
        return out

    # ------------------------------------------------------------ config
    def send_config(self, nonce):
        my_info = mesh_pb2.MyNodeInfo(my_node_num=MY_NUM, reboot_count=3, min_app_version=30200, pio_env="native")
        self.send(mesh_pb2.FromRadio(my_info=my_info))
        self.send(mesh_pb2.FromRadio(node_info=self.my_node()))
        self.send(mesh_pb2.FromRadio(metadata=mesh_pb2.DeviceMetadata(
            firmware_version="2.7.26.fake", hasWifi=True, hasPKC=True, hw_model=mesh_pb2.HardwareModel.PORTDUINO)))
        for ch in self.channels:
            self.send(mesh_pb2.FromRadio(channel=ch))
        for name, value in self.configs.items():
            cfg = config_pb2.Config()
            getattr(cfg, name).CopyFrom(value)
            self.send(mesh_pb2.FromRadio(config=cfg))
        for name, value in self.modules.items():
            cfg = module_config_pb2.ModuleConfig()
            getattr(cfg, name).CopyFrom(value)
            self.send(mesh_pb2.FromRadio(moduleConfig=cfg))
        for node in self.other_nodes():
            self.send(mesh_pb2.FromRadio(node_info=node))
        self.send(mesh_pb2.FromRadio(config_complete_id=nonce))

    # ----------------------------------------------------------- packets
    def deliver(self, sender, to, port, payload, channel=0, request_id=0, hops=0, snr=5.0, delay=0.0, pki=False):
        def run():
            packet = mesh_pb2.MeshPacket(id=self.next_id(), to=to, channel=channel, rx_time=int(time.time()),
                                         rx_snr=snr, rx_rssi=-70 - hops * 12, hop_start=3, hop_limit=3 - hops,
                                         pki_encrypted=pki)
            setattr(packet, "from", sender)
            packet.decoded.portnum = port
            packet.decoded.payload = payload
            packet.decoded.request_id = request_id
            self.send(mesh_pb2.FromRadio(packet=packet))
        threading.Timer(delay, run).start()

    def ack(self, sender, request_id, error=mesh_pb2.Routing.NONE, delay=0.4):
        self.deliver(sender, MY_NUM, portnums_pb2.ROUTING_APP,
                     mesh_pb2.Routing(error_reason=error).SerializeToString(), request_id=request_id, delay=delay)

    def on_packet(self, packet):
        port = packet.decoded.portnum
        dest = packet.to
        known = {n[0]: n for n in NODES}
        if port == portnums_pb2.ADMIN_APP:
            self.on_admin(packet)
            return
        if port == portnums_pb2.TEXT_MESSAGE_APP:
            if dest == BROADCAST:
                self.ack(MY_NUM, packet.id, delay=0.8)  # relayed by a neighbour: implicit ack
            elif dest in known:
                if known[dest][1] == "F0X":
                    self.ack(MY_NUM, packet.id, error=mesh_pb2.Routing.MAX_RETRANSMIT, delay=3.0)
                else:
                    self.ack(dest, packet.id, delay=1.0)
                    self.deliver(dest, MY_NUM, port, random.choice(REPLIES).encode(), hops=known[dest][5],
                                 delay=3.0, pki=True)
            else:
                self.ack(MY_NUM, packet.id, error=mesh_pb2.Routing.NO_ROUTE, delay=1.0)
        elif port == portnums_pb2.TRACEROUTE_APP and dest in known:
            hops = known[dest][5]
            route = mesh_pb2.RouteDiscovery()
            relays = [0xA1B2C303, 0xA1B2C304][:hops]
            route.route.extend(relays)
            route.snr_towards.extend([22, -14, 9][:hops + 1])
            route.route_back.extend(list(reversed(relays)))
            route.snr_back.extend([18, -9, 25][:hops + 1])
            self.deliver(dest, MY_NUM, port, route.SerializeToString(), request_id=packet.id, hops=hops, delay=2.0)
        elif port == portnums_pb2.POSITION_APP and dest in known:
            for node in self.other_nodes():
                if node.num == dest:
                    self.deliver(dest, MY_NUM, port, node.position.SerializeToString(), request_id=packet.id, delay=1.5)
        elif port == portnums_pb2.TELEMETRY_APP and dest in known:
            for node in self.other_nodes():
                if node.num == dest:
                    t = telemetry_pb2.Telemetry(time=int(time.time()), device_metrics=node.device_metrics)
                    self.deliver(dest, MY_NUM, port, t.SerializeToString(), request_id=packet.id, delay=1.5)

    def on_admin(self, packet):
        admin = admin_pb2.AdminMessage()
        admin.ParseFromString(packet.decoded.payload)
        which = admin.WhichOneof("payload_variant")
        if which == "set_owner":
            self.owner_long = admin.set_owner.long_name
            self.owner_short = admin.set_owner.short_name
        elif which == "set_config":
            name = admin.set_config.WhichOneof("payload_variant")
            self.configs[name] = getattr(admin.set_config, name)
        elif which == "set_module_config":
            name = admin.set_module_config.WhichOneof("payload_variant")
            self.modules[name] = getattr(admin.set_module_config, name)
        elif which == "set_channel":
            self.channels[admin.set_channel.index] = admin.set_channel
        print("admin:", which, flush=True)
        self.ack(MY_NUM, packet.id, delay=0.1)

    # ---------------------------------------------------------- activity
    def background(self):
        time.sleep(self.args.chatter_delay)
        for sender, text in CHATTER:
            if self.args.quiet_mesh:
                return
            hops = {n[0]: n[5] for n in NODES}[sender]
            self.deliver(sender, BROADCAST, portnums_pb2.TEXT_MESSAGE_APP, text.encode(), hops=hops,
                         snr=random.choice([6.5, -3.25, 9.0]))
            time.sleep(self.args.chatter_gap)
        stats = telemetry_pb2.Telemetry(time=int(time.time()))
        stats.local_stats.num_packets_rx = 412
        stats.local_stats.num_packets_rx_bad = 9
        stats.local_stats.num_packets_tx = 57
        stats.local_stats.num_tx_relay = 21
        stats.local_stats.num_online_nodes = 6
        stats.local_stats.num_total_nodes = 9
        stats.local_stats.noise_floor = -104
        self.deliver(MY_NUM, MY_NUM, portnums_pb2.TELEMETRY_APP, stats.SerializeToString())
        self.deliver(0xA1B2C302, MY_NUM, portnums_pb2.TEXT_MESSAGE_APP, b"Hey, are you bringing the antenna tonight?",
                     delay=1.0, pki=True)

    def serve(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", self.args.port))
        server.listen(1)
        print("fake radio listening on 127.0.0.1:%d" % self.args.port, flush=True)
        started = False
        while True:
            conn, _ = server.accept()
            with self.lock:
                self.conn = conn
            if not started:
                started = True
                threading.Thread(target=self.background, daemon=True).start()
            buffer = b""
            while True:
                try:
                    chunk = conn.recv(4096)
                except OSError:
                    chunk = b""
                if not chunk:
                    break
                buffer += chunk
                while True:
                    start = buffer.find(b"\x94\xc3")
                    if start < 0 or len(buffer) < start + 4:
                        break
                    length = struct.unpack(">H", buffer[start + 2:start + 4])[0]
                    if len(buffer) < start + 4 + length:
                        break
                    frame = buffer[start + 4:start + 4 + length]
                    buffer = buffer[start + 4 + length:]
                    msg = mesh_pb2.ToRadio()
                    msg.ParseFromString(frame)
                    which = msg.WhichOneof("payload_variant")
                    if which == "want_config_id":
                        self.send_config(msg.want_config_id)
                    elif which == "packet":
                        self.on_packet(msg.packet)
            with self.lock:
                self.conn = None
            conn.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=4404)
    parser.add_argument("--quiet-mesh", action="store_true", help="do not generate channel chatter")
    parser.add_argument("--no-region", action="store_true", help="start with the LoRa region unset")
    parser.add_argument("--chatter-delay", type=float, default=2.0)
    parser.add_argument("--chatter-gap", type=float, default=0.7)
    Radio(parser.parse_args()).serve()

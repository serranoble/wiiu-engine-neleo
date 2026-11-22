#!/usr/bin/env python3
"""
Local Dual TCP + UDP Test Server for Wii U Engine (engine_net).

Runs on Windows/Linux/macOS with zero external dependencies so you can test:
- Real Nintendo Wii U console on your LAN (connect to the printed LAN IPv4).
- Cemu emulator on the same Windows PC (connect to 127.0.0.1).

Protocols:
- TCP (default 7777): length-prefixed [uint16_t BE len][payload] control/handshake channel.
- UDP (default 7778): WUN1 binary protocol (16-byte Big-Endian header + WorldSnapshot payload)
  with sliding ACK bitfield, RTT echo, simulated remote entity orbit, and optional
  artificial latency/packet-loss injection.
"""

from __future__ import annotations

import argparse
import heapq
import math
import random
import select
import socket
import struct
import sys
import time
from dataclasses import dataclass, field

MAGIC = 0x57554E31  # "WUN1"
VERSION = 1
HEADER_STRUCT = struct.Struct("!IBBHHIH")  # 4+1+1+2+2+4+2 = 16 bytes
SNAPSHOT_STRUCT = struct.Struct("!IHfffffffffff")  # 4+2+11*4 = 50 bytes

MSG_HEARTBEAT = 0
MSG_PING = 1
MSG_PONG = 2
MSG_CLIENT_HELLO = 3
MSG_SERVER_WELCOME = 4
MSG_INPUT_COMMAND = 5
MSG_WORLD_SNAPSHOT = 6
MSG_CONTROL_TEXT = 7
MSG_DISCONNECT = 8


def seq_gt(s1: int, s2: int) -> bool:
    return ((s1 > s2) and ((s1 - s2) <= 32768)) or ((s1 < s2) and ((s2 - s1) > 32768))


def detect_lan_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"


@dataclass
class UdpPeer:
    addr: tuple[str, int]
    server_seq: int = 0
    remote_seq: int = 0
    ack_bitfield: int = 0
    has_remote_seq: bool = False
    last_seen: float = field(default_factory=time.monotonic)
    packets_rx: int = 0
    packets_tx: int = 0

    def update_ack(self, incoming_seq: int) -> None:
        if not self.has_remote_seq:
            self.remote_seq = incoming_seq
            self.ack_bitfield = 0
            self.has_remote_seq = True
            return
        if seq_gt(incoming_seq, self.remote_seq):
            diff = (incoming_seq - self.remote_seq) & 0xFFFF
            if diff <= 32:
                self.ack_bitfield = ((self.ack_bitfield << diff) | (1 << (diff - 1))) & 0xFFFFFFFF
            else:
                self.ack_bitfield = 0
            self.remote_seq = incoming_seq
        elif incoming_seq != self.remote_seq:
            diff = (self.remote_seq - incoming_seq) & 0xFFFF
            if 1 <= diff <= 32:
                self.ack_bitfield |= (1 << (diff - 1))


@dataclass
class TcpPeer:
    sock: socket.socket
    addr: tuple[str, int]
    client_id: int
    rx_buf: bytearray = field(default_factory=bytearray)


def build_remote_entity_snapshot(now_sec: float, tick: int) -> bytes:
    """Simulates a deterministic remote entity orbiting at radius 4.0m for interpolation testing."""
    angle = now_sec * 1.2
    radius = 4.0
    px = math.cos(angle) * radius
    py = 1.0 + 0.35 * math.sin(now_sec * 2.4)
    pz = math.sin(angle) * radius
    vx = -math.sin(angle) * radius * 1.2
    vy = 0.35 * 2.4 * math.cos(now_sec * 2.4)
    vz = math.cos(angle) * radius * 1.2
    half_yaw = -angle * 0.5
    qx, qy, qz, qw = 0.0, math.sin(half_yaw), 0.0, math.cos(half_yaw)
    return SNAPSHOT_STRUCT.pack(1001, tick & 0xFFFF, float(now_sec), px, py, pz, qx, qy, qz, qw, vx, vy, vz)


def send_tcp_frame(sock: socket.socket, payload: bytes) -> None:
    frame = struct.pack("!H", len(payload)) + payload
    sock.sendall(frame)


def run_server(bind_ip: str, tcp_port: int, udp_port: int, latency_ms: float, loss_pct: float, tick_hz: float) -> int:
    tcp_listen = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_listen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp_listen.bind((bind_ip, tcp_port))
    tcp_listen.listen(8)
    tcp_listen.setblocking(False)

    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind((bind_ip, udp_port))
    udp_sock.setblocking(False)

    lan_ip = detect_lan_ip()
    print("=" * 68)
    print("  Wii U Engine — Local Dual TCP/UDP Test Server (Cap. 20 Gregory)")
    print("=" * 68)
    print(f"  TCP Control Port : {tcp_port} (listen {bind_ip})")
    print(f"  UDP Realtime Port: {udp_port} (listen {bind_ip})")
    print(f"  For Cemu (PC)    : 127.0.0.1")
    print(f"  For Wii U (LAN)  : {lan_ip}")
    print(f"  Simulation       : latency={latency_ms:.1f} ms | loss={loss_pct:.1f}% | tick={tick_hz:.0f} Hz")
    print("  Press Ctrl+C to stop.")
    print("=" * 68)

    start_time = time.monotonic()
    last_tick_time = start_time
    tick_interval = 1.0 / max(1.0, tick_hz)
    server_tick = 0
    next_client_id = 1

    udp_peers: dict[tuple[str, int], UdpPeer] = {}
    tcp_peers: dict[socket.socket, TcpPeer] = {}
    # Min-heap of (deliver_at_monotonic, addr, raw_bytes) for artificial latency injection
    delayed_udp_queue: list[tuple[float, tuple[str, int], bytes]] = []

    def enqueue_udp(addr: tuple[str, int], payload: bytes) -> None:
        if loss_pct > 0.0 and random.random() * 100.0 < loss_pct:
            return
        if latency_ms <= 0.0:
            try:
                udp_sock.sendto(payload, addr)
            except OSError:
                pass
        else:
            deliver_at = time.monotonic() + (latency_ms / 1000.0)
            heapq.heappush(delayed_udp_queue, (deliver_at, addr, payload))

    try:
        while True:
            rlist = [tcp_listen, udp_sock, *tcp_peers.keys()]
            readable, _, _ = select.select(rlist, [], [], 0.005)
            now = time.monotonic()
            elapsed = now - start_time

            # Flush delayed UDP packets
            while delayed_udp_queue and delayed_udp_queue[0][0] <= now:
                _, dst_addr, pkt = heapq.heappop(delayed_udp_queue)
                try:
                    udp_sock.sendto(pkt, dst_addr)
                except OSError:
                    pass

            for s in readable:
                if s is tcp_listen:
                    conn, addr = tcp_listen.accept()
                    conn.setblocking(False)
                    cid = next_client_id
                    next_client_id += 1
                    tcp_peers[conn] = TcpPeer(sock=conn, addr=addr, client_id=cid)
                    print(f"[TCP+] Client #{cid} connected from {addr[0]}:{addr[1]}")
                    try:
                        send_tcp_frame(conn, f"WELCOME client_id={cid} udp_port={udp_port}".encode("utf-8"))
                    except OSError:
                        pass

                elif s is udp_sock:
                    while True:
                        try:
                            data, addr = udp_sock.recvfrom(2048)
                        except BlockingIOError:
                            break
                        except OSError:
                            break
                        if len(data) < HEADER_STRUCT.size:
                            continue
                        magic, ver, msg_type, seq, ack, ack_bits, client_tick = HEADER_STRUCT.unpack_from(data, 0)
                        if magic != MAGIC or ver != VERSION:
                            continue

                        peer = udp_peers.get(addr)
                        if peer is None:
                            peer = UdpPeer(addr=addr)
                            udp_peers[addr] = peer
                            print(f"[UDP+] Peer registered: {addr[0]}:{addr[1]}")

                        peer.last_seen = now
                        peer.packets_rx += 1
                        peer.update_ack(seq)

                        if msg_type == MSG_DISCONNECT:
                            print(f"[UDP-] Peer disconnected: {addr[0]}:{addr[1]} (rx={peer.packets_rx} tx={peer.packets_tx})")
                            udp_peers.pop(addr, None)
                            continue

                        reply_type = MSG_SERVER_WELCOME if msg_type == MSG_CLIENT_HELLO else MSG_PONG
                        peer.server_seq = 1 if peer.server_seq >= 0xFFFF else (peer.server_seq + 1)
                        reply_hdr = HEADER_STRUCT.pack(
                            MAGIC, VERSION, reply_type, peer.server_seq,
                            peer.remote_seq, peer.ack_bitfield, server_tick & 0xFFFF
                        )
                        peer.packets_tx += 1
                        enqueue_udp(addr, reply_hdr)

                else:
                    peer = tcp_peers.get(s)
                    if not peer:
                        continue
                    try:
                        chunk = s.recv(4096)
                    except BlockingIOError:
                        continue
                    except OSError:
                        chunk = b""

                    if not chunk:
                        print(f"[TCP-] Client #{peer.client_id} disconnected ({peer.addr[0]}:{peer.addr[1]})")
                        s.close()
                        tcp_peers.pop(s, None)
                        continue

                    peer.rx_buf.extend(chunk)
                    while len(peer.rx_buf) >= 2:
                        msg_len = struct.unpack_from("!H", peer.rx_buf, 0)[0]
                        if len(peer.rx_buf) < 2 + msg_len:
                            break
                        body = bytes(peer.rx_buf[2:2 + msg_len])
                        del peer.rx_buf[:2 + msg_len]
                        text = body.decode("utf-8", errors="replace")
                        print(f"[TCP#{peer.client_id}] RX ({msg_len}B): {text}")
                        try:
                            send_tcp_frame(s, f"ECHO:{text}".encode("utf-8"))
                        except OSError:
                            pass

            # Periodic authoritative snapshot broadcast over UDP
            if now - last_tick_time >= tick_interval:
                last_tick_time = now
                server_tick = (server_tick + 1) & 0xFFFF
                snap_payload = build_remote_entity_snapshot(elapsed, server_tick)

                stale = [addr for addr, p in udp_peers.items() if (now - p.last_seen) > 10.0]
                for addr in stale:
                    print(f"[UDP~] Peer timed out: {addr[0]}:{addr[1]}")
                    udp_peers.pop(addr, None)

                for peer in udp_peers.values():
                    peer.server_seq = 1 if peer.server_seq >= 0xFFFF else (peer.server_seq + 1)
                    hdr = HEADER_STRUCT.pack(
                        MAGIC, VERSION, MSG_WORLD_SNAPSHOT, peer.server_seq,
                        peer.remote_seq, peer.ack_bitfield, server_tick
                    )
                    peer.packets_tx += 1
                    enqueue_udp(peer.addr, hdr + snap_payload)

    except KeyboardInterrupt:
        print("\n[SERVER] Shutting down cleanly...")
    finally:
        for conn in list(tcp_peers.keys()):
            conn.close()
        tcp_listen.close()
        udp_sock.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Wii U Engine Local Dual TCP+UDP Test Server")
    parser.add_argument("--bind", default="0.0.0.0", help="Bind IP address (default: 0.0.0.0)")
    parser.add_argument("--tcp-port", type=int, default=7777, help="TCP control port (default: 7777)")
    parser.add_argument("--udp-port", type=int, default=7778, help="UDP realtime port (default: 7778)")
    parser.add_argument("--latency-ms", type=float, default=0.0, help="Artificial one-way UDP latency in ms")
    parser.add_argument("--loss-pct", type=float, default=0.0, help="Artificial UDP packet loss percentage (0-100)")
    parser.add_argument("--tick-hz", type=float, default=20.0, help="Snapshot broadcast rate in Hz (default: 20)")
    args = parser.parse_args()
    return run_server(args.bind, args.tcp_port, args.udp_port, args.latency_ms, args.loss_pct, args.tick_hz)


if __name__ == "__main__":
    sys.exit(main())

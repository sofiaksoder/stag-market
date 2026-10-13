"""Connects to the gateway's WebSocket port like a browser would.

    python3 bots/ws_probe.py [ws_port] [seconds]

Checks the handshake, reads the opening snapshot, asks for another one,
and verifies the feed seqs are contiguous.
"""
import base64
import hashlib
import os
import select
import socket
import struct
import sys
import time

DELTA, TRADE, SNAPSHOT, SNAPSHOT_REQ = 20, 21, 22, 23
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def client_frame(opcode, payload):
    mask = os.urandom(4)
    body = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes([0x80 | opcode, 0x80 | len(payload)]) + mask + body


def connect(port):
    s = socket.create_connection(("127.0.0.1", port))
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
               f"Sec-WebSocket-Version: 13\r\n\r\n").encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        resp += s.recv(1024)
    head, rest = resp.split(b"\r\n\r\n", 1)
    want = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    assert b"101" in head.split(b"\r\n")[0], head
    assert want.encode() in head, "bad accept key"
    return s, rest


def messages(buf):
    """Pulls whole server frames (unmasked) out of buf."""
    out = []
    while len(buf) >= 2:
        n, at = buf[1] & 0x7F, 2
        if n == 126:
            if len(buf) < 4:
                break
            n, at = struct.unpack_from(">H", buf, 2)[0], 4
        if len(buf) < at + n:
            break
        out.append((buf[0] & 0x0F, buf[at : at + n]))
        buf = buf[at + n :]
    return out, buf


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9002
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 5
    s, buf = connect(port)
    s.sendall(client_frame(0x9, b"hi"))  # ping
    snapshots, trades, deltas, last, bad, pong = 0, 0, 0, None, 0, False
    asked = False
    end = time.time() + seconds
    while time.time() < end:
        if select.select([s], [], [], 0.1)[0]:
            data = s.recv(65536)
            if not data:
                break
            buf += data
        msgs, buf = messages(buf)
        for op, payload in msgs:
            if op == 0xA:
                pong = payload == b"hi"
                continue
            kind = payload[0]
            if kind == SNAPSHOT:
                snapshots += 1
                last = struct.unpack_from("<Q", payload, 1)[0]
            elif kind in (DELTA, TRADE):
                seq = struct.unpack_from("<Q", payload, 1)[0]
                if last is not None and seq != last + 1:
                    bad += 1
                last = seq
                deltas += kind == DELTA
                trades += kind == TRADE
        if snapshots and not asked:
            asked = True
            s.sendall(client_frame(0x2, bytes([SNAPSHOT_REQ])))
    print(f"snapshots {snapshots}, deltas {deltas}, trades {trades}, "
          f"seq errors {bad}, pong {'ok' if pong else 'missing'}")


if __name__ == "__main__":
    main()

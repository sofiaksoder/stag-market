"""Market-data client with the gap handling the UI needs.

    python3 bots/md_client.py [md_port] [seconds] [--lossy]

IN_SYNC: every message has seq = last + 1.
GAP: a seq was skipped, so the book is frozen and a snapshot is requested.
RECOVERING: waiting for the snapshot, buffering deltas meanwhile.
--lossy throws away some messages to exercise the recovery.
"""
import random
import select
import socket
import struct
import sys
import time

DELTA, TRADE, SNAPSHOT, SNAPSHOT_REQ = 20, 21, 22, 23
IN_SYNC, GAP, RECOVERING = "IN SYNC", "SEQ GAP", "RECOVERING"


class Book:
    def __init__(self):
        self.bids, self.asks = {}, {}

    def apply(self, side, px, size):
        levels = self.bids if side == 0 else self.asks
        if size:
            levels[px] = size
        else:
            levels.pop(px, None)

    def best(self):
        return (max(self.bids) if self.bids else None, min(self.asks) if self.asks else None)


class MdClient:
    def __init__(self, port, lossy=False):
        self.sock = socket.create_connection(("127.0.0.1", port))
        self.rx = b""
        self.book = Book()
        self.state = RECOVERING  # the gateway sends a snapshot on connect
        self.seq = 0
        self.buffered = []
        self.trades = 0
        self.gaps = 0
        self.msgs = 0
        self.lossy = lossy
        self.rng = random.Random(1)

    def poll(self, timeout):
        ready, _, _ = select.select([self.sock], [], [], timeout)
        if not ready:
            return
        data = self.sock.recv(65536)
        if not data:
            raise ConnectionError("gateway closed")
        self.rx += data
        while len(self.rx) >= 3:
            (n,) = struct.unpack_from("<H", self.rx)
            if len(self.rx) < 2 + n:
                break
            kind, body = self.rx[2], self.rx[3 : 2 + n]
            self.rx = self.rx[2 + n :]
            self.on_message(kind, body)

    def on_message(self, kind, body):
        if kind == SNAPSHOT:
            self.on_snapshot(body)
            return
        if self.lossy and self.state == IN_SYNC and self.rng.random() < 0.002:
            return  # pretend the network ate it
        seq = struct.unpack_from("<Q", body)[0]
        self.msgs += 1
        if self.state == IN_SYNC and seq != self.seq + 1:
            self.state = GAP
            self.gaps += 1
            self.sock.sendall(struct.pack("<HB", 1, SNAPSHOT_REQ))
            self.state = RECOVERING
        if self.state == RECOVERING:
            self.buffered.append((seq, kind, body))
        else:
            self.apply(seq, kind, body)

    def apply(self, seq, kind, body):
        self.seq = seq
        if kind == DELTA:
            _, side, px, size = struct.unpack("<QBqI", body)
            self.book.apply(side, px, size)
        elif kind == TRADE:
            self.trades += 1

    def on_snapshot(self, body):
        seq, nb, na = struct.unpack_from("<QHH", body)
        book = Book()
        off = 12
        for side, count in ((0, nb), (1, na)):
            for _ in range(count):
                px, size = struct.unpack_from("<qI", body, off)
                off += 12
                book.apply(side, px, size)
        self.book = book
        self.seq = seq
        # Replay what arrived while we waited, skipping anything the snapshot already has.
        pending, self.buffered = self.buffered, []
        self.state = IN_SYNC
        for s, kind, b in sorted(pending):
            if s <= self.seq:
                continue
            if s != self.seq + 1:
                self.state = RECOVERING  # still a hole, ask again
                self.buffered = [x for x in pending if x[0] > self.seq]
                self.sock.sendall(struct.pack("<HB", 1, SNAPSHOT_REQ))
                return
            self.apply(s, kind, b)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    port = int(args[0]) if args else 9001
    seconds = float(args[1]) if len(args) > 1 else 10
    c = MdClient(port, lossy="--lossy" in sys.argv)
    end = time.time() + seconds
    while time.time() < end:
        c.poll(0.1)
    bid, ask = c.book.best()
    print(f"{c.state}, seq {c.seq}, {c.msgs} msgs, {c.trades} trades, {c.gaps} gaps, "
          f"best bid {bid and bid / 100}, best ask {ask and ask / 100}")


if __name__ == "__main__":
    main()

"""Random market bot for the STAG gateway.

Mimics the flow from the design prototype: mostly limit orders near a
drifting fair price, some cancels, some market orders.

    python3 bots/bot.py [port] [seconds]
"""
import random
import select
import socket
import struct
import sys
import time

REF = 14250  # 142.50 in ticks
NEW, CANCEL, ACK, FILL = 1, 2, 10, 11
BUY, SELL = 0, 1
LIMIT, MARKET = 0, 1


def frame(msg_type, body):
    return struct.pack("<HB", len(body) + 1, msg_type) + body


def new_order(cl_id, side, kind, px, qty):
    return frame(NEW, struct.pack("<QBBqI", cl_id, side, kind, px, qty))


def cancel(order_id):
    return frame(CANCEL, struct.pack("<Q", order_id))


class Bot:
    def __init__(self, port, seed=None):
        self.rng = random.Random(seed)
        self.sock = socket.create_connection(("127.0.0.1", port))
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.rx = b""
        self.next_cl = 1
        self.pending = {}  # cl_id -> (side, kind, px, qty)
        self.open = {}  # order id -> remaining qty
        self.fair = REF
        self.bias = 0.0
        self.fills = 0
        self.rejects = 0

    def send(self, data):
        self.sock.sendall(data)

    def step(self):
        # Slow mean-reverting drift, like the prototype.
        self.bias = self.bias * 0.95 + self.rng.gauss(0, 0.6)
        self.fair += self.bias + self.rng.gauss(0, 1.0)
        self.fair += (REF - self.fair) * 0.01

        for _ in range(self.rng.randint(1, 3)):
            r = self.rng.random()
            if r < 0.52:
                self.add_limit()
            elif r < 0.78:
                self.cancel_one()
            else:
                self.add_market()

    def add_limit(self):
        side = self.rng.choice([BUY, SELL])
        off = self.rng.randint(-1, 6)
        px = round(self.fair) - off if side == BUY else round(self.fair) + off
        self.place(side, LIMIT, max(px, 1), self.rng.choice([100, 200, 300, 500]))

    def add_market(self):
        self.place(self.rng.choice([BUY, SELL]), MARKET, 0, self.rng.choice([100, 200, 300]))

    def place(self, side, kind, px, qty):
        cl = self.next_cl
        self.next_cl += 1
        self.pending[cl] = (side, kind, px, qty)
        self.send(new_order(cl, side, kind, px, qty))

    def cancel_one(self):
        if self.open:
            self.send(cancel(self.rng.choice(list(self.open))))

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
            self.handle(kind, body)

    def handle(self, kind, body):
        if kind == ACK:
            cl, oid, status, reason, seq = struct.unpack("<QQBBQ", body)
            if status == 0:
                side, k, px, qty = self.pending.pop(cl)
                if k == LIMIT:
                    self.open[oid] = qty
            elif status == 1:
                self.open.pop(oid, None)
            else:
                self.pending.pop(cl, None)
                self.rejects += 1
        elif kind == FILL:
            oid, px, qty, role, seq, trade_seq = struct.unpack("<QqIBQQ", body)
            self.fills += 1
            if oid in self.open:
                self.open[oid] -= qty
                if self.open[oid] <= 0:
                    del self.open[oid]


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
    bot = Bot(port)
    end = time.time() + seconds
    step_s = 0.02
    next_step = time.time()
    while time.time() < end:
        if time.time() >= next_step:
            bot.step()
            next_step += step_s
        bot.poll(0.005)
    bot.poll(0.2)
    print(f"orders sent {bot.next_cl - 1}, fills {bot.fills}, "
          f"rejects {bot.rejects}, open {len(bot.open)}, fair {bot.fair / 100:.2f}")


if __name__ == "__main__":
    main()

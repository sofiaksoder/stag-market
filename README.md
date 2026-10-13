# Stag Market

A C++20 limit order book exchange for one fictional instrument, `STAG`, driven by Python bots and watched through a web UI. The engine is the product; the UI is a thin client.

Prices are integer ticks (1 tick = $0.01). The session reference price is 142.50.

## Results

From `stag_bench` on an Intel i5-8257U laptop, Release build, one thread, 200,000 orders, best of 3 runs. Clock reads (about 59 ns) are inside every figure.

| Op | p50 | p99 |
| --- | --- | --- |
| add (resting) | 169 ns | 387 ns |
| add (match + fill) | 182 ns | 412 ns |
| cancel | 257 ns | 491 ns |

About 5.7M ops/s with adds, matches and cancels mixed. One resting order over loopback TCP, send to ack, takes about 35 µs at p50, so the engine itself (decode, book, encode, publish) is around 1% of that. The rest is the kernel and the socket.

What each change bought, measured on the same workload:

| # | Change | Add p50 | Throughput | vs. baseline |
| --- | --- | --- | --- | --- |
| 1 | `std::map<Price, std::list<Order>>` | 269 ns | 2.1M/s | 1.0× |
| 2 | flat level array + intrusive FIFO + order pool | 174 ns | 3.0M/s | 1.4× |
| 3 | open-addressed id map instead of `std::unordered_map` | 169 ns | 5.7M/s | 2.7× |

The same numbers, a latency histogram and the wire-to-wire breakdown are on the Performance page of the web UI.

## Architecture

```
Python bot ──TCP──▶ ┌──────────────────────────────┐ ──TCP────▶ market-data client
Browser    ──WS───▶ │ gateway: poll loop, decode,  │ ──WS─────▶ Browser
                    │ risk check, exchange, book,  │
                    │ market-data publisher        │
                    └──────────────┬───────────────┘
                                   ▼
                          journal (append-only) ──▶ replay
```

It's all one thread for now. The plan is separate pinned threads joined by lock-free rings (see Progress).

- **Book:** flat array of price levels per side, an intrusive FIFO per level, orders from a preallocated pool, price-time priority. Limit and market orders, cancel, modify.
- **Gateway:** binary protocol (`NewOrder`, `Cancel`, `Ack`, `Fill`) over TCP and WebSocket. Orders over 50,000 shares are rejected.
- **Market data:** every message has a seq that goes up by one. Level deltas carry the new size, trades come with the aggressor side, and a snapshot is sent on connect or on request. A client that sees a gap freezes its book, asks for a snapshot, and replays the deltas it buffered.
- **Journal:** every order, cancel and disconnect is written with its timestamp. Replaying a journal through a fresh exchange gives identical trades, book and seqs.

## Build and test

```bash
cmake -S . -B build -DSTAG_SANITIZE=ON && cmake --build build
./build/order_book_test && ./build/gateway_test && ./build/ws_test && ./build/journal_test
cd web && bun install && bun run test    # npm works too
```

## Run it

```bash
./build/stag_gateway 9000        # orders on 9000, market data on 9001, WebSocket on 9002
python3 bots/bot.py 9000 10      # trades for 10s
python3 bots/md_client.py 9001   # watches the feed (--lossy drops messages to test recovery)
python3 bots/ws_probe.py 9002    # same feed over WebSocket, like a browser
```

Web UI (React + Vite, needs the gateway running):

```bash
cd web && bun install && bun run dev
```

It opens on http://localhost:5173 and connects to `ws://127.0.0.1:9002` (override with `VITE_WS_URL`). Trade has the book, candles, ticket, orders and tape. Portfolio shows average-cost P&L.

Over WebSocket each feed message is one binary message (type byte plus body, no length prefix). A browser gets a snapshot after the handshake and can ask for another by sending the single byte `23`. The same socket is also an order session: send NewOrder (`1`) and Cancel (`2`) the same way and the Ack and Fill replies come back on it. A Fill carries the seq of its trade on the feed, which is how the UI marks your trades on the tape.

### Journal and replay

```bash
./build/stag_gateway 9000 live.journal                      # records every input as it runs
./build/stag_sim 42 2000 session.journal                    # or generate a seeded session
./build/stag_replay session.journal web/public/replay.json  # rebuild it, one snapshot per 500 ms
```

Then open the Replay page.

### Benchmark

Use a Release build, not the sanitizer one:

```bash
cmake -S . -B build-rel && cmake --build build-rel
./build-rel/stag_bench web/public/perf.json
```

It runs the book variants, then starts the gateway itself for the round-trip number. The Performance page reads `web/public/perf.json`.

## Layout

| Dir | Contents |
| --- | --- |
| `engine/` | order book and id index |
| `gateway/` | wire protocol, exchange glue, market-data publisher, journal, WebSocket, TCP server |
| `tools/` | seeded session generator and journal replay |
| `bench/` | benchmark and the `std::map` baseline book |
| `bots/` | Python bot, market-data client, WebSocket probe |
| `web/` | React + Vite UI |
| `tests/` | order book, gateway, WebSocket and journal tests |

## Progress

- [x] 1. Single-threaded order book + tests
- [x] 2. TCP gateway + Python bot
- [x] 3. Market-data publisher + WebSocket gateway
- [x] 4. Web UI: trade, portfolio, replay, performance
- [x] 5. Event journal and replay
- [x] 6. Benchmark and first optimization passes
- [ ] 7. Multithreading: pinned threads, SPSC rings, epoll on Linux
- [ ] 8. Replace the draft logo, add GoogleTest and Google Benchmark, kernel-bypass experiments

## Notes

- Tests use a small `CHECK` macro rather than GoogleTest, and the benchmark is plain `std::chrono`.
- The stag logo is a draft mark and should be redrawn before any public use.
- Simulated market. Not financial software.

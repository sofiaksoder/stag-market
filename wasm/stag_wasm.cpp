// The exchange and the seeded simulator behind a small C API, so the browser
// can run the real engine. The simulator plays the bots and the page plays
// the one human client.
//
// Everything the exchange says (the market-data feed and replies to the human)
// lands in an outbox of protocol frames. The page drains it after each call.
#include <cstdint>
#include <memory>

#include "exchange.hpp"
#include "sim.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define STAG_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define STAG_EXPORT extern "C"
#endif

using namespace stag;

namespace {

constexpr int HUMAN = 100;   // the sim uses connections 1 to 3

// Smaller than the server's, to keep browser memory modest.
const OrderBook::Config CFG{1, 100'000, 1 << 17};

std::unique_ptr<Sim>      sim;
std::unique_ptr<Exchange> ex;
proto::Buffer             outbox;

void emit(const proto::Buffer& frame) { outbox.insert(outbox.end(), frame.begin(), frame.end()); }

}  // namespace

// Starts a fresh market. The book is seeded and runs for `warmup` steps first.
STAG_EXPORT void stag_init(std::uint32_t seed, int warmup) {
    outbox.clear();
    sim = std::make_unique<Sim>(seed, 0);
    ex = std::make_unique<Exchange>(
        CFG,
        [](int conn, const proto::Buffer& b) { if (conn == HUMAN) emit(b); },
        [](const proto::Buffer& b) { emit(b); });
    sim->seed_book(*ex);
    for (int i = 0; i < warmup; ++i) sim->step(*ex);
    outbox.clear();   // nobody was watching yet
}

// The bots do `n` steps of trading.
STAG_EXPORT void stag_step(int n) {
    for (int i = 0; i < n; ++i) sim->step(*ex);
}

// One message from the page: a type byte then the body, like the WebSocket protocol.
// Returns 0 if it was understood.
STAG_EXPORT int stag_submit(const std::uint8_t* msg, int len) {
    if (len < 1) return 1;
    auto type = static_cast<proto::Msg>(msg[0]);
    if (type == proto::Msg::NewOrder) {
        proto::NewOrder m;
        if (!proto::decode(msg + 1, static_cast<std::size_t>(len - 1), m)) return 1;
        ex->on_new_order(HUMAN, m);
    } else if (type == proto::Msg::Cancel) {
        proto::Cancel m;
        if (!proto::decode(msg + 1, static_cast<std::size_t>(len - 1), m)) return 1;
        ex->on_cancel(HUMAN, m);
    } else if (type == proto::Msg::SnapshotReq && len == 1) {
        ex->snapshot(outbox);
    } else {
        return 1;
    }
    return 0;
}

STAG_EXPORT void stag_snapshot() { ex->snapshot(outbox); }

STAG_EXPORT const std::uint8_t* stag_outbox() { return outbox.data(); }
STAG_EXPORT int stag_outbox_size() { return static_cast<int>(outbox.size()); }
STAG_EXPORT void stag_outbox_clear() { outbox.clear(); }

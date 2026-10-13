// Runs the WebAssembly module's C API natively, so the contract is checked
// without a browser.
#include <cstdio>
#include <vector>

#include "protocol.hpp"

extern "C" {
void stag_init(std::uint32_t seed, int warmup);
void stag_step(int n);
int stag_submit(const std::uint8_t* msg, int len);
void stag_snapshot();
const std::uint8_t* stag_outbox();
int stag_outbox_size();
void stag_outbox_clear();
}

using namespace stag;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

struct Frame { std::uint8_t type; std::vector<std::uint8_t> body; };

// Splits the outbox into frames, the way the page does.
static std::vector<Frame> drain() {
    std::vector<Frame> out;
    const std::uint8_t* p = stag_outbox();
    int n = stag_outbox_size(), at = 0;
    while (at + 3 <= n) {
        std::uint16_t len;
        std::memcpy(&len, p + at, 2);
        CHECK(at + 2 + len <= n);
        out.push_back({p[at + 2], std::vector<std::uint8_t>(p + at + 3, p + at + 2 + len)});
        at += 2 + len;
    }
    CHECK(at == n);
    stag_outbox_clear();
    return out;
}

// What the page sends: type byte, then the body.
static std::vector<std::uint8_t> payload(const proto::Buffer& framed) {
    return std::vector<std::uint8_t>(framed.begin() + 2, framed.end());
}

static void test_snapshot_after_warmup() {
    stag_init(5, 100);
    CHECK(stag_outbox_size() == 0);   // warm-up output is thrown away
    stag_snapshot();
    auto f = drain();
    CHECK(f.size() == 1 && f[0].type == static_cast<std::uint8_t>(proto::Msg::Snapshot));
    proto::Snapshot s;
    CHECK(proto::decode(f[0].body.data(), f[0].body.size(), s));
    CHECK(!s.bids.empty() && !s.asks.empty());
    CHECK(s.bids[0].price < s.asks[0].price);
}

static void test_feed_is_contiguous() {
    stag_init(5, 50);
    stag_snapshot();
    auto snap = drain();
    proto::Snapshot s;
    proto::decode(snap[0].body.data(), snap[0].body.size(), s);

    stag_step(200);
    std::uint64_t seq = s.seq;
    int md = 0;
    for (auto& f : drain()) {
        std::uint64_t this_seq = 0;
        std::memcpy(&this_seq, f.body.data(), 8);   // delta and trade both start with seq
        CHECK(this_seq == seq + 1);
        seq = this_seq;
        ++md;
    }
    CHECK(md > 100);
}

static void test_human_order_round_trip() {
    stag_init(5, 50);
    stag_outbox_clear();
    // A buy far under the market rests, and we get an ack plus a level delta.
    proto::Buffer b;
    proto::encode(b, proto::NewOrder{7, Side::Buy, proto::OrderType::Limit, 5000, 100});
    auto msg = payload(b);
    CHECK(stag_submit(msg.data(), static_cast<int>(msg.size())) == 0);
    auto f = drain();
    proto::Ack ack{};
    bool got_ack = false, got_delta = false;
    for (auto& fr : f) {
        if (fr.type == static_cast<std::uint8_t>(proto::Msg::Ack)) got_ack = proto::decode(fr.body.data(), fr.body.size(), ack);
        if (fr.type == static_cast<std::uint8_t>(proto::Msg::L2Delta)) got_delta = true;
    }
    CHECK(got_ack && ack.cl_ord_id == 7 && ack.status == proto::AckStatus::New);
    CHECK(got_delta);

    // Cancelling it works, and cancelling again is rejected.
    b.clear();
    proto::encode(b, proto::Cancel{ack.id});
    msg = payload(b);
    CHECK(stag_submit(msg.data(), static_cast<int>(msg.size())) == 0);
    bool canceled = false;
    for (auto& fr : drain())
        if (fr.type == static_cast<std::uint8_t>(proto::Msg::Ack)) {
            proto::Ack a;
            proto::decode(fr.body.data(), fr.body.size(), a);
            canceled = a.status == proto::AckStatus::Canceled;
        }
    CHECK(canceled);
}

static void test_market_order_fills_and_bad_input() {
    stag_init(5, 50);
    stag_outbox_clear();
    proto::Buffer b;
    proto::encode(b, proto::NewOrder{1, Side::Buy, proto::OrderType::Market, 0, 100});
    auto msg = payload(b);
    CHECK(stag_submit(msg.data(), static_cast<int>(msg.size())) == 0);
    int fills = 0;
    for (auto& fr : drain()) fills += fr.type == static_cast<std::uint8_t>(proto::Msg::Fill);
    CHECK(fills == 1);   // the human is the taker, the sim's resting order is not ours

    std::uint8_t junk[] = {99, 1, 2};
    CHECK(stag_submit(junk, 3) != 0);
    CHECK(stag_submit(junk, 0) != 0);
    std::uint8_t truncated[] = {static_cast<std::uint8_t>(proto::Msg::NewOrder), 1, 2};
    CHECK(stag_submit(truncated, 3) != 0);
}

int main() {
    test_snapshot_after_warmup();
    test_feed_is_contiguous();
    test_human_order_round_trip();
    test_market_order_fills_and_bad_input();
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::puts("all wasm api tests passed");
}

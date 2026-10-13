#include <cstdio>
#include <map>
#include <random>

#include "exchange.hpp"

using namespace stag;
using namespace stag::proto;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void test_roundtrip() {
    Buffer b;
    encode(b, NewOrder{7, Side::Sell, OrderType::Limit, 14250, 300});
    CHECK(b.size() == 2 + 1 + 22);
    std::uint16_t len; std::memcpy(&len, b.data(), 2);
    CHECK(len == 23 && b[2] == static_cast<std::uint8_t>(Msg::NewOrder));
    NewOrder m{};
    CHECK(decode(&b[3], len - 1, m));
    CHECK(m.cl_ord_id == 7 && m.side == Side::Sell && m.type == OrderType::Limit);
    CHECK(m.px == 14250 && m.qty == 300);
    CHECK(!decode(&b[3], len - 2, m));   // truncated

    b.clear();
    encode(b, Fill{9, 14248, 100, Role::Maker, 41, 77});
    Fill f{};
    CHECK(decode(&b[3], b.size() - 3, f));
    CHECK(f.id == 9 && f.px == 14248 && f.qty == 100 && f.role == Role::Maker && f.seq == 41 && f.trade_seq == 77);
}

// Collects what each connection was sent.
struct Rig {
    std::map<int, std::vector<Ack>>  acks;
    std::map<int, std::vector<Fill>> fills;
    Exchange ex{{1, 100'000, 1024}, [this](int c, const Buffer& b) {
        auto type = static_cast<Msg>(b[2]);
        if (type == Msg::Ack)  { Ack a;  decode(&b[3], b.size() - 3, a); acks[c].push_back(a); }
        if (type == Msg::Fill) { Fill f; decode(&b[3], b.size() - 3, f); fills[c].push_back(f); }
    }};
};

static void test_match_and_fills() {
    Rig r;
    r.ex.on_new_order(1, {10, Side::Sell, OrderType::Limit, 14250, 500});
    CHECK(r.acks[1].size() == 1 && r.acks[1][0].status == AckStatus::New && r.acks[1][0].cl_ord_id == 10);
    OrderId resting = r.acks[1][0].id;

    r.ex.on_new_order(2, {20, Side::Buy, OrderType::Limit, 14250, 200});
    CHECK(r.fills[1].size() == 1 && r.fills[1][0].role == Role::Maker && r.fills[1][0].id == resting);
    CHECK(r.fills[2].size() == 1 && r.fills[2][0].role == Role::Taker && r.fills[2][0].qty == 200);
    CHECK(r.fills[2][0].seq > r.acks[2][0].seq);
    CHECK(r.ex.book().depth_at(Side::Sell, 14250) == 300);
}

static void test_risk_and_rejects() {
    Rig r;
    r.ex.on_new_order(1, {1, Side::Buy, OrderType::Limit, 14250, MAX_QTY + 1});
    CHECK(r.acks[1].back().status == AckStatus::Rejected && r.acks[1].back().reason == Status::RiskLimit);
    r.ex.on_new_order(1, {2, Side::Buy, OrderType::Market, 0, 100});
    CHECK(r.acks[1].back().reason == Status::NoLiquidity);
    r.ex.on_new_order(1, {3, Side::Buy, OrderType::Limit, 0, 100});
    CHECK(r.acks[1].back().reason == Status::BadPrice);
    CHECK(r.ex.book().open_orders() == 0);
}

static void test_cancel_ownership() {
    Rig r;
    r.ex.on_new_order(1, {1, Side::Buy, OrderType::Limit, 14000, 100});
    OrderId id = r.acks[1][0].id;
    r.ex.on_cancel(2, {id});   // someone else's order
    CHECK(r.acks[2].back().status == AckStatus::Rejected && r.ex.book().open_orders() == 1);
    r.ex.on_cancel(1, {id});
    CHECK(r.acks[1].back().status == AckStatus::Canceled && r.ex.book().open_orders() == 0);
    r.ex.on_cancel(1, {id});
    CHECK(r.acks[1].back().reason == Status::UnknownId);
}

static void test_disconnect() {
    Rig r;
    r.ex.on_new_order(1, {1, Side::Buy, OrderType::Limit, 14000, 100});
    r.ex.on_new_order(1, {2, Side::Sell, OrderType::Limit, 14100, 100});
    r.ex.on_new_order(2, {3, Side::Buy, OrderType::Limit, 13900, 100});
    r.ex.on_disconnect(1);
    CHECK(r.ex.book().open_orders() == 1 && r.ex.book().best_bid() == 13900);
}

// A client's view of the book, built only from market data.
struct Mirror {
    std::map<Price, Qty> bids, asks;
    std::uint64_t seq = 0;
    int trades = 0;
    bool in_order = true;

    void on(const Buffer& b) {
        auto type = static_cast<Msg>(b[2]);
        if (type == Msg::L2Delta) {
            L2Delta d; decode(&b[3], b.size() - 3, d);
            in_order = in_order && d.seq == seq + 1;
            seq = d.seq;
            auto& side = d.side == Side::Buy ? bids : asks;
            if (d.size) side[d.px] = d.size; else side.erase(d.px);
        } else if (type == Msg::MdTrade) {
            MdTrade t; decode(&b[3], b.size() - 3, t);
            in_order = in_order && t.seq == seq + 1;
            seq = t.seq;
            ++trades;
        }
    }

    bool matches(const OrderBook& book) const {
        std::vector<LevelView> v;
        book.top_levels(Side::Buy, 100000, v);
        if (v.size() != bids.size()) return false;
        std::size_t i = v.size();
        for (auto& [px, q] : bids) { --i; if (v[i].price != px || v[i].qty != q) return false; }
        book.top_levels(Side::Sell, 100000, v);
        if (v.size() != asks.size()) return false;
        i = 0;
        for (auto& [px, q] : asks) { if (v[i].price != px || v[i].qty != q) { return false; } ++i; }
        return true;
    }
};

static void test_md_matches_book() {
    Mirror m;
    Exchange ex({1, 1000, 1 << 16}, [](int, const Buffer&) {}, [&](const Buffer& b) { m.on(b); });
    std::mt19937_64 rng(7);
    std::vector<std::pair<int, OrderId>> live;   // conn, id
    // Ids come out 1, 2, 3... so we can predict them.
    OrderId next = 1;
    for (int i = 0; i < 20000; ++i) {
        int conn = 1 + static_cast<int>(rng() % 3);
        unsigned r = rng() % 100;
        if (r < 60) {
            Side s = rng() & 1 ? Side::Buy : Side::Sell;
            ex.on_new_order(conn, {1, s, OrderType::Limit, static_cast<Price>(90 + rng() % 21), static_cast<Qty>(1 + rng() % 30)});
            live.push_back({conn, next++});
        } else if (r < 75) {
            Side s = rng() & 1 ? Side::Buy : Side::Sell;
            ex.on_new_order(conn, {1, s, OrderType::Market, 0, static_cast<Qty>(1 + rng() % 40)});
            ++next;   // consumed an id even if it was rejected
        } else if (r < 97 && !live.empty()) {
            auto [c, id] = live[rng() % live.size()];
            ex.on_cancel(c, {id});
        } else {
            ex.on_disconnect(conn);
        }
        if (!m.matches(ex.book())) { CHECK(false); return; }
    }
    CHECK(m.in_order && m.trades > 0);
}

static void test_snapshot() {
    Mirror m;
    Exchange ex({1, 1000, 1024}, [](int, const Buffer&) {}, [&](const Buffer& b) { m.on(b); });
    ex.on_new_order(1, {1, Side::Buy, OrderType::Limit, 100, 10});
    ex.on_new_order(1, {2, Side::Buy, OrderType::Limit, 99, 5});
    ex.on_new_order(1, {3, Side::Sell, OrderType::Limit, 103, 7});
    Buffer b;
    ex.snapshot(b);
    Snapshot s;
    CHECK(static_cast<Msg>(b[2]) == Msg::Snapshot && decode(&b[3], b.size() - 3, s));
    CHECK(s.seq == m.seq);
    CHECK(s.bids.size() == 2 && s.bids[0].price == 100 && s.bids[1].price == 99);
    CHECK(s.asks.size() == 1 && s.asks[0].price == 103 && s.asks[0].qty == 7);
}

static void test_fill_points_at_trade() {
    std::vector<std::uint64_t> trade_seqs;
    std::vector<Fill> fills;
    Exchange ex({1, 1000, 1024},
                [&](int, const Buffer& b) {
                    if (static_cast<Msg>(b[2]) == Msg::Fill) { Fill f; decode(&b[3], b.size() - 3, f); fills.push_back(f); }
                },
                [&](const Buffer& b) {
                    if (static_cast<Msg>(b[2]) == Msg::MdTrade) { MdTrade t; decode(&b[3], b.size() - 3, t); trade_seqs.push_back(t.seq); }
                });
    ex.on_new_order(1, {1, Side::Sell, OrderType::Limit, 100, 5});
    ex.on_new_order(1, {2, Side::Sell, OrderType::Limit, 101, 5});
    ex.on_new_order(2, {3, Side::Buy, OrderType::Limit, 101, 8});   // two trades
    CHECK(trade_seqs.size() == 2 && fills.size() == 4);
    CHECK(fills[0].trade_seq == trade_seqs[0] && fills[1].trade_seq == trade_seqs[0]);
    CHECK(fills[2].trade_seq == trade_seqs[1] && fills[3].trade_seq == trade_seqs[1]);
}

int main() {
    test_roundtrip();
    test_match_and_fills();
    test_risk_and_rejects();
    test_cancel_ownership();
    test_disconnect();
    test_md_matches_book();
    test_snapshot();
    test_fill_points_at_trade();
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::puts("all gateway tests passed");
}

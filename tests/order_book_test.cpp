#include <cstdio>
#include <cstdlib>
#include <map>
#include <deque>
#include <unordered_map>
#include <random>

#include "order_book.hpp"

using namespace stag;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static OrderBook::Config small_cfg() { return {1, 1000, 4096}; }

static void test_rest_and_best() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    CHECK(!b.has_bid() && !b.has_ask());
    CHECK(b.add_limit(1, Side::Buy, 100, 10, t) == Status::Ok);
    CHECK(b.add_limit(2, Side::Buy, 101, 5, t) == Status::Ok);
    CHECK(b.add_limit(3, Side::Sell, 110, 7, t) == Status::Ok);
    CHECK(t.empty());
    CHECK(b.best_bid() == 101 && b.best_ask() == 110);
    CHECK(b.depth_at(Side::Buy, 100) == 10);
}

static void test_full_and_partial_match() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    b.add_limit(1, Side::Sell, 100, 10, t);
    b.add_limit(2, Side::Buy, 100, 4, t);
    CHECK(t.size() == 1 && t[0].maker == 1 && t[0].taker == 2 && t[0].qty == 4 && t[0].price == 100);
    CHECK(b.depth_at(Side::Sell, 100) == 6);
    t.clear();
    b.add_limit(3, Side::Buy, 105, 10, t);          // takes 6, rests 4 at 105
    CHECK(t.size() == 1 && t[0].qty == 6 && t[0].price == 100);
    CHECK(!b.has_ask() && b.best_bid() == 105 && b.depth_at(Side::Buy, 105) == 4);
}

static void test_price_time_priority() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    b.add_limit(1, Side::Sell, 100, 5, t);
    b.add_limit(2, Side::Sell, 100, 5, t);
    b.add_limit(3, Side::Sell, 99, 5, t);
    b.add_limit(4, Side::Buy, 100, 12, t);
    CHECK(t.size() == 3);
    CHECK(t[0].maker == 3 && t[0].price == 99);     // better price first
    CHECK(t[1].maker == 1 && t[2].maker == 2);      // then FIFO
    CHECK(t[2].qty == 2 && b.depth_at(Side::Sell, 100) == 3);
}

static void test_cancel() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    b.add_limit(1, Side::Buy, 100, 5, t);
    b.add_limit(2, Side::Buy, 101, 5, t);
    CHECK(b.cancel(2) == Status::Ok);
    CHECK(b.best_bid() == 100);
    CHECK(b.cancel(2) == Status::UnknownId);
    CHECK(b.cancel(1) == Status::Ok && !b.has_bid() && b.open_orders() == 0);
}

static void test_modify() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    b.add_limit(1, Side::Sell, 100, 10, t);
    b.add_limit(2, Side::Sell, 100, 10, t);
    CHECK(b.modify(1, 100, 4, t) == Status::Ok);    // reduce keeps priority
    b.add_limit(3, Side::Buy, 100, 4, t);
    CHECK(t.size() == 1 && t[0].maker == 1);
    t.clear();
    b.add_limit(4, Side::Sell, 100, 5, t);          // queue: 2, 4
    CHECK(b.modify(2, 100, 20, t) == Status::Ok);   // increase loses priority
    b.add_limit(5, Side::Buy, 100, 5, t);
    CHECK(t.size() == 1 && t[0].maker == 4);
    t.clear();
    CHECK(b.modify(4, 100, 1, t) == Status::UnknownId);   // already filled
    CHECK(b.modify(2, 90, 20, t) == Status::Ok);    // price change that crosses nothing
    CHECK(b.best_ask() == 90);
}

static void test_validation() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    CHECK(b.add_limit(1, Side::Buy, 100, 0, t) == Status::BadQty);
    CHECK(b.add_limit(1, Side::Buy, 0, 1, t) == Status::BadPrice);
    CHECK(b.add_limit(1, Side::Buy, 1001, 1, t) == Status::BadPrice);
    CHECK(b.add_limit(1, Side::Buy, 100, 1, t) == Status::Ok);
    CHECK(b.add_limit(1, Side::Buy, 100, 1, t) == Status::DuplicateId);
    OrderBook tiny({1, 10, 2});
    CHECK(tiny.add_limit(1, Side::Buy, 5, 1, t) == Status::Ok);
    CHECK(tiny.add_limit(2, Side::Buy, 5, 1, t) == Status::Ok);
    CHECK(tiny.add_limit(3, Side::Buy, 5, 1, t) == Status::PoolExhausted);
    tiny.cancel(1);
    CHECK(tiny.add_limit(3, Side::Buy, 5, 1, t) == Status::Ok);   // slot reused
}

static void test_market() {
    OrderBook b(small_cfg()); std::vector<Trade> t;
    CHECK(b.add_market(1, Side::Buy, 5, t) == Status::NoLiquidity);
    b.add_limit(2, Side::Sell, 100, 5, t);
    b.add_limit(3, Side::Sell, 102, 5, t);
    CHECK(b.add_market(4, Side::Buy, 8, t) == Status::Ok);
    CHECK(t.size() == 2 && t[0].price == 100 && t[1].price == 102 && t[1].qty == 3);
    CHECK(b.open_orders() == 1 && b.depth_at(Side::Sell, 102) == 2);
    t.clear();
    CHECK(b.add_market(5, Side::Buy, 10, t) == Status::Ok);   // too big, leftover dropped
    CHECK(t.size() == 1 && !b.has_ask() && !b.has_bid() && b.open_orders() == 0);
    CHECK(b.add_market(6, Side::Sell, 0, t) == Status::BadQty);
}

// Naive reference: std::map of deques.
struct RefBook {
    struct O { OrderId id; Qty qty; };
    std::map<Price, std::deque<O>, std::greater<>> bids;
    std::map<Price, std::deque<O>> asks;
    std::map<OrderId, std::pair<Side, Price>> where;

    void add(OrderId id, Side s, Price p, Qty q, std::vector<Trade>& out) {
        if (s == Side::Buy) q = take(asks, id, p, q, out, true);
        else                q = take(bids, id, p, q, out, false);
        if (!q) return;
        if (s == Side::Buy) bids[p].push_back({id, q}); else asks[p].push_back({id, q});
        where[id] = {s, p};
    }
    template <class M>
    Qty take(M& m, OrderId id, Price lim, Qty q, std::vector<Trade>& out, bool buy) {
        while (q && !m.empty()) {
            auto it = m.begin();
            if (buy ? it->first > lim : it->first < lim) break;
            auto& dq = it->second;
            while (q && !dq.empty()) {
                Qty f = std::min(q, dq.front().qty);
                out.push_back({dq.front().id, id, it->first, f});
                dq.front().qty -= f; q -= f;
                if (!dq.front().qty) { where.erase(dq.front().id); dq.pop_front(); }
            }
            if (dq.empty()) m.erase(it);
        }
        return q;
    }
    void cancel(OrderId id) {
        auto w = where.find(id); if (w == where.end()) return;
        auto rm = [&](auto& m) {
            auto& dq = m[w->second.second];
            for (auto i = dq.begin(); i != dq.end(); ++i) if (i->id == id) { dq.erase(i); break; }
            if (dq.empty()) m.erase(w->second.second);
        };
        if (w->second.first == Side::Buy) rm(bids); else rm(asks);
        where.erase(w);
    }
};

template <class Book>
static void test_random_differential() {
    std::mt19937_64 rng(12345);
    Book b({1, 200, 1 << 16}); RefBook r;
    std::vector<OrderId> live; OrderId next = 1;
    for (int i = 0; i < 200000; ++i) {
        std::vector<Trade> t1, t2;
        if (live.empty() || rng() % 10 < 7) {
            OrderId id = next++;
            Side s = rng() & 1 ? Side::Buy : Side::Sell;
            Price p = 90 + rng() % 21; Qty q = 1 + rng() % 20;
            CHECK(b.add_limit(id, s, p, q, t1) == Status::Ok);
            r.add(id, s, p, q, t2);
            live.push_back(id);
        } else {
            size_t k = rng() % live.size();
            OrderId id = live[k]; live[k] = live.back(); live.pop_back();
            b.cancel(id); r.cancel(id);
        }
        if (t1.size() != t2.size()) { CHECK(false); return; }
        for (size_t j = 0; j < t1.size(); ++j)
            if (t1[j].maker != t2[j].maker || t1[j].taker != t2[j].taker ||
                t1[j].price != t2[j].price || t1[j].qty != t2[j].qty) { CHECK(false); return; }
        bool rb = !r.bids.empty(), ra = !r.asks.empty();
        if (b.has_bid() != rb || b.has_ask() != ra) { CHECK(false); return; }
        if (rb && b.best_bid() != r.bids.begin()->first) { CHECK(false); return; }
        if (ra && b.best_ask() != r.asks.begin()->first) { CHECK(false); return; }
        if (b.open_orders() != r.where.size()) { CHECK(false); return; }
    }
}

// Open-addressed id index against std::unordered_map. A small table forces long probe runs.
static void test_open_index() {
    OpenIndex<int> idx(64);
    std::unordered_map<OrderId, int*> ref;
    std::vector<int> store(100000);
    std::mt19937_64 rng(99);
    for (int i = 0; i < 100000; ++i) {
        OrderId id = 1 + rng() % 300;   // few ids, lots of repeats and collisions
        int* v = &store[i];
        if (ref.size() < 64 && rng() % 2) {
            bool fresh = ref.emplace(id, v).second;
            CHECK(idx.put(id, v) == fresh);
        } else {
            bool had = ref.erase(id) != 0;
            CHECK(idx.erase(id) == had);
        }
        OrderId probe = 1 + rng() % 300;
        auto it = ref.find(probe);
        CHECK(idx.get(probe) == (it == ref.end() ? nullptr : it->second));
        CHECK(idx.size() == ref.size());
    }
}

int main() {
    test_rest_and_best();
    test_full_and_partial_match();
    test_price_time_priority();
    test_cancel();
    test_modify();
    test_validation();
    test_market();
    test_random_differential<OrderBook>();
    test_random_differential<StdIndexOrderBook>();
    test_open_index();
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::puts("all order book tests passed");
}

#pragma once
#include <cassert>
#include <cstddef>
#include <vector>

#include "id_index.hpp"
#include "types.hpp"

namespace stag {

// Single-threaded limit order book with price-time priority.
//
// Baseline layout (deliberately simple, optimized later with benchmarks):
//  - flat array of price levels per side, indexed by (price - min_price)
//  - intrusive doubly-linked FIFO of orders per level
//  - orders come from a preallocated pool (no malloc on the hot path)
//  - an id index finds an order from its id; the Index parameter picks which
//    one (the benchmark compares them)
struct BookConfig {
    Price       min_price = 1;
    Price       max_price = 100'000;   // inclusive, in ticks
    std::size_t max_orders = 1 << 20;
};

template <template <class> class Index>
class BasicOrderBook {
public:
    using Config = BookConfig;

    BasicOrderBook() : BasicOrderBook(Config{}) {}
    explicit BasicOrderBook(const Config& cfg)
        : cfg_(cfg),
          bids_(static_cast<std::size_t>(cfg.max_price - cfg.min_price + 1)),
          asks_(bids_.size()),
          pool_(cfg.max_orders),
          index_(cfg.max_orders) {
        for (std::size_t i = 0; i < pool_.size(); ++i)
            pool_[i].next = (i + 1 < pool_.size()) ? &pool_[i + 1] : nullptr;
        free_ = pool_.empty() ? nullptr : &pool_[0];
    }

    BasicOrderBook(const BasicOrderBook&) = delete;
    BasicOrderBook& operator=(const BasicOrderBook&) = delete;

    // Matches against the opposite side, rests any remainder.
    // Trades are appended to `trades`.
    Status add_limit(OrderId id, Side side, Price price, Qty qty,
                     std::vector<Trade>& trades) {
        if (qty == 0) return Status::BadQty;
        if (price < cfg_.min_price || price > cfg_.max_price) return Status::BadPrice;
        if (index_.get(id)) return Status::DuplicateId;

        Qty remaining = match(id, side, price, qty, trades);
        if (remaining == 0) return Status::Ok;

        Order* o = alloc();
        if (!o) return Status::PoolExhausted;
        o->id = id; o->price = price; o->qty = remaining; o->side = side;
        rest(o);
        index_.put(id, o);
        return Status::Ok;
    }

    // Market orders never rest. Whatever doesn't fill is dropped.
    Status add_market(OrderId id, Side side, Qty qty, std::vector<Trade>& trades) {
        if (qty == 0) return Status::BadQty;
        if (side == Side::Buy ? !has_ask() : !has_bid()) return Status::NoLiquidity;
        match(id, side, side == Side::Buy ? cfg_.max_price : cfg_.min_price, qty, trades);
        return Status::Ok;
    }

    Status cancel(OrderId id) {
        Order* o = index_.get(id);
        if (!o) return Status::UnknownId;
        unlink(o);
        index_.erase(id);
        release(o);
        return Status::Ok;
    }

    // Reducing qty at the same price keeps queue priority. Any price change
    // or qty increase is cancel + re-add and goes to the back of the queue.
    Status modify(OrderId id, Price new_price, Qty new_qty,
                  std::vector<Trade>& trades) {
        Order* o = index_.get(id);
        if (!o) return Status::UnknownId;
        if (new_qty == 0) return Status::BadQty;
        if (new_price < cfg_.min_price || new_price > cfg_.max_price)
            return Status::BadPrice;

        if (new_price == o->price && new_qty <= o->qty) {
            level_of(o).total_qty -= o->qty - new_qty;
            o->qty = new_qty;
            return Status::Ok;
        }
        Side side = o->side;
        cancel(id);
        return add_limit(id, side, new_price, new_qty, trades);
    }

    // Best-first, at most n non-empty levels on one side.
    void top_levels(Side s, std::size_t n, std::vector<LevelView>& out) const {
        out.clear();
        const auto& v = (s == Side::Buy) ? bids_ : asks_;
        if (s == Side::Buy) {
            for (long i = best_bid_; i >= 0 && out.size() < n; --i)
                if (v[i].head) out.push_back({cfg_.min_price + i, v[i].total_qty});
        } else if (best_ask_ >= 0) {
            for (long i = best_ask_; i < static_cast<long>(v.size()) && out.size() < n; ++i)
                if (v[i].head) out.push_back({cfg_.min_price + i, v[i].total_qty});
        }
    }

    // Where a resting order sits. False if it isn't on the book.
    bool find(OrderId id, Side& side, Price& price) const {
        const Order* o = index_.get(id);
        if (!o) return false;
        side = o->side;
        price = o->price;
        return true;
    }

    // Queries (0 / -1 when empty).
    bool  has_bid() const { return best_bid_ >= 0; }
    bool  has_ask() const { return best_ask_ >= 0; }
    Price best_bid() const { return has_bid() ? cfg_.min_price + best_bid_ : 0; }
    Price best_ask() const { return has_ask() ? cfg_.min_price + best_ask_ : 0; }
    Qty   depth_at(Side s, Price p) const {
        if (p < cfg_.min_price || p > cfg_.max_price) return 0;
        const auto& v = (s == Side::Buy) ? bids_ : asks_;
        return v[static_cast<std::size_t>(p - cfg_.min_price)].total_qty;
    }
    std::size_t open_orders() const { return index_.size(); }
    bool        has_order(OrderId id) const { return index_.get(id) != nullptr; }

private:
    struct Order {
        OrderId id = 0;
        Price   price = 0;
        Qty     qty = 0;
        Side    side = Side::Buy;
        Order*  prev = nullptr;
        Order*  next = nullptr;   // also the free-list link
    };
    struct Level {
        Order* head = nullptr;
        Order* tail = nullptr;
        Qty    total_qty = 0;
    };

    Level& level_of(const Order* o) {
        auto& v = (o->side == Side::Buy) ? bids_ : asks_;
        return v[static_cast<std::size_t>(o->price - cfg_.min_price)];
    }

    Order* alloc() {
        Order* o = free_;
        if (o) { free_ = o->next; *o = Order{}; }
        return o;
    }
    void release(Order* o) { o->next = free_; free_ = o; }

    void rest(Order* o) {
        Level& L = level_of(o);
        o->prev = L.tail; o->next = nullptr;
        (L.tail ? L.tail->next : L.head) = o;
        L.tail = o;
        L.total_qty += o->qty;
        long idx = o->price - cfg_.min_price;
        if (o->side == Side::Buy) { if (idx > best_bid_) best_bid_ = idx; }
        else if (best_ask_ < 0 || idx < best_ask_) best_ask_ = idx;
    }

    void unlink(Order* o) {
        Level& L = level_of(o);
        (o->prev ? o->prev->next : L.head) = o->next;
        (o->next ? o->next->prev : L.tail) = o->prev;
        L.total_qty -= o->qty;
        if (!L.head) refresh_best(o->side, o->price - cfg_.min_price);
    }

    // Level at `idx` just emptied; rescan only if it was the best.
    void refresh_best(Side s, long idx) {
        if (s == Side::Buy && idx == best_bid_) {
            while (best_bid_ >= 0 && !bids_[best_bid_].head) --best_bid_;
        } else if (s == Side::Sell && idx == best_ask_) {
            const long n = static_cast<long>(asks_.size());
            while (best_ask_ < n && !asks_[best_ask_].head) ++best_ask_;
            if (best_ask_ == n) best_ask_ = -1;
        }
    }

    Qty match(OrderId taker, Side side, Price limit, Qty qty,
              std::vector<Trade>& trades) {
        auto& book = (side == Side::Buy) ? asks_ : bids_;
        while (qty > 0) {
            long best = (side == Side::Buy) ? best_ask_ : best_bid_;
            if (best < 0) break;
            Price p = cfg_.min_price + best;
            if (side == Side::Buy ? p > limit : p < limit) break;

            Level& L = book[static_cast<std::size_t>(best)];
            while (qty > 0 && L.head) {
                Order* m = L.head;
                Qty fill = m->qty < qty ? m->qty : qty;
                trades.push_back({m->id, taker, p, fill});
                m->qty -= fill; L.total_qty -= fill; qty -= fill;
                if (m->qty == 0) {
                    L.head = m->next;
                    (L.head ? L.head->prev : L.tail) = nullptr;
                    index_.erase(m->id);
                    release(m);
                }
            }
            if (!L.head) refresh_best(side == Side::Buy ? Side::Sell : Side::Buy, best);
        }
        return qty;
    }

    Config                               cfg_;
    std::vector<Level>                   bids_, asks_;
    std::vector<Order>                   pool_;
    Order*                               free_ = nullptr;
    Index<Order>                         index_;
    long best_bid_ = -1;   // index into bids_, -1 if empty
    long best_ask_ = -1;   // index into asks_, -1 if empty
};

using OrderBook = BasicOrderBook<OpenIndex>;
using StdIndexOrderBook = BasicOrderBook<StdIndex>;

}  // namespace stag

#pragma once
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "order_book.hpp"

namespace stag {

// The obvious first version: a std::map of price to std::list of orders.
// It's here as the baseline the real book gets measured against.
class MapBook {
public:
    explicit MapBook(const BookConfig&) {}

    Status add_limit(OrderId id, Side side, Price price, Qty qty, std::vector<Trade>& trades) {
        if (qty == 0) return Status::BadQty;
        if (where_.count(id)) return Status::DuplicateId;
        qty = side == Side::Buy ? take(asks_, id, price, qty, trades, true)
                                : take(bids_, id, price, qty, trades, false);
        if (qty == 0) return Status::Ok;
        auto& level = side == Side::Buy ? bids_[price] : asks_[price];
        level.push_back({id, qty, price});
        where_[id] = {side, std::prev(level.end())};
        return Status::Ok;
    }

    Status cancel(OrderId id) {
        auto it = where_.find(id);
        if (it == where_.end()) return Status::UnknownId;
        auto [side, pos] = it->second;
        if (side == Side::Buy) erase_from(bids_, pos); else erase_from(asks_, pos);
        where_.erase(it);
        return Status::Ok;
    }

    bool has_order(OrderId id) const { return where_.count(id) != 0; }

private:
    struct Order { OrderId id; Qty qty; Price price; };
    using Level = std::list<Order>;

    template <class M>
    Qty take(M& book, OrderId taker, Price limit, Qty qty, std::vector<Trade>& trades, bool buying) {
        while (qty && !book.empty()) {
            auto lvl = book.begin();
            if (buying ? lvl->first > limit : lvl->first < limit) break;
            Level& q = lvl->second;
            while (qty && !q.empty()) {
                Order& m = q.front();
                Qty fill = m.qty < qty ? m.qty : qty;
                trades.push_back({m.id, taker, m.price, fill});
                m.qty -= fill;
                qty -= fill;
                if (m.qty == 0) {
                    where_.erase(m.id);
                    q.pop_front();
                }
            }
            if (q.empty()) book.erase(lvl);
        }
        return qty;
    }

    template <class M>
    void erase_from(M& book, Level::iterator pos) {
        auto lvl = book.find(pos->price);
        lvl->second.erase(pos);
        if (lvl->second.empty()) book.erase(lvl);
    }

    std::map<Price, Level, std::greater<>> bids_;
    std::map<Price, Level>                 asks_;
    std::unordered_map<OrderId, std::pair<Side, Level::iterator>> where_;
};

}  // namespace stag

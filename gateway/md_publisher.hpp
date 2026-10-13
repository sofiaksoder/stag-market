#pragma once
#include <functional>

#include "order_book.hpp"
#include "protocol.hpp"

namespace stag {

// Turns book changes into sequenced market-data messages.
// Deltas carry the new size of a level, so a lost one is healed by the next
// delta at that price, and a snapshot fixes everything.
class MdPublisher {
public:
    using Broadcast = std::function<void(const proto::Buffer&)>;

    static constexpr std::size_t SNAPSHOT_DEPTH = 500;   // levels per side

    explicit MdPublisher(Broadcast out = {}) : out_(std::move(out)) {}

    void trade(Price px, Qty qty, Side aggressor, std::uint64_t ts_ns) {
        buf_.clear();
        proto::encode(buf_, proto::MdTrade{++seq_, ts_ns, px, qty, aggressor});
        emit();
    }

    void level(Side side, Price px, Qty size) {
        buf_.clear();
        proto::encode(buf_, proto::L2Delta{++seq_, side, px, size});
        emit();
    }

    // Not broadcast. The caller sends it to whoever asked. Its seq is the last
    // message published, so a client applies deltas with seq above it.
    void snapshot(const OrderBook& book, proto::Buffer& out) {
        proto::Snapshot s{seq_, {}, {}};
        book.top_levels(Side::Buy, SNAPSHOT_DEPTH, s.bids);
        book.top_levels(Side::Sell, SNAPSHOT_DEPTH, s.asks);
        proto::encode(out, s);
    }

    std::uint64_t seq() const { return seq_; }

private:
    void emit() { if (out_) out_(buf_); }

    Broadcast     out_;
    proto::Buffer buf_;
    std::uint64_t seq_ = 0;
};

}  // namespace stag

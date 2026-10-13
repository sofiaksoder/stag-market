#pragma once
#include <chrono>
#include <functional>
#include <unordered_map>

#include "order_book.hpp"
#include "journal.hpp"
#include "md_publisher.hpp"
#include "protocol.hpp"

namespace stag {

constexpr Qty MAX_QTY = 50'000;   // risk limit per order

// Glue between the wire protocol and the order book. No sockets in here,
// replies go out through the send callback, so it's easy to test.
class Exchange {
public:
    using ConnId = int;
    using Send = std::function<void(ConnId, const proto::Buffer&)>;
    using Clock = std::function<std::uint64_t()>;   // nanoseconds

    static std::uint64_t wall_clock_ns() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }

    // A replay passes its own clock so timestamps come from the journal.
    Exchange(const OrderBook::Config& cfg, Send send, MdPublisher::Broadcast md = {}, Clock clock = wall_clock_ns)
        : book_(cfg), send_(std::move(send)), md_(std::move(md)), clock_(std::move(clock)) {}

    void set_journal(JournalWriter* j) { journal_ = j; }

    void on_new_order(ConnId conn, const proto::NewOrder& m) {
        now_ = clock_();
        if (journal_) journal_->write(now_, conn, m);
        OrderId id = next_id_++;
        Status st = Status::Ok;
        if (m.qty > MAX_QTY) st = Status::RiskLimit;
        if (st != Status::Ok) { reject(conn, m.cl_ord_id, id, st); return; }

        trades_.clear();
        if (m.type == proto::OrderType::Limit)
            st = book_.add_limit(id, m.side, m.px, m.qty, trades_);
        else
            st = book_.add_market(id, m.side, m.qty, trades_);
        if (st != Status::Ok) { reject(conn, m.cl_ord_id, id, st); return; }

        owner_[id] = conn;
        reply(conn, proto::Ack{m.cl_ord_id, id, proto::AckStatus::New, Status::Ok, ++seq_});
        // publish() below emits one MdTrade per trade, in order, right after the current md seq.
        std::uint64_t trade_seq = md_.seq();
        for (const Trade& t : trades_) {
            ++trade_seq;
            send_fill(conn, t.taker, t, proto::Role::Taker, trade_seq);
            send_fill(owner_[t.maker], t.maker, t, proto::Role::Maker, trade_seq);
            if (!book_.has_order(t.maker)) owner_.erase(t.maker);
        }
        if (!book_.has_order(id)) owner_.erase(id);
        publish(m.side, m.type == proto::OrderType::Limit ? m.px : 0, id);
    }

    void on_cancel(ConnId conn, const proto::Cancel& m) {
        now_ = clock_();
        if (journal_) journal_->write(now_, conn, m);
        auto it = owner_.find(m.id);
        if (it == owner_.end() || it->second != conn) {
            reject(conn, 0, m.id, Status::UnknownId);
            return;
        }
        Side side; Price px;
        book_.find(m.id, side, px);
        book_.cancel(m.id);
        owner_.erase(it);
        reply(conn, proto::Ack{0, m.id, proto::AckStatus::Canceled, Status::Ok, ++seq_});
        md_.level(side, px, book_.depth_at(side, px));
    }

    // Pull a dropped client's resting orders off the book.
    void on_disconnect(ConnId conn) {
        bool owns_any = false;
        for (const auto& kv : owner_) if (kv.second == conn) { owns_any = true; break; }
        if (!owns_any) return;
        now_ = clock_();
        if (journal_) journal_->write_disconnect(now_, conn);
        for (auto it = owner_.begin(); it != owner_.end();) {
            if (it->second == conn) {
                Side side; Price px;
                book_.find(it->first, side, px);
                book_.cancel(it->first);
                md_.level(side, px, book_.depth_at(side, px));
                it = owner_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Current book for a client that asked for a snapshot.
    void snapshot(proto::Buffer& out) { md_.snapshot(book_, out); }

    const OrderBook& book() const { return book_; }
    std::uint64_t md_seq() const { return md_.seq(); }
    OrderId last_order_id() const { return next_id_ - 1; }

private:
    // Market data for the order just processed: the trades, then the new size
    // of every level it touched (the price it rested at, and each one it hit).
    void publish(Side aggressor, Price rested_px, OrderId id) {
        Side other = aggressor == Side::Buy ? Side::Sell : Side::Buy;
        touched_.clear();
        for (const Trade& t : trades_) {
            md_.trade(t.price, t.qty, aggressor, now_);
            touched_.push_back(t.price);
        }
        for (std::size_t i = 0; i < touched_.size(); ++i) {
            // Trades come in price order, so repeats sit next to each other.
            if (i > 0 && touched_[i] == touched_[i - 1]) continue;
            md_.level(other, touched_[i], book_.depth_at(other, touched_[i]));
        }
        if (rested_px && book_.has_order(id))
            md_.level(aggressor, rested_px, book_.depth_at(aggressor, rested_px));
    }

    void reply(ConnId conn, const proto::Ack& a) {
        out_.clear();
        proto::encode(out_, a);
        send_(conn, out_);
    }

    void reject(ConnId conn, std::uint64_t cl_ord_id, OrderId id, Status why) {
        reply(conn, proto::Ack{cl_ord_id, id, proto::AckStatus::Rejected, why, ++seq_});
    }

    void send_fill(ConnId conn, OrderId id, const Trade& t, proto::Role role, std::uint64_t trade_seq) {
        out_.clear();
        proto::encode(out_, proto::Fill{id, t.price, t.qty, role, ++seq_, trade_seq});
        send_(conn, out_);
    }

    OrderBook                          book_;
    Send                               send_;
    MdPublisher                        md_;
    Clock                              clock_;
    JournalWriter*                     journal_ = nullptr;
    std::uint64_t                      now_ = 0;
    std::vector<Price>                 touched_;
    std::unordered_map<OrderId, ConnId> owner_;
    std::vector<Trade>                 trades_;
    proto::Buffer                      out_;
    OrderId                            next_id_ = 1;
    std::uint64_t                      seq_ = 0;
};

}  // namespace stag

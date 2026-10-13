#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "types.hpp"

// Binary wire format, little-endian.
// Frame: u16 length (type byte + body) | u8 type | body
namespace stag::proto {

static_assert(std::endian::native == std::endian::little);

enum class Msg : std::uint8_t {
    NewOrder = 1, Cancel = 2, Ack = 10, Fill = 11,
    L2Delta = 20, MdTrade = 21, Snapshot = 22, SnapshotReq = 23,
};
enum class OrderType : std::uint8_t { Limit, Market };
enum class AckStatus : std::uint8_t { New, Canceled, Rejected };
enum class Role : std::uint8_t { Maker, Taker };

struct NewOrder { std::uint64_t cl_ord_id; Side side; OrderType type; Price px; Qty qty; };
struct Cancel   { OrderId id; };
struct Ack      { std::uint64_t cl_ord_id; OrderId id; AckStatus status; Status reason; std::uint64_t seq; };
// trade_seq is the seq of the matching MdTrade, so a client can spot its own trades on the tape.
struct Fill     { OrderId id; Price px; Qty qty; Role role; std::uint64_t seq; std::uint64_t trade_seq; };

// Market data. Every message the publisher sends carries a seq that goes up by one.
struct L2Delta  { std::uint64_t seq; Side side; Price px; Qty size; };   // size 0 = level gone
struct MdTrade  { std::uint64_t seq; std::uint64_t ts_ns; Price px; Qty qty; Side aggressor; };
struct Snapshot { std::uint64_t seq; std::vector<LevelView> bids, asks; };   // best first
struct SnapshotReq {};

using Buffer = std::vector<std::uint8_t>;

namespace detail {

template <class T> void put(Buffer& b, T v) {
    auto* p = reinterpret_cast<const std::uint8_t*>(&v);
    b.insert(b.end(), p, p + sizeof v);
}

// Reads sequentially from a body. ok goes false if we run past the end.
struct Reader {
    const std::uint8_t* p;
    std::size_t left;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (left < sizeof v) { ok = false; return v; }
        std::memcpy(&v, p, sizeof v);
        p += sizeof v; left -= sizeof v;
        return v;
    }
};

inline std::size_t begin_frame(Buffer& b, Msg t) {
    std::size_t at = b.size();
    put<std::uint16_t>(b, 0);
    put(b, static_cast<std::uint8_t>(t));
    return at;
}

inline void end_frame(Buffer& b, std::size_t at) {
    auto len = static_cast<std::uint16_t>(b.size() - at - 2);
    std::memcpy(&b[at], &len, sizeof len);
}

}  // namespace detail

inline void encode(Buffer& b, const NewOrder& m) {
    auto at = detail::begin_frame(b, Msg::NewOrder);
    detail::put(b, m.cl_ord_id);
    detail::put(b, static_cast<std::uint8_t>(m.side));
    detail::put(b, static_cast<std::uint8_t>(m.type));
    detail::put(b, m.px);
    detail::put(b, m.qty);
    detail::end_frame(b, at);
}

inline void encode(Buffer& b, const Cancel& m) {
    auto at = detail::begin_frame(b, Msg::Cancel);
    detail::put(b, m.id);
    detail::end_frame(b, at);
}

inline void encode(Buffer& b, const Ack& m) {
    auto at = detail::begin_frame(b, Msg::Ack);
    detail::put(b, m.cl_ord_id);
    detail::put(b, m.id);
    detail::put(b, static_cast<std::uint8_t>(m.status));
    detail::put(b, static_cast<std::uint8_t>(m.reason));
    detail::put(b, m.seq);
    detail::end_frame(b, at);
}

inline void encode(Buffer& b, const Fill& m) {
    auto at = detail::begin_frame(b, Msg::Fill);
    detail::put(b, m.id);
    detail::put(b, m.px);
    detail::put(b, m.qty);
    detail::put(b, static_cast<std::uint8_t>(m.role));
    detail::put(b, m.seq);
    detail::put(b, m.trade_seq);
    detail::end_frame(b, at);
}

// Decoders take just the body (after the type byte). False if it's malformed.
inline bool decode(const std::uint8_t* body, std::size_t n, NewOrder& m) {
    detail::Reader r{body, n};
    m.cl_ord_id = r.get<std::uint64_t>();
    m.side = static_cast<Side>(r.get<std::uint8_t>());
    m.type = static_cast<OrderType>(r.get<std::uint8_t>());
    m.px = r.get<Price>();
    m.qty = r.get<Qty>();
    return r.ok && r.left == 0 && m.side <= Side::Sell && m.type <= OrderType::Market;
}

inline bool decode(const std::uint8_t* body, std::size_t n, Cancel& m) {
    detail::Reader r{body, n};
    m.id = r.get<OrderId>();
    return r.ok && r.left == 0;
}

inline bool decode(const std::uint8_t* body, std::size_t n, Ack& m) {
    detail::Reader r{body, n};
    m.cl_ord_id = r.get<std::uint64_t>();
    m.id = r.get<OrderId>();
    m.status = static_cast<AckStatus>(r.get<std::uint8_t>());
    m.reason = static_cast<Status>(r.get<std::uint8_t>());
    m.seq = r.get<std::uint64_t>();
    return r.ok && r.left == 0;
}

inline bool decode(const std::uint8_t* body, std::size_t n, Fill& m) {
    detail::Reader r{body, n};
    m.id = r.get<OrderId>();
    m.px = r.get<Price>();
    m.qty = r.get<Qty>();
    m.role = static_cast<Role>(r.get<std::uint8_t>());
    m.seq = r.get<std::uint64_t>();
    m.trade_seq = r.get<std::uint64_t>();
    return r.ok && r.left == 0;
}

inline void encode(Buffer& b, const L2Delta& m) {
    auto at = detail::begin_frame(b, Msg::L2Delta);
    detail::put(b, m.seq);
    detail::put(b, static_cast<std::uint8_t>(m.side));
    detail::put(b, m.px);
    detail::put(b, m.size);
    detail::end_frame(b, at);
}

inline void encode(Buffer& b, const MdTrade& m) {
    auto at = detail::begin_frame(b, Msg::MdTrade);
    detail::put(b, m.seq);
    detail::put(b, m.ts_ns);
    detail::put(b, m.px);
    detail::put(b, m.qty);
    detail::put(b, static_cast<std::uint8_t>(m.aggressor));
    detail::end_frame(b, at);
}

// The u16 frame length limits this to about 2,700 levels per side.
inline void encode(Buffer& b, const Snapshot& m) {
    auto at = detail::begin_frame(b, Msg::Snapshot);
    detail::put(b, m.seq);
    detail::put(b, static_cast<std::uint16_t>(m.bids.size()));
    detail::put(b, static_cast<std::uint16_t>(m.asks.size()));
    for (auto* side : {&m.bids, &m.asks})
        for (const LevelView& l : *side) { detail::put(b, l.price); detail::put(b, l.qty); }
    detail::end_frame(b, at);
}

inline void encode(Buffer& b, const SnapshotReq&) {
    detail::end_frame(b, detail::begin_frame(b, Msg::SnapshotReq));
}

inline bool decode(const std::uint8_t* body, std::size_t n, L2Delta& m) {
    detail::Reader r{body, n};
    m.seq = r.get<std::uint64_t>();
    m.side = static_cast<Side>(r.get<std::uint8_t>());
    m.px = r.get<Price>();
    m.size = r.get<Qty>();
    return r.ok && r.left == 0 && m.side <= Side::Sell;
}

inline bool decode(const std::uint8_t* body, std::size_t n, MdTrade& m) {
    detail::Reader r{body, n};
    m.seq = r.get<std::uint64_t>();
    m.ts_ns = r.get<std::uint64_t>();
    m.px = r.get<Price>();
    m.qty = r.get<Qty>();
    m.aggressor = static_cast<Side>(r.get<std::uint8_t>());
    return r.ok && r.left == 0 && m.aggressor <= Side::Sell;
}

inline bool decode(const std::uint8_t* body, std::size_t n, Snapshot& m) {
    detail::Reader r{body, n};
    m.seq = r.get<std::uint64_t>();
    auto nb = r.get<std::uint16_t>();
    auto na = r.get<std::uint16_t>();
    m.bids.clear();
    m.asks.clear();
    for (auto [count, out] : {std::pair{nb, &m.bids}, std::pair{na, &m.asks}})
        for (int i = 0; i < count && r.ok; ++i) {
            Price p = r.get<Price>();
            Qty q = r.get<Qty>();
            out->push_back({p, q});
        }
    return r.ok && r.left == 0;
}

}  // namespace stag::proto

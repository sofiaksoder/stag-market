#pragma once
#include <cstdint>

namespace stag {

using OrderId = std::uint64_t;
using Price   = std::int64_t;   // integer ticks, never floating point
using Qty     = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };

struct Trade {
    OrderId maker;   // resting order
    OrderId taker;   // incoming order
    Price   price;   // always the maker's price
    Qty     qty;
};

struct LevelView {
    Price price;
    Qty   qty;
};

enum class Status : std::uint8_t {
    Ok,
    DuplicateId,
    UnknownId,
    BadPrice,
    BadQty,
    PoolExhausted,
    NoLiquidity,   // market order with nothing to hit
    RiskLimit,     // rejected by the gateway's checks
};

}  // namespace stag

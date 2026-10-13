#pragma once
#include <bit>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace stag {

// Maps an order id to its order. Two versions so the benchmark can compare them.

// The straightforward one.
template <class V>
class StdIndex {
public:
    explicit StdIndex(std::size_t max_items) { map_.reserve(max_items); }

    V* get(OrderId id) const {
        auto it = map_.find(id);
        return it == map_.end() ? nullptr : it->second;
    }
    bool put(OrderId id, V* v) { return map_.emplace(id, v).second; }
    bool erase(OrderId id) { return map_.erase(id) != 0; }
    std::size_t size() const { return map_.size(); }

private:
    std::unordered_map<OrderId, V*> map_;
};

// Open addressing with linear probing, in one flat array. Sized up front for
// max_items at half full, so it never grows. Erase shifts later entries back
// instead of leaving tombstones. Id 2^64-1 can't be used (it marks empty slots).
template <class V>
class OpenIndex {
public:
    explicit OpenIndex(std::size_t max_items)
        : slots_(std::bit_ceil(std::max<std::size_t>(max_items * 2, 16)), Slot{EMPTY, nullptr}),
          mask_(slots_.size() - 1),
          shift_(64 - std::countr_zero(slots_.size())) {}

    V* get(OrderId id) const {
        for (std::size_t i = home(id);; i = (i + 1) & mask_) {
            if (slots_[i].key == id) return slots_[i].val;
            if (slots_[i].key == EMPTY) return nullptr;
        }
    }

    bool put(OrderId id, V* v) {
        std::size_t i = home(id);
        for (; slots_[i].key != EMPTY; i = (i + 1) & mask_)
            if (slots_[i].key == id) return false;
        slots_[i] = {id, v};
        ++size_;
        return true;
    }

    bool erase(OrderId id) {
        std::size_t i = home(id);
        while (slots_[i].key != id) {
            if (slots_[i].key == EMPTY) return false;
            i = (i + 1) & mask_;
        }
        // Pull back any later entry in the run that would otherwise become unreachable.
        for (std::size_t j = (i + 1) & mask_; slots_[j].key != EMPTY; j = (j + 1) & mask_) {
            std::size_t h = home(slots_[j].key);
            if (((j - h) & mask_) >= ((j - i) & mask_)) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].key = EMPTY;
        --size_;
        return true;
    }

    std::size_t size() const { return size_; }

private:
    static constexpr OrderId EMPTY = ~OrderId{0};

    struct Slot {
        OrderId key;
        V*      val;
    };

    std::size_t home(OrderId id) const { return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_); }

    std::vector<Slot> slots_;
    std::size_t       mask_;
    int               shift_;
    std::size_t       size_ = 0;
};

}  // namespace stag

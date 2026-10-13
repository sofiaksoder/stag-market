#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "order_book.hpp"
#include "protocol.hpp"

// Append-only log of everything that goes into the exchange. Run the same
// inputs through a fresh exchange and you get the same book, trades and seqs.
//
// File: header, then records until the end.
//   header: "STAGJRN1" | u64 seed | i64 min_price | i64 max_price | u64 max_orders
//   record: u64 ts_ns | i32 conn | a protocol frame (u16 len | u8 type | body)
// A client dropping is recorded as a frame of type 0 with no body.
namespace stag {

constexpr std::uint8_t DISCONNECT_TYPE = 0;
constexpr char JOURNAL_MAGIC[8] = {'S', 'T', 'A', 'G', 'J', 'R', 'N', '1'};
constexpr std::size_t JOURNAL_HEADER_SIZE = 8 + 8 + 8 + 8 + 8;

struct JournalEvent {
    std::uint64_t ts_ns = 0;
    std::int32_t  conn = 0;
    std::uint8_t  type = 0;   // a proto::Msg value, or DISCONNECT_TYPE
    proto::NewOrder order{};
    proto::Cancel   cancel{};
};

class JournalWriter {
public:
    JournalWriter(const std::string& path, const OrderBook::Config& cfg, std::uint64_t seed = 0)
        : f_(std::fopen(path.c_str(), "wb")) {
        if (!f_) return;
        std::fwrite(JOURNAL_MAGIC, 1, 8, f_);
        put(seed);
        put(static_cast<std::int64_t>(cfg.min_price));
        put(static_cast<std::int64_t>(cfg.max_price));
        put(static_cast<std::uint64_t>(cfg.max_orders));
    }
    ~JournalWriter() { if (f_) std::fclose(f_); }
    JournalWriter(const JournalWriter&) = delete;
    JournalWriter& operator=(const JournalWriter&) = delete;

    bool ok() const { return f_ != nullptr; }

    void write(std::uint64_t ts_ns, std::int32_t conn, const proto::NewOrder& m) {
        proto::Buffer b;
        proto::encode(b, m);
        record(ts_ns, conn, b);
    }
    void write(std::uint64_t ts_ns, std::int32_t conn, const proto::Cancel& m) {
        proto::Buffer b;
        proto::encode(b, m);
        record(ts_ns, conn, b);
    }
    void write_disconnect(std::uint64_t ts_ns, std::int32_t conn) {
        record(ts_ns, conn, {1, 0, DISCONNECT_TYPE});
    }

private:
    template <class T> void put(T v) { std::fwrite(&v, sizeof v, 1, f_); }

    void record(std::uint64_t ts_ns, std::int32_t conn, const proto::Buffer& frame) {
        if (!f_) return;
        put(ts_ns);
        put(conn);
        std::fwrite(frame.data(), 1, frame.size(), f_);
        std::fflush(f_);   // the journal is the source of truth, so don't sit on it
    }

    std::FILE* f_;
};

class JournalReader {
public:
    // False if the file is missing or isn't a journal.
    bool open(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        data_.clear();
        std::uint8_t chunk[1 << 16];
        std::size_t n;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) data_.insert(data_.end(), chunk, chunk + n);
        std::fclose(f);

        if (data_.size() < JOURNAL_HEADER_SIZE || std::memcmp(data_.data(), JOURNAL_MAGIC, 8) != 0) return false;
        std::int64_t lo, hi;
        std::uint64_t max_orders;
        std::memcpy(&seed_, &data_[8], 8);
        std::memcpy(&lo, &data_[16], 8);
        std::memcpy(&hi, &data_[24], 8);
        std::memcpy(&max_orders, &data_[32], 8);
        cfg_ = {lo, hi, static_cast<std::size_t>(max_orders)};
        pos_ = JOURNAL_HEADER_SIZE;
        return true;
    }

    // False at the end of the file, or at a record that doesn't parse.
    bool next(JournalEvent& e) {
        if (data_.size() - pos_ < 8 + 4 + 3) return false;
        std::size_t p = pos_;
        std::memcpy(&e.ts_ns, &data_[p], 8);
        std::memcpy(&e.conn, &data_[p + 8], 4);
        p += 12;
        std::uint16_t len;
        std::memcpy(&len, &data_[p], 2);
        if (len < 1 || data_.size() - p - 2 < len) return false;
        e.type = data_[p + 2];
        const std::uint8_t* body = &data_[p + 3];
        std::size_t blen = len - 1;
        if (e.type == static_cast<std::uint8_t>(proto::Msg::NewOrder)) {
            if (!proto::decode(body, blen, e.order)) return false;
        } else if (e.type == static_cast<std::uint8_t>(proto::Msg::Cancel)) {
            if (!proto::decode(body, blen, e.cancel)) return false;
        } else if (e.type != DISCONNECT_TYPE) {
            return false;
        }
        pos_ = p + 2 + len;
        return true;
    }

    const OrderBook::Config& config() const { return cfg_; }
    std::uint64_t seed() const { return seed_; }

private:
    std::vector<std::uint8_t> data_;
    std::size_t               pos_ = 0;
    OrderBook::Config         cfg_{};
    std::uint64_t             seed_ = 0;
};

}  // namespace stag

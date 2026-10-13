// Rebuilds a session from its journal and writes a snapshot every 500 ms of
// exchange time as JSON, for the web replay page.
//   stag_replay <in.journal> <out.json>
#include <cstdio>
#include <ctime>
#include <deque>
#include <string>

#include "replay.hpp"

using namespace stag;

constexpr std::uint64_t SNAP_NS = 500'000'000;
constexpr std::size_t   BOOK_LEVELS = 8;
constexpr std::size_t   TAPE_ROWS = 16;

struct TapeRow { std::uint64_t seq, ts_ns; Price px; Qty qty; Side side; };

static std::string px_text(Price p) {
    char b[32];
    std::snprintf(b, sizeof b, "%lld.%02lld", static_cast<long long>(p / 100), static_cast<long long>(p % 100));
    return b;
}

static void write_levels(std::FILE* f, const std::vector<LevelView>& v) {
    std::fputc('[', f);
    for (std::size_t i = 0; i < v.size(); ++i)
        std::fprintf(f, "%s[%lld,%u]", i ? "," : "", static_cast<long long>(v[i].price), v[i].qty);
    std::fputc(']', f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <in.journal> <out.json>\n", argv[0]);
        return 1;
    }
    JournalReader in;
    if (!in.open(argv[1])) {
        std::fprintf(stderr, "%s: not a journal\n", argv[1]);
        return 1;
    }
    std::FILE* out = std::fopen(argv[2], "w");
    if (!out) {
        std::perror(argv[2]);
        return 1;
    }

    std::uint64_t clock_ns = 0;
    std::deque<TapeRow> tape;
    Price last = 14250;
    std::string status;   // set when the exchange rejects the event
    std::vector<std::string> events;   // text of what happened since the last snapshot

    Exchange ex(in.config(),
        [&](int, const proto::Buffer& b) {
            if (static_cast<proto::Msg>(b[2]) != proto::Msg::Ack) return;
            proto::Ack a;
            proto::decode(&b[3], b.size() - 3, a);
            if (a.status == proto::AckStatus::Rejected) status = "REJECTED";
        },
        [&](const proto::Buffer& b) {
            if (static_cast<proto::Msg>(b[2]) != proto::Msg::MdTrade) return;
            proto::MdTrade t;
            proto::decode(&b[3], b.size() - 3, t);
            tape.push_front({t.seq, t.ts_ns, t.px, t.qty, t.aggressor});
            if (tape.size() > TAPE_ROWS) tape.pop_back();
            last = t.px;
        },
        [&] { return clock_ns; });

    // Pass 1 reads everything so we know the date and the first timestamp.
    std::vector<JournalEvent> all;
    JournalEvent e;
    while (in.next(e)) all.push_back(e);
    if (all.empty()) {
        std::fprintf(stderr, "journal has no events\n");
        return 1;
    }
    std::uint64_t t0 = all.front().ts_ns;
    std::time_t secs = static_cast<std::time_t>(t0 / 1'000'000'000);
    char date[16];
    std::strftime(date, sizeof date, "%Y-%m-%d", std::gmtime(&secs));

    std::fprintf(out, "{\"session\":\"%s\",\"seed\":%llu,\"stepMs\":%llu,\"ref\":14250,\"snapshots\":[", date,
                 static_cast<unsigned long long>(in.seed()), static_cast<unsigned long long>(SNAP_NS / 1'000'000));

    std::size_t written = 0;
    auto snapshot = [&](std::uint64_t t_ns) {
        std::vector<LevelView> bids, asks;
        ex.book().top_levels(Side::Buy, BOOK_LEVELS, bids);
        ex.book().top_levels(Side::Sell, BOOK_LEVELS, asks);
        std::fprintf(out, "%s{\"seq\":%llu,\"t\":%llu,\"last\":%lld,\"bids\":", written ? "," : "",
                     static_cast<unsigned long long>(ex.md_seq()), static_cast<unsigned long long>(t_ns / 1'000'000),
                     static_cast<long long>(last));
        write_levels(out, bids);
        std::fputs(",\"asks\":", out);
        write_levels(out, asks);
        std::fputs(",\"tape\":[", out);
        for (std::size_t i = 0; i < tape.size(); ++i) {
            const TapeRow& r = tape[i];
            std::fprintf(out, "%s[%llu,%llu,%lld,%u,\"%c\"]", i ? "," : "", static_cast<unsigned long long>(r.seq),
                         static_cast<unsigned long long>(r.ts_ns / 1'000'000), static_cast<long long>(r.px), r.qty,
                         r.side == Side::Buy ? 'B' : 'S');
        }
        std::fputs("],\"ev\":[", out);
        for (std::size_t i = 0; i < events.size(); ++i) std::fprintf(out, "%s\"%s\"", i ? "," : "", events[i].c_str());
        std::fputs("]}", out);
        events.clear();
        ++written;
    };

    std::uint64_t next_snap = t0 + SNAP_NS;
    for (const JournalEvent& ev : all) {
        while (ev.ts_ns >= next_snap) {   // quiet stretches still get a snapshot
            snapshot(next_snap);
            next_snap += SNAP_NS;
        }
        clock_ns = ev.ts_ns;
        status.clear();
        apply(ex, ev);

        char text[96];
        if (ev.type == DISCONNECT_TYPE) {
            std::snprintf(text, sizeof text, "DISCONNECT conn %d", ev.conn);
        } else if (ev.type == static_cast<std::uint8_t>(proto::Msg::Cancel)) {
            std::snprintf(text, sizeof text, "%s #%llu", status.empty() ? "CANCEL" : "CANCEL REJECTED",
                          static_cast<unsigned long long>(ev.cancel.id));
        } else {
            const char* side = ev.order.side == Side::Buy ? "BUY" : "SELL";
            std::string at = ev.order.type == proto::OrderType::Limit ? "@ " + px_text(ev.order.px) : "at market";
            std::snprintf(text, sizeof text, "%s #%llu %s %u %s", status.empty() ? "NEW" : "REJECTED",
                          static_cast<unsigned long long>(ex.last_order_id()), side, ev.order.qty, at.c_str());
        }
        events.push_back(text);
    }
    snapshot(next_snap);   // the last, partly filled interval
    std::fputs("]}\n", out);
    std::fclose(out);
    std::printf("%zu events, %zu snapshots\n", all.size(), written);
}

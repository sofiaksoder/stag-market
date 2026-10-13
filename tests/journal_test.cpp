#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>

#include "replay.hpp"
#include "sim.hpp"

using namespace stag;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string tmp(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

static const OrderBook::Config CFG{1, 100'000, 1 << 16};
static const std::uint64_t START = 1'791'797'400ull * 1'000'000'000ull;

// Fingerprint of everything the exchange published and replied.
struct Tap {
    std::uint64_t hash = 14695981039346656037ull;
    std::size_t   messages = 0;
    void add(const proto::Buffer& b) {
        for (auto x : b) { hash ^= x; hash *= 1099511628211ull; }
        ++messages;
    }
};

static std::string read_file(const std::string& path) {
    std::string s;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

static Tap run_sim(std::uint64_t seed, int steps, const std::string& journal_path) {
    Tap tap;
    Sim sim(seed, START);
    JournalWriter j(journal_path, CFG, seed);
    Exchange ex(CFG, [&](int, const proto::Buffer& b) { tap.add(b); }, [&](const proto::Buffer& b) { tap.add(b); },
                sim.clock());
    ex.set_journal(&j);
    sim.seed_book(ex);
    for (int i = 0; i < steps; ++i) sim.step(ex);
    return tap;
}

static void test_same_seed_same_journal() {
    Tap a = run_sim(42, 500, tmp("stag_j1.bin"));
    Tap b = run_sim(42, 500, tmp("stag_j2.bin"));
    Tap c = run_sim(43, 500, tmp("stag_j3.bin"));
    CHECK(a.messages > 1000);
    CHECK(a.hash == b.hash);
    CHECK(read_file(tmp("stag_j1.bin")) == read_file(tmp("stag_j2.bin")));
    CHECK(a.hash != c.hash);
}

// The point of the journal: replaying it gives back the exact same session.
static void test_replay_reproduces_session() {
    Tap live = run_sim(7, 800, tmp("stag_j4.bin"));

    JournalReader in;
    CHECK(in.open(tmp("stag_j4.bin")));
    CHECK(in.seed() == 7 && in.config().max_price == CFG.max_price);

    Tap replayed;
    std::uint64_t clock_ns = 0;
    Exchange ex(in.config(), [&](int, const proto::Buffer& b) { replayed.add(b); },
                [&](const proto::Buffer& b) { replayed.add(b); }, [&] { return clock_ns; });
    JournalEvent e;
    std::size_t events = 0;
    while (in.next(e)) {
        clock_ns = e.ts_ns;
        apply(ex, e);
        ++events;
    }
    CHECK(events > 1000);
    CHECK(replayed.messages == live.messages);
    CHECK(replayed.hash == live.hash);
}

static void test_disconnect_is_journaled() {
    {
        std::uint64_t t = 1000;
        JournalWriter j(tmp("stag_j5.bin"), CFG, 0);
        Exchange ex(CFG, [](int, const proto::Buffer&) {}, {}, [&] { return t += 10; });
        ex.set_journal(&j);
        ex.on_new_order(1, {1, Side::Buy, proto::OrderType::Limit, 100, 5});
        ex.on_disconnect(2);   // owns nothing, so nothing to record
        ex.on_disconnect(1);
        CHECK(ex.book().open_orders() == 0);
    }
    JournalReader in;
    CHECK(in.open(tmp("stag_j5.bin")));
    JournalEvent e;
    CHECK(in.next(e) && e.type == static_cast<std::uint8_t>(proto::Msg::NewOrder) && e.ts_ns == 1010);
    CHECK(in.next(e) && e.type == DISCONNECT_TYPE && e.conn == 1);
    CHECK(!in.next(e));
}

static void test_rejects_bad_files() {
    JournalReader in;
    CHECK(!in.open(tmp("stag_does_not_exist.bin")));
    if (std::FILE* f = std::fopen(tmp("stag_junk.bin").c_str(), "wb")) { std::fputs("not a journal at all, sorry", f); std::fclose(f); }
    CHECK(!in.open(tmp("stag_junk.bin")));
}

int main() {
    test_same_seed_same_journal();
    test_replay_reproduces_session();
    test_disconnect_is_journaled();
    test_rejects_bad_files();
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::puts("all journal tests passed");
}

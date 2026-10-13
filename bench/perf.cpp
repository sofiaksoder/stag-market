// Times the order book and the gateway path, and writes the numbers as JSON
// for the web UI's Performance page.
//   stag_bench <out.json> [gateway binary]
// Use a Release build. The gateway binary defaults to the one next to this.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "exchange.hpp"
#include "map_book.hpp"
#include "order_book.hpp"

using namespace stag;
using Clock = std::chrono::steady_clock;

constexpr Price REF = 14250;
constexpr int   N = 200'000;       // resting orders in each run
constexpr int   RUNS = 3;          // each variant runs this many times, the fastest run counts
constexpr int   RTT_ORDERS = 20'000;
constexpr int   HIST_BINS = 28;

static double ns_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
}

static double pct(std::vector<double>& v, double p) {
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(p * (v.size() - 1))];
}

struct Op {
    std::vector<double> ns;
    double p50 = 0, p99 = 0;
    void finish() {
        std::vector<double> c = ns;
        p50 = pct(c, 0.50);
        p99 = pct(c, 0.99);
    }
};

struct Result {
    std::string name;
    Op add, match, cancel;
    double ops_per_s = 0;
    double total_ns = 0;   // for picking the fastest run
};

// The same work for every variant. With `timed` null it just runs, so the
// throughput figure doesn't include clock reads.
// Returns how many operations it ran.
template <class Book>
static std::size_t workload(Book& book, Result* timed) {
    std::mt19937_64 rng(42);
    std::vector<Trade> trades;
    trades.reserve(8);
    auto run = [&](Op* op, auto&& fn) {
        if (!timed) return fn();
        auto t0 = Clock::now();
        fn();
        op->ns.push_back(ns_since(t0));
    };

    // Resting orders: bids under the reference, asks over it, so nothing crosses.
    for (int i = 0; i < N; ++i) {
        Side s = (i & 1) ? Side::Buy : Side::Sell;
        Price off = 1 + static_cast<Price>(rng() % 50);
        run(timed ? &timed->add : nullptr, [&] {
            book.add_limit(i + 1, s, s == Side::Buy ? REF - off : REF + off, 100, trades);
        });
    }
    // Each of these crosses the spread and takes one whole resting order.
    for (int i = 0; i < N / 4; ++i) {
        Side s = (i & 1) ? Side::Buy : Side::Sell;
        run(timed ? &timed->match : nullptr, [&] {
            trades.clear();
            book.add_limit(N + 1 + i, s, s == Side::Buy ? REF + 60 : REF - 60, 100, trades);
        });
    }
    // Cancel whatever is left, in random order.
    std::vector<OrderId> ids;
    for (OrderId id = 1; id <= N; ++id) if (book.has_order(id)) ids.push_back(id);
    std::shuffle(ids.begin(), ids.end(), rng);
    for (OrderId id : ids) run(timed ? &timed->cancel : nullptr, [&] { book.cancel(id); });
    return N + N / 4 + ids.size();
}

template <class Book>
static Result measure(const char* name) {
    Result best;
    for (int run = 0; run < RUNS; ++run) {
        Result r;
        r.name = name;
        {
            auto book = std::make_unique<Book>(BookConfig{1, 100'000, 1 << 20});
            workload(*book, &r);
        }
        r.add.finish(); r.match.finish(); r.cancel.finish();
        // Judge runs by their typical latency, not by noise.
        r.total_ns = r.add.p50 + r.match.p50 + r.cancel.p50;

        auto book = std::make_unique<Book>(BookConfig{1, 100'000, 1 << 20});
        auto t0 = Clock::now();
        std::size_t ops = workload(*book, nullptr);
        r.ops_per_s = static_cast<double>(ops) / (ns_since(t0) * 1e-9);

        if (run == 0 || r.total_ns < best.total_ns) best = std::move(r);
    }
    std::printf("%-28s add %5.0f/%5.0f  match %5.0f/%5.0f  cancel %5.0f/%5.0f ns  %.1fM ops/s\n", name,
                best.add.p50, best.add.p99, best.match.p50, best.match.p99, best.cancel.p50, best.cancel.p99,
                best.ops_per_s / 1e6);
    return best;
}

// Log-spaced bins from the fastest sample to a few times the p99.
static void write_hist(std::FILE* f, const Op& op) {
    std::vector<double> c = op.ns;
    double lo = std::max(1.0, pct(c, 0.001));
    double hi = std::max(lo * 2, op.p99 * 3);
    std::vector<int> counts(HIST_BINS, 0);
    double span = std::log(hi / lo);
    for (double x : op.ns) {
        int b = x <= lo ? 0 : static_cast<int>(std::log(x / lo) / span * HIST_BINS);
        ++counts[std::min(b, HIST_BINS - 1)];
    }
    std::fprintf(f, "{\"lo\":%.1f,\"hi\":%.1f,\"counts\":[", lo, hi);
    for (int i = 0; i < HIST_BINS; ++i) std::fprintf(f, "%s%d", i ? "," : "", counts[i]);
    std::fputs("]}", f);
}

static void write_op(std::FILE* f, const Op& op) {
    std::fprintf(f, "{\"p50\":%.1f,\"p99\":%.1f,\"hist\":", op.p50, op.p99);
    write_hist(f, op);
    std::fputc('}', f);
}

// Cost of reading the clock, which is inside every number above.
static double timer_overhead_ns() {
    std::vector<double> v;
    for (int i = 0; i < 100'000; ++i) {
        auto t0 = Clock::now();
        v.push_back(ns_since(t0));
    }
    return pct(v, 0.5);
}

// Time per call of fn, p50 over many calls.
template <class F>
static double p50_of(int n, F&& fn) {
    std::vector<double> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto t0 = Clock::now();
        fn(i);
        v.push_back(ns_since(t0));
    }
    return pct(v, 0.5);
}

struct Breakdown {
    double decode = 0, book = 0, publish = 0, socket = 0, rtt_p50 = 0, rtt_p99 = 0;
    bool have_rtt = false;
};

// Round trip for a resting order against the real gateway over loopback.
static bool measure_rtt(const std::string& gateway, std::vector<double>& rtt) {
    int port = 9400 + getpid() % 500;
    pid_t pid = fork();
    if (pid == 0) {
        int dev_null = open("/dev/null", O_WRONLY);
        dup2(dev_null, 1);
        std::string p = std::to_string(port);
        execl(gateway.c_str(), gateway.c_str(), p.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    int fd = -1;
    for (int tries = 0; tries < 100 && fd < 0; ++tries) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<std::uint16_t>(port));
        if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) fd = s;
        else { close(s); usleep(100'000); }
    }
    bool ok = fd >= 0;
    if (ok) {
        int on = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
        proto::Buffer out;
        std::uint8_t in[256];
        for (int i = 0; i < RTT_ORDERS && ok; ++i) {
            out.clear();
            // Buys well under the touch, so every one just rests.
            proto::encode(out, proto::NewOrder{static_cast<std::uint64_t>(i + 1), Side::Buy, proto::OrderType::Limit,
                                               REF - 1000 - i % 500, 100});
            auto t0 = Clock::now();
            ok = send(fd, out.data(), out.size(), 0) == static_cast<ssize_t>(out.size());
            // The ack is one 29-byte frame.
            std::size_t got = 0;
            while (ok && got < 29) {
                ssize_t n = recv(fd, in + got, sizeof in - got, 0);
                if (n <= 0) ok = false; else got += static_cast<std::size_t>(n);
            }
            if (ok && i >= 1000) rtt.push_back(ns_since(t0));   // the first ones warm things up
        }
        close(fd);
    }
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    return ok && !rtt.empty();
}

static std::string cpu_name() {
    char buf[256];
    std::size_t n = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &n, nullptr, 0) == 0) return buf;
    return "unknown";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out.json> [gateway binary]\n", argv[0]);
        return 1;
    }
    std::string self = argv[0];
    std::string gateway = argc > 2 ? argv[2] : self.substr(0, self.find_last_of('/') + 1) + "stag_gateway";
#ifndef NDEBUG
    std::fprintf(stderr, "warning: not a Release build, the numbers will be slow\n");
#endif

    double overhead = timer_overhead_ns();
    std::printf("clock read costs about %.0f ns and is inside every number\n", overhead);

    Result r1 = measure<MapBook>("std::map + std::list");
    Result r2 = measure<StdIndexOrderBook>("flat array + FIFO + pool");
    Result r3 = measure<OrderBook>("open-addressed id map");

    // Where the time goes for one resting order, from the wire in to the ack out.
    Breakdown bd;
    proto::Buffer frame;
    proto::encode(frame, proto::NewOrder{1, Side::Buy, proto::OrderType::Limit, REF - 10, 100});
    proto::NewOrder decoded{};
    bd.decode = p50_of(200'000, [&](int) { proto::decode(&frame[3], frame.size() - 3, decoded); });
    {
        Exchange ex({1, 100'000, 1 << 20}, [](int, const proto::Buffer&) {}, [](const proto::Buffer&) {});
        bd.publish = p50_of(RTT_ORDERS, [&](int i) {
            ex.on_new_order(1, {static_cast<std::uint64_t>(i), Side::Buy, proto::OrderType::Limit, REF - 1000 - i % 500, 100});
        });
    }
    bd.book = r3.add.p50;
    std::vector<double> rtt;
    if (measure_rtt(gateway, rtt)) {
        bd.have_rtt = true;
        bd.rtt_p50 = pct(rtt, 0.5);
        bd.rtt_p99 = pct(rtt, 0.99);
        std::printf("gateway round trip over loopback: p50 %.0f ns, p99 %.0f ns\n", bd.rtt_p50, bd.rtt_p99);
    } else {
        std::fprintf(stderr, "couldn't reach %s, skipping the round trip\n", gateway.c_str());
    }
    // These tiny timings are mostly clock read, so take it off.
    bd.decode = std::max(0.0, bd.decode - overhead);
    bd.publish = std::max(0.0, bd.publish - overhead);
    // Exchange::on_new_order covers the book and the encodes. Taking the book
    // out of it leaves the encoding and publishing.
    double exchange_total = bd.publish;
    bd.publish = std::max(0.0, exchange_total - bd.book);
    bd.socket = bd.have_rtt ? std::max(0.0, bd.rtt_p50 - exchange_total - bd.decode) : 0;

    std::FILE* f = std::fopen(argv[1], "w");
    if (!f) {
        std::perror(argv[1]);
        return 1;
    }
    std::time_t now = std::time(nullptr);
    char date[16];
    std::strftime(date, sizeof date, "%Y-%m-%d", std::gmtime(&now));
    std::fprintf(f, "{\"date\":\"%s\",\"cpu\":\"%s\",\"compiler\":\"%s\",\"orders\":%d,\"runs\":%d,\"timerNs\":%.0f,\n",
                 date, cpu_name().c_str(), __VERSION__, N, RUNS, overhead);
    std::fputs("\"add\":", f);     write_op(f, r3.add);
    std::fputs(",\"match\":", f);  write_op(f, r3.match);
    std::fputs(",\"cancel\":", f); write_op(f, r3.cancel);
    std::fprintf(f, ",\"opsPerSec\":%.0f,\n\"variants\":[", r3.ops_per_s);
    int k = 0;
    for (const Result* r : {&r1, &r2, &r3})
        std::fprintf(f, "%s{\"name\":\"%s\",\"p50\":%.1f,\"p99\":%.1f,\"opsPerSec\":%.0f}", k++ ? "," : "",
                     r->name.c_str(), r->add.p50, r->add.p99, r->ops_per_s);
    std::fprintf(f, "],\n\"wire\":{\"haveRtt\":%s,\"decode\":%.1f,\"book\":%.1f,\"publish\":%.1f,\"socket\":%.1f,\"rttP50\":%.1f,\"rttP99\":%.1f}}\n",
                 bd.have_rtt ? "true" : "false", bd.decode, bd.book, bd.publish, bd.socket, bd.rtt_p50, bd.rtt_p99);
    std::fclose(f);
    std::printf("wrote %s\n", argv[1]);
}

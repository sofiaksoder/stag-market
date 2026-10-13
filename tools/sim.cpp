// Writes a journal of a simulated session.
//   stag_sim <seed> <steps> <out.journal>
#include <cstdio>
#include <cstdlib>

#include "sim.hpp"

using namespace stag;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <seed> <steps> <out.journal>\n", argv[0]);
        return 1;
    }
    std::uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    int steps = std::atoi(argv[2]);

    OrderBook::Config cfg{1, 100'000, 1 << 20};
    JournalWriter journal(argv[3], cfg, seed);
    if (!journal.ok()) {
        std::perror(argv[3]);
        return 1;
    }

    // Start at 09:30 on 2026-10-12 (UTC), so replays have a believable date.
    Sim sim(seed, 1'791'797'400ull * 1'000'000'000ull);
    Exchange ex(cfg, [](int, const proto::Buffer&) {}, {}, sim.clock());
    ex.set_journal(&journal);
    sim.seed_book(ex);
    for (int i = 0; i < steps; ++i) sim.step(ex);
    std::printf("seed %llu: %d steps, last seq %llu\n", static_cast<unsigned long long>(seed), steps,
                static_cast<unsigned long long>(ex.md_seq()));
}

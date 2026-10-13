#pragma once
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "exchange.hpp"

// A seeded stand-in for the Python bots. Same seed, same session.
// It drives the exchange directly and keeps its own clock, so it runs
// far faster than real time.
namespace stag {

constexpr Price REF_PX = 14250;   // 142.50
constexpr std::uint64_t STEP_NS = 300'000'000;

class Sim {
public:
    // Take the clock from the sim so the exchange (and journal) see simulated time.
    explicit Sim(std::uint64_t seed, std::uint64_t start_ns) : rng_(seed), now_(start_ns) {}

    std::uint64_t now() const { return now_; }
    Exchange::Clock clock() { return [this] { return now_; }; }

    // Two market makers put some depth on the book first.
    void seed_book(Exchange& ex) {
        for (Price off = 1; off <= 8; ++off) {
            place(ex, 3, Side::Buy, REF_PX - off, 300);
            place(ex, 3, Side::Sell, REF_PX + off, 300);
            now_ += 1'000'000;
        }
    }

    void step(Exchange& ex) {
        now_ += STEP_NS;
        refill(ex);
        bias_ = bias_ * 0.95 + gauss() * 0.6;
        fair_ += bias_ + gauss();
        fair_ += (REF_PX - fair_) * 0.01;

        int actions = 1 + static_cast<int>(rng_() % 3);
        for (int i = 0; i < actions; ++i) {
            now_ += 1'000'000 + rng_() % 20'000'000;   // actions land a few ms apart
            int conn = 1 + static_cast<int>(rng_() % 2);
            double r = uniform();
            if (r < 0.52) {
                Side s = rng_() & 1 ? Side::Buy : Side::Sell;
                Price off = static_cast<Price>(rng_() % 8) - 1;
                Price px = static_cast<Price>(std::llround(fair_)) + (s == Side::Buy ? -off : off);
                static const Qty sizes[] = {100, 200, 300, 500};
                // Don't pile more than 2,400 on one level.
                if (ex.book().depth_at(s, px) < 2400) place(ex, conn, s, std::max<Price>(px, 1), sizes[rng_() % 4]);
            } else if (r < 0.78) {
                cancel_one(ex, conn);
            } else {
                static const Qty sizes[] = {100, 200, 300};
                proto::NewOrder m{next_cl_++, rng_() & 1 ? Side::Buy : Side::Sell, proto::OrderType::Market, 0, sizes[rng_() % 3]};
                ex.on_new_order(conn, m);
            }
        }
    }

private:
    // The market makers top up empty levels near the price so a side never dries up.
    void refill(Exchange& ex) {
        Price mid = static_cast<Price>(std::llround(fair_));
        for (Price k = 1; k <= 6; ++k) {
            if (ex.book().depth_at(Side::Buy, mid - k) == 0) place(ex, 3, Side::Buy, std::max<Price>(mid - k, 1), 200);
            if (ex.book().depth_at(Side::Sell, mid + k) == 0) place(ex, 3, Side::Sell, mid + k, 200);
            now_ += 100'000;
        }
    }

    double uniform() { return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0); }
    double gauss() {   // Box-Muller, so the numbers don't depend on the standard library
        double u1 = std::max(uniform(), 1e-12), u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    void place(Exchange& ex, int conn, Side s, Price px, Qty q) {
        ex.on_new_order(conn, {next_cl_++, s, proto::OrderType::Limit, px, q});
        // The exchange hands out ids 1, 2, 3... so the one just used is known.
        if (ex.book().has_order(ex.last_order_id())) live_.push_back({conn, ex.last_order_id()});
    }

    void cancel_one(Exchange& ex, int conn) {
        // Drop ids that already filled, then pick one of this client's.
        for (std::size_t i = 0; i < live_.size();)
            if (!ex.book().has_order(live_[i].second)) { live_[i] = live_.back(); live_.pop_back(); }
            else ++i;
        std::vector<std::size_t> mine;
        for (std::size_t i = 0; i < live_.size(); ++i) if (live_[i].first == conn) mine.push_back(i);
        if (mine.empty()) return;
        std::size_t k = mine[rng_() % mine.size()];
        ex.on_cancel(conn, {live_[k].second});
        live_[k] = live_.back();
        live_.pop_back();
    }

    std::mt19937_64 rng_;
    std::uint64_t   now_;
    double          fair_ = REF_PX;
    double          bias_ = 0;
    std::uint64_t   next_cl_ = 1;
    std::vector<std::pair<int, OrderId>> live_;
};

}  // namespace stag

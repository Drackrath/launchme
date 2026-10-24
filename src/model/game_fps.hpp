#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <vector>

namespace game_fps {
enum class Status { disabled, waiting, collecting, ready, denied, failed };
struct Reading {
    Status status = Status::disabled;
    double fps = 0, low = 0;
    bool operator==(const Reading&) const = default;
};

// Present timestamps (seconds), kept per swapchain: summing two swapchains would
// double the FPS. The busiest recently active chain supplies both measurements.
// 1% low = reciprocal of the mean of the slowest ceil(N / 100) frame intervals
// in the last 60 seconds, not the reciprocal of the 99th percentile.
class History {
public:
    void clear() { chains_.clear(); }
    void present(uint64_t chain, double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        if (!chains_.contains(chain) && chains_.size() >= 32) {
            const auto oldest = std::min_element(chains_.begin(), chains_.end(), [](const auto& a, const auto& b) {
                return a.second.back() < b.second.back();
            });
            chains_.erase(oldest);
        }
        auto& frames = chains_[chain];
        if (!frames.empty() && seconds <= frames.back()) return;
        frames.push_back(seconds);
        while (frames.size() > 32768 || (frames.size() > 1 && frames.front() < seconds - 60)) frames.pop_front();
    }
    Reading read(double now) const {
        const std::deque<double>* best = nullptr;
        size_t best_count = 0;
        for (const auto& [chain, frames] : chains_) {
            (void)chain;
            if (frames.size() < 2 || now - frames.back() > 2.5 || frames.back() > now) continue;
            const auto first = std::lower_bound(frames.begin(), frames.end(), frames.back() - 1);
            const auto count = static_cast<size_t>(frames.end() - first);
            if (count > best_count) { best_count = count; best = &frames; }
        }
        if (!best || best_count < 2) return {Status::collecting};
        const auto& frames = *best;
        const double span = frames.back() - frames[frames.size() - best_count];
        if (span < 0.2) return {Status::collecting};
        Reading result{Status::ready, (best_count - 1) / span};
        std::vector<double> intervals;
        for (size_t i = 1; i < frames.size(); ++i)
            if (frames[i - 1] >= now - 60) intervals.push_back(frames[i] - frames[i - 1]);
        if (intervals.size() >= 100) {
            const size_t count = (intervals.size() + 99) / 100;
            std::partial_sort(intervals.begin(), intervals.begin() + count, intervals.end(), std::greater<double>{});
            result.low = count / std::accumulate(intervals.begin(), intervals.begin() + count, 0.0);
        }
        return result;
    }
private:
    std::map<uint64_t, std::deque<double>> chains_;
};
}

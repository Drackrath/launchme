#pragma once

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// A short boundary tap, stopped by its own worker even when the UI stops polling.
// The sink is replaceable so tests never send output to a physical controller.
class PadRumble {
public:
    struct Target { std::wstring id; bool bluetooth = false; bool steam = false; }; // steam: Steam Controller click.
    using Output = std::function<void(const Target&, bool)>;
    explicit PadRumble(Output output);
    ~PadRumble();
    void pulse(Target target);
    void stop();
private:
    void run();
    Output output_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Target> pending_;
    std::chrono::steady_clock::time_point next_pulse_{};
    uint64_t generation_ = 0;
    bool closing_ = false;
    std::thread worker_;
};

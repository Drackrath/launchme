#include "../src/platform/background_work.hpp"
#include <iostream>
#include <stdexcept>

namespace {
struct ThrowingCopy {
    std::atomic_int& errors;
    explicit ThrowingCopy(std::atomic_int& count) : errors(count) {}
    ThrowingCopy(ThrowingCopy&&) = default;
    ThrowingCopy(const ThrowingCopy& other) : errors(other.errors) { throw std::runtime_error("worker capture failed"); }
    void operator()(std::exception_ptr error) const {
        try { std::rethrow_exception(error); } catch (const std::runtime_error&) { ++errors; }
    }
};
}

int main() {
    using namespace std::chrono_literals;
    {
        // Finished one-off jobs are reaped on the next start instead of piling up.
        BackgroundWork work;
        std::atomic_int ran{0};
        for (int i = 0; i < 8; ++i) if (!work.start([&] { ++ran; })) return 1;
        for (int i = 0; i < 200 && ran.load() < 8; ++i) std::this_thread::sleep_for(5ms);
        std::this_thread::sleep_for(20ms);
        if (ran.load() != 8 || work.owned() != 0) return 2;
        // A throwing body must not terminate the process.
        if (!work.start([] { throw 1; })) return 3;
        if (!work.shutdown(2s)) return 4;
        if (work.start([] {})) return 5; // No new work after shutdown.
    }
    {
        // Cooperative stop: the owner flips its flag, shutdown joins within budget.
        BackgroundWork work;
        auto alive = std::make_shared<std::atomic_bool>(true);
        for (int i = 0; i < 3; ++i) work.start([alive] { while (alive->load()) std::this_thread::sleep_for(5ms); });
        alive->store(false);
        if (!work.shutdown(2s) || work.owned() != 0) return 6;
    }
    {
        // Every independently owned library/artwork loop has an exception boundary.
        // One failing worker and even a failing error reporter leave siblings owned.
        std::atomic_bool stop{false};
        std::atomic_int errors{0}, finished{0};
        BackgroundWork work;
        struct Cleanup { std::atomic_bool& stop; BackgroundWork& work; ~Cleanup() { stop = true; work.shutdown(2s); } } cleanup{stop, work};
        for (int i = 0; i < 3; ++i) if (!work.start([&] {
            while (!stop) std::this_thread::sleep_for(1ms);
            ++finished;
        })) return 9;
        if (!work.start([] { throw 42; }, [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); } catch (int value) { if (value == 42) ++errors; }
            throw 7;
        })) return 10;
        for (int i = 0; i < 1000 && !errors; ++i) std::this_thread::sleep_for(1ms);
        stop = true;
        if (!work.shutdown(2s) || errors != 1 || finished != 3 || work.owned()) return 11;
    }
    {
        // Failure while constructing a new thread's captures, after an earlier
        // worker started: roll back its slot/count, report it, and still join all.
        std::atomic_int ran{0}, errors{0};
        BackgroundWork work;
        if (!work.start([&] { ++ran; })) return 12;
        BackgroundWork::ErrorHandler error{ThrowingCopy{errors}};
        if (work.start([&] { ran += 100; }, std::move(error)) || errors != 1) return 13;
        if (!work.start([&] { ++ran; }) || !work.shutdown(2s) || ran != 2 || work.owned()) return 14;
    }
    {
        // A worker stuck past the budget is released, never waited on forever.
        BackgroundWork work;
        auto release = std::make_shared<std::atomic_bool>(false);
        work.start([release] { while (!release->load()) std::this_thread::sleep_for(5ms); });
        const auto started = std::chrono::steady_clock::now();
        if (work.shutdown(50ms)) return 7;
        if (std::chrono::steady_clock::now() - started > 1s) return 8;
        release->store(true);
        std::this_thread::sleep_for(30ms);
    }
    std::cout << "Owned workers, reaping, bounded shutdown and exception isolation passed\n";
    return 0;
}

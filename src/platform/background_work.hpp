#pragma once
#include "diagnostics.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

// Owns every launcher background thread instead of detaching them. Finished
// one-off jobs are reaped (joined) on the next start(); shutdown() waits a
// bounded time for all bodies to return, so normally no worker is still running
// while statics, COM or WinHTTP are torn down at process exit. A worker stuck
// in a blocking call past the budget is released exactly as before (detached).
//
// Stopping stays cooperative: owners first flip their alive/cancel flags and
// notify their condition variables, then call shutdown(). Threads started by a
// worker must be joined by that worker; start() is meant for the owner thread.
class BackgroundWork {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;
    BackgroundWork() = default;
    ~BackgroundWork() { shutdown(std::chrono::milliseconds(0)); }
    BackgroundWork(const BackgroundWork&) = delete;
    BackgroundWork& operator=(const BackgroundWork&) = delete;

    // on_error runs on the worker, or on the caller when thread setup fails;
    // like body, it must not borrow owner state that can outlive shutdown's budget.
    bool start(std::function<void()> body, ErrorHandler on_error = {}) {
        std::lock_guard lock(mutex_);
        if (stopped_) return false;
        reap();
        const auto finish = [](const std::shared_ptr<Shared>& shared) {
            std::lock_guard count(shared->mutex);
            --shared->active;
            shared->finished.notify_all();
        };
        try {
            auto done = std::make_shared<std::atomic_bool>(false);
            // Allocate ownership before starting a thread: a throwing list
            // insertion must never destroy a temporary, still-joinable thread.
            workers_.push_back({{}, done});
            { std::lock_guard count(shared_->mutex); ++shared_->active; }
            try {
                workers_.back().thread = std::thread([body = std::move(body), done, shared = shared_, finish, on_error] {
                    try { body(); } catch (...) { report(on_error, std::current_exception()); }
                    done->store(true);
                    finish(shared);
                });
            } catch (...) {
                workers_.pop_back();
                finish(shared_);
                throw;
            }
        } catch (...) {
            report(on_error, std::current_exception());
            return false;
        }
        return true;
    }

    // Threads currently owned (running or finished but not yet reaped).
    size_t owned() {
        std::lock_guard lock(mutex_);
        reap();
        return workers_.size();
    }

    // True when every body returned within the budget.
    bool shutdown(std::chrono::milliseconds budget) {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        {
            std::unique_lock count(shared_->mutex);
            shared_->finished.wait_for(count, budget, [&] { return shared_->active == 0; });
        }
        bool all = true;
        for (auto& worker : workers_) {
            if (!worker.thread.joinable()) continue;
            if (worker.done->load()) worker.thread.join();
            else { worker.thread.detach(); all = false; }
        }
        workers_.clear();
        return all;
    }

private:
    static void report(const ErrorHandler& handler, std::exception_ptr error) noexcept {
        // Every failed worker leaves a line in launchme.log, with or without its own handler.
        try { diagnostics::log("error", "Background worker failed: " + diagnostics::describe(error)); } catch (...) {}
        try { if (handler) handler(error); } catch (...) { /* Reporting must not terminate a worker either. */ }
    }
    struct Worker { std::thread thread; std::shared_ptr<std::atomic_bool> done; };
    struct Shared { std::mutex mutex; std::condition_variable finished; size_t active = 0; };
    void reap() {
        workers_.remove_if([](Worker& worker) {
            if (!worker.done->load()) return false;
            if (worker.thread.joinable()) worker.thread.join();
            return true;
        });
    }
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::mutex mutex_;
    std::list<Worker> workers_;
    bool stopped_ = false;
};

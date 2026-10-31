#pragma once
#include "diagnostics.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif

// Owns every launcher background thread instead of detaching them. A thread whose job returned
// waits up to keep_alive for the next one (at most max_idle such threads), so the launcher's many
// short jobs (a lookup, a file read, a save) reuse threads instead of creating one each; a thread
// with nothing to do leaves and is reaped (joined) on the next start(). shutdown() waits a bounded
// time for all bodies to return, so normally no worker is still running while statics, COM or
// WinHTTP are torn down at process exit. A worker stuck in a blocking call past the budget is
// released exactly as before (detached).
//
// Every job starts as on a fresh thread: a priority or background mode a job set is reset after
// it (Windows), and COM apartments are each job's own to balance (they are).
//
// Stopping stays cooperative: owners first flip their alive/cancel flags and
// notify their condition variables, then call shutdown(). Threads started by a
// worker must be joined by that worker; start() is meant for the owner thread.
class BackgroundWork {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;
    static constexpr auto keep_alive = std::chrono::seconds(10);
    static constexpr size_t max_idle = 4;
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
        try {
            Job job{std::move(body), on_error}; // The handler copied: a capture that cannot be made fails here.
            {
                std::lock_guard shared(shared_->mutex);
                ++shared_->active;
                if (shared_->idle > shared_->queue.size()) { // A waiting thread takes it.
                    shared_->queue.push_back(std::move(job));
                    shared_->work.notify_one();
                    return true;
                }
            }
            auto done = std::make_shared<std::atomic_bool>(false), busy = std::make_shared<std::atomic_bool>(true);
            // Allocate ownership before starting a thread: a throwing list
            // insertion must never destroy a temporary, still-joinable thread.
            try { workers_.push_back({{}, done, busy}); } catch (...) { finish(shared_); throw; }
            try {
                workers_.back().thread = std::thread([job = std::move(job), done, busy, shared = shared_]() mutable {
                    serve(std::move(job), *busy, shared);
                    done->store(true);
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

    // Threads currently running a job (or finished but not yet reaped); one waiting for the next job
    // does not count.
    size_t owned() {
        std::lock_guard lock(mutex_);
        reap();
        size_t running = 0;
        for (const auto& worker : workers_) if (worker.busy->load()) ++running;
        return running;
    }

    // True when every body returned within the budget.
    bool shutdown(std::chrono::milliseconds budget) {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        {
            std::unique_lock count(shared_->mutex);
            shared_->finished.wait_for(count, budget, [&] { return shared_->active == 0; });
            // From here no thread takes another job: waiting ones leave at once.
            shared_->stopping = true;
            shared_->work.notify_all();
        }
        bool all = true;
        for (auto& worker : workers_) {
            if (!worker.thread.joinable()) continue;
            if (worker.done->load() || !worker.busy->load()) worker.thread.join();
            else { worker.thread.detach(); all = false; }
        }
        workers_.clear();
        return all;
    }

private:
    struct Job { std::function<void()> body; ErrorHandler on_error; };
    struct Shared {
        std::mutex mutex;
        std::condition_variable finished, work;
        size_t active = 0; // Jobs started and not finished (queued ones too).
        size_t idle = 0;   // Threads waiting for a job.
        std::deque<Job> queue;
        bool stopping = false;
    };
    struct Worker { std::thread thread; std::shared_ptr<std::atomic_bool> done, busy; };

    static void report(const ErrorHandler& handler, std::exception_ptr error) noexcept {
        // Every failed worker leaves a line in launchme.log, with or without its own handler.
        try { diagnostics::log("error", "Background worker failed: " + diagnostics::describe(error)); } catch (...) {}
        try { if (handler) handler(error); } catch (...) { /* Reporting must not terminate a worker either. */ }
    }
    static void finish(const std::shared_ptr<Shared>& shared) {
        std::lock_guard count(shared->mutex);
        --shared->active;
        shared->finished.notify_all();
    }
    // A thread's life: its first job, then each one handed over while it waits.
    static void serve(Job job, std::atomic_bool& busy, const std::shared_ptr<Shared>& shared) {
        while (true) {
            try { job.body(); } catch (...) { report(job.on_error, std::current_exception()); }
            job = {};
#ifdef _WIN32
            // The next job starts as on a fresh thread (some yield the CPU and disk while they run).
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
            std::unique_lock lock(shared->mutex);
            busy.store(false);
            --shared->active;
            shared->finished.notify_all();
            if (shared->stopping || shared->idle >= max_idle) return;
            ++shared->idle;
            shared->work.wait_for(lock, keep_alive, [&] { return shared->stopping || !shared->queue.empty(); });
            --shared->idle;
            if (shared->stopping || shared->queue.empty()) return;
            job = std::move(shared->queue.front());
            shared->queue.pop_front();
            busy.store(true);
        }
    }
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

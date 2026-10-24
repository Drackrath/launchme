#pragma once
#include "worker_messages.hpp"
#include "../platform/background_work.hpp"
#include <condition_variable>
#include <mutex>

// Owns the list description cache, debounce, request generation and worker queue.
// UI methods run on the owner thread; the worker retains only its shared Queue.
class LibraryPreview {
public:
    using Clock = std::chrono::steady_clock;
    using Reader = std::function<GameDetails(uint32_t, uint64_t)>;
    LibraryPreview() = default;
    LibraryPreview(const LibraryPreview&) = delete;
    LibraryPreview& operator=(const LibraryPreview&) = delete;
    ~LibraryPreview() { stop(); }
    bool start(BackgroundWork& work, HWND window, Reader reader = {});
    void settle(Clock::time_point now = Clock::now()) { due_ = now + std::chrono::milliseconds(320); }
    bool request(const Game& game, uint64_t account, Clock::time_point now = Clock::now()) {
        if (!started_ || now < due_ || !steamArtwork(game) || !game.app_id ||
            cache_.contains(game.app_id) || requested_ == game.app_id) return false;
        std::lock_guard lock(queue_->mutex);
        if (queue_->stopped) return false;
        requested_ = game.app_id;
        queue_->pending = std::pair{game.app_id, account};
        queue_->wake.notify_one();
        return true;
    }
    bool accept(PreviewMessage message) {
        if (message.generation != generation()) return false;
        if (requested_ == message.app_id) requested_ = 0;
        if (cache_.size() >= 256) cache_.clear();
        cache_[message.app_id] = std::move(message.details);
        return true;
    }
    const GameDetails* find(const Game& game) const {
        const auto found = cache_.find(game.app_id);
        return steamArtwork(game) && found != cache_.end() ? &found->second : nullptr;
    }
    uint32_t generation() const { return queue_->generation.load(); }
    void clear() {
        cache_.clear(); requested_ = 0;
        std::lock_guard lock(queue_->mutex);
        ++queue_->generation; queue_->pending.reset();
    }
    void stop() {
        { std::lock_guard lock(queue_->mutex); queue_->stopped = true; ++queue_->generation; queue_->pending.reset(); }
        queue_->wake.notify_all();
    }
private:
    struct Queue {
        std::mutex mutex;
        std::condition_variable wake;
        std::optional<std::pair<uint32_t, uint64_t>> pending;
        std::atomic_uint32_t generation{0};
        bool stopped = false;
    };
    std::shared_ptr<Queue> queue_ = std::make_shared<Queue>();
    std::unordered_map<uint32_t, GameDetails> cache_;
    uint32_t requested_ = 0;
    Clock::time_point due_{};
    bool started_ = false;
};

#include "library_preview.hpp"
#include "../library/steam.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/user_config.hpp"
#include "../store/game_details.hpp"
#include <tuple>

bool LibraryPreview::start(BackgroundWork& work, HWND window, Reader reader) {
    if (started_) return true;
    started_ = work.start([queue = queue_, window, reader = std::move(reader)]() mutable {
        if (!reader) {
            const auto cache = userConfigDirectory() / L"metadata";
            const auto steam = findSteamRoot();
            reader = [cache, steam](uint32_t app_id, uint64_t account) {
                auto details = fetchGameDetails(app_id, cache);
                const auto stats = readSteamStats(steam, account, app_id);
                details.playtime_minutes = stats.playtime_minutes;
                details.achievements_unlocked = stats.achievements_unlocked;
                details.achievements_total = stats.achievements_total;
                return details;
            };
        }
        while (true) {
            auto message = std::make_unique<PreviewMessage>();
            uint64_t account = 0;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return queue->stopped || queue->pending.has_value(); });
                if (queue->stopped) return;
                std::tie(message->app_id, account) = *queue->pending;
                message->generation = queue->generation.load();
                queue->pending.reset();
            }
            message->details = reader(message->app_id, account);
            if (queue->generation.load() != message->generation) continue;
            if (PostMessageW(window, preview_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        }
    });
    return started_;
}

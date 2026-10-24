#include "../src/app/library_preview.hpp"
#include <future>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    const auto window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    if (!window) return 1;
    struct Cleanup {
        HWND window;
        ~Cleanup() {
            MSG message{};
            while (PeekMessageW(&message, window, preview_message, preview_message, PM_REMOVE))
                delete reinterpret_cast<PreviewMessage*>(message.lParam);
            DestroyWindow(window);
        }
    } cleanup{window};
    BackgroundWork work;
    LibraryPreview preview;
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    const auto release_future = release.get_future().share();
    std::atomic_int calls = 0;
    if (!preview.start(work, window, [&](uint32_t, uint64_t account) {
        if (++calls == 1) { entered.set_value(); release_future.wait(); }
        GameDetails details; details.description = std::to_wstring(account); return details;
    })) return 2;
    struct Stop {
        LibraryPreview& preview; BackgroundWork& work; std::promise<void>& release;
        ~Stop() { try { release.set_value(); } catch (...) {} preview.stop(); work.shutdown(2s); }
    } stop{preview, work, release};
    Game first{620, L"First"}, second{730, L"Second"};
    const auto now = LibraryPreview::Clock::now();
    preview.settle(now);
    if (preview.request(first, 1, now + 319ms) || calls != 0) return 3;
    if (!preview.request(first, 1, now + 320ms) || preview.request(first, 1, now + 400ms)) return 4;
    if (entered_future.wait_for(2s) != std::future_status::ready) return 5;
    const auto old = preview.generation();
    preview.clear(); // Language/account changed while the first read is blocked.
    if (preview.accept({old, first.app_id, GameDetails{L"Stale"}}) || preview.find(first)) return 6;
    if (!preview.request(first, 2, now + 400ms) || !preview.request(second, 2, now + 400ms)) return 7;
    release.set_value();
    MSG message{};
    const auto until = LibraryPreview::Clock::now() + 2s;
    while (!PeekMessageW(&message, window, preview_message, preview_message, PM_REMOVE)) {
        if (LibraryPreview::Clock::now() >= until) return 8;
        std::this_thread::sleep_for(1ms);
    }
    auto result = std::unique_ptr<PreviewMessage>(reinterpret_cast<PreviewMessage*>(message.lParam));
    if (result->app_id != second.app_id || result->details.description != L"2" || calls != 2) return 9;
    if (!preview.accept(std::move(*result)) || !preview.find(second) || preview.request(second, 2, now + 500ms)) return 10;
    preview.clear();
    if (preview.find(second)) return 11;
    preview.stop();
    if (preview.request(first, 2, now + 500ms) || !work.shutdown(2s)) return 12;
    std::cout << "Preview debounce, latest request, stale results, cache reset and shutdown passed\n";
}

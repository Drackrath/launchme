#include "../src/app/message_dispatch.hpp"
#include "../src/app/library_snapshot.hpp"
#include <iostream>

namespace {
struct Payload {
    int& destroyed;
    ~Payload() { ++destroyed; }
};
struct Owner {
    std::unique_ptr<Payload> kept;
    void take(std::unique_ptr<Payload> value) { kept = std::move(value); }
    void fail(std::unique_ptr<Payload>) { throw 1; }
};
struct Receiver {
    UINT received = 0;
#define LAUNCHME_MESSAGE(id, offset, type, handler) \
    void handler(std::unique_ptr<type>) { received = id; }
#include "../src/app/worker_messages.def"
#undef LAUNCHME_MESSAGE
};
}
int main() {
    int destroyed = 0;
    Owner owner;
    const auto payload = [&] { return reinterpret_cast<LPARAM>(new Payload{destroyed}); };
    posted_message::deliver(static_cast<Owner*>(nullptr), &Owner::take, payload());
    if (destroyed != 1) return 1;
    posted_message::deliver(&owner, &Owner::take, payload());
    if (destroyed != 1 || !owner.kept) return 2;
    owner.kept.reset();
    if (destroyed != 2) return 3;
    try { posted_message::deliver(&owner, &Owner::fail, payload()); return 4; } catch (int) {}
    if (destroyed != 3) return 5;
    posted_message::deliver(&owner, &Owner::fail, 0); // Null payload never enters a handler.

    Receiver receiver;
#define LAUNCHME_MESSAGE(id, offset, type, handler) \
    if (!dispatchWorkerMessage(&receiver, id, reinterpret_cast<LPARAM>(new type)) || receiver.received != id) return 6;
#include "../src/app/worker_messages.def"
#undef LAUNCHME_MESSAGE
    if (dispatchWorkerMessage(&receiver, WM_APP + 999, 0)) return 7;

    const auto window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    if (!window) return 8;
    struct Cleanup { HWND window; ~Cleanup() { DestroyWindow(window); } } cleanup{window};
#define LAUNCHME_MESSAGE(id, offset, type, handler) \
    { auto value = std::make_unique<type>(); \
      if (!PostMessageW(window, id, 0, reinterpret_cast<LPARAM>(value.get()))) return 9; \
      value.release(); }
#include "../src/app/worker_messages.def"
#undef LAUNCHME_MESSAGE
    PostMessageW(window, WM_APP + 999, 0, 123);
    discardWorkerMessages<Receiver>(window);
    MSG queued{};
    if (PeekMessageW(&queued, window, WM_APP + 1, library_worker_error_message, PM_REMOVE)) return 10;
    if (!PeekMessageW(&queued, window, WM_APP + 999, WM_APP + 999, PM_REMOVE) || queued.lParam != 123) return 11;
    // Force the earliest legal receiver schedule: it consumes and destroys the
    // library before the posting call returns. The poller's snapshot must survive.
    auto library = std::make_unique<LibraryMessage>();
    library->games.push_back(Game{10, L"Visible"});
    library->private_games.push_back(Game{20, L"Private"});
    library->excluded.insert(30);
    SteamLibrary baseline;
    std::vector<Game> received;
    if (!postLibrarySnapshot(window, std::move(library), baseline, [&](HWND, UINT id, WPARAM, LPARAM payload) {
        if (id != library_message) return FALSE;
        auto owned = std::unique_ptr<LibraryMessage>(reinterpret_cast<LibraryMessage*>(payload));
        received = std::move(owned->games);
        owned->private_games.clear();
        return TRUE;
    })) return 12;
    if (received.size() != 1 || baseline.visible.size() != 1 || baseline.visible[0].app_id != 10 ||
        baseline.private_games.size() != 1 || baseline.private_games[0].app_id != 20 || !baseline.excluded.contains(30)) return 13;
    if (postLibrarySnapshot(window, std::make_unique<LibraryMessage>(), baseline,
        [](HWND, UINT, WPARAM, LPARAM) { return FALSE; }) || baseline.visible.size() != 1) return 14;
    // "Pokaż ukryte/prywatne gry": hidden and private games join the others in scan order.
    LibraryMessage shown;
    Game recent{1, L"Recent"}; recent.last_played = 300;
    Game secret{2, L"Secret"}; secret.last_played = 200; secret.visibility = GameVisibility::private_game;
    Game old{3, L"Old"}; old.last_played = 100;
    Game unplayed{4, L"Unplayed"}; unplayed.visibility = GameVisibility::private_game;
    shown.games = {recent, old}; shown.private_games = {unplayed, secret}; shown.excluded = {2, 4, 0x80000005u};
    ShortcutGame hidden; hidden.game = Game{0x80000006u, L"Hidden shortcut"}; hidden.hidden = true;
    shown.shortcuts.push_back(hidden);
    revealPrivateSteamGames(shown);
    if (shown.games.size() != 4 || !shown.private_games.empty() || !shown.excluded.empty() || shown.shortcuts[0].hidden ||
        shown.games[0].app_id != 1 || shown.games[1].app_id != 2 || shown.games[2].app_id != 3 || shown.games[3].app_id != 4 ||
        std::any_of(shown.games.begin(), shown.games.end(), [](const Game& game) { return game.visibility != GameVisibility::public_game; })) return 15;
    std::cout << "Worker payload ownership, routing and queue cleanup passed\n";
}

#pragma once
#include "worker_messages.hpp"
#include "../library/steam.hpp"

#include <algorithm>

// Settings → Library → "Pokaż ukryte/prywatne gry": the UI receives Steam's hidden and private
// games (and hidden shortcuts) as ordinary ones, in scan order. The worker's baseline stays as read.
inline void revealPrivateSteamGames(LibraryMessage& message) {
    if (!message.private_games.empty()) {
        for (auto& game : message.private_games) {
            game.visibility = GameVisibility::public_game;
            message.games.push_back(std::move(game));
        }
        message.private_games.clear();
        std::stable_sort(message.games.begin(), message.games.end(), steamLibraryOrder);
    }
    message.excluded.clear();
    for (auto& shortcut : message.shortcuts) shortcut.hidden = false;
}

// The UI can move from and destroy the payload before PostMessageW returns.
// Take the poller's independent snapshot before publishing; a failed post keeps
// both payload ownership and the previous baseline on the producer's side.
template<class Post = decltype(&PostMessageW)>
bool postLibrarySnapshot(HWND window, std::unique_ptr<LibraryMessage> message,
                         SteamLibrary& baseline, Post post = &PostMessageW) {
    SteamLibrary next{message->games, message->private_games};
    next.excluded.insert(message->excluded.begin(), message->excluded.end());
    if (!post(window, library_message, 0, reinterpret_cast<LPARAM>(message.get()))) return false;
    message.release();
    baseline = std::move(next);
    return true;
}

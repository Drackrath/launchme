#pragma once
#include "background_work.hpp"
#include "game_activity.hpp"
#include "../model/game_fps.hpp"

// Owns no UI pointers. All process inspection and ETW setup/teardown runs on an
// owned worker; the UI consumes only a small latest-value snapshot.
class GameOverlayMonitor {
public:
    struct Snapshot {
        std::optional<GameWindow> target;
        game_fps::Reading fps;
        uint64_t revision = 0;
    };
    GameOverlayMonitor();
    ~GameOverlayMonitor();
    void start(BackgroundWork& work);
    void stop();
    void configure(bool enabled, bool fps, HWND retained = nullptr);
    void candidates(const std::vector<Game>& games);
    Snapshot snapshot() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};

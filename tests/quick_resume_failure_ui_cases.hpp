#include "../src/platform/atomic_file.hpp"

int LauncherUiTest::quickResumeFailures(HWND window) {
    const Game game{4100, L"Recovery fixture"};
    const auto key = gameKey(game);
    const std::vector<GameProcess> processes{{4100, 7}};
    const std::vector<FrozenGame> journal{{key, processes}};
    int suspends = 0, resumes = 0;
    bool journal_before_suspend = true;
    QuickResumeBackend backend;
    backend.find = [&](const Game&) { return processes; };
    backend.suspend = [&](std::vector<GameProcess>& list) {
        ++suspends;
        const auto saved = process_freeze::load(userConfigDirectory() / L"quick-resume.txt");
        journal_before_suspend &= saved && saved->size() == 1 && (*saved)[0].key == key &&
            std::all_of(list.begin(), list.end(), [&](const GameProcess& process) {
                const auto& known = (*saved)[0].processes;
                return std::find(known.begin(), known.end(), process) != known.end();
            });
        return process_freeze::Result::ok;
    };
    backend.resume = [&](const std::vector<GameProcess>&) { ++resumes; return process_freeze::Result::denied; };
    backend.alive = [](const std::vector<GameProcess>&) { return true; };
    backend.focus = [](const std::vector<GameProcess>&) { return true; };
    const auto running = [&](Launcher& app) {
        app.hwnd_ = window; app.execute_ = fakeExecute; app.quick_resume_ = backend;
        app.settings_.quick_resume = true;
        app.game_sessions_.try_emplace(key, 0).first->second.observe(GameActivity::running, 1);
        app.trackSession(key, game, {});
    };
    Launcher app; running(app);
    const auto file = app.quickResumeFile();
    if (!process_freeze::save(file, {})) return 1510;
    app.quick_resume_.save = [](const auto&, const auto&) { return false; };
    if (app.freezeGame(key, true) || suspends || app.sessionState(game) != SessionState::running ||
        app.notifications_.empty() || std::filesystem::exists(file)) return 1511;

    // The write-ahead journal survives a failed final rewrite after suspension.
    int writes = 0;
    app.quick_resume_.save = [&](const auto& path, const auto& state) { return ++writes == 1 && process_freeze::save(path, state); };
    if (!app.freezeGame(key, true) || suspends != 1 || !journal_before_suspend ||
        app.sessionState(game) != SessionState::frozen || process_freeze::load(file) != journal || writes != 2) return 1512;

    // A fresh launcher with Quick Resume OFF must retain a denied recovery and
    // still offer Resume. Retrying it must not suspend other running sessions.
    Launcher restored; restored.hwnd_ = window; restored.execute_ = fakeExecute; restored.quick_resume_ = backend;
    restored.settings_.quick_resume = false;
    restored.restoreFrozenGames();
    if (resumes != 1 || restored.sessionState(game) != SessionState::frozen ||
        restored.actionLabel(game) != L"Wznów" || process_freeze::load(file) != journal || restored.notifications_.empty()) return 1513;
    restored.resumeFrozenGames();
    if (resumes != 2 || restored.sessionState(game) != SessionState::frozen || process_freeze::load(file) != journal) return 1514;
    Game other{4200, L"Other running game"};
    restored.game_sessions_.try_emplace(gameKey(other), 0).first->second.observe(GameActivity::running, 1);
    restored.trackSession(gameKey(other), other, {});
    restored.quick_resume_.resume = [&](const auto&) { ++resumes; return process_freeze::Result::ok; };
    restored.resumeGame(game);
    if (resumes != 3 || suspends != 1 || restored.game_sessions_.at(key).frozen() || std::filesystem::exists(file)) return 1515;

    // Late processes are journaled before suspension too; a failed write must
    // never let that second suspend call happen.
    Launcher late; running(late);
    int finds = 0;
    const std::vector<GameProcess> expanded{{4100, 7}, {4101, 8}};
    late.quick_resume_.find = [&](const Game&) { return ++finds == 1 ? processes : expanded; };
    if (!late.freezeGame(key, true) || suspends != 3 || !journal_before_suspend ||
        process_freeze::load(file) != std::vector<FrozenGame>{{key, expanded}}) return 1516;
    if (!process_freeze::save(file, {})) return 1517;
    Launcher late_failure; running(late_failure); finds = 0;
    late_failure.quick_resume_.find = late.quick_resume_.find;
    late_failure.quick_resume_.save = [&](const auto& path, const std::vector<FrozenGame>& state) {
        if (!state.empty() && state[0].processes.size() > 1) return false;
        return process_freeze::save(path, state);
    };
    if (!late_failure.freezeGame(key, true) || suspends != 4 || !journal_before_suspend ||
        process_freeze::load(file) != journal || late_failure.notifications_.empty()) return 1518;

    // An exited process can be forgotten; a failed read cannot. Mixed valid and
    // malformed records must preserve the whole original journal for recovery.
    Launcher gone; gone.hwnd_ = window; gone.quick_resume_ = backend; gone.settings_.quick_resume = false;
    gone.quick_resume_.resume = [](const auto&) { return process_freeze::Result::gone; };
    gone.restoreFrozenGames();
    if (!gone.game_sessions_.empty() || std::filesystem::exists(file)) return 1519;
    const auto corrupt = formatFrozenGames(journal) + "broken record\n";
    if (!atomic_file::write(file, corrupt)) return 1520;
    Launcher unreadable; running(unreadable);
    unreadable.restoreFrozenGames();
    if (unreadable.frozen_state_readable_ || unreadable.notifications_.empty() ||
        unreadable.freezeGame(key, true) || suspends != 4) return 1521;
    std::ifstream input(file, std::ios::binary);
    const std::string preserved{std::istreambuf_iterator<char>{input}, {}}; input.close();
    if (preserved != corrupt) return 1522;
    if (!process_freeze::save(file, {})) return 1523;
    return 0;
}

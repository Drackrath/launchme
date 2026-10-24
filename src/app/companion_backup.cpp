#include "launcher.hpp"

void Launcher::companionBackup() {
    if (companion_.backup_busy) { navigationBoundary(); return; }
    if (companion_.busy || companion_.life_busy || companion_.reading_busy || companion_.feed.view.busy || companion_.feed.view.social_busy) {
        showNotification(NotificationKind::launch, tr(L"Poczekaj na zakończenie odpowiedzi"), tr(L"Kopia powstanie ze spójnego stanu postaci. Spróbuj ponownie za chwilę."));
        return;
    }
    file_browser::Request request;
    request.mode = file_browser::Mode::folder;
    request.title = tr(L"Gdzie zapisać pełną kopię postaci?");
    openFileBrowser(std::move(request), [this](const std::filesystem::path& destination) {
        if (companion_.busy || companion_.backup_busy) { navigationBoundary(); return; }
        companionFlush();
        companion::backup::Input snapshot;
        snapshot.destination = destination; snapshot.config = userConfigDirectory(); snapshot.look = companionLook(); snapshot.settings = settings_;
        snapshot.memory = companionMemory(); snapshot.mind = companionMindState(); snapshot.cognition = companionCognition();
        snapshot.affinity = companionAffinity(); snapshot.mood = companionMoodState(); snapshot.live = companionChatSnapshot();
        if (snapshot.live && snapshot.live->look != snapshot.look) snapshot.live.reset(); // Settings may select another profile before its chat opens.
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768); snapshot.executable = executable;
        const auto name = std::wstring(companion::name(snapshot.look));
        companion_.backup_busy = true;
        showNotification(NotificationKind::launch, tr(L"Tworzenie kopii…"), name);
        if (settingsOpen()) buildSettingsRows();
        work_.start([snapshot = std::move(snapshot), hwnd = hwnd_, alive = alive_, name] {
            auto message = std::make_unique<CompanionBackupMessage>(); message->name = name;
            message->result = companion::backup::create(snapshot);
            if (alive->load() && PostMessageW(hwnd, companion_backup_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    });
}

void Launcher::onCompanionBackup(std::unique_ptr<CompanionBackupMessage> message) {
    companion_.backup_busy = false;
    if (message->result.ok) showNotification(NotificationKind::launch, message->name + L" · " + tr(L"Kopia gotowa"), message->result.path.wstring());
    else showNotification(NotificationKind::error, tr(L"Nie udało się utworzyć kopii"), message->result.error);
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

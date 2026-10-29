// Linux: Ustawienia → Biblioteka → Proton i Wine. The compatibility tools (src/library/compat_tools.hpp)
// with their downloads, the prefix manager (src/library/launch_options_store.hpp: named prefixes, each
// game's, their tools and sizes, the prefix tools of src/platform/wine_runner.hpp) and the Wine/Proton
// options of one game or of every game (src/model/launch_options_catalog.hpp) as an accordion of
// sections. Blocking work (discovery, GitHub's lists, downloads, sizes, wineserver -k, deleting a
// prefix) runs on workers and comes back as RemoteMessages. On Windows only the stubs remain.
#include "launcher.hpp"

#ifndef _WIN32
#include "../model/launch_options_catalog.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/wine_runner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cwctype>
#endif

using namespace settings_ui;

#ifdef _WIN32
bool Launcher::compatGame(const Game&) const { return false; }
void Launcher::openGameCompatSettings(const Game&) {}
#else
namespace catalog = launch_options_catalog;
namespace store = launch_options_store;

namespace {
std::wstring wide(std::string_view value) { return text_encoding::wideLenient(value); }
std::string narrow(const std::wstring& value) { return text_encoding::utf8Lenient(value); }
std::wstring label(const catalog::Text& value) { return catalog::text(value); }
std::string keyOf(const std::filesystem::path& path) { return narrow(path.wstring()); }
std::filesystem::path pathOf(const std::string& key) { return std::filesystem::path(wide(key)); }
bool chosen(const std::string& tool) { return !tool.empty() && tool != "default"; }

std::wstring sizeText(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
    wchar_t text[32]{};
    if (unit == 0) swprintf_s(text, L"%llu %ls", static_cast<unsigned long long>(bytes), units[0]);
    else swprintf_s(text, value < 10 ? L"%.1f %ls" : L"%.0f %ls", value, units[unit]);
    return text;
}

// The bytes of every file under a prefix. Links are not followed: a prefix's dosdevices point at /
// and its pfx at itself.
uint64_t folderSize(const std::filesystem::path& root, const std::shared_ptr<std::atomic_bool>& alive) {
    uint64_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && alive->load(); it.increment(ec)) {
        std::error_code type;
        const auto status = it->symlink_status(type);
        if (!type && std::filesystem::is_regular_file(status)) {
            const auto size = it->file_size(type);
            if (!type) total += size;
        }
    }
    return total;
}

bool windowsProgram(const std::wstring& target) {
    if (target.empty() || target.find(L"://") != std::wstring::npos) return false;
    auto extension = std::filesystem::path(target).extension().wstring();
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".exe" || extension == L".msi" || extension == L".bat" || extension == L".cmd" || extension == L".com" || extension == L".lnk";
}

// A game's options before it has its own: the default ones, with the tool and the prefix left to its
// prefix and the global choice.
LaunchOptions gameDefaults() {
    auto options = store::defaults();
    options.compat_tool = "default";
    options.prefix.clear();
    return options;
}

// The accordion's sections: the catalog's, with the DLL overrides (from Proton i Wine) of their own.
struct Fold {
    const char* id;
    std::optional<catalog::Section> section;
    const wchar_t* title;
    const wchar_t* description;
};
constexpr Fold folds[] = {
    {"runner", catalog::Section::runner, trKey(L"Proton / Wine i prefiks"), trKey(L"Wersja Protona lub Wine, prefiks gry i umu-launcher.")},
    {"proton", catalog::Section::proton, trKey(L"Proton i Wine"), trKey(L"Synchronizacja, DirectX, HDR, FSR, pady, dźwięk i logi.")},
    {"dll", std::nullopt, trKey(L"Biblioteki DLL"), trKey(L"Mody, ReShade i ładowarki ASI (WINEDLLOVERRIDES).")},
    {"nvidia", catalog::Section::nvidia, L"NVIDIA", trKey(L"DLSS, Reflex, laptopy z PRIME i pamięć shaderów.")},
    {"mesa", catalog::Section::mesa, trKey(L"AMD i Mesa"), trKey(L"RADV, Vulkan i OpenGL na kartach AMD i Intel.")},
    {"wrappers", catalog::Section::wrappers, trKey(L"Nakładki i wrappery"), trKey(L"GameMode, MangoHud, gamescope i własne polecenie.")},
    {"gamescope", catalog::Section::gamescope, L"Gamescope", trKey(L"Rozdzielczość, skalowanie i limit klatek gamescope.")},
    {"game", catalog::Section::game, trKey(L"Zmienne i argumenty"), trKey(L"Zmienne środowiska, argumenty, folder roboczy i skrypty.")},
};

constexpr std::pair<const char*, const wchar_t*> wine_programs[] = {
    {"winecfg", trKey(L"Konfiguracja Wine (winecfg)")},
    {"regedit", trKey(L"Edytor rejestru (regedit)")},
    {"control", trKey(L"Panel sterowania")},
    {"taskmgr", trKey(L"Menedżer zadań")},
    {"explorer", trKey(L"Eksplorator plików")},
    {"uninstaller", trKey(L"Odinstaluj programy")},
};
}

bool Launcher::compatGame(const Game& game) const {
    if (!isGame(game) || game.kind == GameKind::nonsteam || game.source == GameSource::steam || game.source == GameSource::multimc) return false;
    const auto& target = game.play_action.target;
    if (localFileGame(game.source)) return fileNotChosen(game) || windowsProgram(target) || windowsProgram(game.executable.wstring());
    // A store's game: its Windows build, unless the action starts a native program.
    if (target.empty() || target.find(L"://") != std::wstring::npos) return true;
    return windowsProgram(target) || !std::filesystem::path(target).has_extension();
}

void Launcher::openGameCompatSettings(const Game& game) {
    const auto key = gameKey(game);
    if (!compatGame(game) || key.empty()) return;
    dismissMenu();
    compat_.game = game;
    compat_.expanded = {"runner"};
    compat_.confirm.clear();
    if (!settingsOpen()) openSettings();
    if (!settingsOpen()) return;
    settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
    settings_state_.open(Page::launch_options, key, 0, std::nullopt);
    settings_page_exits_ = true;
    clearSecretInput();
    settings_motion_.valid = false;
    if (!compat_.tools_loaded) compatLoadTools();
    buildSettingsRows(); settingsPreviewFocus(); audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::compatSettingsPage() const {
    const auto* frame = settings_state_.top();
    return frame && (frame->page == Page::compat || frame->page == Page::compat_source || frame->page == Page::prefixes ||
                     frame->page == Page::prefix || frame->page == Page::launch_options);
}

std::wstring Launcher::compatToolName(const std::string& id) const {
    if (!chosen(id)) {
        if (const auto tool = compat_tools::automatic(compat_.tools)) return tool->name + L" (" + tr(L"automatycznie") + L")";
        return compat_.tools_loaded ? std::wstring(tr(L"Automatycznie (brak narzędzia)")) : std::wstring(tr(L"Automatycznie"));
    }
    for (const auto& tool : compat_.tools) if (tool.id == id) return tool.name;
    return compat_.tools_loaded ? wide(id) + tr(L" (nie ma go już)") : wide(id);
}

std::wstring Launcher::compatPrefixName(const store::PrefixEntry& entry) const {
    if (!entry.info.name.empty()) return entry.info.name;
    if (entry.shared) return tr(L"Wspólny prefiks");
    if (!entry.games.empty()) {
        std::wstring title;
        const auto& key = entry.games.front();
        const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == key; });
        title = found != public_library_.end() ? found->name : wide(key);
        if (entry.games.size() > 1) title += L" +" + std::to_wstring(entry.games.size() - 1);
        return title;
    }
    return entry.path.filename().wstring();
}

CompatLaunch Launcher::compatPrefixLaunch(const std::filesystem::path& prefix) const {
    CompatLaunch launch;
    launch.prefix = prefix;
    const auto normal = prefix.lexically_normal();
    const auto found = std::find_if(compat_.prefixes.begin(), compat_.prefixes.end(), [&](const auto& entry) { return entry.path == normal; });
    // The first game in it: its options and identity (protontricks-like tools use its Steam app ID).
    const std::string key = found != compat_.prefixes.end() && !found->games.empty() ? found->games.front() : std::string{};
    launch.game_key = key;
    launch.title = found != compat_.prefixes.end() ? compatPrefixName(*found) : prefix.filename().wstring();
    launch.options = key.empty() ? store::defaults() : store::effectiveOptions(key);
    launch.options.prefix = prefix;
    launch.options.compat_tool = store::effectiveTool(key, prefix);
    return launch;
}

bool Launcher::compatSaveOptions(const std::string& key, const std::function<bool(LaunchOptions&)>& change) {
    auto options = key.empty() ? store::defaults() : store::gameOptions(key).value_or(gameDefaults());
    if (!change(options)) return false;
    const bool saved = key.empty() ? store::setDefaults(options) : store::setGameOptions(key, options);
    if (!saved) showNotification(NotificationKind::error, tr(L"Nie udało się zapisać opcji uruchamiania"), store::file().wstring());
    return saved;
}

// ---- Workers ---------------------------------------------------------------------------------------

void Launcher::compatLoadTools() {
    if (compat_.tools_loading) return;
    compat_.tools_loading = true;
    const auto alive = alive_; const auto hwnd = hwnd_;
    if (!work_.start([alive, hwnd] {
        auto roots = compat_tools::systemRoots();
        auto tools = compat_tools::discover(roots);
        auto prefixes = store::prefixes();
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [tools = std::move(tools), prefixes = std::move(prefixes), umu = roots.umu_run](Launcher& app) mutable {
            app.compat_.tools = std::move(tools);
            app.compat_.prefixes = std::move(prefixes);
            app.compat_.umu_run = std::move(umu);
            app.compat_.tools_loading = false;
            app.compat_.tools_loaded = true;
            if (app.settingsOpen()) app.buildSettingsRows(); // Also the overview's summary of the default tool.
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) compat_.tools_loading = false;
}

void Launcher::compatLoadReleases(const std::string& id, bool refresh) {
    const auto* source = compat_tools::findSource(id);
    if (!source || compat_.releases_loading.contains(id)) return;
    compat_.releases_loading.insert(id);
    const auto alive = alive_; const auto hwnd = hwnd_;
    if (!work_.start([alive, hwnd, source, id, refresh] {
        auto list = compat_tools::releases(*source, refresh);
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [id, list = std::move(list)](Launcher& app) mutable {
            app.compat_.releases[id] = std::move(list);
            app.compat_.releases_loading.erase(id);
            if (app.settingsOpen() && app.compatSettingsPage()) app.buildSettingsRows();
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) compat_.releases_loading.erase(id);
}

void Launcher::compatInstall(const compat_tools::Release& release) {
    if (compat_.install) return;
    const auto* source = compat_tools::findSource(release.source);
    if (!source) return;
    const auto target = compat_.steam_target && source->kind == compat_tools::Kind::proton ? compat_tools::Target::steam : compat_tools::Target::launchme;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    compat_.install = CompatSettings::Install{release.source, release.tag, {}, cancel};
    compat_.status.clear();
    const auto alive = alive_; const auto hwnd = hwnd_;
    const auto post = [alive, hwnd](std::function<void(Launcher&)> run) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = std::move(run);
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    if (!work_.start([release, target, cancel, post, alive] {
        uint64_t last = 0;
        const auto result = compat_tools::install(release, target, [cancel, alive] { return cancel->load() || !alive->load(); },
            [&](const compat_tools::Progress& progress) {
                const auto now = GetTickCount64();
                if (now - last < 200 && progress.done != progress.total) return;
                last = now;
                post([progress](Launcher& app) {
                    if (!app.compat_.install) return;
                    app.compat_.install->progress = progress;
                    if (app.settingsOpen() && app.settings_state_.at(Page::compat_source)) app.buildSettingsRows();
                });
            });
        post([result, release](Launcher& app) {
            app.compat_.install.reset();
            if (result.error == compat_tools::Error::none) {
                app.compat_.status.clear();
                app.showNotification(NotificationKind::download, release.name, tr(L"Zainstalowano"));
            } else if (result.error != compat_tools::Error::cancelled) {
                app.compat_.status = label(compat_tools::errorText(result.error));
                app.showNotification(NotificationKind::error, tr(L"Nie udało się zainstalować ") + release.name, app.compat_.status);
            }
            app.compatLoadTools();
            if (app.settingsOpen() && app.compatSettingsPage()) app.buildSettingsRows();
        });
    })) compat_.install.reset();
}

void Launcher::compatMeasure(const std::filesystem::path& prefix) {
    const auto normal = prefix.lexically_normal();
    if (compat_.measuring.contains(normal)) return;
    compat_.measuring.insert(normal);
    const auto alive = alive_; const auto hwnd = hwnd_;
    if (!work_.start([alive, hwnd, normal] {
        const uint64_t size = folderSize(normal, alive);
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [normal, size](Launcher& app) {
            app.compat_.measuring.erase(normal);
            app.compat_.sizes[normal] = size;
            if (app.settingsOpen() && app.compatSettingsPage()) app.buildSettingsRows();
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) compat_.measuring.erase(normal);
}

void Launcher::compatPrefixAction(const std::filesystem::path& prefix, const std::string& action, const std::vector<std::string>& arguments) {
    const auto launch = compatPrefixLaunch(prefix);
    const auto title = launch.title;
    const auto failed = [&](int error) {
        showNotification(NotificationKind::error, tr(L"Nie udało się uruchomić narzędzia prefiksu"),
                         error == ENOENT ? std::wstring(tr(L"Brak Protona lub Wine: pobierz go w Ustawieniach → Proton i Wine.")) : wide(std::strerror(error)));
    };
    pid_t pid = 0;
    int error = 0;
    if (action == "create") error = wine_runner::createPrefix(launch, pid);
    else if (action == "winetricks") error = wine_runner::winetricks(launch, arguments, pid);
    else if (action == "run" && !arguments.empty()) error = wine_runner::runInPrefix(launch, pathOf(arguments.front()), {}, pid);
    else if (action == "kill" || action == "delete") {
        const auto alive = alive_; const auto hwnd = hwnd_;
        const bool remove = action == "delete";
        if (!work_.start([alive, hwnd, launch, prefix, remove] {
            bool done = wine_runner::killPrefix(launch);
            if (remove) done = wine_runner::deletePrefix(prefix) && store::forgetPrefix(prefix);
            if (!alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [done, remove, title = launch.title](Launcher& app) {
                if (remove && done) {
                    app.showNotification(NotificationKind::launch, title, tr(L"Prefiks usunięty"));
                    if (app.settingsOpen() && app.settings_state_.at(Page::prefix)) app.closeSettingsPage();
                } else if (remove) app.showNotification(NotificationKind::error, tr(L"Nie udało się usunąć prefiksu"), title);
                else app.showNotification(NotificationKind::launch, title, done ? tr(L"Procesy prefiksu zakończone") : tr(L"Nie wszystkie procesy się zakończyły"));
                app.compatLoadTools();
            };
            if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        })) failed(EAGAIN);
        return;
    } else error = wine_runner::runWineProgram(launch, action, pid);
    if (error) { failed(error); return; }
    showNotification(NotificationKind::launch, title, tr(L"Uruchomiono w prefiksie"));
}

// ---- Rows ------------------------------------------------------------------------------------------

void Launcher::compatSettingsRows() {
    const auto* frame = settings_state_.top();
    if (!frame) return;
    if (!compat_.tools_loaded) compatLoadTools();
    std::wstring next_section;
    const auto section = [&](std::wstring title) { next_section = std::move(title); };
    const auto add = [&](Row row) -> Row& {
        if (!next_section.empty()) { row.section = std::move(next_section); next_section.clear(); }
        settings_rows_.push_back(std::move(row));
        return settings_rows_.back();
    };
    const auto runners = compat_tools::runners(compat_.tools);
    // A choice of tool: "default" first (what it falls back to), then every Proton and Wine build.
    const auto toolChoices = [&](Row& row, const std::string& current, std::wstring fallback) {
        row.choices.push_back({std::move(fallback), 0, false, L"default"});
        row.choice = 0;
        for (const auto& tool : runners) {
            row.choices.push_back({tool.name + L" · " + label(compat_tools::originName(tool.origin)), 0, false, wide(tool.id)});
            if (tool.id == current) row.choice = static_cast<int>(row.choices.size()) - 1;
        }
        if (chosen(current) && row.choice == 0) { // Chosen, but gone: still shown as chosen.
            row.choices.push_back({compatToolName(current), 0, false, wide(current)});
            row.choice = static_cast<int>(row.choices.size()) - 1;
        }
        row.value = row.choices[static_cast<size_t>(row.choice)].label;
    };
    switch (frame->page) {
    case Page::compat: {
        section(tr(L"Domyślnie"));
        {
            Row row{Id::compat_default_tool, Control::choice, tr(L"Wersja Protona / Wine"),
                tr(L"Dla gier, które nie wybrały własnej, i prefiksów bez własnej wersji.")};
            toolChoices(row, store::defaultTool(), compatToolName("default"));
            add(std::move(row));
        }
        {
            const auto defaults = store::defaults();
            int changed = 0;
            for (const auto& option : catalog::options())
                if (std::string_view(option.id) != "compat_tool" && catalog::value(defaults, option.id) != catalog::value(LaunchOptions{}, option.id)) ++changed;
            Row row{Id::compat_defaults, Control::page, tr(L"Domyślne opcje uruchamiania"),
                tr(L"Synchronizacja, nakładki, gamescope, zmienne… dla gier bez własnych ustawień."),
                changed ? tr(L"Zmienione: ") + std::to_wstring(changed) : std::wstring(tr(L"Bez zmian"))};
            row.page = Page::launch_options;
            add(std::move(row));
        }
        add({Id::compat_prefixes, Control::count, tr(L"Prefiksy"), tr(L"Nazwane prefiksy i prefiksy gier: wersja, rozmiar, winecfg, winetricks."),
             compat_.tools_loaded ? std::to_wstring(compat_.prefixes.size()) : std::wstring(L"…")});
        add({Id::compat_prefix_root, Control::action, tr(L"Folder prefiksów"), store::prefixRoot().wstring(), tr(L"Zmień")});
        {
            Row umu{Id::compat_umu, Control::action, L"umu-launcher",
                compat_.umu_run.empty() ? std::wstring(tr(L"Proton w Steam Linux Runtime z poprawkami protonfixes, jak w Steamie. Zalecany.")) :
                                          tr(L"Zainstalowany: ") + compat_.umu_run.wstring(),
                compat_.umu_run.empty() ? tr(L"Pobierz") : tr(L"Wersje")};
            add(std::move(umu));
        }
        section(tr(L"Zainstalowane"));
        if (!compat_.tools_loaded) add({Id::compat_status, Control::action, tr(L"Szukanie Protona i Wine…"), {}, {}, false}).locked = true;
        else if (runners.empty())
            add({Id::compat_status, Control::action, tr(L"Brak Protona i Wine"), tr(L"Pobierz GE-Proton poniżej: najlepszy wybór dla większości gier."), {}}).locked = true;
        const auto global = store::defaultTool();
        for (const auto& tool : runners) {
            std::wstring description = wide(tool.family);
            if (!tool.version.empty() && wide(tool.version) != tool.name && tool.origin != compat_tools::Origin::system) description += L" · " + wide(tool.version);
            description += L" · " + label(compat_tools::originName(tool.origin));
            if (tool.id == global) description += L" · " + std::wstring(tr(L"domyślna"));
            if (tool.deprecated) description += L" · " + std::wstring(tr(L"przestarzała"));
            Row row{Id::compat_tool, Control::choice, tool.name, std::move(description), tr(L"Opcje")};
            row.key = tool.id;
            row.choice = -1;
            row.choices.push_back({tr(L"Ustaw jako domyślną"), 0, false, L"default"});
            if (!tool.path.empty()) row.choices.push_back({tr(L"Otwórz folder"), 0, false, L"folder"});
            if (tool.removable) row.choices.push_back({compat_.confirm == "tool:" + tool.id ? tr(L"Potwierdź usunięcie") : tr(L"Usuń"), 0, false, L"remove"});
            add(std::move(row));
        }
        bool components = false;
        for (const auto& tool : compat_.tools) {
            if (tool.kind != compat_tools::Kind::component) continue;
            if (!components) { section(tr(L"Komponenty dla Wine")); components = true; }
            Row row{Id::compat_tool, Control::choice, tool.name, wide(tool.family) + L" · " + label(compat_tools::originName(tool.origin)), tr(L"Opcje")};
            row.key = tool.id;
            row.choice = -1;
            if (!tool.path.empty()) row.choices.push_back({tr(L"Otwórz folder"), 0, false, L"folder"});
            if (tool.removable) row.choices.push_back({compat_.confirm == "tool:" + tool.id ? tr(L"Potwierdź usunięcie") : tr(L"Usuń"), 0, false, L"remove"});
            if (!row.choices.empty()) add(std::move(row));
        }
        section(tr(L"Pobierz"));
        for (const auto& source : compat_tools::sources()) {
            const bool runner = source.kind == compat_tools::Kind::proton || source.kind == compat_tools::Kind::wine;
            // Its builds wherever they came from (Heroic, Steam, Launchme), not the system's Wine or umu's own.
            const auto count = std::count_if(compat_.tools.begin(), compat_.tools.end(), [&](const auto& tool) {
                return tool.family == source.family && tool.origin != compat_tools::Origin::system && tool.origin != compat_tools::Origin::umu;
            });
            std::wstring description = source.kind == compat_tools::Kind::proton ? tr(L"Proton do gier spoza Steama") :
                                       source.kind == compat_tools::Kind::wine ? tr(L"Wine bez Steam Linux Runtime") :
                                       source.kind == compat_tools::Kind::umu ? tr(L"Uruchamianie Protona jak w Steamie") :
                                       tr(L"Komponent kopiowany do prefiksów Wine");
            description += L" · github.com/" + wide(source.repository);
            Row row{Id::compat_source, Control::count, source.name, std::move(description), runner || count ? std::to_wstring(count) : std::wstring{}};
            row.key = source.id;
            add(std::move(row));
        }
        break;
    }
    case Page::compat_source: {
        const auto* source = compat_tools::findSource(frame->key);
        if (!source) break;
        const auto found = compat_.releases.find(source->id);
        if (found == compat_.releases.end() && !compat_.releases_loading.contains(source->id)) compatLoadReleases(source->id, false);
        if (source->kind == compat_tools::Kind::proton)
            add({Id::compat_steam_target, Control::toggle, tr(L"Instaluj także dla Steama"),
                 tr(L"Do compatibilitytools.d Steama: ta wersja będzie też do wyboru we właściwościach gier Steam."), {}, compat_.steam_target});
        if (found == compat_.releases.end()) {
            add({Id::compat_status, Control::action, tr(L"Ładowanie listy wydań…"), L"github.com/" + wide(source->repository), {}}).locked = true;
            break;
        }
        const auto& list = found->second;
        if (!list.available) {
            add({Id::compat_source_refresh, Control::action, tr(L"Brak połączenia z GitHubem"), tr(L"Lista wydań jest niedostępna. Sprawdź połączenie."), tr(L"Ponów")});
            break;
        }
        section(list.stale ? tr(L"Wydania (zapisana lista: GitHub nie odpowiedział)") : tr(L"Wydania"));
        for (const auto& release : list.releases) {
            const bool installed = compat_tools::installed(release, compat_.tools);
            const bool active = compat_.install && compat_.install->source == release.source && compat_.install->tag == release.tag;
            std::wstring description = wide(release.published);
            if (release.size) description += L" · " + sizeText(release.size);
            if (release.digest == compat_tools::Digest::none) description += L" · " + std::wstring(tr(L"bez sumy kontrolnej"));
            if (active) {
                const auto& progress = compat_.install->progress;
                const auto phase = progress.phase == compat_tools::Progress::Phase::download ? tr(L"Pobieranie") :
                                   progress.phase == compat_tools::Progress::Phase::verify ? tr(L"Sprawdzanie") : tr(L"Rozpakowywanie");
                description = phase + (progress.total ? L" " + std::to_wstring(progress.done * 100 / progress.total) + L"% · " +
                                                        sizeText(progress.done) + L" / " + sizeText(progress.total) : std::wstring(L"…"));
            }
            Row row{Id::compat_release, Control::action, release.name, std::move(description),
                active ? tr(L"Anuluj") : installed ? tr(L"Zainstalowana") : tr(L"Pobierz")};
            row.key = release.tag;
            row.locked = installed || (compat_.install && !active);
            add(std::move(row));
        }
        if (!compat_.status.empty()) add({Id::compat_status, Control::action, tr(L"Ostatnia instalacja"), compat_.status, {}}).locked = true;
        add({Id::compat_source_refresh, Control::action, tr(L"Lista wydań"), tr(L"Zapisana na godzinę; odśwież, aby zobaczyć nowsze."), tr(L"Odśwież")});
        break;
    }
    case Page::prefixes: {
        add({Id::prefix_new, Control::text, tr(L"Nowy prefiks"),
             tr(L"Wspólny dysk C: dla kilku gier, np. Sklepy albo Ubisoft. Wybierasz go potem w opcjach gry."), tr(L"Utwórz…")});
        section(tr(L"Prefiksy"));
        if (!compat_.tools_loaded) add({Id::compat_status, Control::action, tr(L"Szukanie prefiksów…"), {}, {}}).locked = true;
        for (const auto& entry : compat_.prefixes) {
            std::wstring description = entry.path.wstring();
            description += L" · " + compatToolName(chosen(entry.info.compat_tool) ? entry.info.compat_tool : store::defaultTool());
            if (entry.shared) description += L" · " + std::wstring(tr(L"programy bez gry"));
            else if (entry.games.empty()) description += L" · " + std::wstring(tr(L"żadna gra go nie używa"));
            else description += L" · " + (tr(L"Gry: ") + std::to_wstring(entry.games.size()));
            const auto size = compat_.sizes.find(entry.path);
            std::error_code ec;
            const bool exists = std::filesystem::exists(entry.path, ec);
            if (exists && size == compat_.sizes.end()) compatMeasure(entry.path);
            Row row{Id::prefix_entry, Control::count, compatPrefixName(entry), std::move(description),
                !exists ? std::wstring(tr(L"Pusty")) : size == compat_.sizes.end() ? std::wstring(L"…") : sizeText(size->second)};
            row.key = keyOf(entry.path);
            add(std::move(row));
        }
        break;
    }
    case Page::prefix: {
        const auto path = pathOf(frame->key);
        const auto found = std::find_if(compat_.prefixes.begin(), compat_.prefixes.end(), [&](const auto& entry) { return entry.path == path.lexically_normal(); });
        store::PrefixEntry entry;
        if (found != compat_.prefixes.end()) entry = *found; else entry.path = path;
        Row name{Id::prefix_name, Control::text, tr(L"Nazwa"), tr(L"Jak prefiks nazywa się na liście i w opcjach gier."),
            entry.info.name.empty() ? compatPrefixName(entry) : entry.info.name};
        name.key = frame->key;
        add(std::move(name));
        {
            Row row{Id::prefix_tool, Control::choice, tr(L"Wersja Protona / Wine"),
                tr(L"Gry w tym prefiksie używają jej, gdy nie wybrały własnej. Zmiana głównej wersji (np. Wine 9 → 10) aktualizuje prefiks.")};
            toolChoices(row, entry.info.compat_tool, tr(L"Domyślna: ") + compatToolName(store::defaultTool()));
            row.key = frame->key;
            add(std::move(row));
        }
        std::error_code ec;
        const bool exists = wine_runner::prefixExists(path);
        add({Id::prefix_folder, Control::action, tr(L"Folder"), path.wstring(), tr(L"Otwórz")}).key = frame->key;
        {
            const auto size = compat_.sizes.find(path.lexically_normal());
            if (std::filesystem::exists(path, ec) && size == compat_.sizes.end()) compatMeasure(path);
            Row row{Id::prefix_size, Control::action, tr(L"Rozmiar"),
                compat_.measuring.contains(path.lexically_normal()) ? std::wstring(tr(L"Liczenie…")) :
                size == compat_.sizes.end() ? std::wstring(tr(L"Pusty")) : sizeText(size->second),
                tr(L"Przelicz")};
            row.key = frame->key;
            add(std::move(row));
        }
        {
            std::wstring games;
            for (const auto& key : entry.games) {
                const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
                games += (games.empty() ? L"" : L", ") + (game != public_library_.end() ? game->name : wide(key));
            }
            Row row{Id::prefix_games, Control::action, tr(L"Gry"),
                entry.shared ? std::wstring(tr(L"Programy uruchamiane bez gry.")) : games.empty() ? std::wstring(tr(L"Żadna gra go nie używa.")) : games, {}};
            row.locked = true;
            row.lines = games.size() > 120 ? 3 : 0;
            add(std::move(row));
        }
        section(tr(L"Narzędzia"));
        if (!exists) add({Id::prefix_create, Control::action, tr(L"Utwórz prefiks teraz"),
                         tr(L"Inaczej powstanie przy pierwszym uruchomieniu gry (umu, Proton i Wine robią to same)."), tr(L"Utwórz")}).key = frame->key;
        {
            Row row{Id::prefix_program, Control::choice, tr(L"Programy Wine"), tr(L"winecfg, regedit, panel sterowania i inne, w tym prefiksie."), tr(L"Uruchom…")};
            row.key = frame->key;
            row.choice = -1;
            for (const auto& [program, text] : wine_programs) row.choices.push_back({tr(text), 0, false, wide(program)});
            add(std::move(row));
        }
        {
            const auto launch = compatPrefixLaunch(path);
            Row row{Id::prefix_winetricks, Control::choice, L"Winetricks",
                wine_runner::winetricksAvailable(launch) ? std::wstring(tr(L"Biblioteki Microsoft i czcionki, których gra potrzebuje.")) :
                                                           std::wstring(tr(L"Zainstaluj winetricks (albo umu-launcher), aby tego użyć.")),
                tr(L"Zainstaluj…")};
            row.key = frame->key;
            row.choice = -1;
            row.locked = !wine_runner::winetricksAvailable(launch);
            row.choices.push_back({tr(L"Okno winetricks"), 0, false, {}});
            for (const auto& verb : catalog::winetricksVerbs()) row.choices.push_back({label(verb.label) + L" (" + wide(verb.verb) + L")", 0, false, wide(verb.verb)});
            add(std::move(row));
        }
        add({Id::prefix_run, Control::action, tr(L"Uruchom program w prefiksie"), tr(L"Instalator, łatka albo menedżer modów (.exe, .msi, .bat)."), tr(L"Wybierz…")}).key = frame->key;
        add({Id::prefix_kill, Control::action, tr(L"Zakończ procesy prefiksu"), tr(L"wineserver -k: gdy gra albo instalator się zawiesi."), tr(L"Zakończ")}).key = frame->key;
        {
            Row row{Id::prefix_delete, Control::action, tr(L"Usuń prefiks"),
                tr(L"Usuwa dysk C: prefiksu z zapisami gier, które trzymają je w nim. Gry dostaną nowy przy następnym uruchomieniu."),
                compat_.confirm == "prefix:" + frame->key ? tr(L"Potwierdź") : tr(L"Usuń")};
            row.key = frame->key;
            row.lines = 3;
            add(std::move(row));
        }
        break;
    }
    case Page::launch_options: {
        const std::string key = frame->key;
        const bool game = !key.empty();
        const auto stored = game ? store::gameOptions(key) : std::optional<LaunchOptions>{};
        const LaunchOptions options = game ? stored.value_or(gameDefaults()) : store::defaults();
        if (game) {
            Row scope{Id::launch_scope, Control::action, stored ? tr(L"Własne ustawienia tej gry") : tr(L"Ustawienia domyślne"),
                stored ? std::wstring(tr(L"Zmiany dotyczą tylko tej gry.")) :
                         std::wstring(tr(L"Gra używa ustawień domyślnych. Zmiana dowolnej opcji zapisze jej własne.")),
                stored ? (compat_.confirm == "reset:" + key ? tr(L"Potwierdź") : tr(L"Przywróć domyślne")) : tr(L"Edytuj domyślne")};
            add(std::move(scope));
            if (launchEditable(compat_.game))
                add({Id::launch_edit_game, Control::action, tr(L"Plik gry i argumenty"), tr(L"Program, który się uruchamia, jego argumenty i folder."), tr(L"Edytuj")});
        }
        const LaunchOptions plain;
        for (const auto& fold : folds) {
            if (fold.section == catalog::Section::gamescope && !options.gamescope) continue; // Its options follow its switch.
            int changed = 0;
            for (const auto& option : catalog::options()) {
                const bool in = fold.section ? option.section == *fold.section && std::string_view(option.id) != "dll_overrides" :
                                               std::string_view(option.id) == "dll_overrides";
                if (in && (game || std::string_view(option.id) != "prefix") && catalog::value(options, option.id) != catalog::value(plain, option.id)) ++changed;
            }
            const bool open = compat_.expanded.contains(fold.id);
            Row heading{Id::launch_fold, Control::fold, tr(fold.title), tr(fold.description),
                changed ? tr(L"Zmienione: ") + std::to_wstring(changed) : std::wstring{}, open};
            heading.key = fold.id;
            add(std::move(heading));
            if (!open) continue;
            const auto nested = [&](Row row) { row.indent = 1; add(std::move(row)); };
            if (!fold.section) { // WINEDLLOVERRIDES: the presets as switches, then the whole text.
                for (const auto& preset : catalog::dllPresets()) {
                    Row row{Id::launch_dll, Control::toggle, label(preset.label), wide(preset.dlls) + L"=" + wide(preset.mode), {},
                        catalog::presetSet(options.dll_overrides, preset)};
                    row.key = preset.dlls;
                    nested(std::move(row));
                }
                const auto* manual = catalog::find("dll_overrides");
                Row row{Id::launch_option, Control::text, tr(L"Ręcznie (WINEDLLOVERRIDES)"), label(manual->description),
                    options.dll_overrides.empty() ? std::wstring(tr(L"Brak")) : wide(options.dll_overrides)};
                row.key = "dll_overrides";
                nested(std::move(row));
                continue;
            }
            for (const auto& option : catalog::options()) {
                if (option.section != *fold.section) continue;
                const std::string_view id = option.id;
                if (id == "dll_overrides") continue;
                if (option.depends_on && catalog::value(options, option.depends_on) != "1") continue;
                if (id == "compat_tool") {
                    Row row{Id::launch_option, Control::choice, label(option.label), label(option.description)};
                    row.key = option.id;
                    std::wstring fallback;
                    if (game) {
                        const auto prefix = options.prefix.empty() ? store::gamePrefix(compat_.game) : options.prefix;
                        const auto prefix_tool = store::prefixInfo(prefix).compat_tool;
                        fallback = chosen(prefix_tool) ? tr(L"Jak prefiks: ") + compatToolName(prefix_tool) :
                                                         tr(L"Domyślna: ") + compatToolName(store::defaultTool());
                    } else fallback = compatToolName("default");
                    toolChoices(row, options.compat_tool, std::move(fallback));
                    row.lines = 3;
                    nested(std::move(row));
                    continue;
                }
                if (id == "prefix") {
                    if (!game) continue; // The default has no prefix: each game gets its own.
                    const auto own = store::gamePrefix(compat_.game);
                    Row row{Id::launch_option, Control::choice, label(option.label), options.prefix.empty() ? own.wstring() : options.prefix.wstring()};
                    row.key = option.id;
                    row.choices.push_back({tr(L"Własny prefiks gry"), 0, false, {}});
                    for (const auto& entry : compat_.prefixes) {
                        if (!entry.named && !entry.shared) continue;
                        row.choices.push_back({compatPrefixName(entry), 0, false, entry.path.wstring()});
                        if (!options.prefix.empty() && entry.path == options.prefix.lexically_normal()) row.choice = static_cast<int>(row.choices.size()) - 1;
                    }
                    if (!options.prefix.empty() && row.choice == 0) {
                        row.choices.push_back({options.prefix.filename().wstring(), 0, false, options.prefix.wstring()});
                        row.choice = static_cast<int>(row.choices.size()) - 1;
                    }
                    row.choices.push_back({tr(L"Inny folder…"), 0, false, L"browse"});
                    row.value = row.choices[static_cast<size_t>(row.choice)].label;
                    nested(std::move(row));
                    Row tools{Id::prefix_entry, Control::page, tr(L"Narzędzia prefiksu"), tr(L"Rozmiar, winecfg, winetricks, instalatory i usuwanie."), {}};
                    const auto effective = options.prefix.empty() ? own : options.prefix;
                    tools.key = keyOf(effective.lexically_normal());
                    if (const auto size = compat_.sizes.find(effective.lexically_normal()); size != compat_.sizes.end()) tools.value = sizeText(size->second);
                    else if (std::error_code ec; std::filesystem::exists(effective, ec)) compatMeasure(effective);
                    nested(std::move(tools));
                    continue;
                }
                if (id == "environment") {
                    for (size_t i = 0; i < options.environment.size(); ++i) {
                        const auto& variable = options.environment[i];
                        Row row{Id::launch_env, Control::action, wide(variable.name) + L"=" + wide(variable.value), tr(L"Zmienna środowiska tej gry."), tr(L"Usuń")};
                        row.key = std::to_string(i);
                        nested(std::move(row));
                    }
                    nested({Id::launch_env_add, Control::text, tr(L"Dodaj zmienną"),
                            tr(L"NAZWA=wartość. Ma pierwszeństwo przed przełącznikami powyżej."), tr(L"Dodaj…")});
                    Row presets{Id::launch_env_preset, Control::choice, tr(L"Popularne zmienne"), tr(L"Często polecane na ProtonDB i PCGamingWiki."), tr(L"Wybierz…")};
                    presets.choice = -1;
                    for (const auto& preset : catalog::variablePresets())
                        presets.choices.push_back({label(preset.label) + L" (" + wide(preset.name) + L"=" + wide(preset.value) + L")", 0, false, wide(preset.name)});
                    nested(std::move(presets));
                    continue;
                }
                const auto value = catalog::value(options, option.id);
                std::wstring description = label(option.description);
                if (option.runners == catalog::proton) description += L" " + std::wstring(tr(L"Tylko Proton."));
                else if (option.runners == catalog::wine) description += L" " + std::wstring(tr(L"Tylko zwykłe Wine."));
                description += L"\n" + wide(option.sets);
                Row row{Id::launch_option, Control::toggle, label(option.label), std::move(description)};
                row.key = option.id;
                row.lines = 3;
                switch (option.type) {
                case catalog::Type::toggle: row.checked = value == "1"; break;
                case catalog::Type::choice:
                    row.control = Control::choice;
                    for (size_t i = 0; i < option.choices.size(); ++i) {
                        row.choices.push_back({label(option.choices[i].label), 0, false, wide(option.choices[i].value)});
                        if (value == option.choices[i].value) row.choice = static_cast<int>(i);
                    }
                    row.value = row.choices.empty() ? std::wstring{} : row.choices[static_cast<size_t>(row.choice)].label;
                    break;
                case catalog::Type::number:
                    row.control = Control::text;
                    row.value = wide(value);
                    row.description = label(option.description) + L" (" + std::to_wstring(option.minimum) + L"–" + std::to_wstring(option.maximum) + L")\n" +
                                      wide(option.sets);
                    break;
                default:
                    row.control = Control::text;
                    row.value = value.empty() ? std::wstring(tr(L"Brak")) : wide(value);
                    break;
                }
                nested(std::move(row));
            }
        }
        break;
    }
    default: break;
    }
}

// ---- Activation ------------------------------------------------------------------------------------

bool Launcher::compatSettingsActivate(const Row& row) {
    if (!compatSettingsPage()) return false;
    const auto* frame = settings_state_.top();
    const std::string page_key = frame ? frame->key : std::string{};
    // A picker: the first press opens the list, the second applies the focused entry.
    const auto pick = [&](const std::function<void(const Choice&)>& apply) {
        if (row.locked) { navigationBoundary(); return true; }
        if (!settings_state_.editing) {
            settings_state_.choice = std::clamp(row.choice, 0, std::max(0, static_cast<int>(row.choices.size()) - 1));
            settings_state_.editing = true;
            audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        settings_state_.editing = false;
        const int picked = settings_state_.choice;
        if (picked >= 0 && picked < static_cast<int>(row.choices.size())) apply(row.choices[static_cast<size_t>(picked)]);
        buildSettingsRows(); audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    };
    const auto openFolder = [&](const std::filesystem::path& folder) {
        if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
    };
    // Any other row cancels a pending confirmation.
    const auto confirmed = [&](const std::string& what) {
        if (compat_.confirm == what) { compat_.confirm.clear(); return true; }
        compat_.confirm = what;
        buildSettingsRows(); audio_.context();
        return false;
    };
    if (row.id != Id::compat_tool && row.id != Id::prefix_delete && row.id != Id::launch_scope && !compat_.confirm.empty()) compat_.confirm.clear();
    switch (row.id) {
    // Proton i Wine.
    case Id::compat_default_tool:
        return pick([&](const Choice& choice) { store::setDefaultTool(narrow(choice.key)); });
    case Id::compat_defaults:
        compat_.expanded = {"runner"};
        openSettingsPage(Page::launch_options, {});
        return true;
    case Id::compat_prefixes: compatLoadTools(); openSettingsPage(Page::prefixes); return true;
    case Id::compat_prefix_root: {
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = tr(L"Wybierz folder prefiksów");
        request.initial = store::prefixRoot();
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) {
            if (picked.empty() || !store::setPrefixRoot(picked)) return;
            compatLoadTools();
            if (settingsOpen()) buildSettingsRows();
        });
        return true;
    }
    case Id::compat_umu:
        openSettingsPage(Page::compat_source, "umu-launcher");
        return true;
    case Id::compat_tool: {
        const auto tool = std::find_if(compat_.tools.begin(), compat_.tools.end(), [&](const auto& item) { return item.id == row.key; });
        if (tool == compat_.tools.end()) return true;
        return pick([&](const Choice& choice) {
            if (choice.key == L"default") store::setDefaultTool(tool->id);
            else if (choice.key == L"folder") openFolder(tool->path);
            else if (choice.key == L"remove") {
                if (!confirmed("tool:" + tool->id)) return;
                const auto users = store::gamesUsingTool(tool->id);
                if (!compat_tools::remove(*tool)) showNotification(NotificationKind::error, tr(L"Nie udało się usunąć"), tool->name);
                else {
                    showNotification(NotificationKind::download, tool->name, users.empty() ? std::wstring(tr(L"Usunięto")) :
                                     tr(L"Usunięto. Gry, które jej używały, dostaną wersję automatyczną: ") + std::to_wstring(users.size()));
                    compatLoadTools();
                }
            }
        });
    }
    case Id::compat_source:
        compatLoadReleases(row.key, false);
        openSettingsPage(Page::compat_source, row.key);
        return true;
    case Id::compat_steam_target:
        compat_.steam_target = !compat_.steam_target;
        buildSettingsRows(); audio_.context();
        return true;
    case Id::compat_source_refresh:
        compat_.releases.erase(page_key);
        compatLoadReleases(page_key, true);
        buildSettingsRows(); audio_.context();
        return true;
    case Id::compat_release: {
        if (compat_.install && compat_.install->tag == row.key && compat_.install->source == page_key) {
            compat_.install->cancel->store(true); // The partial download stays for the next attempt.
            return true;
        }
        if (row.locked) { navigationBoundary(); return true; }
        const auto list = compat_.releases.find(page_key);
        if (list == compat_.releases.end()) return true;
        const auto release = std::find_if(list->second.releases.begin(), list->second.releases.end(), [&](const auto& item) { return item.tag == row.key; });
        if (release != list->second.releases.end()) { compatInstall(*release); buildSettingsRows(); audio_.context(); }
        return true;
    }
    case Id::compat_status: case Id::prefix_games: navigationBoundary(); return true;
    // Prefiksy.
    case Id::prefix_new:
        settingsTextRow({}, [this](const std::wstring& value) {
            std::wstring name = value;
            name.erase(0, name.find_first_not_of(L" \t"));
            while (!name.empty() && (name.back() == L' ' || name.back() == L'\t')) name.pop_back();
            if (name.empty()) return false;
            const auto created = store::newPrefix(name, "default");
            if (created.empty()) {
                showNotification(NotificationKind::error, tr(L"Nie udało się utworzyć prefiksu"), store::prefixRoot().wstring());
                return false;
            }
            compat_.prefixes = store::prefixes();
            return true;
        });
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::prefix_entry:
        openSettingsPage(Page::prefix, row.key);
        return true;
    case Id::prefix_name: {
        const auto path = pathOf(row.key);
        auto info = store::prefixInfo(path);
        settingsTextRow(info.name, [this, path, info](const std::wstring& value) mutable {
            if (value.size() > 256) return false;
            info.name = value;
            if (!store::setPrefixInfo(path, info)) return false;
            compat_.prefixes = store::prefixes();
            return true;
        });
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    }
    case Id::prefix_tool: {
        const auto path = pathOf(row.key);
        return pick([&](const Choice& choice) {
            auto info = store::prefixInfo(path);
            info.compat_tool = narrow(choice.key);
            store::setPrefixInfo(path, info);
            compat_.prefixes = store::prefixes();
        });
    }
    case Id::prefix_folder: {
        const auto path = pathOf(row.key);
        std::error_code ec;
        std::filesystem::create_directories(path, ec); // A prefix not made yet opens as an empty folder.
        openFolder(path);
        return true;
    }
    case Id::prefix_size:
        compat_.sizes.erase(pathOf(row.key).lexically_normal());
        compatMeasure(pathOf(row.key));
        buildSettingsRows();
        return true;
    case Id::prefix_create: compatPrefixAction(pathOf(row.key), "create"); return true;
    case Id::prefix_program: {
        const auto path = pathOf(row.key);
        return pick([&](const Choice& choice) { compatPrefixAction(path, narrow(choice.key)); });
    }
    case Id::prefix_winetricks: {
        const auto path = pathOf(row.key);
        return pick([&](const Choice& choice) {
            std::vector<std::string> verbs;
            if (!choice.key.empty()) verbs.push_back(narrow(choice.key));
            compatPrefixAction(path, "winetricks", verbs);
        });
    }
    case Id::prefix_run: {
        const auto path = pathOf(row.key);
        file_browser::Request request;
        request.mode = file_browser::Mode::file;
        request.title = tr(L"Wybierz program do uruchomienia w prefiksie");
        request.extensions = {L".exe", L".msi", L".bat", L".cmd", L".lnk"};
        request.filter_label = tr(L"Programy Windows");
        request.all_files_option = true;
        openFileBrowser(std::move(request), [this, path](const std::filesystem::path& picked) {
            if (!picked.empty()) compatPrefixAction(path, "run", {keyOf(picked)});
        });
        return true;
    }
    case Id::prefix_kill: compatPrefixAction(pathOf(row.key), "kill"); return true;
    case Id::prefix_delete:
        if (confirmed("prefix:" + row.key)) compatPrefixAction(pathOf(row.key), "delete");
        return true;
    // Opcje uruchamiania.
    case Id::launch_fold:
        if (!compat_.expanded.erase(row.key)) compat_.expanded.insert(row.key);
        buildSettingsRows(); audio_.context();
        return true;
    case Id::launch_scope:
        if (!store::gameOptions(page_key)) { compat_.expanded = {"runner"}; openSettingsPage(Page::launch_options, {}); return true; }
        if (confirmed("reset:" + page_key)) {
            store::resetGameOptions(page_key);
            buildSettingsRows(); audio_.context();
        }
        return true;
    case Id::launch_edit_game: openLaunchEditor(compat_.game); return true;
    case Id::launch_dll: {
        const auto presets = catalog::dllPresets();
        const auto preset = std::find_if(presets.begin(), presets.end(), [&](const auto& item) { return row.key == item.dlls; });
        if (preset == presets.end()) return true;
        compatSaveOptions(page_key, [&](LaunchOptions& options) {
            options.dll_overrides = catalog::withDllOverride(options.dll_overrides, preset->dlls,
                row.checked ? std::nullopt : std::optional<std::string_view>(preset->mode));
            return true;
        });
        buildSettingsRows(); audio_.context();
        return true;
    }
    case Id::launch_env: {
        const auto index = static_cast<size_t>(std::strtoul(row.key.c_str(), nullptr, 10));
        compatSaveOptions(page_key, [&](LaunchOptions& options) {
            if (index >= options.environment.size()) return false;
            options.environment.erase(options.environment.begin() + static_cast<std::ptrdiff_t>(index));
            return true;
        });
        buildSettingsRows(); audio_.context();
        return true;
    }
    case Id::launch_env_add:
        settingsTextRow({}, [this, page_key](const std::wstring& value) {
            const auto text = narrow(value);
            const auto equals = text.find('=');
            if (equals == std::string::npos || !catalog::variableName(text.substr(0, equals))) {
                showNotification(NotificationKind::error, tr(L"Nieprawidłowa zmienna"), tr(L"Wpisz NAZWA=wartość, np. DXVK_HUD=fps."));
                return false;
            }
            return compatSaveOptions(page_key, [&](LaunchOptions& options) {
                std::erase_if(options.environment, [&](const auto& variable) { return variable.name == text.substr(0, equals); });
                options.environment.push_back({text.substr(0, equals), text.substr(equals + 1)});
                return options.environment.size() <= 64;
            });
        }, false);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::launch_env_preset:
        return pick([&](const Choice& choice) {
            const auto name = narrow(choice.key);
            const auto presets = catalog::variablePresets();
            const auto preset = std::find_if(presets.begin(), presets.end(), [&](const auto& item) { return name == item.name; });
            if (preset == presets.end()) return;
            compatSaveOptions(page_key, [&](LaunchOptions& options) {
                std::erase_if(options.environment, [&](const auto& variable) { return variable.name == preset->name; });
                options.environment.push_back({preset->name, preset->value});
                return true;
            });
        });
    case Id::launch_option: {
        const auto* option = catalog::find(row.key);
        if (!option) return true;
        if (row.key == "prefix") {
            return pick([&](const Choice& choice) {
                if (choice.key == L"browse") {
                    file_browser::Request request;
                    request.mode = file_browser::Mode::folder;
                    request.title = tr(L"Wybierz folder prefiksu");
                    request.initial = store::prefixRoot();
                    openFileBrowser(std::move(request), [this, page_key](const std::filesystem::path& picked) {
                        if (picked.empty()) return;
                        compatSaveOptions(page_key, [&](LaunchOptions& options) { options.prefix = picked.lexically_normal(); return true; });
                        compatLoadTools();
                        if (settingsOpen()) buildSettingsRows();
                    });
                    return;
                }
                compatSaveOptions(page_key, [&](LaunchOptions& options) { options.prefix = std::filesystem::path(choice.key); return true; });
                compatLoadTools();
            });
        }
        if (row.control == Control::choice)
            return pick([&](const Choice& choice) {
                compatSaveOptions(page_key, [&](LaunchOptions& options) { return catalog::setValue(options, option->id, narrow(choice.key)); });
            });
        if (row.control == Control::toggle) {
            if (row.locked) { navigationBoundary(); return true; }
            compatSaveOptions(page_key, [&](LaunchOptions& options) { return catalog::setValue(options, option->id, row.checked ? "0" : "1"); });
            buildSettingsRows(); audio_.context();
            return true;
        }
        // A text, number or path: the dialog shows the current value; an emptied field is the default.
        const auto current = catalog::value(page_key.empty() ? store::defaults() : store::gameOptions(page_key).value_or(gameDefaults()), option->id);
        settingsTextRow(wide(current), [this, page_key, option](const std::wstring& value) {
            auto text = narrow(value);
            if (text.empty() && option->type == catalog::Type::number) text = catalog::value(LaunchOptions{}, option->id);
            const bool saved = compatSaveOptions(page_key, [&](LaunchOptions& options) { return catalog::setValue(options, option->id, text); });
            if (!saved) showNotification(NotificationKind::error, tr(L"Nieprawidłowa wartość"), label(option->label));
            return saved;
        }, option->type == catalog::Type::path);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    }
    default: return false;
    }
}
#endif

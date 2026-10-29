// Skróty: a game's shortcuts outside Launchme (src/platform/game_shortcuts.hpp) on the desktop, in the
// Start menu (Linux: the applications menu) and in Steam (shortcuts.vdf). They start the game through
// Launchme, with its launch options; the desktop's game menu and Tryb TV's Details "…" list them.
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/image_codec.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/game_shortcuts.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"
#include "../library/shortcuts.hpp"
#include "../library/steam.hpp"
#include "../library/steam_accounts.hpp"
#ifndef _WIN32
#include "../platform/updater.hpp"
#endif

#include <algorithm>
#include <cwctype>

namespace {
game_shortcuts::Place placeOf(bool desktop) { return desktop ? game_shortcuts::Place::desktop : game_shortcuts::Place::menu; }

std::wstring hexKey(const std::string& key) {
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    for (const unsigned char c : key) { text += digits[c >> 4]; text += digits[c & 15]; }
    return text;
}
// The Steam shortcut's launch options: they identify Launchme's shortcut of the game (--jump=play:, main.cpp).
std::wstring playOptions(const Game& game) { return L"--jump=play:" + hexKey(gameKey(game)); }
std::filesystem::path shortcutsFile() {
    const auto config = steam_shortcuts::configFolder(findSteamRoot());
    return config.empty() ? config : config / L"shortcuts.vdf";
}

// 32-bit pixels on a transparent square, the picture fitted in its middle.
std::vector<uint8_t> squareIcon(const std::filesystem::path& source, uint32_t side) {
    const auto image = image_codec::decodeFile(source, [side](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (!info.width || !info.height) return std::nullopt;
        const double scale = std::min(static_cast<double>(side) / info.width, static_cast<double>(side) / info.height);
        return image_codec::Size{std::max<uint32_t>(1, static_cast<uint32_t>(info.width * scale)), std::max<uint32_t>(1, static_cast<uint32_t>(info.height * scale))};
    }, image_codec::Filter::cubic);
    if (!image || image->width > side || image->height > side) return {};
    image_codec::Image square;
    square.width = square.height = side;
    square.pixels.assign(static_cast<size_t>(side) * side * 4, 0);
    const uint32_t left = (side - image->width) / 2, top = (side - image->height) / 2;
    for (uint32_t y = 0; y < image->height; ++y)
        std::copy_n(image->pixels.begin() + static_cast<ptrdiff_t>(y) * image->width * 4, static_cast<size_t>(image->width) * 4,
                    square.pixels.begin() + (static_cast<ptrdiff_t>(top + y) * side + left) * 4);
    return image_codec::encodePng(square);
}

#ifdef _WIN32
// An .ico holding one 256×256 PNG (Windows Vista and later read it).
std::vector<uint8_t> icoOf(const std::vector<uint8_t>& png) {
    std::vector<uint8_t> ico{0, 0, 1, 0, 1, 0, // Reserved, type 1 (icon), one image.
                             0, 0, 0, 0, 1, 0, 32, 0}; // 256×256 (0), no palette, 1 plane, 32 bits.
    const auto put32 = [&](uint32_t value) { for (int i = 0; i < 4; ++i) ico.push_back(static_cast<uint8_t>(value >> (8 * i))); };
    put32(static_cast<uint32_t>(png.size()));
    put32(22); // The PNG right after the header and its one entry.
    ico.insert(ico.end(), png.begin(), png.end());
    return ico;
}
#endif
} // namespace

const wchar_t* Launcher::shortcutPlaceName(ShortcutPlace place) const {
    switch (place) {
    case ShortcutPlace::desktop: return tr(L"Skrót na pulpicie");
#ifdef _WIN32
    case ShortcutPlace::menu: return tr(L"Menu Start");
#else
    case ShortcutPlace::menu: return tr(L"Menu aplikacji");
#endif
    case ShortcutPlace::steam: return L"Steam";
    }
    return L"";
}

// Asked while menus are drawn: the answer is kept for a few seconds (folders, Steam's install).
bool Launcher::shortcutAvailableAnywhere(ShortcutPlace place) const {
    const auto index = static_cast<size_t>(place);
    const auto now = GetTickCount64();
    auto& known = shortcut_places_[index];
    if (known.checked && now - known.checked < 5000) return known.available;
    known.available = place == ShortcutPlace::steam ? !findSteamRoot().empty() : !game_shortcuts::folder(placeOf(place == ShortcutPlace::desktop)).empty();
    known.checked = std::max<uint64_t>(1, now);
    return known.available;
}

bool Launcher::shortcutAvailable(const Game& game, ShortcutPlace place) const {
    if (!isGame(game) || gameKey(game).empty() || !game.installed) return false;
    if (place == ShortcutPlace::steam && game.source == GameSource::steam) return false; // A Steam game is in Steam already.
    return shortcutAvailableAnywhere(place);
}

bool Launcher::hasShortcut(const Game& game, ShortcutPlace place) const {
    if (gameKey(game).empty()) return false;
    if (place == ShortcutPlace::steam) return steamShortcut(game) != 0;
    return !game_shortcuts::find(placeOf(place == ShortcutPlace::desktop), gameKey(game), game.name).empty();
}

// Its page in Settings → Biblioteka: the way to its shortcuts from Tryb TV (Details "…" → Skróty).
void Launcher::openGameShortcuts(const Game& game) {
    if (gameKey(game).empty()) return;
    dismissMenu();
    shortcuts_game_ = game;
    if (!settingsOpen()) openSettings();
    if (!settingsOpen()) return;
    settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
    settings_state_.open(settings_ui::Page::game_shortcuts, gameKey(game), 0, std::nullopt);
    settings_page_exits_ = true;
    clearSecretInput();
    settings_motion_.valid = false;
    buildSettingsRows(); settingsPreviewFocus(); audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Steam ---------------------------------------------------------------------------------------

uint32_t Launcher::steamShortcut(const Game& game) const {
    const auto file = shortcutsFile();
    return file.empty() || gameKey(game).empty() ? 0 : steam_shortcuts::find(file, playOptions(game));
}

bool Launcher::setSteamShortcut(const Game& game, bool add, bool notify) {
    const auto file = shortcutsFile();
    if (file.empty()) {
        if (notify) showNotification(NotificationKind::error, tr(L"Nie znaleziono konta Steam"), tr(L"Zaloguj się w Steamie na tym komputerze."));
        return false;
    }
    const bool steam_running = notify && steam_accounts::runningSteamProcess() != 0;
    const std::wstring restart = steam_running ? std::wstring(tr(L"Uruchom ponownie Steam, aby zobaczyć zmianę.")) : std::wstring{};
    if (!add) {
        if (!steam_shortcuts::remove(file, playOptions(game))) {
            if (notify) showNotification(NotificationKind::error, tr(L"Nie udało się zmienić skrótów Steam"), file.wstring());
            return false;
        }
        if (notify) showNotification(NotificationKind::launch, game.name, restart.empty() ? std::wstring(tr(L"Usunięto ze Steam")) : tr(L"Usunięto ze Steam. ") + restart);
        return true;
    }
    // Windows: launchme.exe. Linux: the helper script (a new AppImage elsewhere keeps the shortcut working).
    auto program = shortcutProgram(true);
    std::wstring options = playOptions(game);
#ifndef _WIN32
    if (program.empty()) {
        program = updater::appImage().value_or(system_info::executablePath());
        // A data folder of its own (LAUNCHME_CONFIG_DIR): Steam starts Launchme with it too.
        if (const auto folder = system_info::environment(L"LAUNCHME_CONFIG_DIR", 4095); folder && !folder->empty())
            options = L"LAUNCHME_CONFIG_DIR=\"" + *folder + L"\" %command% " + options;
    }
#endif
    if (program.empty()) return false;
    const auto cache = coverCacheDirectory();
    steam_shortcuts::Shortcut shortcut;
    shortcut.name = game.name;
    shortcut.exe = program.wstring();
    shortcut.start_dir = program.parent_path().wstring();
    shortcut.launch_options = options;
    const auto image = [&](ArtworkKind kind) {
        const auto found = cachedArtworkFile(game, kind, cache);
        auto extension = found.extension().wstring();
        for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
        return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ? found : std::filesystem::path{};
    };
    const auto icon = shortcutIcon(game); // Square, as Steam's list shows it.
    shortcut.icon = (icon.empty() ? image(ArtworkKind::cover) : icon).wstring();
    const uint32_t id = steam_shortcuts::add(file, shortcut);
    if (!id) {
        if (notify) showNotification(NotificationKind::error, tr(L"Nie udało się zmienić skrótów Steam"), file.wstring());
        return false;
    }
    // Its artwork in Steam: the poster, the wide capsule, the hero and the logo under the shortcut's appid.
    const auto grid = file.parent_path() / L"grid";
    std::error_code ec;
    std::filesystem::create_directories(grid, ec);
    const auto copy = [&](ArtworkKind kind, const std::wstring& suffix) {
        const auto source = image(kind);
        if (source.empty()) return;
        std::error_code copy_error;
        std::filesystem::copy_file(source, grid / (std::to_wstring(id) + suffix + source.extension().wstring()),
                                   std::filesystem::copy_options::overwrite_existing, copy_error);
    };
    copy(cachedArtworkFile(game, ArtworkKind::poster, cache).empty() ? ArtworkKind::cover : ArtworkKind::poster, L"p");
    copy(ArtworkKind::landscape, L"");
    copy(ArtworkKind::hero, L"_hero");
    copy(ArtworkKind::logo, L"_logo");
    if (notify) showNotification(NotificationKind::download, game.name, restart.empty() ? std::wstring(tr(L"Dodano do Steam")) : tr(L"Dodano do Steam. ") + restart);
    return true;
}

// Every installed game of the library at once (Settings → Library → Skróty gier): in Steam or the menu.
void Launcher::addShortcutsForAll(ShortcutPlace place) {
    size_t added = 0, failed = 0;
    for (const auto& game : grouped_library_) {
        if (!shortcutAvailable(game, place) || hasShortcut(game, place)) continue;
        if (place == ShortcutPlace::steam) {
            if (setSteamShortcut(game, true, false)) ++added; else ++failed;
            continue;
        }
        game_shortcuts::Shortcut shortcut;
        shortcut.title = game.name;
        shortcut.key = gameKey(game);
        shortcut.program = shortcutProgram(true);
        shortcut.icon = shortcutIcon(game);
        if (!shortcut.program.empty() && !game_shortcuts::create(placeOf(place == ShortcutPlace::desktop), shortcut).empty()) ++added; else ++failed;
    }
    std::wstring detail = tr(L"Dodano: ") + std::to_wstring(added);
    if (failed) detail += std::wstring(L" · ") + tr(L"nie udało się: ") + std::to_wstring(failed);
    if (place == ShortcutPlace::steam && added && steam_accounts::runningSteamProcess()) detail += std::wstring(L" · ") + tr(L"Uruchom ponownie Steam, aby zobaczyć zmianę.");
    showNotification(failed && !added ? NotificationKind::error : NotificationKind::download, shortcutPlaceName(place), detail);
}

// A game that leaves the library takes Launchme's shortcuts of it along.
void Launcher::removeShortcuts(const Game& game) {
    if (gameKey(game).empty()) return;
    for (const auto place : {game_shortcuts::Place::desktop, game_shortcuts::Place::menu}) game_shortcuts::remove(place, gameKey(game), game.name);
    if (const auto file = shortcutsFile(); !file.empty() && steamShortcut(game)) steam_shortcuts::remove(file, playOptions(game));
    std::error_code ec;
    const auto icons = userConfigDirectory() / L"shortcut-icons";
    const auto slug = text_encoding::wideLenient(game_shortcuts::slug(gameKey(game)));
    std::filesystem::remove(icons / (slug + L".ico"), ec);
    std::filesystem::remove(icons / (slug + L".png"), ec);
}

std::filesystem::path Launcher::shortcutProgram(bool create) const {
#ifdef _WIN32
    (void)create;
    return system_info::executablePath();
#else
    return game_shortcuts::helper(userConfigDirectory(), updater::appImage().value_or(system_info::executablePath()), create);
#endif
}

std::filesystem::path Launcher::shortcutIcon(const Game& game) const {
#ifdef _WIN32
    // A Windows game's own program carries its icon.
    std::error_code ec;
    if (!game.executable.empty() && std::filesystem::is_regular_file(game.executable, ec)) {
        auto extension = game.executable.extension().wstring();
        for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
        if (extension == L".exe") return game.executable;
    }
#endif
    const auto cache = coverCacheDirectory();
    auto source = cachedArtworkFile(game, ArtworkKind::poster, cache);
    if (source.empty()) source = cachedArtworkFile(game, ArtworkKind::cover, cache);
    if (source.empty()) return {};
    const auto png = squareIcon(source, 256);
    if (png.empty()) return {};
    const auto folder = userConfigDirectory() / L"shortcut-icons";
#ifdef _WIN32
    const auto file = folder / text_encoding::wideLenient(game_shortcuts::slug(gameKey(game)) + ".ico");
    const auto bytes = icoOf(png);
#else
    const auto file = folder / (game_shortcuts::slug(gameKey(game)) + ".png");
    const auto& bytes = png;
#endif
    return atomic_file::write(file, std::span<const uint8_t>(bytes.data(), bytes.size())) ? file : std::filesystem::path{};
}

void Launcher::setShortcut(const Game& game, ShortcutPlace place, bool add) {
    if (gameKey(game).empty()) return;
    if (place == ShortcutPlace::steam) { setSteamShortcut(game, add); return; }
    const auto where = placeOf(place == ShortcutPlace::desktop);
    const bool desktop = place == ShortcutPlace::desktop;
    if (!add) {
        if (!game_shortcuts::remove(where, gameKey(game), game.name)) {
            showNotification(NotificationKind::error, tr(L"Nie udało się usunąć skrótu"), game_shortcuts::folder(where).wstring());
            return;
        }
        showNotification(NotificationKind::launch, game.name, desktop ? tr(L"Usunięto skrót z pulpitu") :
#ifdef _WIN32
                         tr(L"Usunięto z menu Start"));
#else
                         tr(L"Usunięto z menu aplikacji"));
#endif
        return;
    }
    game_shortcuts::Shortcut shortcut;
    shortcut.title = game.name;
    shortcut.key = gameKey(game);
    shortcut.program = shortcutProgram(true);
    shortcut.icon = shortcutIcon(game);
    const auto file = shortcut.program.empty() ? std::filesystem::path{} : game_shortcuts::create(where, shortcut);
    if (file.empty()) {
        showNotification(NotificationKind::error, tr(L"Nie udało się utworzyć skrótu"), game_shortcuts::folder(where).wstring());
        return;
    }
    showNotification(NotificationKind::download, game.name, desktop ? tr(L"Utworzono skrót na pulpicie") :
#ifdef _WIN32
                     tr(L"Dodano do menu Start"));
#else
                     tr(L"Dodano do menu aplikacji"));
#endif
}

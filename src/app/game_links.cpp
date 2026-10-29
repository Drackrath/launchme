// Skróty: a game's shortcuts outside Launchme (src/platform/game_shortcuts.hpp) on the desktop and in
// the Start menu (Linux: the applications menu), and on Linux in Steam (compat_settings.cpp). They
// start the game through Launchme, with its launch options; the desktop's game menu lists them.
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/image_codec.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/game_shortcuts.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"
#ifndef _WIN32
#include "../platform/updater.hpp"
#endif

#include <algorithm>
#include <cwctype>

namespace {
game_shortcuts::Place placeOf(bool desktop) { return desktop ? game_shortcuts::Place::desktop : game_shortcuts::Place::menu; }

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

bool Launcher::shortcutAvailable(const Game& game, ShortcutPlace place) const {
    if (!isGame(game) || gameKey(game).empty() || !game.installed) return false;
    switch (place) {
    case ShortcutPlace::desktop: case ShortcutPlace::menu: return !game_shortcuts::folder(placeOf(place == ShortcutPlace::desktop)).empty();
    case ShortcutPlace::steam:
#ifdef _WIN32
        return false;
#else
        return game.source != GameSource::steam; // A Steam game is in Steam already.
#endif
    }
    return false;
}

bool Launcher::hasShortcut(const Game& game, ShortcutPlace place) const {
    if (gameKey(game).empty()) return false;
    if (place == ShortcutPlace::steam) {
#ifdef _WIN32
        return false;
#else
        return compatSteamShortcut(game) != 0;
#endif
    }
    return !game_shortcuts::find(placeOf(place == ShortcutPlace::desktop), gameKey(game), game.name).empty();
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
    if (place == ShortcutPlace::steam) {
#ifndef _WIN32
        compatSetSteamShortcut(game, add);
#endif
        return;
    }
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

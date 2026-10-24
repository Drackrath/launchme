#include "esde_art.hpp"
#include "image_compact.hpp"
#include "../library/esde_gamelist.hpp"
#include "../platform/atomic_file.hpp"

#include <fstream>

namespace {
bool image(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 128 && ((bytes[0] == 0x89 && bytes[1] == 'P') ||
                                  (bytes[0] == 0xff && bytes[1] == 0xd8));
}

std::vector<uint8_t> load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 128 || input.tellg() > 12 * 1024 * 1024) return {};
    const size_t size = static_cast<size_t>(input.tellg());
    input.seekg(0);
    std::vector<uint8_t> bytes(size);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return {};
    return image(bytes) ? std::move(bytes) : std::vector<uint8_t>{};
}

std::vector<uint8_t> artworkIn(const std::filesystem::path& folder,
                                const std::wstring& title, const std::wstring& stem) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return {};
    for (const auto& name : {stem, title}) {
        if (name.empty()) continue;
        for (const auto* extension : {L".jpg", L".png", L".jpeg"}) {
            auto bytes = load(folder / (name + extension));
            if (!bytes.empty()) return bytes;
        }
    }
    // Only an unidentified game falls back to a fuzzy file-name scan: an identified ROM's
    // media is named after the ROM, so another file would belong to another game.
    if (!stem.empty()) return {};
    const auto wanted = esde::cleanedTitle(title);
    if (wanted.empty()) return {};
    for (const auto& file : std::filesystem::directory_iterator(folder, ec)) {
        if (ec || !file.is_regular_file(ec)) continue;
        if (esde::cleanedTitle(file.path().stem().wstring()) != wanted) continue;
        auto bytes = load(file.path());
        if (!bytes.empty()) return bytes;
    }
    return {};
}

std::vector<uint8_t> mediaFor(const std::filesystem::path& system_media, const std::wstring& title,
                              const std::wstring& stem, EmuArtKind kind) {
    // ES-DE media folders, best first: fanart/screenshots for backgrounds, covers/box fronts
    // for covers, marquees (ScreenScraper "wheel" logos) for logos.
    const std::vector<const wchar_t*> folders = kind == EmuArtKind::background ? std::vector<const wchar_t*>{L"fanart", L"screenshots"} :
        kind == EmuArtKind::cover ? std::vector<const wchar_t*>{L"covers", L"box2dfront"} : std::vector<const wchar_t*>{L"marquees"};
    for (const auto* folder : folders) {
        auto bytes = artworkIn(system_media / folder, title, stem);
        // Logos are drawn over art: only a PNG can carry transparency.
        if (!bytes.empty() && (kind != EmuArtKind::logo || bytes[0] == 0x89)) return bytes;
    }
    return {};
}

const wchar_t* cacheFolder(EmuArtKind kind) {
    return kind == EmuArtKind::background ? L"backgrounds" : kind == EmuArtKind::logo ? L"logos" : L"covers";
}
} // namespace

std::vector<uint8_t> esdeArtFromMedia(const std::filesystem::path& media_root,
                                      const std::filesystem::path& gamelists_root,
                                      const std::wstring& system, const std::wstring& title,
                                      EmuArtKind kind, const std::filesystem::path& rom) {
    const auto list = esde::loadGamelist(gamelists_root / system / L"gamelist.xml");
    const auto* entry = esde::match(list, rom, title);
    return mediaFor(media_root / system, title, entry ? entry->stem() : std::wstring{}, kind);
}

std::vector<std::filesystem::path> esdeMediaFiles(const std::wstring& platform, const std::wstring& title,
                                                  const std::filesystem::path& rom, const std::vector<const wchar_t*>& folders) {
    std::vector<std::filesystem::path> result;
    const auto root = esde::root();
    if (root.empty()) return result;
    const auto media = esde::mediaDirectory(root);
    esde::Library library(root / L"gamelists");
    std::vector<std::pair<std::wstring, std::wstring>> places; // System, file stem.
    if (const auto found = library.find(platform, rom, title)) places.emplace_back(found->system, found->entry.stem());
    else for (const auto& system : esde::systems(platform, rom)) places.emplace_back(system, title);
    std::error_code ec;
    for (const auto& [system, stem] : places)
        for (const auto* folder : folders)
            for (const auto* extension : {L".png", L".jpg", L".jpeg"}) {
                const auto file = media / system / folder / (stem + extension);
                if (std::filesystem::is_regular_file(file, ec) && !load(file).empty()) result.push_back(file);
            }
    return result;
}

std::filesystem::path esdeVideoFile(const std::wstring& platform, const std::wstring& title, const std::filesystem::path& rom) {
    const auto root = esde::root();
    if (root.empty()) return {};
    const auto media = esde::mediaDirectory(root);
    esde::Library library(root / L"gamelists");
    const auto found = library.find(platform, rom, title);
    if (!found) return {};
    std::error_code ec;
    for (const auto* extension : {L".mp4", L".mkv", L".webm", L".avi", L".mov"}) {
        const auto file = media / found->system / L"videos" / (found->entry.stem() + extension);
        if (std::filesystem::is_regular_file(file, ec) && std::filesystem::file_size(file, ec) > 1024) return file;
    }
    return {};
}

std::filesystem::path esdeArtCachePath(uint32_t app_id, EmuArtKind kind, const std::filesystem::path& cache) {
    return cache / cacheFolder(kind) / (std::to_wstring(app_id) + L".img");
}

std::vector<uint8_t> esdeArtForShortcut(uint32_t app_id, const std::wstring& platform,
                                        const std::wstring& title, EmuArtKind kind,
                                        const std::filesystem::path& cache,
                                        const std::filesystem::path& rom) {
    if (!app_id || title.empty()) return {};
    const auto file = esdeArtCachePath(app_id, kind, cache);
    auto image = load(file);
    if (!image.empty()) return image;
    const auto root = esde::root();
    if (root.empty()) return {};
    const auto media = esde::mediaDirectory(root);
    esde::Library library(root / L"gamelists");
    if (const auto found = library.find(platform, rom, title))
        image = mediaFor(media / found->system, title, found->entry.stem(), kind);
    else // Not in any gamelist (never scraped): media named like the shortcut, if any.
        for (const auto& system : esde::systems(platform, rom)) {
            image = mediaFor(media / system, title, {}, kind);
            if (!image.empty()) break;
        }
    if (image.empty()) return {};
    writeCachedImage(file, image);
    return image;
}

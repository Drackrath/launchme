// Edytuj metadane (desktop mode): the player's own title, description, developer, publisher, release
// date and genres for one game, over what the stores and databases found (a PC port matched as the
// console game it came from). Only fields changed in the dialog become overrides; an emptied field
// goes back to the automatic value, and Przywróć automatyczne forgets them all.
#include "launcher.hpp"

#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

using namespace desktop_ui;

namespace {
constexpr size_t limits[meta_fields] = {120, 4000, 160, 160, 40, 200, 16};
std::wstring metadata::Override::* const members[meta_fields] = {
    &metadata::Override::title, &metadata::Override::description, &metadata::Override::developer,
    &metadata::Override::publisher, &metadata::Override::release_date, &metadata::Override::genres, &metadata::Override::hours,
};
// 360 → "6", 450 → "7,5" (half hours).
std::wstring hoursText(uint32_t minutes) {
    const int halves = static_cast<int>(std::lround(minutes / 30.0));
    return std::to_wstring(halves / 2) + (halves % 2 ? L",5" : L"");
}
// "22", "7,5", "20-24 h" (the middle of a range) → minutes; none without a number.
std::optional<uint32_t> ownMinutes(const std::wstring& text) {
    std::vector<double> numbers;
    for (size_t i = 0; i < text.size() && numbers.size() < 2;) {
        if (!std::iswdigit(text[i])) { ++i; continue; }
        double value = 0;
        while (i < text.size() && std::iswdigit(text[i])) value = value * 10 + (text[i++] - L'0');
        if (i + 1 < text.size() && (text[i] == L',' || text[i] == L'.') && std::iswdigit(text[i + 1])) {
            double scale = 0.1;
            for (++i; i < text.size() && std::iswdigit(text[i]); ++i) { value += (text[i] - L'0') * scale; scale *= 0.1; }
        }
        numbers.push_back(value);
    }
    if (numbers.empty()) return std::nullopt;
    const double hours = numbers.size() == 2 ? (numbers[0] + numbers[1]) * 0.5 : numbers[0];
    if (hours <= 0 || hours > 2000) return std::nullopt;
    return static_cast<uint32_t>(std::lround(hours * 60));
}
// "2024", "12.05.2024", "2024-05-12" → a date (the year at least); unknown without a year.
game_index::Date ownDate(const std::wstring& text) {
    std::vector<int> parts;
    for (size_t i = 0; i < text.size() && parts.size() < 3;) {
        if (!std::iswdigit(text[i])) { ++i; continue; }
        int value = 0;
        while (i < text.size() && std::iswdigit(text[i]) && value < 100000) value = value * 10 + (text[i++] - L'0');
        parts.push_back(value);
    }
    game_index::Date date;
    const auto year = std::find_if(parts.begin(), parts.end(), [](int value) { return value >= 1950 && value <= 2100; });
    if (year == parts.end()) return date;
    date.year = *year;
    if (parts.size() == 3) {
        const bool first = year == parts.begin(); // yyyy-mm-dd, else dd.mm.yyyy.
        const int month = first ? parts[1] : parts[1], day = first ? parts[2] : parts[0];
        if (month >= 1 && month <= 12) { date.month = month; if (day >= 1 && day <= 31) date.day = day; }
    } else if (parts.size() == 2) {
        const int month = year == parts.begin() ? parts[1] : parts[0];
        if (month >= 1 && month <= 12) date.month = month;
    }
    return date;
}
std::vector<std::wstring> ownList(const std::wstring& text) {
    std::vector<std::wstring> items;
    std::wstring item;
    const auto flush = [&] {
        while (!item.empty() && item.back() == L' ') item.pop_back();
        size_t start = 0;
        while (start < item.size() && item[start] == L' ') ++start;
        if (start < item.size() && items.size() < 6) items.push_back(item.substr(start));
        item.clear();
    };
    for (const wchar_t c : text) { if (c == L',' || c == L';') flush(); else item += c; }
    flush();
    return items;
}
std::wstring trimmed(std::wstring text) {
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r'; };
    while (!text.empty() && space(text.back())) text.pop_back();
    size_t start = 0;
    while (start < text.size() && space(text[start])) ++start;
    return text.substr(start);
}
} // namespace

const metadata::Override* Launcher::metadataOverride(const Game& game) const {
    if (!metadata_loaded_) {
        metadata_loaded_ = true;
        metadata_overrides_.load(userConfigDirectory() / L"metadata_overrides.json");
    }
    const auto key = gameKey(game);
    return key.empty() ? nullptr : metadata_overrides_.find(key);
}

// The game index (Towarzyszka's tools, sorting, plan_grania) takes them ahead of every source.
void Launcher::applyMetadataToIndex(game_index::LibraryGame& item, const Game& game) const {
    const auto* own = metadataOverride(game);
    if (!own) return;
    item.own_description = own->description;
    item.own_developers = ownList(own->developer);
    item.own_publishers = ownList(own->publisher);
    item.own_genres = ownList(own->genres);
    item.own_release = ownDate(own->release_date);
    item.own_hltb_main = ownMinutes(own->hours);
}

// A new title: what was looked up under the old one (HowLongToBeat, RAWG, its misses) is dropped and
// looked up again by the new name.
void Launcher::forgetTitleLookups(const std::string& key) {
    std::string cache_key;
    for (const auto* games : {&public_library_, &private_category_.games, &grouped_library_})
        for (const auto& game : *games) if (cache_key.empty() && gameKey(game) == key) cache_key = gameCacheKey(game);
    if (cache_key.empty()) return;
    const auto config = userConfigDirectory();
    const auto name = text_encoding::wideLenient(cache_key);
    std::error_code error;
    for (const auto& file : {config / L"plugins" / L"data" / L"launchme.howlongtobeat" / (name + L".json"), config / L"knowledge" / L"rawg" / (name + L".json"),
                             config / L"knowledge" / L"miss" / (name + L".rawg"), config / L"knowledge" / L"miss" / (name + L".hltb"),
                             config / L"metadata" / (name + L".rawg.json")})
        std::filesystem::remove(file, error);
    rawg_metadata_.erase(key);
    gameIndexMerge(key, game_index::Source::rawg, {});
    gameIndexMerge(key, game_index::Source::hltb, {});
    gameIndexTouch({key});
}

// Once, when the library is there: titles set before their lookups were redone (an older version)
// drop what was found under the old name.
void Launcher::refreshTitleLookups() {
    if (metadata_lookups_checked_ || public_library_.empty() || scanning_) return;
    metadata_lookups_checked_ = true;
    if (!metadata_loaded_) { metadata_loaded_ = true; metadata_overrides_.load(userConfigDirectory() / L"metadata_overrides.json"); }
    std::vector<std::string> stale;
    for (const auto& [key, value] : metadata_overrides_.items()) if (!value.title.empty() && !value.looked_up) stale.push_back(key);
    if (stale.empty()) return;
    for (const auto& key : stale) {
        forgetTitleLookups(key);
        auto value = *metadata_overrides_.find(key);
        value.looked_up = true;
        metadata_overrides_.set(key, std::move(value));
    }
    metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json");
    syncGameIndex();
}

void Launcher::applyMetadataTitles(std::vector<Game>& games) const {
    for (auto& game : games)
        if (const auto* value = metadataOverride(game); value && !value->title.empty()) game.name = value->title;
}

void Launcher::desktopMetaOpen(const Game& game) {
    if (!desktopActive() || gameKey(game).empty()) return;
    desktopCloseMenu();
    desktopQuickClose();
    desktop_downloads_open_ = desktop_widgets_open_ = false;
    desktopSearchFocus(false);
    bool loading = false;
    const GameDetails details = desktopDetails(game, loading); // As the pane shows them, overrides included.
    auto& meta = desktop_meta_;
    meta = {};
    meta.open = true;
    meta.game = game.name;
    meta.fields = {game.name, details.description, details.developer, details.publisher, details.release_date, details.genres, {}};
    // Czas przejścia: their own, else what HowLongToBeat gave the index.
    if (const auto* own = metadataOverride(game); own && !own->hours.empty()) meta.fields[6] = own->hours;
    else if (game_index_)
        meta.fields[6] = game_index_->read([key = gameKey(game)](const game_index::Index& index) {
            const auto* record = index.get(key);
            return record && record->hltb_main ? hoursText(*record->hltb_main) : std::wstring{};
        });
    meta.shown = meta.fields;
    meta.overridden = metadataOverride(game) != nullptr;
    meta.caret = meta.fields[0].size();
    desktop_meta_key_ = gameKey(game);
    MSG pending; // The key that opened it must not type into the title.
    while (PeekMessageW(&pending, hwnd_, WM_CHAR, WM_CHAR, PM_REMOVE)) {}
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMetaClose() {
    if (!desktop_meta_.open) return;
    desktop_meta_ = {};
    desktop_meta_key_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Titles, the pane and Szczegóły take the new values (the library is built again: a title also
// orders, groups and finds the game).
void Launcher::desktopMetadataChanged() {
    desktop_details_key_.clear();
    buildShelves();
    syncGameIndex(); // Towarzyszka's index takes the new facts too.
    if (screen_ == ScreenMode::shelves) rebuildCards(true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMetaSave() {
    auto& meta = desktop_meta_;
    if (!meta.open || desktop_meta_key_.empty()) return;
    metadata::Override value;
    if (const auto* existing = metadata_overrides_.find(desktop_meta_key_)) value = *existing;
    for (int i = 0; i < meta_fields; ++i)
        if (meta.fields[static_cast<size_t>(i)] != meta.shown[static_cast<size_t>(i)]) // Edited: its own text; emptied: automatic again.
            value.*members[i] = trimmed(meta.fields[static_cast<size_t>(i)]);
    const bool retitled = meta.fields[0] != meta.shown[0];
    const auto key = desktop_meta_key_;
    if (retitled) value.looked_up = !value.title.empty(); // Looked up afresh below.
    metadata_overrides_.set(desktop_meta_key_, std::move(value));
    if (!metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json")) {
        showNotification(NotificationKind::error, tr(L"Nie udało się zapisać metadanych"), tr(L"Sprawdź dostęp do katalogu konfiguracji."));
        return;
    }
    desktopMetaClose();
    if (retitled) forgetTitleLookups(key);
    desktopMetadataChanged();
}

void Launcher::desktopMetaActivate(int item) {
    switch (static_cast<MetaItem>(item)) {
    case MetaItem::save: desktopMetaSave(); return;
    case MetaItem::restore: {
        if (!desktop_meta_.overridden) return;
        const auto key = desktop_meta_key_;
        const auto* own = metadata_overrides_.find(key);
        const bool titled = own && !own->title.empty(); // Lookups made under that title go too.
        metadata_overrides_.erase(key);
        if (!metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json")) {
            showNotification(NotificationKind::error, tr(L"Nie udało się zapisać metadanych"), tr(L"Sprawdź dostęp do katalogu konfiguracji."));
            return;
        }
        desktopMetaClose();
        if (titled) forgetTitleLookups(key);
        desktopMetadataChanged();
        return;
    }
    case MetaItem::cancel: case MetaItem::close: desktopMetaClose(); return;
    default: return;
    }
}

void Launcher::desktopMetaInsert(std::wstring_view text) {
    auto& meta = desktop_meta_;
    if (meta.focus < 0 || meta.focus >= meta_fields) return;
    const auto index = static_cast<size_t>(meta.focus);
    auto& field = meta.fields[index];
    std::wstring clean;
    for (const wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n' || c == L'\t') { clean += index == 1 && c == L'\n' ? L'\n' : L' '; continue; } // Lines only in Opis.
        if (c >= L' ' && c != 127) clean += c;
    }
    if (clean.empty()) return;
    if (meta.selected) { field.clear(); meta.caret = 0; meta.selected = false; }
    meta.caret = std::min(meta.caret, field.size());
    const size_t room = limits[index] > field.size() ? limits[index] - field.size() : 0;
    if (clean.size() > room) clean.resize(room);
    field.insert(meta.caret, clean);
    meta.caret += clean.size();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMetaChar(wchar_t character) {
    if (character < L' ' || character == 127) return; // Enter, Tab and Backspace are keys (desktopMetaKey).
    desktopMetaInsert(std::wstring_view(&character, 1));
}

bool Launcher::desktopMetaKey(WPARAM key) {
    auto& meta = desktop_meta_;
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    const int field = meta.focus >= 0 && meta.focus < meta_fields ? meta.focus : -1;
    auto* text = field >= 0 ? &meta.fields[static_cast<size_t>(field)] : nullptr;
    if (text) meta.caret = std::min(meta.caret, text->size());
    switch (key) {
    case VK_ESCAPE: desktopMetaClose(); return true;
    case VK_TAB: { // The fields, then Zapisz, Przywróć automatyczne and Anuluj.
        constexpr int count = meta_fields + 3;
        meta.focus = (meta.focus + (shift ? count - 1 : 1)) % count;
        meta.selected = false;
        if (meta.focus < meta_fields) meta.caret = meta.fields[static_cast<size_t>(meta.focus)].size();
        break;
    }
    case VK_RETURN:
        if (control || (field >= 0 && field != 1)) desktopMetaSave(); // Ctrl+Enter anywhere; Enter in a one-line field.
        else if (field == 1) desktopMetaInsert(L"\n");
        else desktopMetaActivate(meta.focus);
        return true;
    case VK_SPACE:
        if (field < 0) desktopMetaActivate(meta.focus);
        return true; // In a field its character types the space.
    case 'A':
        if (!control || !text) return true;
        meta.selected = !text->empty();
        break;
    case 'V': {
        if (!control || !text || !OpenClipboard(hwnd_)) return true;
        std::wstring pasted;
        if (const auto handle = GetClipboardData(CF_UNICODETEXT))
            if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) { pasted = data; GlobalUnlock(handle); }
        CloseClipboard();
        desktopMetaInsert(pasted);
        return true;
    }
    case VK_LEFT: if (text && meta.caret) --meta.caret; meta.selected = false; break;
    case VK_RIGHT: if (text && meta.caret < text->size()) ++meta.caret; meta.selected = false; break;
    case VK_HOME: meta.caret = 0; meta.selected = false; break;
    case VK_END: if (text) meta.caret = text->size(); meta.selected = false; break;
    case VK_BACK:
        if (!text) return true;
        if (meta.selected) { text->clear(); meta.caret = 0; meta.selected = false; }
        else if (meta.caret) text->erase(--meta.caret, 1);
        break;
    case VK_DELETE:
        if (!text) return true;
        if (meta.selected) { text->clear(); meta.caret = 0; meta.selected = false; }
        else if (meta.caret < text->size()) text->erase(meta.caret, 1);
        break;
    default: return true; // Its own keys only while it is open; text comes as WM_CHAR.
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool Launcher::desktopMetaPointer(float mouse_x, float mouse_y, bool press, bool double_click) {
    auto& meta = desktop_meta_;
    if (!meta.open) return false;
    const auto view = renderer_.desktopViewport();
    const float scale = desktopScale();
    const MetaLayout layout{view.width, view.height};
    const int hit = layout.hit(mouse_x / scale, mouse_y / scale);
    if (!press) {
        if (hit != meta.hover) { meta.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return true;
    }
    if (hit < 0) return true; // Outside: it stays (its edits are not lost to a stray click).
    if (hit < meta_fields) {
        meta.focus = hit;
        meta.caret = meta.fields[static_cast<size_t>(hit)].size();
        meta.selected = false;
        // One-line fields: the caret where the click falls, a drag or double click selects.
        if (hit != static_cast<int>(MetaItem::description)) desktopTextPress(layout.field(hit).left + 12, mouse_x / scale, scale, double_click, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    desktopMetaActivate(hit);
    return true;
}

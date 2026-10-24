#pragma once
#include "settings_view.hpp"
#include "../artwork/steamgriddb.hpp"
#include <array>
#include <unordered_set>

namespace artwork_ui {
// Rail order. Poster (600x900) and wide tile (920x430 / 460x215) are the Flix rows' artwork
// (and the screensaver's boxes); cover is the square card image.
inline constexpr std::array<ArtworkKind, 6> kinds{ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo,
                                                   ArtworkKind::poster, ArtworkKind::landscape, ArtworkKind::disc};
inline constexpr std::array<const wchar_t*, 6> names{trKey(L"Okładka"), trKey(L"Tło"), trKey(L"Logo"),
                                                      trKey(L"Plakat"), trKey(L"Kafel poziomy"), trKey(L"Płyta")};
inline constexpr std::array<ArtworkProvider, 3> providers{ArtworkProvider::steamgriddb, ArtworkProvider::screenscraper, ArtworkProvider::esde};
inline const wchar_t* providerName(ArtworkProvider provider) {
    return provider == ArtworkProvider::screenscraper ? L"ScreenScraper" : provider == ArtworkProvider::esde ? L"ES-DE" : L"SteamGridDB";
}
inline int category(ArtworkKind kind) {
    for (int i = 0; i < static_cast<int>(kinds.size()); ++i) if (kinds[i] == kind) return i;
    return 0;
}
struct State {
    Game game;
    int category = 0, page = 0;
    size_t selected = 0;
    float scroll = 0;
    bool gallery = false, loading = false, applying = false, more = false;
    uint32_t applied_id = 0;
    std::wstring status;
    GridSource source;
    std::vector<GridAsset> assets;
    std::unordered_set<uint32_t> requested;
    std::wstring query;
    bool search_focus = false, query_selected = false, results_mode = false;
    std::vector<GridGame> games;
    GridSource override_source;
    // "Z dysku…" under the rail, then its panel: the artwork type (local_kind) and "Wskaż plik…".
    bool local_focus = false, local_open = false;
    int local_kind = 0;
    int local_row = 0; // Focused panel row: a type (0..kinds-1) or local_pick_row.
    static constexpr int local_pick_row = static_cast<int>(kinds.size());
    // "Źródło: …" under "Z dysku…", only for emulated shortcuts: SteamGridDB, ScreenScraper, ES-DE.
    ArtworkProvider provider = ArtworkProvider::steamgriddb;
    bool provider_focus = false;
    // "Popraw kadr" (the crop editor) under them, then Back at the rail's foot.
    bool crop_focus = false, back_focus = false;
    // The rail has the focus on one of its buttons rather than on a type.
    bool railButton() const { return local_focus || provider_focus || crop_focus || back_focus; }
    bool emulated() const { return game.kind == GameKind::nonsteam && game.platform != L"PC"; }
    ArtworkKind kind() const { return kinds[category]; }
    bool providerChoice() const { return emulated() && kind() != ArtworkKind::disc; }
};
struct Layout {
    float width, height;
    ArtworkKind kind;
    bool providers = false; // The "Źródło" entry is shown (State::emulated()).
    settings_ui::Layout base() const { return {width, height}; }
    float left() const { return base().contentLeft(); }
    float right() const { return base().contentRight(); }
    static constexpr float top = 260, gap = 18;
    // The search field starts level with the first rail tab.
    settings_ui::Rect searchBox() const { return {left(), settings_ui::Layout::rail_top, right() - 100, settings_ui::Layout::rail_top + 55}; }
    settings_ui::Rect searchButton() const { return {right() - 88, settings_ui::Layout::rail_top, right(), settings_ui::Layout::rail_top + 55}; }
    // "Wyczyść" inside the field's right end, shown while it holds text.
    settings_ui::Rect clearButton() const { const auto s = searchBox(); return {s.right - 112, s.top + 9, s.right - 9, s.bottom - 9}; }
    settings_ui::Rect gameRow(size_t index, float scroll) const { return {left(), top + index * 58 - scroll, right(), top + index * 58 + 50 - scroll}; }
    float bottom() const { return height - 78; }
    int columns() const {
        switch (kind) {
        case ArtworkKind::cover: case ArtworkKind::disc: return 4;
        case ArtworkKind::hero: return 2;
        case ArtworkKind::poster: return 5;
        default: return 3; // Logo, wide tile.
        }
    }
    float cellWidth() const { return (right() - left() - (columns() - 1) * gap) / columns(); }
    float imageHeight() const {
        switch (kind) {
        case ArtworkKind::cover: case ArtworkKind::disc: return cellWidth();
        case ArtworkKind::poster: return cellWidth() * 1.5f;
        case ArtworkKind::landscape: return cellWidth() * 430.0f / 920.0f;
        default: return cellWidth() * 0.5625f;
        }
    }
    float step() const { return imageHeight() + 44; }
    settings_ui::Rect cell(size_t index, float scroll) const {
        const float x = left() + (index % columns()) * (cellWidth() + gap);
        const float y = top + (index / columns()) * step() - scroll;
        return {x, y, x + cellWidth(), y + imageHeight()};
    }
    int hit(float x, float y, size_t count, float scroll) const {
        if (y < top || y >= bottom()) return -1;
        for (size_t i = 0; i < count; ++i) if (cell(i, scroll).contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    float maxScroll(size_t count) const {
        if (!count) return 0;
        return std::max(0.0f, ((count + columns() - 1) / columns()) * step() - 8 - (bottom() - top));
    }
    float ensureVisible(size_t selected, size_t count, float scroll) const {
        const auto r = cell(selected, scroll);
        if (r.top < top) scroll -= top - r.top;
        else if (r.bottom + 28 > bottom()) scroll += r.bottom + 28 - bottom();
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    // Directly under the last type, a little more compact than the types: "Z dysku…", "Źródło: …"
    // (emulated games only), "Popraw kadr".
    settings_ui::Rect railButton(int index) const {
        const auto first = base().rail(static_cast<int>(kinds.size()));
        const float y = first.top + index * 62;
        return {first.left, y, first.right, y + 52};
    }
    settings_ui::Rect localButton() const { return railButton(0); }
    settings_ui::Rect providerButton() const { return railButton(1); }
    settings_ui::Rect cropButton() const { return railButton(providers ? 2 : 1); }
    settings_ui::Rect pageButton(bool next) const {
        if (providers) {
            const float middle = (left() + right()) * 0.5f;
            return {middle + (next ? 40 : -120), height - 60, middle + (next ? 120 : -40), height - 12};
        }
        const float center = (56 + base().railRight() - 22) * 0.5f, y = cropButton().bottom + 24;
        return next ? settings_ui::Rect{center + 30, y, base().railRight() - 22, y + 48} :
                      settings_ui::Rect{56, y, center - 30, y + 48};
    }
    // Panel: the type list, then "Wskaż plik…".
    static constexpr float local_row_step = 54;
    settings_ui::Rect localPanel() const {
        const float w = std::min(620.0f, width - 96), h = 83 + static_cast<float>(kinds.size()) * local_row_step + 16 + 44 + 28;
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    settings_ui::Rect localChoice(int index) const {
        const auto p = localPanel();
        return {p.left + 18, p.top + 83 + index * local_row_step, p.right - 18, p.top + 133 + index * local_row_step};
    }
    settings_ui::Rect localPick() const {
        const auto p = localPanel();
        return {p.left + 18, p.bottom - 72, p.right - 18, p.bottom - 28};
    }
    // Panel row under the pointer: a type, State::local_pick_row, or -1.
    int hitLocal(float x, float y) const {
        for (int i = 0; i < static_cast<int>(kinds.size()); ++i) if (localChoice(i).contains(x, y)) return i;
        return localPick().contains(x, y) ? State::local_pick_row : -1;
    }
};
}

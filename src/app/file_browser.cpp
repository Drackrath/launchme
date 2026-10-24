// The launcher's own file and folder picker: drives, quick places and folders as Settings-style
// rows, driven by the D-pad, keyboard and mouse. Geometry: src/ui/file_browser_view.hpp,
// drawing: src/ui/file_browser_renderer.cpp. The Windows dialog stays one button away.
#include "launcher.hpp"
#include "../library/steam.hpp"
#include "../platform/file_listing.hpp"
#include "../platform/file_dialog.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>

using namespace file_browser;

namespace {
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

bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto& a = left.native(); const auto& b = right.native();
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// The folder above `folder`; a drive or share root goes to "Ten komputer" (empty).
std::filesystem::path parentOf(const std::filesystem::path& folder) {
    if (folder.empty() || !folder.has_relative_path()) return {};
    auto parent = folder.parent_path();
    return parent == folder ? std::filesystem::path{} : parent;
}

bool imageFile(const std::filesystem::path& path) {
    auto extension = path.extension().native();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg";
}
} // namespace

void Launcher::openFileBrowser(Request request, std::function<void(const std::filesystem::path&)> done) {
    if (desktop_mode_) {
        // The desktop window always uses Windows' own file dialog (the request's own, else the
        // generic one); Tryb TV's picker stays for the pad.
        cancelEnterHold();
        const auto initial = request.initial.empty() ? settings_.file_browser_folder : request.initial;
        const auto picked = request.native ? request.native() :
            pickPath(hwnd_, request.mode == file_browser::Mode::folder, request.title, request.extensions, request.filter_label,
                     request.all_files_option, initial);
        mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime(); // Its closing click stays in the dialog.
        if (!picked.empty() && done) done(picked);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    auto& ui = file_browser_;
    const bool show_hidden = ui.show_hidden; // Kept for the session, like Explorer's view setting.
    ui = {};
    ui.open = true;
    ui.show_hidden = show_hidden;
    ui.request = std::move(request);
    file_browser_done_ = std::move(done);
    cancelEnterHold();
    fileBrowserPlaces();
    // The requested folder (a file: its folder, with the file highlighted), else the last one used.
    std::filesystem::path start = ui.request.initial, select;
    if (!start.empty() && !file_listing::isFolder(start)) { select = start; start = start.parent_path(); }
    if (!start.is_absolute() || !file_listing::isFolder(start)) { start = settings_.file_browser_folder; select.clear(); }
    if (!start.is_absolute() || !file_listing::isFolder(start)) start.clear();
    fileBrowserOpenFolder(start, select);
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeFileBrowser() {
    if (!file_browser_.open) return;
    file_browser_.open = false;
    file_browser_.entries.clear();
    file_browser_done_ = nullptr;
    file_browser_preview_at_ = 0; file_browser_preview_path_.clear();
    renderer_.setFileBrowserPreview({});
    // The release of the click that closed it must not reach the screen beneath.
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::fileBrowserPlaces() {
    auto& ui = file_browser_;
    ui.places.clear();
    ui.places.push_back({tr(L"Ten komputer"), {}, Icon::computer});
    if (file_listing::isFolder(settings_.file_browser_folder))
        ui.places.push_back({tr(L"Ostatnio używany"), settings_.file_browser_folder, Icon::recent});
    using file_listing::Known;
    const std::pair<Known, std::pair<const wchar_t*, Icon>> known[] = {
        {Known::desktop, {tr(L"Pulpit Windows"), Icon::desktop}}, {Known::downloads, {tr(L"Pobrane"), Icon::downloads}},
        {Known::documents, {tr(L"Dokumenty"), Icon::documents}}, {Known::pictures, {tr(L"Obrazy"), Icon::pictures}},
        {Known::music, {tr(L"Muzyka"), Icon::music}}, {Known::videos, {tr(L"Wideo"), Icon::videos}}};
    for (const auto& [folder, place] : known) {
        // Images first for artwork, music first for the sound folder: the rail keeps what fits.
        if (folder == Known::videos && ui.request.images) continue;
        if (auto path = file_listing::knownFolder(folder); !path.empty()) ui.places.push_back({place.first, std::move(path), place.second});
    }
    if (const auto steam = findSteamRoot(); !steam.empty()) {
        const auto common = steam / L"steamapps" / L"common";
        if (file_listing::isFolder(common)) ui.places.push_back({L"Steam", common, Icon::steam});
    }
    const auto view = renderer_.viewport();
    const int capacity = Layout{view.width, view.height}.placeCapacity();
    if (static_cast<int>(ui.places.size()) > capacity) {
        // Steam (games) outranks Videos and Music when the screen is short.
        if (!ui.places.empty() && ui.places.back().icon == Icon::steam && capacity >= 2) {
            auto steam_place = ui.places.back();
            ui.places.resize(capacity - 1);
            ui.places.push_back(std::move(steam_place));
        } else ui.places.resize(capacity);
    }
}

void Launcher::fileBrowserOpenFolder(const std::filesystem::path& folder_in, const std::filesystem::path& select_in) {
    auto& ui = file_browser_;
    // Copies: callers pass paths of the entries (or the folder) replaced below.
    const auto folder = folder_in, select = select_in;
    ui.entries.clear(); ui.error.clear(); ui.typed.clear();
    ui.scroll = 0; ui.selected = 0; ui.hover = -1;
    ui.folder = folder;
    if (folder.empty()) {
        for (const auto& drive : file_listing::drives()) {
            Entry entry;
            entry.kind = Kind::drive;
            entry.path = drive.root;
            const auto letter = drive.root.native().substr(0, 2);
            entry.name = drive.label.empty() ? letter : drive.label + L" (" + letter + L")";
            if (drive.type == DRIVE_REMOVABLE) entry.detail = tr(L"Dysk wymienny");
            else if (drive.type == DRIVE_CDROM) entry.detail = tr(L"Napęd optyczny");
            else if (drive.type == DRIVE_REMOTE) entry.detail = tr(L"Dysk sieciowy");
            else if (drive.total) entry.detail = sizeText(drive.free) + tr(L" wolne z ") + sizeText(drive.total);
            ui.entries.push_back(std::move(entry));
        }
        if (ui.entries.empty()) ui.error = tr(L"Nie znaleziono dysków.");
    } else {
        const auto parent = parentOf(folder);
        Entry up;
        up.kind = Kind::up;
        up.name = tr(L"Folder nadrzędny");
        up.path = parent;
        up.detail = parent.empty() ? std::wstring(tr(L"Ten komputer")) : parent.filename().empty() ? parent.native() : parent.filename().native();
        ui.entries.push_back(std::move(up));
        const auto listing = file_listing::list(folder);
        if (!listing.ok)
            ui.error = listing.error == ERROR_ACCESS_DENIED ? tr(L"Brak dostępu do tego folderu.") : tr(L"Nie można otworzyć tego folderu.");
        for (const auto& item : listing.items) {
            if (item.hidden && !ui.show_hidden) continue;
            Entry entry;
            entry.path = folder / item.name;
            if (item.folder) entry.kind = Kind::folder;
            else {
                if (!ui.accepts(entry.path)) continue;
                entry.kind = Kind::file;
                entry.detail = sizeText(item.size);
            }
            entry.name = item.name;
            entry.size = item.size; entry.modified = item.modified; entry.hidden = item.hidden;
            ui.entries.push_back(std::move(entry));
        }
        if (listing.ok && ui.entries.size() == 1)
            ui.error = ui.folderMode() ? tr(L"Brak podfolderów.") : tr(L"Brak pasujących plików.");
    }
    int index = !folder.empty() && ui.entries.size() > 1 ? 1 : 0; // Past "Folder nadrzędny".
    if (!select.empty())
        for (int i = 0; i < static_cast<int>(ui.entries.size()); ++i)
            if (ui.entries[i].kind != Kind::up && samePath(ui.entries[i].path, select)) { index = i; break; }
    fileBrowserSelect(index);
}

void Launcher::fileBrowserSelect(int index) {
    auto& ui = file_browser_;
    if (ui.entries.empty()) { ui.selected = 0; return; }
    ui.selected = std::clamp(index, 0, static_cast<int>(ui.entries.size()) - 1);
    const auto view = renderer_.viewport();
    const Layout layout{view.width, view.height, ui.request.images};
    ui.scroll = layout.ensureVisible(ui.selected, static_cast<int>(ui.entries.size()), ui.scroll);
    fileBrowserPreview();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::fileBrowserMove(int amount) {
    auto& ui = file_browser_;
    if (!ui.open || !amount) return;
    const auto boundary = [&] { navigationBoundary(); InvalidateRect(hwnd_, nullptr, FALSE); };
    switch (ui.focus) {
    case Focus::list: {
        const int count = static_cast<int>(ui.entries.size());
        const int next = std::clamp(ui.selected + amount, 0, std::max(0, count - 1));
        if (next == ui.selected) {
            // Down past the last row reaches the buttons along the bottom.
            if (amount > 0) { ui.focus = Focus::buttons; ui.button = 0; break; }
            boundary(); return;
        }
        fileBrowserSelect(next);
        break;
    }
    case Focus::places: {
        const int next = ui.place + amount;
        if (next >= static_cast<int>(ui.places.size())) { ui.focus = Focus::back; break; }
        if (next < 0) { boundary(); return; }
        ui.place = next;
        break;
    }
    case Focus::back:
        if (amount < 0) { ui.focus = Focus::places; ui.place = static_cast<int>(ui.places.size()) - 1; break; }
        boundary(); return;
    case Focus::buttons:
        if (amount < 0) { ui.focus = Focus::list; fileBrowserSelect(ui.selected); break; }
        boundary(); return;
    }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::fileBrowserHorizontal(int amount) {
    auto& ui = file_browser_;
    if (!ui.open || !amount) return;
    const auto boundary = [&] { navigationBoundary(); InvalidateRect(hwnd_, nullptr, FALSE); };
    switch (ui.focus) {
    case Focus::list:
        if (amount > 0) { boundary(); return; }
        ui.focus = Focus::places;
        ui.place = std::max(0, ui.activePlace());
        break;
    case Focus::places:
        if (amount < 0) { boundary(); return; }
        ui.focus = Focus::list;
        break;
    case Focus::back:
        if (amount < 0) { boundary(); return; }
        ui.focus = Focus::buttons; ui.button = 0;
        break;
    case Focus::buttons: {
        const int next = ui.button + amount;
        if (next < 0) { ui.focus = Focus::back; break; }
        if (next >= static_cast<int>(ui.buttons().size())) { boundary(); return; }
        ui.button = next;
        break;
    }
    }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::fileBrowserUp() {
    auto& ui = file_browser_;
    if (ui.root()) { navigationBoundary(); return; }
    const auto from = ui.folder;
    fileBrowserOpenFolder(parentOf(from), from);
    ui.focus = Focus::list;
    audio_.navigate();
}

void Launcher::fileBrowserAccept() {
    auto& ui = file_browser_;
    if (!ui.open) return;
    switch (ui.focus) {
    case Focus::back: closeFileBrowser(); return;
    case Focus::places:
        if (ui.place >= 0 && ui.place < static_cast<int>(ui.places.size())) {
            const auto path = ui.places[ui.place].path;
            fileBrowserOpenFolder(path);
            ui.focus = Focus::list;
            audio_.context();
        }
        return;
    case Focus::buttons: {
        const auto buttons = ui.buttons();
        if (ui.button >= 0 && ui.button < static_cast<int>(buttons.size())) fileBrowserButton(buttons[ui.button]);
        return;
    }
    case Focus::list: break;
    }
    const auto* entry = ui.current();
    if (!entry) { navigationBoundary(); return; }
    if (entry->kind == Kind::up) { fileBrowserUp(); return; }
    if (entry->kind == Kind::file) { fileBrowserPick(entry->path); return; }
    const auto path = entry->path;
    fileBrowserOpenFolder(path);
    audio_.context();
}

void Launcher::fileBrowserButton(Button button) {
    auto& ui = file_browser_;
    switch (button) {
    case Button::choose_folder:
        if (ui.root()) { navigationBoundary(); return; } // "Ten komputer" is not a folder.
        fileBrowserPick(ui.folder);
        return;
    case Button::file_type:
    case Button::hidden: {
        if (button == Button::file_type) ui.all_files = !ui.all_files; else ui.show_hidden = !ui.show_hidden;
        const auto* entry = ui.current();
        const auto keep = entry && entry->kind != Kind::up ? entry->path : std::filesystem::path{};
        const float scroll = ui.scroll;
        fileBrowserOpenFolder(ui.folder, keep);
        if (keep.empty()) ui.scroll = scroll;
        audio_.context();
        return;
    }
    case Button::system_dialog: {
        if (!ui.request.native) return;
        const auto native = ui.request.native;
        const auto picked = native();
        // The modal dialog swallowed the release of the press that opened it.
        mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
        if (!picked.empty() && file_browser_.open) fileBrowserPick(picked);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    }
}

void Launcher::fileBrowserPick(const std::filesystem::path& chosen) {
    auto& ui = file_browser_;
    if (!ui.open || chosen.empty()) return;
    const auto path = chosen; // `chosen` may live in the entries that closing clears.
    // Next time a request names no folder, it starts where this one ended.
    const auto folder = ui.folderMode() ? path : path.parent_path();
    if (folder.is_absolute() && !samePath(folder, settings_.file_browser_folder)) {
        settings_.file_browser_folder = folder;
        settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
    }
    auto done = std::move(file_browser_done_);
    closeFileBrowser();
    audio_.context();
    if (done) done(path);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::fileBrowserCharacter(wchar_t character) {
    auto& ui = file_browser_;
    if (!ui.open || character < L' ' || character == 127) return;
    const auto now = GetTickCount64();
    if (now - ui.typed_at > 900) ui.typed.clear();
    ui.typed_at = now;
    // The same letter again steps through the names that start with it.
    const bool cycle = ui.typed.size() == 1 && std::towlower(ui.typed[0]) == std::towlower(character);
    if (!cycle) ui.typed += character;
    const int count = static_cast<int>(ui.entries.size());
    const int start = cycle || ui.typed.size() == 1 ? ui.selected + 1 : ui.selected;
    for (int step = 0; step < count; ++step) {
        const int index = (start + step) % count;
        const auto& entry = ui.entries[index];
        if (entry.kind == Kind::up || entry.name.size() < ui.typed.size()) continue;
        if (_wcsnicmp(entry.name.c_str(), ui.typed.c_str(), ui.typed.size()) != 0) continue;
        ui.focus = Focus::list;
        if (index != ui.selected) audio_.navigate();
        fileBrowserSelect(index);
        return;
    }
    navigationBoundary();
}

void Launcher::fileBrowserKey(WPARAM key, bool repeated) {
    auto& ui = file_browser_;
    if (!ui.open) return;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_F8 && !repeated) { togglePerformance(); return; }
    if (key == VK_F11 && !repeated) { toggleFullscreen(); return; }
    switch (key) {
    case VK_UP: fileBrowserMove(-1); return;
    case VK_DOWN: fileBrowserMove(1); return;
    case VK_LEFT: fileBrowserHorizontal(-1); return;
    case VK_RIGHT: fileBrowserHorizontal(1); return;
    case VK_PRIOR: case VK_NEXT: {
        if (ui.focus != Focus::list) { ui.focus = Focus::list; }
        const auto view = renderer_.viewport();
        const int page = std::max(1, Layout{view.width, view.height, ui.request.images}.visibleRows() - 1);
        const int before = ui.selected;
        fileBrowserSelect(ui.selected + (key == VK_PRIOR ? -page : page));
        if (before == ui.selected) navigationBoundary(); else audio_.navigate();
        return;
    }
    case VK_HOME: case VK_END:
        ui.focus = Focus::list;
        fileBrowserSelect(key == VK_HOME ? 0 : static_cast<int>(ui.entries.size()) - 1);
        audio_.navigate();
        return;
    case VK_TAB:
        if (repeated) return;
        ui.focus = ui.focus == Focus::list ? Focus::places : Focus::list;
        if (ui.focus == Focus::places) ui.place = std::max(0, ui.activePlace());
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case VK_RETURN: case VK_SPACE:
        if (!repeated) fileBrowserAccept();
        return;
    case VK_BACK:
        // Backspace: one folder up; at "Ten komputer" it cancels like Esc.
        if (ui.root()) closeFileBrowser(); else fileBrowserUp();
        return;
    case VK_ESCAPE: closeFileBrowser(); return;
    case VK_F5: {
        const auto* entry = ui.current();
        fileBrowserOpenFolder(ui.folder, entry && entry->kind != Kind::up ? entry->path : std::filesystem::path{});
        return;
    }
    default: break;
    }
    if (control && key == 'H' && !repeated) fileBrowserButton(Button::hidden);
}

void Launcher::fileBrowserWheel(int amount) {
    auto& ui = file_browser_;
    if (!ui.open || !amount) return;
    const auto view = renderer_.viewport();
    const Layout layout{view.width, view.height, ui.request.images};
    ui.scroll = std::clamp(ui.scroll + amount * Layout::row_step * 3, 0.0f, layout.maxScroll(static_cast<int>(ui.entries.size())));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::fileBrowserClick(float x, float y, bool double_click) {
    auto& ui = file_browser_;
    if (!ui.open) return;
    const auto view = renderer_.viewport();
    const Layout layout{view.width, view.height, ui.request.images};
    if (layout.back().contains(x, y)) { if (!double_click) closeFileBrowser(); return; }
    const auto buttons = ui.buttons();
    if (const int button = layout.hitButton(x, y, static_cast<int>(buttons.size())); button >= 0) {
        if (double_click) return;
        ui.focus = Focus::buttons; ui.button = button;
        fileBrowserButton(buttons[button]);
        return;
    }
    if (const int place = layout.hitPlace(x, y, static_cast<int>(ui.places.size())); place >= 0) {
        if (double_click) return;
        ui.focus = Focus::places; ui.place = place;
        fileBrowserAccept();
        return;
    }
    const int row = layout.hitRow(x, y, static_cast<int>(ui.entries.size()), ui.scroll);
    if (row < 0) return;
    // The first click highlights, the double click opens or picks it.
    if (double_click && GetTickCount64() < mouse_transition_until_) return;
    ui.focus = Focus::list;
    if (double_click && row == ui.selected) { fileBrowserAccept(); return; }
    if (row != ui.selected) audio_.navigate();
    fileBrowserSelect(row);
}

void Launcher::fileBrowserHover(float x, float y) {
    auto& ui = file_browser_;
    const auto view = renderer_.viewport();
    const Layout layout{view.width, view.height, ui.request.images};
    const int row = layout.hitRow(x, y, static_cast<int>(ui.entries.size()), ui.scroll);
    if (row == ui.hover) return;
    ui.hover = row;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Images: the highlighted PNG/JPEG is read a moment after the highlight settles.
void Launcher::fileBrowserPreview() {
    const auto& ui = file_browser_;
    if (!ui.request.images) return;
    const auto* entry = ui.current();
    const auto path = entry && entry->kind == Kind::file && imageFile(entry->path) ? entry->path : std::filesystem::path{};
    if (path.empty()) {
        file_browser_preview_at_ = 0;
        if (!file_browser_preview_path_.empty()) { file_browser_preview_path_.clear(); renderer_.setFileBrowserPreview({}); }
        return;
    }
    if (samePath(path, file_browser_preview_path_)) return;
    file_browser_preview_path_ = path;
    file_browser_preview_at_ = GetTickCount64() + 140;
}

void Launcher::pollFileBrowserPreview() {
    if (!file_browser_preview_at_ || GetTickCount64() < file_browser_preview_at_) return;
    file_browser_preview_at_ = 0;
    if (!file_browser_.open) return;
    std::vector<uint8_t> bytes;
    std::ifstream input(file_browser_preview_path_, std::ios::binary | std::ios::ate);
    const auto size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
    // Same cap as "Z dysku…" applies; larger files show no preview (they still can be picked).
    if (size > 0 && size <= 20 * 1024 * 1024) {
        bytes.resize(static_cast<size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) bytes.clear();
    }
    renderer_.setFileBrowserPreview(std::move(bytes));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

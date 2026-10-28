// Keyboard/pad navigation between cards, shelves and screens: back, search typing,
// quick bar, category menu, Enter hold and context menu, launching.
#include "launcher.hpp"
#include "../ui/launch_animation.hpp"
#include "launcher_internal.hpp"
#include "../library/search.hpp"
#include "../platform/text_encoding.hpp"
#include "../ui/category_menu.hpp"
#include "../ui/context_menu.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/quick_menu.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/empty_library.hpp"

#include <algorithm>

void Launcher::navigate(int amount, bool edge_menu) {
    if (libraryDialogOpen()) { libraryDialogKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (edge_menu && amount) { nav_direction_ = amount < 0 ? -1 : 1; nav_direction_at_ = GetTickCount64(); } // Grid rows: changeCategory.
    if (identifyOpen()) return;
    if (gogInstallOpen()) { gogInstallKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (customEditorOpen()) { customEditorKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (friends_open_) { dashboardMove(amount, 0); return; } // The drawer overlays every screen.
    if (glanceOpen()) { glanceMove(amount, 0); return; }
    if (companionOpen()) { companionMove(amount, 0); return; }
    if (desktopActive() && topDialog() == Modal::none && !quick_open_) { desktopMove(amount, 0); return; } // The pad in the desktop window.
    if (category_menu_open_) {
        if (psfiveActive() && edge_menu && amount) {
            closeCategoryMenu();
            psfiveHorizontal(amount);
            return;
        }
        if (edge_menu && amount > 0) { closeCategoryMenu(); audio_.navigate(); }
        return; // Dismiss without selecting the highlighted category or moving a game card.
    }
    if (screen_ == ScreenMode::plugin_page) { if (amount < 0) { plugin_page_.focus = -1; InvalidateRect(hwnd_, nullptr, FALSE); } else if (plugin_page_.focus < 0) pluginPageMove(1); return; }
    if (downloadsOpen()) { // Left: Back; Right from Back: the rows.
        if (amount < 0 && !downloads_state_.back) { downloads_state_.back = true; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
        else if (amount > 0 && downloads_state_.back) downloadsMove(1);
        else if (edge_menu) navigationBoundary();
        return;
    }
    if (libraryHorizontal(amount, edge_menu)) return;
    if (cropOpen()) { cropMove(amount, 0); return; }
    if (devicesOpen()) { deviceAdjust(amount); return; }
    if (artworkOpen()) { artworkMove(amount, 0); return; }
    if (settingsOpen() && !friends_open_) { settingsAdjust(amount); return; }
    if (achievementsOpen()) { if (achievements_.card) achievementsMove(amount); return; } // A card: the previous/next one; the list scrolls vertically only.
    if (screen_ == ScreenMode::details) {
        if (menu_open_) { dismissMenu(); return; }
        if (edge_menu && std::abs(amount) == 1) { detailsMove(amount, 0); return; } // Keys and pad: by position.
        // Programmatic jumps: the buttons, then the achievements row when the game has achievements.
        const int next = std::clamp(detail_action_ + amount, -1, std::max({lastDetailAction(), achievementsDetailAction(), ratingDetailAction(), pluginDetailAction()}));
        if (next == detail_action_) { navigationBoundary(); return; }
        if (next != detail_action_) audio_.navigate();
        detail_action_ = next;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (quick_open_) { quickMove(amount); return; }
    if (searchBackFocused() && !friends_open_) { if (edge_menu) navigationBoundary(); return; } // Back stands alone in its row.
    if (categoryRailActive() && category_rail_focus_ && edge_menu && !pending_launch_) {
        if (amount > 0) focusCategoryRail(false); else navigationBoundary();
        return;
    }
    // No games: Left/Right walk the empty home's buttons (Left past the first one reaches the rail).
    if (emptyHomeActive() && edge_menu && std::abs(amount) == 1 && !(categoryRailActive() && amount < 0 && empty_focus_ == 0)) {
        emptyHomeMove(amount); return;
    }
    if (categoryRailActive() && edge_menu && amount < 0 && (cards_.empty() || selected_ == 0) && !pending_launch_) {
        focusCategoryRail(true); return;
    }
    if (cards_.empty() || pending_launch_ || friends_open_ || category_menu_open_) return;
    if (xmbActive()) {
        if (menu_open_) dismissMenu();
        // Keys move between tiles; programmatic returns (details, artwork, crop) pass a card index delta.
        if (edge_menu) xmbHorizontal(amount);
        else xmbFocusCard(static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount, 0, static_cast<int>(cards_.size()) - 1)));
        return;
    }
    if (flixActive()) {
        if (menu_open_) dismissMenu();
        if (edge_menu) flixHorizontal(amount);
        else flixFocusCard(static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount, 0, static_cast<int>(cards_.size()) - 1)));
        return;
    }
    if (metroActive()) {
        if (menu_open_) dismissMenu();
        if (edge_menu) metroMove(amount, 0);
        else metroFocus(static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount, 0, static_cast<int>(cards_.size()) - 1)));
        return;
    }
    if ((psfiveActions() || psfiveTopFocused()) && edge_menu) { if (menu_open_) dismissMenu(); psfiveHorizontal(amount); return; } // "Graj" / "…", top bar.
    if (screen_ == ScreenMode::shelves && edge_menu && amount < 0 && selected_ == 0) {
        if (psfiveActive() || snitchActive()) { navigationBoundary(); return; }
        openCategoryMenu();
        return;
    }
    // Grid keys stay in their row: Left in the first column opens the library's view drawer,
    // Right in the last one does nothing. Programmatic moves (edge_menu false) pass index deltas.
    if (gridScreen(screen_) && edge_menu && std::abs(amount) == 1) {
        const auto columns = static_cast<size_t>(libraryColumns());
        const bool horizontal = libraryCarousel() || libraryHorizontalCinematic();
        const size_t column = horizontal ? selected_ : selected_ % columns;
        if (amount < 0 && column == 0) {
            if (screen_ == ScreenMode::library) openLibraryPanel(library_ui::Panel::views);
            else navigationBoundary();
            return;
        }
        if (amount > 0 && ((!horizontal && column + 1 == columns) || selected_ + 1 >= cards_.size())) { if (!libraryList()) navigationBoundary(); return; }
    }
    if (menu_open_) dismissMenu();
    const auto next = static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount,
                                                    0, static_cast<int>(cards_.size()) - 1));
    if (next == selected_) { navigationBoundary(); return; }
    focusCard(next);
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openLibrary() {
    rememberPlace(); library_controls_ = {};
    versions_ = versions::Index(public_library_);
    grouped_library_ = versions_.collapse(public_library_, library_state_.preferred_versions, library_state_.first_seen);
    pc_source_.reset(); library_console_.clear();
    library_category_.clear(); category_library_.clear();
    if (public_library_.empty()) {
        // Metro's Biblioteka tile and the bar's Wszystkie gry: say why nothing opens.
        if (!libraryLoading()) showNotification(NotificationKind::launch, tr(L"Biblioteka jest pusta"), tr(L"Dodaj grę albo włącz Platformy w Ustawieniach → Biblioteka."));
        return;
    }
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    audio_.quick();
    rebuildCards(true);
}

void Launcher::openCategoryLibrary(const std::string& id) {
    const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& item) { return item.id == id; });
    // Wydawcy and Developerzy: a board of their logos; a logo opens its games (openPlatform).
    const bool board = studioCategory(id);
    if (category == categories_.end() || (!board && std::none_of(category->games.begin(), category->games.end(), [](const Game& game) { return isGame(game); })) ||
        category->games.empty()) return;
    rememberPlace(); library_controls_ = {};
    pc_source_.reset(); library_console_.clear(); studio_board_return_.clear();
    library_category_ = id;
    category_library_title_ = category->title;
    category_library_.clear();
    for (const auto& game : category->games) if (board || isGame(game)) category_library_.push_back(game);
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    dismissMenu();
    rebuildCards(true);
    audio_.drawer();
}

const std::vector<Game>& Launcher::librarySource() const {
    static const std::vector<Game> none;
    if (!library_console_.empty()) { const auto* games = consoleGames(library_console_); return games ? *games : none; }
    return pc_source_ ? pc_library_ : !library_category_.empty() ? category_library_ : grouped_library_;
}

// A console's games: a library like a PC store's (its own view, order, filter and search,
// saved as "console:<name>"), in every home layout.
void Launcher::openPlatform(const std::wstring& name) {
    const auto* games = consoleGames(name); // A console's, or a publisher's or developer's.
    if (!games || games->empty()) return;
    // From a board of logos (Wydawcy, Developerzy): Back returns to it.
    // Another studio's tab keeps it.
    const bool studio_tab = studios::parse(text_encoding::utf8(library_console_)) && studios::parse(text_encoding::utf8(name));
    studio_board_return_ = studioBoard() ? library_category_ : studio_tab ? studio_board_return_ : std::string{};
    rememberPlace(); library_controls_ = {};
    pc_source_.reset();
    library_category_.clear(); category_library_.clear();
    library_console_ = nested_platform_ = name;
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    dismissMenu();
    rebuildCards(true);
    audio_.drawer();
}

void Launcher::openSearch() {
    rememberPlace();
    screen_ = ScreenMode::search;
    grid_back_focus_ = false;
    search_query_.clear(); search_after_ = 0;
    search_results_.clear();
    search_dirty_ = false;
    grid_scroll_ = 0;
    closeQuick();
    audio_.quick();
    rebuildCards(true);
}

void Launcher::back() {
    const auto dialog = topDialog();
    if (desktopActive() && dialog == Modal::none && !friends_open_ && !quick_open_ && !menu_open_) { desktopBack(); return; }
    if (dialog == Modal::bulk_covers) { bulkCoversClose(); return; }
    if (dialog == Modal::library_dialog) { collectionDialogBack(); return; }
    if (plugin_command_pending_) { ++plugin_command_generation_; plugin_command_pending_ = false; }
    switch (dialog) {
    case Modal::aurelia: aureliaKey(VK_ESCAPE, false); return;
    case Modal::companion_picker: companionPicker(false); return;
    case Modal::file_browser: closeFileBrowser(); return; // Over every panel that opens it.
    case Modal::custom_editor: closeCustomEditor(); return;
    case Modal::metadata: desktopMetaClose(); return;
    case Modal::identify: closeIdentify(); return;
    case Modal::gog_install: gogInstallKey(VK_ESCAPE, false); return;
    case Modal::tarot: closeTarot(); return;
    case Modal::profile: closeProfile(); return;
    case Modal::whats_new: closeWhatsNew(); return;
    default: break;
    }
    if (color_editor_.open) { closeColorEditor(false); return; }
    if (friends_open_) { friendsBack(); return; } // The drawer overlays every screen.
    if (achievementsOpen()) { if (achievements_.card) closeAchievementCard(); else closeAchievements(); return; }
    if (!quick_open_ && !menu_open_ && pluginPageBack()) return; // The page's own level above.
    if (libraryButtons() && library_controls_.action >= 0 && !menu_open_ && !library_controls_.open()) {
        library_controls_.action = -1; InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (libraryControls() && (library_controls_.open() || library_controls_.focus >= 0 || library_controls_.search)) {
        // Panel, then the search field (typing or not: its filter goes before leaving), then the bar.
        if (library_controls_.panel == library_ui::Panel::views && library_controls_.view_dropdown) library_controls_.view_dropdown = false;
        else if (library_controls_.open()) closeLibraryPanel();
        else if (library_controls_.typing() || (library_controls_.search && library_controls_.focus < 0)) closeLibrarySearch();
        else { library_controls_.focus = -1; focusCard(selected_); }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (cropOpen()) { closeCropEditor(); return; }
    if (devicesOpen()) {
        if (device_state_.audio_edit_column >= 0) { endAudioEditing(); return; }
        pointerUp();
        if (device_service_) device_service_->close();
        if (device_state_.panel == devices::Panel::bluetooth && bluetooth_from_panel_) { openDevices(bluetooth_return_); return; }
        screen_ = device_return_;
        if (glanceOpen() && glance_pending_) requestGlance(glance_pending_refresh_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (artworkOpen()) {
        if (artwork_state_.local_open) { closeLocalArtwork(); return; } // "Z dysku…" panel first.
        if (artwork_state_.provider_open) { artwork_state_.provider_open = false; InvalidateRect(hwnd_, nullptr, FALSE); return; } // Then "Źródło"'s list.
        if (artwork_state_.provider_focus) { artwork_state_.provider_focus = false; InvalidateRect(hwnd_, nullptr, FALSE); return; } // Back to the rail.
        if (artwork_state_.search_focus || artwork_state_.results_mode) {
            artwork_state_.search_focus = artwork_state_.results_mode = false;
            artwork_state_.source = artwork_state_.override_source; loadArtworkPage(); return;
        }
        if (artwork_state_.gallery && !artwork_state_.applying) {
            artwork_state_.gallery = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else closeArtworkBrowser();
        return;
    }
    if (quick_open_) { closeQuick(); return; }
    if (category_menu_open_) { closeCategoryMenu(); return; }
    if (menu_open_) { dismissMenu(); return; }
    if (screen_ == ScreenMode::settings) {
        pointerUp();
        mouseLeave();
        if (const auto platform = settings_state_.platform()) {
            // A sign-in in progress ends with its page; while editing, Back ends the edit first.
            const bool editing = settings_state_.editing;
            cancelPlatformSignIn(*platform);
            clearSecretInput(); settings_state_.editing = false;
            // DRM-free game list → its page (focus on "Znalezione gry"), then the overview.
            if (!editing) settings_state_.close();
            settings_motion_.valid = false; buildSettingsRows(); return;
        }
        // A page → the row it was opened from (a value being edited closes first, below).
        if (settings_state_.subpage() && !settings_state_.editing) { closeSettingsPage(); return; }
        if (gog_state_.state == gog::State::preparing || gog_state_.state == gog::State::signing_in || gog_state_.state == gog::State::verifying) gogCommand(gog::Command::cancel);
        if (epic_state_.state == epic::State::preparing || epic_state_.state == epic::State::signing_in || epic_state_.state == epic::State::verifying) epicCommand(epic::Command::cancel);
        if (settings_state_.back()) { clearSecretInput(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        saveSettings();
        showCategory(category_);
        return;
    }
    if (screen_ == ScreenMode::details) {
        store_jobs_->generation.fetch_add(1);
        details_loading_ = false;
        screen_ = detail_return_;
        grid_scroll_ = detail_return_scroll_;
        rebuildCards(true);
        if (!cards_.empty()) navigate(static_cast<int>(std::min(detail_return_selected_, cards_.size() - 1)) - static_cast<int>(selected_), false);
        return;
    }
    if (glanceOpen()) { closeGlance(); return; }
    if (companionOpen()) {
        if (companion_.chat.feed) companionFeedBack();
        else if (companion_.chat.viewer) companionViewer(false); // Her enlarged photo closes first.
        else if (companion_.chat.mind && companion_.chat.mind_confirm) companionMindDeleteConfirm(false); // Memory delete dialog: Back is Anuluj.
        else if (companion_.chat.mind) companionMind(false); // Her 3D mind screen closes first.
        else if (companion_.chat.confirm) companionResetConfirm(false); // The reset dialog: Back is Anuluj.
        else if (companion_.chat.menu) companionMenu(false); // Her "…" menu closes first.
        else if (companion_.chat.archive) companionCloseArchive(); // A history page: back to the live conversation.
        else closeCompanion();
        return;
    }
    if (screen_ == ScreenMode::store) { store_jobs_->generation.fetch_add(1); store_loading_ = false; }
    if (screen_ == ScreenMode::platform_games) { showCategory(category_); return; }
    if (screen_ == ScreenMode::library && !library_console_.empty() && !studio_board_return_.empty()) {
        // A studio's games opened from the board of logos: back to the board, on its logo.
        const auto studio = library_console_;
        openCategoryLibrary(std::exchange(studio_board_return_, {}));
        for (size_t i = 0; i < cards_.size(); ++i) if (cards_[i].game.platform == studio) { focusCard(i); break; }
        return;
    }
    if (screen_ != ScreenMode::shelves) { showCategory(category_); return; }
    if (xmbActive() && xmb_row_ >= 0) { xmbFocus(xmb_column_, -1); audio_.navigate(); return; }
    if (flixActive() && flix_row_ > 0) { flixFocus(0, categories_.empty() ? 0 : categories_[0].selected); audio_.navigate(); return; }
    if (metroTabsFocused()) { metroFocusTabs(false); audio_.navigate(); return; } // The tabs give the focus back to the page.
    if (metroActive() && metro_page_ > 0) { homePage(-static_cast<int>(metro_page_)); return; } // Back to the start page.
    if (psfiveActions()) { psfiveFocusAction(-1); audio_.navigate(); return; } // The buttons give the focus back to the row.
    if (psfiveTopFocused()) { psfiveFocusTop(-1); audio_.navigate(); return; } // So does the top bar.
    if (psfiveActive() && category_ != 0) { showCategory(0); audio_.navigate(); return; } // Another shelf: back to Recent.
    if (categoryRailActive()) { focusCategoryRail(true); return; }
}

void Launcher::onCharacter(wchar_t character) {
    if (desktop_mode_ && character >= L' ' && character != 127) desktopTextBeforeTyping(); // Typing replaces a selection.
    if (desktop_meta_.open && desktopActive() && topModal() == Modal::none) { desktopMetaChar(character); return; } // Edytuj metadane.
    if (desktop_quick_.open && desktopActive() && topModal() == Modal::none) { desktopQuickChar(character); return; } // Ctrl+K's field.
    switch (topDialog()) {
    case Modal::file_browser: fileBrowserCharacter(character); return;
    case Modal::aurelia: aureliaCharacter(character); return;
    case Modal::library_dialog: collectionDialogEdit(character); return;
    case Modal::custom_editor: customEditorInsert(std::wstring_view(&character, 1)); return;
    case Modal::metadata: desktopMetaChar(character); return;
    case Modal::identify: identifyEdit(character); return;
    case Modal::gog_install: case Modal::companion_picker: case Modal::tarot: case Modal::whats_new: case Modal::profile: return;
    default: break; // No dialog, or one without a text field (Pobierz brakujące okładki).
    }
    if (friendChatOpen() && topDialog() == Modal::none) { friendChatCharacter(character); return; } // The drawer's conversation.
    if (desktopActive() && desktopCharacter(character)) return; // Type-to-search in the desktop window.
    if (pluginPageTyping()) { pluginPageType(character); return; }
    if (companionTyping()) { companionType(character); return; }
    if (artworkOpen()) {
        if (character < L' ' || character == 127 || artwork_state_.applying) return;
        if (artwork_state_.query_selected) { artwork_state_.query.clear(); artwork_state_.query_selected = false; }
        if (!artwork_state_.search_focus) artwork_state_.query.clear();
        if (artwork_state_.query.size() < 160) artwork_state_.query += character;
        editArtworkQuery(); return;
    }
    if (editingSecret()) {
        // Plugin settings and plain text rows (ScreenScraper softname, URL) also take spaces and non-ASCII.
        const bool plugin_text = settings_rows_[settings_state_.row()].id == settings_ui::Id::plugin_setting ||
                                 settings_rows_[settings_state_.row()].control == settings_ui::Control::text;
        if (plugin_text ? character < 32 : (character <= 32 || character >= 127)) return;
        if (settings_secret_selected_) { clearSecretInput(); settings_secret_selected_ = false; }
        const auto id = settings_rows_[settings_state_.row()].id;
        const size_t limit = id == settings_ui::Id::battlenet_callback ? 262144u : id == settings_ui::Id::ea_callback ? 16384u :
                             id == settings_ui::Id::gog_callback || id == settings_ui::Id::amazon_callback ? 8192u : 512u;
        if (settings_secret_input_.size() < limit) settings_secret_input_ += character;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // On the library screen typing starts its search at once (a leading space does not).
    if (!libraryTyping() && libraryTypeAhead() && character > L' ' && character != 127) openLibrarySearch();
    if (libraryTyping()) {
        if (character < L' ' || character == 127 || library_controls_.query.size() >= 70) return;
        library_controls_.query.insert(library_controls_.caret(), 1, character);
        library_query_dirty_ = true;
        library_query_edited_ = std::chrono::steady_clock::now();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!searching() || character < L' ' || character == 127 || search_query_.size() >= 70) return;
    dismissMenu();
    grid_back_focus_ = false; // Typing returns to the field and its results.
    if (screen_ == ScreenMode::store) {
        store_jobs_->generation.fetch_add(1);
        store_loading_ = false;
        store_results_.clear();
        rebuildCards(false);
        store_status_.clear();
    }
    search_query_.insert(searchCaret(), 1, character);
    search_dirty_ = true;
    search_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::eraseSearchCharacter(bool repeated) {
    if (pluginPageTyping()) { pluginPageErase(); return; }
    if (companionTyping()) { companionErase(); return; }
    if (libraryTyping()) {
        // Backspace on an empty field folds it away; a held one only empties it.
        if (library_controls_.query.empty()) { if (!repeated) closeLibrarySearch(); return; }
        const auto caret = library_controls_.caret();
        if (!caret) return; // The insertion point at the start: nothing before it.
        library_controls_.query.erase(caret - 1, 1);
        library_query_dirty_ = true;
        library_query_edited_ = std::chrono::steady_clock::now();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!searching() || search_query_.empty()) { if (!repeated) back(); return; }
    const auto caret = searchCaret();
    if (!caret) return; // The insertion point at the start: nothing before it.
    dismissMenu();
    search_query_.erase(caret - 1, 1);
    if (screen_ == ScreenMode::store) {
        store_jobs_->generation.fetch_add(1);
        store_loading_ = false;
        store_results_.clear();
        rebuildCards(false);
    }
    search_dirty_ = true;
    search_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::updateSearch() {
    search_dirty_ = false;
    versions_ = versions::Index(public_library_);
    grouped_library_ = versions_.collapse(public_library_, library_state_.preferred_versions, library_state_.first_seen);
    search_results_ = searchPublicGames(grouped_library_, search_query_, private_shelf_);
    grid_scroll_ = 0;
    rebuildCards(false);
}

// Details as laid out on screen (ui/game_details_layout.hpp): Back at the top left, the rating,
// the row of buttons, the cards at the bottom. Keys move to the nearest target that way; from a
// card Up returns to the button last left. Action indices only identify the targets.
void Launcher::detailsMove(int dx, int dy) {
    if (cards_.empty()) return;
    const auto view = renderer_.viewport();
    const details_layout::Layout layout{view.width, view.height};
    const auto actions = detailsActions();
    const auto all = layout.items(actions);
    const int current = detail_action_;
    const auto* from = layout.find(all, current);
    int next = !from ? 0 : details_layout::move(all, current, dx, dy); // A target that is gone: the primary button.
    if (from && from->card() && dy < 0) {
        const auto row = layout.buttons(actions);
        if (std::any_of(row.begin(), row.end(), [&](const details_layout::Item& item) { return item.action == detail_column_; })) next = detail_column_;
    }
    if (const auto* to = layout.find(all, next); to && !to->card() && to->kind != details_layout::Kind::back && to->kind != details_layout::Kind::rating)
        detail_column_ = next;
    if (next == current) { navigationBoundary(); return; }
    audio_.navigate();
    detail_action_ = next;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::changeCategory(int amount) {
    if (glanceOpen() && !friends_open_) { glanceMove(0, amount); return; }
    if (companionOpen() && !friends_open_) { companionMove(0, amount); return; }
    if (libraryDialogOpen()) { libraryDialogKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    if (menu_open_ && screen_ == ScreenMode::details) { menuMove(amount); return; }
    if (screen_ == ScreenMode::plugin_page && !friends_open_) { pluginPageMove(amount); return; }
    if (downloadsOpen() && !friends_open_ && !gogInstallOpen()) { downloadsMove(amount); return; }
    if (amount) { nav_direction_ = amount < 0 ? -2 : 2; nav_direction_at_ = GetTickCount64(); }
    if (identifyOpen()) { identifyMove(amount); return; }
    if (gogInstallOpen()) { gogInstallKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    if (customEditorOpen()) { customEditorKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    if (desktopActive() && topDialog() == Modal::none && !friends_open_ && !quick_open_) { desktopMove(0, amount); return; }
    if (libraryVertical(amount)) return;
    if (cropOpen()) { cropMove(0, amount); return; }
    if (devicesOpen()) { deviceMove(amount); return; }
    if (artworkOpen()) { artworkMove(0, amount); return; }
    if (achievementsOpen()) { achievementsMove(amount); return; }
    if (screen_ == ScreenMode::details) { if (std::abs(amount) == 1) detailsMove(0, amount); else navigate(amount, false); return; }
    if (screen_ == ScreenMode::settings) { settingsMove(amount); return; }
    if (gridScreen(screen_)) {
        if (screen_ == ScreenMode::search || screen_ == ScreenMode::store) {
            // Up from the first row: Back at the top left; Down returns to the games.
            if (grid_back_focus_) {
                if (amount > 0 && !cards_.empty()) { grid_back_focus_ = false; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
                else navigationBoundary();
                return;
            }
            if (amount < 0 && (cards_.empty() || selected_ < static_cast<size_t>(libraryColumns()))) {
                grid_back_focus_ = true; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
            }
        }
        const auto before = selected_;
        navigate(amount * libraryColumns(), false);
        if (screen_ == ScreenMode::library && selected_ != before) noteFastScroll();
        return;
    }
    if (screen_ != ScreenMode::shelves) return;
    if (categories_.empty() || pending_launch_ || category_menu_open_) return;
    // The quick bar closes towards the tiles: Down on the other layouts, Up on Metro (opened by Down).
    // PSfive opens it both from the row (Up) and from the buttons (Down): either way closes it.
    if (quick_open_) { if (psfiveActive() || ((metroActive() || snitchActive()) ? amount < 0 : amount > 0)) closeQuick(); return; }
    if (snitchActive()) {
        if (amount > 0) openQuick(); else navigationBoundary();
        return;
    }
    if (xmbActive()) { dismissMenu(); xmbVertical(amount); return; }
    if (flixActive()) { dismissMenu(); flixVertical(amount); return; }
    if (metroActive()) { dismissMenu(); metroMove(0, amount); return; }
    if (psfiveActive()) { dismissMenu(); psfiveVertical(amount); return; }
    if (category_ == 0 && amount < 0) { openQuick(); return; }
    dismissMenu();
    const int next = std::clamp(static_cast<int>(category_) + amount, 0,
                                static_cast<int>(categories_.size()) - 1);
    if (next == static_cast<int>(category_)) { navigationBoundary(); return; }
    audio_.navigate();
    const bool rail_focus = categoryRailActive() && category_rail_focus_;
    showCategory(static_cast<size_t>(next));
    category_rail_focus_ = rail_focus;
}

void Launcher::openQuick() {
    if ((category_ != 0 && !metroActive() && !psfiveActive()) || pending_launch_ || friends_open_ || category_menu_open_) return;
    dismissMenu();
    metro_tabs_ = false;
    quick_selected_ = 0;
    quick_open_ = true;
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeQuick() {
    quick_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::quickMove(int amount) {
    if (!quick_open_) return;
    const int next = std::clamp(quick_selected_ + amount, 0, quick_menu::count(static_cast<int>(plugin_menu_.size())) - 1);
    if (next == quick_selected_) { navigationBoundary(); return; }
    quick_selected_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::quickAccept() {
    if (!quick_open_) return;
    const auto action = quick_menu::action(quick_selected_, static_cast<int>(plugin_menu_.size()));
    closeQuick();
    if (action == quick_menu::Action::search) openSearch();
    else if (action == quick_menu::Action::library) openLibrary();
    else if (action == quick_menu::Action::store) pluginAction(plugin_menu_.at(quick_selected_ - 2));
    else if (action == quick_menu::Action::settings) openSettings();
    else if (action == quick_menu::Action::controllers) openDevices(devices::Panel::controllers);
    else if (action == quick_menu::Action::audio) openDevices(devices::Panel::audio);
    else if (action == quick_menu::Action::downloads) openDownloads();
    else if (action == quick_menu::Action::widgets) toggleFriendsDrawer();
    else openPowerMenu();
}

void Launcher::togglePrivateCategory() {
    if (cropOpen() || devicesOpen() || artworkOpen() || pending_launch_) return;
    if (!private_shelf_ && private_category_.games.empty()) return;
    rememberPlace();
    closeQuick();
    closeCategoryMenu();
    const auto current = categories_.empty() ? std::string{} : categories_[category_].id;
    private_shelf_ = !private_shelf_;
    buildShelves();
    // Shown on the home screen: focus it at the bottom. Hidden: the same category, or the first.
    const std::string wanted = private_shelf_ && screen_ == ScreenMode::shelves ? "private" : current;
    size_t next = 0;
    for (size_t i = 0; i < categories_.size(); ++i) if (categories_[i].id == wanted) next = i;
    const auto shown = [](const Game& game) { return game.visibility == GameVisibility::public_game; };
    // Hiding again leaves the screens that only listed (or show) hidden games.
    const bool leave = !private_shelf_ &&
        ((screen_ == ScreenMode::library && library_category_ == "private") ||
         (screen_ == ScreenMode::details && !detail_games_.empty() && !shown(detail_games_[0])));
    if (screen_ == ScreenMode::details && leave) suspendDetails();
    if (screen_ == ScreenMode::library || screen_ == ScreenMode::platform_games) {
        if (leave) { showCategory(next); return; }
        // Grids stay where they are: the games appear or disappear in place.
        dismissMenu();
        const auto selected = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
        if (screen_ == ScreenMode::platform_games) {
            const auto found = platform_games_.find(nested_platform_);
            if (found == platform_games_.end()) { showCategory(next); return; }
            nested_games_.resize(1); // The back tile.
            nested_games_.insert(nested_games_.end(), found->second.begin(), found->second.end());
        }
        category_ = next;
        rebuildCards(false);
        if (const auto found = findCard(selected, true)) focusCard(*found);
        return;
    }
    if (screen_ == ScreenMode::shelves || leave) {
        dismissMenu();
        showCategory(next);
        if (xmbActive())
            for (size_t i = 0; i < xmb_top_.size(); ++i)
                if (xmb_top_[i].kind == GameKind::category && xmb_top_[i].app_id == next) { xmbFocus(i, -1); break; }
        audio_.drawer();
    } else category_ = next;
}

bool Launcher::categoryRailActive() const {
    return settings_.show_categories && settings_.home_layout == AppSettings::HomeLayout::shelves &&
           screen_ == ScreenMode::shelves && !desktop_mode_;
}

void Launcher::focusCategoryRail(bool focus) {
    if (!categoryRailActive() || pending_launch_) return;
    dismissMenu();
    closeCategoryMenu(); category_menu_progress_ = 0;
    if (category_rail_focus_ != focus) audio_.navigate();
    category_rail_focus_ = focus;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openCategoryMenu() {
    if (categoryRailActive()) { focusCategoryRail(true); return; }
    if (metroActive() || snitchActive()) return; // Metro has tabs; snitch has no categories.
    if (psfiveActive() && categories_.size() <= psfive_layout::visible_tabs) return;
    if (categories_.size() < 2 || pending_launch_) return;
    dismissMenu();
    if (friends_open_) closeFriends();
    category_cursor_ = static_cast<int>(category_);
    if (psfiveActive()) {
        category_cursor_ = std::max(static_cast<int>(psfive_layout::visible_tabs), category_cursor_);
        psfive_action_ = -1;
        psfive_top_ = static_cast<int>(psfive_layout::visible_tabs);
    }
    category_menu_scroll_ = 0;
    ensureCategoryCursorVisible();
    if (psfiveActive()) {
        psfive_menu_motion_.row.snap(static_cast<float>(category_cursor_));
        psfive_menu_motion_.scroll.snap(category_menu_scroll_);
        psfive_menu_motion_.valid = true;
    }
    category_highlight_y_ = category_menu::top(renderer_.viewport().height, category_titles_.size()) +
                            category_cursor_ * category_menu::row_height - category_menu_scroll_;
    category_menu_open_ = true;
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeCategoryMenu() {
    category_menu_open_ = false;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::categoryMenuHorizontal(int amount, bool repeated) {
    if (!category_menu_open_ || !amount) return;
    // PSfive's dropdown belongs to the top bar: leave it for the adjacent tab/icon,
    // without accepting the highlighted category. The side drawer keeps its keyboard shortcuts.
    if (psfiveActive()) navigate(amount);
    else if (amount > 0) categoryMenuAccept();
    else if (!repeated) closeCategoryMenu();
}

void Launcher::categoryMenuMove(int amount) {
    if (!category_menu_open_ || categories_.empty()) return;
    const int first = psfiveActive() ? static_cast<int>(psfive_layout::visible_tabs) : 0;
    if (first >= static_cast<int>(categories_.size())) { closeCategoryMenu(); return; }
    const int next = std::clamp(category_cursor_ + amount, first, static_cast<int>(categories_.size()) - 1);
    if (next == category_cursor_) { navigationBoundary(); return; }
    category_cursor_ = next;
    ensureCategoryCursorVisible();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::ensureCategoryCursorVisible() {
    if (psfiveActive()) {
        const auto layout = renderer_.psfiveMoreMenu(categories_.size());
        const float row = layout.rowTop(category_cursor_, category_menu_scroll_);
        if (row < layout.top()) category_menu_scroll_ += row - layout.top();
        else if (row + layout.row_height > layout.bottom()) category_menu_scroll_ += row + layout.row_height - layout.bottom();
        category_menu_scroll_ = std::clamp(category_menu_scroll_, 0.0f, layout.maxScroll());
        return;
    }
    const float height = renderer_.viewport().height;
    const float row = category_menu::top(height, category_titles_.size()) +
                      category_cursor_ * category_menu::row_height - category_menu_scroll_;
    if (row < 55) category_menu_scroll_ += row - 58;
    else if (row + category_menu::row_height > height - 22)
        category_menu_scroll_ += row + category_menu::row_height - (height - 25);
    category_menu_scroll_ = std::clamp(category_menu_scroll_, 0.0f,
                             category_menu::maxScroll(height, category_titles_.size()));
}

void Launcher::scrollCategoryMenu(int amount) {
    if (!category_menu_open_) return;
    if (psfiveActive()) {
        const auto layout = renderer_.psfiveMoreMenu(categories_.size());
        category_menu_scroll_ = std::clamp(category_menu_scroll_ + amount * layout.row_height, 0.0f, layout.maxScroll());
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const float height = renderer_.viewport().height;
    category_menu_scroll_ = std::clamp(category_menu_scroll_ + amount * category_menu::row_height,
                                      0.0f, category_menu::maxScroll(height, category_titles_.size()));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::categoryMenuAccept() {
    if (!category_menu_open_) return;
    const auto next = static_cast<size_t>(category_cursor_);
    closeCategoryMenu();
    if (next >= categories_.size()) return;
    if (psfiveActive()) psfiveFocusTop(-1);
    if (next == category_) return;
    audio_.navigate();
    if (flixActive()) { // Every category is a row already: scroll to it.
        const auto row = std::find_if(flix_rows_.begin(), flix_rows_.end(), [&](const FlixRow& item) { return item.category == next; });
        if (row != flix_rows_.end()) flixFocus(static_cast<size_t>(row - flix_rows_.begin()), categories_[next].selected);
        return;
    }
    if (metroActive()) { // Every category is a page already: slide to it.
        const auto page = std::find_if(metro_pages_.begin(), metro_pages_.end(), [&](const MetroPage& item) { return item.category == next; });
        if (page != metro_pages_.end()) metroFocus(page->begin + std::min(categories_[next].selected, page->count - 1));
        return;
    }
    showCategory(next);
}

bool Launcher::overCategoryMenu(float mouse_x) const {
    return mouse_x / renderer_.viewport().scale <= 350.0f;
}

void Launcher::enterDown(bool repeated) {
    if (libraryDialogOpen()) { libraryDialogKey(VK_RETURN, repeated); return; }
    if (identifyOpen()) { if (!repeated) identifyAccept(); return; }
    if (gogInstallOpen()) { gogInstallKey(VK_RETURN, repeated); return; }
    if (customEditorOpen()) { customEditorKey(VK_RETURN, repeated); return; }
    if (repeated || pending_launch_) return;
    if (friends_open_) { if (!dashboardPress(1, true, GetTickCount64())) friendsAccept(); return; } // The drawer overlays every screen.
    if (glanceOpen()) { glanceAccept(); return; }
    if (companionOpen()) { companionAccept(); return; }
    if (screen_ == ScreenMode::plugin_page) { pluginPageAccept(); return; }
    if (downloadsOpen()) { downloadsAccept(); return; }
    if (searchBackFocused()) { back(); return; }
    if (libraryAccept()) return;
    if (cropOpen()) { cropAccept(); return; }
    if (devicesOpen()) { deviceAccept(); return; }
    if (artworkOpen()) { artworkAccept(); return; }
    if (quick_open_) { quickAccept(); return; }
    if (screen_ == ScreenMode::settings) { settingsActivate(); return; }
    if (screen_ == ScreenMode::details) { if (menu_open_) menuAccept(); else launch(); return; }
    if (category_menu_open_) { categoryMenuAccept(); return; }
    if (enter_held_) return; // Ignore Windows auto-repeat while holding Enter.
    if (menu_open_) { menuAccept(); return; }
    if (categoryRailActive() && category_rail_focus_) { focusCategoryRail(false); return; }
    if (psfiveActions()) { psfiveActivate(); return; } // "Graj" / "…" act at once, no hold.
    if (psfiveTopFocused()) { psfiveTopActivate(); return; } // The top bar acts immediately too.
    if (cards_.empty()) return;
    if (cards_[selected_].game.kind == GameKind::platform ||
        cards_[selected_].game.kind == GameKind::pc_platform ||
        uiTile(cards_[selected_].game)) { launch(); return; }
    if (!isGame(cards_[selected_].game)) return;
    enter_held_ = true;
    enter_started_ = std::chrono::steady_clock::now();
}

void Launcher::enterUp() {
    if (dashboardPress(1, false, GetTickCount64())) return;
    if (!enter_held_) return;
    enter_held_ = false;
    const float held = std::chrono::duration<float>(std::chrono::steady_clock::now() - enter_started_).count();
    if (menu_open_) return;
    if (held >= enter_hold_seconds) {
        openContextMenu();
    } else accept();
}

void Launcher::cancelEnterHold() { enter_held_ = pad_enter_hold_ = false; dashboardCancelPress(); }

// A press on a game card that may become a hold for its menu (Enter, A on the pad): the
// cases accept() itself would hand to a dialog, panel or non-game tile act at once instead.
bool Launcher::acceptHoldsForMenu() const {
    if (glanceOpen() || companionOpen()) return false;
    if (pending_launch_ || menu_open_ || identifyOpen() || screen_ == ScreenMode::plugin_page || screen_ == ScreenMode::details || downloadsOpen() ||
        cropOpen() || devicesOpen() || artworkOpen() || (categoryRailActive() && category_rail_focus_) || metroTabsFocused() || psfiveActions() || psfiveTopFocused() ||
        (libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() || library_controls_.action >= 0)) ||
        searchBackFocused() || cards_.empty() || selected_ >= cards_.size()) return false;
    const auto& game = cards_[selected_].game;
    return isGame(game) && game.kind != GameKind::platform && game.kind != GameKind::pc_platform && !uiTile(game);
}

void Launcher::openContextMenu() {
    if (desktopActive()) {
        // The pad's menu button or a held Enter: the game's menu beside its poster.
        if (pending_launch_ || friends_open_ || quick_open_ || topModal() != Modal::none || cards_.empty()) return;
        if (desktop_menu_) { desktopCloseMenu(); return; }
        const auto poster = desktopLayout().poster(selected_, desktop_scroll_);
        desktopOpenGameMenu(poster.right - 14, poster.top + 18, true);
        return;
    }
    if (categoryRailActive() && category_rail_focus_) return;
    if (metroTabsFocused() || psfiveTopFocused()) return; // The tab strip / top bar has no game.
    // A dialog above the screen (Zidentyfikuj grę, a collection, the file picker) owns the input: a
    // menu opened beneath it would take D-pad and A while hidden (Ukryj, Wyklucz, Zamknij grę).
    // (The walkthrough opens it on purpose to show it.)
    if (topDialog() != Modal::none || exit_prompt_ || power_menu_) return;
    if (pending_launch_ || friends_open_ || category_menu_open_ || quick_open_ || cards_.empty() ||
        (screen_ != ScreenMode::shelves && screen_ != ScreenMode::platform_games && screen_ != ScreenMode::details && !gridScreen(screen_)) ||
        (libraryControls() && (library_controls_.focus >= 0 || library_controls_.open())) || searchBackFocused() || !isGame(cards_[selected_].game)) return;
    if (menu_open_) { dismissMenu(); return; }
    cancelEnterHold(); menu_open_ = true; menu_selected_ = 0;
    library_menu_from_action_ = psfiveActions(); // PSfive buttons: the menu unfolds from "…".
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::contextClick(float mouse_x, float mouse_y) {
    desktopEmbedMap(mouse_x, mouse_y); // Urządzenia, kadrowanie, plugin pages in a desktop sheet.
    if (desktopActive() && topModal() == Modal::none && !friends_open_ && !quick_open_ && !pending_launch_) {
        desktopContextClick(mouse_x, mouse_y);
        return;
    }
    if (libraryDialogOpen()) return;
    if (gogInstallOpen()) return;
    if (pending_launch_ || friends_open_ || category_menu_open_ || quick_open_ || exit_prompt_ || power_menu_ || tour_.open ||
        (screen_ != ScreenMode::shelves && screen_ != ScreenMode::platform_games && !gridScreen(screen_))) return;
    const auto view = renderer_.viewport(); const float x = mouse_x / view.scale, y = mouse_y / view.scale;
    for (size_t i = 0; i < cards_.size(); ++i) {
        if (!isGame(cards_[i].game) || !cardHit(i, x, y)) continue;
        library_controls_.focus = -1; closeLibraryPanel();
        dismissMenu(); focusCard(i); openContextMenu(); return;
    }
    // Beside the games: the launcher's own menu (Szukaj, Ustawienia, Zasilanie...), as Guide/Home
    // opens it; with only a mouse there is no other way to it in most home layouts.
    if (menu_open_) { dismissMenu(); return; }
    openGuideMenu();
}

void Launcher::menuMove(int amount) {
    if (!menu_open_) return;
    const int next = std::clamp(menu_selected_ + amount, 0, context_menu::rows(menuRows()) - 1);
    if (next == menu_selected_) { navigationBoundary(); return; }
    menu_selected_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::menuAccept() {
    if (!menu_open_) return;
    using context_menu::Item;
    const auto item = context_menu::item(menu_selected_, menuRows());
    if (item == Item::primary) {
        dismissMenu();
        if (!cards_.empty() && cards_[selected_].game.kind == GameKind::catalog) catalogAction(cards_[selected_].game);
        else launch();
        return;
    }
    if (cards_.empty()) return;
    const auto game = cards_[selected_].game;
    switch (item) {
    case Item::suspend: dismissMenu(); freezeGame(gameKey(game), true); break;
    case Item::close: dismissMenu(); closeGame(game); break;
    case Item::plugin: {
        const auto actions = pluginContext(game);
        const int index = menu_selected_ - context_menu::row(Item::plugin, menuRows());
        if (index >= 0 && index < static_cast<int>(actions.size())) pluginAction(actions[index], game);
        break;
    }
    case Item::details: openDetails(game); break;
    case Item::retry: retryGameArtwork(game); dismissMenu(); break;
    case Item::folder:
        dismissMenu();
        if (const auto folder = gameFolder(game); !folder.empty())
            if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        break;
    case Item::hide: dismissMenu(); toggleHidden(game); break;
    case Item::exclude: excludeGame(game); break;
    case Item::uninstall: uninstallGame(game); break;
    case Item::launch: dismissMenu(); openLaunchEditor(game); break;
    case Item::metadata: dismissMenu(); if (desktop_mode_) desktopMetaOpen(game); else openMetadata(game); break;
    case Item::aurelia: dismissMenu(); openAurelia(&game); break;
    case Item::steam_account: cycleSteamAccount(game); break; // The menu stays: the row shows the next choice.
    default: openArtworkBrowser(game); break;
    }
}

void Launcher::dismissMenu() {
    enter_held_ = false;
    menu_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::accept() {
    if (libraryDialogOpen()) { collectionDialogAccept(); return; }
    if (desktopActive() && topDialog() == Modal::none && !friends_open_ && !quick_open_) { desktopAccept(); return; }
    if (searchBackFocused() && !friends_open_ && !menu_open_) { back(); return; }
    if (metroTabsFocused()) { launch(); return; } // Enter/A on the tabs: back to the page's tile.
    if (psfiveAccept()) return; // PSfive: a game goes down to "Graj" / "…", a button acts.
    // Only a plain pick of a game card; dialogs, panels, menus, tiles, the Store (always
    // details), the details screen and the Detailed view's buttons keep launch()'s handling.
    const bool details = settings_.accept_action == AppSettings::AcceptAction::details &&
        !launchAnimationActive() && !pending_launch_ && !friends_open_ && !category_menu_open_ && !quick_open_ && !menu_open_ &&
        screen_ != ScreenMode::plugin_page && !identifyOpen() && !gogInstallOpen() && !customEditorOpen() && !cropOpen() &&
        !devicesOpen() && !artworkOpen() && !achievementsOpen() && !(categoryRailActive() && category_rail_focus_) &&
        (screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games || screen_ == ScreenMode::library || screen_ == ScreenMode::search) &&
        !(libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() || library_controls_.action >= 0)) &&
        selected_ < cards_.size() && isGame(cards_[selected_].game) && cards_[selected_].game.kind != GameKind::catalog;
    if (details) openDetails(cards_[selected_].game); else launch();
}

void Launcher::launch() {
    if (glanceOpen() && !friends_open_) { glanceAccept(); return; }
    if (companionOpen() && !friends_open_) { companionAccept(); return; }
    if (libraryDialogOpen()) { collectionDialogAccept(); return; }
    if (menu_open_ && screen_ == ScreenMode::details) { menuAccept(); return; }
    if (launchAnimationActive()) return;
    if (screen_ == ScreenMode::plugin_page && !friends_open_) { pluginPageAccept(); return; }
    if (identifyOpen()) { identifyAccept(); return; }
    if (downloadsOpen() && !friends_open_ && !gogInstallOpen()) { downloadsAccept(); return; }
    if (gogInstallOpen()) { gogInstallAccept(); return; }
    if (searchBackFocused() && !friends_open_ && !menu_open_) { back(); return; }
    if (customEditorOpen()) { customEditorAccept(); return; }
    if (libraryAccept()) return;
    if (cropOpen()) { cropAccept(); return; }
    if (devicesOpen()) { deviceAccept(); return; }
    if (artworkOpen()) { artworkAccept(); return; }
    if (categoryRailActive() && category_rail_focus_ && !friends_open_ && !quick_open_) { focusCategoryRail(false); return; }
    if (metroTabsFocused() && !friends_open_ && !quick_open_ && !pending_launch_) { metroFocusTabs(false); audio_.navigate(); return; }
    // No games: Accept presses the empty home's focused button (Dodaj grę, Platformy, Odśwież).
    if (emptyHomeActive()) { emptyHomeActivate(empty_focus_); return; }
    if (cards_.empty() || pending_launch_ || friends_open_ || category_menu_open_ || quick_open_) return;
    if (achievementsOpen()) { // Back above the list closes it; an achievement opens its card, which closes again.
        if (achievements_.card) closeAchievementCard();
        else if (achievements_.selected < 0) closeAchievements();
        else openAchievementCard(achievements_.selected);
        return;
    }
    const auto& active = cards_[selected_].game;
    if (screen_ == ScreenMode::store) { openDetails(active); return; }
    if (screen_ == ScreenMode::details && detail_action_ == -1) { back(); return; }
    if (screen_ == ScreenMode::details && detail_action_ >= 0 && detail_action_ == closeDetailAction()) { const auto game = active; detail_action_ = 0; closeGame(game); return; } // ✕ beside Wróć.
    if (screen_ == ScreenMode::details && detail_action_ >= 0 && detail_action_ == cloudDetailAction()) {
        const auto game = active; openAurelia(&game); aureliaPage(aurelia_ui::Page::cloud); return;
    }
    if (screen_ == ScreenMode::details && detail_action_ == pluginDetailAction()) { openPluginDetail(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == favoriteDetailAction()) { toggleFavorite(active); return; }
    if (screen_ == ScreenMode::details && detail_action_ == collectionsDetailAction()) { openCollectionManager(active); return; }
    if (screen_ == ScreenMode::details && detail_action_ == versionsDetailAction()) { openVersions(active, false); return; }
    if (screen_ == ScreenMode::details && detail_action_ == identifyDetailAction()) { openIdentify(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == ratingDetailAction()) { openRatingSource(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == achievementsDetailAction()) { openAchievements(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == 1) { openContextMenu(); return; }
    if (screen_ == ScreenMode::details && active.kind == GameKind::catalog) { catalogAction(active); return; }
    if (active.kind == GameKind::all) { openLibrary(); return; }
    if (active.kind == GameKind::back) { back(); return; }
    if (active.kind == GameKind::settings) { openSettings(); return; }
    // Metro and XMB: Glance and Towarzyszka are tiles of the layout.
    if (active.kind == GameKind::glance) { openGlance(); return; }
    if (active.kind == GameKind::companion) { openCompanion(); return; }
    // XMB collection/console tiles open their column below instead of a separate screen.
    if (xmbActive() && selected_ < xmb_top_.size() && xmbColumn(active)) { xmbVertical(1); return; }
    if (active.kind == GameKind::category && (flixActive() || shelfAllTile() || psfiveActive())) { openCategoryLibrary(active.source_id); return; }
    if (metroActivate()) return; // Metro: store/console shortcuts slide to their page, "Wszystkie" opens the library.
    if (active.kind == GameKind::category) return;
    if (active.kind == GameKind::platform) { openPlatform(active.platform); return; }
    if (active.kind == GameKind::pc_platform) { openPcLibrary(active.source); return; }
    if (!active.version_group.empty() && !(screen_ == ScreenMode::details && detail_version_selected_)) {
        if (const auto pref = library_state_.preferred_versions.find(active.version_group); pref != library_state_.preferred_versions.end())
            for (const auto& copy : public_library_)
                if (const auto group = versions_.by_game.find(gameKey(copy)); gameKey(copy) == pref->second &&
                    group != versions_.by_game.end() && group->second == active.version_group && actionEnabled(copy)) { launchGame(copy); return; }
        openVersions(active, true); return;
    }
    launchGame(active);
}

void Launcher::launchGame(const Game& original, bool via_steam) {
    Game active = original; active.version_group.clear();
    if (pending_launch_ || launchAnimationActive() || !isGame(active)) return;
    if (switchToGame(active)) { focusPlayedGame(gameKey(active)); return; }
    if (aurelia_active_ && aurelia::validGame(active) && (aurelia_operations_.contains(active.app_id) || aurelia_playing_.contains(active.app_id))) {
        showNotification(NotificationKind::error, L"Aurelia", tr(L"Operacja dla tej gry już trwa")); return;
    }
    // A game Aurelia manages is installed and launched through it; any other Steam game, or one
    // whose launch through Aurelia was refused, keeps the Steam client (below).
    if (!via_steam && (aureliaManaged(active) || (aurelia_active_ && aurelia::validGame(active) && aurelia_downloads_.contains(active.app_id)))) {
        aureliaLaunch(active); return;
    }
    if (active.source == GameSource::gog && !active.installed && !gog_state_.account.empty() &&
        (!active.account_id.empty() || active.installation.active())) { openGogInstall(active); return; }
    if (active.source == GameSource::epic && !active.installed && !epic_state_.account.empty() &&
        (!active.account_id.empty() || active.installation.active())) { openEpicInstall(active); return; }
    if (active.source == GameSource::epic && active.epic_managed) { startEpicGame(active); return; }
    // Amazon Games: an account title is installed through Nile; an installed one starts from its fuel.json.
    if (active.source == GameSource::amazon && !active.installed && amazonConnected() &&
        (!active.account_id.empty() || active.installation.active())) { openAmazonInstall(active); return; }
    // A Steam game to install: the library picked in Launchme's dialog (src/app/steam_install.cpp).
    if (steamInstallDialogWanted(active) && openSteamInstall(active)) return;
    const auto action = primaryGameAction(active);
    if (!action) {
        if (localFileGame(active.source))
            showNotification(NotificationKind::error, tr(L"Nie znaleziono pliku gry"), active.play_action.target);
        else showNotification(NotificationKind::error, tr(L"Brak akcji dla tej gry"),
                              std::wstring(tr(L"Sprawdź konfigurację ")) + sourceName(active.source));
        return;
    }
    // A Steam game another remembered account installed: Steam restarts signed in to it first.
    if (action.kind == ActionKind::play && !beginSteamAccountSwitch(active)) return;
    if (settings_.launch_animation && action.kind == ActionKind::play) startLaunchAnimation(active);
    audio_.launch();
    if (action.kind == ActionKind::play) audio_.stopMusic();
    pending_game_ = active;
    pending_launch_ = true;
    launch_age_ = 0;
    dismissMenu();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::finishLaunch() {
    auto action = primaryGameAction(pending_game_);
    pending_launch_ = false;
    // "Opcje uruchamiania…": a Steam game's own options go through steam.exe -applaunch.
    if (const auto options = launchArguments(pending_game_); !options.empty() && action.kind == ActionKind::play)
        action = steamOptionsAction(std::move(action), steam_executable_(), pending_game_.app_id, options, settings_.steam_silent_launch);
    if (settings_.steam_silent_launch && pending_game_.source == GameSource::steam && action.kind == ActionKind::play)
        action = silentSteamAction(std::move(action), steam_executable_());
    auto result = execute_(hwnd_, action);
    if (result <= 32 && result != launch_error::cancelled && action.keep_focus) {
        // steam.exe refused (moved, blocked): the plain steam:// URL still starts the game.
        action = primaryGameAction(pending_game_);
        result = execute_(hwnd_, action);
    }
    // The started program's process, when Windows reports one (a link handed to a running
    // client has none): its process tree follows the session (scripts, .lnk, launcher stubs).
    std::shared_ptr<void> job;
    if (const HANDLE process = execute_ == executeGameAction ? takeLaunchedProcess() : nullptr) {
        if (result > 32 && action.kind == ActionKind::play)
            if (const HANDLE created = launchJob(process)) job = std::shared_ptr<void>(created, CloseHandle);
        CloseHandle(process);
    }
    if (result <= 32) {
        stopLaunchAnimation();
        if (!anyActiveSession()) audio_.resumeMusic();
        if (result == launch_error::cancelled) return; // The user said no (UAC, the client's own dialog).
        const bool file = std::filesystem::path(action.target).has_root_path() && action.target.find(L"://") == std::wstring::npos;
        const auto title = action.kind == ActionKind::open_client ? tr(L"Nie udało się otworzyć klienta") :
                           action.kind == ActionKind::install ? tr(L"Nie udało się rozpocząć instalacji") : tr(L"Nie udało się uruchomić gry");
        std::wstring detail;
        if (result == launch_error::no_handler && !file)
            detail = std::wstring(sourceName(pending_game_.source)) + L": " + tr(L"klient nie jest zainstalowany");
        else if ((result == launch_error::file_missing || result == launch_error::path_missing) && file)
            detail = std::wstring(tr(L"Brak pliku: ")) + action.target;
        else if (result == launch_error::denied && file)
            detail = std::wstring(tr(L"Brak dostępu: ")) + action.target;
        else if (file || localFileGame(pending_game_.source)) detail = action.target; // A file on disk: show which one.
        else detail = std::wstring(tr(L"Sprawdź, czy ")) + sourceName(pending_game_.source) + tr(L" jest dostępny");
        showNotification(NotificationKind::error, title, pending_game_.name + L" · " + detail);
    } else if (action.kind == ActionKind::play) {
        // Silent Steam: nobody else may take the foreground, so wait longer for the game's window.
        beginGameSession(pending_game_, action.keep_focus ? silent_focus_ms : default_focus_ms, std::move(job));
        showNotification(NotificationKind::launch, pending_game_.name, tr(L"Uruchamianie…")); // Clients and updates can take a while.
        if (settings_.quick_resume) freezeOthers(gameKey(pending_game_));
        // On return the started game waits at the front of Recent, focused; under the
        // cover animation the view behind it changes only when the animation ends.
        if (launchAnimationActive()) launch_played_key_ = gameKey(pending_game_);
        else focusPlayedGame(gameKey(pending_game_));
    }
}

void Launcher::startLaunchAnimation(const Game& game) {
    if (!settings_.launch_animation) return;
    launch_animation_title_ = game.name;
    launch_artwork_key_ = gameKey(game);
    launch_animation_started_ = std::chrono::steady_clock::now();
    launch_animation_ending_.reset();
    launch_animation_rested_ = false;
    launch_progress_ = 0;
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu();
    cancelEnterHold(); cancelTabHold();
    // Its background and logo on the focused card (the picture uses them); missing ones are asked for.
    if (selected_ < cards_.size() && gameKey(cards_[selected_].game) == launch_artwork_key_) {
        if (const auto* art = art_.find(launch_artwork_key_)) {
            if (art->hero) renderer_.setHero(selected_, art->hero, art->hero_tone, false, art->hero_focus);
            if (art->logo) renderer_.setLogo(selected_, art->logo);
        }
        requestArt(game, CoverMessage::Kind::hero);
        requestArt(game, CoverMessage::Kind::logo);
    }
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd_) SetCursor(nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::stopLaunchAnimation() {
    if (!launchAnimationActive()) return;
    launch_animation_started_.reset();
    launch_animation_ending_.reset();
    launch_animation_rested_ = false;
    launch_animation_title_.clear();
    launch_artwork_key_.clear();
    launch_played_key_.clear();
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd_) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The picture stays until the game is there (src/ui/launch_animation.hpp):
// - another window in front (the game, its launcher): it rests, still, behind it;
// - the launcher seen again after that while the game runs (Alt+Tab, the Guide's home): it fades out;
// - in front all along: at least `seconds`, longer while the game still starts or its window is
//   being brought forward, never beyond `longest`;
// - the game's session ending (or failing to start) ends it at once (onGameActivity).
void Launcher::tickLaunchAnimation(std::chrono::steady_clock::time_point now) {
    if (!launchAnimationActive()) return;
    if (launch_animation_ending_) {
        if (now - *launch_animation_ending_ >= std::chrono::duration<float>(launch_animation::fade_out)) endLaunchAnimation();
        return;
    }
    if (pending_launch_) return; // Waiting for a Steam account switch: the game is not started yet.
    const float age = std::chrono::duration<float>(now - *launch_animation_started_).count();
    if (const HWND foreground = overlay_backend_.foreground(); foreground != hwnd_) {
        if (foreground) launch_animation_rested_ = true; // No foreground at all is a moment between two windows.
        return;
    }
    const auto session = game_sessions_.find(launch_artwork_key_);
    const bool running = session != game_sessions_.end() && session->second.running();
    const bool starting = session != game_sessions_.end() && !running;
    if (launch_animation_rested_ && running) { fadeLaunchAnimation(now); return; }
    if (age < launch_animation::seconds) return;
    if (age < launch_animation::longest && (starting || (running && launch_focus_key_ == launch_artwork_key_))) return;
    fadeLaunchAnimation(now);
}

void Launcher::fadeLaunchAnimation(std::chrono::steady_clock::time_point now) {
    if (!launchAnimationActive() || launch_animation_ending_) return;
    launch_animation_ending_ = now;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Behind the game the picture is still: no frames until it is seen again (or fades out).
bool Launcher::launchAnimationMoving(std::chrono::steady_clock::time_point now) const {
    if (!launchAnimationActive()) return false;
    if (launch_animation_ending_) return true;
    if (overlay_backend_.foreground() == hwnd_) return true;
    return !launch_animation_rested_ && !launch_animation::still(std::chrono::duration<float>(now - *launch_animation_started_).count());
}

// Back (Esc, B, the mouse's back button) hides the picture early; the press that started the game
// (and its repeats) does not.
bool Launcher::dismissLaunchAnimation(std::chrono::steady_clock::time_point now) {
    if (!launchAnimationActive() || now - *launch_animation_started_ < std::chrono::duration<float>(launch_animation::dismiss_after)) return false;
    endLaunchAnimation();
    return true;
}

void Launcher::endLaunchAnimation() {
    const auto played = std::move(launch_played_key_);
    stopLaunchAnimation();
    if (screen_ == ScreenMode::details) {
        store_jobs_->generation.fetch_add(1);
        details_loading_ = false;
    }
    if (screen_ != ScreenMode::shelves) showCategory(category_);
    focusPlayedGame(played);
}

// ---- The home without games (src/ui/empty_library.hpp) ---------------------------------------

empty_library::Reason Launcher::emptyLibraryReason() const {
    using empty_library::Reason;
    if (libraryLoading()) return Reason::loading;
    // No Steam account: either no Steam on this PC at all, or Steam with nobody signed in.
    if (!account_) return steam_executable_ && !steam_executable_().empty() ? Reason::no_account : Reason::no_steam;
    return Reason::empty;
}

bool Launcher::emptyHomeActive() const {
    return screen_ == ScreenMode::shelves && cards_.empty() && !libraryLoading() && !desktopActive() && !pending_launch_ &&
        !quick_open_ && !friends_open_ && !menu_open_ && !category_menu_open_ && !category_rail_focus_ && topModal() == Modal::none &&
        !keyboard_.open && !closing_;
}

empty_library::Layout Launcher::emptyHomeLayout() const {
    const auto view = renderer_.viewport();
    return {view.width, view.height, categoryRailActive() ? category_rail::Layout{view.width, view.height}.right() : 0.0f};
}

void Launcher::emptyHomeMove(int amount) {
    const int next = std::clamp(empty_focus_ + amount, 0, empty_library::action_count - 1);
    if (next == empty_focus_) { navigationBoundary(); return; }
    empty_focus_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Dodaj grę: the editor of a program, shortcut or script; Platformy: Ustawienia → Biblioteka with its
// stores; Odśwież: every library scanned again (Steam signed in meanwhile, a store installed).
void Launcher::emptyHomeActivate(int action) {
    empty_focus_ = std::clamp(action, 0, empty_library::action_count - 1);
    audio_.context();
    switch (empty_focus_) {
    case empty_library::add_game: openCustomEditor({}); break;
    case empty_library::platforms:
        openSettings();
        settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        buildSettingsRows();
        settingsPreviewFocus();
        break;
    default: desktopRefreshLibraries(); break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::emptyHomeClick(float x, float y, bool double_click) {
    if (!emptyHomeActive()) return false;
    const int action = emptyHomeLayout().hit(x, y);
    if (action < 0) return false;
    if (!double_click) emptyHomeActivate(action);
    return true;
}

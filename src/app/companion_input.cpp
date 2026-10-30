// Her chat under the pad, keys and mouse: focus moves, accept, clicks and hover, game links and
// videos, and the "..." menu.
#include "launcher.hpp"
#include "companion_app_internal.hpp"
#include "../net/companion_links.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>

using namespace companion_app;

void Launcher::companionMove(int dx, int dy) {
    companion_.link_pointer.reset();
    auto& chat = companion_.chat;
    if (chat.feed) { companionFeedMove(dx, dy); return; }
    if (chat.viewer) { navigationBoundary(); return; } // Her enlarged photo: only closing it.
    if (chat.mind) {
        if (chat.mind_confirm) {
            const int step = dx ? dx : dy;
            const int next = std::clamp(chat.mind_confirm_focus + step, companion_ui::confirm_reset, companion_ui::confirm_cancel);
            if (!step || next == chat.mind_confirm_focus) navigationBoundary();
            else { chat.mind_confirm_focus = next; audio_.navigate(); }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto nodes = companionMindNodes();
        const int count = static_cast<int>(nodes.size());
        const int step = dx ? dx : dy;
        if (!step || count <= 0) { navigationBoundary(); return; }
        int next = chat.mind_selected;
        bool found = false;
        for (int i = 0; i < count; ++i) {
            next = (next + (step > 0 ? 1 : -1) + count) % count;
            if (nodes[static_cast<size_t>(next)].matches_filter) { found = true; break; }
        }
        if (!found) { navigationBoundary(); return; }
        chat.mind_selected = next;
        const auto& node = nodes[static_cast<size_t>(chat.mind_selected)];
        if (std::abs(node.x) > 0.01f || std::abs(node.z) > 0.01f) chat.mind_yaw = -std::atan2(node.x, -node.z);
        chat.mind_pitch = std::clamp(std::asin(std::clamp(node.y, -0.95f, 0.95f)) * 0.55f, -0.65f, 0.65f);
        static constexpr std::array<companion::Emotion, 6> node_emotions{
            companion::Emotion::smug, companion::Emotion::playful, companion::Emotion::thinking,
            companion::Emotion::smile, companion::Emotion::excited, companion::Emotion::gaming};
        companion_.rig.emote(node.emotion.value_or(node_emotions[static_cast<size_t>(node.kind) % node_emotions.size()]));
        audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    chat.hover = companion_ui::no_hit;
    if (chat.confirm) { // The reset dialog: Left/Right (or Up/Down) between its two buttons.
        const int step = dx ? dx : dy;
        const int next = std::clamp(chat.confirm_focus + step, companion_ui::confirm_reset, companion_ui::confirm_cancel);
        if (!step || next == chat.confirm_focus) navigationBoundary();
        else { chat.confirm_focus = next; audio_.navigate(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (chat.menu) { // Up/Down through the menu's rows; her costume tiles also Left/Right, a row of tiles per Up/Down.
        const int count = static_cast<int>(chat.menu_items.size());
        int next = chat.menu_focus;
        if (companion_ui::Layout::wardrobe(chat)) {
            const int columns = companion_ui::Layout::wardrobeColumns(chat);
            const int moved = chat.menu_focus + (dx ? dx : dy * columns);
            const bool same_row = !dx || moved / columns == chat.menu_focus / columns;
            if (moved >= 0 && moved < count && same_row) next = moved;
        } else next = std::clamp(chat.menu_focus + dy, 0, std::max(0, count - 1));
        if ((!dx && !dy) || next == chat.menu_focus) navigationBoundary();
        else { chat.menu_focus = next; audio_.navigate(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (chat.archive) { navigationBoundary(); return; } // A history page: only Back (and scrolling).
    const auto view = renderer_.viewport();
    const companion_ui::Layout layout{view.width, view.height};
    const int count = static_cast<int>(chat.chips.size());
    const int old = chat.focus;
    if (dy) {
        // Rows from the top: Back with "…", the chip rows, the field with Send.
        const auto rects = layout.chips(chat);
        const int rows = count ? layout.chipRow(chat, count - 1) + 1 : 0;
        const int row = old == companion_ui::focus_back || old == companion_ui::focus_more || old == companion_ui::focus_feed ? -1 : old >= 0 ? layout.chipRow(chat, old) : rows;
        const int next = std::clamp(row + dy, -1, rows);
        if (next != row) {
            if (next == -1) {
                // The top row's button on that side.
                const float x = old >= 0 && old < count ? (rects[old].left + rects[old].right) * 0.5f : old == companion_ui::focus_send ? layout.send().left : layout.input().left;
                chat.focus = x > (layout.left() + layout.right()) * 0.5f ? companion_ui::focus_more : companion_ui::focus_back;
            }
            else if (next == rows) chat.focus = companion_ui::focus_input;
            else {
                // The chip of that row nearest to the current horizontal position.
                const float x = old >= 0 && old < count ? (rects[old].left + rects[old].right) * 0.5f :
                                old == companion_ui::focus_send ? layout.send().left : layout.input().left + 60;
                int best = -1; float distance = 1e9f;
                for (int i = 0; i < count; ++i) if (layout.chipRow(chat, i) == next) {
                    const float d = std::abs((rects[i].left + rects[i].right) * 0.5f - x);
                    if (d < distance) { distance = d; best = i; }
                }
                if (best >= 0) chat.focus = best;
            }
        }
    } else if (dx) {
        if (old >= 0) {
            const int next = old + dx;
            if (next >= 0 && next < count && layout.chipRow(chat, next) == layout.chipRow(chat, old)) chat.focus = next;
        } else if (old == companion_ui::focus_input && dx > 0) chat.focus = companion_ui::focus_send;
        else if (old == companion_ui::focus_send && dx < 0) chat.focus = companion_ui::focus_input;
        else if (old == companion_ui::focus_back && dx > 0) chat.focus = companion_ui::focus_feed;
        else if (old == companion_ui::focus_feed) chat.focus = dx > 0 ? companion_ui::focus_more : companion_ui::focus_back;
        else if (old == companion_ui::focus_more && dx < 0) chat.focus = companion_ui::focus_feed;
    }
    if (old == chat.focus) navigationBoundary(); else audio_.navigate();
    if (keyboard_.open && chat.focus != companion_ui::focus_input) closeKeyboard();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionAccept() {
    auto& chat = companion_.chat;
    if (chat.feed) {
        auto& feed = companion_.feed.view;
        if (!feed.ready()) return;
        if (!feed.thread.empty()) companionFeedComment();
        else {
            const int index = companion_ui::FeedLayout::visible(feed.archive, feed.motion.value + 160, feed.thread).first;
            companionFeedAction(index * 3 + feed.focus);
        }
        return;
    }
    if (chat.viewer) { companionViewer(false); return; } // Enter or A closes her enlarged photo.
    if (chat.mind && chat.mind_confirm) { companionMindDeletePick(chat.mind_confirm_focus == companion_ui::confirm_reset); return; }
    if (chat.mind) { // Enter or A shows (or masks) a selected hidden thought, else closes the 3D mind screen.
        const auto nodes = companionMindNodes();
        const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        if (!nodes.empty() && nodes[static_cast<size_t>(idx)].revealable) companionMindReveal();
        else companionMind(false);
        return;
    }
    if (chat.confirm) { companionResetPick(chat.confirm_focus == companion_ui::confirm_reset); return; }
    if (chat.menu) { companionMenuPick(chat.menu_focus); return; }
    if (chat.focus == companion_ui::focus_back) { back(); return; }
    if (chat.archive) { navigationBoundary(); return; }
    if (chat.focus == companion_ui::focus_more) { companionMenu(true); return; }
    if (chat.focus == companion_ui::focus_feed) { companionFeed(true); return; }
    if (chat.ended && chat.focus == companion_ui::focus_send) { companionRestart(); return; } // "Nowa rozmowa".
    if (chat.ended && chat.focus == companion_ui::focus_input) { navigationBoundary(); return; }
    if (chat.focus == companion_ui::focus_input) {
        if (!chat.input.empty()) { companionSend(chat.input); return; }
        if (pad_input_ && !keyboard_.open) openKeyboard(); // A on the empty field: the on-screen keyboard.
        return;
    }
    if (chat.focus == companion_ui::focus_send) { companionSend(chat.input); return; }
    if (chat.focus < 0 || chat.focus >= static_cast<int>(chat.chips.size())) return;
    const auto chip = chat.chips[static_cast<size_t>(chat.focus)];
    switch (chip.kind) {
    case companion_ui::Chip::Kind::game: {
        const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == chip.game; });
        if (found == public_library_.end()) { navigationBoundary(); return; }
        const Game game = *found;
        companion_.rig.emote(companion::Emotion::excited);
        closeCompanion();
        launchGame(game);
        return;
    }
    case companion_ui::Chip::Kind::roulette: companionRoulette(); return;
    case companion_ui::Chip::Kind::prompt: companionSend(chip.prompt); return;
    }
}

void Launcher::companionClick(float x, float y) {
    if (companion_.chat.feed) {
        const auto size = renderer_.viewport();
        const int hit = companion_ui::FeedLayout{size.width, size.height}.hit(x, y, companion_.feed.view);
        companionFeedAction(hit);
        return;
    }
    if (companion_.chat.viewer) { companionViewer(false); return; } // Her enlarged photo: a click anywhere closes it.
    const auto view = renderer_.viewport();
    if (companion_.chat.mind) {
        const companion_ui::Layout layout{view.width, view.height};
        if (companion_.chat.mind_confirm) {
            for (int button : {companion_ui::confirm_reset, companion_ui::confirm_cancel})
                if (layout.mindConfirmButton(button).contains(x, y)) { companionMindDeletePick(button == companion_ui::confirm_reset); return; }
            if (!layout.mindConfirm().contains(x, y)) companionMindDeleteConfirm(false);
            return;
        }
        if (layout.mindClose().contains(x, y)) { companionMind(false); return; }
        for (int i = 0; i < companion_ui::mind_filter_count; ++i)
            if (layout.mindFilter(i).contains(x, y)) { companionMindFilter(static_cast<companion_ui::MindFilter>(i)); return; }
        if (companion_.chat.mind_hover == companion_ui::mind_delete_hit) { companionMindDeleteConfirm(true); return; }
        if (companion_.chat.mind_hover == companion_ui::mind_reveal_hit) { companionMindReveal(); return; }
        if (companion_.chat.mind_hover == companion_ui::mind_live_hit) { companionMindLiveReveal(); return; }
        if (companion_.chat.mind_hover >= 0) {
            companion_.chat.mind_selected = companion_.chat.mind_hover;
            const auto nodes = companionMindNodes();
            if (companion_.chat.mind_selected < static_cast<int>(nodes.size())) {
                const auto& node = nodes[static_cast<size_t>(companion_.chat.mind_selected)];
                if (std::abs(node.x) > 0.01f || std::abs(node.z) > 0.01f)
                    companion_.chat.mind_yaw = -std::atan2(node.x, -node.z);
                companion_.chat.mind_pitch = std::clamp(std::asin(std::clamp(node.y, -0.85f, 0.85f)) * 0.6f, -0.7f, 0.7f);
                static constexpr std::array<companion::Emotion, 6> node_emotions{
                    companion::Emotion::smug, companion::Emotion::playful, companion::Emotion::thinking,
                    companion::Emotion::smile, companion::Emotion::excited, companion::Emotion::gaming};
                companion_.rig.emote(node.emotion.value_or(node_emotions[static_cast<size_t>(node.kind) % node_emotions.size()]));
            }
            audio_.navigate();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (layout.mindSphere().contains(x, y)) {
            companion_.chat.mind_dragging = true;
            companion_.chat.mind_drag_x = x;
            companion_.chat.mind_drag_y = y;
            return;
        }
        return;
    }
    const int hit = companion_ui::Layout{view.width, view.height}.hit(x, y, companion_.chat);
    if (companion_.chat.confirm) {
        // The reset dialog: a button picks; a click beside the dialog cancels (as Anuluj).
        const int button = companion_ui::confirmButtonOf(hit);
        const auto dialog = companion_ui::Layout{view.width, view.height}.confirm();
        if (button >= 0) companionResetPick(button == companion_ui::confirm_reset);
        else if (!dialog.contains(x, y)) companionResetConfirm(false);
        return;
    }
    if (companion_.chat.menu) {
        // Open menu: a row picks, "…" or anywhere else closes it.
        if (const int row = companion_ui::menuRowOf(hit); row >= 0) companionMenuPick(row);
        else companionMenu(false);
        return;
    }
    // Her photo in the log (live or a history page): enlarged.
    if (hit == companion_ui::no_hit && renderer_.companionPhoto().contains(x, y) && companionPhotoShown()) { companionViewer(true); return; }
    if (hit == companion_ui::no_hit && companionLinkClick(x, y)) return;
    if (hit == companion_ui::no_hit) return;
    companion_.chat.focus = hit;
    if (hit == companion_ui::focus_input) { InvalidateRect(hwnd_, nullptr, FALSE); return; } // Click into the field: type.
    companionAccept();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring Launcher::companionGameUrl(const std::wstring& title) const {
    if (title.empty()) return {};
    // Prefer an exact Steam library entry, including when the player owns another store's copy too.
    const auto wanted = game_index::fold(title);
    for (const auto& game : public_library_)
        if (steamArtwork(game) && game.app_id && game_index::fold(game.name) == wanted)
            return L"https://store.steampowered.com/app/" + std::to_wstring(game.app_id) + L"/";
    // The index also knows aliases; only a real Steam key supplies an app id, never model text.
    const auto key = companionGameKey(title);
    if (key.starts_with("steam:")) {
        uint32_t id = 0;
        const auto parsed = std::from_chars(key.data() + 6, key.data() + key.size(), id);
        if (parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size() && id)
            return L"https://store.steampowered.com/app/" + std::to_wstring(id) + L"/";
    }
    const auto query = url_encoding::component(title + L" game");
    return query.empty() ? std::wstring{} : L"https://www.google.com/search?q=" + query;
}

void Launcher::companionVideo(const std::wstring& url) {
    const auto id = companion::links::youtubeId(url);
    if (id.empty()) return;
    const auto destination = desktop_mode_ ? L"https://www.youtube.com/watch?v=" + text_encoding::wideLenient(id) : companion::links::playerUrl(id);
    openGlanceBrowser(destination, true);
}

bool Launcher::companionLinkClick(float x, float y) {
    if (const auto url = renderer_.companionVideoAt(x, y); !url.empty()) { companionVideo(url); return true; }
    const auto title = renderer_.companionLinkAt(x, y);
    if (title.empty()) return false;
    // An installed game in the player's library opens its native Szczegóły screen; uninstalled or
    // external games (or offline URL tests with browse_ set) open the store/search page as before.
    if (!browse_) {
        const auto key = companionGameKey(title);
        if (!key.empty()) {
            const auto found = std::find_if(public_library_.begin(), public_library_.end(),
                                            [&](const Game& game) { return gameKey(game) == key && game.installed; });
            if (found != public_library_.end()) {
                const Game game = *found;
                companionFlush();
                companionHush();
                companion_.rig.silence();
                openDetails(game);
                return true;
            }
        }
    }
    openGlanceBrowser(companionGameUrl(title));
    return true;
}

void Launcher::companionPointer(float mouse_x, float mouse_y) {
    auto& chat = companion_.chat;
    if (companionOpen() && chat.feed) {
        const float scale = desktopActive() ? desktopScale() : renderer_.viewport().scale;
        const auto size = renderer_.viewport();
        const int hit = companion_ui::FeedLayout{size.width, size.height}.hit(mouse_x / scale, mouse_y / scale, companion_.feed.view);
        if (companion_.feed.view.hover != hit) { companion_.feed.view.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (companionOpen() && chat.mind) {
        const float scale = desktopActive() ? desktopScale() : renderer_.viewport().scale;
        const float x = mouse_x / scale, y = mouse_y / scale;
        if (chat.mind_dragging) {
            if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
                chat.mind_dragging = false;
            } else {
                chat.mind_yaw += (x - chat.mind_drag_x) * 0.0075f;
                chat.mind_pitch = std::clamp(chat.mind_pitch + (y - chat.mind_drag_y) * 0.006f, -0.85f, 0.85f);
                chat.mind_drag_x = x;
                chat.mind_drag_y = y;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        const auto view = renderer_.viewport();
        const companion_ui::Layout layout{view.width, view.height};
        if (chat.mind_confirm) {
            int hit_btn = companion_ui::no_hit;
            for (int button : {companion_ui::confirm_reset, companion_ui::confirm_cancel})
                if (layout.mindConfirmButton(button).contains(x, y)) hit_btn = companion_ui::confirmFocus(button);
            if (chat.mind_hover != hit_btn) { chat.mind_hover = hit_btn; InvalidateRect(hwnd_, nullptr, FALSE); }
            return;
        }
        const auto sphere = layout.mindSphere();
        const auto nodes = companionMindNodes();
        const int sel = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        int best_node = companion_ui::no_hit;
        for (int i = 0; i < companion_ui::mind_filter_count; ++i) if (layout.mindFilter(i).contains(x, y)) best_node = companion_ui::mind_filter_hit_base - i;
        const bool revealable = !nodes.empty() && nodes[static_cast<size_t>(sel)].revealable;
        if (best_node <= companion_ui::mind_filter_hit_base) {
            // The toolbar is outside the sphere and list; retain its own hover identity.
        } else if (!nodes.empty() && (nodes[static_cast<size_t>(sel)].deletable || nodes[static_cast<size_t>(sel)].locked) && layout.mindDelete().contains(x, y)) {
            best_node = companion_ui::mind_delete_hit;
        } else if (revealable && (layout.mindReveal().contains(x, y) || (nodes[static_cast<size_t>(sel)].masked && layout.mindDetail().contains(x, y)))) {
            best_node = companion_ui::mind_reveal_hit; // Pokaż/Ukryj, or a click on the masked thought itself.
        } else if (layout.mindLiveBar().contains(x, y) && companionMindTelemetry().live_hidden) {
            best_node = companion_ui::mind_live_hit;
        } else if (sphere.contains(x, y)) {
            const float cx = (sphere.left + sphere.right) * 0.5f, cy = (sphere.top + sphere.bottom) * 0.5f;
            const float radius = layout.mindRadius(chat.mind_zoom_spring.value);
            const float cyaw = std::cos(chat.mind_yaw_spring.value), syaw = std::sin(chat.mind_yaw_spring.value);
            const float cpitch = std::cos(chat.mind_pitch_spring.value), spitch = std::sin(chat.mind_pitch_spring.value);
            float best_dist = 26.0f * 26.0f;
            for (size_t i = 0; i < nodes.size(); ++i) {
                const float rx = nodes[i].x * cyaw - nodes[i].z * syaw;
                const float rz0 = nodes[i].x * syaw + nodes[i].z * cyaw;
                const float ry = nodes[i].y * cpitch - rz0 * spitch;
                const float rz = nodes[i].y * spitch + rz0 * cpitch;
                const float persp = 2.4f / (2.4f + rz * 0.65f);
                const float sx = cx + rx * radius * persp;
                const float sy = cy - ry * radius * persp;
                const float d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy);
                if (d2 < best_dist) { best_dist = d2; best_node = static_cast<int>(i); }
            }
        } else if (layout.mindClose().contains(x, y)) {
            best_node = companion_ui::focus_back;
        } else {
            const int total = static_cast<int>(nodes.size());
            const int first = layout.mindListFirst(std::clamp(chat.mind_selected, 0, std::max(0, total - 1)), total);
            for (int slot = 0; slot < layout.mindListRows() && first + slot < total; ++slot) {
                if (layout.mindListRow(slot).contains(x, y)) { best_node = first + slot; break; }
            }
        }
        if (chat.mind_hover != best_node) {
            chat.mind_hover = best_node;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    const bool home = companionAvailable() && !desktop_menu_ && !desktop_quick_.open && !desktop_meta_.open && !desktop_downloads_open_;
    const bool chatting = companionOpen() && topModal() == Modal::none && !friends_open_ && !chat.menu && !chat.confirm && !chat.viewer && !chat.mind && !chat.feed && !keyboard_.open;
    const auto before = companion_.link_pointer;
    if (home || chatting) {
        const float scale = desktopActive() ? desktopScale() : renderer_.viewport().scale;
        companion_.link_pointer = std::pair{mouse_x / scale, mouse_y / scale};
    } else companion_.link_pointer.reset();
    if (before != companion_.link_pointer) {
        const auto overLink = [&](const auto& point) { return point && (!renderer_.companionLinkAt(point->first, point->second).empty() || !renderer_.companionVideoAt(point->first, point->second).empty()); };
        if (overLink(before) || overLink(companion_.link_pointer)) InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

std::vector<companion_ui::MenuItem> Launcher::companionMenuItems() const {
    using Kind = companion_ui::MenuItem::Kind;
    const auto& chat = companion_.chat;
    const auto& guest = companion_.guest;
    // A guest comes and goes between replies, never over one on its way.
    const bool free = !chat.waiting && !companionRouletteSpinning() && !chat.ended;
    std::vector<companion_ui::MenuItem> items;
    if (guest.look) items.push_back({Kind::farewell, std::wstring(tr(L"Pożegnaj ")) + companion::name(*guest.look), free && !guest.leaving});
    else if (settings_.companion_guests) {
        const auto others = companionGuestCandidates();
        if (others.size() == 1) items.push_back({Kind::invite, std::wstring(tr(L"Zawołaj ")) + companion::name(others.front()), free});
        else if (!others.empty()) items.push_back({Kind::invite, tr(L"Zawołaj kogoś"), free});
    }
    if (companionPhotoShown()) items.push_back({Kind::photo, tr(L"Powiększ zdjęcie"), true}); // Her photo, for the pad and keyboard.
    // The latest shared videos also have a pad/keyboard route through the menu.
    for (size_t i = chat.log.size(); i-- > 0;) {
        if (!chat.log[i].user) continue;
        bool found = false;
        const auto links = companion::links::find(chat.log[i].text);
        for (size_t j = 0; j < links.size(); ++j) if (!links[j].video_id.empty()) {
            items.push_back({Kind::video, L"YouTube · " + std::wstring(tr(L"Odtwórz film")), true, static_cast<int>(i * companion::links::max_links + j), text_encoding::wideLenient(links[j].video_id)});
            found = true;
        }
        if (found) break;
    }
    items.push_back({Kind::diagnostics, settings_.companion_debug ? tr(L"Ukryj diagnostykę") : tr(L"Pokaż diagnostykę"), true});
    items.push_back({Kind::costumes, tr(L"Zmień strój"), true});
    items.push_back({Kind::change, tr(L"Zmień towarzyszkę"), companion::availableLooks().size() > 1});
    items.push_back({Kind::mind, tr(L"Umysł i pamięć"), true});
    if (settings_.companion_gamification) {
        items.push_back({Kind::reset_affinity, tr(L"Zresetuj sympatię"), true});
    }
    // Her older conversations, read only (never while a reply or a guest is in this one).
    if (companion_.persist) {
        const auto all = companion::chats::list(userConfigDirectory(), companionLook());
        const bool older = std::any_of(all.begin(), all.end(), [&](uint64_t started) { return started != companion_.chat_started; });
        items.push_back({Kind::history, tr(L"Historia czatu"), older && !chat.waiting && !chat.talking() && !companionRouletteSpinning() && !guest.look});
    }
    items.push_back({Kind::backup, tr(L"Pełna kopia postaci"), !companion_.backup_busy && !companion_.busy});
    // The conversation: ended by the player (her goodbye, then her rest), or a new one once she is back.
    if (chat.ended) {
        companion_ui::MenuItem item{Kind::clear, tr(L"Nowa rozmowa"), !companionResting()};
        if (!item.enabled) item.detail = std::wstring(tr(L"za ")) + companion::chats::duration(companionAway().seconds, english());
        items.push_back(std::move(item));
    } else items.push_back({Kind::clear, tr(L"Zakończ rozmowę"), free && !chat.talking() && companion_.typed > 0});
    return items;
}

void Launcher::companionMenu(bool open) {
    auto& chat = companion_.chat;
    if (chat.menu == open) return;
    if (open && chat.archive) { navigationBoundary(); return; } // A history page has no menu.
    chat.menu = open;
    if (open) {
        closeKeyboard();
        chat.menu_items = companionMenuItems();
        // The first row that can be picked.
        const auto usable = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& item) { return item.enabled; });
        chat.menu_focus = usable == chat.menu_items.end() ? 0 : static_cast<int>(usable - chat.menu_items.begin());
        chat.focus = companion_ui::focus_more;
        audio_.context();
    } else audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMenuPick(int row) {
    auto& chat = companion_.chat;
    if (row < 0 || row >= static_cast<int>(chat.menu_items.size()) || !chat.menu_items[static_cast<size_t>(row)].enabled) { navigationBoundary(); return; }
    const auto kind = chat.menu_items[static_cast<size_t>(row)].kind;
    if (kind == companion_ui::MenuItem::Kind::costumes) { companionCostumeMenu(); return; } // The menu lists her costumes.
    if (kind == companion_ui::MenuItem::Kind::history) { companionHistoryMenu(); return; } // …or her older conversations.
    if (kind == companion_ui::MenuItem::Kind::relationship) { companionRelationMenu(); return; } // …or her relationship track/distance.
    const int value = chat.menu_items[static_cast<size_t>(row)].value;
    chat.menu = false;
    chat.focus = chat.ended ? companion_ui::focus_send : companion_ui::focus_input;
    switch (kind) {
    case companion_ui::MenuItem::Kind::invite: companionInvite(); break;
    case companion_ui::MenuItem::Kind::farewell: companionDismiss(); break;
    case companion_ui::MenuItem::Kind::clear: if (chat.ended) companionRestart(); else companionEnd(true); break;
    case companion_ui::MenuItem::Kind::history: break; // Handled above.
    case companion_ui::MenuItem::Kind::archive: companionOpenArchive(value); return;
    case companion_ui::MenuItem::Kind::reset_affinity: companionResetConfirm(true); return;
    case companion_ui::MenuItem::Kind::costumes: break; // Handled above.
    case companion_ui::MenuItem::Kind::wear: companionWear(value); break;
    case companion_ui::MenuItem::Kind::relationship: break; // Handled above.
    case companion_ui::MenuItem::Kind::relation_pick: companionRelationPick(value); break;
    case companion_ui::MenuItem::Kind::photo: companionViewer(true); return;
    case companion_ui::MenuItem::Kind::video: {
        const size_t index = static_cast<size_t>(value) / companion::links::max_links, slot = static_cast<size_t>(value) % companion::links::max_links;
        if (index < chat.log.size()) {
            const auto links = companion::links::find(chat.log[index].text);
            if (slot < links.size()) companionVideo(links[slot].url);
        }
        return;
    }
    case companion_ui::MenuItem::Kind::change: companionPicker(true); return;
    case companion_ui::MenuItem::Kind::mind: companionMind(true); return;
    case companion_ui::MenuItem::Kind::backup: companionBackup(); return;
    case companion_ui::MenuItem::Kind::diagnostics: // Kept in settings.ini, like a Settings switch.
        settings_.companion_debug = !settings_.companion_debug;
        settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
        audio_.context();
        break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

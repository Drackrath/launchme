#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <sstream>

namespace {
using C = aurelia::Command;
using A = aurelia_ui::Action;
using P = aurelia_ui::Page;
using Row = aurelia_ui::Row;
using text_encoding::wideLenient;
const std::vector<json::Value>& array(const json::Value& value, const char* key = nullptr) {
    static const std::vector<json::Value> empty;
    const auto* v = key ? value.get(key) : &value;
    return v && v->type == json::Value::Type::array ? v->elements : empty;
}
std::wstring sizeText(uint64_t bytes) {
    wchar_t buffer[48]{}; swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024)); return buffer;
}
}

void Launcher::openAurelia(const Game* game, bool intro) {
    if (game && (!aurelia_active_ || !aurelia::validGame(*game))) return;
    const auto copy = game ? *game : Game{};
    dismissMenu(); closeQuick(); closeFriends(); closeKeyboard(); closeCategoryMenu(); cancelEnterHold(); cancelTabHold();
    desktop_menu_.reset();
    aurelia_chat_cancel_->store(true);
    aurelia_ui_ = {}; aurelia_ui_.open = true; aurelia_ui_.intro = intro; aurelia_ui_.game = copy;
    aureliaPage(game ? P::game : P::setup);
    audio_.context();
}
void Launcher::closeAurelia() {
    if (!aurelia_ui_.open) return;
    if (aurelia_ui_.confirmation) { aurelia_ui_.confirmation.reset(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    const bool intro = aurelia_ui_.intro;
    if (aurelia_ui_.page == P::chat && aurelia_ui_.friend_id) aurelia_chat_drafts_[aurelia_ui_.friend_id] = aurelia_ui_.input;
    aurelia_chat_cancel_->store(true); ++aurelia_page_generation_; closeKeyboard();
    aurelia_ui_ = {};
    if (intro) { settings_.aurelia_intro_done = true; settings_dirty_ = true; saveSettings(); if (!desktop_mode_ && !settings_.tour_done) startTour(); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaRows() {
    auto& s = aurelia_ui_; if (!s.open) return;
    s.enabled = settings_.aurelia; s.active = aurelia_active_; s.installed = !aurelia_version_.empty(); s.authenticated = aurelia_authenticated_;
    s.rows.clear(); s.input_hint.clear(); s.title = L"Aurelia";
    const auto add = [&](A action, std::wstring title, std::wstring description = {}, std::wstring value = {}, uint64_t id = 0, std::wstring payload = {}) -> Row& {
        s.rows.push_back({std::move(title), std::move(description), std::move(value), action, id, std::move(payload)}); return s.rows.back();
    };
    const auto& data = aurelia_page_data_;
    if (s.page == P::setup) {
        s.subtitle = s.intro ? tr(L"Więcej możliwości dla Twojej biblioteki Steam") : tr(L"Rozszerzona integracja Steam");
        s.status = aurelia_setup_status_;
        auto& toggle = add(A::toggle, tr(L"Rozszerzona integracja Aurelia"), tr(L"Pobierania, zapisy w chmurze, DLC, wersje gier i czat ze znajomymi."));
        toggle.toggle = true; toggle.checked = settings_.aurelia;
        auto& helper = add(A::helper, s.installed ? tr(L"Sprawdź i zainstaluj aktualizację") : tr(L"Zainstaluj Aurelię"),
                          s.installed ? aurelia_version_ : tr(L"Najnowsze aurelia_windows_x86_64.exe z GitHub · weryfikacja SHA-256"),
                          aurelia_helper_busy_ ? tr(L"Pobieranie…") : s.installed ? tr(L"Zainstalowano") : tr(L"Brak instalacji"));
        helper.locked = aurelia_helper_busy_;
        add(A::repository, tr(L"Repozytorium Aurelii"), L"github.com/Drackrath/Aurelia", tr(L"Otwórz"));
        auto& login = add(A::login, tr(L"Zaloguj w oknie Aurelii"), tr(L"Hasło i Steam Guard wpisujesz bezpośrednio w oknie Aurelii."), aurelia_account_name_);
        login.locked = !aurelia_transport_ || s.busy;
        add(A::refresh, tr(L"Sprawdź połączenie"), {}, aurelia_authenticated_ ? tr(L"Połączono") : tr(L"Nie zalogowano")).locked = !aurelia_transport_ || s.busy;
        if (aurelia_authenticated_) {
            add(A::friends, tr(L"Znajomi i czat")).locked = !aurelia_active_;
            add(A::reconnect, tr(L"Połącz ponownie")).locked = s.busy;
            add(A::logout, tr(L"Wyloguj z Aurelii")).danger = true;
        }
        add(A::close, s.intro ? tr(L"Dalej") : tr(L"Gotowe"));
    } else {
        s.subtitle = s.page == P::friends || s.page == P::chat ? tr(L"Znajomi Steam · Aurelia") : s.game.name;
        switch (s.page) {
        case P::game: {
            s.title = tr(L"Zarządzaj grą · Aurelia");
            add(A::cloud, tr(L"Zapisy w chmurze"), aureliaCloudText(s.game), tr(L"Otwórz"));
            add(A::dlc, tr(L"Edytuj DLC"), tr(L"Instaluj, włączaj i wyłączaj posiadane dodatki."));
            add(A::versions, tr(L"Wersje i przypinanie"), tr(L"Przypnij obecną wersję albo zainstaluj starszy manifest."));
            add(A::workshop, L"Steam Workshop", tr(L"Przeglądaj i instaluj mody oraz zarządzaj subskrypcjami."));
            auto& steam = add(A::steam, tr(L"Uruchamiaj bez Steam"), tr(L"Włączone: uruchamia przez Aurelię. Wyłączone: przez klienta Steam. Dla zgodnych gier single player / offline."));
            steam.toggle = true; steam.checked = settings_.aurelia_standalone.contains(std::to_string(s.game.app_id));
            add(A::play, tr(L"Graj"), {}, steam.checked ? L"Aurelia" : L"Steam").locked = !s.game.installed;
            add(A::install, tr(L"Zainstaluj grę"), tr(L"Wybierz bibliotekę i pobieraj w tle.")).locked = s.game.installed;
            add(A::update, tr(L"Aktualizuj grę")).locked = !s.game.installed;
            add(A::verify, tr(L"Sprawdź pliki gry")).locked = !s.game.installed;
            add(A::move, tr(L"Przenieś do innej biblioteki")).locked = !s.game.installed;
            if (aurelia_downloads_.contains(s.game.app_id)) add(A::cancel, tr(L"Anuluj pobieranie"));
            auto& remove = add(A::uninstall, tr(L"Odinstaluj"), tr(L"Usuwa pliki gry. Zapisy w chmurze pozostają.")); remove.danger = true; remove.locked = !s.game.installed;
            break;
        }
        case P::cloud: {
            s.title = tr(L"Zapisy w chmurze");
            auto& automatic = add(A::auto_cloud, tr(L"Synchronizuj przed grą i po niej"), tr(L"Tylko przy uruchamianiu bez Steam. Konflikt lub niepełne zapisy zatrzymują start gry."));
            automatic.toggle = true; automatic.checked = settings_.aurelia_auto_cloud.contains(std::to_string(s.game.app_id));
            add(A::sync, tr(L"Synchronizuj zapisy"), tr(L"Aurelia wykrywa konflikty bez wybierania zwycięskiej kopii."));
            const auto found = aurelia_cloud_.find(s.game.app_id);
            if (found != aurelia_cloud_.end() && found->second.state == aurelia::CloudState::conflicts) {
                add(A::resolve_cloud, tr(L"Użyj zapisów z chmury"), tr(L"Zastępuje konfliktujące zapisy lokalne.")).danger = true;
                add(A::resolve_local, tr(L"Użyj zapisów lokalnych"), tr(L"Zastępuje konfliktujące zapisy w chmurze.")).danger = true;
            }
            if (found != aurelia_cloud_.end()) for (const auto& issue : found->second.issues) add(A::none, issue);
            add(A::refresh, tr(L"Odśwież listę plików"), aureliaCloudText(s.game));
            for (const auto& f : array(data, "files")) add(A::none, wideLenient(aurelia::text(f, "filename")),
                std::to_wstring(aurelia::integer(f.get("size")).value_or(0)) + L" B");
            break;
        }
        case P::dlc:
            s.title = tr(L"Edytuj DLC");
            add(A::refresh, tr(L"Odśwież dodatki"));
            if (const auto list = aurelia::dlcs(data)) for (const auto& d : *list) {
                const auto action = d.owned != true ? A::none : d.installed != true ? A::install : d.disabled == true ? A::enable_dlc : A::disable_dlc;
                auto& row = add(action, d.name, d.owned == true ? tr(L"Posiadasz") : d.owned == false ? tr(L"Nie posiadasz") : tr(L"Stan nieznany"),
                    action == A::install ? tr(L"Instaluj") : action == A::enable_dlc ? tr(L"Włącz") : action == A::disable_dlc ? tr(L"Wyłącz") : L"", d.app);
                row.locked = !d.owned.has_value() || !d.installed.has_value() || !d.disabled.has_value();
            }
            break;
        case P::versions:
            s.title = tr(L"Wersje i przypinanie");
            add(A::pin, tr(L"Przypnij obecną wersję"), tr(L"Blokuje aktualizacje w Aurelii. Klient Steam ma własne ustawienia aktualizacji."));
            add(A::unpin, tr(L"Odblokuj aktualizacje"));
            add(A::branches, tr(L"Gałęzie beta"));
            add(A::refresh, tr(L"Odśwież manifesty"), tr(L"Steam udostępnia tylko aktualne manifesty. Starszych ID szukaj na SteamDB."));
            for (const auto& m : array(data)) {
                const auto depot = aurelia::integer(m.get("depot_id")).value_or(0), manifest = aurelia::integer(m.get("manifest_id")).value_or(0);
                add(A::none, wideLenient(aurelia::text(m, "depot_name")) + L" · " + wideLenient(aurelia::text(m, "branch")),
                    std::to_wstring(depot) + L" : " + std::to_wstring(manifest));
            }
            s.input_hint = tr(L"Downgrade: depot_id manifest_id"); break;
        case P::branches:
            s.title = tr(L"Gałęzie beta");
            for (const auto& b : array(data, "branches")) if (b.type == json::Value::Type::string)
                add(A::set_branch, wideLenient(b.text), tr(L"Zmień gałąź; następnie wybierz Aktualizuj grę."), tr(L"Wybierz"), 0, wideLenient(b.text));
            add(A::refresh, tr(L"Odśwież")); break;
        case P::libraries:
            s.title = aurelia_library_action_ == A::move ? tr(L"Przenieś do innej biblioteki") : tr(L"Wybierz bibliotekę Steam");
            for (const auto& l : array(data, "libraries")) {
                const auto path = wideLenient(aurelia::text(l, "path"));
                const auto free = aurelia::integer(l.get("free_bytes"));
                add(A::choose_library, path, free ? tr(L"Wolne miejsce: ") + sizeText(*free) : tr(L"Wolne miejsce nieznane"), tr(L"Wybierz"), 0, path);
            }
            s.input_hint = tr(L"Albo wpisz pełną ścieżkę biblioteki, np. D:\\SteamLibrary"); break;
        case P::workshop:
            s.title = L"Steam Workshop";
            add(A::refresh, tr(L"Odśwież Workshop"));
            for (const auto& item : array(data, "items")) {
                const auto id = aurelia::integer(item.get("id")).value_or(0);
                if (!id) continue;
                const auto title = wideLenient(aurelia::text(item, "title"));
                add(A::workshop_subscribe, title, std::to_wstring(id), tr(L"Subskrybuj i instaluj"), id);
            }
            s.input_hint = tr(L"ID Workshop: +ID subskrybuje, -ID anuluje, ID instaluje"); break;
        case P::friends:
            s.title = tr(L"Znajomi i czat");
            add(A::refresh, tr(L"Odśwież znajomych"));
            add(A::presence, tr(L"Status: online"), {}, {}, 0, L"online");
            add(A::presence, tr(L"Status: niewidoczny"), {}, {}, 0, L"invisible");
            for (const auto& f : aurelia::friends(data).friends)
                add(A::chat, f.name, f.game.empty() ? f.status ? tr(L"Online") : tr(L"Offline") : f.game, tr(L"Czat"), f.id, f.name);
            break;
        case P::chat: s.title = s.friend_name; s.input_hint = tr(L"Napisz wiadomość…"); break;
        default: break;
        }
    }
    const auto v = renderer_.viewport(); const aurelia_ui::Layout layout{v.width, v.height};
    s.focus = std::clamp(s.focus, 0, std::max(0, static_cast<int>(s.rows.size()) - 1));
    if (s.page != P::chat) s.first = std::clamp(s.first, 0, std::max(0, static_cast<int>(s.rows.size()) - layout.visible(s)));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaPage(P page) {
    aurelia_chat_cancel_->store(true); closeKeyboard();
    auto& s = aurelia_ui_; const auto generation = ++aurelia_page_generation_;
    if (s.page == P::chat && s.friend_id) aurelia_chat_drafts_[s.friend_id] = s.input;
    s.sending = s.live = false;
    s.page = page; s.focus = s.first = 0; s.input_focus = false; s.input.clear(); s.confirmation.reset(); s.status.clear(); s.busy = false;
    aurelia_page_data_ = {}; aureliaRows();
    if (!aurelia_transport_ || (page != P::setup && !aurelia_authenticated_)) return;
    aurelia::Request request; request.app = s.game.app_id;
    switch (page) {
    case P::setup: request.command = C::health; break;
    case P::game: request.command = C::available; break;
    case P::cloud: request.command = C::cloud_list; break;
    case P::dlc: request.command = C::dlc; break;
    case P::versions: request.command = C::manifests; break;
    case P::branches: request.command = C::branches; break;
    case P::workshop: request.command = C::workshop_browse; break;
    case P::libraries: request.command = C::libraries; break;
    case P::friends: request.command = C::friends; break;
    default: return;
    }
    s.busy = true; s.status = tr(L"Wczytywanie…");
    aureliaRequest(request, [generation, page](Launcher& app, aurelia::Reply reply) {
        if (!app.aurelia_ui_.open || app.aurelia_page_generation_ != generation) return;
        auto& s = app.aurelia_ui_; s.busy = false; s.status = reply.error;
        if (reply.ok) {
            app.aurelia_page_data_ = reply.data;
            if (page == P::setup) {
                app.aurelia_authenticated_ = aurelia::flag(reply.data, "logged_in");
                app.aurelia_account_name_ = wideLenient(aurelia::text(reply.data, "account"));
                app.aurelia_setup_status_ = app.settings_.aurelia != app.aurelia_active_ ? tr(L"Zmiana integracji będzie aktywna po ponownym uruchomieniu Launchme.") : L"";
            } else if (page == P::game) {
                s.game.installed = aurelia::flag(reply.data, "available");
                s.status = aurelia::flag(reply.data, "pinned") ? tr(L"Wersja przypięta · aktualizacje Aurelii zablokowane") : tr(L"Aktualizacje Aurelii odblokowane");
            } else if (page == P::cloud) {
                auto& cloud = app.aurelia_cloud_[s.game.app_id];
                // Listing files is not a sync and cannot erase a reported conflict.
                if (cloud.state != aurelia::CloudState::conflicts && cloud.state != aurelia::CloudState::incomplete && cloud.state != aurelia::CloudState::failed && cloud.state != aurelia::CloudState::synced)
                    cloud = aurelia::cloud(reply, false);
                cloud.checked = GetTickCount64();
            }
        } else if (page == P::setup) app.aurelia_setup_status_ = reply.error;
        app.aureliaRows();
    });
}

void Launcher::aureliaActivate(const Row& row, bool confirmed) {
    auto& s = aurelia_ui_; if (!s.open || row.locked || row.action == A::none) return;
    if (row.action == A::close) { closeAurelia(); return; }
    if (row.action == A::toggle) { toggleAurelia(); return; }
    if (row.action == A::helper) { installAurelia(); return; }
    if (row.action == A::repository) {
        execute_(hwnd_, GameAction{L"https://github.com/Drackrath/Aurelia", {}, {}, ActionKind::open_client}); return;
    }
    if (s.busy && row.action != A::cancel) return;
    switch (row.action) {
    case A::game: aureliaPage(P::game); return;
    case A::cloud: aureliaPage(P::cloud); return;
    case A::dlc: aureliaPage(P::dlc); return;
    case A::versions: aureliaPage(P::versions); return;
    case A::branches: aureliaPage(P::branches); return;
    case A::workshop: aureliaPage(P::workshop); return;
    case A::friends: if (aurelia_active_) aureliaPage(P::friends); return;
    case A::refresh: aureliaPage(s.page); steam_rescan_->wake(); return;
    case A::chat: aureliaOpenChat(row.id, row.payload); return;
    case A::steam: {
        const auto id = std::to_string(s.game.app_id);
        if (settings_.aurelia_standalone.contains(id)) settings_.aurelia_standalone.erase(id); else settings_.aurelia_standalone.insert(id);
        settings_dirty_ = true; saveSettings(); aureliaRows(); return;
    }
    case A::auto_cloud: {
        const auto id = std::to_string(s.game.app_id);
        if (!settings_.aurelia_auto_cloud.erase(id)) settings_.aurelia_auto_cloud.insert(id);
        settings_dirty_ = true; saveSettings(); aureliaRows(); return;
    }
    case A::play: { const auto game = s.game; closeAurelia(); launchGame(game); return; }
    case A::install: if (s.page != P::dlc) { aurelia_library_action_ = A::install; aureliaPage(P::libraries); return; } break;
    case A::move: aurelia_library_action_ = A::move; aureliaPage(P::libraries); return;
    default: break;
    }
    const bool destructive = row.action == A::uninstall || row.action == A::downgrade || row.action == A::resolve_cloud || row.action == A::resolve_local || row.action == A::logout;
    if (destructive && !confirmed) { s.confirmation = row; s.input_focus = false; s.hover = -4; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    aurelia::Request request; request.app = s.game.app_id;
    switch (row.action) {
    case A::login: request.command = C::login; break;
    case A::logout: request.command = C::logout; break;
    case A::reconnect: request.command = C::reconnect; break;
    case A::sync: case A::resolve_cloud: case A::resolve_local:
        request.command = C::cloud_sync; request.text = row.action == A::resolve_cloud ? L"cloud" : row.action == A::resolve_local ? L"local" : L""; break;
    case A::install: request.command = C::install; request.app = static_cast<uint32_t>(row.id); break;
    case A::choose_library: request.command = aurelia_library_action_ == A::move ? C::move : C::install; request.folder = row.payload; break;
    case A::update: request.command = C::update; break;
    case A::verify: request.command = C::verify; break;
    case A::cancel: request.command = C::cancel_install; break;
    case A::uninstall: request.command = C::uninstall; break;
    case A::enable_dlc: request.command = C::enable_dlc; request.app = static_cast<uint32_t>(row.id); break;
    case A::disable_dlc: request.command = C::disable_dlc; request.app = static_cast<uint32_t>(row.id); break;
    case A::pin: request.command = C::pin; break;
    case A::unpin: request.command = C::unpin; break;
    case A::downgrade: {
        std::wistringstream values(row.payload); std::wstring depot, manifest, extra; values >> depot >> manifest;
        const auto d = json::string(text_encoding::utf8(depot)), m = json::string(text_encoding::utf8(manifest));
        const auto di = aurelia::integer(&d), mi = aurelia::integer(&m);
        if (!di || !*di || *di > UINT32_MAX || !mi || !*mi || (values >> extra)) { s.status = tr(L"Wpisz poprawny depot_id i manifest_id"); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        request.command = C::downgrade; request.depot = static_cast<uint32_t>(*di); request.id = *mi; break;
    }
    case A::set_branch: request.command = C::set_branch; request.text = row.payload; break;
    case A::workshop_install: request.command = C::workshop_install; request.id = row.id; break;
    case A::workshop_subscribe: request.command = C::workshop_subscribe; request.id = row.id; break;
    case A::workshop_unsubscribe: request.command = C::workshop_unsubscribe; request.id = row.id; break;
    case A::presence: request.command = C::presence; request.text = row.payload; break;
    default: return;
    }
    s.confirmation.reset();
    if (row.action == A::login || row.action == A::logout || row.action == A::reconnect || row.action == A::presence) {
        if (row.action != A::presence && (!aurelia_operations_.empty() || !aurelia_playing_.empty() || !aurelia_downloads_.empty())) { s.status = tr(L"Najpierw zakończ gry i pobierania Aurelii"); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        s.busy = true; s.status = tr(L"Wykonywanie polecenia…");
        const auto gen = aurelia_page_generation_;
        aureliaRequest(request, [gen, logout = row.action == A::logout](Launcher& app, aurelia::Reply reply) {
            if (reply.ok && logout) {
                ++*app.aurelia_epoch_; app.aurelia_authenticated_ = false; app.aurelia_account_ = 0;
                app.aurelia_cloud_.clear(); app.aurelia_chat_cancel_->store(true);
                app.aurelia_chat_drafts_.clear();
            }
            app.steam_rescan_->wake();
            if (!app.aurelia_ui_.open || app.aurelia_page_generation_ != gen) return;
            app.aurelia_ui_.busy = false;
            if (reply.ok) app.aureliaPage(app.aurelia_ui_.page);
            else { app.aurelia_setup_status_ = app.aurelia_ui_.status = reply.error; app.aureliaRows(); }
        });
    } else {
        if (game_sessions_.contains(gameKey(s.game)) || aurelia_playing_.contains(s.game.app_id)) {
            s.status = tr(L"Najpierw zamknij grę, zanim zmienisz jej pliki lub zapisy"); InvalidateRect(hwnd_, nullptr, FALSE); return;
        }
        if (request.command == C::cancel_install) {
            aureliaRequest(request, [](Launcher& app, aurelia::Reply r) {
                app.aurelia_ui_.status = r.ok ? tr(L"Zatrzymywanie pobierania…") : r.error;
                app.aurelia_poll_at_ = 0; InvalidateRect(app.hwnd_, nullptr, FALSE);
            });
        } else aureliaOperation(request, s.game);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaOpenChat(uint64_t id, const std::wstring& name) {
    if (!id || !aurelia_active_ || !aurelia_authenticated_) return;
    const std::wstring friend_name = name; // The source may be a row which aureliaPage destroys.
    if (!aurelia_ui_.open) openAurelia();
    aureliaPage(P::chat); auto& s = aurelia_ui_; s.friend_id = id; s.friend_name = friend_name; s.messages.clear(); s.input_focus = true; s.chat_follow = true; s.chat_scroll = 0;
    s.input = aurelia_chat_drafts_[id];
    s.busy = true; s.status = tr(L"Wczytywanie rozmowy…"); aureliaRows();
    const auto gen = aurelia_page_generation_;
    aurelia_chat_cancel_ = std::make_shared<std::atomic_bool>(false);
    aurelia::Request request{C::chat_history}; request.id = id;
    aureliaRequest(request, [gen, id](Launcher& app, aurelia::Reply reply) {
        if (!app.aurelia_ui_.open || gen != app.aurelia_page_generation_) return;
        auto& s = app.aurelia_ui_; s.busy = false;
        if (const auto history = reply.ok ? aurelia::chatHistory(reply.data) : std::nullopt) s.messages = *history;
        else s.status = reply.error.empty() ? tr(L"Nieprawidłowa odpowiedź Aurelii") : reply.error;
        s.first = std::max(0, static_cast<int>(s.messages.size()) - 5);
        aurelia::Request live{C::chat_live}; live.id = id;
        app.aureliaRequest(live, [gen](Launcher& a, aurelia::Reply r) {
            if (gen != a.aurelia_page_generation_ || !a.aurelia_ui_.open) return;
            a.aurelia_ui_.live = false;
            if (!r.cancelled) a.aurelia_ui_.status = r.error.empty() ? tr(L"Czat rozłączony. F5 łączy ponownie.") : r.error;
            InvalidateRect(a.hwnd_, nullptr, FALSE);
        }, [gen](Launcher& a, json::Value event) {
            if (gen != a.aurelia_page_generation_ || !a.aurelia_ui_.open) return;
            auto& s = a.aurelia_ui_; const auto kind = aurelia::text(event, "event");
            if (kind == "ready") {
                s.live = true; s.status = tr(L"Połączono · wiadomości na żywo");
                // Close the gap between the initial history request and arming the listener.
                aurelia::Request history{C::chat_history}; history.id = s.friend_id;
                a.aureliaRequest(history, [gen](Launcher& host, aurelia::Reply reply) {
                    if (!host.aurelia_ui_.open || host.aurelia_page_generation_ != gen) return;
                    if (auto messages = reply.ok ? aurelia::chatHistory(reply.data) : std::nullopt) {
                        auto& visible = host.aurelia_ui_.messages;
                        for (const auto& m : *messages) if (std::none_of(visible.begin(), visible.end(), [&](const auto& old) {
                            return old.timestamp == m.timestamp && old.self == m.self && old.text == m.text;
                        })) visible.push_back(m);
                        std::stable_sort(visible.begin(), visible.end(), [](const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
                    }
                    InvalidateRect(host.hwnd_, nullptr, FALSE);
                }, {}, a.aurelia_chat_cancel_);
            }
            else if (kind == "typing") s.status = tr(L"Pisze…");
            else if (kind == "error") s.status = wideLenient(aurelia::text(event, "message"));
            else if (kind == "message" || kind == "echo") {
                aurelia::ChatMessage m{aurelia::integer(event.get("timestamp")).value_or(0), kind == "echo", wideLenient(aurelia::text(event, "text"))};
                if (std::none_of(s.messages.begin(), s.messages.end(), [&](const auto& old) { return old.timestamp == m.timestamp && old.self == m.self && old.text == m.text; })) s.messages.push_back(std::move(m));
                if (s.messages.size() > 1000) s.messages.erase(s.messages.begin(), s.messages.begin() + 200);
                s.first = std::max(0, static_cast<int>(s.messages.size()) - 5); s.status = tr(L"Połączono · wiadomości na żywo");
            }
            InvalidateRect(a.hwnd_, nullptr, FALSE);
        }, app.aurelia_chat_cancel_);
        InvalidateRect(app.hwnd_, nullptr, FALSE);
    }, {}, aurelia_chat_cancel_);
}

void Launcher::aureliaSubmit() {
    auto& s = aurelia_ui_; if (!s.open || s.input.empty() || s.busy || s.sending) return;
    if (s.page == P::chat) {
        const auto message = s.input; const auto generation = aurelia_page_generation_; s.sending = true;
        aurelia::Request request{C::chat_send}; request.id = s.friend_id; request.text = message;
        aureliaRequest(request, [generation, message](Launcher& app, aurelia::Reply reply) {
            if (!app.aurelia_ui_.open || app.aurelia_page_generation_ != generation) return;
            auto& s = app.aurelia_ui_; s.sending = false;
            if (reply.ok && aurelia::flag(reply.data, "sent")) {
                const auto timestamp = aurelia::integer(reply.data.get("server_timestamp")).value_or(0);
                auto delivered = wideLenient(aurelia::text(reply.data, "modified_message")); if (delivered.empty()) delivered = message;
                if (std::none_of(s.messages.begin(), s.messages.end(), [&](const auto& old) { return old.self && old.timestamp == timestamp && old.text == delivered; }))
                    s.messages.push_back({timestamp, true, delivered});
                if (s.input == message) { s.input.clear(); app.aurelia_chat_drafts_.erase(s.friend_id); }
                s.chat_follow = true; s.status = tr(L"Wysłano");
            } else s.status = reply.error.empty() ? tr(L"Nie udało się wysłać wiadomości. Tekst zachowano.") : reply.error;
            InvalidateRect(app.hwnd_, nullptr, FALSE);
        });
    } else if (s.page == P::versions) aureliaActivate({tr(L"Zainstalować i przypiąć wskazany manifest?"), tr(L"Starsza wersja może nie obsługiwać nowszych zapisów gry."), {}, A::downgrade, 0, s.input});
    else if (s.page == P::libraries) {
        const std::filesystem::path path(s.input);
        if (!path.is_absolute()) { s.status = tr(L"Podaj pełną ścieżkę biblioteki"); return; }
        aureliaActivate({{}, {}, {}, A::choose_library, 0, s.input});
    } else if (s.page == P::workshop) {
        auto input = s.input; A action = A::workshop_install;
        if (input.starts_with(L"+")) { action = A::workshop_subscribe; input.erase(0, 1); }
        else if (input.starts_with(L"-")) { action = A::workshop_unsubscribe; input.erase(0, 1); }
        const auto value = json::string(text_encoding::utf8(input)); const auto id = aurelia::integer(&value);
        if (id && *id) aureliaActivate({{}, {}, {}, action, *id});
        else s.status = tr(L"Wpisz poprawny identyfikator Workshop");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaCharacter(wchar_t c) {
    if (!aureliaTyping() || c < 32 || c == 127 || aurelia_ui_.input.size() >= 4000) return;
    aurelia_ui_.input.push_back(c); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::aureliaScroll(int rows) {
    auto& s = aurelia_ui_; if (!s.open || s.confirmation) return;
    const auto v = renderer_.viewport(); const aurelia_ui::Layout layout{v.width, v.height};
    if (s.page == P::chat) {
        const auto area = layout.log(); const float maximum = std::max(0.0f, s.chat_content - (area.bottom - area.top));
        s.chat_scroll = std::clamp(s.chat_scroll + rows * 60.0f, 0.0f, maximum); s.chat_follow = s.chat_scroll >= maximum;
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    const int count = s.page == P::chat ? static_cast<int>(s.messages.size()) : static_cast<int>(s.rows.size());
    s.first = std::clamp(s.first + rows, 0, std::max(0, count - (s.page == P::chat ? 1 : layout.visible(s))));
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::aureliaKey(WPARAM key, bool repeated) {
    auto& s = aurelia_ui_; if (!s.open) return;
    if (s.confirmation) {
        if (key == VK_ESCAPE || key == VK_BACK) s.confirmation.reset();
        else if (key == VK_LEFT || key == VK_RIGHT || key == VK_TAB) s.hover = s.hover == -5 ? -4 : -5;
        else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) {
            const auto row = *s.confirmation; s.confirmation.reset(); if (s.hover == -5) aureliaActivate(row, true);
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (key == VK_ESCAPE || (key == VK_BACK && !s.input_focus)) {
        if (s.page == P::setup) closeAurelia();
        else if (s.page == P::chat) aureliaPage(P::friends);
        else if (s.page == P::game || s.page == P::friends) closeAurelia();
        else aureliaPage(P::game);
        return;
    }
    if (key == VK_F5 && !repeated) { if (s.page == P::chat) aureliaOpenChat(s.friend_id, s.friend_name); else aureliaPage(s.page); return; }
    if (key == VK_TAB && !s.input_hint.empty()) { s.input_focus = !s.input_focus; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (key == VK_PRIOR || key == VK_NEXT) { aureliaScroll(key == VK_PRIOR ? -4 : 4); return; }
    if (s.input_focus) {
        if (key == VK_BACK && !s.input.empty()) {
            const auto last = s.input.back(); s.input.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !s.input.empty() && s.input.back() >= 0xd800 && s.input.back() <= 0xdbff) s.input.pop_back();
        } else if (key == VK_RETURN && !repeated) aureliaSubmit();
        else if (key == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && OpenClipboard(hwnd_)) {
            if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
                if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                    const auto count = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 4001);
                    for (size_t i = 0; i < count && text[i] && s.input.size() < 4000; ++i) if (text[i] >= 32) s.input.push_back(text[i]);
                    GlobalUnlock(handle);
                }
            }
            CloseClipboard();
        } else if (key == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { /* Append-only field: keep its text. */ }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (key == VK_UP || key == VK_DOWN) {
        s.focus = std::clamp(s.focus + (key == VK_UP ? -1 : 1), 0, std::max(0, static_cast<int>(s.rows.size()) - 1));
        const auto v = renderer_.viewport(); const aurelia_ui::Layout layout{v.width, v.height};
        if (s.focus < s.first) s.first = s.focus;
        if (s.focus >= s.first + layout.visible(s)) s.first = s.focus - layout.visible(s) + 1;
    } else if ((key == VK_RETURN || key == VK_SPACE) && !repeated && s.focus >= 0 && s.focus < static_cast<int>(s.rows.size())) aureliaActivate(s.rows[s.focus]);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::aureliaClick(float x, float y) {
    auto& s = aurelia_ui_; const auto v = renderer_.viewport(); const aurelia_ui::Layout layout{v.width, v.height};
    const auto hit = layout.hit(s, x, y);
    if (hit == -1) { aureliaKey(VK_ESCAPE, false); return; }
    if (s.confirmation) {
        if (hit == -4) s.confirmation.reset();
        else if (hit == -5) { const auto row = *s.confirmation; s.confirmation.reset(); aureliaActivate(row, true); }
    } else if (hit == -2) { s.input_focus = true; if (!desktop_mode_ && pad_input_) openKeyboard(); }
    else if (hit == -3) aureliaSubmit();
    else if (hit >= 0) { s.input_focus = false; s.focus = hit; aureliaActivate(s.rows[hit]); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

#include "companion_fixture.hpp"
#include "../src/net/json.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/net/companion_links.hpp"
#include <ole2.h>

// Phone access (companion_remote.cpp): the same conversation from the phone while this screen
// shows the home, her reply whole (no chat screen to reveal it), the key and the real HTTP path.
int LauncherUiTest::companionRemote(HWND window) {
    const companion_fixture::Cast cast;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.companion = true; app.settings_.companion_look = "hana"; app.settings_.companion_guests = false;
    app.settings_.companion_memory = app.settings_.companion_notes = false;
    app.settings_.companion_voice = AppSettings::CompanionVoice::off;
    app.settings_.companion_provider = AppSettings::CompanionProvider::ollama;
    app.settings_.companion_ollama_host = L"http://127.0.0.1:11434/"; app.settings_.companion_ollama_model = "fixture";
    app.settings_.screen_transitions = false;
    app.companion_.message_cap = companion::chats::message_cap;
    app.companion_.clock = [] { SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 20; t.wHour = 15; return t; };
    app.companion_.chat_look = companion_fixture::hana;
    app.companion_.chat_started = app.companion_.chat_updated = app.companionNow();
    app.companion_.turns = {{true, L"Opening"}};
    auto& chat = app.companion_.chat;
    chat.log.push_back({false, L"Hej!"}); chat.reveal = 4;
    if (!app.renderer_.initialize(window)) return 9600;
    const DWORD ui_thread = GetCurrentThreadId();
    std::atomic_bool hold{false};
    app.companion_.post = [&hold, ui_thread](const std::wstring&, const std::wstring&, const std::string&) {
        if (GetCurrentThreadId() == ui_thread) return HttpResponse{};
        for (const auto end = GetTickCount64() + 2000; hold.load() && GetTickCount64() < end; Sleep(2)) {}
        const std::string answer = R"({"message":{"role":"assistant","content":"[smile]Pierwsza myśl.\n[thinking]Druga myśl."},"done":true})";
        return HttpResponse{200, {answer.begin(), answer.end()}};
    };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
            app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
        while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
    };
    const auto wait = [&] {
        for (const auto end = GetTickCount64() + 4000; GetTickCount64() < end; Sleep(5)) { pump(); if (!app.companion_.busy) return true; }
        return false;
    };
    app.screen_ = ScreenMode::shelves;
    auto state = app.remoteState();
    if (!json::parse(state) || state.find("\"look\":\"hana\"") == state.npos || state.find("Hej!") == state.npos ||
        state.find("\"waiting\":false") == state.npos || state.find("\"name\":\"Rin\"") == state.npos) return 9601;
    if (app.remoteSend(L"  ").find("\"ok\":false") == std::string::npos || chat.log.size() != 1) return 9602;
    hold.store(true);
    if (app.remoteSend(L"Cześć z telefonu").find("\"ok\":true") == std::string::npos || !chat.waiting || app.companionOpen()) return 9603;
    if (app.remoteSend(L"Jeszcze?").find("\"why\":\"waiting\"") == std::string::npos) return 9604;
    hold.store(false);
    if (!wait()) return 9605;
    state = app.remoteState();
    if (state.find("Cześć z telefonu") == state.npos || state.find("Pierwsza myśl.") == state.npos || state.find("Druga myśl.") == state.npos ||
        !chat.queue.empty() || state.find("\"emotion\":\"thinking\"") == state.npos || app.companion_.turns.back().user) return 9606;
    if (app.remoteLook("nobody").find("\"ok\":false") == std::string::npos) return 9607;
    // The server itself: the key, the page and her state over HTTP, the UI thread answering.
    app.remote_loopback_ = true;
    app.settings_.companion_remote = true;
    for (int port = 46980; port < 47000 && (!app.remote_ || app.remote_failed_); ++port) { app.settings_.companion_remote_port = port; app.companionRemoteSync(); }
    if (!app.remote_ || app.remote_failed_ || app.settings_.companion_remote_key.size() != 12) return 9608;
    const auto base = L"http://127.0.0.1:" + std::to_wstring(app.settings_.companion_remote_port);
    const auto key = text_encoding::wide(app.settings_.companion_remote_key);
    std::atomic_bool done{false};
    HttpResponse denied, page, remote_state;
    std::thread phone([&] {
        denied = httpLocal(base + L"/api/state?k=wrong", L"", nullptr, 4096, 3000);
        page = httpLocal(base + L"/", L"", nullptr, 256 * 1024, 3000);
        remote_state = httpLocal(base + L"/api/state?k=" + key, L"", nullptr, 256 * 1024, 5000);
        done.store(true);
    });
    for (const auto end = GetTickCount64() + 8000; !done.load() && GetTickCount64() < end; Sleep(5)) pump();
    phone.join();
    const std::string html(page.body.begin(), page.body.end()), body(remote_state.body.begin(), remote_state.body.end());
    if (denied.status != 403 || page.status != 200 || html.find("/api/state") == html.npos || remote_state.status != 200 || body.find("Druga myśl.") == body.npos) return 9609;
    app.settings_state_.category = static_cast<int>(settings_ui::Category::companion); app.settings_state_.options = true;
    app.screen_ = ScreenMode::settings;
    app.buildSettingsRows();
    // Asystent → "Dostęp z telefonu" (a tile): its page has the switch and one tile with every address.
    if (!app.revealSetting(settings_ui::Id::companion_remote) || !app.settings_state_.at(settings_ui::Page::companion_remote)) return 9610;
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& each) { return each.id == settings_ui::Id::companion_remote; });
    if (row == app.settings_rows_.end() || !row->checked || row->control != settings_ui::Control::toggle) return 9610;
    if (std::count_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& each) { return each.id == settings_ui::Id::companion_remote_address; }) != 1) return 9612;
    {
        const auto& addresses = app.settings_rows_.back();
        const size_t lines = std::count(addresses.description.begin(), addresses.description.end(), L'\n') + 1;
        if (addresses.id != settings_ui::Id::companion_remote_address || (!addresses.locked &&
            (!addresses.key.starts_with("http://") || static_cast<size_t>(addresses.lines) < lines))) return 9613;
    }
    {
        // Every address in that one taller tile, Tailscale first (what its button copies).
        app.remote_addresses_ = {{remote::Address::Kind::local, L"192.168.1.20"}, {remote::Address::Kind::tailscale, L"100.64.1.2"}};
        app.remote_public_ = L"203.0.113.7";
        app.buildSettingsRows();
        const auto& addresses = app.settings_rows_.back();
        const auto suffix = L":" + std::to_wstring(app.settings_.companion_remote_port) + L"/?k=" + key;
        if (addresses.id != settings_ui::Id::companion_remote_address || addresses.lines != 3 ||
            addresses.key != text_encoding::utf8(L"http://100.64.1.2" + suffix) || addresses.description.find(L"Tailscale: http://100.64.1.2") != 0 ||
            addresses.description.find(L"http://192.168.1.20" + suffix) == std::wstring::npos ||
            addresses.description.find(L"http://203.0.113.7" + suffix) == std::wstring::npos) return 9614;
        const auto view = app.renderer_.viewport();
        const auto layout = settings_ui::Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
        const int tile = static_cast<int>(app.settings_rows_.size()) - 1;
        if (layout.rowHeight(tile) <= settings_ui::Layout::row_height || layout.rowHeight(0) != settings_ui::Layout::row_height) return 9615;
        app.settings_state_.selected[app.settings_state_.category] = tile;
        wchar_t folder[4096]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) {
            app.notifications_.clear();
            if (!app.renderer_.captureForTest(std::filesystem::path(folder) / L"companion-remote-settings.png", [&] { app.paint(); })) return 9616;
        }
        app.remote_addresses_.clear(); app.remote_public_.clear();
    }
    app.settings_.companion_remote = false;
    app.companionRemoteSync();
    if (app.remote_) return 9611;
    return 0;
}

int LauncherUiTest::companionPaste(HWND window) {
    // Exercise the actual Win32 clipboard/key state and restore them on every exit.
    struct ClipboardScope {
        IDataObject* previous = nullptr;
        BYTE keys[256]{};
        bool initialized = SUCCEEDED(OleInitialize(nullptr));
        ClipboardScope() { GetKeyboardState(keys); if (initialized) OleGetClipboard(&previous); }
        ~ClipboardScope() {
            SetKeyboardState(keys);
            if (initialized) {
                OleSetClipboard(previous);
                if (previous) { OleFlushClipboard(); previous->Release(); }
                OleUninitialize();
            }
        }
    } saved;
    if (!saved.initialized) return 8250;
    const auto clipboard = [&](const std::wstring& text) {
        bool opened = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (OpenClipboard(window)) { opened = true; break; }
            Sleep(5);
        }
        if (!opened) return false;
        bool ok = false;
        if (EmptyClipboard()) {
            const auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
            if (memory) {
                if (auto* data = static_cast<wchar_t*>(GlobalLock(memory))) {
                    std::copy_n(text.c_str(), text.size() + 1, data); GlobalUnlock(memory);
                    ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                }
                if (!ok) GlobalFree(memory);
            }
        }
        CloseClipboard(); return ok;
    };
    Launcher app; app.hwnd_ = window;
    app.screen_ = ScreenMode::companion;
    auto& chat = app.companion_.chat;
    chat.focus = companion_ui::focus_input;
    const auto key = [&](WPARAM value, bool control, bool shift = false, bool alt = false, bool repeated = false) {
        BYTE keys[256]{};
        keys[VK_CONTROL] = control ? 0x80 : 0;
        keys[VK_SHIFT] = shift ? 0x80 : 0;
        keys[VK_MENU] = alt ? 0x80 : 0;
        SetKeyboardState(keys);
        return app.companionPasteKey(value, repeated);
    };
    for (bool desktop : {false, true}) {
        app.desktop_mode_ = desktop;
        const std::wstring link = L"https://youtu.be/AbCdEF_12-3?si=test&x=1";
        if (!clipboard(link)) return 8251;
        chat.input = L"Zobacz: ";
        if (!key('V', true) || chat.input != L"Zobacz: " + link || chat.waiting || !chat.log.empty()) return 8252;
        if (!key('V', true, false, false, true) || chat.input != L"Zobacz: " + link) return 8253;
        app.onCharacter(22); // TranslateMessage's Ctrl+V WM_CHAR must not paste a second time.
        if (chat.input != L"Zobacz: " + link || key('V', false) || key('V', true, false, true)) return 8254;
        chat.input.clear();
        if (!key(VK_INSERT, false, true) || chat.input != link) return 8255;
        if (!clipboard(L"Zażółć\r\ngęślą\nogonek\t\U0001F3AE\x01")) return 8256;
        chat.input.clear(); app.companionPaste(); // WM_PASTE's common path.
        if (chat.input != L"Zażółć gęślą ogonek \U0001F3AE" || chat.waiting) return 8257;
        app.companionErase();
        if (chat.input != L"Zażółć gęślą ogonek ") return 8258;
        if (!clipboard(std::wstring(2000, L'x'))) return 8259;
        chat.input = L"prefix"; app.companionPaste();
        if (chat.input.size() != 600 || !chat.input.starts_with(L"prefix")) return 8260;
        if (!clipboard(L"\U0001F3AEextra")) return 8261;
        chat.input.assign(599, L'x'); app.companionPaste();
        if (chat.input.size() != 599) return 8262;
        chat.input.assign(598, L'x'); app.companionPaste();
        if (chat.input.size() != 600 || !chat.input.ends_with(L"\U0001F3AE")) return 8263;
        for (bool* blocked : {&chat.menu, &chat.confirm, &chat.ended, &chat.archive, &chat.viewer, &chat.mind, &app.friends_open_, &app.companion_.picker.open}) {
            chat.input = L"Keep"; *blocked = true;
            const bool handled = key('V', true); app.companionPaste();
            *blocked = false;
            if (handled || chat.input != L"Keep") return 8264;
        }
        app.exit_prompt_ = 0;
        const bool handled = key('V', true); app.companionPaste(); app.exit_prompt_.reset();
        if (handled || chat.input != L"Keep") return 8268;
        chat.focus = companion_ui::focus_more;
        if (key('V', true)) return 8265;
        app.companionPaste(); if (chat.input != L"Keep") return 8266;
        chat.focus = companion_ui::focus_input;
    }
    auto& feed = app.companion_.feed.view;
    chat.feed = true; feed.app_age = 3; feed.entrance.snap(1); feed.thread = "fixture"; feed.focus = 3;
    feed.input.assign(298, L'x'); app.companionPaste();
    if (feed.input.size() != 300 || !feed.input.ends_with(L"\U0001F3AE") || chat.input != L"Keep") return 8267;
    return 0;
}

int LauncherUiTest::companionLinks(HWND window) {
    const companion_fixture::Cast cast;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.companion = true; app.settings_.companion_look = "hana"; app.settings_.companion_guests = false;
    app.settings_.companion_memory = app.settings_.companion_notes = false;
    app.settings_.companion_voice = AppSettings::CompanionVoice::off;
    app.settings_.companion_provider = AppSettings::CompanionProvider::ollama;
    app.settings_.companion_ollama_host = L"http://127.0.0.1:11434/"; app.settings_.companion_ollama_model = "fixture";
    app.settings_.screen_transitions = false;
    app.companion_.clock = [] { SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 20; t.wHour = 15; return t; };
    app.companion_.chat_look = companion_fixture::hana;
    app.companion_.chat_started = app.companion_.chat_updated = app.companionNow();
    app.companion_.turns = {{true, L"Opening"}};
    auto& chat = app.companion_.chat;
    chat.log.push_back({false, L"Hej!"}); chat.reveal = 4;
    if (!app.renderer_.initialize(window)) return 8200;
    struct Seen { std::mutex mutex; std::string body; std::atomic_int reads{0}; std::atomic_bool wrong_thread{false}; };
    const auto seen = std::make_shared<Seen>();
    const DWORD ui_thread = GetCurrentThreadId();
    app.companion_.link_get = [seen, ui_thread](const std::wstring& url, size_t) {
        ++seen->reads;
        if (GetCurrentThreadId() == ui_thread) seen->wrong_thread = true;
        const auto text = url.find(L"oembed") != url.npos ? std::string(R"({"title":"Fixture film","author_name":"Fixture channel"})") :
            url.find(L"youtube") != url.npos ? std::string(R"(<script>{"videoDetails":{"videoId":"AbCdEF_12-3","title":"Fixture film","author":"Fixture channel","shortDescription":"Fixture description"}}</script>)") :
            "<title>Fixture article</title><article>" + std::string(12000, 'x') + "</article>";
        return HttpResponse{200, {text.begin(), text.end()}};
    };
    app.companion_.post = [seen](const auto&, const auto&, const std::string& body) {
        { std::lock_guard lock(seen->mutex); seen->body = body; }
        const std::string answer = R"({"message":{"role":"assistant","content":"[smile]Ciekawy materiał."},"done":true})";
        return HttpResponse{200, {answer.begin(), answer.end()}};
    };
    const auto body = [&] { std::lock_guard lock(seen->mutex); return seen->body; };
    const auto wait = [&] {
        for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end; Sleep(5)) {
            MSG message{};
            while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
            if (!app.companion_.busy) return true;
        }
        return false;
    };
    app.screen_ = ScreenMode::companion;
    app.companionSend(L"Zobacz https://example.com/article i https://youtu.be/AbCdEF_12-3");
    if (!wait() || seen->wrong_thread || seen->reads != 3) return 8201;
    if (body().find("Fixture description") == std::string::npos || body().find("LINK_PREVIEWS") == std::string::npos || body().find(std::string(1600, 'x')) != std::string::npos) return 8202;
    for (const auto& turn : app.companion_.turns) if (turn.text.find(L"LINK_PREVIEWS") != turn.text.npos) return 8203;
    for (const auto& message : chat.log) if (message.text.find(L"LINK_PREVIEWS") != message.text.npos) return 8204;
    app.companionFlush(); app.companionSend(L"Co o tym myślisz?");
    if (!wait() || seen->reads != 3 || body().find("LINK_PREVIEWS") != std::string::npos) return 8205;
    app.companionFlush();
    chat.log = {{true, L"https://youtu.be/AbCdEF_12-3"}}; chat.scroll = 0; chat.scroll_motion.snap(0); chat.age = 4;
    app.paint();
    const auto viewport = app.renderer_.viewport();
    std::optional<D2D1_POINT_2F> card;
    for (float y = 0; y < viewport.height && !card; y += 4)
        for (float x = 0; x < viewport.width; x += 4)
            if (!app.renderer_.companionVideoAt(x, y).empty()) { card = D2D1::Point2F(x, y); break; }
    if (!card) return 8206;
    wchar_t capture_folder[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", capture_folder, 4096) &&
        !app.renderer_.captureForTest(std::filesystem::path(capture_folder) / L"companion-youtube-card.png", [&] { app.paint(); })) return 8214;
    std::wstring opened;
    app.browse_ = [&](const auto& url) { opened = url; };
    app.companionLinkClick(card->x, card->y);
    if (opened != companion::links::playerUrl("AbCdEF_12-3")) return 8207;
    app.desktop_mode_ = true;
    app.companionVideo(L"https://youtu.be/AbCdEF_12-3");
    if (opened != L"https://www.youtube.com/watch?v=AbCdEF_12-3") return 8208;
    app.desktop_mode_ = false;
    app.companionMenu(true);
    const auto item = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& m) { return m.kind == companion_ui::MenuItem::Kind::video; });
    if (item == chat.menu_items.end()) return 8209;
    opened.clear(); app.companionMenuPick(static_cast<int>(item - chat.menu_items.begin()));
    if (chat.menu || opened != companion::links::playerUrl("AbCdEF_12-3")) return 8210;
    chat.scroll = 10000; chat.scroll_motion.snap(10000); app.paint();
    if (!app.renderer_.companionVideoAt(card->x, card->y).empty()) return 8211;
    // Desktop must really use WebView2 too, not ShellExecute (even without a runtime).
    app.browse_ = {}; app.desktop_mode_ = true;
    app.browser_ = std::make_unique<BrowserWindow>(); app.browser_->testPage(L"", true);
    app.companionVideo(L"https://youtu.be/AbCdEF_12-3");
    if (!app.browser_->isOpen() || app.browser_->status() != BrowserWindow::Status::missing_runtime || app.browser_->urlForTest() != L"https://www.youtube.com/watch?v=AbCdEF_12-3") return 8212;
    app.browser_->close(); app.desktop_mode_ = false;
    if (!IsWindowEnabled(window)) return 8213;
    return 0;
}

int LauncherUiTest::companionReplyLength(HWND window) {
    const companion_fixture::Cast cast;
    using Length = companion::ReplyLength;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.companion = true; app.settings_.companion_look = "hana"; app.settings_.companion_guests = false;
    app.settings_.companion_memory = app.settings_.companion_notes = false;
    app.settings_.companion_voice = AppSettings::CompanionVoice::off;
    app.settings_.companion_provider = AppSettings::CompanionProvider::ollama;
    app.settings_.companion_ollama_host = L"http://127.0.0.1:11434/"; app.settings_.companion_ollama_model = "fixture";
    app.settings_.screen_transitions = false;
    app.companion_.message_cap = companion::chats::message_cap;
    app.companion_.clock = [] { SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 20; t.wHour = 15; return t; };
    app.companion_.chat_look = companion_fixture::hana;
    app.companion_.chat_started = app.companion_.chat_updated = app.companionNow();
    app.companion_.turns = {{true, L"Opening"}};
    auto& chat = app.companion_.chat;
    chat.log.push_back({false, L"Hej!"}); chat.reveal = 4;
    if (!app.renderer_.initialize(window)) return 8100;
    struct Seen { std::mutex mutex; std::string body; std::atomic_bool hold{false}, entered{false}; };
    const auto seen = std::make_shared<Seen>();
    const DWORD ui_thread = GetCurrentThreadId();
    app.companion_.post = [seen, ui_thread](const std::wstring&, const std::wstring&, const std::string& body) {
        if (GetCurrentThreadId() == ui_thread) return HttpResponse{};
        { std::lock_guard lock(seen->mutex); seen->body = body; }
        seen->entered.store(true);
        for (const auto end = GetTickCount64() + 2000; seen->hold.load() && GetTickCount64() < end; Sleep(2)) {}
        const std::string answer = R"({"message":{"role":"assistant","content":"[smile]Pierwsza myśl.\n[thinking]Druga myśl o [[Portal 2]]."},"done":true})";
        return HttpResponse{200, {answer.begin(), answer.end()}};
    };
    const auto body = [&] { std::lock_guard lock(seen->mutex); return seen->body; };
    const auto wait = [&] {
        for (const auto end = GetTickCount64() + 4000; GetTickCount64() < end; Sleep(5)) {
            MSG message{};
            while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
            if (!app.companion_.busy) return true;
        }
        return false;
    };
    app.screen_ = ScreenMode::settings;
    app.settings_state_.category = static_cast<int>(settings_ui::Category::companion); app.settings_state_.options = true;
    app.buildSettingsRows();
    const auto slider = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::companion_reply_length; });
    if (slider == app.settings_rows_.end() || slider->control != settings_ui::Control::slider || slider->level != 1 || slider->value != L"100%" || settings_ui::sizeSteps(slider->id) != 3) return 8101;
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(slider - app.settings_rows_.begin());
    app.settings_state_.editing = true;
    const auto started = app.companion_.chat_started;
    app.settingsSetLevel(0.51f);
    if (app.settings_.companion_reply_length != Length::concise || app.companionMessageLimit() != 60 || app.companionMessages() != 1) return 8102;
    app.settingsSetLevel(0.1f);
    if (app.settings_.companion_reply_length != Length::brief || app.companionMessageLimit() != 120 || app.companion_.chat_started != started || chat.ended) return 8103;
    app.settingsAdjust(1);
    if (app.settings_.companion_reply_length != Length::concise) return 8104;
    app.settingsAdjust(1);
    if (app.settings_.companion_reply_length != Length::full || app.companionMessageLimit() != 40 || app.companion_.turns.size() != 1) return 8105;
    app.settingsSetLevel(0);
    app.saveSettings();
    if (loadAppSettings().companion_reply_length != Length::brief) return 8106;
    app.ensureSettingVisible();
    wchar_t captures[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", captures, 4096) &&
        !app.renderer_.captureForTest(std::filesystem::path(captures) / L"companion-reply-length-settings.png", [&] { app.paint(); })) return 8107;
    // A slider change doesn't cancel or re-split the reply already being generated.
    app.screen_ = ScreenMode::companion;
    app.companion_.burst_in = 0; seen->hold.store(true);
    app.companionSend(L"Hej");
    const auto generation = app.companion_.generation;
    app.settingsSetLevel(1);
    if (!app.companion_.busy || app.companion_.generation != generation || app.companion_.reply_length != Length::brief) return 8108;
    seen->hold.store(false);
    if (!wait() || chat.log.size() != 3 || !chat.queue.empty() || chat.log.back().text.find(L"Druga myśl") == std::wstring::npos || chat.log.back().mentions.empty()) return 8109;
    auto request = body();
    const auto parsed = json::parse(request);
    if (request.find("17 słów") == request.npos || request.find("sila_uczucia") == request.npos || request.find("komunikatorze") != request.npos ||
        !parsed || !parsed->get("options") || parsed->get("options")->get("num_predict")->intOr() < companion::replyStyle(Length::brief).tokens(420) + 300 + companion::thought_room) return 8110;
    app.companionFlush(); app.companionSend(L"Jeszcze raz");
    if (!wait() || body().find("50 słów") == std::string::npos || app.companion_.reply_length != Length::full) return 8111;
    app.companionFlush();
    // "Pisze…" (Settings): no words appearing one by one — her reply waits whole behind the typing
    // dots, then each message appears at once.
    {
        app.buildSettingsRows();
        if (std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::companion_typing && row.control == settings_ui::Control::toggle; }))
            return 8117;
        // Who she talks with: your name and gender in Settings, named in her prompt — never "gracz".
        if (std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::companion_user_name && row.control == settings_ui::Control::text; }) ||
            std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::companion_user_gender && row.control == settings_ui::Control::choice && row.choices.size() == 3; }))
            return 8120;
        app.settings_.companion_user_name = L"Marek";
        app.settings_.companion_user_gender = AppSettings::CompanionUserGender::male;
        app.settings_.companion_typing = true;
        app.companionSend(L"Pisze test");
        if (!wait() || chat.queue.size() != 2 || chat.log.empty() || !chat.log.back().user || chat.revealing()) return 8118;
        if (body().find("Z kim piszesz: Marek") == std::string::npos || body().find("To m") == std::string::npos) return 8121;
        app.settings_.companion_user_name.clear();
        app.settings_.companion_user_gender = AppSettings::CompanionUserGender::automatic;
        for (int i = 0; i < 400 && !chat.queue.empty(); ++i) app.tickCompanion(0.05f);
        if (!chat.queue.empty() || chat.log.back().user || chat.reveal != chat.log.back().text.size() || chat.log.back().text.find(L"Druga") == std::wstring::npos) return 8119;
        app.settings_.companion_typing = false;
        app.companionFlush();
    }
    // Already beyond the standard cap: brief keeps going; selecting standard ends naturally on
    // the next answer, rather than clearing history or cancelling an existing rest period.
    chat.log.assign(41, companion_ui::Message{true, L"Wcześniejsza wiadomość"}); chat.queue.clear();
    app.settingsSetLevel(0); app.companionSend(L"Krótko dalej");
    if (app.companion_.closing || !wait() || chat.ended) return 8112;
    app.companionFlush();
    const auto count = app.companionMessages();
    app.settingsSetLevel(1);
    if (app.companionMessages() != count || chat.ended) return 8113;
    app.companionSend(L"Ostatnia");
    if (!app.companion_.closing || !wait() || !chat.ended || !app.companionResting()) return 8114;
    const auto rest = app.companion_.rest_until;
    app.settingsSetLevel(0);
    if (!chat.ended || app.companion_.rest_until != rest || !app.companionResting()) return 8115;
    // Preserve the opening and correct the summarized offset when the bounded context fills.
    app.companion_.turns = {{true, L"ORIGINAL OPENING"}};
    for (int i = 1; i <= 170; ++i) app.companion_.turns.push_back({i % 2 == 0, L"turn " + std::to_wstring(i)});
    app.companion_.summarized = 100;
    app.companionTrimTurns();
    if (app.companion_.turns.size() != companion::chats::context_turn_limit - 1 || app.companion_.turns.front().text != L"ORIGINAL OPENING" ||
        app.companion_.turns.back().text != L"turn 170" || app.companion_.summarized != 88) return 8116;
    return 0;
}

int LauncherUiTest::companionAppearance(HWND window) {
    const companion_fixture::Cast cast;
    const auto hana = companion_fixture::hana, rin = companion_fixture::rin;
    // An unavailable character must never be offered by the picker.
    auto characters = companion_fixture::cast();
    auto soon = characters.front(); soon.key = "soon"; soon.name = L"Soon"; soon.soon = true;
    characters.push_back(soon); companion::setCharacters(std::move(characters));
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.companion = true; app.settings_.companion_look = "hana";
    app.settings_.companion_memory = app.settings_.companion_notes = false;
    app.settings_.companion_voice = AppSettings::CompanionVoice::off;
    app.settings_.launch_animation = app.settings_.screen_transitions = false;
    app.companion_.persist = true;
    app.companion_.clock = [] { SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 17; t.wHour = 16; return t; };
    app.desktop_mode_ = true;
    if (!app.renderer_.initialize(window)) return 7920;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games.push_back(Game{203, L"Game 3"});
    app.onLibrary(std::move(library)); app.showCategory(0);
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    for (const auto look : {hana, rin}) {
        companion::chats::Conversation conversation;
        conversation.look = look; conversation.started = app.companionNow() - 600;
        conversation.updated = app.companionNow();
        const auto parsed = companion::parseText(L"[smile]Zagraj w [[Game 3]]!");
        companion::chats::Line line; line.speaker = look; line.text = parsed.text; line.emotions = parsed.emotions; line.mentions = parsed.mentions;
        conversation.lines.push_back(line);
        conversation.turns = {{true, L"Hej"}, {false, parsed.raw}};
        if (!companion::chats::write(userConfigDirectory(), conversation)) return 7921;
    }
    if (!capture(L"desktop-companion-more.png")) return 7922;
    const auto more = app.desktopLayout().companionMore();
    const float mx = (more.left + more.right) * 0.5f, my = (more.top + more.bottom) * 0.5f;
    if (app.desktopHit(mx, my).target != desktop_ui::Target::companion_more ||
        app.desktopCursor(mx * app.desktopScale(), my * app.desktopScale()) != IDC_HAND) return 7923;
    app.click(mx * app.desktopScale(), my * app.desktopScale(), false);
    app.desktopPointerUp(mx * app.desktopScale(), my * app.desktopScale());
    app.pointerUp();
    if (!app.desktop_menu_ || app.desktop_menu_->kind != desktop_ui::Menu::Kind::companion ||
        app.desktop_menu_->items.size() != 3 || app.desktop_menu_->items[0].label != L"Zmień" ||
        app.desktop_menu_->items[1].label != L"Umysł i pamięć" || app.desktop_menu_->items[2].label != L"Ustawienia" ||
        app.desktop_menu_->items[2].action != desktop_ui::Action::companion_settings || !app.desktop_menu_->items[2].enabled) return 7924;
    if (!capture(L"desktop-companion-menu.png")) return 7925;
    app.desktopMenuKey(VK_DOWN); app.desktopMenuKey(VK_RETURN);
    if (!app.companion_.picker.open || app.companion_.picker.choices.size() != 2 || app.companionOpen() ||
        app.topModal() != Launcher::Modal::companion_picker) return 7926;
    if (!capture(L"desktop-companion-picker.png")) return 7927;
    app.modalKey(VK_RIGHT, false); app.modalKey(VK_ESCAPE, false);
    if (app.companion_.picker.open || app.companionLook() != hana || !app.desktopActive()) return 7928;
    { // "…" → Ustawienia: Asystent, its heading at the top of the list (the sheet's tick keeps it there).
        app.click(mx * app.desktopScale(), my * app.desktopScale(), false);
        app.desktopPointerUp(mx * app.desktopScale(), my * app.desktopScale());
        app.pointerUp();
        if (!app.desktop_menu_ || app.desktop_menu_->items.size() != 3) return 7946;
        app.desktopMenuPick(app.desktop_menu_->items[2], false);
        if (app.desktop_menu_ || !app.desktopSheet() || app.screen_ != ScreenMode::settings ||
            app.settings_state_.category != static_cast<int>(settings_ui::Category::companion) || !app.settings_state_.options) return 7947;
        const int row = app.settings_state_.row();
        if (row < 0 || row >= static_cast<int>(app.settings_rows_.size()) || app.settings_rows_[static_cast<size_t>(row)].id != settings_ui::Id::companion ||
            app.settings_rows_[static_cast<size_t>(row)].section != L"Towarzyszka AI") return 7948;
        for (int i = 0; i < 5; ++i) app.tickDesktopSheet(0.05f);
        const auto L = app.desktopSettingsLayout();
        const float top = L.row(row, app.desktop_sheet_scroll_).top;
        const bool at_top = std::abs(top - (L.listTop() + desktop_ui::settings_sheet::section_gap - 12)) < 0.5f;
        if (app.desktop_sheet_scroll_ != 0 || app.desktop_sheet_scroll_ != app.desktop_sheet_scroll_target_ ||
            (!at_top && app.desktop_sheet_scroll_ < L.maxScroll()) || top < L.listTop() || top > L.listBottom()) return 7949;
        if (!capture(L"desktop-companion-settings.png")) return 7950;
        app.desktopSettingsActivate({desktop_ui::SheetHit::Kind::close});
        if (!app.desktopActive()) return 7951;
    }
    app.companionPicker(true); app.paint();
    auto size = app.renderer_.viewport();
    const auto tile = companion_ui::PickerLayout{size.width, size.height}.tile(app.companion_.picker, 1);
    app.click((tile.left + tile.right) * 0.5f * size.scale, (tile.top + tile.bottom) * 0.5f * size.scale, false);
    if (app.companion_.picker.open || app.companionLook() != rin || !app.desktopActive() || loadAppSettings().companion_look != "rin") return 7929;
    app.openCompanion();
    auto& chat = app.companion_.chat;
    if (!app.companionOpen() || app.companion_.chat_look != rin || chat.log.size() != 1 || chat.log[0].speaker != rin) return 7930;
    chat.age = 1; chat.pose_age = 10; chat.focus = companion_ui::focus_input; chat.hover = companion_ui::focus_input;
    if (!capture(L"desktop-companion-input-focus.png")) return 7931;
    chat.focus = companion_ui::focus_send;
    if (!capture(L"desktop-companion-input-hover.png")) return 7932;
    chat.hover = companion_ui::no_hit;
    if (!capture(L"desktop-companion-input-normal.png")) return 7933;
    const auto openFromChat = [&] {
        app.companionMenu(true);
        const auto found = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& item) { return item.kind == companion_ui::MenuItem::Kind::change; });
        if (found == chat.menu_items.end()) return false;
        chat.menu_focus = static_cast<int>(found - chat.menu_items.begin()); app.companionAccept();
        return app.companion_.picker.open;
    };
    chat.focus = companion_ui::focus_input; chat.input = L"Draft";
    if (!openFromChat()) return 7934;
    app.onCharacter(L'X');
    if (chat.input != L"Draft" || app.companionTyping()) return 7935;
    app.back();
    if (app.companion_.picker.open || !app.companionOpen() || chat.input != L"Draft") return 7936;
    // A controller can cancel and choose; a previous character's late response is ignored.
    PadReading pad{true, L"companion-picker-fixture"}; app.controller(pad);
    if (!openFromChat()) return 7937;
    pad.buttons = XINPUT_GAMEPAD_B; app.controller(pad); pad.buttons = 0; app.controller(pad);
    if (app.companion_.picker.open || app.companionLook() != rin) return 7938;
    chat.log.push_back({true, L"Remember Rin"});
    const auto rin_started = app.companion_.chat_started;
    const auto old_generation = app.companion_.generation;
    const auto old_cancel = app.companion_.cancel;
    chat.waiting = true; app.companion_.busy = true;
    if (!openFromChat()) return 7939;
    pad.buttons = XINPUT_GAMEPAD_DPAD_LEFT; app.controller(pad); pad.buttons = 0; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad); pad.buttons = 0; app.controller(pad);
    if (app.companionLook() != hana || !app.companionOpen() || app.companion_.picker.open || app.companion_.chat_look != hana ||
        chat.log.size() != 1 || chat.log[0].speaker != hana || !old_cancel->load() || app.companion_.generation == old_generation) return 7940;
    auto stale = std::make_unique<CompanionMessage>(); stale->generation = old_generation; stale->purpose = CompanionMessage::Purpose::chat;
    stale->reply = companion::parseText(L"[angry]Old reply"); app.onCompanion(std::move(stale));
    if (chat.log.size() != 1 || chat.log[0].text.find(L"Old") != std::wstring::npos) return 7941;
    bool saved = false;
    for (const auto end = GetTickCount64() + 3000; GetTickCount64() < end; Sleep(5)) {
        const auto previous = companion::chats::read(companion::chats::file(userConfigDirectory(), rin, rin_started));
        if (previous && previous->lines.back().text == L"Remember Rin") { saved = true; break; }
    }
    if (!saved) return 7942;
    app.desktop_mode_ = false; chat.age = 1; chat.pose_age = 10;
    if (!openFromChat() || !capture(L"tv-companion-picker.png")) return 7943;
    app.modalKey(VK_RIGHT, false); app.modalKey(VK_RETURN, false);
    if (app.companionLook() != rin || !app.companionOpen() || chat.log.back().text != L"Remember Rin") return 7944;
    // Paging geometry never exposes tiles from another page.
    companion_ui::Picker many; many.choices.assign(19, hana); many.focus = 8;
    const companion_ui::PickerLayout pages{1600, 900};
    const auto page_tile = pages.tile(many, 8);
    if (pages.hit(many, page_tile.left + 10, page_tile.top + 10) != 8 || pages.tile(many, 0).right != 0) return 7945;
    // Launchfeed replaces the conversation; wheel and pad scroll in both directions, and only
    // the selected profile's response can land. An explicit reference survives in the chat.
    app.companion_.persist = false;
    app.companionFeedReset();
    auto& feed = app.companion_.feed;
    feed.view.loaded = true;
    feed.view.archive.posts = {{"fixture-one", app.companionNow(), 20261017, L"A photoshoot for myself", L"Sesja tylko dla mnie. Może jakiś magazyn mnie wypatrzy? Na razie mam zdjęcia, które po prostu lubię."},
                               {"fixture-two", app.companionNow() - 18000, 20261017, L"Morning coffee", L"Dzisiaj bez pośpiechu. Kawa, słońce i chwila tylko dla siebie."}};
    const HRSRC image_resource = FindResourceW(nullptr, MAKEINTRESOURCEW(companion_ui::spriteId(companion::Look{}, companion_ui::Frame::base, companion::calm)), RT_RCDATA);
    const HGLOBAL handle = image_resource ? LoadResource(nullptr, image_resource) : nullptr;
    if (handle) {
        const auto* bytes = static_cast<const uint8_t*>(LockResource(handle));
        const auto image_size = SizeofResource(nullptr, image_resource);
        for (const auto& post : feed.view.archive.posts) {
            feed.view.images[post.id] = std::make_shared<const std::vector<uint8_t>>(bytes, bytes + image_size);
            feed.requested_images.insert(post.id);
        }
    }
    // Optional read-only portrait preview; normal regression runs use embedded fixtures.
    wchar_t preview_image[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_FEED_PREVIEW_IMAGE", preview_image, 4096)) {
        const auto bytes = companion::photos::read(preview_image);
        if (!bytes) return 7990;
        const auto [w, h] = companion::feed::dimensions(*bytes);
        if (!w || !h) return 7991;
        for (auto& post : feed.view.archive.posts) {
            post.width = w; post.height = h;
            feed.view.images[post.id] = std::make_shared<const std::vector<uint8_t>>(*bytes);
        }
    }
    const auto log_size = chat.log.size();
    app.settings_.screen_transitions = true;
    chat.focus = companion_ui::focus_feed; app.companionAccept();
    if (!chat.feed || app.companionTyping() || feed.view.ready()) return 7960;
    for (int i = 0; i < 4; ++i) app.tickCompanionFeed(0.05f);
    if (feed.view.entrance.value <= 0 || feed.view.entrance.value >= 1 || !capture(L"launchfeed-entrance.png")) return 7980;
    for (int i = 0; i < 20; ++i) app.tickCompanionFeed(0.05f);
    if (feed.view.ready() || !capture(L"launchfeed-splash.png")) return 7981;
    app.companionFeedAction(1);
    if (!feed.view.thread.empty()) return 7982; // Boot screen consumes input.
    for (int i = 0; i < 22; ++i) app.tickCompanionFeed(0.05f);
    if (!feed.view.ready() || !capture(L"launchfeed-phone.png")) return 7983;
    app.settings_.screen_transitions = false;
    app.companionType(L'X');
    if (chat.input == L"X") return 7961;
    app.companionWheel(-WHEEL_DELTA);
    if (feed.view.scroll <= 0) return 7962;
    app.companionMove(0, 1);
    if (feed.view.scroll <= 130 || !capture(L"launchfeed-scroll.png")) return 7963;
    app.companionWheel(WHEEL_DELTA);
    if (feed.view.scroll >= 280) return 7964;
    auto stale_feed = std::make_unique<CompanionFeedMessage>(); stale_feed->generation = feed.generation; stale_feed->look = "hana";
    stale_feed->archive = companion::feed::State{};
    app.onCompanionFeed(std::move(stale_feed));
    if (feed.view.archive.posts.size() != 2) return 7965;
    app.companionFeedAction(1); // Comments, not a private chat.
    if (!app.companionTyping() || feed.view.thread != "fixture-one") return 7984;
    const auto prior_input = chat.input;
    app.companionType(L'A'); app.companionType(L'B'); app.companionErase();
    if (feed.view.input != L"A" || chat.input != prior_input || !capture(L"launchfeed-comment-field.png")) return 7985;
    app.companionFeedBack();
    if (!chat.feed || !feed.view.thread.empty()) return 7986;
    app.companionFeedDiscuss(0);
    if (chat.feed || feed.reference.find(L"photoshoot") == std::wstring::npos || chat.log.size() != log_size || chat.input.empty()) return 7966;
    app.companionFeed(true); app.back();
    if (chat.feed || !app.companionOpen() || chat.focus != companion_ui::focus_feed) return 7967;
    if (feed.view.images.empty()) return 7968;
    const auto sample = feed.view.images.begin()->second;
    // Profile changes clear both its archive and a pending conversation reference.
    app.settings_.companion_look = "hana";
    app.pollCompanionFeed();
    if (!feed.view.archive.posts.empty() || !feed.reference.empty()) return 7969;
    app.settings_.companion_look = "rin";
    // End-to-end publication with a fixture model: TXT is source material, never copied as the
    // finished caption; the request is off the UI thread and repeated opening consumes no slot.
    const auto source = userConfigDirectory() / L"feed-source";
    if (!sample || !atomic_file::write(source / L"rin" / L"1.png", *sample) ||
        !atomic_file::write(source / L"rin" / L"1.txt", "A photoshoot for myself. Maybe a magazine will hire me.")) return 7970;
    app.settings_.companion_feed_folder = source.wstring();
    app.settings_.companion_provider = AppSettings::CompanionProvider::ollama;
    app.settings_.companion_ollama_host = L"http://127.0.0.1:11434/";
    app.settings_.companion_ollama_model = "fixture";
    app.companion_.persist = true;
    auto calls = std::make_shared<std::atomic_int>(0);
    auto described = std::make_shared<std::atomic_bool>(false);
    const auto ui_thread = GetCurrentThreadId();
    app.companion_.post = [calls, described, ui_thread](const std::wstring&, const std::wstring&, const std::string& body) {
        ++*calls;
        described->store(body.find("photoshoot for myself") != std::string::npos && GetCurrentThreadId() != ui_thread);
        HttpResponse response; response.status = 200;
        const std::string answer = body.find("Launchfeed comments") == std::string::npos
            ? R"({"message":{"role":"assistant","content":"[smile]Moj nowy post z sesji."},"done":true})"
            : R"({"message":{"role":"assistant","content":"[\"Bardzo fajne swiatlo!\",\"Podoba mi sie ten kadr.\",\"Super pomysl na sesje.\",\"Swietnie wyszlo!\"]"},"done":true})";
        response.body.assign(answer.begin(), answer.end());
        return response;
    };
    app.companionFeedReset();
    app.companion_.seed = 0; // Four community authors, deterministic fixture response.
    app.pollCompanionFeed(true);
    const auto wait_feed = [&] {
        for (const auto end = GetTickCount64() + 4000; GetTickCount64() < end; Sleep(5)) {
            MSG message{};
            while (PeekMessageW(&message, window, companion_feed_message, companion_feed_message, PM_REMOVE))
                app.onCompanionFeed(std::unique_ptr<CompanionFeedMessage>(reinterpret_cast<CompanionFeedMessage*>(message.lParam)));
            if (!feed.view.busy) return true;
        }
        return false;
    };
    if (!wait_feed() || !described->load() || calls->load() != 2 || feed.view.archive.posts.size() != 1 ||
        feed.view.archive.posts[0].text != L"Moj nowy post z sesji." || feed.view.archive.posts[0].social.comments.size() != 4) return 7971;
    app.companionFeed(true); app.companionFeed(false);
    if (calls->load() != 2 || !companion::feed::load(companion::feed::folderOf(userConfigDirectory(), "rin"))) return 7972;
    app.companionFeed(true); feed.view.app_age = 2.1f; feed.view.entrance.snap(1);
    app.companionFeedAction(0); // A durable heart on the worker.
    const auto wait_social = [&] {
        for (const auto end = GetTickCount64() + 3000; GetTickCount64() < end; Sleep(5)) {
            MSG message{};
            while (PeekMessageW(&message, window, companion_feed_message, companion_feed_message, PM_REMOVE))
                app.onCompanionFeed(std::unique_ptr<CompanionFeedMessage>(reinterpret_cast<CompanionFeedMessage*>(message.lParam)));
            if (!feed.view.social_busy) return true;
        }
        return false;
    };
    if (!wait_social() || !feed.view.archive.posts[0].social.liked) return 7987;
    app.companionFeedAction(1);
    feed.view.input = L"Bardzo fajna sesja!";
    app.companionFeedComment();
    if (!wait_social() || !feed.view.input.empty() || feed.view.archive.posts[0].social.comments.size() != 5 || !capture(L"launchfeed-comments.png")) return 7988;
    app.companionFeedBack();
    // A stale archive response cannot overwrite a newer durable social revision.
    auto old_feed = std::make_unique<CompanionFeedMessage>(); old_feed->generation = feed.generation; old_feed->look = "rin";
    old_feed->archive = feed.view.archive; old_feed->archive->posts[0].social = {};
    app.onCompanionFeed(std::move(old_feed));
    if (!feed.view.archive.posts[0].social.liked || feed.view.archive.posts[0].social.comments.size() != 5) return 7989;
    app.companionFeedDiscuss(0);
    app.companionSend(L"Co to za sesja?");
    const auto request_end = GetTickCount64() + 4000;
    while (app.companion_.busy && GetTickCount64() < request_end) {
        MSG message{};
        while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
            app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
        Sleep(5);
    }
    if (app.companion_.busy || !described->load() || !feed.reference.empty() ||
        std::none_of(app.companion_.turns.begin(), app.companion_.turns.end(), [](const auto& turn) { return turn.text.find(L"opublikowany post") != std::wstring::npos; }) ||
        std::none_of(chat.log.begin(), chat.log.end(), [](const auto& message) { return message.free && message.text.find(L"Launchfeed") != std::wstring::npos; })) return 7973;
    // A committed private reaction waits for a quiet chat; it never interrupts the phone or a
    // response in flight, adds no user turn and is delivered just once.
    app.companionFlush();
    const auto before_reaction = chat.log.size();
    auto reaction = std::make_unique<CompanionFeedMessage>();
    reaction->kind = CompanionFeedMessage::Kind::private_message;
    reaction->generation = feed.generation; reaction->look = "rin"; reaction->image_id = feed.view.archive.posts[0].id;
    reaction->social = feed.view.archive.posts[0].social;
    reaction->social->revision += 10; reaction->social->private_text = L"Dzięki za serduszko pod zdjęciem!"; reaction->social->private_claimed = true;
    app.companionFeed(true);
    app.onCompanionFeed(std::move(reaction)); app.tickCompanionFeed(0.05f);
    if (chat.log.size() != before_reaction) return 7992;
    app.companionFeed(false); chat.waiting = true; app.tickCompanionFeed(0.05f);
    if (chat.log.size() != before_reaction) return 7993;
    chat.waiting = false; app.tickCompanionFeed(0.05f);
    if (chat.log.size() != before_reaction + 1 || !chat.log.back().free || chat.log.back().user || chat.log.back().text.find(L"serduszko") == std::wstring::npos) return 7994;
    app.companionFlush(); app.tickCompanionFeed(0.05f);
    if (chat.log.size() != before_reaction + 1) return 7995;
    return 0;
}

// Towarzyszka: the home tile and greeting, L2/LT and the chat (opening line, chips, a typed
// message, the roulette, a suggested game started), her remarks on launcher events, offline lines,
// and her Settings section with the look previews and characters. A fixture answers as Foundry
// would; nothing here reaches the network.
int LauncherUiTest::companionChat(HWND window) {
    // The test's own Hana and Rin (companion_fixture.hpp; the fixture's guest is Rin), whatever
    // res/companion holds; the built-in list comes back at the end.
    const companion_fixture::Cast cast;
    const auto hana_look = companion_fixture::hana;
    const auto rin_look = companion_fixture::rin;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.launch_animation = false;
    app.settings_.companion_gamification = false; // This flow enables it explicitly in the affinity cases below.
    if (!app.renderer_.initialize(window)) return 7000;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 8; ++i) {
        Game game{200 + i, L"Game " + std::to_wstring(i)};
        game.last_played = i < 3 ? 1'760'000'000 - i * 86400 : 0;
        game.installed = i != 7;
        library->games.push_back(game);
    }
    app.onLibrary(std::move(library)); app.showCategory(0); app.navigate(2);
    app.snapCardsForTransition(); app.detail_ = 1;
    for (size_t i = 0; i < app.cards_.size(); ++i) app.renderer_.setCover(i, pngFixture(static_cast<uint8_t>(30 + i * 20), 64, 64), {});
    app.renderer_.setHero(app.selected_, pngFixture(90, 320, 180));
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    // Foundry fixture: the home greeting, the chat's opening, then answers to typed messages.
    struct Seen { int calls = 0; std::wstring headers, url; std::string last; bool wrong_thread = false; };
    const auto seen = std::make_shared<Seen>();
    const DWORD ui_thread = GetCurrentThreadId();
    app.companion_.post = [seen, ui_thread](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        if (GetCurrentThreadId() == ui_thread) seen->wrong_thread = true;
        ++seen->calls; seen->url = url; seen->headers = headers; seen->last = body;
        const auto answer = [](const std::string& reply) {
            const std::string json = R"({"candidates":[{"content":{"role":"model","parts":[{"text":")" + reply + R"("}]},"finishReason":"STOP"}]})";
            return HttpResponse{200, {json.begin(), json.end()}};
        };
        // The player's last message decides the answer.
        const auto request = json::parse(body);
        const auto* contents = request ? request->get("contents") : nullptr;
        const auto said = contents && !contents->elements.empty() ? contents->elements.back().get("parts")->elements[0].get("text")->stringOr() : std::string{};
        if (said.find("powitania") != std::string::npos) return answer("[smug][nod]Znowu Game 0? Serio, baka. Zagraj wreszcie w [[Game 4]].");
        // A guest: both of them, each message on its own line and signed (as the prompt asks).
        if (said.find("wpada nieproszona") != std::string::npos) return answer("Rin: [smug]Hej, co tu robicie beze mnie?\\nHana: [angry]Rin?! To moj czat!");
        if (said.find("ostatnia wiadomo") != std::string::npos || said.find("po swojemu i wyjdzie") != std::string::npos)
            return answer("**Rin**: [sad]No to pa!\\n\\nHana: [smug]Wreszcie.");
        if (said.find("z którą piszesz, zawo") != std::string::npos)
            return answer("Rin: [excited]Jestem! Wolales?\\nHana: [angry]Hmpf. Tylko nie siadaj za blisko.\\nRin: Za pozno~");
        // Every 3rd–4th answer alone: two short messages.
        if (said == "Krotko") return answer(body.find("komunikatorze") != std::string::npos ? "[smile]No hej.\\n[thinking]Grasz w [[Game 5]]?" : "[smile]No hej.");
        if (said.find("Zroastuj") != std::string::npos) return answer("[angry]Masz 5 nieruszonych gier! [shy]...ale i tak cię lubię.");
        if (said == "Hej") return answer("[happy][nod]Hej hej! {giggles} [thinking]Może [[Game 5]]?"); // {giggles}: her voice's cue, never shown.
        if (said == "Daj buziaka, natychmiast") return answer("[granica][angry]Hamuj! Nie tak szybko."); // Much too far: she sets a boundary.
        // The roulette and her remarks: she names the game the hidden turn names.
        const auto open = said.find("[["), close = said.find("]]", open == std::string::npos ? 0 : open);
        const auto named = open != std::string::npos && close != std::string::npos ? said.substr(open, close + 2 - open) : std::string{};
        if (said.find("ruletk") != std::string::npos) return answer("[excited]Ruletka mówi: " + named + "!");
        if (said.find("Krótka uwaga") != std::string::npos)
            return answer(said.find("premiera") != std::string::npos ? "[excited]Dziś premiera!" : !named.empty() ? "[smile]O, " + named + "!" : "[sleepy]Dobranoc!");
        return answer("[excited]Yahallo! Kapibary grają tylko w RPG-i. [smug]Zagraj w [[Game 3]] albo [[Game 5]], a nie znowu w [[Game 0]].");
    };
    app.settings_.companion_provider = AppSettings::CompanionProvider::foundry; // The fixture answers as Foundry's proxy.
    app.companion_.probe = [] {
        companion::Probe probe;
        probe.environment = [](const wchar_t* name) -> std::wstring {
            if (std::wstring_view(name) == L"FOUNDRY_HOST") return L"https://fixture.palantirfoundry.test/";
            if (std::wstring_view(name) == L"FOUNDRY_TOKEN") return L"fixture.token.value.0123456789abcdef";
            return {};
        };
        return probe;
    };
    const auto wait = [&] {
        const auto end = GetTickCount64() + 5000;
        do {
            MSG message{};
            while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
            while (PeekMessageW(&message, window, companion_event_message, companion_event_message, PM_REMOVE))
                app.onCompanionEvent(std::unique_ptr<CompanionEventMessage>(reinterpret_cast<CompanionEventMessage*>(message.lParam)));
            if (!app.companion_.busy) return !seen->wrong_thread;
            Sleep(5);
        } while (GetTickCount64() < end);
        return false;
    };
    const auto settle = [&](int frames) { for (int i = 0; i < frames; ++i) app.tickCompanion(0.03f); };
    // An afternoon: no goodnight unless a test asks for one.
    const auto at = [](WORD hour) { return [hour] { SYSTEMTIME local{}; local.wYear = 2026; local.wMonth = 10; local.wDay = 14; local.wHour = hour; local.wMinute = 30; return local; }; };
    app.companion_.clock = at(15);
    app.companion_.guest_day = 20261014; // Today's guest has been already: none drops in until a test asks.

    // Her pictures are embedded (companion.rc): every built-in character that can be chosen has at
    // least her basic outfit's calm face, where every missing picture ends (CMake leaves out one without).
    for (size_t i = 0; i < cast.built_in.size(); ++i)
        if (!cast.built_in[i].soon &&
            !FindResourceW(nullptr, MAKEINTRESOURCEW(companion_ui::spriteId(static_cast<companion::Look>(i), companion_ui::Frame::base, companion::calm)), RT_RCDATA)) return 7060;
    // Home: the tile beside Glance, then her greeting once the library is in.
    if (!app.companionAvailable() || !app.glanceAvailable()) return 7001;
    // On start she jumps up out of her tile, then greets.
    if (app.companion_.entrance != 0) return 7111;
    for (int i = 0; i < 10; ++i) app.tickCompanion(0.016f);
    if (app.companion_.entrance <= 0 || app.companion_.entrance >= 1 || !capture(L"companion-home-entrance.png")) return 7112;
    for (int i = 0; i < 60; ++i) app.tickCompanion(0.016f);
    if (app.companion_.entrance != 1 || companion_ui::entranceDrop(1) != 0 || companion_ui::entranceDrop(0) != 1 || companion_ui::entranceDrop(0.7f) >= 0) return 7113;
    // Settled: her remarks may start, but no greeting bubble of her own (the chat keeps the
    // conversation, and a greeting would not know it): nothing is asked yet.
    app.companion_.age = 2; app.tickCompanion(0.016f);
    if (!app.companion_.greeted || app.companion_.bubble.waiting || app.companion_.busy || seen->calls != 0) return 7002;
    settle(200);
    if (!app.companion_.bubble.text.empty() || !capture(L"companion-home.png")) return 7008;
    CompanionView view; app.companionView(view);
    if (!view.tile || view.name != L"Hana" || view.key != L"C" || view.persona != L"Himedere") return 7009; // The first by key.

    // L2/LT opens the chat; she opens the conversation with nonsense and suggestions.
    PadReading pad{true, L"companion-fixture"}; pad.name = L"DualSense";
    app.controller(pad); // Connection: swallowed.
    pad.left_trigger = true; app.controller(pad);
    if (!app.companionOpen() || !app.companion_.chat.waiting) return 7010;
    app.companionView(view);
    if (view.key != L"L2") return 7011;
    if (!capture(L"companion-chat-waiting.png") || !wait()) return 7012;
    // Her first request of the start: Foundry with the fixture token; the library and her persona in the prompt.
    if (seen->calls != 1 || seen->headers.find(L"Authorization: Bearer fixture.token.value.0123456789abcdef") != 0 ||
        seen->url.find(L"https://fixture.palantirfoundry.test/api/v2/llm/proxy/google/v1/models/") != 0) return 7003;
    if (seen->last.find("Game 0") == std::string::npos || seen->last.find("himedere") == std::string::npos) return 7004;
    auto& chat = app.companion_.chat;
    if (chat.log.size() != 1 || chat.log[0].user || chat.log[0].text.find(L"Kapibary") == std::wstring::npos) return 7013;
    if (chat.chips.size() < 3 || chat.chips[0].label != L"▶ Game 3" || chat.chips[0].game != "steam:203" || chat.chips[1].game != "steam:205" ||
        chat.chips[2].game != "steam:200" || chat.chips.back().game.size()) return 7014;
    if (chat.focus != 0) return 7015; // A pad player lands on the first suggestion.
    for (int i = 0; i < 12; ++i) { app.tickCompanion(0.03f); }
    if (!capture(L"companion-chat-talking.png")) return 7016;
    settle(300);
    if (chat.reveal != chat.log[0].text.size() || !capture(L"companion-chat.png")) return 7017;
    // Inline game links: real DirectWrite glyph positions, a browser callback, no game/client launch.
    {
        const auto saved = chat;
        const auto size = app.renderer_.viewport();
        const companion_ui::Layout layout{size.width, size.height};
        const auto points = [&](const std::wstring& title) {
            std::vector<D2D1_POINT_2F> found;
            for (float y = 0; y < size.height; y += 4)
                for (float x = layout.left(); x < layout.right(); x += 4)
                    if (app.renderer_.companionLinkAt(x, y) == title) { found.push_back(D2D1::Point2F(x, y)); break; }
            return found;
        };
        std::wstring opened;
        app.browse_ = [&](const std::wstring& url) { opened = url; };
        const auto game = points(L"Game 3");
        if (chat.log[0].mentions.size() != 3 || game.empty()) return 7900;
        app.click(game.front().x * size.scale, game.front().y * size.scale, false);
        if (opened != L"https://store.steampowered.com/app/203/" || !app.companionOpen() || app.pending_launch_) return 7901;
        if (!app.hoverButton(game.front().x * size.scale, game.front().y * size.scale)) return 7902;
        app.mouseMove(game.front().x * size.scale, game.front().y * size.scale);
        app.companionView(view);
        if (!view.link_pointer || !capture(L"companion-game-link-hover.png")) return 7914;
        app.mouseLeave(); app.companionView(view);
        if (view.link_pointer) return 7915;
        chat.log[0].speaker = rin_look;
        if (!capture(L"companion-game-link-rin.png")) return 7916;
        chat.log[0].speaker = saved.log[0].speaker;
        // Partially typed titles cannot open an incomplete search or a different game's card.
        chat.reveal = chat.log[0].mentions[0].at + 2;
        app.paint();
        if (!points(L"Game 3").empty() || !points(L"Ga").empty()) return 7903;
        // A long title wraps into several clickable fragments, each opening the entire title.
        const std::wstring title = L"A Very Long Game Title With Many Words Across Several Lines & Żółw: The Definitive Complete Adventure Edition";
        const auto parsed = companion::parseText(L"[smile]Sprawdź [[" + title + L"]]!");
        companion_ui::Message linked{false, parsed.text, parsed.emotions};
        linked.mentions = parsed.mentions; linked.speaker = hana_look;
        chat.log = {linked}; chat.reveal = linked.text.size(); chat.scroll = 0; chat.scroll_motion.snap(0);
        if (!capture(L"companion-game-link-wrapped.png")) return 7904;
        auto wrapped = points(title);
        if (wrapped.empty() || wrapped.back().y - wrapped.front().y < 30) return 7905;
        opened.clear(); app.companionClick(wrapped.back().x, wrapped.back().y);
        if (!opened.starts_with(L"https://www.google.com/search?q=A%20Very%20Long") || opened.find(L"%26%20%C5%BB%C3%B3%C5%82w") == std::wstring::npos) return 7906;
        // Menu overlays intercept clicks, and archived conversations retain the same links.
        chat.menu = true; opened.clear(); app.companionClick(wrapped.back().x, wrapped.back().y);
        if (!opened.empty()) return 7907;
        chat.archive = true; app.paint(); wrapped = points(title);
        if (wrapped.empty()) return 7908;
        app.companionClick(wrapped.front().x, wrapped.front().y);
        if (opened.empty()) return 7909;
        chat.archive = false; chat.log[0].user = true; app.paint();
        if (!points(title).empty()) return 7910;
        // Fully clipped lines do not leave hit rectangles behind after scrolling.
        chat.log[0].user = false; chat.scroll_motion.snap(2000); app.paint();
        if (!points(title).empty()) return 7911;
        chat = saved; app.browse_ = {}; app.paint();
    }
    // Pad: Down to the field, Up back through the chips to Back.
    pad.left_trigger = false; app.controller(pad);
    // (The chips may wrap: Down walks their rows, then reaches the field.)
    for (int i = 0; i < 4 && chat.focus != companion_ui::focus_input; ++i) { pad.buttons = XINPUT_GAMEPAD_DPAD_DOWN; app.controller(pad); pad.buttons = 0; app.controller(pad); }
    if (chat.focus != companion_ui::focus_input) return 7018;
    pad.buttons = XINPUT_GAMEPAD_DPAD_UP; app.controller(pad); pad.buttons = 0; app.controller(pad);
    if (chat.focus < 0) return 7019;
    // A typed message (physical keyboard): characters, Backspace, Enter.
    app.noteDesktopInput();
    chat.focus = companion_ui::focus_input;
    if (!app.companionTyping() || !app.searching() || !app.textFieldActive()) return 7020;
    for (const wchar_t ch : std::wstring(L"Hejx")) app.onCharacter(ch);
    app.eraseSearchCharacter();
    if (chat.input != L"Hej" || app.keyboardPreview() != L"Hej") return 7021;
    app.enterDown(false); app.enterUp();
    if (!chat.waiting || chat.log.size() != 2 || !chat.log[1].user || chat.log[1].text != L"Hej" || !chat.input.empty()) return 7022;
    if (!wait() || chat.log.size() != 3 || chat.log[2].text != L"Hej hej! Może Game 5?" || app.companion_.rig.emotion() != companion::Emotion::smile) return 7023;
    // The whole conversation goes back to the model, alternating, starting with the hidden opening.
    const auto request = json::parse(seen->last);
    if (!request || request->get("contents")->elements.size() != 3 || request->get("contents")->elements[1].get("role")->stringOr() != "model") return 7024;
    if (app.companion_.turns.size() != 4) return 7025;
    // A quick prompt chip, then a suggested game started from its chip.
    settle(300);
    const auto roast = std::find_if(chat.chips.begin(), chat.chips.end(), [](const auto& chip) { return chip.prompt == L"Zroastuj moją kupkę wstydu"; });
    if (roast == chat.chips.end()) return 7026;
    chat.focus = static_cast<int>(roast - chat.chips.begin()); app.companionAccept();
    if (!wait() || chat.log.back().text != L"Masz 5 nieruszonych gier! ...ale i tak cię lubię.") return 7027;
    settle(300);
    if (!capture(L"companion-chat-roast.png")) return 7028;
    // The roulette: a reel spins in the log, her verdict waits until it stops, then the pick's ▶ chip.
    const auto spin = std::find_if(chat.chips.begin(), chat.chips.end(), [](const auto& chip) { return chip.kind == companion_ui::Chip::Kind::roulette; });
    if (spin == chat.chips.end() || spin->label != L"Wylosuj grę") return 7080;
    chat.focus = static_cast<int>(spin - chat.chips.begin()); app.companionAccept();
    if (!app.companionRouletteSpinning() || !chat.log.back().roulette || chat.log.back().roulette->reel.size() != 28 || !chat.chips.empty()) return 7081;
    const auto pick = chat.log.back().roulette->reel.back();
    if (!wait() || !app.companion_.held || !chat.log.back().roulette) return 7082; // Her answer waits for the reel.
    settle(40);
    if (!app.companionRouletteSpinning() || !capture(L"companion-chat-roulette.png")) return 7083;
    settle(80);
    if (app.companionRouletteSpinning() || app.companion_.held || chat.log.back().roulette || chat.chips.empty() || chat.chips[0].label != L"▶ " + pick ||
        chat.log.back().text != L"Ruletka mówi: " + pick + L"!") return 7084;
    if (std::none_of(chat.chips.begin(), chat.chips.end(), [](const auto& chip) { return chip.label == L"Losuj jeszcze raz"; })) return 7085;
    settle(200);
    if (!capture(L"companion-chat-roulette-landed.png")) return 7086;
    app.companionSend(L"Hej");
    if (!wait()) return 7029;
    const auto game = std::find_if(chat.chips.begin(), chat.chips.end(), [](const auto& chip) { return chip.game == "steam:205"; });
    if (game == chat.chips.end()) return 7030;
    // Mouse: the chip under the pointer.
    const auto view_size = app.renderer_.viewport();
    const auto rects = companion_ui::Layout{view_size.width, view_size.height}.chips(chat);
    const auto& rect = rects[static_cast<size_t>(game - chat.chips.begin())];
    app.click((rect.left + 10) * view_size.scale, (rect.top + 10) * view_size.scale, false);
    if (app.companionOpen() || !app.pending_launch_ || app.pending_game_.name != L"Game 5") return 7031;
    app.pending_launch_ = false;
    app.showCategory(0);
    // Back home: the conversation stays for the next visit; B closes it again.
    app.openCompanion();
    if (!app.companionOpen() || chat.log.size() < 6 || chat.waiting) return 7032;
    pad.buttons = XINPUT_GAMEPAD_B; app.controller(pad); pad.buttons = 0; app.controller(pad);
    if (app.companionOpen()) return 7033;

    // Her remarks on the home, one at a time: a launch, a finished session, a new game, today's
    // premieres and a goodnight (once a night).
    app.foreground_ = [](HWND) { return true; };
    app.companion_.events.clear(); // The fixture library itself may have counted as new games.
    const auto remark = [&]() -> std::wstring {
        app.companion_.bubble = {}; app.companion_.quiet_until = 0;
        app.tickCompanion(0.016f);
        if (!app.companion_.bubble.waiting || !wait()) return L"<none>";
        return app.companion_.bubble.text;
    };
    Game doom{900, L"Doom"}; doom.kind = GameKind::nonsteam;
    app.companionSessionStarted(doom, true);
    if (app.companion_.events.size() != 1 || remark() != L"O, Doom!") return 7090;
    if (seen->last.find("uruchamia [[Doom]]") == std::string::npos) return 7091;
    app.companionSessionEnded(doom, 42 * 60'000);
    if (remark() != L"O, Doom!" || seen->last.find("po 42 min") == std::string::npos) return 7092;
    settle(30);
    if (!capture(L"companion-home-remark.png")) return 7093;
    {
        const auto bubble = app.renderer_.companionBubble();
        std::optional<D2D1_POINT_2F> link;
        for (float y = bubble.top; y < bubble.bottom && !link; y += 2)
            for (float x = bubble.left; x < bubble.right && !link; x += 2)
                if (app.renderer_.companionLinkAt(x, y) == L"Doom") link = D2D1::Point2F(x, y);
        if (!link || app.companion_.bubble.mentions.empty()) return 7912;
        std::wstring opened;
        app.browse_ = [&](const std::wstring& url) { opened = url; };
        const auto scale = app.renderer_.viewport().scale;
        app.click(link->x * scale, link->y * scale, false);
        if (opened != L"https://www.google.com/search?q=Doom%20game" || app.companionOpen()) return 7913;
        app.browse_ = {};
    }
    auto more = std::make_unique<LibraryMessage>(); more->account = 1;
    for (uint32_t i = 0; i < 9; ++i) { Game entry{200 + i, L"Game " + std::to_wstring(i)}; entry.installed = i != 7; more->games.push_back(entry); }
    app.onLibrary(std::move(more));
    app.scanning_ = false; app.recordLibraryGames(); // The scan done: compare the whole library.
    if (remark() != L"O, Game 8!" || seen->last.find("nowa gra: [[Game 8]]") == std::string::npos) return 7094;
    app.companion_premieres_ = [](bool, int today) { return today == 20261014 ? std::vector<std::wstring>{L"Big Game"} : std::vector<std::wstring>{}; };
    app.companion_.premieres_requested = false; app.requestCompanionPremieres();
    for (const auto end = GetTickCount64() + 5000; !app.companion_.events.size() && GetTickCount64() < end; Sleep(5)) { // The calendar is read on a worker.
        MSG message{};
        while (PeekMessageW(&message, window, companion_event_message, companion_event_message, PM_REMOVE))
            app.onCompanionEvent(std::unique_ptr<CompanionEventMessage>(reinterpret_cast<CompanionEventMessage*>(message.lParam)));
    }
    if (remark() != L"Dziś premiera!" || seen->last.find("Big Game") == std::string::npos) return 7095;
    app.companion_.clock = at(1); app.noteUserInput();
    if (remark() != L"Dobranoc!" || seen->last.find("1:30") == std::string::npos || app.companion_.rig.emotion() != companion::Emotion::sleepy) return 7096;
    if (remark() != L"<none>" || app.companion_.events.size()) return 7097; // One goodnight a night, nothing else queued.
    app.companion_.clock = at(15);

    // Her voice: ElevenLabs answers (the key's check lists two voices), her line spoken chunk by
    // chunk (Hana has no elevenlabs_* lines: the himedere's built-in voice, Alice), her text waiting
    // for the first chunk, the credit in the chat; closing the chat silences her.
    struct Service { std::vector<std::wstring> paths; std::vector<std::string> bodies; std::wstring key; };
    const auto service = std::make_shared<Service>();
    std::vector<uint8_t> wav;
    {
        const auto put = [&](uint32_t value, int bytes) { for (int i = 0; i < bytes; ++i) wav.push_back(static_cast<uint8_t>(value >> (8 * i))); };
        const auto text = [&](const char* tag) { wav.insert(wav.end(), tag, tag + 4); };
        text("RIFF"); put(36 + 4800, 4); text("WAVE"); text("fmt "); put(16, 4); put(1, 2); put(1, 2); put(24000, 4); put(48000, 4); put(2, 2); put(16, 2);
        text("data"); put(4800, 4);
        for (int i = 0; i < 2400; ++i) put(static_cast<uint16_t>(static_cast<int16_t>(6000 * std::sin(i * 0.07))), 2);
    }
    app.companion_.voice_key = [] { return std::wstring(L"sk_test"); };
    app.companion_.voice_request = [service, wav](const std::wstring& key, const std::wstring& path, const std::string* body) {
        service->paths.push_back(path); service->key = key;
        if (!body) {
            const std::string list = R"({"voices":[{"voice_id":"cgSgspJ2msm6clMCkdW9","name":"Jessica - Playful, Bright, Warm","category":"premade"},)"
                                     R"({"voice_id":"Xb7hH8MSUJpSbSDYk0k2","name":"Alice - Clear, Engaging Educator","category":"premade"}]})";
            return path == L"v1/voices" ? HttpResponse{200, {list.begin(), list.end()}} : HttpResponse{404, {}};
        }
        service->bodies.push_back(*body);
        return HttpResponse{200, wav};
    };
    const auto pumpVoice = [&](const std::function<bool()>& done) {
        for (const auto end = GetTickCount64() + 5000; !done() && GetTickCount64() < end; Sleep(5)) {
            MSG message{};
            while (PeekMessageW(&message, window, companion_voice_message, companion_voice_message, PM_REMOVE))
                app.onCompanionVoice(std::unique_ptr<CompanionVoiceMessage>(reinterpret_cast<CompanionVoiceMessage*>(message.lParam)));
        }
        return done();
    };
    app.settings_.companion_voice = AppSettings::CompanionVoice::chat;
    app.requestCompanionVoiceStatus();
    if (!pumpVoice([&] { return app.companion_.voice.engine == CompanionVoice::Engine::ready; }) || app.companion_.voice.voices.size() != 2 ||
        app.companionVoiceSpeaker(hana_look).voices != std::vector<std::string>{"Xb7hH8MSUJpSbSDYk0k2"} || app.companionVoiceName(hana_look) != L"Alice") return 7100;
    if (app.companionVoiceFor(false) || !app.companionVoiceFor(true)) return 7101; // The chat only.
    app.openCompanion();
    app.companionSend(L"Hej");
    if (!wait() || seen->last.find("<ja>") != std::string::npos || seen->last.find("{laughs}") == std::string::npos || chat.log.back().text != L"Hej hej! Może Game 5?" ||
        !app.companion_.voice.pending) return 7102; // Her voice acts (v4 Turbo): the prompt offers cues.
    settle(10);
    if (chat.reveal != 0) return 7103; // Her text waits for her voice.
    if (!pumpVoice([&] { return !app.companion_.voice.pending; })) return 7104;
    settle(10);
    if (chat.reveal == 0 || service->key != L"sk_test" || service->bodies.empty() ||
        std::none_of(service->paths.begin(), service->paths.end(), [](const auto& path) { return path == L"v1/text-to-speech/Xb7hH8MSUJpSbSDYk0k2?output_format=wav_24000"; }) ||
        service->bodies.front().find("\"language_code\":\"pl\"") == std::string::npos || service->bodies.front().find("haughty") == std::string::npos ||
        service->bodies.size() < 2 || service->bodies[1].find("[giggles] Mo") == std::string::npos || service->bodies[0].find("giggles") != std::string::npos) return 7105;
    app.companionView(view);
    if (view.status.find(L"ElevenLabs: Alice") == std::wstring::npos) return 7106;
    const auto line = app.companion_.voice.generation;
    app.back();
    if (app.companionOpen() || app.companion_.voice.generation == line || app.companion_.voice.pending) return 7107; // Silenced with the chat.
    app.settings_.companion_voice = AppSettings::CompanionVoice::off;

    // Now and then two short messages, as in a messenger: the second after the first, with typing between.
    app.openCompanion();
    app.companion_.burst_in = 1;
    app.companionSend(L"Krotko");
    if (!wait() || seen->last.find("komunikatorze") == std::string::npos || app.companion_.burst_in < 3) return 7120;
    if (chat.log.back().text != L"No hej." || chat.queue.size() != 1 || chat.queue[0].text != L"Grasz w Game 5?" || !chat.talking()) return 7121;
    settle(60);
    if (!chat.queue.empty() || chat.log.back().text != L"Grasz w Game 5?" || chat.log.back().emotions.front().emotion != companion::Emotion::thinking ||
        chat.log.back().speaker != hana_look || chat.chips.empty() || chat.chips[0].game != "steam:205") return 7122;
    app.companionSend(L"Krotko"); // The next answer is whole again.
    if (!wait() || seen->last.find("komunikatorze") != std::string::npos || !chat.queue.empty() || chat.log.back().text != L"No hej.") return 7123;
    settle(60);

    // A guest: once a day the other companion drops in by herself after one of the player's
    // messages (here the next), signed with her name; the host makes room and both talk.
    auto& guest = app.companion_.guest;
    app.companion_.guest_day = 0; app.companion_.guest_after = app.companion_.typed + 1;
    app.companionSend(L"Co tam?");
    if (!guest.look || *guest.look != rin_look || guest.invited || guest.shown || app.companion_.guest_day != 20261014) return 7124;
    if (!wait() || seen->last.find("Rin: [angry]") == std::string::npos || seen->last.find("yandere") == std::string::npos) return 7125; // Both in the prompt.
    if (chat.log.back().speaker != rin_look || chat.log.back().text != L"Hej, co tu robicie beze mnie?" || chat.queue.size() != 1 ||
        chat.queue[0].speaker != hana_look || chat.queue[0].text != L"Rin?! To moj czat!" || !guest.shown) return 7126;
    if (guest.rig.emotion() != companion::Emotion::smug || &app.companionRig(rin_look) != &guest.rig) return 7127; // Her own face.
    settle(8);
    if (guest.motion <= 0 || guest.motion >= 1 || !capture(L"companion-guest-entering.png")) return 7128;
    settle(120);
    app.companionView(view);
    if (guest.motion != 1 || !chat.queue.empty() || chat.log.back().speaker != hana_look) return 7129;
    if (!view.guest || view.guest_name != L"Rin" || view.duo != 1 || view.guest_leaving || !capture(L"companion-guest.png")) return 7117;
    // Two or three messages later she runs off: her goodbye, then off-screen; the model hears she is gone.
    for (int i = 0; i < 3 && guest.look; ++i) {
        app.companionSend(L"Hej");
        if (!wait()) return 7130;
        if (guest.leaving && !capture(L"companion-guest-goodbye.png")) return 7131;
        settle(150);
    }
    if (guest.look || guest.motion != 0 || app.companion_.turns.back().text.find(L"wyszła z czatu") == std::wstring::npos) return 7132;
    if (std::none_of(chat.log.begin(), chat.log.end(), [&](const auto& message) { return message.speaker == rin_look && message.text == L"No to pa!"; })) return 7133;
    if (chat.log.back().speaker != hana_look || chat.log.back().text != L"Wreszcie.") return 7134;
    // The day's visit is over: no other comes by herself.
    app.companion_.guest_after = app.companion_.typed + 1;
    app.companionSend(L"Hej");
    if (guest.look || !wait()) return 7135;
    settle(150);

    // "…" beside the header: the menu calls her in, says goodbye to her and clears the chat.
    chat.focus = companion_ui::focus_back;
    app.companionMove(1, 0);
    if (chat.focus != companion_ui::focus_feed) return 7974;
    app.companionMove(1, 0);
    if (chat.focus != companion_ui::focus_more) return 7136;
    app.companionAccept();
    if (!chat.menu || chat.menu_items.size() != 7 || chat.menu_items[0].label != L"Zawołaj Rin" || chat.menu_focus != 0 ||
        chat.menu_items[1].label != L"Pokaż diagnostykę" || chat.menu_items[2].label != L"Zmień strój" ||
        chat.menu_items[3].kind != companion_ui::MenuItem::Kind::change || chat.menu_items[4].kind != companion_ui::MenuItem::Kind::mind ||
        chat.menu_items[5].kind != companion_ui::MenuItem::Kind::backup || chat.menu_items[6].kind != companion_ui::MenuItem::Kind::clear ||
        !capture(L"companion-chat-menu.png")) return 7137;
    app.back(); // Esc/B closes the menu first.
    if (chat.menu || !app.companionOpen()) return 7138;
    app.companionMenu(true);
    app.companionAccept(); // "Zawołaj Rin": she comes in at once, her hello follows.
    if (chat.menu || !guest.look || !guest.invited || !guest.shown || !chat.waiting) return 7139;
    if (!wait() || chat.log.back().text != L"Jestem! Wolales?" || chat.queue.size() != 2) return 7140;
    settle(250);
    if (chat.log.back().speaker != rin_look || chat.log.back().text != L"Za pozno~" ||
        chat.log.back().emotions.front().emotion != companion::Emotion::excited) return 7141; // No tag: her feeling goes on.
    // Called in, she stays however long they talk.
    for (int i = 0; i < 4; ++i) { app.companionSend(L"Hej"); if (!wait()) return 7142; settle(60); }
    if (!guest.look || guest.leaving) return 7143;
    // The mouse: "…", then "Pożegnaj Rin".
    {
        const auto size = app.renderer_.viewport();
        const companion_ui::Layout layout{size.width, size.height};
        app.click((layout.more().left + 10) * size.scale, (layout.more().top + 10) * size.scale, false);
        if (!chat.menu || chat.menu_items[0].kind != companion_ui::MenuItem::Kind::farewell || chat.menu_items[0].label != L"Pożegnaj Rin") return 7144;
        app.click((layout.menuRow(0).left + 20) * size.scale, (layout.menuRow(0).top + 10) * size.scale, false);
        if (chat.menu || !guest.leaving || !chat.waiting) return 7145;
    }
    if (!wait()) return 7146;
    settle(150);
    if (guest.look) return 7147;
    // "Zakończ rozmowę": her goodbye in her style, then she rests; nothing more can be sent, and
    // "Nowa rozmowa" waits until she is back.
    app.companionMenu(true);
    const auto finish_item = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& item) { return item.kind == companion_ui::MenuItem::Kind::clear; });
    if (finish_item == chat.menu_items.end() || finish_item->label != L"Zakończ rozmowę" || !finish_item->enabled) return 7601;
    chat.menu_focus = static_cast<int>(finish_item - chat.menu_items.begin());
    app.companionAccept();
    if (!chat.ended || chat.waiting || chat.log.back().user || chat.log.back().text.empty() || !app.companionResting() ||
        chat.focus != companion_ui::focus_send || app.companionTyping()) return 7602;
    const auto ended_size = chat.log.size();
    app.companionSend(L"Hej");
    if (chat.log.size() != ended_size || chat.waiting) return 7603;
    settle(200);
    app.companionView(view);
    if (view.can_restart || view.chat_note.find(L"Rozmowa zakończona") != 0 || view.chat_note.find(L"wróci za") == std::wstring::npos ||
        !capture(L"companion-chat-ended.png")) return 7604;
    app.companionMenu(true);
    if (chat.menu_items.back().label != L"Nowa rozmowa" || chat.menu_items.back().enabled || chat.menu_items.back().detail.find(L"za ") != 0) return 7605;
    app.companionMenu(false);
    chat.focus = companion_ui::focus_send; app.companionAccept(); // Still resting: nothing.
    if (!chat.ended) return 7606;
    // Two hours later she is back: "Nowa rozmowa" opens a new conversation.
    app.companion_.clock = at(17);
    app.companionView(view);
    if (app.companionResting() || !view.can_restart) return 7607;
    chat.focus = companion_ui::focus_send; app.companionAccept();
    if (chat.ended || !chat.log.empty() || !chat.waiting || app.companion_.turns.size() != 1 || app.companion_.typed != 0) return 7148;
    if (!wait() || chat.log.size() != 1 || chat.log[0].text.find(L"Kapibary") == std::wstring::npos) return 7149;
    app.companion_.clock = at(15);
    // A conversation's cap (every message counts; 4 here): on the player's last message her answer
    // says goodbye too. Kept on disk: a restart resumes it, and "Historia czatu" shows it read only.
    {
        namespace chats = companion::chats;
        app.companion_.persist = true;
        app.companion_.message_cap = 4;
        app.companion_.burst_in = 100;
        app.companionSend(L"Hej");
        if (app.companion_.closing || !wait()) return 7610;
        settle(200);
        app.companionSend(L"Hej");
        if (!app.companion_.closing) return 7611;
        if (!wait() || seen->last.find("dobiega ko") == std::string::npos || !chat.ended || app.companion_.closing || !app.companionResting() ||
            std::any_of(chat.chips.begin(), chat.chips.end(), [](const auto& chip) { return chip.kind != companion_ui::Chip::Kind::game; })) return 7612;
        settle(200);
        const auto lines = chat.log.size();
        const auto config = userConfigDirectory();
        std::optional<chats::Conversation> saved;
        for (const auto end = GetTickCount64() + 3000; GetTickCount64() < end; Sleep(5)) // Written on a worker.
            if ((saved = chats::latest(config, hana_look)) && saved->ended && saved->lines.size() == lines) break;
        if (!saved || !saved->ended || saved->lines.size() != lines || saved->lines[1].text != L"Hej" || !saved->rest_until) return 7613;
        // As after a restart: the launcher forgets it, opening the chat brings it back as it was.
        app.closeCompanion(); app.companionReset();
        app.openCompanion();
        if (chat.log.size() != lines || !chat.ended || chat.waiting || chat.log[1].text != L"Hej" || !app.companionResting()) return 7614;
        // Her rest over: a new conversation; the ended one is in her history.
        app.companion_.rest_until = 0;
        chat.focus = companion_ui::focus_send; app.companionAccept();
        if (chat.ended || !chat.waiting || !wait()) return 7615;
        settle(200);
        app.companionMenu(true);
        const auto history = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& item) { return item.kind == companion_ui::MenuItem::Kind::history; });
        if (history == chat.menu_items.end() || !history->enabled || history->label != L"Historia czatu") return 7616;
        chat.menu_focus = static_cast<int>(history - chat.menu_items.begin()); app.companionAccept();
        if (!chat.menu || chat.menu_items.empty() || chat.menu_items[0].kind != companion_ui::MenuItem::Kind::archive ||
            chat.menu_items[0].label != L"14.10 · 17:30" || chat.menu_items[0].detail != L"dziś") return 7617; // Begun at 17:30 above.
        app.companionAccept();
        app.companionView(view);
        if (!chat.archive || chat.menu || chat.log.size() != lines || view.status.find(L"Historia · 14.10") != 0 || app.companionTyping()) return 7618;
        app.companionSend(L"Hej"); app.companionMenu(true); // Read only: nothing is sent, no menu.
        if (chat.log.size() != lines || chat.menu || app.companion_.busy) return 7619;
        settle(20);
        if (!capture(L"companion-chat-history.png")) return 7620;
        app.back(); // Back to the live conversation.
        if (!app.companionOpen() || chat.archive || chat.log.size() != 1 || chat.ended) return 7621;
        app.companion_.persist = false;
        app.companion_.message_cap = 1000;
    }
    // Her night, by her own clock (Hana's set to 3:00 in her city now, whatever the machine's time):
    // a message past her bedtime gets her goodnight and she sleeps until her morning; reopened, the
    // empty chat waits, ended, with nobody asked; in her morning she opens the day's conversation.
    {
        const auto clockAt = [&](int hour) {
            const auto utc = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            int offset = hour * 60 - static_cast<int>(utc % 86400 / 60);
            while (offset > 12 * 60) offset -= 24 * 60;
            while (offset <= -12 * 60) offset += 24 * 60;
            auto zoned = companion_fixture::cast();
            zoned[0].zoned = true; zoned[0].utc_offset = offset; zoned[0].dst = companion::Character::Dst::none;
            companion::setCharacters(std::move(zoned));
        };
        clockAt(3);
        const auto night = app.companionNight(hana_look);
        if (!night) return 7622;
        if (!night->sleepless) { // A sleepless night (1.5%) she simply talks on.
            if (!app.companionAsleep(hana_look) || app.companionAsleep(rin_look) || !app.companionBedtime()) return 7622;
            app.companionSend(L"Hej");
            if (!app.companion_.closing || !wait() || seen->last.find("idziesz teraz spa") == std::string::npos || !chat.ended || !app.companionResting()) return 7623;
            settle(200);
            app.companionView(view);
            const auto away = app.companionAway();
            if (!away.sleeping || away.seconds < 3 * 3600 || app.companion_.rest_until < app.companionNow() + 3 * 3600 || view.can_restart ||
                view.chat_note.find(L"Rozmowa zakończona · Hana śpi, wstanie za ") != 0 || !capture(L"companion-chat-asleep.png")) return 7624;
            app.closeCompanion(); app.companionReset();
            wait(); // Her diary of the ended conversation.
            app.openCompanion();
            app.companionView(view);
            if (!chat.ended || !chat.log.empty() || chat.waiting || app.companion_.busy || view.chat_note.find(L"Hana śpi, wstanie za ") != 0) return 7625;
            app.companionSend(L"Hej");
            for (int i = 0; i < 5; ++i) app.tickCompanionInitiative(60.0f);
            if (!chat.log.empty() || chat.waiting || app.companion_.busy) return 7626;
            // Her morning: she opens the day's conversation herself.
            clockAt(12);
            if (app.companionAsleep(hana_look) || !app.tickCompanionInitiative(0.1f) || chat.ended || !chat.waiting || !wait() || chat.log.size() != 1) return 7627;
            settle(200);
        }
        companion::setCharacters(companion_fixture::cast());
    }
    // Visits switched off (Settings): nobody to call.
    app.settings_.companion_guests = false;
    app.companionMenu(true);
    if (chat.menu_items.size() != 6 || chat.menu_items[0].kind != companion_ui::MenuItem::Kind::diagnostics ||
        chat.menu_items[1].kind != companion_ui::MenuItem::Kind::costumes ||
        chat.menu_items[3].kind != companion_ui::MenuItem::Kind::mind) return 7150; // "Zmień strój" and "Umysł i pamięć".
    // Diagnostics: where her last answer's time went (each call, its tokens, her typewriter), kept in settings.
    app.companionAccept();
    if (chat.menu || !app.settings_.companion_debug || app.companion_.diagnostics.empty()) return 7151;
    const auto& measured = app.companion_.diagnostics.back();
    if (measured.timing.trace.calls.size() != 1 || measured.timing.trace.calls[0].status != 200 || !measured.chars || measured.typing_seconds <= 0 ||
        measured.timing.system_chars < 500 || measured.model.find(L"Foundry") == std::wstring::npos) return 7152;
    app.companionView(view);
    if (view.diagnostics.size() < 5 || view.diagnostics[0] != L"Diagnostyka" || view.diagnostics[1].find(L"Ostatnia odpowiedź: ") != 0 ||
        std::none_of(view.diagnostics.begin(), view.diagnostics.end(), [](const auto& line) { return line.find(L"zapytanie 1: ") == 0; })) return 7153;
    settle(10);
    if (!capture(L"companion-chat-diagnostics.png")) return 7154;
    app.companionMenu(true); app.companionAccept(); // "Ukryj diagnostykę".
    app.companionView(view);
    if (app.settings_.companion_debug || !view.diagnostics.empty()) return 7155;
    // "Umysł i pamięć": 3D holographic sphere and telemetry panel opened from "…". One of her
    // hidden thoughts is on it, masked until the player shows it.
    const std::wstring hidden_thought = L"[shy]Udaje obojętną, ale cieszy się, że wrócił.";
    app.companionCognition().think(hidden_thought, companion::Emotion::shy, 1'760'000'000, companion::lookKey(app.companionLook()));
    {
        auto& self = app.companionMindState().of(companion::lookKey(app.companionLook()));
        companion::mind::addKeyMemory(self, L"Nasza pierwsza długa rozmowa wieczorem", 1'760'000'100);
        companion::mind::addWorldEntity(self, L"Tomek (brat)", L"grają razem w co-opy", 1'760'000'200);
        self.feelings.push_back({companion::Feeling::care, 0.7f, L"Chcę, żeby odpoczął po trudnym dniu.", 1'760'000'300});
        self.feelings.push_back({companion::Feeling::worry, 0.45f, L"Martwię się, czy dobrze sypia.", 1'760'000'400});
        self.feelings.push_back({companion::Feeling::indifference, 0.3f, L"Ten temat mało mnie angażuje.", 1'760'000'500});
        // Her curiosity: a question she asked back and waits on (lit), and one set aside for later.
        const auto unix_now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        companion::mind::CuriousAct asked;
        asked.id = "c901"; asked.move = companion::CuriousMove::clarify; asked.at = unix_now - 30;
        asked.about = L"Czekaj, chodzi ci o pracę czy o grę?"; asked.why = L"odpowiedział niejednoznacznie";
        companion::mind::CuriousAct set_aside;
        set_aside.id = "c902"; set_aside.later_at = unix_now - 60;
        set_aside.about = L"Jak poszła rozmowa z szefem?"; set_aside.why = L"to nie była dobra chwila";
        self.curious = {asked, set_aside};
    }
    app.companionMenu(true);
    chat.menu_focus = 3; // Kind::mind
    app.companionAccept();
    if (!chat.mind || chat.menu || chat.mind_age != 0) return 7156;
    settle(14); // A moment into its build: the core lit, ribbons drawing, the first embers out.
    if (!capture(L"companion-mind-build.png")) return 7198;
    settle(100); // Built (Layout::mind_build_seconds).
    app.companionView(view);
    if (view.mind_nodes.empty() || std::none_of(view.mind_nodes.begin(), view.mind_nodes.end(),
                                                [](const auto& n) { return n.kind == CompanionView::MindNode::Kind::core; }) ||
        std::none_of(view.mind_nodes.begin(), view.mind_nodes.end(),
                     [](const auto& n) { return n.category == L"Ważne wspomnienie"; }) ||
        std::none_of(view.mind_nodes.begin(), view.mind_nodes.end(),
                     [](const auto& n) { return n.category == L"Twój świat" && n.label == L"Tomek (brat)"; }) ||
        !capture(L"companion-mind.png")) return 7157;
    {
        // Her curiosity is on the graph: what she asked back (a lit thought, never masked) and the question for later.
        using Kind = CompanionView::MindNode::Kind;
        const auto asked = std::find_if(view.mind_nodes.begin(), view.mind_nodes.end(), [](const auto& n) { return n.label == L"Dopytała — czeka na ciebie"; });
        const auto later = std::find_if(view.mind_nodes.begin(), view.mind_nodes.end(), [](const auto& n) { return n.label == L"Zapytam cię (później)"; });
        if (asked == view.mind_nodes.end() || asked->kind != Kind::thought || !asked->excited || asked->masked || asked->revealable ||
            asked->category != L"Ciekawość · dopytanie" || asked->detail.find(L"chodzi ci o pracę czy o grę") == std::wstring::npos ||
            later == view.mind_nodes.end() || later->category != L"Ciekawość · na później" || later->excited) return 8008;
        app.companionMindFilter(companion_ui::MindFilter::thoughts);
        const auto thoughts = app.companionMindNodes();
        if (std::none_of(thoughts.begin(), thoughts.end(), [](const auto& n) { return n.matches_filter && n.label == L"Zapytam cię (później)"; })) return 8009;
        chat.mind_selected = static_cast<int>(std::find_if(thoughts.begin(), thoughts.end(), [](const auto& n) { return n.label == L"Dopytała — czeka na ciebie"; }) - thoughts.begin());
        settle(6);
        if (!capture(L"companion-mind-curiosity.png")) return 8010;
        app.companionMindFilter(companion_ui::MindFilter::all);
        app.companionView(view);
    }
    {
        const auto all_nodes = app.companionMindNodes();
        app.companionMindFilter(companion_ui::MindFilter::feelings);
        const auto filtered = app.companionMindNodes();
        if (filtered.size() != all_nodes.size() || std::none_of(filtered.begin(), filtered.end(), [](const auto& n) { return !n.matches_filter; }) ||
            std::none_of(filtered.begin(), filtered.end(), [](const auto& n) { return n.matches_filter && n.feeling == companion::Feeling::care; })) return 8001;
        for (size_t i = 0; i < filtered.size(); ++i)
            if (filtered[i].x != all_nodes[i].x || filtered[i].y != all_nodes[i].y || filtered[i].z != all_nodes[i].z || filtered[i].parent != all_nodes[i].parent) return 8002;
        if (!capture(L"companion-mind-feelings-filter.png")) return 8003;
        app.companionMove(0, 1);
        if (!app.companionMindNodes()[chat.mind_selected].matches_filter) return 8004;
        app.companionMindFilter(companion_ui::MindFilter::memories);
        if (!capture(L"companion-mind-memory-filter.png")) return 8005;
        const auto size = app.renderer_.viewport();
        const auto all_chip = companion_ui::Layout{size.width, size.height}.mindFilter(0);
        app.companionClick(all_chip.left + 8, all_chip.top + 8);
        if (chat.mind_filter != companion_ui::MindFilter::all) return 8006;
        app.companionQuick(); // Y cycles the graph filter instead of sending a chat prompt.
        if (chat.mind_filter != companion_ui::MindFilter::feelings || !chat.mind) return 8007;
        app.companionMindFilter(companion_ui::MindFilter::all);
        app.companionView(view);
    }
    // Her hidden thought: masked until Pokaż (Enter / A or a click) shows it, Ukryj masks it again;
    // masked again on the next visit, too.
    {
        using Kind = CompanionView::MindNode::Kind;
        const auto masked_thought = [&] {
            app.companionView(view);
            // Her hidden <mysl> thoughts are the revealable ones (her curiosity's acts are thoughts too, never masked).
            const auto found = std::find_if(view.mind_nodes.begin(), view.mind_nodes.end(), [](const auto& n) { return n.kind == Kind::thought && n.revealable; });
            return found == view.mind_nodes.end() ? -1 : found->masked ? 1 : 0;
        };
        const auto thought = std::find_if(view.mind_nodes.begin(), view.mind_nodes.end(), [](const auto& n) { return n.kind == Kind::thought && n.revealable; });
        if (thought == view.mind_nodes.end() || !thought->masked || !thought->revealable || thought->detail != hidden_thought) return 7199;
        chat.mind_selected = static_cast<int>(thought - view.mind_nodes.begin());
        settle(4);
        if (!capture(L"companion-mind-thought.png")) return 7200;
        app.companionAccept(); // Pokaż: Enter on a hidden thought shows it and the screen stays.
        if (!chat.mind || masked_thought() != 0) return 7201;
        settle(4);
        if (!capture(L"companion-mind-thought-shown.png")) return 7202;
        app.companionAccept(); // Ukryj.
        if (masked_thought() != 1) return 7203;
        app.companionAccept();
        app.companionMind(false); app.companionMind(true);
        if (!chat.mind || masked_thought() != 1 || !chat.mind_revealed.empty()) return 7204;
        settle(100);
    }
    // The wheel zooms the sphere up to 200% (dragging turns it); back down to fitted.
    app.companionWheel(WHEEL_DELTA * 7);
    if (chat.mind_zoom != companion_ui::Layout::mind_zoom_max) return 7205;
    settle(30);
    if (!capture(L"companion-mind-zoom.png")) return 7206;
    app.companionWheel(-WHEEL_DELTA * 20);
    if (chat.mind_zoom != 1) return 7207;
    settle(30);
    app.companionMove(1, 0);
    if (chat.mind_selected == 0 && view.mind_nodes.size() > 1) return 7158;
    // A memory she may forget: its card offers Usuń, which asks first (Anuluj focused, Back cancels).
    {
        const auto deletable = std::find_if(view.mind_nodes.begin(), view.mind_nodes.end(), [](const auto& node) { return node.deletable; });
        if (deletable == view.mind_nodes.end()) return 7187;
        chat.mind_selected = static_cast<int>(deletable - view.mind_nodes.begin());
        settle(6);
        if (!capture(L"companion-mind-memory.png")) return 7188;
        app.companionMindDeleteConfirm(true);
        if (!chat.mind_confirm || chat.mind_confirm_focus != companion_ui::confirm_cancel) return 7189;
        settle(4);
        if (!capture(L"companion-mind-delete.png")) return 7196;
        app.back();
        if (chat.mind_confirm || !chat.mind) return 7197;
    }
    app.back();
    if (chat.mind || !app.companionOpen()) return 7159;
    // Gamification: her level grows as the player writes; "…" → "Zresetuj sympatię" asks first.
    {
        using Kind = companion_ui::MenuItem::Kind;
        app.companionView(view);
        if (view.affinity) return 7160; // Off by default: no badge, no menu row.
        namespace affinity = companion::affinity;
        app.settings_.companion_gamification = true;
        settle(1);
        auto& state = app.companionAffinity();
        auto& points = state.points[std::string(companion::lookKey(app.companionLook()))];
        state.day = 20261014; // Today's visit is counted already: only the message pays.
        points = affinity::step(1) - 5; // One message short of level 2.
        app.companionSend(L"Hej");
        if (points != affinity::step(1) + 5 || !wait()) return 7161;
        // Her prompt knows today's date and time (the test clock: 14.10.2026, 15:30), their stage,
        // and the new level in a hidden note after the message.
        if (seen->last.find("14.10.2026, godz. 15:30") == std::string::npos || seen->last.find("poziom 2 na 50") == std::string::npos) return 7162;
        app.companionView(view);
        if (!view.affinity || view.affinity_level != 2 || view.affinity_progress <= 0 || view.affinity_progress >= 1 || view.affinity_pulse <= 0) return 7163;
        // The day's first message: a streak of days in a row pays more.
        state.day = 20261013; state.streak = 4;
        const auto before_visit = points;
        app.companionSend(L"Krotko");
        if (points != before_visit + affinity::per_message + affinity::dailyBonus(5) || state.streak != 5 || state.day != 20261014 || !wait()) return 7171;
        if (seen->last.find("5 dni z rz") == std::string::npos) return 7172;
        app.companionView(view);
        if (view.streak != 5) return 7173;
        // Her boundary ([granica], below level 50, alone): a yellow card first — nothing lost, a card
        // on her badge — then, while it is live, a red one with a small penalty that keeps the level;
        // her prompt knows she is cooler now.
        {
            const std::string key(companion::lookKey(app.companionLook()));
            const auto before_yellow = points;
            app.companionSend(L"Daj buziaka, natychmiast");
            if (!wait() || points != before_yellow + affinity::per_message || state.card(key, 20261014) != affinity::Card::yellow ||
                chat.log.back().text != L"Hamuj! Nie tak szybko." || seen->last.find("[granica]") == std::string::npos) return 7190;
            app.companionView(view);
            if (view.affinity_card != 1 || view.affinity_card_pulse <= 0) return 7191;
            settle(5);
            if (!capture(L"companion-card.png")) return 7192;
            const auto before_red = points;
            const int level = affinity::level(before_red);
            app.companionSend(L"Daj buziaka, natychmiast");
            const int64_t lost = affinity::penalty(before_red + affinity::per_message);
            if (!wait() || lost <= 0 || points != before_red + affinity::per_message - lost || state.card(key, 20261014) != affinity::Card::red) return 7193;
            app.companionView(view);
            if (view.affinity_card != 2 || view.affinity_level != level) return 7194;
            app.companionSend(L"Hej");
            if (!wait() || seen->last.find("mimo upomnienia") == std::string::npos) return 7195;
            state.cards.clear(); app.companion_.affinity_card_pulse = 0;
        }
        // Her costumes: "…" → "Zmień strój"; locked ones wait for their level.
        app.companion_.costumes = std::vector<companion::costumes::Costume>{
            {hana_look, 0, 0, "costume_00"}, {hana_look, 1, 10, "costume_10_kimono"}, {hana_look, 2, 50, "costume_50"},
            {rin_look, 0, 0, "costume_00"}};
        const auto pickMenu = [&](Kind kind) {
            app.companionMenu(true);
            const auto item = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [&](const auto& entry) { return entry.kind == kind; });
            if (item == chat.menu_items.end()) return false;
            chat.menu_focus = static_cast<int>(item - chat.menu_items.begin()); app.companionAccept();
            return true;
        };
        if (!pickMenu(Kind::costumes) || !chat.menu || chat.menu_items.size() != 3 || chat.menu_items[0].kind != Kind::wear || chat.menu_items[0].detail != L"\u2713" ||
            chat.menu_items[1].enabled || chat.menu_items[1].label != L"Kimono" || chat.menu_items[1].detail != L"od poziomu 10") return 7174;
        settle(5);
        if (!capture(L"companion-costume-menu.png")) return 7180;
        app.companionMenu(false);
        points = affinity::threshold(10); // Level 10: the kimono opens.
        if (!pickMenu(Kind::costumes) || !chat.menu_items[1].enabled) return 7175;
        chat.menu_focus = 1; app.companionAccept();
        app.companionView(view);
        if (chat.menu || app.settings_.companion_costumes["hana"] != "costume_10_kimono" || view.costume != 1) return 7176;
        settle(10);
        if (!capture(L"companion-costume.png")) return 7177;
        // Gamification off: every costume is free; on again with a lower level: back to the basic outfit.
        app.settings_.companion_gamification = false;
        if (!pickMenu(Kind::costumes) || !chat.menu_items[2].enabled) return 7178;
        app.companionMenu(false);
        app.settings_.companion_gamification = true;
        points = 0;
        app.companionView(view);
        if (view.costume != 0) return 7179;
        points = affinity::threshold(3);
        app.settings_.companion_costumes.clear();
        app.companion_.costumes.reset();
        // An embedded costume of a built-in character (the first one with a second costume; none:
        // skipped): she wears it, offered in her "Zmień strój" list; a feeling it lacks shows its
        // calm face, so the outfit stays. Then Hana and Rin again.
        {
            companion::setCharacters(cast.built_in);
            const auto& all = app.companionCostumes();
            const auto embedded = std::find_if(all.begin(), all.end(), [](const auto& c) { return c.slot != 0 && !companion::character(c.look)->soon; });
            if (embedded != all.end()) {
                const auto costume = *embedded;
                const std::string key(companion::lookKey(costume.look));
                app.settings_.companion_gamification = false;
                app.settings_.companion_look = key; app.settings_.companion_costumes[key] = costume.folder;
                app.companionView(view);
                if (view.costume != costume.slot || view.look != costume.look || app.companionCostume(costume.look) != costume.slot) return 7184;
                app.companion_.rig.emote(companion::Emotion::smug); settle(40);
                if (!capture(L"companion-costume-embedded.png")) return 7185;
                app.settings_.companion_gamification = true;
            }
            app.settings_.companion_look.clear(); app.settings_.companion_costumes.clear();
            companion::setCharacters(companion_fixture::cast());
            if (app.companionLook() != hana_look) return 7186;
        }
        app.companionMenu(true);
        const auto found = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [](const auto& item) { return item.kind == Kind::reset_affinity; });
        if (found == chat.menu_items.end() || chat.menu_items.back().kind != Kind::clear) return 7164;
        const int reset_row = static_cast<int>(found - chat.menu_items.begin());
        chat.menu_focus = reset_row; app.companionAccept();
        if (!chat.confirm || chat.menu || chat.confirm_focus != companion_ui::confirm_cancel) return 7165;
        settle(10);
        if (!capture(L"companion-reset-affinity.png")) return 7166;
        app.companionAccept(); // Anuluj is focused: nothing changes.
        if (chat.confirm || points == 0) return 7167;
        app.companionMenu(true); chat.menu_focus = reset_row; app.companionAccept();
        app.back(); // Back is Anuluj too; the chat stays.
        if (chat.confirm || !app.companionOpen() || points == 0) return 7168;
        app.companionMenu(true); chat.menu_focus = reset_row; app.companionAccept();
        app.companionMove(-1, 0); app.companionAccept(); // The red Zresetuj.
        if (chat.confirm || points != 0) return 7169;
        app.companionView(view);
        if (view.affinity_level != 1) return 7170;
        app.settings_.companion_gamification = false;
    }
    app.settings_.companion_guests = true;
    app.back();

    // Offline (no token anywhere): canned lines with suggestions from the library, never a request.
    app.companionReset();
    app.companion_.probe = [] { return companion::Probe{}; };
    const int calls = seen->calls;
    app.openCompanion();
    if (!wait() || seen->calls != calls || chat.log.size() != 1 || !app.companion_.offline || chat.chips.empty()) return 7034;
    for (const auto& chip : chat.chips) if (!chip.game.empty() && chip.game.rfind("steam:2", 0) != 0) return 7035;
    settle(300);
    if (!capture(L"companion-chat-offline.png")) return 7036;
    app.back();

    // Settings → Asystent: the rows, the animated previews and the saved choices.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system));
    app.buildSettingsRows();
    const auto row = [&](settings_ui::Id id) {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
        return found == app.settings_rows_.end() ? -1 : static_cast<int>(found - app.settings_rows_.begin());
    };
    // Glance stays in System; all companion settings live on their own page.
    if (row(settings_ui::Id::glance) < 0 || app.settings_rows_[row(settings_ui::Id::glance)].section != tr(L"Funkcje") ||
        row(settings_ui::Id::companion) >= 0 || row(settings_ui::Id::companion_look) >= 0) return 7039;
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::companion));
    app.buildSettingsRows();
    if (row(settings_ui::Id::companion) != 0 || app.settings_rows_[0].section != L"Towarzyszka AI" ||
        row(settings_ui::Id::glance) >= 0 || row(settings_ui::Id::windowed) >= 0) return 7039;
    // Her model, voice and phone access are tiles; their rows are on their pages.
    const auto tile = [&](settings_ui::Page page) {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.control == settings_ui::Control::page && r.page == page; });
        return found == app.settings_rows_.end() ? -1 : static_cast<int>(found - app.settings_rows_.begin());
    };
    if (row(settings_ui::Id::companion_look) < 0 || app.settings_rows_[row(settings_ui::Id::companion_look)].control != settings_ui::Control::avatar ||
        tile(settings_ui::Page::companion_model) < 0 || tile(settings_ui::Page::companion_voice) < 0 || tile(settings_ui::Page::companion_remote) < 0 ||
        row(settings_ui::Id::companion_model) >= 0 || row(settings_ui::Id::companion_voice) >= 0) return 7040;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = tile(settings_ui::Page::companion_model);
    app.settingsActivate();
    if (!app.settings_state_.at(settings_ui::Page::companion_model) || row(settings_ui::Id::companion_provider) != 0 ||
        row(settings_ui::Id::companion_model) < 0 || row(settings_ui::Id::companion_token) < 0 ||
        row(settings_ui::Id::companion_host) < 0 || row(settings_ui::Id::companion_test) < 0) return 7040;
    if (app.settings_rows_[row(settings_ui::Id::companion_token)].value != L"Wpisz token") return 7041; // Offline probe: nothing found.
    app.back(); // Back on the tile it was opened from.
    if (app.settings_state_.subpage() || app.settings_state_.row() != tile(settings_ui::Page::companion_model)) return 7040;
    // Her voice rows: only the switch while it is off; on, the key, the model and her voice.
    if (!app.revealSetting(settings_ui::Id::companion_voice) || !app.settings_state_.at(settings_ui::Page::companion_voice) ||
        row(settings_ui::Id::companion_voice_style) >= 0 || row(settings_ui::Id::companion_voice_key) >= 0) return 7108;
    app.settings_.companion_voice = AppSettings::CompanionVoice::everywhere; app.buildSettingsRows();
    const int voice_style = row(settings_ui::Id::companion_voice_style);
    // Hana is a himedere without elevenlabs_* lines: automatic is her built-in voice, then the account's two.
    if (voice_style < 0 || app.settings_rows_[voice_style].choices.size() != 3 || app.settings_rows_[voice_style].choices[0].label != L"Automatycznie: Alice" ||
        app.settings_rows_[voice_style].description.find(L"Alice") == std::wstring::npos || row(settings_ui::Id::companion_voice_key) < 0 ||
        app.settings_rows_[row(settings_ui::Id::companion_voice_key)].value != L"Klucz zapisany" || row(settings_ui::Id::companion_voice_model) < 0 ||
        app.settings_rows_[row(settings_ui::Id::companion_voice_model)].choices.size() != 1 + elevenlabs::models.size() || row(settings_ui::Id::companion_voice_test) < 0) return 7109;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = voice_style;
    app.ensureSettingVisible();
    if (!capture(L"companion-settings-voice.png")) return 7110;
    app.settings_.companion_voice = AppSettings::CompanionVoice::off; app.buildSettingsRows();
    app.closeSettingsPage();
    const int look = row(settings_ui::Id::companion_look);
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = look;
    app.ensureSettingVisible();    for (int i = 0; i < 90; ++i) app.tickCompanion(0.03f);
    app.companionView(view);
    if (view.previews[static_cast<size_t>(hana_look)].time <= 0 || view.previews[static_cast<size_t>(rin_look)].time != 0) return 7042;
    if (!capture(L"companion-settings.png")) return 7042;
    // Gamification on: both cards show their level (their rivalry).
    {
        app.settings_.companion_gamification = true;
        app.companionAffinity().points = {{"hana", companion::affinity::threshold(3)}, {"rin", companion::affinity::threshold(20)}};
        app.companionView(view);
        if (view.preview_levels[0] != 3 || view.preview_levels[1] != 20) return 7181;
        if (!capture(L"companion-settings-levels.png")) return 7182;
        app.settings_.companion_gamification = false;
        app.companionView(view);
        if (view.preview_levels[1] != 0) return 7183;
    }
    app.settingsAdjust(1);
    if (app.settings_.companion_look != "rin" || app.settings_rows_[row(settings_ui::Id::companion_look)].choice != 1) return 7043;
    app.tickCompanion(0.03f);
    app.companionView(view);
    if (view.name != L"Rin" || view.previews[static_cast<size_t>(hana_look)].time != 0 || view.previews[static_cast<size_t>(rin_look)].time <= 0) return 7044;
    if (!capture(L"companion-settings-red.png")) return 7045;
    // Each companion's character is fixed (her character.txt's persona=).
    if (app.companionPersona() != companion::Persona::yandere || app.companionPersonaOf(hana_look) != companion::Persona::himedere) return 7046;
    for (const auto hidden : {settings_ui::Id::companion_guests, settings_ui::Id::companion_gamification, settings_ui::Id::companion_memory,
                             settings_ui::Id::companion_notes, settings_ui::Id::companion_model_info, settings_ui::Id::companion_photos})
        if (row(hidden) >= 0) return 7116;
    app.saveSettings();
    const auto saved = loadAppSettings();
    if (!saved.companion || saved.companion_look != "rin" || saved.companion_model != "gemini-3-8-flash") return 7047;
    // Switched off: no tile, no shortcut, nothing pending.
    app.settings_state_.selected[app.settings_state_.category] = row(settings_ui::Id::companion);
    app.settingsActivate();
    if (app.settings_.companion || row(settings_ui::Id::companion_look) >= 0) return 7048;
    app.back(); app.back(); app.showCategory(0);
    if (app.companionAvailable()) return 7049;
    pad.left_trigger = true; app.controller(pad); pad.left_trigger = false; app.controller(pad);
    if (app.companionOpen()) return 7050;
    app.settings_.companion = true; app.settings_.companion_look = "rin"; app.companion_.tile_progress = 1;
    app.companionReset(); app.companion_.greeted = true;
    app.companion_.rig.emote(companion::Emotion::playful);
    settle(20);
    if (!capture(L"companion-home-red.png")) return 7051;
    // A long quiet: she dozes off; the next input wakes her with a start, then she calms down.
    app.last_input_ = std::chrono::steady_clock::now() - std::chrono::seconds(50);
    app.companion_.bubble = {};
    settle(10);
    if (!app.companion_.dozing || app.companion_.rig.emotion() != companion::Emotion::sleepy || !capture(L"companion-home-sleepy.png")) return 7052;
    app.noteUserInput(); settle(2);
    if (app.companion_.dozing || app.companion_.rig.emotion() != companion::Emotion::shock) return 7053;
    settle(60);
    if (app.companion_.rig.emotion() != companion::calm) return 7054;
    return 0;
}

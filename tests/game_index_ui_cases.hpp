#include "companion_fixture.hpp"
#include "../src/net/json.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/store/game_details.hpp"

#include <fstream>
#include <iterator>

// The game index in the launcher: the library and the cached store pages go into it; opening
// Szczegóły adds a game's page; Szczegóły offline take the index's facts; the library sorts by
// release date and finds games by genre; Towarzyszka answers "the oldest game?" through her tool
// (the fixture checks that her call goes back with its thought signature) and remembers what she
// suggested; her memory settings. A fixture answers as Foundry would; nothing reaches the network.
int LauncherUiTest::gameKnowledge(HWND window) {
    const companion_fixture::Cast cast; // Her photos' folder is her key's: the test's own companions.
    const auto config = userConfigDirectory();
    {
        Launcher app;
        app.hwnd_ = window; app.execute_ = fakeExecute;
        app.overlay_backend_.foreground = [window] { return window; };
        app.settings_.launch_animation = false;
        if (!app.renderer_.initialize(window)) return 7200;
        const auto capture = [&](const wchar_t* name) {
            wchar_t folder[4096]{};
            if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { app.paint(); return true; }
            return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
        };
        // Store pages as Szczegóły and list previews leave them in the cache.
        const auto page = [&](uint32_t id, const char* description, const char* date, const char* genre) {
            const std::string json = "{\"" + std::to_string(id) + R"(":{"success":true,"data":{"short_description":")" + description +
                R"(","developers":["Retro Studio"],"genres":[{"id":")" + genre + R"("}],"release_date":{"date":")" + date + "\"}}}}";
            atomic_file::write(config / L"metadata" / (std::to_wstring(id) + L".details.json"), json);
            return json;
        };
        page(300, "Stara klasyka.", "1 lis 1998", "3");
        page(301, "Nowsza gra.", "12 lut 2011", "1");
        auto library = std::make_unique<LibraryMessage>(); library->account = 1;
        const wchar_t* names[]{L"Old Game", L"New Game", L"Mystery Game", L"Fourth Game"};
        for (uint32_t i = 0; i < 4; ++i) {
            Game game{300 + i, names[i]};
            game.last_played = i == 1 ? 1'760'000'000 : 0;
            game.installed = true;
            library->games.push_back(game);
        }
        app.onLibrary(std::move(library)); app.showCategory(0);
        app.scanning_ = false;
        app.startGameIndex();
        if (!app.game_index_) return 7201;
        const auto pump = [&] {
            MSG message{};
            while (PeekMessageW(&message, window, game_index_message, game_index_message, PM_REMOVE))
                app.onGameIndex(std::unique_ptr<GameIndexMessage>(reinterpret_cast<GameIndexMessage*>(message.lParam)));
            while (PeekMessageW(&message, window, companion_message, companion_message, PM_REMOVE))
                app.onCompanion(std::unique_ptr<CompanionMessage>(reinterpret_cast<CompanionMessage*>(message.lParam)));
            while (PeekMessageW(&message, window, companion_photo_message, companion_photo_message, PM_REMOVE))
                app.onCompanionPhoto(std::unique_ptr<CompanionPhotoMessage>(reinterpret_cast<CompanionPhotoMessage*>(message.lParam)));
        };
        const auto until = [&](const std::function<bool(const game_index::Index&)>& done) {
            for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end; Sleep(5)) {
                pump();
                if (app.game_index_->read(done)) return true;
            }
            return false;
        };
        // The library goes to the index once it settles; the cached pages are read with it.
        app.syncGameIndex();
        app.game_index_changed_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        app.tickGameIndex();
        if (!until([](const game_index::Index& index) { return index.keys().size() == 4 && index.get("steam:300")->release == game_index::Date{1998, 11, 1}; })) return 7202;
        if (!app.game_index_->read([](const game_index::Index& index) {
                const auto* old = index.get("steam:300");
                return old->genres == std::vector<std::wstring>{L"RPG"} && old->description == L"Stara klasyka." && !index.get("steam:302")->from(game_index::Source::steam_store);
            })) return 7203;
        // Szczegóły of a game the index has no page for: its page arrives, the index grows.
        const auto mystery = page(302, "Tajemnica.", "2005", "25");
        app.openDetails(*std::find_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.app_id == 302; }));
        auto store = std::make_unique<StoreMessage>();
        store->generation = app.store_jobs_->generation.load(); store->account = 1; store->details = true; store->info_ready = true; store->installation_checked = false;
        store->info = parseGameDetails(302, mystery);
        app.onStore(std::move(store));
        if (app.details_.description != L"Tajemnica.") return 7204;
        if (!until([](const game_index::Index& index) { return index.get("steam:302")->description == L"Tajemnica." && index.get("steam:302")->release.year == 2005; })) return 7205;
        app.back();
        // Szczegóły offline: the store page did not come, the index's facts fill them.
        app.openDetails(*std::find_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.app_id == 300; }));
        store = std::make_unique<StoreMessage>();
        store->generation = app.store_jobs_->generation.load(); store->account = 1; store->details = true; store->info_ready = true; store->installation_checked = false;
        store->info.error = L"Opis tej gry nie jest teraz dostępny.";
        app.onStore(std::move(store));
        if (app.details_.description != L"Stara klasyka." || !app.details_.error.empty() || gameReleaseYear(app.details_.release_date) != L"1998" ||
            app.details_.genres != L"RPG" || app.details_.developer != L"Retro Studio") return 7206;
        if (!capture(L"details-from-index.png")) return 7207;
        app.back();
        // The library: newest release first, undated last; a typed genre finds its games.
        app.openLibrary();
        app.library_controls_.options.order = library_view::Order::release; app.applyLibraryOptions();
        std::vector<std::string> order;
        for (const auto& card : app.cards_) if (isGame(card.game)) order.push_back(gameKey(card.game));
        if (order.size() != 4 || order[0] != "steam:301" || order[1] != "steam:302" || order[2] != "steam:300" || order[3] != "steam:303") return 7208;
        app.openLibrarySearch(); app.library_controls_.query = L"rpg"; app.applyLibraryQuery();
        if (app.cards_.size() != 1 || gameKey(app.cards_[0].game) != "steam:300") return 7209;
        app.library_controls_.query = L"przygodowe"; app.applyLibraryQuery();
        if (app.cards_.size() != 1 || gameKey(app.cards_[0].game) != "steam:302") return 7210;
        app.library_controls_.query = L"New"; app.applyLibraryQuery();
        if (app.cards_.size() != 1 || gameKey(app.cards_[0].game) != "steam:301") return 7211;
        app.library_controls_.options.order = library_view::Order::last_played; app.applyLibraryOptions();
        app.showCategory(0);

        // Her tool: "Jaka jest moja najstarsza gra?" answered from the index, the call sent back with its signature.
        struct Seen { std::vector<std::string> bodies; };
        const auto seen = std::make_shared<Seen>();
        app.companion_.post = [seen](const std::wstring&, const std::wstring&, const std::string& body) {
            seen->bodies.push_back(body);
            const auto text = [](const std::string& value) {
                const std::string json = R"({"candidates":[{"content":{"role":"model","parts":[{"text":")" + value + R"("}]},"finishReason":"STOP"}]})";
                return HttpResponse{200, {json.begin(), json.end()}};
            };
            const auto request = json::parse(body);
            const auto& contents = request->get("contents")->elements;
            const auto& part = contents.back().get("parts")->elements[0];
            if (const auto* answer = part.get("functionResponse")) {
                const auto& games = answer->get("response")->get("games")->elements;
                return text("[smug]Najstarsza? [[" + (games.empty() ? std::string("?") : games[0].get("title")->stringOr()) + "]] z 1998, prawdziwy zabytek.");
            }
            const auto said = part.get("text") ? part.get("text")->stringOr() : std::string{};
            if (said.find("najstarsza") != std::string::npos && body.find("\"functionDeclarations\"") != std::string::npos) {
                const std::string call = R"({"candidates":[{"content":{"role":"model","parts":[{"thoughtSignature":"fixture-signature","functionCall":{"id":"c1","name":"szukaj_w_bibliotece","args":{"sortuj":"oldest","limit":1}}}]},"finishReason":"STOP"}]})";
                return HttpResponse{200, {call.begin(), call.end()}};
            }
            return text("[smile]Hej! Pytaj o swoje gry.");
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
        app.companion_.greeted = true;
        const auto wait = [&] {
            for (const auto end = GetTickCount64() + 5000; GetTickCount64() < end; Sleep(5)) { pump(); if (!app.companion_.busy) return true; }
            return false;
        };
        app.openCompanion();
        if (!app.companionOpen() || !wait() || app.companion_.chat.log.empty()) return 7212;
        // Tools in the chat only, with the looser rule and the summary's note on them.
        if (seen->bodies.empty() || seen->bodies.back().find("\"functionDeclarations\"") == std::string::npos ||
            seen->bodies.back().find("najpierw sprawdź to narzędziami") == std::string::npos) return 7213;
        seen->bodies.clear();
        app.companionSend(L"Jaka jest moja najstarsza gra?");
        if (!wait()) return 7214;
        auto& chat = app.companion_.chat;
        if (chat.log.back().text != L"Najstarsza? Old Game z 1998, prawdziwy zabytek." || seen->bodies.size() != 2 ||
            seen->bodies[1].find("fixture-signature") == std::string::npos || seen->bodies[1].find("\"functionResponse\"") == std::string::npos) return 7215;
        if (chat.chips.empty() || chat.chips[0].game != "steam:300") return 7216;
        // A title from her tools, not from the summary she saw, is still found.
        app.companion_.library = {};
        if (app.companionGameKey(L"Old Game") != "steam:300" || !app.companionGameKey(L"Old").empty()) return 7217;
        // She remembers what she said and suggested.
        const auto& memory = app.companionMemory();
        if (memory.remarks.empty() || memory.picks.empty() || memory.picks.back().key != "steam:300") return 7218;
        for (const auto end = GetTickCount64() + 3000; !std::filesystem::exists(config / L"companion_memory.dat") && GetTickCount64() < end; Sleep(5)) {}
        if (!std::filesystem::exists(config / L"companion_memory.dat")) return 7219;
        for (int i = 0; i < 300; ++i) app.tickCompanion(0.03f);
        if (!capture(L"companion-chat-tools.png")) return 7220;
        seen->bodies.clear();
        app.companionSend(L"Hej");
        if (!wait() || seen->bodies.empty() || seen->bodies.back().find("Twoja pamięć") == std::string::npos) return 7221; // Her memory in the prompt.
        app.back();

        // Settings → Asystent: memory and notes on by default; clearing forgets everything.
        app.openSettings();
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::companion));
        app.buildSettingsRows();
        const auto row = [&](settings_ui::Id id) {
            const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
            return found == app.settings_rows_.end() ? -1 : static_cast<int>(found - app.settings_rows_.begin());
        };
        const int remember = row(settings_ui::Id::companion_memory), notes = row(settings_ui::Id::companion_notes), forget = row(settings_ui::Id::companion_forget);
        if (remember >= 0 || notes >= 0 || forget < 0 || !app.settings_.companion_memory || !app.settings_.companion_notes) return 7222;
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
        app.settings_state_.selected[app.settings_state_.category] = forget;
        app.ensureSettingVisible();
        if (!capture(L"companion-settings-memory.png")) return 7224;
        app.settings_state_.selected[app.settings_state_.category] = row(settings_ui::Id::companion_forget);
        app.settingsActivate();
        if (!app.companionMemory().empty()) return 7225;
        for (const auto end = GetTickCount64() + 3000; std::filesystem::exists(config / L"companion_memory.dat") && GetTickCount64() < end; Sleep(5)) {}
        if (std::filesystem::exists(config / L"companion_memory.dat")) return 7226;
        app.saveSettings();
        if (!loadAppSettings().companion_notes || !loadAppSettings().companion_memory) return 7227;

        // Providers (Asystent → "Model AI"): each with its model and credentials; the chat names the model only when asked.
        if (!app.revealSetting(settings_ui::Id::companion_provider) || !app.settings_state_.at(settings_ui::Page::companion_model)) return 7229;
        const auto choose = [&](settings_ui::Id id, int choice) {
            app.settings_state_.selected[app.settings_state_.category] = row(id);
            app.settingsActivate(); app.settings_state_.choice = choice; app.settingsActivate();
        };
        choose(settings_ui::Id::companion_provider, static_cast<int>(companion::Provider::claude));
        if (app.settings_.companion_provider != AppSettings::CompanionProvider::claude || row(settings_ui::Id::companion_host) >= 0 ||
            app.settings_rows_[row(settings_ui::Id::companion_model)].choices.size() != companion::claude_models.size() ||
            app.settings_rows_[row(settings_ui::Id::companion_model)].value != L"Claude Opus" ||
            app.settings_rows_[row(settings_ui::Id::companion_token)].title != L"Klucz API Claude") return 7230;
        choose(settings_ui::Id::companion_provider, static_cast<int>(companion::Provider::ollama));
        if (row(settings_ui::Id::companion_ollama_model) < 0 || app.settings_rows_[row(settings_ui::Id::companion_host)].title != L"Adres Ollama" ||
            app.settings_rows_[row(settings_ui::Id::companion_host)].value != L"http://127.0.0.1:11434") return 7231;
        app.ensureSettingVisible();
        if (!capture(L"companion-settings-ollama.png")) return 7232;
        choose(settings_ui::Id::companion_provider, static_cast<int>(companion::Provider::foundry));
        if (row(settings_ui::Id::companion_token) < 0 || app.settings_rows_[row(settings_ui::Id::companion_token)].title != L"Token Foundry" ||
            row(settings_ui::Id::companion_model_info) >= 0 || app.settings_.companion_model_info) return 7233;
        CompanionView view; app.companionView(view);
        if (!view.status.empty()) return 7234; // Hidden by default.
        app.settings_.companion_model_info = true; app.companionView(view);
        if (view.status != L"Gemini 3.8 Flash · Foundry") return 7235;
        app.settings_.companion_model_info = false;
        app.closeSettingsPage(); // Back on Asystent's own list.

        // Her photos from her own subfolder (named by her key, whoever she is): after the chosen
        // message of a new chat, then not again today. A loose picture beside it is never hers.
        if (row(settings_ui::Id::companion_photos) >= 0 || row(settings_ui::Id::companion_import_folder) < 0 ||
            row(settings_ui::Id::companion_photo_folder) >= 0 || row(settings_ui::Id::companion_feed_folder) >= 0) return 7236;
        const auto imports = config / L"companion_import", pictures = imports / L"photos";
        const auto picture = pngFixture(150, 300, 400);
        atomic_file::write(pictures / text_encoding::wideLenient(companion::lookKey(app.companionLook())) / L"selfie.png", std::span<const uint8_t>(picture));
        atomic_file::write(pictures / L"loose.png", std::span<const uint8_t>(pngFixture(20, 30, 40)));
        app.settings_state_.selected[app.settings_state_.category] = row(settings_ui::Id::companion_import_folder);
        app.settingsActivate();
        if (!app.editingSecret()) return 7242;
        if (!capture(L"companion-import-instructions.png")) return 7243;
        app.desktop_mode_ = true;
        if (!capture(L"desktop-companion-import-instructions.png")) return 7243;
        const auto dialog = app.desktopSettingsLayout();
        const auto browse = dialog.secretBrowse();
        const auto browse_hit = app.desktopSettingsHit((browse.left + browse.right) * 0.5f, (browse.top + browse.bottom) * 0.5f);
        if (browse_hit.kind != desktop_ui::SheetHit::Kind::dialog_browse) return 7245;
        app.desktop_mode_ = false;
        // The same desktop action opens the TV picker here, so no native modal blocks the fixture.
        const auto draft = L"\"" + imports.wstring() + L"\"";
        app.settings_secret_input_ = draft;
        app.desktopSettingsActivate(browse_hit);
        if (!app.fileBrowserOpen() || !app.file_browser_.folderMode() || app.file_browser_.folder != imports ||
            !app.file_browser_.request.native || app.topModal() != Launcher::Modal::file_browser) return 7246;
        if (!capture(L"companion-import-folder-picker.png")) return 7247;
        app.fileBrowserKey(VK_ESCAPE, false);
        if (app.fileBrowserOpen() || !app.editingSecret() || app.settings_secret_input_ != draft || !app.settings_.companion_import_folder.empty()) return 7248;
        // Left from Save visits Cancel, then Browse; Enter / A opens the folder selector.
        app.settingsAdjust(-1); app.settingsAdjust(-1);
        if (!app.settings_secret_browse_ || app.settings_secret_cancel_) return 7249;
        app.settingsActivate();
        if (!app.fileBrowserOpen()) return 7250;
        app.fileBrowserButton(file_browser::Button::choose_folder);
        if (app.fileBrowserOpen() || !app.editingSecret() || app.settings_secret_input_ != imports.wstring() ||
            app.settings_secret_browse_ || app.settings_secret_cancel_ || !app.settings_.companion_import_folder.empty()) return 7251;
        app.settingsActivate();
        if (app.editingSecret() || app.settings_.companion_photo_folder != pictures.wstring() ||
            app.settings_.companion_feed_folder != (imports / L"feed").wstring()) return 7244;
        app.settings_.companion_photos = AppSettings::CompanionPhotos::off; // Old opt-out no longer blocks available pictures.
        app.back(); app.back(); app.showCategory(0);
        const auto photoChat = [&]() -> bool {
            app.companionReset(); app.companion_.greeted = true;
            app.openCompanion();
            if (!wait()) return false;
            app.companion_.photo_turn = 1; // This chat's photo comes after the first message.
            app.companionSend(L"Hej");
            if (!wait()) return false;
            for (const auto end = GetTickCount64() + 3000; GetTickCount64() < end; Sleep(5)) {
                pump();
                app.tickCompanion(0.05f);
                if (std::any_of(app.companion_.chat.log.begin(), app.companion_.chat.log.end(), [](const auto& message) { return message.photo; })) return true;
            }
            return false;
        };
        if (!photoChat()) return 7237;
        const auto& log = app.companion_.chat.log;
        const auto photo = std::find_if(log.begin(), log.end(), [](const auto& message) { return message.photo; });
        if (photo + 1 == log.end() || (photo + 1)->text.empty() || !app.companion_.photo) return 7238;
        app.companionView(view);
        if (!view.photo || view.photo->size() != picture.size()) return 7239;
        for (int i = 0; i < 300; ++i) app.tickCompanion(0.03f);
        if (!capture(L"companion-chat-photo.png")) return 7240;
        if (photoChat()) return 7241; // One a day.
        app.back();
        app.settings_.setCompanionImportFolder({});
    }
    // Closing the launcher saves the index for the next start.
    const auto saved = game_index::Index::load([&] {
        std::ifstream input(config / L"knowledge" / L"games-v1.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }());
    if (!saved || saved->keys().size() != 4 || saved->get("steam:302")->description != L"Tajemnica.") return 7228;
    return 0;
}

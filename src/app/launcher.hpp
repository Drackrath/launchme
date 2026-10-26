#pragma once

#include "../platform/audio.hpp"
#include "../platform/video_player.hpp"
#include "../platform/background_work.hpp"
#include "../platform/change_watch.hpp"
#include "../platform/updater.hpp"
#include "../platform/launch.hpp"
#include "../model/game_card.hpp"
#include "../artwork/text_crop.hpp"
#include "../model/notification.hpp"
#include "../model/lru_cache.hpp"
#include "../platform/system_audio.hpp"
#include "../platform/volume_monitor.hpp"
#include "../platform/game_controllers.hpp"
#include "../platform/device_service.hpp"
#include "../platform/library_state.hpp"
#include "../friends/steam_friends.hpp"
#include "../library/collections.hpp"
#include "../library/versions.hpp"
#include "../library/shortcuts.hpp"
#include "../library/store_adapter.hpp"
#include "../library/pc_platforms.hpp"
#include "../library/custom_games.hpp"
#include "../library/metadata_overrides.hpp"
#include "../library/drm_free.hpp"
#include "../library/steam_achievements.hpp"
#include "../library/steam.hpp"
#include "../library/steam_accounts.hpp"
#include "../library/rawg.hpp"
#include "../library/game_index_service.hpp"
#include "../net/companion_memory.hpp"
#include "../net/companion_mood.hpp"
#include "../net/companion_photos.hpp"
#include "../net/companion_tools.hpp"
#include "../net/remote_server.hpp"
#include "../artwork/identified_artwork.hpp"
#include "../ui/identify_view.hpp"
#include "../ui/bulk_covers_view.hpp"
#include "../ui/achievements_view.hpp"
#include "../platform/file_dialog.hpp"
#include "../plugins/jobs.hpp"
#include "../plugins/time_comparison.hpp"
#include "../store/add_requests.hpp"
#include "../store/gog_account.hpp"
#include "../store/epic_account.hpp"
#include "../store/ea_account.hpp"
#include "../store/web_import.hpp"
#include "../library/recent_games.hpp"
#include "../model/game_details.hpp"
#include "../model/game_session.hpp"
#include "../platform/game_activity.hpp"
#include "../platform/process_freeze.hpp"
#include "../ui/context_menu.hpp"
#include "../ui/power_menu.hpp"
#include "../ui/onscreen_keyboard.hpp"
#include "../ui/renderer.hpp"
#include "../ui/metro_layout.hpp"
#include "../ui/home_widgets.hpp"
#include "../ui/screensaver.hpp"
#include "../ui/spring.hpp"
#include "../platform/power.hpp"
#include "../platform/discord.hpp"
#include "../platform/sensors.hpp"
#include "hold_gesture.hpp"
#include "worker_messages.hpp"
#include "launcher_queues.hpp"
#include "companion_state.hpp"
#include "library_preview.hpp"
#include "../ui/overlay_window.hpp"
#include "../ui/browser_window.hpp"
#include "../ui/guide_motion.hpp"
#include "../platform/game_overlay_monitor.hpp"

#include <Windows.h>
#include <shellapi.h> // HDROP: files dropped on the desktop window.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Experimental quick resume. Injectable, so UI tests never touch real processes.
struct QuickResumeBackend {
    std::function<std::vector<GameProcess>(const Game&)> find = findGameProcesses;
    std::function<process_freeze::Result(std::vector<GameProcess>&)> suspend = process_freeze::suspend;
    std::function<process_freeze::Result(const std::vector<GameProcess>&)> resume = process_freeze::resume;
    std::function<process_freeze::Result(const std::vector<GameProcess>&)> terminate = process_freeze::terminate;
    std::function<bool(const std::vector<GameProcess>&)> alive = process_freeze::alive;
    std::function<bool(const std::vector<GameProcess>&)> focus = process_freeze::focus;
    std::function<bool(const std::filesystem::path&, const std::vector<FrozenGame>&)> save = process_freeze::save;
};

// Discord dashboard tile: launch/focus only. Injectable, so UI tests never touch a real client.
// `running` is called on the dashboard worker; `focus` and `locate` on the UI thread.
struct DiscordBackend {
    std::function<bool()> running = discord::running;
    std::function<bool()> focus = discord::focus;
    std::function<std::optional<GameAction>()> locate = discord::launchAction;
};

// Window actions are injectable: UI regression tests never steal the user's
// foreground, activate a real game, or start performance tracing.
struct GameOverlayBackend {
    GameOverlayBackend();
    std::function<bool(const GameWindow&)> valid;
    std::function<HWND()> foreground;
    std::function<bool(OverlayWindow&, const RECT&)> show;
    std::function<void(HWND)> focus;
    std::function<GameOverlayMonitor::Snapshot()> read;
};

class Launcher {
    friend class LauncherUiTest;
public:
    ~Launcher();
    bool initialize(HWND hwnd);
    void onLibrary(std::unique_ptr<LibraryMessage> library);
    void onLibraryWorkerError(std::unique_ptr<LibraryWorkerErrorMessage> message);
    void onCover(std::unique_ptr<CoverMessage> cover);
    void openBulkCovers();
    void onBulkCovers(std::unique_ptr<BulkCoversMessage> message);
    void onSteamSync(std::unique_ptr<SteamSyncMessage> message);
    bool bulkCoversOpen() const { return bulk_ui_.open; }
    void bulkCoversKey(WPARAM key, bool repeated);
    void bulkCoversClose();
    void onFriends(std::unique_ptr<FriendsMessage> message);
    void onAvatar(std::unique_ptr<AvatarMessage> message);
    void onAudio(std::unique_ptr<AudioMessage> message);
    void onMetadata(std::unique_ptr<MetadataMessage> message);
    void rebuildUninstalledSteamGames(const std::unordered_map<uint32_t, std::wstring>& cached_names = {});
    void onStore(std::unique_ptr<StoreMessage> message);
    void onArtworkBrowser(std::unique_ptr<ArtworkBrowserMessage> message);
    void onPcLibrary(std::unique_ptr<PcLibraryMessage> message);
    // A store's installation data changed (registry/manifests, startPcWatcher): one rescan.
    void onStoreChange(std::unique_ptr<StoreChangeMessage> message);
    void onStoreImport(std::unique_ptr<StoreImportMessage> message);
    void onCrop(std::unique_ptr<CropMessage> message);
    void onGameActivity(std::unique_ptr<GameActivityMessage> message);
    // Games started outside the launcher become sessions ("W grze"), as if launched here.
    void onRunningGames(std::unique_ptr<RunningGamesMessage> message);
    void onPlugins(std::unique_ptr<PluginsMessage> message);
    void onCustomGames(std::unique_ptr<CustomGamesMessage> message);
    void onDrmFree(std::unique_ptr<DrmFreeMessage> message);
    void onDashboard(std::unique_ptr<DashboardMessage> message);
    void onScreensaver(std::unique_ptr<ScreensaverMessage> message);
    void onPreview(std::unique_ptr<PreviewMessage> message);
    void onAchievements(std::unique_ptr<AchievementsMessage> message);
    void onControllerCount(size_t count);
    void onControllers(std::unique_ptr<ControllersMessage> message);
    void onHeadphones(std::unique_ptr<HeadphonesMessage> message);
    // Screensaver (src/app/screensaver.cpp). Keyboard and mouse messages pass this gate
    // first; true = swallowed (it is running, or the input that ended it is still arriving).
    bool screensaverActive() const { return saver_.active; }
    bool launchAnimationActive() const { return launch_animation_started_.has_value(); }
    bool screensaverInput(UINT message, WPARAM wparam, LPARAM lparam);
    // User-added games: Settings → Library editor panel (src/app/my_games.cpp).
    bool customEditorOpen() const { return custom_editor_.open; }
    bool libraryDialogOpen() const { return library_dialog_.open; }
    void libraryDialogKey(WPARAM key, bool repeated);
    void customEditorKey(WPARAM key, bool repeated);
    void customEditorPaste();
    // The launcher's own file/folder picker (src/app/file_browser.cpp), over every screen and panel.
    bool fileBrowserOpen() const { return file_browser_.open; }
    void fileBrowserKey(WPARAM key, bool repeated);
    void fileBrowserWheel(int amount);
    bool cropOpen() const { return screen_ == ScreenMode::crop; }
    bool downloadsOpen() const { return screen_ == ScreenMode::downloads; }
    bool glanceOpen() const { return screen_ == ScreenMode::glance; }
    bool glanceAvailable(bool allow_closing = false) const;
    void openGlance();
    void closeGlance();
    void requestGlance(bool refresh = false);
    void onGlance(std::unique_ptr<GlanceMessage> message);
    void glanceMove(int dx, int dy);
    void glanceSection(int section);
    void glanceStepSection(int amount);
    std::vector<glance::OwnedGame> glanceGames() const;
    void syncGlanceLibrary();
    bool tickGlance(float dt);
    void glanceAccept();
    void openGlanceBrowser(const std::wstring& url, bool embedded = false);
    void companionVideo(const std::wstring& url);
    void glanceClick(float x, float y);
    // Co nowego (src/app/updates.cpp): the new version's changes once after an update (Modal::whats_new).
    bool whatsNewOpen() const { return whats_new_.open; }
    void whatsNewScroll(float amount); // Logical pixels; the mouse wheel.
    // Tarot (src/app/tarot.cpp): a card reading over the home that ends with up to three games.
    // A modal layer while open (Modal::tarot); T, the pad's View button or the desktop top bar.
    bool tarotOpen() const { return tarot_.open; }
    bool tarotAvailable() const;
    void openTarot();
    void closeTarot();
    void tarotKey(WPARAM key, bool repeated);
    void tarotClick(float x, float y);   // Logical coordinates, as the renderer's viewport.
    void tarotPointer(float x, float y); // Hover.
    bool tarotPointerOver(float x, float y) const;
    bool tickTarot(float dt);
    // The home (any layout) with no menu, panel or dialog over it: Glance's and the companion's tiles.
    bool homeOverlayAvailable(bool allow_closing = false) const;
    // Where this layout puts Glance and Towarzyszka (src/ui/home_widgets.hpp), and their free-standing
    // tiles or Flix pills (none on Metro and XMB, where they are tiles of the layout).
    home_widgets::Placement homeWidgets() const;
    home_widgets::Spots homeWidgetSpots() const;
    // Flix with the quick bar's icons small at the top (AppSettings::flix_top_icons).
    bool flixIconsShown() const;
    int flixIconCount() const;
    bool flixIconClick(float x, float y, bool double_click);
    int flix_icon_hover_ = -1;
    // The TV home without games (src/ui/empty_library.hpp): its reason, its buttons and their focus.
    empty_library::Reason emptyLibraryReason() const;
    bool emptyHomeActive() const;
    empty_library::Layout emptyHomeLayout() const;
    void emptyHomeMove(int amount);
    void emptyHomeActivate(int action);
    bool emptyHomeClick(float x, float y, bool double_click);
    int empty_focus_ = 0, empty_hover_ = -1;
    // Glance's shortcut on the default home: atop the right column, or under the companion's tile while she is on.
    float glanceTop() const { return settings_.companion ? companion_ui::glance_top : glance_ui::shortcut_top; }
    // Towarzyszka (src/app/companion.cpp): her home tile and her chat screen.
    bool companionOpen() const { return screen_ == ScreenMode::companion; }
    bool companionAvailable(bool allow_closing = false) const;
    bool companionTyping() const {
        if (companionOpen() && companion_.chat.feed) return !friends_open_ && companion_.feed.view.typing();
        return companionOpen() && !friends_open_ && companion_.chat.focus == companion_ui::focus_input && !companion_.chat.ended && !companion_.chat.archive &&
               !companion_.chat.viewer && !companion_.chat.mind && !companion_.chat.feed && !companion_.picker.open;
    }
    void openCompanion();
    void closeCompanion();
    void companionMove(int dx, int dy);
    void companionAccept();
    void companionClick(float x, float y);
    std::wstring companionGameUrl(const std::wstring& title) const;
    bool companionLinkClick(float x, float y);
    void companionPointer(float mouse_x, float mouse_y);
    void companionPicker(bool open);
    void companionPickerKey(WPARAM key, bool repeated);
    void companionPickerClick(float x, float y);
    void companionChoose(companion::Look look);
    // Her photo of this chat enlarged over it (a click on it, or the "…" menu); a click, Back, Enter or A closes it.
    void companionViewer(bool open);
    bool companionPhotoShown() const; // The log shows her photo (and its picture is here).
    // "…" → "Umysł i pamięć": 3D holographic memory/knowledge sphere and character telemetry panel.
    void companionMind(bool open);
    void companionMindDeleteConfirm(bool open);
    void companionMindDeletePick(bool remove);
    // Her hidden thoughts are masked: Pokaż shows the selected one (Ukryj masks it again), the live
    // bar's click the one she is thinking now. Shown only for this visit of the screen.
    void companionMindReveal();
    void companionMindLiveReveal();
    bool companionThoughtShown(uint64_t at, std::wstring_view text) const;
    std::vector<CompanionView::MindNode> companionMindNodes() const;
    CompanionView::MindTelemetry companionMindTelemetry() const;
    void companionApplyCollections(const std::vector<companion::tools::CollectionMutation>& mutations);
    void companionType(wchar_t character);
    bool companionPasteKey(WPARAM key, bool repeated);
    void companionPaste();
    void companionErase();
    void companionSend(std::wstring text);
    void companionQuick(); // Pad Y: "W co mam zagrać?".
    void companionScroll(int amount); // + towards older messages; on her mind screen it turns the sphere.
    // The mouse wheel (WHEEL_DELTA units): scrolls her chat; on her mind screen it zooms the sphere.
    void companionWheel(int delta);
    void companionTest();
    void companionReset(bool keep_screen = false); // Another account, character or the feature switched off.
    bool tickCompanion(float dt);
    void onCompanion(std::unique_ptr<CompanionMessage> message);
    // Phone access (companion_remote.cpp): the server's work on the UI thread, as JSON for her page.
    void onRemote(std::unique_ptr<RemoteMessage> message);
    std::string remoteState();
    std::string remoteSend(std::wstring text);
    std::string remoteLook(const std::string& key);
    std::string remoteRestart();
    void companionRemoteSync(); // Starts or stops the server as Settings say.
    std::vector<companion::GameFact> companionGames() const;
    companion::Look companionLook() const;       // The host: the look chosen in Settings.
    companion::Persona companionPersona() const; // Hers.
    companion::Persona companionPersonaOf(companion::Look look) const; // Every companion keeps her own.
    avatar::Rig& companionRig(companion::Look speaker); // The guest's rig for her lines, else the host's.
    void companionFlush(); // Her reply said at once: the line being typed and every message still waiting.
    void companionBegin(); // A new conversation: she opens it (a hidden first turn).
    void companionBeginAwake(); // The same, unless she is asleep: then the empty chat waits, ended, for her morning.
    // The chat's "…" menu (Zawołaj / Pożegnaj, Wyczyść czat).
    void companionMenu(bool open);
    std::vector<companion_ui::MenuItem> companionMenuItems() const;
    void companionMenuPick(int row);
    void companionClear(); // A new conversation: the log and her context go (the old one stays in her history).
    // Her conversations between starts (src/app/companion_chat_store.cpp, net/companion_chats.hpp).
    uint64_t companionNow() const;            // Local wall-clock seconds (companion_.clock).
    void companionLoadChat();                 // Opening the chat: hers resumed, or a new day's.
    void companionSaveChat();                 // On a worker, in order.
    int companionMessages() const;            // Everything in the conversation (the cap counts it).
    int companionMessageLimit() const;        // 30 / 45 / 90, read live from the reply-length setting.
    void companionTrimTurns();                // Keep the opening and recent context, with room for her reply.
    bool companionResting() const;            // Ended, and her rest (or her night) is not over.
    // Her night by her own clock (src/app/companion_mind.cpp, mind::nightAt): in bed ~23:00–7:00
    // (an owl ~2:00–10:00), ±30 min a night; now and then a sleepless one. Asleep: nobody to talk to.
    std::optional<companion::mind::Night> companionNight(companion::Look look) const;
    bool companionAsleep(companion::Look look) const;
    bool companionBedtime() const;            // The host is off to bed now (or within the hour): her goodbye is goodnight.
    struct CompanionAway { uint64_t seconds = 0; bool sleeping = false; };
    CompanionAway companionAway() const;      // Until the host can talk again (her rest after a conversation, her night).
    // The conversation ends: her goodbye (said already, or `farewell` added now) and her rest.
    void companionEnd(bool farewell);
    void companionRestart();                  // "Nowa rozmowa", once her rest is over.
    std::wstring companionChatNote() const;   // The ended field's text.
    void companionHistoryMenu();              // "…" → "Historia czatu": the menu lists them.
    void companionOpenArchive(int index);     // One of them, read only.
    void companionCloseArchive();             // Back to the live conversation.
    // Gamification (src/app/companion_affinity.cpp): each companion's sympathy and level (1–50,
    // affinity::max_level), the streak of days and her suggestions that pay when played.
    companion::affinity::State& companionAffinity(); // Loaded on first use (companion_affinity.txt).
    int companionLevel(companion::Look look) const;  // 1 until loaded.
    // Their stage (affinity::effectiveStage; gamification off: affinity::unlevelled_stage) and how
    // much jealousy fits her now (affinity::jealousy: her character's exclusivity, her day).
    companion::affinity::Stage companionStage(companion::Look look) const;
    companion::affinity::Jealousy companionJealousy(companion::Look look) const;
    // Who she talks with (companion_person.hpp): Settings → "Twoje imię lub ksywka" and "Twoja
    // płeć", else the gender she learnt from the chat. companionSyncPerson hands it to every request.
    companion::Person companionPerson() const;
    void companionSyncPerson() const;
    // The host's, while Settings → "Grywalizacja" is on; `reason` shows a toast (games, not messages).
    void companionAffinityGain(int64_t points, const wchar_t* reason = nullptr);
    void companionAffinityGainFor(companion::Look look, int64_t points, const wchar_t* reason = nullptr);
    void companionAffinityReset();                    // The host's back to level 1.
    // Her [granica] in a chat reply (alone, below affinity::boundary_level): a yellow card first,
    // then a red one with a small penalty that never takes a level; a toast and the badge's card.
    void companionBoundary();
    void companionRelationshipShift(const companion::Reply& reply);
    void companionRelationMenu();  // "…" → "Relacja i dystans": track, player distance cap and player gender.
    void companionRelationPick(int code);
    void saveCompanionAffinity();
    void companionLevelUp(companion::Look look, int from, int level); // Toast, badge pulse, her remark.
    void companionAffinitySuggested(const std::vector<std::wstring>& titles); // [[Titles]] of her reply.
    // A finished session: her suggestion (lately) or a first go at a never-played game pays.
    void companionAffinityPlayed(const std::string& key, uint64_t played_ms, const companion::Progress& before);
    companion::PromptExtras::Bond companionBond() const; // Her prompt's stage (level 0: gamification off).
    // Her costumes (companion_costumes.hpp): every character's from the embedded manifest, or a test's.
    const std::vector<companion::costumes::Costume>& companionCostumes() const;
    int companionCostume(companion::Look look) const; // The slot she wears (a locked choice: 0).
    std::wstring companionCostumeName(const companion::costumes::Costume& costume) const;
    bool companionCostumeUnlocked(const companion::costumes::Costume& costume) const;
    void companionCostumeMenu();   // "…" → "Zmień strój": the menu lists her costumes.
    void companionWear(int slot);
    // "…" → "Zresetuj sympatię": a dialog asks first (its red button resets, Anuluj keeps it).
    void companionResetConfirm(bool open);
    void companionResetPick(bool reset);
    std::vector<std::wstring> companionDiagnosticsLines() const; // "Pokaż diagnostykę": where the last answer's time went.
    // Her guests (src/app/companion_guest.cpp): another companion in the chat for a while.
    std::vector<companion::Look> companionGuestCandidates() const; // Every look but the host's and those asleep now.
    std::optional<companion::Guest> companionGuestPrompt() const;  // The guest the next request writes for.
    void companionInvite();     // "Zawołaj": one of the others comes in (she stays until sent off).
    void companionDismiss();    // "Pożegnaj": she says goodbye and goes.
    // After the player's message: a guest may drop in (once a day) or run off; returns the offline
    // line for that moment (empty: her usual answer).
    std::wstring companionGuestTyped();
    void companionGuestLeave(companion::GuestMoment moment); // Her goodbye asked for; she goes once it is said.
    void companionGuestGone(bool told); // She has left; `told`: the model hears it in a hidden turn.
    int companionGuestDay();    // yyyymmdd of the last visit on her own (companion_guest.txt).
    bool tickCompanionGuest(float dt);
    // A request on a worker. `kickoff` replaces the hidden opening turn of home and test requests;
    // `fallback` replaces her offline line (both for events and the roulette).
    void requestCompanion(CompanionMessage::Purpose purpose, std::wstring kickoff = {}, std::wstring fallback = {}, std::wstring shared_text = {});
    void companionChips(const std::vector<std::wstring>& titles, const std::vector<std::wstring>& prompts = {});
    // A title she wrote in [[ ]] → its library game: the digest she saw, else the game index.
    std::string companionGameKey(const std::wstring& title) const;
    // Her provider as Settings choose it; a worker resolves its credentials (DPAPI file,
    // environment, opencode) with resolve().
    struct CompanionAccess {
        companion::Provider provider = companion::Provider::foundry;
        std::wstring host;  // Foundry's stack or the Ollama server; empty: the environment/default.
        std::string model;
        std::filesystem::path config;
        std::function<companion::Probe()> probe;
        companion::Connection resolve() const;
    };
    CompanionAccess companionAccess() const;
    // Her memory (src/net/companion_memory.hpp): loaded on first use, saved on a worker.
    companion::Memory& companionMemory();
    void saveCompanionMemory();
    companion::cognition::State& companionCognition();
    void saveCompanionCognition();
    // Her mind (src/app/companion_mind.cpp, net/companion_mind.hpp): its state per companion, its
    // context and the moments that move it — the player's message (System 1, her face), her reply
    // (her appraisal), a conversation's start (gossip leaks) and end, a game session — her workspace
    // candidates for a prompt, her background web reading, her life for her diary and her Mind view.
    companion::mind::State& companionMindState();
    void saveCompanionMind();
    companion::mind::Context companionMindContext(companion::Look look) const;
    std::vector<companion::mind::Candidate> companionMindCandidates(companion::Look look, const std::wstring& query, bool chatting);
    void companionMindHeard(const std::wstring& text);
    void companionMindReplied(const CompanionMessage& message);
    void companionMindBegin();
    void companionMindEnd();
    void companionMindPlayed(const std::string& key, uint64_t played_ms);
    void companionDailyReading();
    // Her life in the background, while the launcher runs: a look every ten minutes (tick) — her
    // web reading when due, her diary filling in a long gap (companionLifeFill).
    void pollCompanionLife();
    void companionLifeFill();
    bool tickCompanionInitiative(float dt); // Spontaneous initiative (#21) when the chat pauses or after rest ends.
    // Her diary's plans or a gap filled in (lines: mind::parseEpisodes): into her life, and into
    // another companion's when she is in one.
    void companionMindEpisodes(std::string_view look_key, std::wstring_view lines, bool planned, uint64_t from);
    std::wstring companionOthersText(companion::Look look) const; // "Saki (Paryż), …": the other companions and their cities.
    std::wstring companionMindLife(companion::Look look, bool* invite);
    std::wstring companionPlanRules(companion::Look look) const;  // Her diary's plans: mind::planRules (empty: her day is planned).
    bool companionMindPeek(std::string_view id);                  // The Mind view's Odkryj: a private episode opened.
    struct CompanionMindItem {
        std::wstring label, category, detail, when;
        uint64_t at = 0;
        bool locked = false; // A private episode not yet told or opened: the Mind view keeps it closed.
        std::string key;     // Its episode id.
        bool inner = false;  // Her mood: the inner shell; the rest is her life.
        bool short_term = false; // True when stored in working / short-term memory, false when consolidated in long-term memory.
        bool thought = false;    // A mental act (her curiosity): drawn as a thought (the Myśli filter), never masked.
        bool active = false;     // What she is doing right now (about to ask, waiting for his answer): lit up.
    };
    std::vector<CompanionMindItem> companionMindItems(companion::Look look) const;
    // The real weather where she is (src/net/companion_weather.hpp): her trip's town or her home city
    // (`travelling` says which), refreshed in the background every 45 minutes or when she moves.
    std::wstring companionWeatherPlace(companion::Look look, bool* travelling) const;
    // Settings → Gdzie mieszkają into companion::setHomeCities (their clocks, homes and weather).
    void companionApplyHomes();
    void companionWeatherRefresh();
    // Her current report when it is fresh (three hours) and still where she is; nullptr otherwise.
    const companion::weather::Report* companionWeatherNow(companion::Look look) const;
    void companionForget();          // Settings → "Wyczyść pamięć".
    void requestCompanionSummary();  // A finished chat → a sentence for her notes (notes on).
    void companionRemember(const CompanionMessage& message); // Her reply, suggestions and notes.
    // Her mood as a conversation starts (src/net/companion_mood.hpp, memory on): loaded on first
    // use from companion_mood.txt, saved on a worker in order.
    companion::mood::State& companionMoodState();
    void saveCompanionMood();
    void companionMoodPlayed(const std::string& key, uint64_t played_ms); // A session ended: her suggestion? a late night?
    // Her reply arriving word by word (Gemini streaming): her last message grows as it comes.
    void companionStream(const std::wstring& heard);
    // Her photos (src/app/companion_photos.cpp): once per chat after the player's 2nd/3rd/5th
    // message, at most one a day; from her own subfolder of the chosen folder.
    void companionPhotoChat();          // A conversation begins: when she will send one.
    void companionFeed(bool open);
    void companionFeedReset();
    void pollCompanionFeed(bool force = false);
    void onCompanionFeed(std::unique_ptr<CompanionFeedMessage> message);
    void companionFeedImages();
    void companionFeedScroll(float amount);
    void companionFeedDiscuss(int index);
    void companionFeedAction(int hit);
    void companionFeedMove(int dx, int dy);
    void companionFeedComment();
    void companionFeedBack();
    bool tickCompanionFeed(float dt);
    std::vector<companion::feed::CommentAuthor> companionFeedAuthors(uint32_t seed);
    void companionPhotoTyped();         // The player sent a message.
    void onCompanionPhoto(std::unique_ptr<CompanionPhotoMessage> message);
    bool tickCompanionPhoto(float dt);  // A waiting photo appears once her line is out.
    std::vector<settings_ui::Row> companionPhotoRows() const;
    bool companionPhotoSetting(const settings_ui::Row& row); // Settings rows of hers; true when handled.
    void companionView(CompanionView& view) const;
    void companionMindFilter(companion_ui::MindFilter filter);
    void companionMindNextFilter();
    void companionBackup();
    void onCompanionBackup(std::unique_ptr<CompanionBackupMessage> message);
    std::optional<companion::chats::Conversation> companionChatSnapshot() const;
    std::vector<settings_ui::Row> companionSettingsRows() const; // Asystent (companion_settings.cpp).
    void browseCompanionImport(); // Folder picker for the open import-path dialog; fills its draft.
    std::vector<settings_ui::Row> companionVoiceRows() const;    // Its voice rows (ElevenLabs).
    std::vector<settings_ui::Row> companionModelRows() const;    // Provider, model, credentials, test.
    std::vector<settings_ui::Row> companionRemoteRows() const;   // Phone access: the switch and its addresses.
    bool companionSetting(const settings_ui::Row& row);          // Enter on one of those rows; true when handled.
    // Her reactions to the launcher (src/app/companion_events.cpp): the game hooks of
    // game_sessions.cpp and library_view.cpp report what happened; pumpCompanionEvents lets her
    // say one remark at a time in the home bubble while the player is looking.
    void companionNotice(companion::Event event);
    void companionSessionStarted(const Game& game, bool launched_here);
    void companionSessionEnded(const Game& game, uint64_t played_ms);
    void companionLibraryChanged(std::vector<std::string> added_keys);
    std::vector<companion::RosterEntry> companionRoster() const;
    void requestCompanionPremieres();
    void onCompanionEvent(std::unique_ptr<CompanionEventMessage> message);
    void pumpCompanionEvents();
    static std::vector<std::wstring> glancePremieres(bool english, int today_yyyymmdd); // Once a day, from Glance's calendar.
    // Her voice (src/app/companion_voice.cpp): ElevenLabs reads her line in the interface's
    // language while her text appears; in a duet each companion speaks with her own voice.
    bool companionVoiceFor(bool chat) const; // Voice on for the chat, or for her home remarks too.
    void companionSpeak(const std::wstring& text, companion::Emotion emotion);
    void companionSpeakLines(const std::vector<companion::Line>& lines);
    void companionHush();
    void companionVoiceTest();
    void requestCompanionVoiceStatus();
    void onCompanionVoice(std::unique_ptr<CompanionVoiceMessage> message);
    bool tickCompanionVoice(float dt);       // Her mouth follows the audio; true while she speaks.
    bool companionVoiceHolding() const { return companion_.voice.pending && companion_.voice.waited < 4.0f; } // Text waits for audio.
    // Who speaks for a companion: Settings' voice and model, else her character.txt's, then a built-in voice.
    elevenlabs::Speaker companionVoiceSpeaker(companion::Look look) const;
    std::wstring companionVoiceName(companion::Look look) const; // The voice she speaks with now ("Zofia").
    std::wstring companionVoiceProblem() const; // What went wrong, in words (empty: nothing).
    // The game index (src/app/game_index.cpp): what the launcher knows about each library game —
    // dates, genres, tags, studios, scores, HowLongToBeat — for her tools, Szczegóły and the
    // library's year sort and genre filter. Its worker reads the caches and fills gaps slowly.
    void startGameIndex();
    void syncGameIndex();     // The library changed: a snapshot goes out once it settles.
    void tickGameIndex();
    void gameIndexTouch(std::vector<std::string> keys); // Their cached descriptions changed.
    void gameIndexMerge(const std::string& key, game_index::Source source, game_index::Patch patch);
    void onGameIndex(std::unique_ptr<GameIndexMessage> message);
    // Szczegóły: the fields the store page/local data left empty, from the index; true when filled.
    bool completeDetails(GameDetails& details, const Game& game) const;
    std::vector<game_index::LibraryGame> gameIndexGames() const;
    // The game roulette in her chat (src/app/companion_roulette.cpp); her verdict waits in
    // companion_.held until the reel stops.
    void companionRoulette();
    bool companionRouletteSpinning() const;
    bool tickCompanionRoulette(float dt);
    // Pobierania (src/app/downloads.cpp): the games with an install in progress and their rows.
    std::vector<Game> collectDownloads(std::vector<downloads_ui::Row>& rows) const;
    int downloadsMoving() const;
    void openDownloads();
    void refreshDownloads();
    void requestDownloadsArt();
    void requestDownloadArt(const Game& game);
    void pollDownloadArt();
    void onDownloadArt(std::unique_ptr<DownloadArtMessage> message);
    download_art::Target downloadArtTarget(const Game& game) const;
    void downloadsMove(int amount);
    void downloadsAccept();
    void downloadsClick(float x, float y);
    bool downloadsHover(float x, float y) const;
    bool artworkOpen() const { return screen_ == ScreenMode::artwork; }
    void artworkMove(int horizontal, int vertical);
    void artworkAccept();
    void artworkPage(int amount);
    // Metro home: LB/RB, Page Up/Down move a whole page (and keep each page's focused tile).
    void homePage(int amount);
    void artworkWheel(float mouse_x, int amount);
    void deviceWheel(float mouse_x, int amount);
    void resize();
    void tick();
    void navigate(int amount, bool edge_menu = true);
    // Details: one step by screen position (top row, button column, info column) instead of action index.
    void detailsMove(int dx, int dy);
    void wheel(int amount, float mouse_x = -1); // Mouse wheel on card screens; optional client coordinate.
    void changeCategory(int amount);
    bool quickOpen() const { return quick_open_; }
    void openQuick();
    void closeQuick();
    void quickMove(int amount);
    void quickAccept();
    void back();
    void onCharacter(wchar_t character);
    // `repeated`: a held Backspace; it erases, but never closes a field or leaves a screen.
    void eraseSearchCharacter(bool repeated = false);
    // Text input goes to a search field: the search screen, Store, the library's own filter or a
    // plugin page's search field.
    bool searching() const { return screen_ == ScreenMode::search || screen_ == ScreenMode::store || libraryTyping() || pluginPageTyping() || companionTyping(); }
    bool libraryTyping() const { return screen_ == ScreenMode::library && library_controls_.typing(); }
    bool pluginPageTyping() const { return screen_ == ScreenMode::plugin_page && !plugin_drawer_active_ && !friends_open_ && plugin_page_.typing(); }
    // Library screen: a typed character opens its search field (so A/D do not navigate there
    // and Space does not pick a game).
    bool libraryScreen() const { return screen_ == ScreenMode::library; }
    bool libraryTypeAhead() const;
    bool editingSecret() const;
    bool editingArtworkSearch() const { return artworkOpen() && artwork_state_.search_focus; }
    void clearArtworkQuery(); // "Wyczyść" / Delete in the artwork search field.
    void selectSecret();
    void pasteSecret();
    void eraseSecret();
    void retryStore();
    bool settingsOpen() const { return screen_ == ScreenMode::settings; }
    bool gogInstallOpen() const { return gog_install_ui_.open; }
    void gogInstallKey(WPARAM key, bool repeated);
    void settingsMove(int amount);
    void settingsAdjust(int amount);
    void settingsActivate();
    void settingsReorder(int amount);
    // LB/RB, PgUp/PgDn: reorder a collection, or the focused sound source's volume (5% steps).
    void settingsShoulder(int amount);
    // Kinowy / Przegląd: the category tab strip over the library (LB/RB, PgUp/PgDn, click).
    bool libraryTabsShown() const;
    void switchLibraryTab(int amount);
    void settingsWheel(float mouse_x, int amount);
    void mouseMove(float mouse_x, float mouse_y);
    void mouseLeave();
    void pointerUp(bool cancelled = false);
    void saveSettings();
    // Ctrl+H: the hidden and private games as one more category at the bottom (every layout).
    void togglePrivateCategory();
    bool categoryMenuOpen() const { return category_menu_open_; }
    void categoryMenuMove(int amount);
    void categoryMenuHorizontal(int amount, bool repeated); // Keyboard Left/Right (also A/D).
    void scrollCategoryMenu(int amount);
    void categoryMenuAccept();
    void closeCategoryMenu();
    bool overCategoryMenu(float mouse_x) const;
    void tabDown(bool repeated);
    void tabUp();
    void cancelTabHold();
    // Keyboard Home mirrors the pad Guide/PS button: short = menu, hold = power.
    void homeDown(bool repeated);
    void homeUp();
    bool powerMenuOpen() const { return power_menu_.has_value(); }
    void powerMenuKey(WPARAM key, bool repeated);
    // Right drawer (src/app/friends.cpp): dashboard tiles, the Steam friends list inside it.
    bool friendsOpen() const { return friends_open_; }
    void closeFriends();
    // Back: from the friends list to the tiles, from the tiles closes the drawer.
    void friendsBack();
    void scrollFriends(int amount);
    void friendsMove(int amount); // Vertical: list rows or tile rows.
    void friendsAccept();         // A/Enter/Space: activate the tile or chat with the friend.
    void friendsChat();
    bool overFriends(float mouse_x) const;
    void enterDown(bool repeated);
    void enterUp();
    void cancelEnterHold();
    bool acceptHoldsForMenu() const;
    // On-screen keyboard for the pad (src/app/keyboard.cpp).
    bool keyboardOpen() const { return keyboard_.open; }
    // Keys or the mouse: a text field reached from them does not open the keyboard by itself.
    void noteDesktopInput() { pad_input_ = false; }
    void autoKeyboard(); // A field entered from the pad shows the keyboard (tick()).
    size_t keyboardPreviewCaret() const; // Insertion point within keyboardPreview().
    bool textFieldActive() const;
    void openKeyboard();
    void closeKeyboard();
    void keyboardMove(int dx, int dy);
    void keyboardPress(const osk::Key& key);
    void keyboardBackspace();
    void keyboardDone();
    void keyboardCaret(int amount);
    std::wstring keyboardPreview() const;
    bool keyboardClick(float x, float y); // Logical coordinates; true when the keyboard took it.
    bool menuOpen() const { return menu_open_; }
    void menuMove(int amount);
    void menuAccept();
    void openContextMenu();
    void contextClick(float mouse_x, float mouse_y);
    void dismissMenu();
    void launch();
    // Picking the focused card (Enter, A, Space, double click): a game's details when Appearance →
    // "Domyślna akcja gry" asks for them, otherwise launch() (launch/install, tiles, dialogs).
    void accept();
    void click(float mouse_x, float mouse_y, bool double_click);
    bool hoverButton(float mouse_x, float mouse_y) const;
    void toggleFullscreen();
    void focusRegained();
    // Windows is ending the session (WM_QUERYENDSESSION/WM_ENDSESSION): play time, places and
    // settings reach the disk now, because the process ends without running ~Launcher.
    void saveSessionState();
    // Resolution, monitor layout or DPI changed: full-screen Tryb TV covers its monitor again.
    void displayChanged();
    bool coversMonitor() const { return !desktop_mode_ && fullscreen_; }
    void placeOnTvMonitor();  // Start: the monitor Tryb TV covered last, when it is connected.
    void rememberTvMonitor();
    bool applyAutostart(bool enabled);
    // Back from sleep/hibernation: audio output and pads are found again.
    void resumedFromSleep();
    // GUID_CONSOLE_DISPLAY_STATE: the screen Windows switched off (or back on).
    void displayPower(bool on) { display_off_ = !on; if (on && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    bool displayOff() const { return display_off_; }
    bool display_off_ = false;
    void paint(bool overlay = false);
    // Paints follow the frame clock. A WM_PAINT between two frames (a decoded image, a wheel
    // notch, an arrived cover) is folded into the next frame instead of presenting an extra,
    // unpaced one: those starved the frame loop and showed the same scroll position twice.
    // deferPaint(): true when the next frame will paint (the caller validates the window);
    // false inside updateFrame(), while moving/sizing, or when frames stopped (modal loops).
    bool deferPaint();
    // After tick(): paints now if anything asked for a paint since the last frame.
    void updateFrame();
    void sizeMove(bool active) { size_move_ = active; }
    static constexpr UINT_PTR guide_timer = 0x4C47;
    static constexpr UINT_PTR foreground_scan_timer = 0x4C48;
    static constexpr int guide_hotkey = 0x4C47;
    // EVENT_SYSTEM_FOREGROUND from the WinEvent hook (main.cpp): a game may have started.
    void foregroundChanged();
    void foregroundScanDue();
    void pollGuideInput();
    void toggleGameOverlay();
    void guideKeyboardHotkey();
    bool gameOverlayOpen() const { return overlay_target_.has_value(); }
    HWND frameWindow() const { return gameOverlayOpen() && game_overlay_ ? game_overlay_->handle() : hwnd_; }
    void togglePerformance();
    void noteUserInput();
    bool identifyOpen() const { return identify_state_.open; }
    void identifyKey(WPARAM key, bool repeated);
    void onRatings(std::unique_ptr<RatingsMessage> message);
    void onVideo(std::unique_ptr<VideoMessage> message);
    void onScreenScraperTest(std::unique_ptr<ScreenScraperTestMessage> message);
    void onVideoMove(std::unique_ptr<VideoMoveMessage> message);
    void onCacheCompacted(std::unique_ptr<CacheCompactedMessage> message);
    void identifySwitchSource(); // "Zidentyfikuj grę" on an emulated game: RAWG ⇄ ScreenScraper.
    void testScreenScraper();  // Settings → ScreenScraper → "Sprawdź połączenie" (ssuserInfos.php).
    void clearScreenScraper(); // "Wyczyść dane ScreenScraper": identifications and pauses.
    void applyEmulationNames(); // emulation_names: ES-DE/ScreenScraper titles on emulated shortcuts.
    void onIdentify(std::unique_ptr<IdentifyMessage> message);
    void focusLibraryControls();
    void setFrameRate(int target, int refresh) { target_hz_ = target; refresh_hz_ = refresh; }
    void controllerDeviceChanged(HANDLE device) { controller_input_.deviceChanged(device); controller_rescan_->request(); }
    // WM_DEVICECHANGE: a device arrived or left (a Bluetooth headset, a pad Raw Input misses).
    void devicesChanged() { controller_rescan_->request(); headphones_rescan_->request(); }
    bool devicesOpen() const { return screen_ == ScreenMode::devices; }
    // Dźwięk systemowy: the focus rests on a volume slider (Left/Right change the volume).
    bool volumeSliderFocused() const {
        return devicesOpen() && device_state_.panel == devices::Panel::audio && device_state_.selected >= 0 &&
            device_state_.selected < static_cast<int>(device_state_.rows.size()) &&
            device_state_.rows[device_state_.selected].kind == device_ui::Kind::volume;
    }
    void rawControllerInput(HRAWINPUT input) { controller_input_.rawInput(input); }
    // Experimental quick resume (src/app/quick_resume.cpp).
    // WM_CLOSE: true while confirming suspended games or playing the exit animation.
    bool requestClose();
    bool closing() const { return closing_; }
    bool exitPromptOpen() const { return exit_prompt_.has_value(); }
    void exitPromptKey(WPARAM key, bool repeated);
    // First-run walkthrough (src/app/tour.cpp): while open it takes every key, click and pad button.
    bool tourOpen() const { return tour_.open; }
    void tourKey(WPARAM key, bool repeated);
    // Modal layers (src/app/input.cpp), topmost first: the order Renderer::paint draws them in, the
    // exit prompt over the power menu over the walkthrough … the GOG/Epic installer lowest. The
    // topmost open one takes keys, pad buttons, the wheel and clicks before any screen, drawer or
    // menu. The on-screen keyboard is not a layer: it types into the field of the layer beneath.
    enum class Modal { none, exit_prompt, power_menu, tour, file_browser, library_dialog, custom_editor, bulk_covers, identify, gog_install, companion_picker, tarot,
                       whats_new };
    Modal topModal() const;
    // The topmost of the dialogs under the walkthrough (file picker … installer): Back and typed
    // characters reach it even under the exit prompt or the power menu.
    Modal topDialog() const;
    // A key while a layer is open goes to that layer's handler (true). False: no layer is open, or
    // Home is the Guide gesture there.
    bool modalKey(WPARAM key, bool repeated);
    // Keyboard Home acts as the pad's Guide button (homeDown) unless a field or a dialog uses it.
    bool homeKeyIsGuide() const;
    // Logoff, shutdown, launcher exit or the feature turned off: games must not stay suspended.
    void resumeFrozenGames();
    // Desktop mode (src/app/desktop.cpp, desktop_window.cpp; src/ui/desktop_view.hpp): the windowed
    // library for the mouse. The console layouts are Tryb TV. desktopActive(): its library is shown
    // (Settings, Glance, Szczegóły… stay the TV screens, inside the window).
    bool desktopMode() const { return desktop_mode_; }
    bool desktopActive() const { return desktop_mode_ && screen_ == ScreenMode::shelves; }
    void setDesktopMode(bool desktop);
    bool startMaximized() const { return desktop_mode_ && settings_.desktop_maximized; }
    // The taskbar's Jump List (src/app/jump_list.cpp): a picked entry's command ("launch:<hex key>",
    // "settings", "tv"), from this process's arguments or a second one (WM_COPYDATA).
    void jumpCommand(const std::wstring& command);
    // The window's own top bar (WM_NCCALCSIZE, WM_NCHITTEST in main.cpp): client pixels → HTCAPTION
    // over its empty parts, HTMAXBUTTON over maximize (Windows 11 snap layouts), else HTCLIENT.
    LRESULT desktopNonClientHit(int x, int y) const;
    void desktopMaximizeHover(bool hover);
    void desktopMaximizePress(bool down);
    void windowPlacementChanged();
    void windowActivated(bool active);
    void minimumWindowSize(MINMAXINFO& info) const;
    void dropFiles(HDROP drop);
    // Keys, the pointer and the wheel while the desktop library is shown; false: not handled.
    bool desktopKey(WPARAM key, bool repeated);
    void desktopPointerUp(float mouse_x, float mouse_y);
    bool desktopWheel(float mouse_x, float mouse_y, int delta, bool control);
    LPCWSTR desktopCursor(float mouse_x, float mouse_y) const; // nullptr: the usual cursor.
    // Urządzenia, kadrowanie and plugin pages inside a desktop sheet (desktop_settings.cpp): client
    // pixels → their canvas's pixels (unchanged elsewhere).
    bool desktopEmbedded() const;
    void desktopEmbedMap(float& mouse_x, float& mouse_y) const;

private:
    // Worker queues (app/launcher_queues.hpp).
    using Job = launcher_queues::Job;
    using JobQueue = launcher_queues::JobQueue;
    using StoreRequest = launcher_queues::StoreRequest;
    using StoreQueue = launcher_queues::StoreQueue;
    using MusicQueue = launcher_queues::MusicQueue;
    using TrackedGame = launcher_queues::TrackedGame;
    using GameActivityQueue = launcher_queues::GameActivityQueue;
    using RunningScanQueue = launcher_queues::RunningScanQueue;
    using ArtworkBrowserJob = launcher_queues::ArtworkBrowserJob;
    using ArtworkBrowserQueue = launcher_queues::ArtworkBrowserQueue;
    using AchievementsJob = launcher_queues::AchievementsJob;
    using AchievementsQueue = launcher_queues::AchievementsQueue;
    using DashboardQueue = launcher_queues::DashboardQueue;
    using ScreensaverRequest = launcher_queues::ScreensaverRequest;
    using ScreensaverQueue = launcher_queues::ScreensaverQueue;
    using SteamSyncQueue = launcher_queues::SteamSyncQueue;
    using EsdeWatch = launcher_queues::EsdeWatch;
    // Towarzyszka (app/companion_state.hpp; her code: src/app/companion*.cpp).
    CompanionState companion_;
    // Phone access (companion_remote.cpp): the server, this computer's addresses and its public one.
    std::unique_ptr<remote::Server> remote_;
    std::vector<remote::Address> remote_addresses_;
    std::wstring remote_public_;
    bool remote_failed_ = false; // The port was taken.
    bool remote_loopback_ = false; // UI tests: 127.0.0.1 only, no public-address lookup.
    // Glance (src/app/glance.cpp) and its in-app browser.
    std::unique_ptr<BrowserWindow> browser_;
    std::function<void(const std::wstring&)> browse_; // Offline UI tests capture the action.
    glance_ui::State glance_;
    bool glance_hover_ = false, glance_busy_ = false, glance_pending_ = false;
    bool glance_pending_refresh_ = false;
    std::shared_ptr<std::atomic_int> glance_image_focus_ = std::make_shared<std::atomic_int>(0);
    Spring glance_hover_motion_;
    float glance_shortcut_progress_ = 1;
    std::vector<glance::OwnedGame> glance_roster_;
    uint32_t glance_generation_ = 0;
    ULONGLONG glance_requested_ = 0;
    std::shared_ptr<std::atomic_bool> glance_cancel_ = std::make_shared<std::atomic_bool>(false);
    // Injectable transport; the UI and parser tests never contact real providers.
    glance::Get glance_get_ = httpGet;
    // Tarot (src/app/tarot.cpp): the reading on screen, its seed, the store search in flight.
    tarot_ui::State tarot_;
    uint32_t tarot_seed_ = 0, tarot_generation_ = 0;
    std::shared_ptr<std::atomic_bool> tarot_cancel_ = std::make_shared<std::atomic_bool>(false);
    std::vector<tarot::Pick> tarot_pending_; // Picks waiting for the shuffle to end.
    glance::Get tarot_get_ = httpGet;        // Steam's search; UI tests answer it themselves.
    void tarotAgain();
    void tarotMove(int dx, int dy);
    void tarotAccept();
    void tarotChoose(int card);
    void tarotBack();
    void tarotDivine();
    void tarotStoreFinished(uint32_t generation, std::vector<tarot::Pick> picks, bool failed);
    void tarotReveal();
    void tarotOpenResult(int index);
    std::vector<tarot::Pick> tarotLibraryPicks(const tarot::Wish& wish) const;
    void bindTarotArt();
    // A poster or cover that arrived (onCover): bound to the result showing that game.
    void tarotArtArrived(const std::string& key, CoverMessage::Kind kind, const ImageBytes& image);
    // Her premiere remarks from Glance's calendar (companion_events.cpp); UI tests read no calendar.
#ifdef LAUNCHME_UI_TEST
    std::function<std::vector<std::wstring>(bool, int)> companion_premieres_ = [](bool, int) { return std::vector<std::wstring>{}; };
#else
    std::function<std::vector<std::wstring>(bool, int)> companion_premieres_ = &Launcher::glancePremieres;
#endif
    bool controller_left_trigger_ = false; // L2/LT held: its press opens and closes her chat.
    // The game index (src/app/game_index.cpp).
    std::shared_ptr<game_index::Service> game_index_;
    bool game_index_dirty_ = false, game_index_filling_ = false;
    std::chrono::steady_clock::time_point game_index_changed_{};
    ULONGLONG game_index_started_ = 0, game_index_later_at_ = 0;
    std::vector<std::string> game_index_later_; // Touched once game_index_later_at_ passes.
    uint64_t game_index_revision_ = 0;
    // Game lengths for the index (game_index.cpp): the HowLongToBeat plugin asked about one owned
    // game at a time, quietly, while nothing else needs it; installed and recently played first.
    void fillGameLengths();
    ULONGLONG length_fill_at_ = 0;
    int length_fill_misses_ = 0;                  // Lookups in a row that found no time: a long pause after a few.
    static constexpr int length_fill_pause_after = 6;
    std::unordered_set<std::string> length_fill_tried_; // This start's attempts.
    std::shared_ptr<std::atomic_bool> game_index_posted_ = std::make_shared<std::atomic_bool>(false);
    // Tests adjust the worker (transports, no background requests).
#ifdef LAUNCHME_UI_TEST
    std::function<void(game_index::ServiceConfig&)> game_index_configure_ = [](game_index::ServiceConfig& config) {
        config.background = false; config.steam_root.clear(); config.steam_stats = {}; config.rawg_key.clear(); // Never this machine's Steam or the network.
    };
#else
    std::function<void(game_index::ServiceConfig&)> game_index_configure_;
#endif
    // Pobierania (src/app/downloads.cpp).
    downloads_ui::State downloads_state_;
    std::vector<Game> downloads_games_;
    std::chrono::steady_clock::time_point downloads_refreshed_{};
    struct PluginComparison {
        plugins::TimeComparison value;
        std::wstring name;
        std::string locale;
        uint32_t generation = 0;
        uint64_t checked_at = 0;
        bool pending = false;
    };
    struct PluginPlaytime {
        std::optional<uint64_t> seconds, session_started;
        uint64_t session_ms = 0, checked_at = 0;
        bool pending = false;
    };
    std::map<std::string, PluginComparison> plugin_comparisons_;
    std::map<std::string, PluginPlaytime> plugin_playtimes_;
    uint32_t plugin_details_generation_ = 0, plugin_widget_generation_ = 0;
    uint64_t next_plugin_widget_refresh_ = 0;
    std::string plugin_widget_scope_;
    Game plugin_page_game_;
    std::optional<Game> pluginActiveGame() const;
    std::string pluginWidgetScope() const;
    std::optional<plugins::Action> pluginDetailProvider() const;
    int pluginDetailAction() const;
    void openPluginDetail();
    void pollPluginGameViews();
    void requestGameComparisons(const Game& game, bool force = false);
    static std::string pluginComparisonKey(const std::string& plugin, const Game& game);
    const PluginComparison* pluginComparison(const std::string& plugin, const Game& game) const;
    void ensurePluginPlaytime(const Game& game);
    std::optional<uint64_t> pluginPlaytimeSeconds(const Game& game) const;
    json::Value pluginGameParams(const Game& game) const;
    bool receivePluginGameView(const plugins::Result& message);
    std::wstring pluginWidgetText(size_t index) const;
    // Achievements of games outside Steam from a plugin (src/app/plugin_achievements.cpp): the
    // last answer per game key, the list the achievements screen shows and the counts the card,
    // the game index and the previews use. Store requests go through store_gateway_.
    struct PluginAchievements {
        std::string plugin, locale;
        std::wstring name;               // The game's title when asked (a rename asks again).
        SteamAchievements list;          // Empty: none for this game, or not known yet.
        std::wstring source, message;
        uint32_t generation = 0;
        uint64_t checked_at = 0;
        bool pending = false, answered = false;
    };
    std::map<std::string, PluginAchievements> plugin_achievements_;
    uint32_t plugin_achievements_generation_ = 0;
    std::shared_ptr<launcher_queues::StoreGateway> store_gateway_ = std::make_shared<launcher_queues::StoreGateway>();
    std::optional<plugins::Action> pluginAchievementsProvider(const Game& game) const;
    const PluginAchievements* pluginAchievements(const Game& game) const;
    // Launchme's own updates (src/app/updates.cpp, src/platform/updater.hpp): a look at GitHub's
    // latest release now and then, a notification pointing to Ustawienia → System → Aktualizacje
    // (and the desktop top bar's icon), then one click downloads, checks, swaps the files in and
    // restarts; the new version shows its changes once (Modal::whats_new).
    struct UpdateState {
        enum class Phase { idle, checking, downloading, ready, installing, failed } phase = Phase::idle;
        std::optional<updater::Release> available; // Newer than this version.
        std::vector<std::filesystem::path> files;   // Phase::ready: the unpacked package.
        uint64_t done = 0;                          // Downloaded bytes.
        std::wstring error;
        std::wstring announced;                     // The version already notified in this run.
        ULONGLONG next_check = 0;                   // GetTickCount64 of the next look; 0: not scheduled yet.
        uint32_t generation = 0;
        bool manual = false;                        // Sprawdź teraz: also say when there is none.
    } update_;
    std::wstring app_version_;                      // diagnostics::version(); tests set their own.
    std::filesystem::path update_app_dir_;          // Where launchme.exe lives; tests use a folder of theirs.
    std::function<HttpResponse(const std::wstring&)> update_get_;  // Tests: GitHub's answers.
    std::function<bool(const updater::Release&, const std::filesystem::path&, const std::function<void(uint64_t)>&)> update_download_; // Tests: the package.
    std::function<bool(const std::filesystem::path& exe)> update_restart_; // Starts the new launchme.exe; tests record it.
    update_ui::WhatsNew whats_new_;
    void initializeUpdates();       // At start: the previous update's files away, its changes shown once.
    void tickUpdates();             // The periodic look (never while a game runs).
    void checkForUpdate(bool manual);
    void updateFound(uint32_t generation, std::optional<updater::Release> release, bool failed);
    void updateAction();            // The row's or the icon's one click.
    void startUpdate();
    void updateProgress(uint32_t generation, uint64_t done);
    void updateStaged(uint32_t generation, std::wstring error, std::vector<std::filesystem::path> files);
    void installUpdate();
    bool updateAvailable() const;   // A newer version than this one is known.
    float updateFraction() const;   // The download's share, -1 when none runs.
    // System → Aktualizacje's row: its title, help and button.
    std::tuple<std::wstring, std::wstring, std::wstring> updateRow() const;
    void openWhatsNew(std::wstring version, std::wstring text);
    void closeWhatsNew();
    void whatsNewKey(WPARAM key);
    void whatsNewClick(float x, float y);
    // background: the desktop pane's selected game; a newer selection drops it from the queue.
    void requestPluginAchievements(const Game& game, bool force = false, bool background = false);
    std::string plugin_achievements_focus_; // The pane's game last asked about (once per selection).
    void requestFocusedAchievements(const Game& game);
    bool receivePluginAchievements(const plugins::Result& message);
    void showAchievementList(const SteamAchievements& list, bool final); // Fills achievements_ (the open screen).
    library_manager_ui::State library_dialog_;
    void openCollectionManager(std::optional<Game> game = {});
    void rebuildCollectionDialog();
    void collectionDialogAccept();
    void collectionDialogBack();
    void collectionDialogEdit(wchar_t ch);
    void collectionDialogClick(float x, float y);
    bool isFavorite(const Game& game) const;
    void toggleFavorite(const Game& game);
    int favoriteDetailAction() const;
    int collectionsDetailAction() const;
    void collectionChanged();
    // Szczegóły → "…" → "Ukryj" / "Pokaż": local visibility; Steam choices are account-scoped.
    // Copies: the game, or every copy of its version group; empty when any of them cannot be
    // hidden here (a Steam shortcut, a catalog entry, hidden or private in its store).
    std::string hiddenKey(const Game& game) const;
    bool locallyHidden(const Game& game) const;
    bool hiddenInSteam(const Game& game) const;
    void startSteamSync();
    void requestSteamSync(bool force = false);
    std::wstring steamSyncStatus() const;
    std::shared_ptr<SteamSyncQueue> steam_sync_queue_ = std::make_shared<SteamSyncQueue>();
    std::optional<steam_sync::Snapshot> steam_sync_snapshot_;
    std::string steam_sync_error_;
    bool steam_sync_cloud_pending_ = false;
    std::vector<Game> hideableCopies(const Game& game) const;
    bool hiddenInLauncher(const Game& game) const;
    void toggleHidden(const Game& game);
    bool canExclude(const Game& game) const;
    bool excludedGame(const Game& game) const;
    void excludeGame(const Game& game);
    void openExcludedGames(GameSource source);
    uint32_t localPlaytime(const Game& game) const;
    bool playtime_dirty_ = false;
    uint64_t next_playtime_save_ = 0;
    versions::Index versions_;
    std::vector<Game> grouped_library_;
    bool detail_version_selected_ = false;
    int versionsDetailAction() const;
    void openVersions(const Game& game, bool launch);
    void chooseVersion();
    void launchGame(const Game& game);
    void beginClose();
    void tickClose(std::chrono::steady_clock::time_point now);
    void configureGameOverlay();
    bool guideHotkeyWanted() const;
    void updateGuideHotkey();
    void pollGameOverlay();
    bool openGameOverlay(const GameWindow& target);
    void openOverlayDrawer();
    void closeGameOverlay(bool return_to_game);
    void dismissGameOverlay();
    void tickGameOverlay(uint64_t now);
    void overlayDashboard();
    void overlayKey(WPARAM key, bool repeated = false);
    void overlayKeyboard(WPARAM key, bool repeated, bool shift_tab);
    void overlayClick(float x, float y);
    void overlayAccept();
    void confirmOverlayClose();
    bool overlayForeground(HWND window) const {
        return gameOverlayOpen() && (window == overlay_target_->window || (game_overlay_ && window == game_overlay_->handle()));
    }
    LRESULT overlayMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    int dashboardTileCount() const;
    Viewport drawerViewport() const { return gameOverlayOpen() && game_overlay_ ? game_overlay_->renderer().viewport() : renderer_.viewport(); }
    void invalidateGameOverlay() { if (gameOverlayOpen() && game_overlay_) game_overlay_->invalidate(); }
    std::unique_ptr<OverlayWindow> game_overlay_;
    GameOverlayBackend overlay_backend_;
    GameOverlayMonitor overlay_monitor_;
    GameOverlayMonitor::Snapshot overlay_snapshot_;
    std::optional<GameWindow> overlay_target_;
    int overlay_selected_ = 0;
    std::optional<int> overlay_close_confirm_;
    bool overlay_opening_ = false;
    bool overlay_closing_ = false;
    GuideMotion overlay_motion_;
    uint64_t overlay_tick_at_ = 0;
    float overlay_ring_time_ = 0; // Guide over a game: the clock of the quick bar's travelling ring light.
    bool guide_timer_active_ = false, guide_hotkey_active_ = false;
    uint64_t next_overlay_poll_ = 0;
    uint64_t next_guide_hotkey_retry_ = 0;
    // Empties settings_secret_input_ (a saved key or sign-in code) after wiping its characters.
    void clearSecretInput();
    void startRatings();
    std::wstring ratingLabel() const;
    std::wstring ratingUrl() const;
    int ratingDetailAction() const;
    void openRatingSource();
    int identifyDetailAction() const;
    void openIdentify();
    void openIdentifyFor(const Game& game); // Also from Settings (DRM-free game options).
    void closeIdentify();
    void identifySearch();
    void identifyAccept();
    void identifyMove(int amount);
    void identifyClick(float x, float y);
    void identifyEdit(wchar_t character);
    bool libraryControls() const;
    std::string libraryScope() const;
    library_view::Options libraryOptions() const;
    void recordLibraryGames();
    void refreshRecentGames();
    void rememberPlace();
    void restorePlace();
    void saveLibraryPreferences();
    void initializeLibraryPreferences();
    void pollLibrarySave();
    void focusCard(size_t index, bool reveal = true);
    void applyLibraryOptions();
    bool libraryHorizontal(int amount, bool edge_menu = true);
    bool libraryVertical(int amount);
    bool libraryAccept();
    bool libraryClick(float x, float y, bool double_click = false);
    // Current library presentation (other screens use grid geometry).
    bool libraryList() const;
    library_view::View libraryView() const;
    library_view::Options libraryPresentation() const;
    bool libraryGridPosters() const;
    bool libraryGridLandscape() const; // Siatka (or the search results) with 920:430 landscape tiles.
    bool libraryGridBackdrop() const; // TV library grid: the focused game's background behind the covers.
    bool libraryHorizontalCinematic() const;
    bool adjustLibraryView(int amount);
    bool library_view_drag_ = false;
    bool libraryPreview() const;
    bool libraryCinematic() const;
    bool libraryOverview() const;
    bool libraryBookshelf() const; // Półka.
    bool libraryLandscape() const; // Kinowy or Przegląd: 920:430 tiles.
    // The categories of the tab strip: "Wszystkie gry" (empty id), then every shelf with games.
    struct LibraryTab { std::string id; std::wstring title; size_t count = 0; };
    std::vector<LibraryTab> libraryTabs() const;
    int libraryTabIndex(const std::vector<LibraryTab>& tabs) const;
    bool libraryListPlus() const;
    bool libraryCoverflow() const;
    bool libraryCarousel() const;
    bool selectedLogoPending() const;
    float library_focus_age_ = 0;
    bool libraryActions() const;
    // Buttons beside the games that take the focus (library_controls_.action): Szczegółowy's
    // primary action (0) and "…" (1), Przegląd's "Szczegóły" (0) and primary action (1).
    bool libraryButtons() const;
    settings_ui::Rect libraryButton(int index) const;
    void bindLibraryLandscape(size_t index, bool replace = false);
    void bindLibraryPoster(size_t index, bool replace = false);
    int libraryColumns() const;
    void openLibraryPanel(library_ui::Panel panel);
    void closeLibraryPanel();
    // The library's own search field (src/app/library_view.cpp): filters the games in place.
    void openLibrarySearch();
    void closeLibrarySearch();
    void applyLibraryQuery();
    // Hit test of a card as drawn: list rows, Flix posters, square cards.
    bool cardHit(size_t index, float x, float y) const;
    // The game menu opens beside the card (XMB, Flix, list) rather than above it.
    bool menuBeside() const;
    std::optional<settings_ui::Rect> libraryMenuAnchor() const;
    bool library_menu_from_action_ = false;
    // Focused game's description in the list view (worker, debounced).
    void requestPreview();
    bool heroFactsShown() const; // Default home: the focused game's facts under its logo.
    // Szczegóły → Osiągnięcia (src/app/achievements.cpp).
    void initializeAchievements();
    bool achievementsOpen() const { return achievements_.open && screen_ == ScreenMode::details; }
    int achievementsDetailAction() const; // Details action index of the achievements row; -2 without one.
    void openAchievements();
    void closeAchievements();
    void openAchievementCard(int index);  // The achievement's card: larger icon, name, description, date and rarity.
    void closeAchievementCard();          // Back to the list, on the same row.
    void achievementsMove(int amount);
    void achievementsClick(float x, float y);
    void requestAchievementIcons();
    // Quick vertical scrolling in the library: the screen dims under the focused initial.
    void noteFastScroll();
    std::wstring scrollLetter() const;
    // A card by its identity; in Flix the focused row is searched first (a game can
    // sit in "recently played" and in a collection).
    std::optional<size_t> findCard(const std::string& key, bool selection_key) const;
    void openCropEditor(const Game& game);
    void closeCropEditor();
    void cropMove(int horizontal, int vertical);
    void cropTab(int amount);
    void cropReset();
    void cropAccept();
    void cropClick(float x, float y);
    void cropDrag(float x, float y);
    void controller();
    // A direction held while a dialog, menu or screen opens is latched until released, so it
    // does not run on into the new layer; then controllerInput() acts on the reading.
    void controller(const PadReading& state);
    void controllerInput(const PadReading& state);
    int pad_layer_ = -1;
    bool pad_direction_latched_ = false, pad_direction_held_ = false;
    ULONGLONG pad_direction_since_ = 0; // When the held direction began: repeats speed up after 1.5 s.
    // Pad buttons while a modal layer or the on-screen keyboard is open; false: the screen takes them.
    bool modalPad(const PadReading& state, WORD buttons, ULONGLONG now);
    void controllerNavigate(int direction); // ±1 horizontal, ±2 vertical, 0 releases the edge latch.
    void navigationBoundary() { navigation_edge_ = true; focusBoundary(); }
    // Focus motion (src/app/focus_motion.cpp): quick bar circle, Settings highlights and the
    // experimental spring outline of the focused cover (settings focus_spring).
    void animateFocus(float dt, float target_x, float target_y, float target_w, float target_h, bool& needs_paint);
    void focusContext(ViewContext& context) const;
    void focusBoundary(); // A list edge was hit in nav_direction_: the outline bumps and glows.
    bool cardFocusVisible() const;
    // Guide/PS short press: back to the home row with the quick bar open (toggle).
    void openGuideMenu();
    // Start/Options short press and short TAB: friends drawer (toggle).
    void toggleFriendsDrawer();
    // Start/Options hold and TAB hold: audio devices panel.
    void openAudioShortcut();
    void pollHoldGestures();
    void openPowerMenu();
    void closePowerMenu();
    void powerMenuMove(int amount);
    void powerMenuAccept();
    void powerMenuClick(float x, float y);
    power_menu::Geometry powerMenuGeometry() const; // Tryb TV's Zasilanie on the current canvas.
    // Steam scan, installation/account poller and artwork downloaders (src/app/library_worker.cpp).
    void startLibraryWorker();
    void initializeMusic();
    void initializeGameSessions();
    // How long a started game's window is handed the focus: normally 15 s; a silent Steam
    // launch (Steam has no foreground permission) waits up to a minute for slow starts.
    static constexpr uint64_t default_focus_ms = 15000, silent_focus_ms = 60000;
    void beginGameSession(const Game& game, uint64_t focus_ms = default_focus_ms, std::shared_ptr<void> job = {});
    // The last game started here ended: the launcher returns to the front when a store client,
    // the desktop or nothing holds it (never over another program the user chose).
    void returnFromGame();
    void bringToFront(); // Restored and in front now (a shell's own window may take the foreground back).
    std::set<std::string> launched_here_; // Sessions started by the launcher itself (not adopted).
    void requestGameFocus(const Game& game, uint64_t focus_ms = default_focus_ms);
    void cancelGameFocus();
    void pollGameFocus();
    void applyGameFocus(const GameActivityMessage& message);
    std::string launch_focus_key_;
    uint64_t launch_focus_id_ = 0, launch_focus_until_ = 0;
    // The library's installed games handed to the running-game scan (after buildShelves).
    void updateRunningCandidates();
    void pokeRunningScan();
    void requestMusic();
    void requestSound(SoundCue cue, int index, bool preview);
    void openDevices(devices::Panel panel);
    void deviceMove(int amount);
    void deviceAdjust(int amount);
    void deviceColumn(int direction);
    void deviceSetVolume(bool input, float value);
    void endAudioEditing();
    void deviceDrag(float x, float y);
    void deviceAccept();
    void deviceClick(float x, float y);
    void pollDevices();
    void initializeControllerMonitor();
    void initializeHeadphoneMonitor();
    void requestHeadphonePicture();
    void requestControllerPicture(devices::Picture picture);
    void focusCouchCategory();
    void showCategory(size_t index);
    void rebuildCards(bool preserve_background = true);
    void requestVisibleArt();
    // True when a new download job was queued. `ready_at` delays a background job.
    bool requestArt(const Game& game, CoverMessage::Kind kind, bool background = false,
                    std::chrono::steady_clock::time_point ready_at = {});
    std::vector<Game> backdropGames(const Game& tile);
    void prefetchLibraryArt();
    void prefetchPosters();
    void invalidateArtwork(const Game& game, CoverMessage::Kind kind);
    void retryGameArtwork(const Game& game, bool notify = true);
    void showNotification(NotificationKind kind, std::wstring title,
                          std::wstring subtitle, float level = 0);
    void showVolume(float level, bool muted, bool input = false);
    void pollVolume();
    void finishLaunch();
    void startLaunchAnimation(const Game& game);
    void stopLaunchAnimation();
    void tickLaunchAnimation(std::chrono::steady_clock::time_point now);
    void endLaunchAnimation(); // Its time is up or Back hid it: the home with the started game in front.
public:
    // Back while it plays (main.cpp: Esc/Backspace, the pad: B); false while too young to dismiss.
    bool dismissLaunchAnimation(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
private:
    void loadFriends();
    void startDashboardPoll();
    void stopDashboardPoll();
    void dashboardMove(int horizontal, int vertical);
    void dashboardActivate(int tile);
    friends_layout::dashboard::Layout dashboardLayout() const;
    void dashboardEnsureVisible();
    void dashboardEdit(bool editing);
    void dashboardReorder(int target);
    bool dashboardEditing() const;
    bool dashboardPress(int source, bool down, uint64_t now);
    void dashboardPollHold(uint64_t now);
    void dashboardCancelPress();
    void dashboardPointerDown(float x, float y);
    void dashboardPointerMove(float x, float y);
    void openDiscord();
    void openLibrary();
    void openPcLibrary(GameSource source);
    void refreshPcLibraries();
    void pollPcInstallations();
    void startPcWatcher();
    void initializeGog();
    // EA / Battle.net libraries copied from the store's website (src/app/web_imports.cpp).
    struct WebImportState {
        std::optional<web_import::Snapshot> snapshot; // Last saved import, as read by the PC scan.
        bool awaiting = false;                        // Browser opened, paste field shown.
        bool busy = false;                            // Importing or clearing on a worker.
        uint32_t generation = 0;
    };
    WebImportState& webImport(GameSource source);
    const WebImportState& webImport(GameSource source) const;
    void startWebImport(GameSource source);
    void cancelWebImport(GameSource source);
    void submitWebImport(GameSource source);
    void clearWebImport(GameSource source);
    void initializeEpic();
    void epicCommand(epic::Command command);
    void pollEpic();
    void openEpicInstall(const Game& game);
    void epicInstallAccept();
    void startEpicGame(const Game& game);
    void updateSteamInstallations(const LibraryMessage& library);
    void pollGog();
    void openGogInstall(const Game& game);
    // Steam in Launchme's hands (src/app/steam_install.cpp): the install dialog picks the library
    // (gog_install_ui_ with `steam`), Play on another remembered account's game switches Steam to it.
    bool steamInstallDialogWanted(const Game& game) const;
    bool openSteamInstall(const Game& game); // false: no library known, Steam's own window instead.
    void steamInstallAccept();
    void steamInstallFinished(uint32_t generation, bool ok);
    std::optional<steam_accounts::Account> steamSwitchTarget(const Game& game, bool& unremembered) const;
    bool beginSteamAccountSwitch(const Game& game); // false: the launch must not go on.
    void steamSwitchFinished(uint32_t generation, bool ok, const std::wstring& persona);
    bool steamGameRunning() const;
    void gogInstallAccept();
    void pollGogInstall();
    void applyGogInstallProgress();
    void gogCommand(gog::Command command);
    // User-added games ("Moje gry", src/app/my_games.cpp).
    void loadCustomGames();
    void refreshCustomGames();
    void rebuildCustomLibrary();
    void applyCustomGames();
    void forgetCustomArtwork(const Game& game, bool selections);
    void openCustomEditor(const std::string& id);
    // "Opcje uruchamiania…" of a library game (my_games.cpp): user-added, DRM-free and Steam games.
    bool launchEditable(const Game& game) const;
    void openLaunchEditor(const Game& game);
    void launchEditorSave();
    std::wstring launchArguments(const Game& game) const; // A Steam game's own launch options; empty: none.
    void closeCustomEditor();
    void customEditorFocus(custom_game_ui::Item item);
    void customEditorMove(int horizontal, int vertical);
    void customEditorAccept();
    void customEditorInsert(std::wstring_view text);
    void customEditorBrowse(bool folder);
    // File picker (src/app/file_browser.cpp). `done` runs once with the chosen path; cancelling
    // never calls it.
    void openFileBrowser(file_browser::Request request, std::function<void(const std::filesystem::path&)> done);
    void closeFileBrowser();
    void fileBrowserPlaces();
    void fileBrowserOpenFolder(const std::filesystem::path& folder, const std::filesystem::path& select = {});
    void fileBrowserSelect(int index);
    void fileBrowserMove(int amount);
    void fileBrowserHorizontal(int amount);
    void fileBrowserAccept();
    void fileBrowserUp();
    void fileBrowserButton(file_browser::Button button);
    void fileBrowserPick(const std::filesystem::path& path);
    void fileBrowserCharacter(wchar_t character);
    void fileBrowserClick(float x, float y, bool double_click);
    void fileBrowserHover(float x, float y);
    void fileBrowserPreview();
    void pollFileBrowserPreview();
    // Settings → "Inny…" colour (settings.cpp): live preview, saved by A/Save, dropped by B.
    void openColorEditor(const settings_ui::Row& row);
    void closeColorEditor(bool save);
    void colorEditorPreview();
    void colorEditorMove(int amount);
    void colorEditorAdjust(int amount);
    void colorEditorClick(float x, float y);
    void colorEditorDrag(float x);
    void customEditorSave();
    void customEditorRemove();
    void customEditorClick(float x, float y);
    // DRM-free folders (src/app/drm_free.cpp): catalog, scans (manual, after startup, while
    // idle) and the Settings → Platformy PC → DRM-free page.
    void loadDrmFree();
    void rebuildDrmLibrary();
    void applyDrmFree(const std::vector<std::string>& artwork_reset = {});
    bool saveDrmFree();
    // manual: every folder again, normal priority. Otherwise only changed folders, gently;
    // idle scans end at the first input.
    void startDrmScan(bool manual, bool idle = false);
    void pollDrmFree();
    void drmSettingsRows();
    bool drmSettingsActivate(const settings_ui::Row& row);
    void drmAddRoot();
    void drmAddRoot(const std::filesystem::path& picked);
    void drmIdentified(const Game& game, const rawg::Metadata& metadata);
    std::wstring drmSummary() const;
    void openPlatform(const std::wstring& name);
    void openSearch();
    void openSettings();
    // Ustawienia → Asystent (desktop: the companion card's "…" menu).
    void openCompanionSettings();
    void initializeStore();
    void initializePlugins();
    void submitPlugin(plugins::Job job);
    void pluginSettingsRows();
    void pcSettingsRows();
    bool pcSettingsActivate(const settings_ui::Row& row);
    void openPlatformSettings(GameSource source);
    void cancelPlatformSignIn(GameSource source);
    bool pluginSettingsActivate(const settings_ui::Row& row);
    // query: a plugin page's search text (params.query). background: the page's periodic refresh,
    // which neither blocks nor outlives a command picked meanwhile.
    void pluginAction(const plugins::Action& action, const Game& game = {}, bool refresh = false,
                      const std::wstring* query = nullptr, bool background = false);
    std::vector<plugins::Action> pluginContext(const Game& game) const;
    void refreshPluginContributions();
    void refreshPluginLibraries();
    void refreshPluginWidgets(bool active_only = false);
    void openPluginPage(const std::string& owner, const json::Value& page, bool drawer = false, bool refreshed = false);
    void pluginPageMove(int direction);
    void pluginPageAccept();
    void pluginPageCommand(const std::string& command, const std::wstring* query = nullptr);
    bool pluginPageBack();
    void pluginPageType(wchar_t character);
    void pluginPageErase();
    void pollPluginPageRefresh();
    void pollPluginDownloads();
    void downloadQueued(const std::wstring& title);
    void downloadFinished(const std::wstring& title, const Game* game = nullptr);
    void receivePluginDownloads(const std::string& owner, const json::Value& response);
    bool refreshPluginPage(bool background);
    void pluginPageClick(float x, float y);
    void requestPluginPageArt();
    json::Value pluginLibrary() const;
    bool storeEnabled() const;
    void initializeArtworkBrowser();
    void openArtworkBrowser(const Game& game);
    void closeArtworkBrowser();
    // A game that is gone (account change, removed folder or entry, excluded from Steam): the
    // artwork browser, then the crop editor showing it close.
    void closeEditorsFor(const std::function<bool(const Game&)>& gone);
    void loadArtworkPage();
    void requestArtworkThumbnails();
    void artworkClick(float x, float y, bool double_click);
    // "Z dysku…": the type panel, then an image file applied like a SteamGridDB pick.
    void openLocalArtwork();
    void openArtworkCrop(); // "Popraw kadr" on the artwork browser's rail.
    void cycleArtworkProvider(); // "Źródło": SteamGridDB → ScreenScraper → ES-DE (emulated shortcuts).
    void openArtworkProviders();          // Tryb TV: "Źródło"'s list, right of the search field.
    void chooseArtworkProvider(int index); // Its row picked (artwork_ui::providers order); closes the list.
    void closeLocalArtwork();
    void pickLocalArtwork();
    void applyLocalArtwork(const std::filesystem::path& picked);
    void leaveArtworkSearch(); // Field → types; an unsearched edit gets the gallery back.
    void searchArtwork();
    void editArtworkQuery();
    void openStore();
    void searchStore();
    void openDetails(const Game& game);
    void requestDetails();
    void suspendDetails();
    void catalogAction(const Game& game);
    context_menu::Rows menuRows() const;
    // Experimental quick resume (src/app/quick_resume.cpp).
    SessionState sessionState(const Game& game) const;
    bool anyActiveSession() const;
    // A tile stands for the game `key` (itself, or the version group it was launched from).
    bool sameTile(const Game& tile, const std::string& key) const;
    // The running, starting or suspended session of a tile's game, if any.
    const GameSession* tileSession(const Game& tile) const;
    // A game was started or switched to: Home, its tile at the front of Recent takes the focus.
    void focusPlayedGame(const std::string& key);
    void trackSession(const std::string& key, const Game& game, std::vector<GameProcess> processes, std::shared_ptr<void> job = {});
    Game trackedGame(const std::string& key) const;
    bool freezeGame(const std::string& key, bool notify);
    void freezeOthers(const std::string& except);
    void resumeGame(const Game& game);
    void closeGame(const Game& game);
    bool switchToGame(const Game& game);
    // An optional pending suspension is journaled before touching its processes.
    bool persistFrozenGames(const FrozenGame* pending = nullptr);
    void restoreFrozenGames();
    void exitPromptMove(int amount);
    void exitPromptAccept();
    void exitPromptClick(float x, float y);
    std::filesystem::path quickResumeFile() const;
    Game installedCatalogGame(const Game& game) const;
    std::wstring actionLabel(const Game& game) const;
    bool actionEnabled(const Game& game) const;
    // "Otwórz folder" in details: install directory of an installed game (no disk access).
    std::filesystem::path gameFolder(const Game& game) const;
    int lastDetailAction() const;
    details_layout::Actions detailsActions() const; // What the details screen shows (ui/game_details_layout.hpp).
    void updateSearch();
    void buildShelves();
    void buildSettingsRows();
    std::string settingsPage() const; // Category and open sub-page; equal pages keep the focused row.
    // Settings → Dźwięk / Kontrolery read the device service while shown (settings_devices.cpp).
    std::optional<devices::Panel> settingsDevicePanel() const;
    void syncSettingsDevices();
    void pollSettingsDevices();
    bool settingsTileActivate(const settings_ui::Row& row);
    void settingsPickSource(const settings_ui::Row& row, int choice);
    void settingsChoose(const settings_ui::Row& row); // A choice row's picked option (settingsActivate).
    void settingsToggle(const settings_ui::Row& row); // A switch row; saves and repaints itself.
    // A text row (settings_text.cpp): the first Enter edits `current`, the next trims spaces and
    // quotes (unless !unquote) and hands it to `commit`; true closes the field and saves Settings,
    // false keeps it open (commit showed the error).
    void settingsTextRow(std::wstring current, const std::function<bool(const std::wstring&)>& commit, bool unquote = true);
    // Opens a page of the current category from the focused row (settings_ui::State::open); Back
    // returns to that row.
    void openSettingsPage(settings_ui::Page page, std::string key = {}, int value = 0);
    // Back from the innermost page to the row it was opened from.
    void closeSettingsPage();
    std::wstring settingsPageTitle() const; // The open page's heading; empty on a category's list.
    // Focuses the row `id` (and `key`) of the current category, opening the page it is on; false: none.
    bool revealSetting(settings_ui::Id id, const std::string& key = {});
    void openSettingsCollections();
    // Bottom-right button hints (input_hints.cpp): for the screen and the last used input.
    void inputHints(ViewContext& context) const;
    HintDevice hintDevice() const; // Keys, or the last used pad's family (or the chosen symbols).
    void settingsClick(float x, float y);
    void settingsChanged(bool shelves = false);
    void settingsSetLevel(float level);
    void ensureSettingVisible();
    void settingsPreviewFocus();
    void setHomeLayout(int option);
    // Applies settings_.language to tr(); true when the UI language changed (callers rebuild shelves).
    bool applyLanguage();
    // Default layout with "Pionowe plakaty": the shown shelf holds games (stores and consoles keep
    // their square logo tiles), drawn as 2:3 posters.
    bool shelfPosters() const; // Posters bind through bindLibraryPoster.
    // Default layout with "Kafelek „Wszystkie” w rzędach": the shown game shelf starts with a
    // tile that opens its category's library (as on Flix).
    bool shelfAllTile() const;
    // Its picture: a 2x2 preview of the category's covers, or (posters) a random game's background.
    void bindShelfAllTile(size_t index, bool replace = false);
    // XMB home (src/app/xmb.cpp). cards_ = top-row tiles followed by the focused column's games.
    bool xmbActive() const;
    const std::vector<Game>* xmbColumn(const Game& tile) const;
    std::string xmbKey(const Game& tile) const;
    void rebuildXmb(bool keep_top, bool preserve_background = true);
    void xmbFocus(size_t column, int row);
    void xmbFocusCard(size_t index);
    void xmbHorizontal(int amount);
    void xmbVertical(int amount, bool edge_menu = true);
    void xmbClick(size_t index, bool double_click);
    void xmbTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const;
    void bindXmbArt(size_t index, bool replace = false);
    void xmbRequestArt();
    void xmbBindThumbnails(size_t tile);
    // Flix home (src/app/flix.cpp). cards_ = every row's games, row after row (flix_rows_);
    // category_ follows the focused row, each category's `selected` is its row position.
    bool flixActive() const;
    void rebuildFlix(bool preserve_background = true);
    void flixFocus(size_t row, size_t column);
    void flixFocusCard(size_t index);
    void flixHorizontal(int amount);
    void flixVertical(int amount, bool edge_menu = true);
    void flixClick(size_t index, bool double_click);
    void flixTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const;
    // The focused game of a poster row, rested on long enough (flix_layout::unfold_rest) with no
    // menu, drawer or dialog over the page: its tile unfolds into the wide card.
    bool flixUnfoldReady(std::chrono::steady_clock::time_point now) const;
    bool tickFlixUnfold(std::chrono::steady_clock::time_point now, float dt); // true while it moves.
    void flixRequestArt();
    // Binds the card's poster (landscape in the first row) from art_ or requests it;
    // `replace` rebinds when new bytes arrived for an already bound card.
    void bindFlixArt(size_t index, bool replace = false);
    // The game whose background a category's "Wszystkie" tile shows (null: none usable).
    const Game* flixBackdrop(size_t category);
    void flixFocusArt(); // Logo and background of the focused tile (a "Wszystkie" tile: its backdrop).
    // Metro home (src/app/metro.cpp). cards_ = every page's tiles, page after page (metro_pages_);
    // category_ follows the focused page, each category's `selected` is the tile on its page.
    bool metroActive() const;
    void rebuildMetro(bool preserve_background = true);
    void metroFocus(size_t index);
    // Arrows: the nearest tile that way; past a page edge the next/previous page.
    void metroMove(int dx, int dy, bool edge_menu = true);
    void metroClick(size_t index, bool double_click);
    bool metroActivate(); // Store/console shortcuts and "Wszystkie" tiles; false: the usual handling.
    void metroTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const;
    void bindMetroArt(size_t index, bool replace = false);
    void metroRequestArt();
    void metroArtArrived(const std::string& key, CoverMessage::Kind kind);
    const Game* metroInstallGame() const; // An active installation, else the newest installed game.
    void metroFocusTabs(bool focus);
    bool metroTabsFocused() const { return metroActive() && metro_tabs_; }
    // PSfive home (src/app/psfive.cpp): the default shelf (Recent: psfive_layout::row_games games,
    // then "Wszystkie gry") as one row of tiles at the top, the focused tile pinned to the fixed
    // left margin shared with the tabs. Enter/A or a click on a game moves the focus down to its buttons
    // ("Graj", "…" = the game menu); psfive_action_ is the focused button, -1 the tiles.
    bool psfiveActive() const;
    bool snitchActive() const;
    void snitchTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const;
    bool snitchClick(float x, float y, bool double_click);
    float psfiveScale() const; // psfive_layout::sizeScale of the tile size setting.
    // A game category other than Recent shown in the row: its first row_games games, then a
    // "Wszystkie" tile opening the category's library.
    bool psfiveAllTile() const;
    bool psfiveButtons() const; // The focused tile is a game with buttons.
    bool psfiveActions() const { return psfive_action_ >= 0 && psfiveActive() && psfiveButtons(); }
    void psfiveFocusAction(int action);
    // The top bar (Up from the row): three category tabs, optional "Więcej...", Widgets (drawer)
    // and Menu (the quick bar). psfive_top_ counts them in that
    // order; -1: not focused.
    bool psfiveTopFocused() const { return psfive_top_ >= 0 && psfiveActive(); }
    bool psfiveTopAvailable() const;
    bool psfiveTopActive() const;
    float psfiveMenuScroll() const; // Animated offset, shared by drawing and pointer input.
    std::vector<size_t> psfiveTabs() const; // First three category indices, in shelf order.
    int psfiveTabCount() const; // Includes "Więcej..." when there are more categories.
    int psfiveActiveTab() const;           // The tab of the shown category; -1: none.
    void psfiveSelectTab(int tab, bool keep_focus);
    void psfiveFocusTop(int item);
    void psfiveTopActivate();
    void psfiveTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const;
    bool psfiveAccept(); // Enter/A: a game tile opens its details (or goes down to its buttons), a button acts.
    bool psfiveOpensDetails() const; // The focused game opens its details on Enter/A/double click (the default game action).
    void psfiveActivate();
    void psfiveHorizontal(int amount);
    void psfiveVertical(int amount);
    void psfiveClickCard(size_t index, bool double_click);
    void psfiveHover(float x, float y);
    bool psfiveClick(float x, float y, bool double_click); // The buttons; false: not on one.
    bool artBlocked(const Game& game, CoverMessage::Kind kind) const;
    // Library screen with only one shelf category's games (its own title and view options).
    void openCategoryLibrary(const std::string& id);
    const std::vector<Game>& librarySource() const;
    void openCategoryMenu();
    void ensureCategoryCursorVisible();
    bool categoryRailActive() const;
    void focusCategoryRail(bool focus);
    // First-run walkthrough (src/app/tour.cpp). Starts over the home screen (leaving Settings).
    void startTour();
    void tourEnter(int step, bool backwards = false);
    // Opens the panel the step describes if it is not open yet; true when it opened now.
    bool tourOpenPanel();
    void tourClosePanels();
    void tourNext();
    void tourPrevious();
    void tourPress(int button);
    void tourFinish();
    void tourLayout(int option, bool keyed);
    void tourFocusButton(int amount);
    void tourClick(float x, float y);
    void tourHover(float x, float y);
    void tickTour(float dt, bool& needs_paint);
    void tourView(TourView& view) const;
    bool tourCardAtBottom() const;
    int tourIcons() const;

    struct Category {
        std::wstring title;
        std::vector<Game> games;
        size_t selected = 0;
        std::string id;
    };
    // Encoded artwork in RAM, shared with the renderer (never copied per bind). Null = none.
    struct CachedArt {
        ImageBytes cover, logo, hero;
        ImageBytes poster, landscape, disc;
        CropFocus focus;
        HeroTone hero_tone = HeroTone::full;
    };
    struct CachedArtBytes {
        size_t operator()(const CachedArt& art) const {
            return imageSize(art.cover) + imageSize(art.logo) + imageSize(art.hero) + imageSize(art.poster) + imageSize(art.landscape) + imageSize(art.disc);
        }
    };
    // RAM budget of art_: past it, least recently used artwork of cards that are not bound
    // on screen is dropped and later read again from the disk cache (a worker, no network).
    static constexpr size_t art_budget = size_t{128} << 20;
    // Called after art_ grows: trims it to the budget and lets dropped keys be requested again.
    void trimArt();
    void startArtworkWorkers(bool network = true); // Tests can run the actual disk lane alone.
    // One game of the slideshow bound to a renderer image slot (0-2; the three items
    // keep distinct slots and swap them as the show advances).
    struct ScreensaverItem {
        std::string key;
        std::wstring title;
        int slot = -1;
        uint32_t sequence = 0; // Image request; answers for another sequence are stale.
        uint32_t seed = 0;
        bool poster = false, hero = false;
        bool loaded = false;   // Bytes handed to the renderer.
        float shown_at = -1;   // Screensaver time it became current; -1 = waiting for its image.
        void clear() { const int keep = slot; *this = {}; slot = keep; }
    };
    struct ScreensaverState {
        bool active = false;
        screensaver::Style style = screensaver::Style::backgrounds;
        screensaver::Playlist playlist;
        std::unordered_map<std::string, std::pair<Game, screensaver::Artwork>> games;
        bool scanned = false;
        float time = 0;
        uint32_t next_sequence = 0;
        ScreensaverItem previous{.slot = 0}, current{.slot = 1}, upcoming{.slot = 2};
    };
    void startScreensaver();
    // `woken`: ended by input, whose remaining messages the gate keeps swallowing.
    void stopScreensaver(bool woken);
    void updateScreensaver(std::chrono::steady_clock::time_point now, float dt);
    void screensaverAssign(ScreensaverItem& item);
    bool screensaverVerdict(screensaver::Verdict verdict);
    screensaver::View screensaverView() const;
    bool windowWatched() const { return foreground_(hwnd_); }
    bool textEntry() const;

    HWND hwnd_ = nullptr;
    ActionExecutor execute_ = executeGameAction;
    // steam.exe for Ciche uruchamianie Steam (registry + one file check, at launch time); tests inject a path.
    std::filesystem::path (*steam_executable_)() = installedSteamExecutable;
    // Steam accounts and installs (src/library/steam_accounts.hpp); tests replace each backend.
    std::function<std::vector<steam_accounts::Account>()> steam_accounts_ = [] { return steam_accounts::loginUsers(findSteamRoot()); };
    std::function<std::vector<steam_accounts::Folder>()> steam_libraries_ = [] { return steam_accounts::libraryFolders(findSteamRoot()); };
    // Closes Steam and makes it sign in to `account` when it next starts (a worker thread).
    std::function<bool(const steam_accounts::Account&)> steam_switcher_ = [](const steam_accounts::Account& account) {
        const auto root = findSteamRoot();
        return steam_accounts::shutdownSteam(steamExecutable(root), std::chrono::seconds(45)) && steam_accounts::prepareSignIn(root, account);
    };
    // Puts the game's install stub into `library` and (re)starts Steam, which downloads it (a worker thread).
    std::function<bool(const Game&, const std::filesystem::path&, uint64_t)> steam_installer_ =
        [](const Game& game, const std::filesystem::path& library, uint64_t owner) {
            const auto exe = steamExecutable(findSteamRoot());
            if (exe.empty()) return false;
            const bool running = steam_accounts::runningSteamProcess() != 0;
            if (running && !steam_accounts::shutdownSteam(exe, std::chrono::seconds(45))) return false;
            const bool written = steam_accounts::writeInstallManifest(library, game.app_id, game.name, exe, owner);
            return steam_accounts::startSteam(exe) && written;
        };
    bool steam_switch_pending_ = false;
    uint32_t steam_switch_generation_ = 0, steam_install_generation_ = 0;
    // EA library import from a pasted sign-in page; tests replace the network backend.
    std::function<ea::Import(const std::string&)> ea_importer_ = [](const std::string& pasted) {
        return ea::importLibrary(pasted, [](const std::wstring& url, const std::wstring& headers, const std::string& body) {
            return httpPostPrivate(url, headers, body, 8 * 1024 * 1024);
        });
    };
    WebImportState ea_import_, battlenet_import_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::shared_ptr<std::atomic_uint32_t> friends_token_ = std::make_shared<std::atomic_uint32_t>(0);
    std::shared_ptr<std::atomic_uint32_t> metadata_token_ = std::make_shared<std::atomic_uint32_t>(0);
    std::shared_ptr<std::atomic_uint32_t> pc_token_ = std::make_shared<std::atomic_uint32_t>(0);
    std::shared_ptr<JobQueue> jobs_ = std::make_shared<JobQueue>();
    std::shared_ptr<StoreQueue> store_jobs_ = std::make_shared<StoreQueue>();
    std::shared_ptr<plugins::Queue> plugin_jobs_ = std::make_shared<plugins::Queue>();
    plugins::Snapshot plugins_;
    std::vector<plugins::Action> plugin_menu_, plugin_widgets_;
    std::vector<std::wstring> plugin_widget_text_;
    std::string catalog_plugin_;
    plugin_ui::Page plugin_page_;
    bool plugin_drawer_active_ = false;
    std::string plugin_page_action_;
    std::vector<Game> plugin_page_games_;
    uint32_t plugin_epoch_ = 0;
    std::map<std::string, std::vector<plugins::Download>> plugin_downloads_;
    std::map<std::string, uint32_t> plugin_download_pending_;
    uint64_t plugin_download_poll_at_ = 0;
    std::map<std::string, download_art::Target> download_art_targets_;
    std::map<std::string, std::pair<download_art::Target, Game>> download_art_queue_;
    std::map<std::string, uint64_t> download_art_retry_at_;
    std::string download_art_active_;
    uint64_t download_art_poll_at_ = 0;
    std::function<std::vector<uint8_t>(const download_art::Target&)> download_art_fetch_; // Offline test backend.
    uint32_t plugin_command_generation_ = 0;
    uint32_t plugin_refresh_generation_ = 0; // The page refresh in flight (plugin_refresh_pending_).
    uint64_t plugin_page_refresh_at_ = 0;    // Next periodic refresh of the open page (Page::refresh).
    uint32_t plugin_library_generation_ = 0;
    std::map<std::string, std::vector<uint32_t>> plugin_library_ids_;
    bool plugins_busy_ = false, plugin_command_pending_ = false, plugin_refresh_pending_ = false;
    std::function<std::filesystem::path(HWND)> pick_plugin_; // Tests only; empty = the launcher's file picker.
    std::function<bool(const std::filesystem::path&)> reveal_download_; // Tests; default uses Explorer's PIDL API.
    std::shared_ptr<ArtworkBrowserQueue> artwork_jobs_ = std::make_shared<ArtworkBrowserQueue>();
    std::shared_ptr<MusicQueue> music_jobs_ = std::make_shared<MusicQueue>();
    std::shared_ptr<GameActivityQueue> game_activity_jobs_ = std::make_shared<GameActivityQueue>();
    std::shared_ptr<RunningScanQueue> running_scan_ = std::make_shared<RunningScanQueue>();
    std::shared_ptr<WakeSignal> steam_apps_wake_ = std::make_shared<WakeSignal>(); // Shutdown of the Steam Apps registry watch.
    ULONGLONG foreground_scan_at_ = 0; // foregroundChanged(): the next scan it may schedule.
    std::vector<std::string> running_candidate_keys_; // Keys of running_scan_->candidates.
    uint32_t running_version_ = 0;                     // running_scan_->version, read on the UI thread.
    std::shared_ptr<DashboardQueue> dashboard_jobs_ = std::make_shared<DashboardQueue>();
    std::shared_ptr<ScreensaverQueue> saver_jobs_ = std::make_shared<ScreensaverQueue>();
    std::shared_ptr<AchievementsQueue> achievement_jobs_ = std::make_shared<AchievementsQueue>();
    achievements_ui::State achievements_;
    std::vector<char> achievement_icons_requested_; // Per row.
    LibraryPreview preview_;
    ScreensaverState saver_;
    screensaver::InputGate saver_gate_; // Outlives a run: the waking input's tail is swallowed after it.
    uint32_t saver_seed_ = 0;           // Shuffle seed; 0 = from the clock. Tests pin it.
    bool saver_watched_ = true;         // Last frame's windowWatched(): refocus restarts the idle period.
    std::optional<std::pair<float, float>> saver_mouse_; // Last cursor position: real moves count as input.
    // Injectable so UI tests decide whether the window counts as watched.
    std::function<bool(HWND)> foreground_ = [](HWND window) { return GetForegroundWindow() == window && !IsIconic(window); };
    // Every worker thread is owned here; ~Launcher signals them, then joins within a budget.
    BackgroundWork work_;
    std::map<std::string, GameSession> game_sessions_;
    QuickResumeBackend quick_resume_;
    bool frozen_state_readable_ = true; // Never overwrite a journal that failed recovery.
    std::optional<int> exit_prompt_; // Selected exit_prompt::Choice while asking.
    // The same three-button prompt also asks whether downloaded videos follow a new video folder.
    enum class PromptKind { exit, move_videos, uninstall };
    Game uninstall_game_; // PromptKind::uninstall: the game Launchme itself would remove (Legendary).
    // Szczegóły → … → Odinstaluj: Steam's own dialog, the uninstaller the game's installer registered
    // in Windows, Legendary for games Launchme installed, else the store client. Never a guess.
    bool canUninstall(const Game& game) const;
    void uninstallGame(const Game& game);
    void uninstallManagedGame(const Game& game); // After the question: Legendary on a worker.
    PromptKind prompt_kind_ = PromptKind::exit;
    std::filesystem::path video_move_from_, video_move_to_;
    uint64_t video_move_bytes_ = 0;
    // Biblioteka → Folder filmów: a new folder (empty = default); asks to move the videos already there.
    void changeVideoFolder(const std::filesystem::path& folder);
    void moveVideoFolder(const std::filesystem::path& from, const std::filesystem::path& to);
    std::filesystem::path videoFolder() const;
    bool closing_ = false, close_ready_ = false, exit_black_painted_ = false;
    std::chrono::steady_clock::time_point close_started_{};
    float exit_progress_ = 0; // One second, independent of the capped animation dt.
    std::wstring music_status_;
    uint32_t sound_generation_[4]{};
    bool boot_played_ = false;
    artwork_ui::State artwork_state_;
    ratings::Map ratings_;
    rawg::Map rawg_metadata_;
    // Emulated shortcuts identified in ES-DE's gamelist.xml by ROM file (ScreenScraper data ES-DE
    // scraped). Details only; a RAWG identification the user confirmed always wins.
    std::unordered_map<std::string, GameDetails> emulation_metadata_;
    const GameDetails* localDetails(const Game& game) const;
    // Game videos (src/app/game_video.cpp): 3 s on an emulated game, then its ES-DE/ScreenScraper
    // video replaces the background and the music is ducked until focus moves on.
    std::unordered_map<std::string, std::wstring> emulation_names_;          // Titles matched by ROM file.
    std::unordered_map<std::string, std::wstring> shortcut_original_names_;  // Shortcut names from shortcuts.vdf.
    bool renameShortcuts(); // emulation_names_ or the original names on shortcut_library_; true = changed.
    // ES-DE gamelist.xml changes (ES-DE scraped meanwhile): details and missing artwork reload.
    std::shared_ptr<EsdeWatch> esde_watch_ = std::make_shared<EsdeWatch>();
    std::chrono::steady_clock::time_point esde_watch_next_{};
    void watchEsde();
    std::wstring screenscraper_status_; // Last "Sprawdź połączenie" answer, shown under that row.
    bool screenscraper_testing_ = false;
    VideoPlayer video_player_;
    bool video_silent_ = false; // The playing video is a muted Steam trailer.
    std::string video_key_;
    std::chrono::steady_clock::time_point video_since_{};
    bool video_requested_ = false;
    uint32_t video_generation_ = 0;
    std::vector<uint8_t> video_frame_;
    const Game* videoCandidate() const;
    void tickVideo(bool& needs_paint);
    void stopVideo();
    bool ratings_started_ = false;
    uint32_t ratings_generation_ = 0;
    std::shared_ptr<std::atomic_bool> ratings_alive_ = std::make_shared<std::atomic_bool>(true);
    identify_ui::State identify_state_;
    std::shared_ptr<std::atomic_uint32_t> identify_token_ = std::make_shared<std::atomic_uint32_t>(0);
    crop_ui::State crop_state_;
    std::shared_ptr<std::atomic_uint32_t> crop_token_ = std::make_shared<std::atomic_uint32_t>(0);
    ScreenMode crop_return_ = ScreenMode::shelves;
    size_t crop_return_selected_ = 0;
    float crop_return_scroll_ = 0;
    std::vector<Game> crop_games_;
    ScreenMode artwork_return_ = ScreenMode::shelves;
    size_t artwork_return_selected_ = 0;
    float artwork_return_scroll_ = 0;
    std::unordered_map<std::string, uint32_t> art_revisions_;
    std::vector<Game> store_results_, detail_games_;
    GameDetails details_;
    bool store_loading_ = false, details_loading_ = false;
    bool detail_catalog_ = false, details_requested_ = false;
    bool detail_catalog_available_ = false;
    int detail_action_ = 0;
    int detail_column_ = 0; // Details: the left-column button to return to from the info column.
    std::wstring store_status_;
    ScreenMode detail_return_ = ScreenMode::store;
    size_t detail_return_selected_ = 0;
    float detail_return_scroll_ = 0;
    AddRequests add_requests_;
    ULONGLONG next_install_poll_ = 0;
    std::vector<Category> categories_;
    std::vector<Game> public_library_;
    std::vector<Game> installed_library_, uninstalled_library_;
    bulk_covers_ui::State bulk_ui_;
    std::shared_ptr<bulk_covers::Control> bulk_control_;
    uint64_t bulk_generation_ = 0;
    std::vector<Game> bulk_games_;
    std::optional<bulk_covers::Operations> bulk_ops_; // Injected offline UI tests.
    std::unordered_set<uint32_t> completed_installs_;
    std::vector<Game> store_library_;
    std::vector<Game> store_private_library_;
    std::vector<StoreClient> pc_clients_;
    std::unique_ptr<gog::Service> gog_service_;
    std::unique_ptr<epic::Service> epic_service_;
    epic::Snapshot epic_state_;
    bool epic_initial_ready_ = false, epic_enable_on_connect_ = false, epic_launch_pending_ = false;
    gog::Snapshot gog_state_;
    gog_install_ui::State gog_install_ui_;
    std::unordered_set<std::string> completed_pc_installs_;
    bool gog_initial_ready_ = false;
    bool gog_enable_on_connect_ = false;
    std::optional<GameSource> pc_source_;
    std::vector<Game> pc_library_;
    // Library of one console (openPlatform): its name, the games are platform_games_[name]; empty = none.
    std::wstring library_console_;
    // Library of one shelf category (Flix "Wszystkie" tile): its id, games and title; empty = none.
    std::string library_category_;
    std::vector<Game> category_library_;
    std::wstring category_library_title_;
    bool pc_scanning_ = false;
    bool pc_refresh_pending_ = false;
    bool pc_sources_detected_ = false; // First start: stores with installed games were switched on once.
    std::string crashed_plugin_; // The plugin whose code crashed the previous run: switched off at start.
    bool session_saved_ = false;  // saveSessionState() ran (Windows is ending the session).
    // The library is still arriving: Steam's first scan, or the first PC store scan when stores
    // are on (or not yet detected). "No games" is shown only after both.
    bool libraryLoading() const {
        return scanning_ || (!pc_initial_ready_ && (!settings_.enabled_pc_sources.empty() || !settings_.pc_sources_detected));
    }
    uint32_t pc_scan_generation_ = 0; // Completion identity, even when its result has been invalidated.
    std::function<PcLibraryMessage(const PcLibraryRequest&, const std::function<bool()>&)> read_pc_library_;
    bool battlenet_available_ = true;
    bool ubisoft_account_ = false; // Ubisoft Connect's ownership cache was read in the last scan.
    ULONGLONG next_pc_poll_ = 0;
    std::vector<std::wstring> pc_scan_errors_;
    // User-added games: definitions (custom-games.json), found files by ID (absent = not
    // checked yet, treated as present) and the library games built from both.
    std::vector<custom_games::Entry> custom_games_;
    std::unordered_map<std::string, bool> custom_installed_;
    std::vector<Game> custom_library_;
    std::shared_ptr<std::atomic_uint32_t> custom_token_ = std::make_shared<std::atomic_uint32_t>(0);
    custom_game_ui::State custom_editor_;
    file_browser::State file_browser_;
    std::function<void(const std::filesystem::path&)> file_browser_done_;
    uint64_t file_browser_preview_at_ = 0; // GetTickCount64() when the highlighted image is read; 0 = none.
    std::filesystem::path file_browser_preview_path_;
    settings_ui::ColorEditor color_editor_;
    // Injectable so UI tests never show a modal Windows dialog.
    std::function<std::filesystem::path(HWND, bool folder, const std::filesystem::path& initial)> pick_path_; // Tests only.
    // DRM-free folders: the catalog (drm-free.json), its library games and the one scan at a time.
    drm_free::Catalog drm_catalog_;
    std::vector<Game> drm_library_;
    std::shared_ptr<std::atomic_bool> drm_stop_;     // Interrupts the running scan (keeps its partial result).
    uint32_t drm_generation_ = 0;                     // The running scan; older messages are ignored.
    bool drm_scanning_ = false, drm_identifying_ = false, drm_idle_scan_ = false;
    bool drm_pending_ = false, drm_pending_full_ = false, drm_started_ = false;
    drm_free::Progress drm_progress_;
    ULONGLONG drm_next_idle_ = 0;
    std::string drm_confirm_; // Folder row waiting for its second "Usuń".
    // Injectable so UI tests never show a modal Windows dialog.
    std::function<std::filesystem::path(HWND)> pick_games_folder_; // Tests only.
    std::function<std::filesystem::path(HWND, const std::filesystem::path&)> pick_sound_folder_; // Tests only.
    std::vector<std::wstring> sound_files_; // Audio files of settings_.sound_folder, as listed in Settings → Dźwięk.
    std::function<std::filesystem::path(HWND)> pick_image_; // Tests only ("Zmień grafikę → Z dysku…").
    std::vector<Game> shortcut_library_, nested_games_;
    std::unordered_map<std::wstring, std::vector<Game>> platform_games_;
    std::wstring nested_platform_;
    std::vector<SteamCollection> collections_source_, settings_collections_;
    std::unordered_set<uint32_t> excluded_ids_;
    // Asks the Steam worker for a fresh snapshot of the same account (LibraryMessage::refresh).
    // Otherwise the worker sleeps until Steam's files or registry really change.
    std::shared_ptr<WakeSignal> steam_rescan_ = std::make_shared<WakeSignal>();
    // Wakes the PC store watcher (shutdown); it rescans only after a store's data changed.
    std::shared_ptr<WakeSignal> pc_watch_wake_ = std::make_shared<WakeSignal>();
    std::vector<Game> search_results_;
    std::vector<std::wstring> category_titles_;
    Category private_category_; // Title follows the language in buildShelves().
    LruCache<CachedArt, CachedArtBytes> art_{art_budget};
    std::vector<GameCard> cards_;
    Audio audio_;
    VolumeMonitor volume_monitor_;
    ControllerInput controller_input_;
    // The pad and headset censuses sleep until a device change (or their slow battery refresh).
    std::shared_ptr<WakeSignal> controller_rescan_ = std::make_shared<WakeSignal>();
    std::shared_ptr<WakeSignal> headphones_rescan_ = std::make_shared<WakeSignal>();
    std::function<std::vector<devices::Controller>()> read_controllers_ = enumerateControllers;
    std::function<std::optional<headphones::State>()> read_headphones_ = [] { SystemAudio audio; return audio.headphonesState(); };
    bool headphones_connected_ = false, headphone_image_requested_ = false, headphone_image_ready_ = false;
    std::vector<devices::Controller> connected_pads_;
    size_t connected_controllers_ = 0;
    bool couch_category_pending_ = false;
    std::wstring controller_id_;
    std::wstring controller_name_;
    std::unique_ptr<DeviceService> device_service_;
    device_ui::State device_state_;
    ScreenMode device_return_ = ScreenMode::shelves;
    devices::Panel bluetooth_return_ = devices::Panel::controllers;
    bool bluetooth_from_panel_ = false; // Bluetooth opened from another device panel (Back returns there).
    ULONGLONG next_panel_poll_ = 0;
    // Settings → Dźwięk / Kontrolery: the panel the service shows for them, its latest data and
    // whether one snapshot of that panel arrived; a pending system volume command (its sequence).
    std::optional<devices::Panel> settings_device_panel_;
    devices::Snapshot settings_devices_;
    bool settings_devices_ready_ = false;
    ULONGLONG settings_device_poll_ = 0;
    std::array<uint64_t, 2> settings_volume_pending_{}; // Output, input.
    int audio_drag_column_ = -1;
    uint64_t audio_command_sequence_ = 0;
    std::unordered_set<int> controller_images_;
    std::unordered_set<int> controller_image_requests_;
    Renderer renderer_;
    FrameProfiler profiler_;
    int refresh_hz_ = 0, target_hz_ = 60;
    bool paint_deferred_ = false, frame_painting_ = false, size_move_ = false; // deferPaint()/updateFrame().
    ULONGLONG last_tick_ms_ = 0;
    std::deque<Notification> notifications_;
    std::optional<VolumeNotice> volume_notice_;
    size_t selected_ = 0;
    size_t category_ = 0;
    std::vector<Game> xmb_top_;    // XMB top row: recent games, all, collections, PC stores, consoles, settings.
    size_t xmb_column_ = 0;        // Focused top-row tile.
    int xmb_row_ = -1;             // -1 = the tile itself, otherwise a game in its column.
    std::unordered_map<std::string, int> xmb_rows_; // Remembered column position per xmbKey.
    // Identity of every XMB card (top tile or column game) and Renderer::slotGeneration() after
    // the last rebuildXmb, as flix_slots_: a rebuild over the same list keeps unchanged tiles.
    std::vector<std::string> xmb_card_keys_;
    uint64_t xmb_slots_ = ~uint64_t{0};
    // The same for the default shelves: the shelf's category id and its cards' identities.
    std::string shelf_cards_category_;
    std::vector<std::string> shelf_card_keys_;
    uint64_t shelf_slots_ = ~uint64_t{0};
    ScreenMode screen_ = ScreenMode::shelves;
    float grid_scroll_ = 0;
    std::wstring search_query_;
    size_t search_after_ = 0; // Characters after the search field's insertion point.
    size_t searchCaret() const { return search_query_.size() - std::min(search_after_, search_query_.size()); }
    bool search_dirty_ = false;
    std::chrono::steady_clock::time_point search_edited_{};
    std::vector<settings_ui::Row> settings_rows_;
    std::string settings_rows_page_; // settingsPage() of settings_rows_.
    settings_ui::State settings_state_;
    bool settings_dirty_ = false;
    bool settings_save_failed_ = false;
    bool settings_dragging_ = false;
    settings_ui::LayoutPreview settings_preview_;
    int settings_hover_row_ = -1;
    struct TourState {
        bool open = false;
        int step = 0;            // tour::Step.
        int button = 2;          // tour::Button with keyboard/pad focus.
        bool tiles = true;       // Last step: the layout tiles have the focus (false: the buttons).
        int icon = 0;            // Quick bar step: the icon being described.
        uint64_t step_at = 0, icon_at = 0;
        float alpha = 0;         // Overlay fade in and out.
        int hover_button = -1, hover_tile = -1;
        settings_ui::LayoutPreview preview;
    } tour_;
    std::chrono::steady_clock::time_point settings_edited_{};
    std::wstring settings_secret_input_;
    bool settings_secret_selected_ = false;
    bool settings_secret_cancel_ = false; // The key/text dialog's Cancel has the focus (Left), else Save.
    bool settings_secret_browse_ = false; // Import-path dialog: Browse, before Cancel and Save.
    AppSettings settings_;
    LibraryState library_state_;
    std::unique_ptr<LibraryStateWriter> library_writer_;
    library_ui::State library_controls_;
    float library_menu_progress_ = 0, library_views_progress_ = 0; // Filter menu, view drawer.
    float library_dropdown_progress_ = 0; // Independent selector reveal; reversible on a quick close/reopen.
    library_ui::Panel library_panel_shown_ = library_ui::Panel::none; // Last open panel, kept while it fades out.
    // Library search field: typed text is applied a moment later (like the search screen).
    bool library_query_dirty_ = false;
    std::chrono::steady_clock::time_point library_query_edited_{};
    float library_search_progress_ = 0; // The field unfolding under the title.
    // Quick scrolling: when the current run of vertical moves began, the last move, overlay opacity.
    std::chrono::steady_clock::time_point fast_scroll_started_{}, fast_scroll_at_{};
    float fast_scroll_alpha_ = 0;
    // Flix rows: category index, its cards in cards_ and the first column on screen.
    struct FlixRow {
        size_t category = 0;
        size_t begin = 0, count = 0;
        size_t first = 0;
        std::string id; // Category id: a rebuild keeps the row's scroll.
    };
    std::vector<FlixRow> flix_rows_;
    std::vector<uint32_t> flix_card_rows_; // Row of every card.
    size_t flix_row_ = 0;
    // The unfolded tile (flixUnfoldReady): its card and how far it has opened (0..1); a new focus
    // folds it back first. flix_focus_at_: when the focus last moved, the start of the rest.
    size_t flix_unfold_index_ = SIZE_MAX;
    float flix_unfold_ = 0;
    std::chrono::steady_clock::time_point flix_focus_at_{};
    // Renderer::slotGeneration() right after the last rebuildFlix: unchanged means cards_ and the
    // renderer slots are still that Flix list, so the next rebuild can keep its tiles.
    uint64_t flix_slots_ = ~uint64_t{0};
    // Category id -> game key whose background the row's "Wszystkie" tile shows this session.
    std::unordered_map<std::string, std::string> flix_backdrops_;
    std::mt19937 flix_random_{std::random_device{}()};
    // Metro pages: category index, its tiles in cards_ and their page-space extent.
    struct MetroPage {
        size_t category = 0;
        size_t begin = 0, count = 0;
        float left = 0, right = 0;
        std::string id; // Category id: a rebuild keeps the tiles of an unchanged page.
    };
    struct MetroTile {
        metro_layout::Role role = metro_layout::Role::game;
        metro_layout::Rect rect;
        uint32_t page = 0;
    };
    std::vector<MetroPage> metro_pages_;
    std::vector<MetroTile> metro_tiles_; // One per card.
    size_t metro_page_ = 0;
    float metro_scroll_ = 0; // Page-space x shown at metro_layout::left.
    float metro_tab_ = 0;    // Eased page position: the tab strip glides with the pages.
    // The tab strip has the focus (Up from a page's top row): Left/Right change the page,
    // Down, Enter/A and Back return to its remembered tile.
    bool metro_tabs_ = false;
    uint64_t metro_slots_ = ~uint64_t{0}; // As flix_slots_.
    int psfive_action_ = -1; // PSfive: 0 "Graj", 1 "…" under the row; -1 the tiles have the focus.
    int psfive_top_ = -1;    // PSfive: the focused tab, then the Widgets/Menu icons; -1 none.
    bool psfive_more_hover_ = false; // Open overflow once on pointer entry, not again after Esc.
    bool psfive_top_hover_ = false;
    bool library_state_dirty_ = false, library_state_save_failed_ = false;
    bool library_state_readable_ = true;
    bool restore_pending_ = false, library_transition_ = false, pc_initial_ready_ = false;
    ULONGLONG next_library_save_ = 0;
    uint64_t account_ = 0;
    bool metadata_ready_ = true;
    // Ctrl+H for this session: private_category_ is the last category ("private"); its games are
    // on screen legitimately, so hidden-game cleanups leave them alone.
    bool private_shelf_ = false;
    bool scanning_ = true;
    bool runtime_started_ = false;
    bool fullscreen_ = false;
    bool pad_connected_ = false;
    bool volume_initialized_ = false;
    VolumeLevel last_volume_;
    bool enter_held_ = false;
    bool pad_enter_hold_ = false; // enter_held_ was started by A on the pad; its release ends it.
    osk::State keyboard_;
    float keyboard_progress_ = 0; // Slide-in of the on-screen keyboard.
    uint64_t keyboard_erase_repeat_ = 0; // Next repeat of a held B (Backspace).
    bool pad_input_ = false;        // The last button/key came from the pad.
    bool text_field_shown_ = false; // A typing field was active on the previous frame.
    bool menu_open_ = false;
    bool quick_open_ = false;
    float quick_progress_ = 0;
    int quick_selected_ = 0;
    bool category_menu_open_ = false;
    bool category_rail_focus_ = false;
    float category_menu_progress_ = 0;
    float category_highlight_y_ = 0;
    float category_menu_scroll_ = 0;
    int category_cursor_ = 0;
    // Short/hold gestures (src/app/input.cpp). Keyboard TAB = pad Start/Options
    // (friends / audio panel), keyboard Home = pad Guide/PS (menu / power).
    HoldGesture tab_key_, home_key_, pad_start_, pad_guide_;
    // Power menu: selected row while open, rows offered on this machine.
    std::optional<int> power_menu_;
    std::vector<power_menu::Item> power_items_;
    // Injectable so UI tests never sleep or shut down the machine.
    std::function<std::vector<PowerAction>()> power_actions_ = availablePowerActions;
    std::function<bool(PowerAction)> power_ = performPowerAction;
    bool friends_open_ = false;
    bool friends_loading_ = false;
    float friends_progress_ = 0;
    int friends_scroll_ = 0;
    size_t friends_cursor_ = 0;
    uint32_t friends_generation_ = 0;
    FriendsResult friends_;
    // Drawer dashboard: tiles by default, the Steam friends list after activating its tile.
    bool friends_list_ = false;
    int dashboard_tile_ = 0;
    float dashboard_scroll_ = 0;
    bool dashboard_editing_ = false, dashboard_grabbed_ = false;
    int dashboard_press_source_ = 0, dashboard_pressed_tile_ = -1; // 1 keyboard, 2 pad, 3 mouse.
    uint64_t dashboard_pressed_at_ = 0, dashboard_edit_at_ = 0;
    float dashboard_pointer_x_ = 0, dashboard_pointer_y_ = 0;
    bool dashboard_pointer_release_ = false; // Swallow click-up even after a cancelled overlay drag.
    // Last poll; kept between openings so the tiles never flash empty.
    sensors::Reading sensors_;
    bool discord_running_ = false;
    DiscordBackend discord_;
    // Empty = the real PDH/D3DKMT reader, created on the poll worker. Tests inject a fixture.
    std::function<sensors::Reading()> read_sensors_;
    // Injectable so UI tests format a fixed moment.
    std::function<SYSTEMTIME()> local_time_ = [] { SYSTEMTIME now{}; GetLocalTime(&now); return now; };
    uint64_t clock_minute_ = 0;   // drawer_clock::minute() of the painted time.
    ULONGLONG clock_check_at_ = 0; // Next GetTickCount64() to compare it with the local time.
    int menu_selected_ = 0;
    // Clock of ambient motion (drift, glow, spinning disc, caret). It only runs while
    // someone watches, so an idle or background launcher stops painting frames and
    // resumes later from the same phase instead of jumping.
    float ambient_time_ = 0;
    std::chrono::steady_clock::time_point last_input_ = std::chrono::steady_clock::now();
    float arrived_ = 0;
    // The next tile wave opens a fresh Tryb TV screen (startup, from the desktop window, a new layout):
    // the PS5-like entrance waits a moment on the bare background first. Other rebuilds (another
    // shelf, a filter) start at once.
    bool entrance_lead_ = true;
    float detail_ = 0;
    float menu_progress_ = 0;
    float launch_progress_ = 0;
    float launch_age_ = 0;
    bool pending_launch_ = false;
    Game pending_game_;
    std::string launch_played_key_; // Focused in Recent once the cover animation ends.
    std::string launch_artwork_key_;
    std::optional<std::chrono::steady_clock::time_point> launch_animation_started_;
    std::wstring launch_animation_title_;
    RECT previous_rect_{};
    std::chrono::steady_clock::time_point enter_started_{};
    std::chrono::steady_clock::time_point last_frame_ = std::chrono::steady_clock::now();
    int controller_direction_ = 0;
    bool navigation_edge_ = false;
    int pad_edge_direction_ = 0;
    int nav_direction_ = 0; // Last key/pad move between cards: ±1 horizontal, ±2 vertical.
    ULONGLONG nav_direction_at_ = 0; // GetTickCount64() of that move: mouse focus changes get no glow.
    struct FocusRing {
        Spring x, y, width, height;
        float glow = 0, glow_peak = 1; // glow: 1 → 0 fade; peak: 1 at a list edge, 0.5 for a plain move.
        int glow_direction = 0;
        size_t selected = 0;
        bool valid = false;
    } focus_ring_;
    Spring quick_circle_;
    bool quick_circle_valid_ = false;
    struct SettingsMotion {
        Spring pill_top, pill_bottom, rail_focus, row, scroll, height;
        int category = -1;
        bool valid = false;
    } settings_motion_;
    // Menu highlights (focus_motion.cpp): rows by fractional index, frames by their edges.
    struct IndexGlide { Spring value; bool valid = false; };
    IndexGlide menu_glide_, power_glide_, exit_glide_, library_choice_glide_;
    IndexGlide settings_choice_glide_; // Settings picker highlight.
    IndexGlide library_tab_glide_;     // The library's category tab strip.
    IndexGlide library_flow_;          // Półka: Cover Flow's gliding centre (a fractional card index).
    // Settings picker fade (paint.cpp): open or not at the last paint, when that changed, and the
    // row/tile/choice it showed (drawn while it fades out).
    bool settings_picker_open_ = false;
    uint64_t settings_picker_changed_ = 0;
    int settings_picker_row_ = -1, settings_picker_tile_ = 0, settings_picker_choice_ = 0;
#ifdef LAUNCHME_UI_TEST
    bool picker_motion_for_test_ = false; // Captures show pickers settled unless a test asks for the motion.
    bool screen_motion_for_test_ = false; // Captures show screens settled unless a test asks for the transition.
#endif
    static constexpr float screen_transition_duration = 0.35f;
    void beginScreenTransition();
    void syncScreenTransition();
    void snapCardsForTransition();
    ScreenMode transition_screen_ = ScreenMode::shelves;
    float screen_transition_ = 1.0f;
    float wallpaper_zoom_ = 1.0f;
    bool settingsPicking() const;
    struct BoxGlide { SpringBox box; int key = -1; bool valid = false; };
    BoxGlide device_glide_, drawer_glide_, library_row_glide_, library_action_glide_; // Drawer: x relative to the drawer's left edge.
    BoxGlide psfive_action_glide_; // PSfive: the focus frame between "Graj" and "…".
    BoxGlide details_glide_, artwork_glide_; // Szczegóły: the focus between Back, the buttons and the cards; Zmień grafikę: its rail.
    BoxGlide library_bar_glide_, manager_glide_; // Library top bar; the Kolekcje / Wersje dialog.
    BoxGlide downloads_glide_; // Pobierania: Back or a row.
    struct PsfiveMenuMotion { Spring row, scroll; bool valid = false; } psfive_menu_motion_;
    Spring psfive_top_opacity_{0.66f};
    Spring friends_scroll_glide_;
    bool friends_scroll_valid_ = false;
    bool friends_back_ = false; // Friends list: Back (top left) has the keyboard/pad focus.
    // Search and Store: Back (top left) has the focus (Up from the first row; with no results the only target).
    bool grid_back_focus_ = false;
    bool searchBackFocused() const { return grid_back_focus_ && (screen_ == ScreenMode::search || screen_ == ScreenMode::store); }
    std::function<void()> boundary_feedback_ = [this] { controller_input_.pulseBoundary(); };
    WORD controller_buttons_ = 0;
    ULONGLONG controller_repeat_ = 0;
    ULONGLONG pad_last_seen_ = 0;
    ULONGLONG mouse_transition_until_ = 0;

    // ---- Desktop mode (src/app/desktop.cpp, desktop_window.cpp) ----
    // Off until initialize() reads Settings: the UI tests build a Launcher without it and see Tryb TV.
    bool desktop_mode_ = false;
    struct DesktopDrag {
        enum class Kind { none, card, entry, scrollbar, slider, pane } kind = Kind::none; // pane: its left edge.
        float x0 = 0, y0 = 0, x = 0, y = 0; // Logical (desktop) coordinates.
        bool active = false;                // Past the threshold: a drag, not a click.
        int card = -1, row = -1;            // The pressed card / sidebar row.
        float grab = 0;                     // Scrollbar: where the thumb was taken.
        std::vector<std::string> games;     // Selection keys being carried.
        int drop_row = -1;                  // Sidebar row under the pointer that accepts them.
        float reorder_y = -1;               // Collections: the line where the dragged one would land.
        int reorder_before = -1;            // …before this entry (-1: after the last collection).
    };
    std::vector<desktop_ui::Entry> desktop_entries_;
    std::string desktop_section_ = "all";
    std::array<bool, desktop_ui::group_count> desktop_collapsed_{};
    std::array<float, desktop_ui::group_count> desktop_collapse_anim_{};
    bool desktop_collapse_valid_ = false;
    float desktop_narrow_anim_ = 0;   // The sidebar 0 wide … 1 narrow, easing to settings_.desktop_narrow.
    bool desktop_narrow_valid_ = false;
    int desktop_columns_ = 0;         // The grid's columns last laid out: a change asks for the new posters' art.
    Spring desktop_active_y_;
    bool desktop_active_y_valid_ = false;
    std::string desktop_drop_flash_id_;
    float desktop_drop_flash_ = 0;
    float desktop_refresh_spin_ = 0, desktop_refresh_angle_ = 0;
    std::unordered_map<std::string, float> desktop_mark_pulse_;
    float favorite_pulse_ = 0;
    std::string favorite_pulse_key_;
    std::unordered_map<std::string, float> settings_toggles_;
    std::string settings_toggles_page_;
    std::wstring desktop_query_;
    size_t desktop_caret_ = 0;
    bool desktop_query_dirty_ = false, desktop_search_focus_ = false;
    std::chrono::steady_clock::time_point desktop_query_edited_{};
    float desktop_scroll_ = 0, desktop_scroll_target_ = 0, desktop_scrollbar_ = 0;
    ULONGLONG desktop_scrolled_at_ = 0;
    float desktop_sidebar_scroll_ = 0, desktop_pane_scroll_ = 0;
    int desktop_zone_ = 1, desktop_sidebar_focus_ = -1, desktop_pane_focus_ = 0;
    bool desktop_keyboard_ = false;
    desktop_ui::Hit desktop_hover_, desktop_pressed_;
    ULONGLONG desktop_hover_at_ = 0;
    float desktop_tooltip_ = 0;
    std::optional<desktop_ui::Menu> desktop_menu_;
    bool desktop_downloads_open_ = false;
    std::vector<downloads_ui::Row> desktop_download_rows_;
    std::vector<Game> desktop_download_games_;
    ULONGLONG desktop_downloads_at_ = 0;
    DesktopDrag desktop_drag_;
    std::unordered_set<std::string> desktop_marked_;
    size_t desktop_anchor_ = 0;
    std::vector<std::string> desktop_keys_; // Selection keys of cards_, for rebuilds that keep the art.
    float desktop_request_scroll_ = 0; // Where desktopRequestArt last looked…
    int desktop_scroll_dir_ = 1;       // …and which way the grid went since: the look-ahead side.
    // Games whose poster the disk lane did not find (CoverMessage::cache_miss): their square
    // cover stands in until a download brings the poster.
    std::unordered_set<std::string> desktop_poster_missing_;
    uint64_t desktop_slots_ = ~uint64_t{0};
    std::vector<float> desktop_card_hover_;
    std::string desktop_focus_key_, desktop_restore_key_;
    float desktop_arrived_ = 10;
    bool desktop_max_hover_ = false, desktop_max_pressed_ = false, desktop_window_active_ = true;
    bool desktop_bubble_hover_ = false;
    GameDetails desktop_details_;
    std::string desktop_details_key_;
    uint64_t desktop_details_revision_ = ~uint64_t{0};
    bool desktop_details_preview_ = false;
    desktop_ui::Layout desktopLayout() const;
    std::vector<desktop_ui::Tool> desktopTools() const;
    float desktopScale() const; // Client pixels per desktop DIP.
    void buildDesktopSidebar();
    std::vector<desktop_ui::Row> desktopRows() const;
    int desktopEntryIndex(const std::string& id) const;
    const desktop_ui::Entry* desktopEntry() const;
    std::vector<Game> desktopSource() const;
    std::vector<Game> desktopGames() const;
    void rebuildDesktop(bool preserve_background);
    void desktopShowSection(const std::string& id);
    void desktopStepSection(int amount);
    void desktopFocus(size_t index, bool reveal = true);
    void desktopRequestArt();
    // The grid's cards around the view, in the order their art is wanted (desktop.cpp).
    struct DesktopArtRange { size_t first = 0, end = 0, keep_first = 0, keep_end = 0; std::vector<size_t> order; };
    DesktopArtRange desktopArtRange() const;
    uint32_t desktopArtEdge() const; // Decode cap for the grid's posters: a cell's width in pixels.
    bool tickDesktop(float dt);
    void desktopView(desktop_ui::View& view);
    const GameDetails& desktopDetails(const Game& game, bool& loading);
    desktop_ui::Hit desktopHit(float x, float y) const;
    void desktopClick(float mouse_x, float mouse_y, bool double_click);
    void desktopMouseMove(float mouse_x, float mouse_y);
    void desktopContextClick(float mouse_x, float mouse_y);
    void desktopActivate(const desktop_ui::Hit& hit);
    void desktopMove(int dx, int dy, bool extend = false);
    void desktopAccept();
    void desktopBack();
    bool desktopCharacter(wchar_t character);
    void desktopApplyQuery();
    void desktopSearchFocus(bool focus);
    void desktopLaunchSelected();
    void desktopPickSelected(); // Enter/A/double click on a card: the default game action (details or launch).
    void desktopScrollBy(float amount);
    void desktopSetPoster(float width);
    void desktopSetPane(int width);   // The pane's width by its edge; 0 automatic.
    void desktopToggleSidebar();      // Wide ↔ narrow (the toggle by the logo).
    void desktopReflow();             // After the grid's width changed: scroll kept, art asked for.
    void desktopToggleMark(size_t index);
    void desktopMarkRange(size_t from, size_t to);
    std::vector<Game> desktopTargets() const;
    std::vector<Game> desktopGamesOf(const std::vector<std::string>& keys) const;
    std::pair<bool, bool> desktopEmptyButtons() const; // The empty grid's primary and secondary buttons.
    void desktopOpenSubmenu(int index);
    void desktopOpenGameMenu(float x, float y, bool keyboard);
    void desktopOpenSectionMenu(int entry, float x, float y);
    void desktopOpenSortMenu();
    void desktopOpenCollectionsMenu(float x, float y);
    void desktopOpenStoreMenu();
    std::vector<desktop_ui::MenuItem> desktopCollectionItems(const std::vector<Game>& games) const;
    void desktopMenuKey(WPARAM key);
    void desktopMenuPick(const desktop_ui::MenuItem& item, bool from_sub);
    void desktopCloseMenu();
    SteamCollection desktopFavorites() const;
    const SteamCollection* desktopCollection(const std::string& id) const;
    void desktopSetMember(const std::vector<Game>& games, const SteamCollection& collection, bool member);
    bool desktopMember(const Game& game, const SteamCollection& collection) const;
    void desktopDrop(const desktop_ui::Entry& entry, const std::vector<std::string>& keys);
    void desktopReorder(const std::string& collection, int before);
    void desktopNewCollection(std::vector<Game> games);
    void desktopCollectionDialog(const std::string& id, library_manager_ui::Page page, bool confirm = false);
    void desktopMoveCollection(const std::string& id, int amount);
    void desktopToolPick(desktop_ui::Tool tool);
    void desktopToggleDownloads();
    void desktopRefreshDownloads();
    std::wstring desktopTooltip(const desktop_ui::Hit& hit) const;
    desktop_ui::Rect desktopTooltipAnchor(const desktop_ui::Hit& hit) const;
    void desktopSaveState();
    // Widżety under their shortcut (desktop_input.cpp): friends online, the plugins' widgets.
    bool desktop_widgets_open_ = false;
    std::vector<uint64_t> desktop_widget_friend_ids_; // Steam IDs of View::widget_friends, in order.
    void desktopToggleWidgets();
    void desktopRefreshLibraries(); // F5 and Biblioteka's refresh button.
    // Pobierania's "Wyczyść listę": game key → its state when put away (shown again once it changes).
    std::unordered_map<std::string, std::wstring> desktop_cleared_downloads_;
    void openDownloadSource(const Game& game, const downloads_ui::Row& row); // Pobierania's row action (downloads.cpp).
    bool desktopDownloadsClearable() const; // The popover's Wyczyść listę: something is not moving.
    // Ctrl+K (desktop_quick.cpp): the quick search over the library window.
    // Text selection in the desktop window's fields (desktop_text.cpp).
    struct DesktopText { std::wstring* text = nullptr; size_t* caret = nullptr; bool* all = nullptr; size_t limit = 0; bool multiline = false; int owner = 0; };
    DesktopText desktopText();
    std::pair<size_t, size_t> desktopTextSelection() const;
    void desktopTextChanged(int owner);
    bool desktopTextKey(WPARAM key);
    void desktopTextBeforeTyping();
    void desktopTextPress(float left, float x, float scale, bool double_click, bool title_font);
    bool desktopTextDrag(float mouse_x);
    void desktopTextRelease();
    const std::wstring* desktop_text_field_ = nullptr;
    size_t desktop_text_anchor_ = std::wstring::npos;
    bool desktop_text_drag_ = false, desktop_text_drag_title_ = false;
    float desktop_text_drag_left_ = 0, desktop_text_drag_scale_ = 1;
    // Edytuj metadane (desktop_metadata.cpp): the player's own title and details over the found ones.
    desktop_ui::MetaView desktop_meta_;
    std::string desktop_meta_key_;
    mutable metadata::Overrides metadata_overrides_; // Read on first use (metadata_overrides.json).
    mutable bool metadata_loaded_ = false;
    const metadata::Override* metadataOverride(const Game& game) const;
    void applyMetadataTitles(std::vector<Game>& games) const;
    void applyMetadataToIndex(game_index::LibraryGame& item, const Game& game) const;
    void forgetTitleLookups(const std::string& key);
    void refreshTitleLookups(); // Once a start: older title overrides get fresh lookups.
    bool metadata_lookups_checked_ = false;
    void desktopMetaOpen(const Game& game);
    void desktopMetaClose();
    void desktopMetadataChanged();
    void desktopMetaSave();
    void desktopMetaActivate(int item);
    void desktopMetaInsert(std::wstring_view text);
    void desktopMetaChar(wchar_t character);
    bool desktopMetaKey(WPARAM key);
    bool desktopMetaPointer(float mouse_x, float mouse_y, bool press, bool double_click = false);
    desktop_ui::QuickView desktop_quick_;
    ULONGLONG desktop_quick_blink_ = 0; // Its caret's phase.
    void desktopQuickOpen();
    void desktopQuickClose();
    void desktopQuickUpdate();
    void desktopQuickRun(int index, bool play);
    bool desktopQuickKey(WPARAM key);
    void desktopQuickChar(wchar_t character);
    bool desktopQuickPointer(float mouse_x, float mouse_y, bool press, bool double_click = false);
    void pollJumpList();     // Waiting command run once the library holds its game; the list follows Ostatnio grane.
    std::wstring jump_pending_;
    ULONGLONG jump_pending_since_ = 0, jump_list_checked_ = 0;
    std::string jump_list_signature_;
    bool desktopOwnInstall(const Game& game) const; // GOG/Epic installed by the launcher: Anuluj opens its dialog.
    bool desktopStoreKey(WPARAM key); // Sklep: arrows walk the results, Enter, Esc; typing stays with the field.
    void desktopLeaveLibrary(); // A page opens over the library: one last frame of it is kept behind the page.
    // Installations under way now, by game key (Steam's observer, Launchme's GOG/Epic installer, Battle.net):
    // the library's cards keep the copy they were built with.
    std::unordered_map<std::string, Installation> desktopInstallations() const;
    void desktopSyncInstallations(); // Cards and the pane follow them (bar and per cent).
    ULONGLONG desktop_install_sync_ = 0;
    std::vector<Game> desktopFavoriteGames() const; // Ulubione as Tryb TV's shelf shows it.
    // Dialogs of the desktop window (desktop_settings.cpp; src/ui/desktop_sheet.hpp).
    float desktop_sheet_scroll_ = 0, desktop_sheet_scroll_target_ = 0;
    Spring desktop_sheet_nav_;
    int desktop_sheet_nav_screen_ = -1;
    desktop_ui::SheetHit desktop_sheet_hover_, desktop_sheet_pressed_;
    bool desktop_sheet_keyboard_ = false;
    std::string desktop_sheet_page_;
    int desktop_sheet_row_ = -1;
    ULONGLONG desktop_sheet_caret_ = 0;
    bool desktopSheet() const; // A TV screen shown as a desktop dialog (Ustawienia).
    desktop_ui::settings_sheet::Layout desktopSettingsLayout() const;
    settings_ui::Rect desktopPickerAnchor(const desktop_ui::settings_sheet::Layout& layout) const;
    desktop_ui::SheetHit desktopSettingsHit(float x, float y) const;
    void desktopSettingsClick(float mouse_x, float mouse_y, bool double_click);
    desktop_ui::SheetHit desktopHotspotHit(float x, float y) const; // Szczegóły, Zmień grafikę.
    float desktopSheetViewHeight() const;
    void desktopPageActivate(const desktop_ui::SheetHit& hit, bool double_click);
    void desktopSettingsRelease(float mouse_x, float mouse_y);
    void desktopSettingsActivate(const desktop_ui::SheetHit& hit);
    void desktopSettingsMove(float mouse_x, float mouse_y);
    bool desktopSheetWheel(float mouse_x, float mouse_y, int delta);
    LPCWSTR desktopSheetCursor(float mouse_x, float mouse_y) const;
    bool tickDesktopSheet(float dt);
    std::string desktopSettingsPage() const; // Ustawienia's page key: a new one starts at its top.
    void desktopSheetView(desktop_ui::SheetView& view) const;
    // Tryb TV's dialogs drawn as desktop dialogs (desktop_dialogs.cpp): Kolekcje, Dodaj grę, Zidentyfikuj grę.
    desktop_ui::SheetHit desktop_dialog_hover_;
    ULONGLONG desktop_dialog_caret_ = 0;
    bool desktopModalShown() const;
    desktop_ui::SheetHit desktopModalHit(float mouse_x, float mouse_y) const;
    bool desktopModalClick(float mouse_x, float mouse_y, bool double_click);
    bool desktopModalMove(float mouse_x, float mouse_y);
    LPCWSTR desktopModalCursor(float mouse_x, float mouse_y) const;
    bool tickDesktopDialog();
    // The window (desktop_window.cpp).
    int desktop_strip_hover_ = -1; // Window button hovered over a TV screen in the desktop window.
    float windowDpi() const;
    int windowStripHit(float mouse_x, float mouse_y) const;
    bool windowStripClick(float mouse_x, float mouse_y);
    void applyWindowMode();
    void rememberDesktopWindow();
    RECT desktopWindowRect() const;
    void toggleMaximized();
};

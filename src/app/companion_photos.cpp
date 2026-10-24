// Towarzyszka's photos in the chat (the work: src/net/companion_photos.cpp). A new conversation
// draws after which of the player's messages (2nd, 3rd or 5th) she sends one; at that message a
// worker picks a random picture of hers from the chosen folder (<folder>/<her key>/, its level
// ranges as far as her level goes), at most one a day; it appears once her answer has been
// spoken, with a line in her style.
#include "launcher.hpp"
#include "../net/companion_person.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"
#include "../platform/file_dialog.hpp"

#include <algorithm>
#include <array>

namespace {
namespace photos = companion::photos;
int today() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    return local.wYear * 10000 + local.wMonth * 100 + local.wDay;
}
}

void Launcher::companionPhotoChat() {
    ++companion_.photo_chat;
    companion_.photo_turn = photos::turn(companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64()));
    companion_.photo_typed = 0;
    companion_.photo_asked = false;
    companion_.photo.reset(); companion_.photo_pending.reset(); companion_.photo_description.clear();
    companion_.photo_file.clear(); companion_.photo_file_pending.clear();
}

void Launcher::companionPhotoTyped() {
    if (settings_.companion_photo_folder.empty() || companion_.photo_asked || ++companion_.photo_typed != companion_.photo_turn) return;
    companion_.photo_asked = true;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionPhotoMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_photo_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    // Her own folder (<folder>/<her key>/<00-09|10-19|…>) opens with her level; gamification off: all of it.
    const int level = settings_.companion_gamification ? companionLevel(companionLook()) : companion::affinity::max_level;
    work_.start([post, chat = companion_.photo_chat, day = today(), seed = companion_.seed, io = companion_.photo_io,
                 folder = photos::folderOf(userConfigDirectory()), pictures = settings_.companion_photo_folder, look = companionLook(), level] {
        std::lock_guard lock(*io);
        auto state = photos::load(folder);
        if (!photos::due(state, day)) return; // One a day.
        auto message = std::make_unique<CompanionPhotoMessage>();
        message->chat = chat;
        if (const auto file = photos::pick(pictures, look, level, state.recent, seed)) {
            auto bytes = photos::read(*file);
            std::error_code ec;
            const auto name = std::filesystem::relative(*file, pictures, ec).wstring();
            if (bytes) {
                state.last_shown = day;
                message->bytes = std::move(*bytes);
                message->description = photos::description(*file); // <stem>.txt: what it shows.
                if (!ec) message->file = name; // The chat's history keeps where it is, not the picture.
            }
            std::erase(state.recent, name);
            state.recent.push_back(name);
            if (state.recent.size() > 30) state.recent.erase(state.recent.begin());
        }
        photos::save(folder, state);
        post(std::move(message));
    });
}

void Launcher::onCompanionPhoto(std::unique_ptr<CompanionPhotoMessage> message) {
    if (message->bytes.empty() || message->chat != companion_.photo_chat) return;
    companion_.photo_pending = std::make_shared<const std::vector<uint8_t>>(std::move(message->bytes));
    companion_.photo_description = std::move(message->description);
    companion_.photo_file_pending = std::move(message->file);
}

bool Launcher::tickCompanionPhoto(float) {
    auto& chat = companion_.chat;
    // After her answer has been spoken: the picture, then her line about it.
    if (!companion_.photo_pending || !companionOpen() || chat.waiting || chat.talking() || companionRouletteSpinning() || chat.archive) return false;
    if (chat.ended) { companion_.photo_pending.reset(); companion_.photo_file_pending.clear(); return false; } // Too late for this conversation.
    companion_.photo = std::move(companion_.photo_pending);
    companion_.photo_pending.reset();
    companion_.photo_file = std::exchange(companion_.photo_file_pending, std::wstring{});
    const bool en = i18n::language() == i18n::Language::en;
    const auto stock = companion::photoLine(companionPersona(), en, companion_.seed);
    companion_ui::Message photo; photo.photo = true; photo.speaker = companionLook();
    chat.log.push_back(std::move(photo));
    const auto description = std::exchange(companion_.photo_description, std::wstring{});
    if (!description.empty() && !companion_.busy) {
        // She knows what the photo shows (<stem>.txt): the model says something about it, in a hidden turn that stays
        // in the conversation (the player may ask about it); offline, or when it fails, her stock line.
        companion_.turns.push_back({true, companion::photoKickoff(description, en)});
        chat.waiting = true; chat.scroll = 0;
        requestCompanion(CompanionMessage::Purpose::chat, {}, stock);
        audio_.context();
        return true;
    }
    const auto line = companion::parseText(companion::personalize(stock, companionPerson())); // Her stock line may speak of you in both genders.
    chat.log.push_back({false, line.text, line.emotions, false});
    chat.log.back().speaker = companionLook();
    chat.reveal = 0; chat.reveal_clock = 0; chat.scroll = 0;
    companion_.turns.push_back({false, line.raw});
    if (!line.emotions.empty()) companion_.rig.emote(line.emotions.front().emotion);
    companionSaveChat();
    audio_.context();
    return true;
}

std::vector<settings_ui::Row> Launcher::companionPhotoRows() const {
    using namespace settings_ui;
    Row folder{Id::companion_import_folder, Control::text, tr(L"Folder zdjęć i feedu"),
        tr(L"Jeden katalog z podfolderami photos i feed. Kliknij, aby ustawić ścieżkę i zobaczyć instrukcję struktury."),
        settings_.companion_import_folder.empty() ? std::wstring(tr(L"Ustaw folder")) : settings_.companion_import_folder};
    folder.section = tr(L"Zdjęcia i Launchfeed");
    return {std::move(folder)};
}

void Launcher::browseCompanionImport() {
    if (!editingSecret() || settings_rows_[settings_state_.row()].id != settings_ui::Id::companion_import_folder) return;
    const auto first = settings_secret_input_.find_first_not_of(L" \t\""), last = settings_secret_input_.find_last_not_of(L" \t\"");
    file_browser::Request request;
    request.mode = file_browser::Mode::folder;
    request.title = tr(L"Folder zdjęć i feedu");
    request.initial = first == std::wstring::npos ? settings_.companion_import_folder : settings_secret_input_.substr(first, last - first + 1);
    request.native = [this, initial = request.initial, title = request.title] {
        return pickPath(hwnd_, true, title, {}, {}, false, initial);
    };
    closeKeyboard();
    openFileBrowser(std::move(request), [this](const std::filesystem::path& folder) {
        if (!editingSecret() || settings_rows_[settings_state_.row()].id != settings_ui::Id::companion_import_folder) return;
        clearSecretInput();
        settings_secret_input_ = folder.wstring();
        settings_secret_selected_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
    });
}

bool Launcher::companionPhotoSetting(const settings_ui::Row& row) {
    using settings_ui::Id;
    if (row.id == Id::companion_import_folder) {
        settingsTextRow(settings_.companion_import_folder, [&](const std::wstring& value) {
            std::error_code ec;
            if (value.size() >= 1024 || value.find_first_of(L"\r\n") != value.npos ||
                (!value.empty() && !std::filesystem::is_directory(value, ec))) {
                showNotification(NotificationKind::error, tr(L"Nie ma takiego katalogu"), value);
                return false;
            }
            const auto path = value.empty() ? std::filesystem::path{} : std::filesystem::absolute(value, ec).lexically_normal();
            if (ec) return false;
            settings_.setCompanionImportFolder(path.wstring());
            companionPhotoChat(); // Drop pending replies from the previous source.
            companionFeedReset();
            return true;
        });
        return true;
    }
    // Text rows: Enter edits the current value, Enter again saves it (empty: the default).
    if (row.id == Id::companion_photo_folder || row.id == Id::companion_feed_folder) {
        const bool feed = row.id == Id::companion_feed_folder;
        settingsTextRow(feed ? settings_.companion_feed_folder : settings_.companion_photo_folder, [&](const std::wstring& value) {
            std::error_code ec;
            if (!value.empty() && !std::filesystem::is_directory(value, ec)) {
                showNotification(NotificationKind::error, tr(L"Nie ma takiego katalogu"), value);
                return false;
            }
            if (feed) {
                auto path = std::filesystem::path(value);
                if (!value.empty() && path.filename().wstring() == text_encoding::wideLenient(companion::lookKey(companionLook()))) path = path.parent_path();
                settings_.companion_feed_folder = path.wstring();
                companionFeedReset();
            } else settings_.companion_photo_folder = value;
            return true;
        });
        return true;
    }
    return false;
}

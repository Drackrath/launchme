#include "companion_backup.hpp"
#include "../platform/hashing.hpp"
#include "companion_characters.hpp"
#include "companion_feed.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/system.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <bcrypt.h>
#include <sddl.h>
#include <objbase.h>
#endif
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>

namespace companion::backup {
namespace {
constexpr uint64_t budget = 8ull * 1024 * 1024 * 1024;
void require(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
std::string read(const std::filesystem::path& path, size_t limit = 128 * 1024 * 1024) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(file && file.tellg() >= 0 && static_cast<uint64_t>(file.tellg()) <= limit, "Cannot read a backup file, or it exceeds its size limit.");
    std::string text(static_cast<size_t>(file.tellg()), '\0'); file.seekg(0);
    require(text.empty() || bool(file.read(text.data(), static_cast<std::streamsize>(text.size()))), "Incomplete file read.");
    return text;
}
bool present(const std::filesystem::path& path) { std::error_code ec; const bool found = std::filesystem::exists(path, ec); require(!ec, "Cannot inspect a source path."); return found; }
void regular(const std::filesystem::path& path) {
    const auto attrs = GetFileAttributesW(path.c_str());
    require(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "Links and reparse points are not admitted in a backup.");
}
std::string sha(const std::filesystem::path& file) {
    { std::ifstream input(file, std::ios::binary); require(bool(input), "Cannot open a backup file."); }
    auto digest = hashing::fileHex(hashing::Algorithm::sha256, file);
    require(!digest.empty(), "SHA-256 read failed.");
    return digest;
}
bool safeRelative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        const auto s = part.wstring();
        if (s.empty() || s == L"." || s == L".." || s.find_first_of(L":\r\n") != s.npos) return false;
    }
    return true;
}
bool within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    if (parent.empty()) return false;
    const auto a = std::filesystem::weakly_canonical(child).wstring(), b = std::filesystem::weakly_canonical(parent).wstring();
    return unicode_text::startsWithIgnoreCase(a, b) &&
        (a.size() == b.size() || a[b.size()] == L'\\' || a[b.size()] == L'/');
}
std::wstring unique() {
    GUID id{}; require(SUCCEEDED(CoCreateGuid(&id)), "Cannot create a backup identity.");
    wchar_t text[40]{}; StringFromGUID2(id, text, 40);
    return std::wstring(text + 1, 36);
}
struct Staging {
    std::filesystem::path path;
    ~Staging() { if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path, ec); } }
};
struct Writer {
    std::filesystem::path root;
    json::Value files = json::array();
    uint64_t bytes = 0;
    void record(const std::filesystem::path& relative) {
        const auto full = root / relative;
        regular(full);
        const auto size = std::filesystem::file_size(full);
        require(files.elements.size() < 20000 && size <= budget && bytes <= budget - size, "Backup exceeds the 8 GiB / 20000 file limit.");
        auto item = json::object(); item.properties["path"] = json::string(text_encoding::utf8(relative.generic_wstring()));
        item.properties["bytes"] = json::number(static_cast<double>(size)); item.properties["sha256"] = json::string(sha(full));
        files.elements.push_back(std::move(item)); bytes += size;
    }
    void text(const std::filesystem::path& relative, std::string_view value) {
        require(atomic_file::write(root / relative, value), "Cannot write backup data."); record(relative);
    }
    void copy(const std::filesystem::path& source, const std::filesystem::path& relative) {
        regular(source); require(safeRelative(relative), "Invalid backup destination.");
        require(std::filesystem::file_size(source) <= budget - bytes, "Backup exceeds the 8 GiB limit.");
        std::filesystem::create_directories((root / relative).parent_path());
        require(std::filesystem::copy_file(source, root / relative), "Cannot copy a backup file."); record(relative);
        require(files.elements.back().get("sha256")->stringOr() == sha(source), "Source changed during backup; try again.");
    }
    void tree(const std::filesystem::path& source, const std::filesystem::path& relative) {
        if (!present(source)) return;
        regular(source);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
            regular(entry.path());
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().wstring();
            if (name.ends_with(L".lock") || name.find(L".tmp.") != name.npos) continue;
            copy(entry.path(), relative / std::filesystem::relative(entry.path(), source));
        }
    }
};
template<class T> T protectedState(const std::filesystem::path& config, const wchar_t* filename, const std::optional<T>& live) {
    const auto path = config / filename;
    std::optional<T> stored;
    if (present(path)) {
        const auto plain = protected_file::read(path, 128 * 1024 * 1024);
        require(plain.has_value(), "Cannot decrypt companion state; backup stopped.");
        stored = T::load(*plain); require(stored.has_value(), "Invalid companion state; backup stopped.");
    }
    return live ? *live : stored.value_or(T{});
}
AppSettings ownSettings(const AppSettings& source, const std::string& key) {
    AppSettings out;
    out.companion_look = key; out.companion = true;
    out.companion_guests = source.companion_guests; out.companion_gamification = source.companion_gamification;
    out.companion_memory = source.companion_memory; out.companion_notes = source.companion_notes;
    out.companion_semantic_memory = source.companion_semantic_memory;
    out.companion_provider = source.companion_provider; out.companion_model = source.companion_model;
    out.companion_xai_model = source.companion_xai_model; out.companion_claude_model = source.companion_claude_model;
    out.companion_ollama_model = source.companion_ollama_model;
    out.companion_host = source.companion_host; out.companion_ollama_host = source.companion_ollama_host;
    out.companion_model_info = source.companion_model_info;
    out.companion_reply_length = source.companion_reply_length;
    out.companion_typing = source.companion_typing;
    out.companion_weather = source.companion_weather;
    out.language = source.language; out.screen_transitions = source.screen_transitions;
    out.companion_voice = source.companion_voice; out.companion_voice_model = source.companion_voice_model;
    out.companion_photos = source.companion_photos;
    if (const auto at = source.companion_voice_ids.find(key); at != source.companion_voice_ids.end()) out.companion_voice_ids.insert(*at);
    if (const auto at = source.companion_costumes.find(key); at != source.companion_costumes.end()) out.companion_costumes.insert(*at);
    // Where she lives now: her own city from Settings, else the one for all of them.
    out.companion_city = source.companion_city;
    if (const auto at = source.companion_cities.find(key); at != source.companion_cities.end()) out.companion_cities.insert(*at);
    return out;
}
json::Value manifest(const std::filesystem::path& bundle) {
    regular(bundle);
    const auto parsed = json::parse(read(bundle / L"backup.json", 8 * 1024 * 1024), 8 * 1024 * 1024);
    require(parsed && parsed->get("version") && parsed->get("version")->intOr() == 1 && parsed->get("character") &&
        characterKey(parsed->get("character")->stringOr()) && parsed->get("files") && parsed->get("files")->type == json::Value::Type::array, "Invalid companion backup manifest.");
    return *parsed;
}
}

Result create(const Input& input) {
    Result result;
    try {
        const auto key = std::string(lookKey(input.look));
        require(characterKey(key), "Unknown companion.");
        require(std::filesystem::is_directory(input.destination), "Select an existing destination folder.");
        for (const auto& source : {input.config, std::filesystem::path(input.settings.companion_photo_folder), std::filesystem::path(input.settings.companion_feed_folder)})
            require(!within(input.destination, source), "Choose a destination outside the source profile and photo folders.");
        const auto id = unique();
        Staging staging{input.destination / (L".companion-backup-" + id)};
        require(std::filesystem::create_directory(staging.path), "Cannot create backup folder.");
        Writer writer{staging.path};
        auto memory = protectedState(input.config, L"companion_memory.dat", input.memory);
        auto mind_state = protectedState(input.config, L"companion_mind.dat", input.mind);
        auto cog = protectedState(input.config, L"companion_cognition.dat", input.cognition);
        const auto owned = [&](const auto& item) { return item.look.empty() || item.look == key; };
        std::erase_if(memory.notes, [&](const auto& n) { return !Memory::knows(n, key); });
        std::erase_if(memory.summaries, [&](const auto& n) { return !owned(n); });
        std::erase_if(memory.remarks, [&](const auto& n) { return !owned(n); });
        std::erase_if(memory.picks, [&](const auto& n) { return !owned(n); });
        std::erase_if(mind_state.selves, [&](const auto& pair) { return pair.first != key; });
        std::erase_if(cog.reflections, [&](const auto& n) { return !owned(n); });
        std::erase_if(cog.thoughts, [&](const auto& n) { return !owned(n); });
        std::erase_if(cog.readings, [&](const auto& n) { return !owned(n); });
        std::erase_if(cog.schemas, [&](const auto& n) { return !owned(n); });
        std::erase_if(cog.dispositions, [&](const auto& p) { return p.first != key; });
        std::erase_if(cog.peer_tension, [&](const auto& p) { return !p.first.starts_with(key + ":") && !p.first.ends_with(":" + key); });
        std::erase_if(cog.gossip, [&](const auto& n) { return n.kind == "secret" && n.other_look != key; });
        auto affinity_state = input.affinity ? *input.affinity : affinity::parse(present(input.config / L"companion_affinity.txt") ? read(input.config / L"companion_affinity.txt") : "");
        std::erase_if(affinity_state.points, [&](const auto& p) { return p.first != key; });
        std::erase_if(affinity_state.cards, [&](const auto& p) { return p.first != key; });
        std::erase_if(affinity_state.relations, [&](const auto& p) { return p.first != key; });
        auto mood_state = input.mood ? *input.mood : mood::parse(present(input.config / L"companion_mood.txt") ? read(input.config / L"companion_mood.txt") : "");
        std::erase_if(mood_state.notes, [&](const auto& n) { return n.look != key; });
        writer.text(L"portable/memory.json", memory.save());
        writer.text(L"portable/mind.json", mind_state.save());
        writer.text(L"portable/cognition.json", cog.save());
        writer.text(L"portable/affinity.txt", affinity::serialize(affinity_state));
        writer.text(L"portable/mood.txt", mood::serialize(mood_state));
        const auto authored = input.character_text.empty() ? characterManifest(key) : input.character_text;
        const auto characters = parseCharacters(authored);
        require(characters.size() == 1 && characters[0].key == key, "Cannot preserve the character definition.");
        writer.text(L"personality.txt", authored);
        auto settings = ownSettings(input.settings, key);
        require(saveAppSettings(settings, staging.path / L"portable/settings.ini"), "Cannot save profile settings."); writer.record(L"portable/settings.ini");
        std::map<uint64_t, chats::Conversation> conversations;
        const auto history = chats::folder(input.config, input.look);
        if (present(history)) for (const auto& entry : std::filesystem::directory_iterator(history)) {
            if (entry.path().extension() != L".dat") continue;
            regular(entry.path());
            const auto chat = chats::read(entry.path());
            require(chat && chat->look == input.look && chats::fileStem(chat->started) == entry.path().stem().wstring(), "Unreadable conversation; backup stopped.");
            conversations[chat->started] = *chat;
        }
        if (input.live) {
            require(input.live->look == input.look, "Snapshot belongs to another companion.");
            auto live = *input.live;
            if (const auto stored = conversations.find(live.started); stored != conversations.end() && live.photo_description.empty() && live.photo == stored->second.photo)
                live.photo_description = stored->second.photo_description;
            conversations[live.started] = std::move(live);
        }
        for (const auto& [at, chat] : conversations) {
            if (!chat.photo.empty()) require(!input.settings.companion_photo_folder.empty() && safeRelative(chat.photo) &&
                present(std::filesystem::path(input.settings.companion_photo_folder) / chat.photo), "A conversation's photo is missing; restore its source before backing up.");
            writer.text(std::filesystem::path(L"portable/chats") / (chats::fileStem(at) + L".json"), chat.save());
        }
        result.conversations = conversations.size();
        const auto wide_key = text_encoding::wide(key);
        // A short exclusive lock gives the feed a coherent snapshot, including mutable likes/comments.
        const auto feed_folder = feed::folderOf(input.config, key);
        HANDLE feed_lock = INVALID_HANDLE_VALUE;
        if (present(feed_folder)) {
            feed_lock = CreateFileW((feed_folder / L"publish.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(feed_lock != INVALID_HANDLE_VALUE, "Launchfeed is writing; try the backup again shortly.");
        }
        struct FeedLock { HANDLE value; ~FeedLock() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } lock{feed_lock};
        writer.tree(feed_folder, std::filesystem::path(L"portable/feed") / wide_key);
        if (!input.settings.companion_photo_folder.empty()) writer.tree(std::filesystem::path(input.settings.companion_photo_folder) / wide_key, std::filesystem::path(L"content/photos") / wide_key);
        if (!input.settings.companion_feed_folder.empty()) writer.tree(std::filesystem::path(input.settings.companion_feed_folder) / wide_key, std::filesystem::path(L"content/feed-source") / wide_key);
        for (const auto* file : {L"companion_week.txt", L"companion_guest.txt", L"companion_premieres.txt"})
            if (present(input.config / file)) writer.copy(input.config / file, std::filesystem::path(L"portable/aux") / file);
        const auto photo_state = input.config / L"companion/photos/state.json";
        if (present(photo_state)) writer.copy(photo_state, L"portable/photos-state.json");
        writer.copy(input.executable, L"launchme.exe");
        writer.tree(input.executable.parent_path() / L"assets", L"assets");
        writer.tree(input.executable.parent_path() / L"plugin-packages/licenses", L"licenses");
        writer.text(L"Open-copy.cmd", "@echo off\r\nsetlocal DisableDelayedExpansion\r\nstart \"\" \"%~dp0launchme.exe\" --companion-copy \"%~dp0.\"\r\n");
        writer.text(L"README.txt", "Launchme companion backup\r\n\r\nRun Open-copy.cmd to create and open an INDEPENDENT profile in this folder.\r\nYour normal launcher profile is never overwritten. Copy the WHOLE folder to another PC.\r\nThe exact executable contains the character's authored personality, costumes and sprites.\r\nPortable JSON contains private conversations and memories in readable form. Keep this folder private.\r\nAPI credentials and store accounts are not included; configure your connection in the copy.\r\nDo not delete backup.json or portable/. A profile under profiles/ is created for each Windows user/machine.\r\nThe portable snapshot stays immutable; export a new backup to preserve later progress in the copy.\r\n");
        auto root = json::object(); root.properties["version"] = json::number(1); root.properties["character"] = json::string(key);
        root.properties["files"] = writer.files; root.properties["conversations"] = json::number(static_cast<double>(result.conversations));
        root.properties["photo_source"] = json::boolean(!input.settings.companion_photo_folder.empty());
        root.properties["feed_source"] = json::boolean(!input.settings.companion_feed_folder.empty());
        require(atomic_file::write(staging.path / L"backup.json", json::serialize(root)), "Cannot finalize backup manifest.");
        const auto t = system_info::localTime(); wchar_t date[32]{}; swprintf_s(date, L"%04d%02d%02d-%02d%02d%02d", t.year,t.month,t.day,t.hour,t.minute,t.second);
        result.path = input.destination / (wide_key + L"-backup-" + date + L"-" + id.substr(0,8));
        require(MoveFileExW(staging.path.c_str(), result.path.c_str(), MOVEFILE_WRITE_THROUGH), "Cannot publish the completed backup.");
        staging.path.clear(); result.files = writer.files.elements.size(); result.bytes = writer.bytes; result.ok = true;
    } catch (const std::exception& e) { result.error = text_encoding::wideLenient(e.what()); }
    return result;
}

Result verify(const std::filesystem::path& bundle) {
    Result result; result.path = bundle;
    try {
        const auto root = manifest(bundle);
        if (const auto* count = root.get("conversations")) result.conversations = static_cast<size_t>(std::max(0, count->intOr()));
        std::set<std::wstring> paths;
        for (const auto& item : root.get("files")->elements) {
            require(item.get("path") && item.get("sha256") && item.get("bytes"), "Incomplete manifest entry.");
            const auto relative = std::filesystem::path(text_encoding::wide(item.get("path")->stringOr()));
            require(safeRelative(relative) && paths.insert(relative.generic_wstring()).second, "Invalid or duplicate backup path.");
            auto walk = bundle; for (const auto& part : relative) { walk /= part; regular(walk); }
            const auto size = std::filesystem::file_size(walk);
            require(size <= budget && result.bytes <= budget - size && ++result.files <= 20000, "Backup is too large.");
            require(item.get("bytes")->type == json::Value::Type::number && item.get("bytes")->numeric == static_cast<double>(size) && sha(walk) == item.get("sha256")->stringOr(), "Backup integrity mismatch.");
            result.bytes += size;
        }
        for (const auto* required : {L"launchme.exe", L"personality.txt", L"portable/settings.ini", L"portable/memory.json", L"portable/mind.json", L"portable/cognition.json", L"portable/affinity.txt", L"portable/mood.txt"})
            require(paths.contains(required), "Missing required backup member.");
        for (std::filesystem::recursive_directory_iterator it(bundle), end; it != end; ++it) {
            const auto relative = std::filesystem::relative(it->path(), bundle);
            if (relative == L"profiles") { it.disable_recursion_pending(); continue; } // Mutable, independent restored copies.
            regular(it->path());
            if (it->is_regular_file() && relative != L"backup.json") require(paths.contains(relative.generic_wstring()), "Unlisted file in backup.");
        }
        result.ok = true;
    } catch (const std::exception& e) { result.error = text_encoding::wideLenient(e.what()); }
    return result;
}

Result openCopy(const std::filesystem::path& bundle) {
    Result result;
    try {
        const auto root = manifest(bundle);
        const auto identity = sha(bundle / L"backup.json");
        wchar_t machine[256]{}; DWORD machine_size = 256;
        require(GetComputerNameW(machine, &machine_size), "Cannot identify this Windows machine.");
        HANDLE token = nullptr;
        require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "Cannot identify this Windows user.");
        struct Token { HANDLE handle; ~Token() { CloseHandle(handle); } } token_guard{token};
        DWORD required = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &required);
        require(required > 0 && required < 65536, "Cannot identify this Windows user.");
        std::vector<uint8_t> token_data(required);
        require(GetTokenInformation(token, TokenUser, token_data.data(), required, &required), "Cannot read this Windows identity.");
        LPWSTR sid = nullptr;
        require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(token_data.data())->User.Sid, &sid), "Cannot identify this Windows SID.");
        const auto owner = text_encoding::utf8(std::wstring(machine) + L"/" + sid); LocalFree(sid);
        const auto owner_hash = feed::fingerprint(std::span(reinterpret_cast<const uint8_t*>(owner.data()), owner.size()));
        require(!owner_hash.empty(), "Cannot identify this Windows profile.");
        const auto profiles = bundle / L"profiles";
        if (present(profiles)) regular(profiles);
        const auto profile = profiles / text_encoding::wide(owner_hash.substr(0, 20));
        if (present(profile)) {
            regular(profile);
            require(read(profile / L"backup-origin.txt", 128) == identity, "The existing profile does not belong to this backup.");
            for (const auto* file : {L"companion_memory.dat", L"companion_mind.dat", L"companion_cognition.dat"})
                require(protected_file::read(profile / file, 128 * 1024 * 1024).has_value(), "The copy's private state cannot be decrypted. Move its profiles folder aside to restore the original snapshot again.");
            result.ok = true; result.path = profile; return result;
        }
        const auto checked = verify(bundle); require(checked.ok, text_encoding::utf8(checked.error).c_str());
        const auto key = root.get("character")->stringOr();
        const auto look = lookFromKey(key); require(look.has_value(), "This executable does not contain the backed-up companion.");
        std::filesystem::create_directories(profiles);
        Staging staging{profiles / (L".pending-" + unique())}; require(std::filesystem::create_directory(staging.path), "Cannot create the copy's profile.");
        const auto memory = Memory::load(read(bundle / L"portable/memory.json"));
        const auto mind_state = mind::State::load(read(bundle / L"portable/mind.json"));
        const auto cog = cognition::State::load(read(bundle / L"portable/cognition.json"));
        require(memory && mind_state && cog, "Invalid portable personality state.");
        require(saveMemory(staging.path, *memory) && mind::saveState(staging.path, *mind_state) && cognition::saveState(staging.path, *cog), "Cannot protect restored state for this Windows user.");
        Writer writer{staging.path};
        writer.copy(bundle / L"portable/affinity.txt", L"companion_affinity.txt");
        writer.copy(bundle / L"portable/mood.txt", L"companion_mood.txt");
        writer.tree(bundle / L"portable/feed", L"companion/feed");
        writer.tree(bundle / L"content", L"content");
        writer.tree(bundle / L"portable/aux", L"");
        if (present(bundle / L"portable/photos-state.json")) writer.copy(bundle / L"portable/photos-state.json", L"companion/photos/state.json");
        const auto history = bundle / L"portable/chats";
        if (present(history)) for (const auto& entry : std::filesystem::directory_iterator(history)) {
            require(entry.path().extension() == L".json", "Unexpected conversation file.");
            const auto chat = chats::Conversation::load(read(entry.path()));
            require(chat && chat->look == *look && chats::fileStem(chat->started) == entry.path().stem().wstring(), "Invalid conversation identity.");
            require(chats::write(staging.path, *chat), "Cannot restore a conversation."); ++result.conversations;
        }
        auto settings = loadAppSettings(bundle / L"portable/settings.ini");
        settings.companion_look = key;
        if (const auto* source = root.get("photo_source"); source && source->flag) settings.companion_photo_folder = (profile / L"content/photos").wstring();
        if (const auto* source = root.get("feed_source"); source && source->flag) settings.companion_feed_folder = (profile / L"content/feed-source").wstring();
        require(saveAppSettings(settings, staging.path / L"settings.ini") && atomic_file::write(staging.path / L"backup-origin.txt", identity), "Cannot finalize restored profile.");
        require(MoveFileExW(staging.path.c_str(), profile.c_str(), MOVEFILE_WRITE_THROUGH), "Cannot activate the copy's profile.");
        staging.path.clear(); result.ok = true; result.path = profile;
    } catch (const std::exception& e) { result.error = text_encoding::wideLenient(e.what()); }
    return result;
}
}

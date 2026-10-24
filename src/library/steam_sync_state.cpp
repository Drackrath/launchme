#include "steam_sync_state.hpp"
#include <charconv>

namespace steam_sync {
namespace {
uint32_t appId(std::string_view text) {
    uint32_t id = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), id);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && id && !steamShortcutId(id) ? id : 0;
}
bool nativeCollection(std::string_view id) { return id.starts_with("user-collections.") && id != "user-collections.hidden"; }
}
std::vector<Change> pending(const LibraryState& state, uint64_t account) {
    std::vector<Change> out;
    const auto prefix = std::to_string(account) + ':';
    for (const auto& scope : state.collections.creates) {
        if (!scope.starts_with(prefix)) continue;
        const auto name = state.collections.names.find(scope);
        if (name != state.collections.names.end()) out.push_back({Kind::create, scope.substr(prefix.size()), 0, false, name->second});
    }
    for (const auto& [scope, name] : state.collections.names) {
        if (!scope.starts_with(prefix) || state.collections.creates.contains(scope)) continue;
        const auto id = scope.substr(prefix.size());
        if (nativeCollection(id) && id != "user-collections.favorite") out.push_back({Kind::name, id, 0, false, name});
    }
    for (const auto& [scope, members] : state.collections.members) {
        if (!scope.starts_with(prefix) || state.collections.creates.contains(scope)) continue;
        const auto id = scope.substr(prefix.size());
        if (!nativeCollection(id)) continue;
        for (const auto& [key, value] : members) if (key.starts_with("steam:"))
            if (const auto app = appId(std::string_view(key).substr(6))) out.push_back({Kind::member, id, app, value});
    }
    const auto hidden_prefix = "steam-user:" + std::to_string(account) + "/steam:";
    std::map<uint32_t, bool> hidden;
    for (const auto& key : state.hidden) if (key.starts_with(hidden_prefix))
        if (const auto app = appId(std::string_view(key).substr(hidden_prefix.size()))) hidden[app] = true;
    for (const auto& [key, value] : state.steam_hidden_pending) if (key.starts_with(hidden_prefix))
        if (const auto app = appId(std::string_view(key).substr(hidden_prefix.size()))) hidden[app] = value;
    for (const auto& [app, value] : hidden) out.push_back({Kind::hidden, {}, app, value});
    return out;
}
bool acknowledge(LibraryState& state, uint64_t account, const Change& c) {
    const auto scope = std::to_string(account) + ':' + c.collection;
    if (c.kind == Kind::create) {
        if (!state.collections.creates.erase(scope)) return false;
        const auto name = state.collections.names.find(scope);
        if (name != state.collections.names.end() && name->second == c.name) state.collections.names.erase(name);
        return true;
    } else if (c.kind == Kind::name) {
        const auto found = state.collections.names.find(scope);
        if (found != state.collections.names.end() && found->second == c.name) { state.collections.names.erase(found); return true; }
    } else if (c.kind == Kind::member) {
        const auto group = state.collections.members.find(scope);
        if (group == state.collections.members.end()) return false;
        const auto found = group->second.find("steam:" + std::to_string(c.app));
        if (found != group->second.end() && found->second == c.value) {
            group->second.erase(found); if (group->second.empty()) state.collections.members.erase(group); return true;
        }
    } else {
        const auto key = "steam-user:" + std::to_string(account) + "/steam:" + std::to_string(c.app);
        const auto found = state.steam_hidden_pending.find(key);
        if (found != state.steam_hidden_pending.end() && found->second != c.value) return false; // A newer edit wins.
        bool changed = state.steam_hidden_pending.erase(key) != 0;
        changed |= state.hidden.erase(key) != 0;
        return changed;
    }
    return false;
}
bool moveCollection(LibraryState& state, AppSettings& settings, uint64_t account, const std::string& local_id, const std::string& steam_id) {
    if (!account || !local_id.starts_with("local:") || !steam_id.starts_with("user-collections.uc-launchme-")) return false;
    const auto found = state.collections.names.find(local_id);
    const auto scope = std::to_string(account) + ':' + steam_id;
    if (found == state.collections.names.end() || state.collections.names.contains(scope) || state.collections.creates.contains(scope)) return false;
    const auto prefix = "steam-user:" + std::to_string(account) + '/';
    std::map<std::string, bool> members;
    if (const auto group = state.collections.members.find(local_id); group != state.collections.members.end()) {
        for (const auto& [key, value] : group->second) {
            if (!value) continue; // A new static Steam collection starts empty; old local exclusions need no write.
            if (key.starts_with("steam-user:") && !key.starts_with(prefix)) return false;
            members[key.starts_with(prefix) ? key.substr(prefix.size()) : key] = value;
        }
    }
    const auto name = found->second;
    state.collections.names[scope] = name; state.collections.names.erase(local_id);
    state.collections.members[scope] = std::move(members); state.collections.members.erase(local_id);
    state.collections.creates.insert(scope);
    if (settings.hidden_collections.erase(local_id)) settings.hidden_collections.insert(scope);
    for (auto& key : settings.collection_order) if (key == local_id) key = scope;
    if (state.bookmark && state.bookmark->account == account && state.bookmark->category == local_id) state.bookmark->category = steam_id;
    const auto old_options = std::to_string(account) + ":category:" + local_id;
    if (const auto options = state.options.find(old_options); options != state.options.end()) {
        state.options[std::to_string(account) + ":category:" + steam_id] = options->second;
        state.options.erase(options);
    }
    return true;
}
}

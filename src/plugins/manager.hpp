#pragma once
#include "manifest.hpp"
#include <memory>
#include <set>

namespace plugins {
struct Plugin {
    Manifest manifest;
    bool enabled = false;
    std::string error;
    json::Value settings = wire::object(); // Snapshot redacts secrets to an empty/nonempty marker.
    std::shared_ptr<const std::vector<uint8_t>> icon;
};
struct Action { std::string plugin; Contribution item; };
struct Snapshot {
    std::vector<Plugin> plugins;
    const Plugin* find(const std::string& id) const;
    std::vector<Action> contributions(std::string_view kind, const Game* game = nullptr) const;
};

// All methods, destruction and all DLL calls belong to one worker thread.
// A revision is immutable; registry.dat atomically selects the installed revision.
// A failed update never changes the previous registry, module or settings.
class Manager {
public:
    explicit Manager(std::filesystem::path root);
    ~Manager();
    // `crashed`: a plugin whose code crashed the previous run; it is kept off instead of loaded.
    void load(const std::string& crashed = {});
    std::string install(const std::filesystem::path& zip);
    void ensureBundled(const std::string& id, const std::filesystem::path& zip);
    void enable(const std::string& id, bool enabled);
    void setting(const std::string& id, const std::string& key, const json::Value& value);
    void remove(const std::string& id);
    json::Value invoke(const std::string& id, const std::string& method, const json::Value& params, const std::string& locale);
    Snapshot snapshot() const;
    bool initialized() const;
private:
    struct Module;
    struct Entry { Plugin plugin; std::string revision; std::shared_ptr<Module> module; };
    std::filesystem::path root_;
    std::map<std::string, Entry> entries_;
    std::set<std::string> bundled_; // Receipts survive uninstall, preserving the user's choice.
    Entry& get(const std::string& id);
    void save(const std::map<std::string, Entry>& entries);
    std::shared_ptr<Module> activate(const Entry& entry);
};
}

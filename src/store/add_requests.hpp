#pragma once
#include "../platform/atomic_file.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

// A successful protocol dispatch is a handoff, not proof of installation.
class AddRequests {
public:
    static constexpr uint64_t acknowledgement = 2;
    bool pending(uint32_t id, uint64_t now) const {
        if (dispatching_.contains(id)) return true;
        const auto found = sent_.find(id);
        return found != sent_.end() && now >= found->second && now - found->second < acknowledgement;
    }
    bool ready(uint32_t id, uint64_t now) const {
        return sent_.contains(id) && !pending(id, now);
    }
    bool begin(uint32_t id) {
        return id && !sent_.contains(id) && dispatching_.insert(id).second;
    }
    void complete(uint32_t id, uint64_t now) {
        if (dispatching_.erase(id)) sent_[id] = now;
    }
    void clear(uint32_t id) { dispatching_.erase(id); sent_.erase(id); }
    void load(const std::filesystem::path& file, uint64_t now) {
        std::ifstream input(file);
        uint32_t id = 0;
        uint64_t time = 0;
        for (int i = 0; i < 4096 && input >> id >> time; ++i)
            if (id) sent_[id] = time > now ? now : time;
    }
    void save(const std::filesystem::path& file) const {
        std::string text;
        for (const auto& [id, time] : sent_) text += std::to_string(id) + ' ' + std::to_string(time) + '\n';
        // Keep successful handoffs across restarts; they enable Steam installation, not Play.
        atomic_file::write(file, text);
    }
private:
    std::unordered_map<uint32_t, uint64_t> sent_;
    std::unordered_set<uint32_t> dispatching_;
};

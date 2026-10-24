#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// String-keyed map with least-recently-used order and a byte budget. `Size` measures a
// value in bytes. find()/at() mark an entry as used (peek()/contains() do not); after
// changing a value in place call resize() so the byte total stays exact. Nothing is
// dropped implicitly: trim() evicts, and only entries the caller does not pin.
template <class Value, class Size>
class LruCache {
public:
    explicit LruCache(size_t budget = static_cast<size_t>(-1)) : budget_(budget) {}

    size_t budget() const { return budget_; }
    size_t bytes() const { return bytes_; }
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    bool contains(const std::string& key) const { return entries_.contains(key); }

    Value* find(const std::string& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) return nullptr;
        touch(found->second);
        return &found->second.value;
    }
    const Value* peek(const std::string& key) const {
        const auto found = entries_.find(key);
        return found == entries_.end() ? nullptr : &found->second.value;
    }
    // Inserts a default value when missing; either way the entry becomes the most recent.
    Value& at(const std::string& key) {
        auto [found, inserted] = entries_.try_emplace(key);
        if (inserted) {
            order_.push_front(key);
            found->second.order = order_.begin();
            found->second.bytes = Size{}(found->second.value);
            bytes_ += found->second.bytes;
        } else touch(found->second);
        return found->second.value;
    }
    void resize(const std::string& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) return;
        bytes_ -= found->second.bytes;
        found->second.bytes = Size{}(found->second.value);
        bytes_ += found->second.bytes;
    }
    bool erase(const std::string& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) return false;
        bytes_ -= found->second.bytes;
        order_.erase(found->second.order);
        entries_.erase(found);
        return true;
    }
    void clear() { entries_.clear(); order_.clear(); bytes_ = 0; }

    // Over budget: drops the least recently used unpinned entries until the total is at
    // most 7/8 of the budget (hysteresis, so a full cache is not trimmed on every insert).
    // Returns the dropped keys, oldest first.
    template <class Pinned>
    std::vector<std::string> trim(Pinned&& pinned) {
        std::vector<std::string> dropped;
        if (bytes_ <= budget_) return dropped;
        const size_t target = budget_ - budget_ / 8;
        for (auto position = order_.end(); position != order_.begin() && bytes_ > target;) {
            --position;
            if (pinned(std::as_const(*position))) continue;
            const auto found = entries_.find(*position);
            bytes_ -= found->second.bytes;
            dropped.push_back(std::move(*position));
            entries_.erase(found);
            position = order_.erase(position);
        }
        return dropped;
    }

private:
    struct Entry {
        Value value{};
        size_t bytes = 0;
        typename std::list<std::string>::iterator order;
    };
    void touch(Entry& entry) { order_.splice(order_.begin(), order_, entry.order); }

    std::list<std::string> order_; // Front = most recently used.
    std::unordered_map<std::string, Entry> entries_;
    size_t bytes_ = 0;
    size_t budget_;
};

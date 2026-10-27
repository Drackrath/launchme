#pragma once
#include "companion_embeddings.hpp"
#include "companion_cognition.hpp"
#include <functional>

namespace companion::semantic {
struct Document {
    std::string key; // Exact UTF-8 content digest, independent of ownership; never grants access.
    std::wstring text;
    uint64_t at = 0;
    float importance = 0;
    mind::Status evidence = mind::Status::remember;
    std::string owner;
    std::vector<Memory::Knower> known;
    bool shared = false;
    bool operator==(const Document&) const = default;
};
// Immutable projection only: indexing never changes, summarizes or deletes the primary archives.
std::vector<Document> documents(const Memory&, const cognition::State&, const mind::State&);
bool accessible(const Document&, std::string_view look);
struct Hit { Document document; float similarity = 0, score = 0; bool semantic = false; };
std::vector<mind::Candidate> candidates(const std::vector<Hit>&, std::string_view look, bool english);
struct Progress {
    enum class State { loading, indexing, ready, unavailable, stopped } state = State::loading;
    size_t indexed = 0, total = 0;
    std::string error; // Generic diagnostic; no user text.
};
class Service {
public:
    using Factory = std::function<std::unique_ptr<Embedder>()>;
    Service(std::filesystem::path cache, Factory factory);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void replace(std::vector<Document> documents);
    // Bounded query; during migration, available vectors and lexical matches are immediately usable.
    std::vector<Hit> search(std::string_view look, std::wstring_view query, uint64_t now, size_t limit = 6);
    std::vector<Hit> searchMany(std::string_view look, const std::vector<std::wstring>& queries, uint64_t now, size_t limit = 6);
    Progress progress() const;
    void stop();
    void clear(); // Explicit user reset: removes only the derived cache, after stopping its worker.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

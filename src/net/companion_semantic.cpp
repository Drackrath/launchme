#include "companion_semantic.hpp"
#include "companion_chats.hpp"
#include "../platform/protected_file.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace companion::semantic {
namespace {
constexpr size_t max_vectors = 30000, cache_bytes = 64 * 1024 * 1024;
using Vectors = std::unordered_map<std::string, Vector>;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Statement {
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db, const char* sql) { require(sqlite3_prepare_v2(db, sql, -1, &value, nullptr) == SQLITE_OK, "Invalid vector database schema."); }
    ~Statement() { sqlite3_finalize(value); }
    void text(int index, const std::string& text) { require(sqlite3_bind_text(value, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT) == SQLITE_OK, "Cannot bind vector key."); }
    void done() { require(sqlite3_step(value) == SQLITE_DONE, "Cannot update vector database."); sqlite3_reset(value); sqlite3_clear_bindings(value); }
};
class Database {
    sqlite3* db_ = nullptr;
    std::string model_;
    void sql(const char* text) { require(sqlite3_exec(db_, text, nullptr, nullptr, nullptr) == SQLITE_OK, "Cannot initialize vector database."); }
    void fresh() {
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        require(sqlite3_open(":memory:", &db_) == SQLITE_OK, "Cannot open vector database.");
        sql("PRAGMA trusted_schema=OFF; PRAGMA secure_delete=ON; CREATE TABLE meta(model TEXT NOT NULL); CREATE TABLE vectors(id TEXT PRIMARY KEY, embedding BLOB NOT NULL) WITHOUT ROWID;");
        Statement insert(db_, "INSERT INTO meta VALUES(?)"); insert.text(1, model_); insert.done();
    }
public:
    Database(const std::filesystem::path& path, std::string model) : model_(std::move(model)) {
        fresh();
        if (const auto bytes = protected_file::read(path, cache_bytes); bytes && !bytes->empty()) {
            auto* data = static_cast<unsigned char*>(sqlite3_malloc64(bytes->size()));
            require(data != nullptr, "Cannot allocate vector database.");
            std::memcpy(data, bytes->data(), bytes->size());
            const auto size = static_cast<sqlite3_int64>(bytes->size());
            // SQLite owns the allocation even if deserialize fails with FREEONCLOSE.
            if (sqlite3_deserialize(db_, "main", data, size, size, SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE) != SQLITE_OK) { fresh(); return; }
            try {
                sql("PRAGMA trusted_schema=OFF; PRAGMA secure_delete=ON;");
                Statement meta(db_, "SELECT model FROM meta");
                require(sqlite3_step(meta.value) == SQLITE_ROW, "Missing vector database identity.");
                const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(meta.value, 0));
                require(id && model_ == id, "Stale vector model identity.");
                Statement rows(db_, "SELECT id, embedding FROM vectors LIMIT 1");
                const int status = sqlite3_step(rows.value);
                require(status == SQLITE_ROW || status == SQLITE_DONE, "Unreadable vector database.");
            } catch (...) { fresh(); } // Derived cache only; primary memory is never opened here.
        }
    }
    ~Database() { if (db_) sqlite3_close(db_); }
    Vectors load() {
        Vectors result;
        Statement rows(db_, "SELECT id, embedding FROM vectors");
        int status = SQLITE_OK;
        while ((status = sqlite3_step(rows.value)) == SQLITE_ROW) {
            if (result.size() >= max_vectors) break;
            const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(rows.value, 0));
            if (!id || std::strlen(id) != 64 || sqlite3_column_bytes(rows.value, 1) != sizeof(Vector)) continue;
            Vector vector;
            std::memcpy(vector.data(), sqlite3_column_blob(rows.value, 1), sizeof(vector));
            if (normalize(vector)) result.emplace(id, vector);
        }
        require(status == SQLITE_DONE || status == SQLITE_ROW, "Cannot read cached vectors.");
        return result;
    }
    void put(const std::string& id, const Vector& vector) {
        Statement insert(db_, "INSERT OR REPLACE INTO vectors VALUES(?,?)"); insert.text(1, id);
        require(sqlite3_bind_blob(insert.value, 2, vector.data(), sizeof(vector), SQLITE_TRANSIENT) == SQLITE_OK, "Cannot bind vector."); insert.done();
    }
    void erase(const std::string& id) { Statement remove(db_, "DELETE FROM vectors WHERE id=?"); remove.text(1, id); remove.done(); }
    bool save(const std::filesystem::path& path) {
        sqlite3_int64 size = 0;
        auto* bytes = sqlite3_serialize(db_, "main", &size, 0);
        if (!bytes) return false;
        const bool result = size > 0 && size < cache_bytes - 4096 && protected_file::write(path,
            std::string_view(reinterpret_cast<const char*>(bytes), static_cast<size_t>(size)), L"Launchme companion vector cache");
        sqlite3_free(bytes);
        return result;
    }
};
}

bool accessible(const Document& document, std::string_view look) {
    if (look.empty()) return false; // No implicit all-character query.
    return document.shared || document.owner == look || std::any_of(document.known.begin(), document.known.end(),
        [&](const auto& knower) { return knower.look == look; });
}
std::vector<Document> documents(const Memory& memory, const cognition::State& cognition, const mind::State& mind) {
    std::vector<Document> out;
    const auto add = [&](std::wstring text, uint64_t at, std::string owner, float importance, mind::Status evidence,
                         const std::vector<Memory::Knower>& known = std::vector<Memory::Knower>{}) {
        if (text.empty()) return;
        Document d;
        d.text = std::move(text); d.key = contentKey(d.text); d.at = at; d.owner = std::move(owner);
        d.importance = std::clamp(importance, 0.0f, 1.0f); d.evidence = evidence; d.known = known;
        d.shared = d.owner.empty() && known.empty();
        out.push_back(std::move(d));
    };
    for (const auto& n : memory.notes) {
        const float confidence = n.confidence > 0 ? n.confidence : 0.8f;
        add(n.text, n.at, n.look, n.important ? 1.0f : 0.3f * confidence + (n.topic == "place" ? 0.3f : 0.0f) + 0.03f * std::log2(static_cast<float>(std::clamp(n.confirmations, 1u, 64u))),
            n.source == "inferred" ? mind::Status::guess : n.source == "observed" ? mind::Status::know : mind::Status::heard, n.known);
    }
    for (const auto& s : memory.summaries) add(s.text, s.at, s.look, s.important ? 1.0f : 0.4f, mind::Status::remember);
    for (const auto& r : cognition.reflections) add(r.text, r.at, r.look, 0.4f, mind::Status::guess);
    for (const auto& s : cognition.schemas) add(s.text, s.at, s.look, 0.5f, mind::Status::guess);
    for (const auto& [look, self] : mind.selves) {
        for (const auto& c : self.learned_concepts) add(c.term + L": " + c.meaning, c.at, look, c.mastery * 0.5f, mind::Status::heard);
        for (const auto& c : self.key_memories) add(c.text, c.at, look, 1.0f, mind::Status::remember);
        for (const auto& c : self.shared_idioms) add(c.text, c.at, look, 0.6f, mind::Status::remember);
        for (const auto& c : self.player_world) add(c.name + L": " + c.relation, c.at, look, 0.6f, mind::Status::heard);
    }
    std::stable_sort(out.begin(), out.end(), [](const Document& a, const Document& b) {
        return a.importance != b.importance ? a.importance > b.importance : a.at > b.at;
    });
    return out;
}
std::vector<mind::Candidate> candidates(const std::vector<Hit>& hits, std::string_view look, bool english) {
    std::vector<mind::Candidate> out;
    for (const auto& hit : hits) {
        if (!hit.semantic || !accessible(hit.document, look)) continue;
        auto evidence = hit.document.evidence;
        for (const auto& knower : hit.document.known) if (knower.look == look && knower.hearsay) evidence = mind::Status::gossip;
        out.push_back({"semantic_memory", (english ? L"Relevant earlier memory: " : L"Pasujące wcześniejsze wspomnienie: ") + hit.document.text,
            std::min(0.8f, 0.55f + 0.2f * hit.similarity + 0.08f * hit.document.importance), evidence});
        if (out.size() == 2) break; // Same source and existing workspace budget, no prompt expansion.
    }
    return out;
}

struct Service::Impl {
    std::filesystem::path path;
    Factory factory;
    mutable std::mutex state_mutex;
    std::mutex stop_mutex;
    std::timed_mutex engine_mutex;
    std::condition_variable_any wake;
    std::vector<Document> current;
    Vectors vectors;
    std::unique_ptr<Embedder> engine;
    std::atomic_bool stopped{false};
    uint64_t revision = 0; // Do not reconcile an empty snapshot before the caller supplies its archive.
    Progress status;
    std::jthread worker;
    Impl(std::filesystem::path cache, Factory create) : path(std::move(cache)), factory(std::move(create)) {
        worker = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    void run(std::stop_token stop) {
        try {
            auto loaded_engine = factory();
            require(loaded_engine != nullptr, "Embedding model unavailable.");
            Database database(path, loaded_engine->identity());
            auto loaded_vectors = database.load();
            { std::lock_guard lock(engine_mutex); engine = std::move(loaded_engine); }
            { std::lock_guard lock(state_mutex); vectors = std::move(loaded_vectors); }
            uint64_t processed = 0;
            while (!stop.stop_requested()) {
                std::vector<Document> snapshot;
                uint64_t generation;
                {
                    std::unique_lock lock(state_mutex);
                    wake.wait(lock, stop, [&] { return processed != revision; });
                    if (stop.stop_requested()) break;
                    snapshot = current; generation = revision;
                    status = {Progress::State::indexing, 0, snapshot.size(), {}};
                }
                std::set<std::string> live;
                for (const auto& d : snapshot) if (live.size() < max_vectors) live.insert(d.key);
                std::vector<std::string> removed;
                {
                    std::lock_guard lock(state_mutex);
                    for (const auto& [key, value] : vectors) if (!live.contains(key)) removed.push_back(key);
                    for (const auto& key : removed) vectors.erase(key);
                }
                for (const auto& key : removed) database.erase(key);
                // Persist deletions before lengthy incremental indexing; source memory stays untouched.
                if (!removed.empty()) database.save(path);
                size_t pending = 0;
                for (const auto& document : snapshot) {
                    bool cached;
                    {
                        std::lock_guard lock(state_mutex);
                        if (generation != revision || stop.stop_requested()) break;
                        cached = vectors.contains(document.key);
                    }
                    if (!live.contains(document.key)) continue;
                    if (!cached) {
                        Vector vector;
                        { std::lock_guard lock(engine_mutex); vector = engine->encode(document.text, false); }
                        require(normalize(vector), "Embedding model returned an invalid vector.");
                        {
                            std::lock_guard lock(state_mutex);
                            if (generation != revision || stop.stop_requested()) break;
                            vectors[document.key] = vector;
                        }
                        database.put(document.key, vector);
                        ++pending;
                    }
                    { std::lock_guard lock(state_mutex); if (generation == revision) ++status.indexed; }
                    if (pending >= 64 && !stop.stop_requested()) { database.save(path); pending = 0; }
                    std::this_thread::yield();
                }
                if (!stop.stop_requested()) {
                    const bool saved = database.save(path);
                    std::lock_guard lock(state_mutex);
                    processed = generation;
                    if (generation == revision) {
                        status.state = Progress::State::ready;
                        if (!saved) status.error = "Vector cache could not be saved; retrieval remains available in memory.";
                    }
                }
            }
        } catch (...) {
            std::lock_guard lock(state_mutex);
            status.state = Progress::State::unavailable;
            status.error = "Local semantic memory unavailable. Check the packaged model and runtime; keyword memory remains active.";
        }
    }
};
Service::Service(std::filesystem::path path, Factory factory) : impl_(std::make_unique<Impl>(std::move(path), std::move(factory))) {}
Service::~Service() { stop(); }
void Service::replace(std::vector<Document> documents) {
    std::lock_guard lock(impl_->state_mutex);
    if (impl_->stopped || (impl_->revision && impl_->current == documents)) return;
    impl_->current = std::move(documents);
    ++impl_->revision;
    if (impl_->status.state != Progress::State::unavailable) impl_->status.state = Progress::State::indexing;
    impl_->status.total = impl_->current.size(); impl_->status.indexed = 0;
    impl_->wake.notify_all();
}
Progress Service::progress() const { std::lock_guard lock(impl_->state_mutex); return impl_->status; }
void Service::stop() {
    std::lock_guard shutdown(impl_->stop_mutex);
    impl_->stopped = true;
    impl_->worker.request_stop(); impl_->wake.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard lock(impl_->state_mutex); impl_->status.state = Progress::State::stopped;
}
void Service::clear() {
    stop();
    std::error_code ec;
    std::filesystem::remove(impl_->path, ec);
}
std::vector<Hit> Service::searchMany(std::string_view look, const std::vector<std::wstring>& queries, uint64_t now, size_t limit) {
    std::vector<Hit> all;
    std::set<std::wstring> used;
    for (size_t q = 0; q < queries.size() && q < 3; ++q) {
        if (!used.insert(queries[q]).second) continue;
        auto found = search(look, queries[q], now, 12);
        for (size_t i = 0; i < found.size(); ++i) {
            const float score = (q == 0 ? 1.2f : 1.0f) / (20.0f + static_cast<float>(i));
            const auto old = std::find_if(all.begin(), all.end(), [&](const Hit& h) { return h.document.key == found[i].document.key; });
            if (old == all.end()) { found[i].score = score; all.push_back(std::move(found[i])); }
            else { old->score += score; old->semantic = old->semantic || found[i].semantic; old->similarity = std::max(old->similarity, found[i].similarity); }
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    {
        std::lock_guard lock(impl_->state_mutex);
        if (impl_->stopped) return {};
        std::erase_if(all, [&](const Hit& hit) { return std::none_of(impl_->current.begin(), impl_->current.end(), [&](const Document& d) {
            return d.key == hit.document.key && accessible(d, look);
        }); });
    }
    if (all.size() > std::min<size_t>(12, limit)) all.resize(std::min<size_t>(12, limit));
    return all;
}
std::vector<Hit> Service::search(std::string_view look, std::wstring_view query, uint64_t now, size_t limit) {
    if (impl_->stopped || query.empty() || look.empty() || !limit) return {};
    const auto words = chats::searchWords(query);
    std::optional<Vector> embedded;
    {
        std::unique_lock lock(impl_->engine_mutex, std::defer_lock);
        if (lock.try_lock_for(std::chrono::milliseconds(100)) && impl_->engine && !impl_->stopped) {
            try { embedded = impl_->engine->encode(query, true); if (!normalize(*embedded)) embedded.reset(); }
            catch (...) { embedded.reset(); }
        }
    }
    struct Ranked { Hit hit; int lexical; };
    std::vector<Ranked> ranked;
    {
        std::lock_guard lock(impl_->state_mutex);
        if (impl_->stopped) return {};
        // Current snapshot is read AFTER inference, so a deletion/revocation during inference wins.
        for (const auto& d : impl_->current) {
            if (!accessible(d, look)) continue;
            const int lexical = chats::matches(words, d.text);
            float similarity = 0;
            if (embedded) if (const auto at = impl_->vectors.find(d.key); at != impl_->vectors.end()) similarity = cosine(*embedded, at->second);
            const bool semantic = similarity >= similarity_floor;
            if (lexical || semantic) ranked.push_back({{d, similarity, 0, semantic}, lexical});
        }
    }
    std::vector<size_t> lexical, semantic;
    for (size_t i = 0; i < ranked.size(); ++i) {
        if (ranked[i].lexical) lexical.push_back(i);
        if (ranked[i].hit.semantic) semantic.push_back(i);
    }
    const auto order = [&](const auto& a, const auto& b) { return ranked[a].hit.document.at > ranked[b].hit.document.at; };
    std::stable_sort(lexical.begin(), lexical.end(), [&](size_t a, size_t b) { return ranked[a].lexical != ranked[b].lexical ? ranked[a].lexical > ranked[b].lexical : order(a, b); });
    std::stable_sort(semantic.begin(), semantic.end(), [&](size_t a, size_t b) { return ranked[a].hit.similarity != ranked[b].hit.similarity ? ranked[a].hit.similarity > ranked[b].hit.similarity : order(a, b); });
    for (const auto& list : {lexical, semantic}) for (size_t i = 0; i < list.size(); ++i) ranked[list[i]].hit.score += 1.0f / (20.0f + static_cast<float>(i));
    for (auto& r : ranked) {
        const auto& d = r.hit.document;
        const double age = now > d.at ? static_cast<double>(now - d.at) / 86400 : 0;
        r.hit.score += 0.003f * d.importance + static_cast<float>(0.001 / (1.0 + age / 90));
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.hit.score > b.hit.score; });
    std::set<std::string> selected;
    std::vector<Hit> result;
    for (auto& r : ranked) {
        if (selected.insert(r.hit.document.key).second) result.push_back(std::move(r.hit));
        if (result.size() >= std::min<size_t>(limit, 12)) break;
    }
    return result;
}
}

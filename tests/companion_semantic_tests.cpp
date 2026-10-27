#include "companion_fixture.hpp"
#include "../src/net/companion_semantic.hpp"
#include "../src/platform/protected_file.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace s = companion::semantic;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() / (L"launchme-semantic-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    ~Temp() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
void wait(s::Service& service) {
    const auto end = GetTickCount64() + 90000;
    while (GetTickCount64() < end) {
        const auto state = service.progress();
        check(state.state != s::Progress::State::unavailable, state.error.c_str());
        if (state.state == s::Progress::State::ready) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Indexing did not finish.");
}
struct Fake : s::Embedder {
    std::shared_ptr<std::atomic<int>> calls;
    std::string version;
    Fake(std::shared_ptr<std::atomic<int>> c, std::string v) : calls(std::move(c)), version(std::move(v)) {}
    std::string identity() const override { return version; }
    s::Vector encode(std::wstring_view text, bool query) override {
        if (!query) ++*calls;
        s::Vector vector{};
        vector[text.find(L"unrelated") == text.npos ? 0 : 1] = 1;
        return vector;
    }
};
void unit() {
    Temp temp;
    const auto cache = temp.path / L"vectors.dat";
    companion::Memory memory;
    memory.note(L"Original private memory", 100, "hana");
    memory.notes[0].secret = true;
    memory.notes[0].confirmations = 5;
    memory.note(L"An unrelated topic", 101, "rin");
    const auto before = memory.save();
    check(companion::saveMemory(temp.path, memory), "Save original memory");
    const auto original = protected_file::read(temp.path / L"companion_memory.dat", 32 * 1024 * 1024);
    const auto calls = std::make_shared<std::atomic<int>>(0);
    const auto factory = [calls] { return std::make_unique<Fake>(calls, "fixture-v1"); };
    {
        s::Service service(cache, factory);
        service.replace(s::documents(memory, {}, {})); wait(service);
        check(calls->load() == 2, "Initial index should encode both documents");
        const auto found = service.search("hana", L"paraphrase", 200);
        check(found.size() == 1 && found[0].semantic && found[0].document.text == memory.notes[0].text, "Semantic lookup");
        check(service.search("rin", L"paraphrase", 200).empty(), "Private memory must not leak to another character");
        check(service.search("", L"paraphrase", 200).empty(), "Empty owner must not bypass filtering");
        check(memory.save() == before && protected_file::read(temp.path / L"companion_memory.dat", 32 * 1024 * 1024) == original, "Indexing changed primary memory");
        service.replace(s::documents(memory, {}, {})); wait(service);
        check(calls->load() == 2, "Unchanged notes must not be re-encoded");
        memory.notes[0].known.push_back({"rin", true, "hana"});
        service.replace(s::documents(memory, {}, {})); wait(service);
        const auto gossip = s::candidates(service.search("rin", L"paraphrase", 200), "rin", false);
        check(gossip.size() == 1 && gossip.front().status == companion::mind::Status::gossip, "Shared knowledge must retain its hearsay status");
        check(calls->load() == 2, "Access metadata change must not recompute embeddings");
        memory.notes[0].known.clear();
        service.replace(s::documents(memory, {}, {})); wait(service);
        check(service.search("rin", L"paraphrase", 200).empty(), "Revoked access must not leak a cached vector");
    }
    check(std::filesystem::exists(cache), "Encrypted index was not saved");
    const auto db = protected_file::read(cache, 64 * 1024 * 1024);
    check(db && db->starts_with("SQLite format 3") && db->find("Original private memory") == db->npos, "Cache should be encrypted SQLite with no raw text");
    {
        s::Service service(cache, factory);
        Sleep(100); // The model/cache may finish loading before the initial archive snapshot arrives.
        service.replace(s::documents(memory, {}, {})); wait(service);
        check(calls->load() == 2, "Restart should reuse embeddings");
        memory.notes[0].text = L"Updated private memory";
        service.replace(s::documents(memory, {}, {})); wait(service);
        check(calls->load() == 3, "Edit should encode only changed text");
        memory.notes.erase(memory.notes.begin());
        service.replace(s::documents(memory, {}, {}));
        check(service.search("hana", L"paraphrase", 200).empty(), "Deleted memory must vanish immediately");
        wait(service);
        service.clear();
        check(!std::filesystem::exists(cache), "Reset must remove derived cache");
        check(protected_file::read(temp.path / L"companion_memory.dat", 32 * 1024 * 1024) == original, "Reset changed original memory");
    }
    check(atomic_file::write(cache, "corrupt derived cache"), "Corrupt-cache fixture");
    {
        s::Service service(cache, factory); service.replace(s::documents(memory, {}, {})); wait(service);
        check(service.progress().indexed == 1, "Corrupt cache must rebuild");
    }
    const auto previous = calls->load();
    {
        s::Service service(cache, [calls] { return std::make_unique<Fake>(calls, "fixture-v2"); });
        service.replace(s::documents(memory, {}, {})); wait(service);
        check(calls->load() == previous + 1, "New model identity must invalidate old vectors");
    }
    {
        s::Service service(temp.path / L"missing.dat", []() -> std::unique_ptr<s::Embedder> { throw std::runtime_error("missing"); });
        service.replace(s::documents(memory, {}, {}));
        for (int i = 0; i < 500 && service.progress().state != s::Progress::State::unavailable; ++i) Sleep(2);
        check(service.search("rin", L"unrelated topic", 200).size() == 1, "Missing model must retain lexical retrieval");
    }
    companion::Memory legacy;
    for (int i = 0; i < 600; ++i) {
        companion::Memory::Entry e;
        e.text = L"Legacy memory " + std::to_wstring(i); e.at = 1 + i; e.look = "hana";
        e.id = "n" + std::to_string(i); e.confirmations = 3;
        legacy.notes.push_back(std::move(e));
    }
    const auto roundtrip = companion::Memory::load(legacy.save());
    check(roundtrip && roundtrip->save() == legacy.save(), "Existing expanded memories and evidence counts must survive");
    // A source edit/revocation while query inference is running wins over its old vector.
    {
        struct Gate { std::promise<void> entered, release; std::shared_future<void> go = release.get_future().share(); };
        const auto gate = std::make_shared<Gate>();
        struct Blocking : s::Embedder {
            std::shared_ptr<Gate> gate;
            explicit Blocking(std::shared_ptr<Gate> g) : gate(std::move(g)) {}
            std::string identity() const override { return "blocking-fixture"; }
            s::Vector encode(std::wstring_view, bool query) override {
                if (query) { gate->entered.set_value(); gate->go.wait(); }
                s::Vector result{}; result[0] = 1; return result;
            }
        };
        s::Service service(temp.path / L"racing.dat", [gate] { return std::make_unique<Blocking>(gate); });
        service.replace(s::documents(memory, {}, {})); wait(service);
        auto result = std::async(std::launch::async, [&] { return service.search("rin", L"paraphrase", 200); });
        const bool entered = gate->entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        service.replace({}); gate->release.set_value();
        check(entered && result.get().empty(), "In-flight retrieval resurrected removed memory");
        service.clear();
        check(!std::filesystem::exists(temp.path / L"racing.dat"), "Stopped worker recreated the cleared index");
    }
}
void model(const std::filesystem::path& directory) {
    auto engine = s::localEmbedder(directory);
    const std::vector<std::wstring> passages{
        L"Po stresującym dniu w pracy potrzebuje ciszy i odpoczynku.",
        L"Gracz hoduje dwa koty i regularnie chodzi z nimi do weterynarza.",
        L"Nie lubi gier grozy, bo boi się nagłych straszaków.",
        L"Planuje zmianę pracy i przygotowuje CV.",
        L"Uwielbia wspinaczkę górską i długie piesze wędrówki.",
        L"Jego ulubionym posiłkiem jest pizza z pieczarkami."};
    const std::vector<std::wstring> queries{
        L"Cały dzień w biurze mnie wyssał, marzę o świętym spokoju.",
        L"Muszę umówić wizytę, żeby zaszczepić moje zwierzęta domowe.",
        L"Odpuszczę ten survival horror, jumpscare'y są dla mnie za mocne.",
        L"Jutro rozmowa rekrutacyjna, trzymaj kciuki.",
        L"Weekend spędzę na szlaku w Tatrach.",
        L"I would love to order a mushroom pizza for dinner."};
    std::vector<s::Vector> vectors;
    for (const auto& passage : passages) vectors.push_back(engine->encode(passage, false));
    size_t correct = 0, admitted = 0;
    for (size_t i = 0; i < queries.size(); ++i) {
        const auto query = engine->encode(queries[i], true);
        size_t best = 0; float maximum = -1;
        std::cout << "query " << i << ':';
        for (size_t j = 0; j < vectors.size(); ++j) {
            const auto similarity = s::cosine(query, vectors[j]); std::cout << ' ' << similarity;
            if (similarity > maximum) { maximum = similarity; best = j; }
        }
        std::cout << '\n';
        if (best == i) ++correct;
        if (maximum >= s::similarity_floor) ++admitted;
    }
    check(correct == queries.size(), "Real multilingual semantic retrieval failed");
    check(admitted >= 5, "Too many representative semantic matches were rejected");
    const auto tokens = s::localTokens(directory, L"Żółw 🐢", true);
    check(tokens.front() == 0 && tokens.back() == 2 && tokens.size() > 4, "Tokenizer must frame Unicode text");
    Temp temp;
    s::Service service(temp.path / L"vectors.dat", [directory] { return s::localEmbedder(directory); });
    companion::Memory memory;
    for (const auto& text : passages) memory.note(text, 100, "hana");
    service.replace(s::documents(memory, {}, {})); wait(service);
    const auto found = service.search("hana", queries[1], 200);
    check(!found.empty() && found.front().semantic && found.front().document.text == passages[1], "Real model through persistent hybrid service");
    check(!s::candidates(found, "hana", false).empty(), "Semantic findings must reach the prompt workspace");
    check(service.search("hana", L"Jaka jest odległość Ziemi od Marsa?", 200).empty(), "Unrelated astronomy query should not recall private memories");
    check(s::localTokens(directory, std::wstring(10000, L'ż'), true).size() <= 128, "Long input must retain the encoder token budget");
}
}
int main(int argc, char** argv) {
    const companion_fixture::Cast cast;
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--model") {
            wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
            model(std::filesystem::path(exe).parent_path() / L"assets/companion-embeddings");
        } else unit();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}

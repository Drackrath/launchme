#include "companion_fixture.hpp"
#include "../src/net/companion_chats.hpp"
#include "../src/net/companion_cognition.hpp"
#include "../src/net/companion_memory.hpp"
#include "../src/net/companion_mind.hpp"
#include <algorithm>
#include <iostream>
#include <set>

namespace {
constexpr uint64_t now = 1'800'000'000, day = 86400;
using companion::Memory;
bool contains(std::wstring_view text, std::wstring_view part) { return text.find(part) != text.npos; }
bool has(const std::vector<Memory::Entry>& entries, std::wstring_view text) {
    return std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.text == text; });
}
Memory::Entry note(size_t i, std::string look = "hana") {
    Memory::Entry e;
    e.at = now - 10000 + i;
    e.text = L"An ordinary memory number " + std::to_wstring(i);
    e.look = std::move(look);
    e.id = "n" + std::to_string(i);
    return e;
}
int weightedRetention() {
    Memory memory;
    for (size_t i = 0; i < companion::max_notes_capacity; ++i) memory.notes.push_back(note(i));
    // More than the old 16 pinned slots: all important memories outrank recent ordinary entries.
    for (size_t i = 0; i < 24; ++i) {
        memory.notes[i].important = true;
        memory.notes[i].at = now - 365 * day;
    }
    memory.notes[24].confirmations = 32;
    memory.notes[24].at = now - 365 * day;
    const auto original_size = memory.notes.size();
    Memory::Facts guess; guess.look = "hana"; guess.source = "inferred"; guess.confidence = 0.1f;
    if (memory.note(L"Perhaps enjoys mountaineering alone", now, guess) || memory.notes.size() != original_size) return 1;
    Memory::Facts fact; fact.look = "hana"; fact.important = true;
    if (!memory.note(L"Our first shared celebration", now, fact) || memory.notes.size() != original_size) return 2;
    for (size_t i = 0; i <= 24; ++i) if (!has(memory.notes, note(i).text)) return 3;
    if (has(memory.notes, note(25).text)) return 4; // Oldest ordinary evidence lost, not the oldest memory.
    // A snapshot exceeding the capacity is pruned by the same policy on load.
    auto weak = note(6000); weak.text = L"A weak speculative conclusion"; weak.source = "inferred"; weak.confidence = 0.05f;
    memory.notes.push_back(weak);
    const auto loaded = Memory::load(memory.save());
    if (!loaded || loaded->notes.size() != original_size || has(loaded->notes, weak.text) || !loaded->notes.front().important) return 5;
    return 0;
}
int confirmations() {
    Memory memory;
    Memory::Facts fact; fact.look = "hana";
    memory.note(L"Enjoys cooperative puzzle games", now, fact);
    memory.note(L"Enjoys cooperative puzzle games", now, fact);
    if (memory.notes.front().confirmations != 1) return 10;
    memory.note(L"Enjoys cooperative puzzle games", now + day, fact);
    memory.note(L"Enjoys cooperative puzzle games", now + 2 * day, fact);
    if (memory.notes.front().confirmations != 3) return 11;
    const auto saved = memory.save();
    const auto loaded = Memory::load(saved);
    if (!loaded || loaded->save() != saved) return 12;
    auto changed = *loaded;
    changed.note(L"Never enjoys cooperative puzzle games", now + 3 * day, fact);
    if (changed.notes.size() != 1 || changed.notes.front().confirmations != 1 || !contains(changed.notes.front().text, L"Never")) return 13;
    changed.notes.front().confirmations = 6;
    if (!changed.revise(changed.notes.front().id, L"Prefers solitary adventures", {}, "hana", now + 4 * day) || changed.notes.front().confirmations != 1) return 14;
    Memory guessed;
    fact.source = "inferred"; fact.confidence = 0.3f;
    for (int i = 0; i < 5; ++i) guessed.note(L"Possibly enjoys rainy walks", now + i, fact);
    if (guessed.notes.front().confirmations != 1) return 15;
    fact.source = "told";
    guessed.note(L"Possibly enjoys rainy walks", now + day, fact);
    if (guessed.notes.front().confirmations != 1 || guessed.notes.front().source == "inferred") return 16;
    const auto legacy = Memory::load(R"({"version":1,"notes":[{"at":1,"text":"An old note","id":"n1"}]})");
    if (!legacy || legacy->notes.front().confirmations != 1) return 17;
    const auto bounded = Memory::load(R"({"version":1,"notes":[{"text":"negative count","confirmations":-20},{"text":"large count","confirmations":999999999999}]})");
    if (!bounded || bounded->notes[0].confirmations != 1 || bounded->notes[1].confirmations != 64) return 18;
    return 0;
}
int recall() {
    Memory memory;
    auto core = note(0); core.text = L"A foundational birthday tradition"; core.important = true; core.at = now - 300 * day;
    memory.notes.push_back(core);
    auto private_note = core; private_note.text = L"Rin alone knows the lighthouse secret"; private_note.look = "rin"; private_note.id = "private";
    memory.notes.push_back(private_note);
    auto repeated = note(1); repeated.text = L"A repeatedly confirmed preference"; repeated.confirmations = 5; repeated.at = core.at;
    memory.notes.push_back(repeated);
    for (size_t i = 0; i < 12; ++i) {
        auto old = note(i + 2); old.at = now - 200 * day + i; old.text = L"Lighthouse anecdote " + std::to_wstring(i);
        memory.notes.push_back(old);
        memory.summaries.push_back(old);
    }
    for (size_t i = 20; i < 100; ++i) memory.notes.push_back(note(i));
    for (size_t i = 0; i < 3; ++i) memory.summarize(L"A recent unrelated chat " + std::to_wstring(i), now - i, "hana");
    const auto before = memory.save();
    const auto plain = memory.prompt(true, now, false, true, "hana");
    const auto query = memory.prompt(true, now, false, true, "hana", L"lighthouse");
    const auto count = [](const std::wstring& text) { return std::count(text.begin(), text.end(), L'\n'); };
    if (!contains(plain, core.text) || !contains(plain, repeated.text) || contains(plain, private_note.text) || contains(query, private_note.text)) return 20;
    if (contains(plain, L"Lighthouse anecdote") || !contains(query, L"Lighthouse anecdote")) return 21;
    if (count(plain) != 35 || count(query) != 47 || memory.save() != before) return 22; // 30+3 (+8+4 recalled), two headings.
    auto weak = core; weak.important = false; weak.source = "inferred"; weak.confidence = 0.1f;
    if (Memory::recallWeight(core, 0, now) != 0 || Memory::recallWeight(core, 1, now) >= Memory::recallWeight(weak, 2, now)) return 23;
    if (Memory::recallWeight(core, 1, now) <= Memory::recallWeight(weak, 1, now)) return 24;
    companion::cognition::State cognition;
    const auto hits = cognition.spreadActivation(memory, "hana", L"lighthouse", now);
    if (hits.empty() || std::any_of(hits.begin(), hits.end(), [&](const auto& hit) { return contains(hit.text, L"Rin alone"); })) return 25;
    return 0;
}
int summaries() {
    Memory memory;
    memory.summarize(L"Our first meeting", now - 365 * day, "hana");
    memory.summarize(L"Another companion's first meeting", now - 365 * day, "rin");
    for (size_t i = 0; i < companion::max_summaries_per_look + 10; ++i)
        memory.summarize(L"Ordinary conversation " + std::to_wstring(i), now - 5000 + i, "hana");
    if (memory.summaries.size() != companion::max_summaries_per_look + 1 || !has(memory.summaries, L"Our first meeting") ||
        !has(memory.summaries, L"Another companion's first meeting") || has(memory.summaries, L"Ordinary conversation 0")) return 30;
    const auto loaded = Memory::load(memory.save());
    if (!loaded || loaded->save() != memory.save()) return 31;
    return 0;
}
int capacityAndStorage(const std::filesystem::path& root) {
    Memory memory;
    for (size_t i = 0; i < companion::max_notes_capacity; ++i) {
        auto e = note(i);
        e.text = std::wstring(180, L'ą') + std::to_wstring(i);
        e.feeling = std::wstring(150, L'ę');
        e.confirmations = 3;
        memory.notes.push_back(std::move(e));
    }
    for (size_t i = 0; i < companion::max_summaries_capacity; ++i) {
        auto e = note(i, "cast" + std::to_string(i / companion::max_summaries_per_look));
        e.text = std::wstring(280, L'ż') + std::to_wstring(i);
        memory.summaries.push_back(std::move(e));
    }
    const auto serialized = memory.save();
    if (serialized.size() <= 4 * 1024 * 1024 || serialized.size() >= companion::max_memory_file_bytes) return 40;
    if (!companion::saveMemory(root, memory)) return 41;
    const auto loaded = companion::loadMemory(root);
    if (loaded.save() != serialized) return 42;
    std::cout << "Full text archive fixture: " << serialized.size() << " JSON bytes, "
              << std::filesystem::file_size(root / L"companion_memory.dat") << " encrypted bytes\n";

    companion::mind::State state;
    auto& self = state.of("hana");
    for (size_t i = 0; i < companion::mind::max_learned_concepts + 1; ++i)
        companion::mind::learnConcept(self, L"Term " + std::to_wstring(i), L"A taught meaning", now + i);
    for (size_t i = 0; i < companion::mind::max_key_memories + 1; ++i)
        companion::mind::addKeyMemory(self, L"Significant event " + std::to_wstring(i), now + i);
    for (size_t i = 0; i < companion::mind::max_shared_idioms + 1; ++i)
        companion::mind::addSharedIdiom(self, L"Our phrase " + std::to_wstring(i), now + i);
    for (size_t i = 0; i < companion::mind::max_player_world + 1; ++i)
        companion::mind::addWorldEntity(self, L"Person " + std::to_wstring(i), L"Friend", now + i);
    const auto restored = companion::mind::State::load(state.save());
    if (!restored || restored->save() != state.save() || self.learned_concepts.size() != companion::mind::max_learned_concepts ||
        self.key_memories.size() != companion::mind::max_key_memories || self.shared_idioms.size() != companion::mind::max_shared_idioms ||
        self.player_world.size() != companion::mind::max_player_world || self.learned_concepts.front().term != L"Term 1") return 43;

    companion::cognition::State cognition;
    for (size_t i = 0; i < companion::cognition::max_reflections + 1; ++i)
        cognition.reflect(L"Insight" + std::to_wstring(i), now + i, "hana");
    for (size_t i = 0; i < companion::cognition::max_schemas + 1; ++i)
        cognition.addSchema(L"Pattern" + std::to_wstring(i), now + i, "hana");
    for (size_t i = 0; i < companion::cognition::max_synapses; ++i)
        cognition.synapses.push_back({L"alpha" + std::to_wstring(i), L"beta" + std::to_wstring(i), 0.4f, now});
    cognition.learnHebbian(L"Unexpected comet", now);
    const auto cog = companion::cognition::State::load(cognition.save());
    if (!cog || cog->save() != cognition.save() || cognition.reflections.size() != companion::cognition::max_reflections ||
        cognition.schemas.size() != companion::cognition::max_schemas || cognition.synapses.size() != companion::cognition::max_synapses) return 44;
    return 0;
}
int chatArchive(const std::filesystem::path& root) {
    using namespace companion;
    chats::Conversation c;
    c.look = companion_fixture::hana;
    c.lines.push_back({true, L"A historical conversation"});
    for (size_t i = 0; i <= chats::keep; ++i) {
        c.started = c.updated = now + i;
        if (!chats::write(root, c)) return 50;
    }
    chats::prune(root, c.look);
    const auto archived = chats::list(root, c.look);
    if (archived.size() != chats::keep || archived.back() != now + 1 || archived.front() != now + chats::keep) return 51;
    const auto oldest = chats::read(chats::file(root, c.look, now + 1));
    if (!oldest || oldest->lines.front().text != c.lines.front().text) return 52;
    return 0;
}
}
int main() {
    const companion_fixture::Cast cast;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-memory-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    const int results[]{weightedRetention(), confirmations(), recall(), summaries(), capacityAndStorage(root), chatArchive(root)};
    for (const auto result : results) if (result) { std::cerr << "Memory check failed: " << result << '\n'; return result; }
    return 0;
}

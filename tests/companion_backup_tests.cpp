#include "companion_fixture.hpp"
#include "../src/net/companion_backup.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/protected_file.hpp"
#include <iostream>

int main() {
    const companion_fixture::Cast cast;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-backup-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    const auto config = root / L"original", destination = root / L"backups";
    std::filesystem::create_directories(destination);
    companion::Memory memory;
    memory.note(L"Lubię herbatę.", 100, "hana");
    memory.note(L"Tylko Rin zna tę historię.", 101, "rin");
    companion::mind::State mind;
    mind.of("hana").felt = L"troska";
    mind.of("hana").feelings.push_back({companion::Feeling::care, 0.7f, L"Myślę o graczu", 100});
    mind.of("rin").felt = L"smutek";
    companion::cognition::State cognition;
    cognition.think(L"Myśl Hany", companion::Emotion::thinking, 100, "hana");
    cognition.think(L"Myśl Rin", companion::Emotion::sad, 101, "rin");
    if (!companion::saveMemory(config, memory) || !companion::mind::saveState(config, mind) || !companion::cognition::saveState(config, cognition)) return 1;
    const auto original = protected_file::read(config / L"companion_memory.dat", 1024 * 1024);
    companion::chats::Conversation conversation;
    conversation.look = companion_fixture::hana;
    conversation.started = conversation.updated = companion::chats::localSeconds(2026, 10, 20, 12, 0, 0);
    conversation.lines.push_back({true, L"Zachowaj naszą rozmowę."}); conversation.turns.push_back({true, L"Zachowaj naszą rozmowę."});
    if (!companion::chats::write(config, conversation) || !atomic_file::write(config / L"companion_token.dat", "NOT_A_REAL_SECRET") ||
        !atomic_file::write(root / L"engine/launchme.exe", "fixture executable")) return 2;
    companion::backup::Input input;
    input.config = config; input.destination = destination; input.executable = root / L"engine/launchme.exe"; input.look = companion_fixture::hana;
    input.settings.companion_look = "hana";
    input.settings.companion_reply_length = companion::ReplyLength::brief;
    input.character_text = "[hana]\nname=Hana\npersona=genki\n";
    // Live UI state must override an older saved conversation without touching the original.
    input.live = conversation; input.live->lines.push_back({false, L"Najnowsza, jeszcze niezapisana odpowiedź."});
    input.live->lines.back().speaker = companion_fixture::hana;
    const auto made = companion::backup::create(input);
    if (!made.ok) { std::cerr << text_encoding::utf8(made.error) << '\n'; return 3; }
    if (made.conversations != 1 || !companion::backup::verify(made.path).ok || std::filesystem::exists(made.path / L"portable/companion_token.dat")) return 4;
    const auto copy = companion::backup::openCopy(made.path);
    if (!copy.ok) { std::cerr << text_encoding::utf8(copy.error) << '\n'; return 5; }
    if (loadAppSettings(copy.path / L"settings.ini").companion_reply_length != companion::ReplyLength::brief) return 16;
    const auto restored = companion::loadMemory(copy.path);
    if (restored.notes.size() != 1 || restored.notes[0].look != "hana") return 6;
    const auto restored_mind = companion::mind::loadState(copy.path);
    if (restored_mind.selves.size() != 1 || !restored_mind.find("hana") || restored_mind.find("hana")->feelings.size() != 1) return 7;
    const auto restored_cog = companion::cognition::loadState(copy.path);
    if (restored_cog.thoughts.size() != 1 || restored_cog.thoughts[0].look != "hana") return 8;
    const auto chat = companion::chats::latest(copy.path, companion_fixture::hana);
    if (!chat || chat->lines.size() != 2 || !protected_file::read(copy.path / L"companion_memory.dat", 1024 * 1024)) return 9;
    if (protected_file::read(config / L"companion_memory.dat", 1024 * 1024) != original || companion::chats::latest(config, companion_fixture::hana)->lines.size() != 1) return 10;
    auto changed = restored; changed.note(L"Nowy fakt tylko w kopii.", 200, "hana"); companion::saveMemory(copy.path, changed);
    const auto again = companion::backup::openCopy(made.path);
    if (!again.ok || again.path != copy.path || companion::loadMemory(copy.path).notes.size() != 2) return 11;
    if (!companion::backup::verify(made.path).ok) return 12; // Mutable copy excluded from original manifest.
    input.destination = config;
    if (companion::backup::create(input).ok) return 13; // No recursive self-copy.
    if (!atomic_file::write(made.path / L"portable/memory.json", "{}") || companion::backup::verify(made.path).ok) return 14;
    // A corrupt encrypted source must not become a silently empty backup.
    input.destination = destination;
    atomic_file::write(config / L"companion_mind.dat", "damaged");
    if (companion::backup::create(input).ok) return 15;
    return 0;
}

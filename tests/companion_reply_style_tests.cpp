#include "companion_fixture.hpp"
#include "../src/net/companion_chats.hpp"
#include "../src/platform/user_config.hpp"
#include <Windows.h>
#include <iostream>

int main() {
    using namespace companion;
    const companion_fixture::Cast cast;
    const auto baseline = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, 0, 1, 15);
    PromptExtras full;
    full.reply_length = ReplyLength::full;
    if (systemPrompt(Persona::genki, companion_fixture::hana, {}, false, 0, 1, 15, full) != baseline ||
        AppSettings{}.companion_reply_length != ReplyLength::full || parseAppSettings("companion_reply_length=garbage\n").companion_reply_length != ReplyLength::full) return 1;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-reply-style-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    const auto file = root / L"settings.ini";
    const std::array<int, 3> words{17, 34, 50}, duo{24, 47, 70}, limits{120, 60, 40};
    if (chats::message_cap != 40 || chats::context_turn_limit < 120 + 32) return 2; // +33% in every mode; context keeps room for the longest one.
    for (int i = 0; i < 3; ++i) {
        const auto length = static_cast<ReplyLength>(i);
        const auto& style = replyStyle(length);
        if (replyLengthFromKey(style.key) != length || style.messageLimit(chats::message_cap) != limits[i]) return 2;
        AppSettings settings; settings.companion_reply_length = length;
        if (!saveAppSettings(settings, file) || loadAppSettings(file).companion_reply_length != length) return 3;
        for (const bool en : {false, true}) {
            PromptExtras extras; extras.inner = true; extras.reply_length = length;
            auto prompt = systemPrompt(Persona::genki, companion_fixture::hana, {}, en, 0, 1, 15, extras);
            const auto word_target = std::to_wstring(words[i]) + (en ? L" words" : L" słów");
            if (prompt.find(word_target) == prompt.npos || prompt.find(L"sila_uczucia") == prompt.npos || prompt.find(L"<ocena>") == prompt.npos) return 4;
            if (i < 2 && (prompt.find(en ? L"50 words" : L"50 słów") != prompt.npos ||
                         prompt.find(en ? L"not inner appraisal" : L"nie oceny wewnętrznej") == prompt.npos)) return 5;
            extras.burst = true;
            prompt = systemPrompt(Persona::genki, companion_fixture::hana, {}, en, 0, 1, 15, extras);
            if (length == ReplyLength::brief && prompt.find(en ? L"two separate, very short messages" : L"dwie osobne, bardzo krótkie wiadomości") != prompt.npos) return 6;
            extras.guest = Guest{companion_fixture::rin, Persona::tsundere};
            prompt = systemPrompt(Persona::genki, companion_fixture::hana, {}, en, 0, 1, 15, extras);
            if (prompt.find(std::to_wstring(duo[i]) + (en ? L" words" : L" słów")) == prompt.npos) return 7;
        }
    }
    // A short-message conversation and its hidden references survive a restart without the old 80-turn truncation.
    chats::Conversation conversation;
    conversation.look = companion_fixture::hana;
    conversation.started = conversation.updated = chats::localSeconds(2026, 10, 20, 15, 0, 0);
    for (int i = 0; i < 110; ++i) conversation.turns.push_back({i % 2 == 0, L"Turn " + std::to_wstring(i)});
    for (int i = 0; i < 90; ++i) { chats::Line line; line.user = i % 2 == 0; line.speaker = companion_fixture::hana; line.text = L"Message " + std::to_wstring(i); conversation.lines.push_back(line); }
    const auto loaded = chats::Conversation::load(conversation.save());
    if (!loaded || loaded->lines.size() != 90 || loaded->turns.size() != 110 || loaded->turns.front().text != L"Turn 0" || loaded->turns.back().text != L"Turn 109") return 8;
    if (replyStyle(ReplyLength::full).tokens(420) != 420 || replyStyle(ReplyLength::concise).tokens(420) != 282 || replyStyle(ReplyLength::brief).tokens(420) != 143) return 9;
    return 0;
}

// Her answer: the reply text parsed (hidden appraisal, emotions, mentions, voice cues), split into
// chat lines, completed sentences and the speakable prefix of a stream.
#include "companion_internal.hpp"
#include "json.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace companion {
using namespace detail;

namespace {
// Emotion words a model may use instead of the listed ones.
std::optional<Emotion> emotionSynonym(const std::string& key) {
    if (const auto exact = emotionFromKey(key)) return exact;
    static constexpr std::pair<const char*, Emotion> synonyms[]{
        {"happy", Emotion::smile}, {"joy", Emotion::smile}, {"glad", Emotion::smile}, {"cheerful", Emotion::smile},
        {"hype", Emotion::excited}, {"star", Emotion::excited}, {"tease", Emotion::smug}, {"teasing", Emotion::smug},
        {"mad", Emotion::angry}, {"annoyed", Emotion::angry}, {"pout", Emotion::angry}, {"tsun", Emotion::angry}, {"huff", Emotion::angry},
        {"crying", Emotion::cry}, {"tears", Emotion::cry}, {"upset", Emotion::sad}, {"surprised", Emotion::shock}, {"shocked", Emotion::shock},
        {"blush", Emotion::shy}, {"embarrassed", Emotion::shy}, {"flustered", Emotion::shy}, {"dere", Emotion::shy},
        {"hmm", Emotion::thinking}, {"think", Emotion::thinking}, {"huh", Emotion::confused}, {"puzzled", Emotion::confused},
        {"heart", Emotion::love}, {"inlove", Emotion::love}, {"adore", Emotion::love}, {"laughing", Emotion::laugh}, {"lol", Emotion::laugh},
        {"haha", Emotion::laugh}, {"focus", Emotion::determined}, {"fired", Emotion::determined}, {"hello", Emotion::wave}, {"hi", Emotion::wave},
        {"bye", Emotion::wave}, {"sip", Emotion::drink}, {"soda", Emotion::drink}, {"cola", Emotion::drink}, {"game", Emotion::gaming},
        {"controller", Emotion::gaming},
        {"scared", Emotion::fear}, {"afraid", Emotion::fear}, {"panic", Emotion::fear}, {"tired", Emotion::sleepy}, {"yawn", Emotion::sleepy},
        {"wink", Emotion::playful}, {"crazy", Emotion::playful}, {"yandere", Emotion::playful}, {"mischief", Emotion::playful},
        {"neutral", Emotion::cool}, {"calm", Emotion::cool}, {"chill", Emotion::cool},
    };
    for (const auto& [word, emotion] : synonyms) if (key == word) return emotion;
    return std::nullopt;
}
std::wstring replaceAll(std::wstring text, std::wstring_view from, std::wstring_view to) {
    for (size_t at = text.find(from); at != std::wstring::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}
// A voice cue at `at` ("{laughs}", "{nervous laugh}", "{śmieje się}"): its words and the index
// after its brace; nullopt for anything else in braces (JSON, numbers, a long aside).
std::optional<std::pair<std::wstring, size_t>> voiceCue(std::wstring_view text, size_t at) {
    if (at >= text.size() || text[at] != L'{') return std::nullopt;
    const auto end = text.find(L'}', at + 1);
    if (end == std::wstring_view::npos || end - at > 48) return std::nullopt;
    const auto inside = text.substr(at + 1, end - at - 1);
    static constexpr std::wstring_view polish = L"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ";
    const bool words = std::all_of(inside.begin(), inside.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L' ' || c == L',' || c == L'-' || c == L'\'' || polish.find(c) != std::wstring_view::npos;
    });
    auto cue = trim(inside);
    if (!words || cue.empty()) return std::nullopt;
    return std::pair{std::move(cue), end + 1};
}
// Her words without tags: [emotion] and {cue} go, [[Title]] keeps its title, trimmed.
std::wstring voiceText(std::wstring_view text) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 2, L"[[") == 0 || text.compare(i, 2, L"]]") == 0) { ++i; continue; } // [[Title]]: the title stays.
        if (text[i] == L'[') { // [tag]: gone.
            const auto end = text.find(L']', i);
            if (end != std::wstring_view::npos && end - i < 24) { i = end; continue; }
        }
        if (const auto cue = voiceCue(text, i)) { i = cue->second - 1; continue; } // {cue}: gone.
        if (text[i] == L' ' && !out.empty() && out.back() == L' ') continue; // The space a removed tag or cue left.
        if (text[i] != L'[' && text[i] != L']') out += text[i];
    }
    return trim(out);
}
} // namespace

Reply parseText(std::wstring_view input) {
    Reply reply;
    std::wstring text(input);
    reply.full = trim(input);
    // Her inner appraisal before her words (<ocena>{…}</ocena>, JSON), kept apart; unfinished, it goes to the end.
    for (const auto& [open_tag, close_tag] : {std::pair<std::wstring_view, std::wstring_view>{L"<ocena>", L"</ocena>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<appraisal>", L"</appraisal>"}}) {
        if (const auto open = text.find(open_tag); open != std::wstring::npos) {
            const auto close = text.find(close_tag, open);
            reply.appraisal = trim(text.substr(open + open_tag.size(), close == std::wstring::npos ? std::wstring::npos : close - open - open_tag.size()));
            text.erase(open, close == std::wstring::npos ? std::wstring::npos : close + close_tag.size() - open);
        }
    }
    // A Japanese line the old VOICEVOX voice asked for (<ja>…</ja>, still in older conversations): never shown.
    if (const auto open = text.find(L"<ja>"); open != std::wstring::npos) {
        const auto close = text.find(L"</ja>", open);
        text.erase(open, close == std::wstring::npos ? std::wstring::npos : close + 5 - open);
    }
    // Hidden subconscious inner thought (<mysl>…</mysl> or <thought>…</thought>), kept apart.
    for (const auto& [open_tag, close_tag] : {std::pair<std::wstring_view, std::wstring_view>{L"<mysl>", L"</mysl>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<myśl>", L"</myśl>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<thought>", L"</thought>"}}) {
        if (const auto open = text.find(open_tag); open != std::wstring::npos) {
            const auto close = text.find(close_tag, open);
            const auto raw_thought = trim(text.substr(open + open_tag.size(), close == std::wstring::npos ? std::wstring::npos : close - open - open_tag.size()));
            if (raw_thought.starts_with(L"[") && !raw_thought.starts_with(L"[[")) {
                if (const auto bracket = raw_thought.find(L']'); bracket != std::wstring::npos && bracket <= 24) {
                    std::string key;
                    for (size_t j = 1; j < bracket; ++j) key += static_cast<char>(raw_thought[j] == L'-' || raw_thought[j] == L' ' ? '_' : raw_thought[j]);
                    if (const auto em = emotionSynonym(lower(key))) reply.subconscious_emotion = *em;
                }
            }
            reply.subconscious = voiceText(raw_thought);
            text.erase(open, close == std::wstring::npos ? std::wstring::npos : close + close_tag.size() - open);
        }
    }
    // Optional contextual reply chips (<opcje>A|B</opcje> or <options>A|B</options>), kept apart.
    for (const auto& [open_tag, close_tag] : {std::pair<std::wstring_view, std::wstring_view>{L"<opcje>", L"</opcje>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<options>", L"</options>"}}) {
        if (const auto open = text.find(open_tag); open != std::wstring::npos) {
            const auto close = text.find(close_tag, open);
            const auto raw_opts = text.substr(open + open_tag.size(), close == std::wstring::npos ? std::wstring::npos : close - open - open_tag.size());
            for (size_t start = 0; start <= raw_opts.size() && reply.prompts.size() < 3;) {
                auto bar = raw_opts.find(L'|', start);
                if (bar == std::wstring::npos) bar = raw_opts.size();
                auto item = voiceText(raw_opts.substr(start, bar - start));
                if (item.size() >= 2 && item.size() <= 72 && std::find(reply.prompts.begin(), reply.prompts.end(), item) == reply.prompts.end())
                    reply.prompts.push_back(std::move(item));
                start = bar + 1;
            }
            text.erase(open, close == std::wstring::npos ? std::wstring::npos : close + close_tag.size() - open);
        }
    }
    const std::wstring_view raw = text;
    reply.raw = trim(raw);
    std::wstring out;
    for (size_t i = 0; i < raw.size();) {
        const wchar_t ch = raw[i];
        if (ch == L'[' && i + 1 < raw.size() && raw[i + 1] == L'[') {
            const auto end = raw.find(L"]]", i + 2);
            if (end != std::wstring_view::npos && end - i < 160) {
                const auto title = trim(raw.substr(i + 2, end - i - 2));
                if (!title.empty()) {
                    reply.mentions.push_back({out.size(), title.size()});
                    out += title;
                    if (std::find(reply.games.begin(), reply.games.end(), title) == reply.games.end()) reply.games.push_back(title);
                }
                i = end + 2;
                continue;
            }
        }
        if (ch == L'[') {
            const auto end = raw.find(L']', i + 1);
            if (end != std::wstring_view::npos && end - i <= 28) {
                std::string key;
                bool word = end > i + 1;
                const auto asciiFold = [](wchar_t c) -> char {
                    switch (c) {
                    case L'ą': case L'Ą': return 'a';
                    case L'ć': case L'Ć': return 'c';
                    case L'ę': case L'Ę': return 'e';
                    case L'ł': case L'Ł': return 'l';
                    case L'ń': case L'Ń': return 'n';
                    case L'ó': case L'Ó': return 'o';
                    case L'ś': case L'Ś': return 's';
                    case L'ź': case L'Ź': case L'ż': case L'Ż': return 'z';
                    default: return 0;
                    }
                };
                for (size_t j = i + 1; j < end; ++j) {
                    const wchar_t c = raw[j];
                    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' || c == L'-' || c == L' ' || c == L':')
                        key += static_cast<char>(c == L'-' || c == L' ' ? '_' : c);
                    else if (const char folded_ch = asciiFold(c))
                        key += folded_ch;
                    else { word = false; break; }
                }
                if (word) {
                    key = lower(key);
                    while (!key.empty() && key.back() == '_') key.pop_back();
                    while (!key.empty() && key.front() == '_') key.erase(0, 1);
                    if (const auto emotion = emotionSynonym(key)) {
                        // Leading spaces belong after the tag: keep the offset at the next word.
                        size_t at = out.size();
                        if (!reply.emotions.empty() && reply.emotions.back().at == at) reply.emotions.back().emotion = *emotion;
                        else reply.emotions.push_back({at, *emotion});
                    } else if (key == "granica" || key == "boundary") {
                        reply.boundary = true; // She set a boundary: never shown.
                    } else if (key == "dylemat" || key == "dilemma") {
                        reply.stance_change = RelationStance::dilemma;
                    } else if (key == "dystans" || key == "distance") {
                        reply.stance_change = RelationStance::distanced;
                    } else if (key == "przelamanie" || key == "breakthrough") {
                        reply.stance_change = RelationStance::broken;
                    } else if (key == "otwarcie" || key == "open_up" || key == "open") {
                        reply.stance_change = RelationStance::open;
                    } else if (key.starts_with("relacja:") || key.starts_with("relacja_") ||
                               key.starts_with("relation:") || key.starts_with("relation_")) {
                        const auto sep = key.find_first_of(":_");
                        const auto sub = sep == std::string::npos ? std::string_view{} : std::string_view(key).substr(sep + 1);
                        if (sub == "dystans" || sub == "distance") reply.player_distance_change = true;
                        else if (sub == "blizej" || sub == "closer" || sub == "swobodna" || sub == "open") reply.player_distance_change = false;
                        else if (const auto role = relationRoleFromKey(sub)) reply.role_change = *role;
                    } else if (key.starts_with("zycie:") || key.starts_with("zycie_") ||
                               key.starts_with("life:") || key.starts_with("life_")) {
                        const auto sep = key.find_first_of(":_");
                        const auto sub = sep == std::string::npos ? std::string_view{} : std::string_view(key).substr(sep + 1);
                        if (const auto h = lifeHealthFromKey(sub)) reply.health_change = *h;
                        else if (const auto w = lifeWorkFromKey(sub)) reply.work_change = *w;
                        else if (const auto wl = lifeWealthFromKey(sub)) reply.wealth_change = *wl;
                        else if (const auto f = lifeFamilyFromKey(sub)) reply.family_change = *f;
                        else if (const auto so = lifeSocialFromKey(sub)) reply.social_change = *so;
                    }
                    // Any other short bracketed word is a stray tag (a gesture, a sound): dropped.
                    i = end + 1;
                    continue;
                }
            }
        }
        if (ch == L'{') { // A cue for her voice ({laughs}, {whispers}): heard here, never shown.
            if (const auto cue = voiceCue(raw, i)) {
                reply.cues.push_back({out.size(), cue->first});
                i = cue->second;
                continue;
            }
        }
        if (ch == L'*' || ch == L'#' || ch == L'`') { ++i; continue; } // Markdown emphasis.
        out += ch == L'\r' || ch == L'\t' ? L' ' : ch;
        ++i;
    }
    // Collapse whitespace the removed tags left behind, keeping the emotion offsets in step.
    std::wstring clean;
    std::vector<size_t> map(out.size() + 1, 0);
    for (size_t i = 0; i < out.size(); ++i) {
        map[i] = clean.size();
        const wchar_t c = out[i];
        const bool space = c == L' ' || c == L'\n';
        if (space && (clean.empty() || clean.back() == L' ' || clean.back() == L'\n')) continue;
        // A space a removed tag left before punctuation goes; an ellipsis after a sentence stays apart.
        const bool ellipsis = c == L'.' && i + 1 < out.size() && out[i + 1] == L'.';
        if ((c == L',' || c == L'.' || c == L'!' || c == L'?' || c == L':' || c == L';') && !ellipsis && !clean.empty() && clean.back() == L' ') clean.pop_back();
        clean += c == L'\n' ? L' ' : c;
    }
    map[out.size()] = clean.size();
    while (!clean.empty() && clean.back() == L' ') clean.pop_back();
    for (auto& mention : reply.mentions) {
        const auto end = std::min(map[mention.at + mention.length], clean.size());
        mention.at = std::min(map[mention.at], end);
        mention.length = end - mention.at;
    }
    std::erase_if(reply.mentions, [](const GameMention& mention) { return !mention.length; });
    for (auto& segment : reply.emotions) segment.at = std::min(map[std::min(segment.at, out.size())], clean.size());
    for (auto& cue : reply.cues) cue.at = std::min(map[std::min(cue.at, out.size())], clean.size());
    if (reply.emotions.empty() || reply.emotions.front().at > 0) reply.emotions.insert(reply.emotions.begin(), {0, reply.emotions.empty() ? calm : reply.emotions.front().emotion});
    // Segments that start at the same offset: the last one wins.
    std::vector<Segment> merged;
    for (const auto& segment : reply.emotions) {
        if (!merged.empty() && merged.back().at == segment.at) merged.back().emotion = segment.emotion;
        else merged.push_back(segment);
    }
    reply.emotions = std::move(merged);
    reply.text = std::move(clean);
    reply.ok = !reply.text.empty();
    return reply;
}

std::wstring withoutHidden(std::wstring_view raw) {
    std::wstring text(raw);
    for (const auto& [open_tag, close_tag] : {std::pair<std::wstring_view, std::wstring_view>{L"<ocena>", L"</ocena>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<appraisal>", L"</appraisal>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<mysl>", L"</mysl>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<myśl>", L"</myśl>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<thought>", L"</thought>"}}) {
        for (auto open = text.find(open_tag); open != std::wstring::npos; open = text.find(open_tag, open)) {
            const auto close = text.find(close_tag, open);
            text.erase(open, close == std::wstring::npos ? std::wstring::npos : close + close_tag.size() - open);
        }
    }
    return text;
}

namespace {
// Where the question that ends `text` starts: after the last sentence end (. ! ? … followed by a
// space) before it; npos when the text does not end with a question, the question is all of it,
// or what comes before it is too short to stand as a message of its own.
size_t trailingQuestion(std::wstring_view text) {
    size_t end = text.size();
    while (end && (text[end - 1] == L' ' || text[end - 1] == L'~' || text[end - 1] == L'!')) --end;
    if (!end || text[end - 1] != L'?') return std::wstring_view::npos;
    size_t mark = end - 1;
    while (mark && (text[mark - 1] == L'?' || text[mark - 1] == L'!')) --mark; // "?!", "??"
    for (size_t i = mark; i-- > 1;) {
        const wchar_t c = text[i - 1];
        if (text[i] != L' ' || (c != L'.' && c != L'!' && c != L'?' && c != L'…')) continue;
        const size_t start = i + 1;
        const auto before = trim(text.substr(0, i));
        // A short opener ("Hej hej! Może Hades?") stays with its question; an answer before it does not.
        if (before.size() < 16 || end - start < 6) return std::wstring_view::npos;
        return start;
    }
    return std::wstring_view::npos;
}

// Her last message ends with a question after something else: the question becomes a message of
// its own, the way people write in a messenger (the answer, then — as its own bubble — the
// question back). Its feeling, game mentions and voice cues follow it.
void splitTrailingQuestion(std::vector<Line>& out) {
    if (out.empty()) return;
    auto& last = out.back();
    const size_t at = trailingQuestion(last.text);
    if (at == std::wstring_view::npos) return;
    // A game title running across the cut keeps the message whole.
    if (std::any_of(last.mentions.begin(), last.mentions.end(), [&](const GameMention& m) { return m.at < at && m.at + m.length > at; })) return;
    Line tail{last.speaker, last.text.substr(at), {}, {}, {}};
    Emotion feeling = last.emotions.empty() ? calm : last.emotions.front().emotion;
    for (const auto& segment : last.emotions) if (segment.at <= at) feeling = segment.emotion;
    tail.emotions.push_back({0, feeling});
    for (const auto& segment : last.emotions)
        if (segment.at > at) {
            if (tail.emotions.back().at == segment.at - at) tail.emotions.back().emotion = segment.emotion;
            else tail.emotions.push_back({segment.at - at, segment.emotion});
        }
    for (const auto& mention : last.mentions) if (mention.at >= at) tail.mentions.push_back({mention.at - at, mention.length});
    for (const auto& cue : last.cues) if (cue.at >= at) tail.cues.push_back({cue.at - at, cue.text});
    std::erase_if(last.emotions, [&](const Segment& segment) { return segment.at >= at && segment.at != 0; });
    std::erase_if(last.mentions, [&](const GameMention& mention) { return mention.at >= at; });
    std::erase_if(last.cues, [&](const Cue& cue) { return cue.at >= at; });
    last.text = trim(std::wstring_view(last.text).substr(0, at));
    for (auto& cue : last.cues) cue.at = std::min(cue.at, last.text.size());
    out.push_back(std::move(tail));
}
} // namespace

std::vector<Line> lines(std::wstring_view raw, Look host, std::optional<Look> guest, size_t limit, bool question_apart) {
    struct Part { Look speaker; std::wstring body; bool present; };
    std::vector<Part> parts;
    // Her hidden parts first (a JSON appraisal may span lines): never a message of hers.
    const std::wstring text = replaceAll(withoutHidden(raw), L"||", L"\n");
    Look current = host;
    for (size_t start = 0; start < text.size();) {
        auto end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        auto line = trim(std::wstring_view(text).substr(start, end - start));
        start = end + 1;
        // "- Akane:", "**Akane**:", "[smug] Akane:" — the name signs the message; a leading tag stays with it.
        std::wstring_view rest = line;
        while (!rest.empty() && (rest.front() == L'-' || rest.front() == L'*' || rest.front() == L'•' || rest.front() == L' ')) rest.remove_prefix(1);
        std::wstring lead;
        if (rest.starts_with(L"[") && !rest.starts_with(L"[[")) {
            const auto close = rest.find(L']');
            if (close != std::wstring_view::npos && close <= 24) {
                lead = std::wstring(rest.substr(0, close + 1));
                rest.remove_prefix(close + 1);
                while (!rest.empty() && (rest.front() == L' ' || rest.front() == L'*')) rest.remove_prefix(1);
            }
        }
        std::optional<Look> named;
        for (int i = 0; i < lookCount() && !named; ++i) {
            const std::wstring_view who = name(static_cast<Look>(i));
            if (rest.size() <= who.size() || !equalsIgnoringCase(rest.substr(0, who.size()), who)) continue;
            auto after = rest.substr(who.size());
            while (!after.empty() && (after.front() == L'*' || after.front() == L' ')) after.remove_prefix(1);
            if (after.empty() || (after.front() != L':' && after.front() != L'：')) continue;
            named = static_cast<Look>(i);
            line = lead + trim(after.substr(1));
        }
        if (named) current = *named;
        if (trim(line).empty()) continue;
        parts.push_back({current, std::move(line), current == host || (guest && current == *guest)});
    }
    // Someone who is not here: her lines go (unless that leaves nothing, then they are the host's).
    if (std::any_of(parts.begin(), parts.end(), [](const Part& part) { return part.present; }))
        std::erase_if(parts, [](const Part& part) { return !part.present; });
    else for (auto& part : parts) part.speaker = host;
    std::vector<Line> out;
    for (const auto& part : parts) {
        auto parsed = parseText(part.body);
        if (!parsed.ok) continue;
        // No tag of its own (a voice cue before it does not count): the feeling she last showed goes on.
        std::wstring_view opening = part.body;
        if (const auto cue = voiceCue(opening, 0)) opening = trim(opening.substr(cue->second)).empty() ? std::wstring_view{} : opening.substr(opening.find_first_not_of(L' ', cue->second));
        const bool tagged = opening.starts_with(L"[") && !opening.starts_with(L"[[");
        if (!tagged) {
            const auto before = std::find_if(out.rbegin(), out.rend(), [&](const Line& line) { return line.speaker == part.speaker; });
            if (before != out.rend() && !before->emotions.empty()) parsed.emotions.front().emotion = before->emotions.back().emotion;
        }
        out.push_back({part.speaker, std::move(parsed.text), std::move(parsed.emotions), std::move(parsed.mentions), std::move(parsed.cues)});
    }
    // Too many: one speaker's neighbouring messages join (from the end), then the rest goes.
    for (size_t i = out.size(); limit && out.size() > limit && i-- > 1;) {
        if (out[i].speaker != out[i - 1].speaker) continue;
        auto& into = out[i - 1];
        const size_t shift = into.text.size() + 1;
        into.text += L" " + out[i].text;
        for (const auto& mention : out[i].mentions) into.mentions.push_back({mention.at + shift, mention.length});
        for (const auto& cue : out[i].cues) into.cues.push_back({cue.at + shift, cue.text});
        for (const auto& segment : out[i].emotions)
            if (into.emotions.empty() || into.emotions.back().emotion != segment.emotion) into.emotions.push_back({segment.at + shift, segment.emotion});
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (limit && out.size() > limit) out.resize(limit);
    if (question_apart && !guest) splitTrailingQuestion(out);
    return out;
}

Reply failedReply(const HttpResponse& response) {
    Reply failed;
    failed.status = response.status;
    failed.error = L"HTTP " + std::to_wstring(response.status);
    if (const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()))) {
        // Google/Anthropic: {"error":{"message"}}; Ollama: {"error":"…"}; Foundry: {"errorName"}.
        if (const auto* error = parsed->get("error"); error && error->get("message")) failed.error += L": " + text_encoding::wideLenient(error->get("message")->stringOr());
        else if (error && error->type == json::Value::Type::string) failed.error += L": " + text_encoding::wideLenient(error->text);
        else if (const auto* name = parsed->get("errorName")) failed.error += L": " + text_encoding::wideLenient(name->stringOr());
    }
    if (failed.error.size() > 200) failed.error.resize(200);
    return failed;
}

Reply finishReply(std::wstring text, bool cut, std::string_view reason) {
    // Cut by the token limit: only what she finished saying.
    if (cut) text = completeSentences(text);
    auto reply = parseText(text);
    if (!reply.ok || (cut && reply.text.size() < 12)) {
        Reply failed;
        failed.status = 200;
        failed.truncated = cut;
        failed.error = cut ? L"reply cut off" : reason.empty() ? L"empty reply" : L"empty reply (" + text_encoding::wideLenient(reason) + L")";
        return failed;
    }
    reply.truncated = cut;
    return reply;
}

Reply parseResponse(const HttpResponse& response) {
    if (response.status != 200) return failedReply(response);
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
    if (!parsed) { Reply failed; failed.status = response.status; failed.error = L"invalid JSON"; return failed; }
    const auto* candidates = parsed->get("candidates");
    std::wstring text;
    std::string reason;
    if (candidates && candidates->type == json::Value::Type::array && !candidates->elements.empty()) {
        const auto& first = candidates->elements.front();
        if (const auto* finish = first.get("finishReason")) reason = finish->stringOr();
        if (const auto* content = first.get("content"); content && content->get("parts")) {
            for (const auto& part : content->get("parts")->elements) {
                if (const auto* thought = part.get("thought"); thought && thought->flag) continue;
                if (const auto* value = part.get("text")) text += text_encoding::wideLenient(value->stringOr());
            }
        }
    }
    return finishReply(std::move(text), reason == "MAX_TOKENS", reason);
}

namespace {
// Where her words begin after her hidden parts at the start (<ocena>…</ocena><mysl>…</mysl>);
// `pending`: one of them is still arriving (or its tag has only begun), so nothing is said yet.
size_t hiddenPrefixEnd(std::wstring_view text, bool& pending) {
    static constexpr std::pair<std::wstring_view, std::wstring_view> hidden[]{
        {L"<ocena>", L"</ocena>"}, {L"<appraisal>", L"</appraisal>"}, {L"<mysl>", L"</mysl>"}, {L"<myśl>", L"</myśl>"}, {L"<thought>", L"</thought>"}};
    pending = false;
    size_t at = 0;
    for (;;) {
        while (at < text.size() && (text[at] == L' ' || text[at] == L'\n' || text[at] == L'\r' || text[at] == L'\t')) ++at;
        const auto rest = text.substr(at);
        bool skipped = false;
        for (const auto& [open, close] : hidden) {
            if (!rest.empty() && rest.size() < open.size() && open.starts_with(rest)) { pending = true; return at; }
            if (!rest.starts_with(open)) continue;
            const auto end = text.find(close, at + open.size());
            if (end == std::wstring_view::npos) { pending = true; return at; }
            at = end + close.size();
            skipped = true;
            break;
        }
        if (!skipped) return at;
    }
}
} // namespace

std::wstring completeSentences(std::wstring_view text) {
    // Her hidden parts first: they stay, and her words after them are what may be cut.
    bool pending = false;
    if (const size_t start = hiddenPrefixEnd(text, pending); pending) return {};
    else if (start > 0) {
        auto words = completeSentences(text.substr(start));
        return words.empty() ? std::wstring{} : trim(text.substr(0, start)) + L"\n" + words;
    }
    std::wstring_view kept = text;
    // Her Japanese line, subconscious thought or contextual reply options come last: unfinished, they go, and the full reply before them stays.
    for (const auto& [open_tag, close_tag] : {std::pair<std::wstring_view, std::wstring_view>{L"<ja>", L"</ja>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<mysl>", L"</mysl>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<myśl>", L"</myśl>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<thought>", L"</thought>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<opcje>", L"</opcje>"},
                                              std::pair<std::wstring_view, std::wstring_view>{L"<options>", L"</options>"}}) {
        if (const auto open = kept.find(open_tag); open != std::wstring_view::npos && kept.find(close_tag, open) == std::wstring_view::npos)
            return completeSentences(trim(kept.substr(0, open)));
    }
    if (kept.find(L"</ja>") != std::wstring_view::npos || kept.find(L"</mysl>") != std::wstring_view::npos ||
        kept.find(L"</myśl>") != std::wstring_view::npos || kept.find(L"</thought>") != std::wstring_view::npos ||
        kept.find(L"</opcje>") != std::wstring_view::npos || kept.find(L"</options>") != std::wstring_view::npos)
        return trim(kept); // Complete with its trailing tags.
    constexpr std::wstring_view ends = L".!?…。！？";
    constexpr std::wstring_view closers = L"\"'”’»)]」』";
    size_t end = std::wstring_view::npos;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (ends.find(kept[i]) == std::wstring_view::npos) continue;
        size_t stop = i + 1;
        while (stop < kept.size() && (ends.find(kept[stop]) != std::wstring_view::npos || closers.find(kept[stop]) != std::wstring_view::npos)) {
            // "]" closes a [[Title]] only when it is "]]"; a lone "]" may start a cut tag.
            if (kept[stop] == L']' && !(stop + 1 < kept.size() && kept[stop + 1] == L']')) break;
            stop += kept[stop] == L']' ? 2 : 1;
        }
        // Inside an unfinished [[Title or [tag the sentence is not over.
        const auto open = kept.rfind(L'[', i), close = kept.rfind(L']', i);
        if (open != std::wstring_view::npos && (close == std::wstring_view::npos || close < open)) continue;
        end = stop;
        i = stop - 1;
    }
    return end == std::wstring_view::npos ? std::wstring{} : trim(kept.substr(0, end));
}

size_t speakablePrefix(std::wstring_view whole) {
    // Her hidden parts at the start are never words; while one is still arriving nothing is.
    bool pending = false;
    const size_t start = hiddenPrefixEnd(whole, pending);
    if (pending) return 0;
    if (start > 0) {
        const auto rest = speakablePrefix(whole.substr(start));
        return rest ? start + rest : 0;
    }
    const auto text = whole;
    size_t end = text.size();
    for (const auto* tag : {L"<ja>", L"<mysl>", L"<myśl>", L"<thought>", L"<opcje>", L"<options>", L"<ocena>"})
        if (const auto open = text.find(tag); open != std::wstring_view::npos && open < end) end = open;
    if (const auto lt = text.substr(0, end).rfind(L'<'), gt = text.substr(0, end).rfind(L'>');
        lt != std::wstring_view::npos && (gt == std::wstring_view::npos || gt < lt) && end - lt <= 12) end = lt;
    // Never inside an unfinished [tag or [[Title.
    if (const auto open = text.substr(0, end).rfind(L"[["); open != std::wstring_view::npos && text.substr(0, end).find(L"]]", open) == std::wstring_view::npos) end = open;
    const auto shown = text.substr(0, end);
    if (const auto open = shown.rfind(L'['), close = shown.rfind(L']'); open != std::wstring_view::npos && (close == std::wstring_view::npos || close < open)) end = open;
    // Whole words: up to the last space or line end before that.
    if (!end) return 0;
    const auto space = text.substr(0, end).find_last_of(L" \n\t");
    return space == std::wstring_view::npos ? 0 : space;
}
} // namespace companion

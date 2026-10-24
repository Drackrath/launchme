#include "companion_memory.hpp"
#include "companion_chats.hpp"
#include "json.hpp"
#include "../library/game_index.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace companion {
namespace {
constexpr size_t remark_limit = 30, pick_limit = max_picks_capacity, note_limit = max_notes_capacity, summary_limit = max_summaries_capacity;
constexpr size_t summaries_per_look = 100;
constexpr size_t core_notes_shown = 30, summaries_shown = 3; // Always in prompt; older matching ones join via auto-recall (`query`) or przypomnij.
std::wstring trimmed(std::wstring_view text, size_t limit) {
    std::wstring out;
    for (const wchar_t ch : text) {
        const wchar_t c = ch == L'\n' || ch == L'\r' || ch == L'\t' ? L' ' : ch;
        if (c == L' ' && (out.empty() || out.back() == L' ')) continue;
        out += c;
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    if (out.size() > limit) {
        auto cut = out.rfind(L' ', limit);
        out.resize(cut == std::wstring::npos || cut < limit / 2 ? limit : cut);
        out += L"…";
    }
    return out;
}
template <class T> void keepLast(std::vector<T>& list, size_t limit) {
    if (list.size() > limit) list.erase(list.begin(), list.end() - static_cast<std::ptrdiff_t>(limit));
}
// Self-defining memories (`important = true`, McAdams/Tulving) are protected from FIFO eviction (up to 16 pinned).
void keepEntries(std::vector<Memory::Entry>& list, size_t limit) {
    if (list.size() <= limit) return;
    size_t pinned = 0;
    for (const auto& e : list) if (e.important) ++pinned;
    for (auto it = list.begin(); list.size() > limit && it != list.end();) {
        if (it->important && pinned <= 16) { ++it; continue; }
        if (it->important) --pinned;
        it = list.erase(it);
    }
    if (list.size() > limit) list.erase(list.begin(), list.end() - static_cast<std::ptrdiff_t>(limit));
}
std::wstring ago(uint64_t when, uint64_t now, bool english) {
    const uint64_t days = now > when ? (now - when) / 86400 : 0;
    if (english) return days == 0 ? L"today" : days == 1 ? L"yesterday" : std::to_wstring(days) + L" days ago";
    return days == 0 ? L"dziś" : days == 1 ? L"wczoraj" : std::to_wstring(days) + L" dni temu";
}

// The folded words of a text, all of them (function words too).
std::vector<std::wstring> tokens(std::wstring_view text) {
    std::vector<std::wstring> out;
    std::wstring word;
    const auto flush = [&] { if (!word.empty()) out.push_back(game_index::fold(word)); word.clear(); };
    for (const wchar_t ch : text) {
        if (std::iswalnum(ch)) word += ch;
        else flush();
    }
    flush();
    return out;
}
// "Nie lubi horrorów" says the opposite of "Lubi horrory".
bool negated(std::wstring_view text) {
    static constexpr std::wstring_view negators[]{L"nie", L"nigdy", L"not", L"never", L"no", L"dont", L"doesnt", L"didnt", L"isnt", L"wont", L"cant", L"hates", L"nienawidzi"};
    const auto words = tokens(text);
    return std::any_of(words.begin(), words.end(), [](const std::wstring& word) {
        return std::find(std::begin(negators), std::end(negators), word) != std::end(negators);
    });
}
// The numbers a note mentions: "ma 2 koty" and "ma 3 koty" are two facts, not one.
std::vector<std::wstring> numbers(std::wstring_view text) {
    std::vector<std::wstring> out;
    for (auto& word : tokens(text))
        if (!word.empty() && std::all_of(word.begin(), word.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) out.push_back(std::move(word));
    std::sort(out.begin(), out.end());
    return out;
}
// A short id for a note ("n" + four base-36 characters), unique among `notes`.
std::string noteId(std::wstring_view text, uint64_t salt, const std::vector<Memory::Entry>& notes) {
    uint64_t hash = 1469598103934665603ull ^ salt;
    for (const wchar_t ch : game_index::fold(text)) { hash ^= static_cast<uint64_t>(ch); hash *= 1099511628211ull; }
    static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (int attempt = 0; attempt < 64; ++attempt) {
        uint64_t value = hash + static_cast<uint64_t>(attempt) * 0x9E3779B97F4A7C15ull;
        std::string id = "n";
        for (int i = 0; i < 4; ++i) { id += digits[value % 36]; value /= 36; }
        if (std::none_of(notes.begin(), notes.end(), [&](const Memory::Entry& note) { return note.id == id; })) return id;
    }
    return "n" + std::to_string(notes.size());
}
void addKnower(Memory::Entry& note, std::string_view look, bool hearsay, std::string_view from = {}) {
    if (look.empty() || note.look == look) return;
    const auto found = std::find_if(note.known.begin(), note.known.end(), [&](const Memory::Knower& k) { return k.look == look; });
    if (found != note.known.end()) {
        if (!hearsay) { found->hearsay = false; found->from.clear(); } // Heard it herself now.
        return;
    }
    note.known.push_back({std::string(look), hearsay, std::string(from)});
}

json::Value entries(const std::vector<Memory::Entry>& list) {
    auto array = json::array();
    for (const auto& entry : list) {
        auto item = json::object();
        item.properties["at"] = json::number(static_cast<double>(entry.at));
        item.properties["text"] = json::string(text_encoding::utf8Lenient(entry.text));
        if (!entry.look.empty()) item.properties["look"] = json::string(entry.look);
        if (!entry.emotion.empty()) item.properties["emotion"] = json::string(entry.emotion);
        if (!entry.feeling.empty()) item.properties["feeling"] = json::string(text_encoding::utf8Lenient(entry.feeling));
        if (!entry.id.empty()) item.properties["id"] = json::string(entry.id);
        if (!entry.source.empty()) item.properties["source"] = json::string(entry.source);
        if (entry.confidence > 0) item.properties["confidence"] = json::number(std::round(entry.confidence * 100) / 100.0);
        if (entry.secret) item.properties["secret"] = json::boolean(true);
        if (entry.important) item.properties["important"] = json::boolean(true);
        if (!entry.topic.empty()) item.properties["topic"] = json::string(entry.topic);
        if (!entry.known.empty()) {
            auto known = json::array();
            for (const auto& k : entry.known) {
                auto who = json::object();
                who.properties["look"] = json::string(k.look);
                if (k.hearsay) who.properties["hearsay"] = json::boolean(true);
                if (!k.from.empty()) who.properties["from"] = json::string(k.from);
                known.elements.push_back(std::move(who));
            }
            item.properties["known"] = std::move(known);
        }
        array.elements.push_back(std::move(item));
    }
    return array;
}
std::string shortKey(const json::Value* value, size_t limit) {
    return value && value->type == json::Value::Type::string && value->text.size() <= limit ? value->text : std::string{};
}
std::vector<Memory::Entry> entries(const json::Value* list, size_t limit) {
    std::vector<Memory::Entry> result;
    if (!list || list->type != json::Value::Type::array) return result;
    for (const auto& item : list->elements) {
        const auto* at = item.get("at");
        const auto* text = item.get("text");
        if (!text || text->type != json::Value::Type::string || text->text.empty()) continue;
        const auto* feeling = item.get("feeling");
        Memory::Entry entry;
        entry.at = at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0;
        entry.text = text_encoding::wideLenient(text->text);
        entry.look = shortKey(item.get("look"), 32);
        entry.emotion = shortKey(item.get("emotion"), 24);
        entry.feeling = feeling && feeling->type == json::Value::Type::string ? trimmed(text_encoding::wideLenient(feeling->text), 160) : std::wstring{};
        entry.id = shortKey(item.get("id"), 12);
        entry.source = shortKey(item.get("source"), 12);
        if (const auto* confidence = item.get("confidence"); confidence && confidence->type == json::Value::Type::number)
            entry.confidence = static_cast<float>(std::clamp(confidence->numeric, 0.0, 1.0));
        if (const auto* secret = item.get("secret"); secret && secret->type == json::Value::Type::boolean) entry.secret = secret->flag;
        if (const auto* imp = item.get("important"); imp && imp->type == json::Value::Type::boolean) entry.important = imp->flag;
        entry.topic = shortKey(item.get("topic"), 16);
        if (const auto* known = item.get("known"); known && known->type == json::Value::Type::array)
            for (const auto& who : known->elements) {
                Memory::Knower k;
                k.look = shortKey(who.get("look"), 32);
                if (k.look.empty() || k.look == entry.look) continue;
                if (const auto* hearsay = who.get("hearsay"); hearsay && hearsay->type == json::Value::Type::boolean) k.hearsay = hearsay->flag;
                k.from = shortKey(who.get("from"), 32);
                if (entry.known.size() < 12) entry.known.push_back(std::move(k));
            }
        result.push_back(std::move(entry));
    }
    keepEntries(result, limit);
    return result;
}
std::filesystem::path file(const std::filesystem::path& config) { return config / L"companion_memory.dat"; }

// How she knows a note, for her prompt: "słyszałaś", "przypuszczasz, pewność 40%", "plotka od Miki", "przez mgłę — dopytaj szczegółu".
std::wstring status(const Memory::Entry& note, std::string_view look, uint64_t now, bool english, const std::function<std::wstring(std::string_view)>& name) {
    std::wstring out;
    if (const auto* k = Memory::knower(note, look); k && k->hearsay) {
        const std::wstring who = k->from.empty() ? std::wstring{} : name ? name(k->from) : text_encoding::wideLenient(k->from);
        out = english ? (who.empty() ? L"gossip" : L"gossip from " + who) : (who.empty() ? L"plotka" : L"plotka od " + who);
    } else if (note.source == "observed") out = english ? L"you know" : L"wiesz";
    else if (note.source == "inferred") out = english ? L"you guess" : L"przypuszczasz";
    else out = english ? L"you heard" : L"słyszałaś";
    if (note.confidence > 0 && note.confidence < 0.7f)
        out += (english ? L", sure " : L", pewność ") + std::to_wstring(static_cast<int>(std::lround(note.confidence * 10)) * 10) + L"%";
    if (note.secret) out += english ? L", a secret" : L", w sekrecie";
    if (note.important) out += english ? L", key memory" : L", ważne wspomnienie";
    else if (note.topic != "place" && note.at > 0 && now > note.at + 6 * 86400 && (note.confidence <= 0.0f || note.confidence < 0.85f))
        out += english ? L", hazy — ask if you remember the detail right" : L", przez mgłę — dopytaj, czy dobrze kojarzysz szczegół";
    return out;
}
} // namespace

void Memory::said(std::wstring_view text, uint64_t now) {
    auto line = trimmed(text, 140);
    if (line.empty()) return;
    remarks.push_back({now, std::move(line)});
    keepLast(remarks, remark_limit);
}

void Memory::suggested(std::string key, std::wstring title, uint64_t now, std::string look) {
    if (key.empty()) return;
    std::erase_if(picks, [&](const Pick& pick) { return pick.key == key; });
    picks.push_back({now, std::move(key), std::move(title), std::move(look)});
    keepLast(picks, pick_limit);
}

bool Memory::note(std::wstring_view text, uint64_t now, std::string look, std::string emotion, std::wstring feeling) {
    Facts facts;
    facts.look = std::move(look);
    facts.emotion = std::move(emotion);
    facts.feeling = std::move(feeling);
    facts.confidence = 0; // The tool's notes: told, sure enough (as older notes).
    return note(text, now, facts);
}

bool Memory::note(std::wstring_view text, uint64_t now, const Facts& facts) {
    auto line = trimmed(text, 200);
    auto clean_feeling = trimmed(facts.feeling, 160);
    const auto folded = game_index::fold(line);
    if (folded.size() < 4 || text.size() > 400) return false;
    // No secrets in her notes: passwords, ID numbers, addresses.
    for (const auto* word : {L"haslo", L"password", L"pesel", L"adreszamieszkania", L"homeaddress", L"token", L"numerkarty", L"cardnumber"})
        if (folded.find(word) != std::wstring::npos) return false;
    const auto source = facts.source == "observed" || facts.source == "inferred" ? facts.source : std::string{};
    // What a known note learns from hearing it again.
    const auto refresh = [&](Entry& entry, bool replace_text) {
        if (replace_text) entry.text = std::move(line);
        if (!facts.emotion.empty()) entry.emotion = facts.emotion;
        if (!clean_feeling.empty()) entry.feeling = std::move(clean_feeling);
        if (!entry.look.empty() || !entry.known.empty()) { // An older shared note stays everyone's.
            addKnower(entry, facts.look, false);
            addKnower(entry, facts.guest, false);
        }
        if (facts.confidence > 0) entry.confidence = std::max(entry.confidence, facts.confidence);
        if (facts.secret) entry.secret = true;
        if (facts.important) entry.important = true;
        if (!facts.topic.empty()) entry.topic = facts.topic;
        if (source.empty() || entry.source == "inferred") entry.source = source; // Heard beats guessed.
        entry.at = now;
        if (entry.id.empty()) entry.id = noteId(entry.text, entry.at, notes);
    };
    const auto same = std::find_if(notes.begin(), notes.end(), [&](const Entry& entry) { return game_index::fold(entry.text) == folded; });
    if (same != notes.end()) {
        refresh(*same, false);
        return false;
    }
    // Near-duplicate consolidation: if a longer note expands an existing one (or shares almost all stem words),
    // update the existing entry in place so the 500-note archive stays clean and dense. A note that says
    // the opposite replaces the old one (the newer word wins); different numbers are different facts.
    const auto stems = chats::searchWords(line, 16);
    const bool negative = negated(line);
    const auto digits = numbers(line);
    for (auto& entry : notes) {
        if (!facts.look.empty() && !knows(entry, facts.look)) continue; // Another companion's note is not hers to change.
        if (numbers(entry.text) != digits) continue;
        const bool opposite = negated(entry.text) != negative;
        const auto existing_folded = game_index::fold(entry.text);
        if (!opposite && existing_folded.size() >= 14 && folded.size() >= 14 &&
            (folded.find(existing_folded) != std::wstring::npos || existing_folded.find(folded) != std::wstring::npos)) {
            refresh(entry, line.size() >= entry.text.size());
            return true;
        }
        if (stems.size() >= 3) {
            const auto other_stems = chats::searchWords(entry.text, 16);
            if (other_stems.size() >= 3) {
                size_t shared = 0;
                chats::overlap(stems, other_stems, &shared);
                const size_t min_len = std::min(stems.size(), other_stems.size());
                if (shared * 5 >= min_len * 4 && std::abs(static_cast<int>(stems.size()) - static_cast<int>(other_stems.size())) <= 2) {
                    refresh(entry, opposite || line.size() >= entry.text.size());
                    return true;
                }
            }
        }
    }
    Entry entry;
    entry.at = now;
    entry.text = std::move(line);
    entry.look = facts.look;
    entry.emotion = facts.emotion;
    entry.feeling = std::move(clean_feeling);
    entry.id = noteId(entry.text, now, notes);
    entry.source = source;
    entry.confidence = facts.confidence > 0 ? std::clamp(facts.confidence, 0.05f, 1.0f) : 0;
    entry.secret = facts.secret;
    entry.important = facts.important;
    entry.topic = facts.topic;
    if (!entry.look.empty()) addKnower(entry, facts.guest, false);
    notes.push_back(std::move(entry));
    keepEntries(notes, note_limit);
    return true;
}

bool Memory::revise(std::string_view id, std::wstring_view text, std::optional<float> confidence, std::string_view look, uint64_t now) {
    if (id.empty()) return false;
    const auto found = std::find_if(notes.begin(), notes.end(), [&](const Entry& note) { return note.id == id && knows(note, look); });
    if (found == notes.end()) return false;
    auto line = trimmed(text, 200);
    const bool forget = line.empty() ? !confidence || *confidence < 0.15f : confidence && *confidence < 0.15f;
    if (forget) {
        // Others still know it: she alone lets go of it.
        if (!look.empty() && found->look != look) {
            std::erase_if(found->known, [&](const Knower& k) { return k.look == look; });
            return true;
        }
        if (!look.empty() && found->look == look && !found->known.empty()) {
            auto next = found->known.front();
            found->known.erase(found->known.begin());
            found->look = next.look;
            return true;
        }
        notes.erase(found);
        return true;
    }
    if (game_index::fold(line).size() >= 4) found->text = std::move(line);
    if (confidence) found->confidence = std::clamp(*confidence, 0.05f, 1.0f);
    found->at = now;
    return true;
}

int Memory::reconsolidate(std::string_view look, std::wstring_view query, float valence, std::string_view emotion, std::wstring_view tint, uint64_t now) {
    if (query.empty() || std::abs(valence) < 0.25f) return 0;
    const auto words = chats::searchWords(query);
    if (words.empty()) return 0;
    int count = 0;
    const auto clean_tint = trimmed(tint, 120);
    for (auto& entry : notes) {
        if (count >= 2) break;
        if (!knows(entry, look) || (!entry.look.empty() && entry.look != look)) continue;
        if (entry.at && now < entry.at + 3600) continue; // Only older memories reconsolidate on recall.
        if (chats::matches(words, entry.text) <= 0) continue;
        if (valence >= 0.35f && (entry.emotion == "angry" || entry.emotion == "sad" || entry.emotion == "fear")) {
            entry.emotion = !emotion.empty() ? std::string(emotion) : "playful";
        } else if (valence <= -0.35f && (entry.emotion.empty() || entry.emotion == "smile" || entry.emotion == "calm" || entry.emotion == "cool")) {
            entry.emotion = !emotion.empty() ? std::string(emotion) : "sad";
        }
        if (!clean_tint.empty() && entry.feeling != clean_tint) entry.feeling = clean_tint;
        if (entry.confidence > 0) entry.confidence = std::clamp(entry.confidence + 0.04f, 0.1f, 1.0f);
        ++count;
    }
    return count;
}

void Memory::summarize(std::wstring_view text, uint64_t now, std::string look, bool important) {
    auto line = trimmed(text, 300);
    if (line.empty()) return;
    // The very first conversation summary with a companion is a foundational self-defining memory (McAdams).
    const bool first_for_look = !look.empty() && std::none_of(summaries.begin(), summaries.end(), [&](const Entry& s) { return s.look == look; });
    Entry entry{now, std::move(line), look};
    entry.important = important || first_for_look;
    summaries.push_back(std::move(entry));
    if (!look.empty()) {
        size_t own = 0, own_pinned = 0;
        for (const auto& s : summaries) if (s.look == look) { ++own; if (s.important) ++own_pinned; }
        for (auto it = summaries.begin(); own > summaries_per_look && it != summaries.end();) {
            if (it->look == look) {
                if (it->important && own_pinned <= 10) { ++it; continue; }
                if (it->important) --own_pinned;
                it = summaries.erase(it);
                --own;
            } else ++it;
        }
    }
    keepEntries(summaries, summary_limit);
}

bool Memory::removeNote(uint64_t at, std::wstring_view text) {
    const auto before = notes.size();
    std::erase_if(notes, [&](const Entry& e) { return (at && e.at == at && e.text == text) || (!at && e.text == text); });
    if (notes.size() == before && !text.empty())
        std::erase_if(notes, [&](const Entry& e) { return e.text == text; });
    return notes.size() != before;
}

bool Memory::removeSummary(uint64_t at, std::wstring_view text) {
    const auto before = summaries.size();
    std::erase_if(summaries, [&](const Entry& e) { return (at && e.at == at && e.text == text) || (!at && e.text == text); });
    if (summaries.size() == before && !text.empty())
        std::erase_if(summaries, [&](const Entry& e) { return e.text == text; });
    return summaries.size() != before;
}

bool Memory::removePick(std::string_view key, uint64_t at) {
    const auto before = picks.size();
    std::erase_if(picks, [&](const Pick& p) { return (!key.empty() && p.key == key) || (at && p.at == at); });
    return picks.size() != before;
}

std::vector<std::string> Memory::resting(uint64_t now, uint64_t days) const {
    std::vector<std::string> keys;
    for (const auto& pick : picks) if (pick.at + days * 86400 > now) keys.push_back(pick.key);
    return keys;
}

bool Memory::knows(const Entry& note, std::string_view look) {
    if (look.empty() || (note.look.empty() && note.known.empty()) || note.look == look) return true;
    return std::any_of(note.known.begin(), note.known.end(), [&](const Knower& k) { return k.look == look; });
}

const Memory::Knower* Memory::knower(const Entry& note, std::string_view look) {
    const auto found = std::find_if(note.known.begin(), note.known.end(), [&](const Knower& k) { return k.look == look; });
    return found == note.known.end() ? nullptr : &*found;
}

std::vector<const Memory::Entry*> Memory::notesOf(std::string_view look) const {
    std::vector<const Entry*> out;
    for (const auto& note : notes) if (knows(note, look)) out.push_back(&note);
    return out;
}

bool Memory::knowsPlace(std::string_view look) const {
    return std::any_of(notes.begin(), notes.end(), [&](const Entry& note) { return note.topic == "place" && knows(note, look); });
}

std::wstring Memory::prompt(bool english, uint64_t now, bool short_term, bool long_term, std::string_view look, std::wstring_view query,
                            const std::function<std::wstring(std::string_view)>& name) const {
    std::wstring out;
    const auto recent = [&](uint64_t at) { return at + 3 * 86400 > now; };
    std::vector<const Entry*> said_lately;
    std::vector<const Pick*> picked;
    if (short_term) {
        for (auto it = remarks.rbegin(); it != remarks.rend() && said_lately.size() < 8; ++it) if (recent(it->at)) said_lately.push_back(&*it);
        for (auto it = picks.rbegin(); it != picks.rend() && picked.size() < 12; ++it) if (recent(it->at)) picked.push_back(&*it);
    }
    if (!said_lately.empty() || !picked.empty()) {
        out += english ? L"Your memory — what you said lately (do not repeat yourself; find new topics, jokes and games):\n"
                       : L"Twoja pamięć — co ostatnio mówiłaś (nie powtarzaj się; szukaj nowych tematów, żartów i gier):\n";
        for (auto it = said_lately.rbegin(); it != said_lately.rend(); ++it) out += L"- " + ago((*it)->at, now, english) + L": " + (*it)->text + L"\n";
        if (!picked.empty()) {
            out += english ? L"Games you suggested lately (suggest others unless asked):" : L"Gry, które ostatnio proponowałaś (proponuj inne, chyba że ‹on|ona› o nie pyta):";
            for (size_t i = 0; i < picked.size(); ++i) out += (i ? L", " : L" ") + picked[i]->title;
            out += L"\n";
        }
    }
    // Layer 1 (Core): her latest `core_notes_shown` notes and her latest `summaries_shown` summaries.
    // Layer 2 (Auto-Recall): older notes and summaries that match stem words of `query`.
    const auto known = notesOf(look);
    std::vector<const Entry*> shown_notes;
    std::vector<const Entry*> latest;
    if (long_term) {
        const size_t start_note = known.size() > core_notes_shown ? known.size() - core_notes_shown : 0;
        for (size_t i = start_note; i < known.size(); ++i) shown_notes.push_back(known[i]);
        for (auto it = summaries.rbegin(); it != summaries.rend() && latest.size() < (look.empty() ? summary_limit : summaries_shown); ++it)
            if (look.empty() || it->look.empty() || it->look == look) latest.push_back(&*it);

        if (!query.empty()) {
            const auto words = chats::searchWords(query);
            if (!words.empty()) {
                if (start_note > 0) {
                    std::vector<std::pair<int, const Entry*>> recalled_notes;
                    for (size_t i = 0; i < start_note; ++i)
                        if (const int score = chats::matches(words, known[i]->text); score > 0) recalled_notes.push_back({score, known[i]});
                    std::stable_sort(recalled_notes.begin(), recalled_notes.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                    for (size_t i = 0; i < recalled_notes.size() && i < 8; ++i) shown_notes.insert(shown_notes.begin() + static_cast<std::ptrdiff_t>(i), recalled_notes[i].second);
                }
                std::vector<std::pair<int, const Entry*>> recalled_sums;
                for (const auto& sum : summaries) {
                    if (!look.empty() && !sum.look.empty() && sum.look != look) continue;
                    if (std::find(latest.begin(), latest.end(), &sum) != latest.end()) continue;
                    if (const int score = chats::matches(words, sum.text); score > 0)
                        recalled_sums.push_back({score + (sum.important ? 2 : 0), &sum});
                }
                std::stable_sort(recalled_sums.begin(), recalled_sums.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                for (size_t i = 0; i < recalled_sums.size() && i < 4; ++i) latest.push_back(recalled_sums[i].second);
            }
        }
    }
    if (long_term && (!shown_notes.empty() || !latest.empty())) {
        if (!shown_notes.empty())
            out += english ? L"What you know about ‹him|her|them› (in brackets: the note's #id, for you only — never say it — and how you know it; speak surely only of what you "
                             L"know or heard, carefully of guesses and gossip):\n"
                           : L"Co wiesz o ‹nim|niej› (w nawiasie: #id notatki, tylko dla ciebie — nigdy nie wypowiadaj tego #id — i skąd to wiesz; pewnie mów tylko o tym, co wiesz "
                             L"albo słyszałaś, ostrożnie o przypuszczeniach i plotkach):\n";
        else out += english ? L"What you know about ‹him|her|them› (from earlier talks):\n" : L"Co wiesz o ‹nim|niej› (z wcześniejszych rozmów):\n";
        for (const auto* note : shown_notes) {
            out += L"- [";
            if (!note->id.empty()) out += L"#" + text_encoding::wideLenient(note->id) + L" · ";
            out += status(*note, look, now, english, name) + L"] " + note->text;
            if (!note->feeling.empty() && (look.empty() || note->look.empty() || note->look == look)) {
                out += (english ? L" (how you felt then" : L" (jak się wtedy poczułaś");
                if (!note->emotion.empty()) out += L" [" + text_encoding::wideLenient(note->emotion) + L"]";
                out += L": " + note->feeling + L")";
            }
            out += L"\n";
        }
        if (!latest.empty() && !shown_notes.empty()) out += english ? L"Your earlier talks:\n" : L"Wasze wcześniejsze rozmowy:\n";
        for (auto it = latest.rbegin(); it != latest.rend(); ++it) out += L"- " + ago((*it)->at, now, english) + L": " + (*it)->text + L"\n";
    }
    return out;
}

std::string Memory::save() const {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    root.properties["remarks"] = entries(remarks);
    root.properties["notes"] = entries(notes);
    root.properties["summaries"] = entries(summaries);
    auto list = json::array();
    for (const auto& pick : picks) {
        auto item = json::object();
        item.properties["at"] = json::number(static_cast<double>(pick.at));
        item.properties["key"] = json::string(pick.key);
        item.properties["title"] = json::string(text_encoding::utf8Lenient(pick.title));
        if (!pick.look.empty()) item.properties["look"] = json::string(pick.look);
        list.elements.push_back(std::move(item));
    }
    root.properties["picks"] = std::move(list);
    return json::serialize(root);
}

std::optional<Memory> Memory::load(std::string_view text) {
    const auto root = json::parse(text, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object || !root->get("version") || root->get("version")->intOr() != 1) return std::nullopt;
    Memory memory;
    memory.remarks = entries(root->get("remarks"), remark_limit);
    memory.notes = entries(root->get("notes"), note_limit);
    memory.summaries = entries(root->get("summaries"), summary_limit);
    // Older notes get their ids (stable: from when and what).
    for (auto& note : memory.notes) {
        if (!note.id.empty() && std::count_if(memory.notes.begin(), memory.notes.end(), [&](const Entry& other) { return other.id == note.id; }) == 1) continue;
        note.id.clear();
        note.id = noteId(note.text, note.at, memory.notes);
    }
    if (const auto* list = root->get("picks"); list && list->type == json::Value::Type::array)
        for (const auto& item : list->elements) {
            const auto* key = item.get("key");
            const auto* at = item.get("at");
            if (!key || key->type != json::Value::Type::string || key->text.empty()) continue;
            const auto* look = item.get("look");
            memory.picks.push_back({at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0, key->text,
                                    item.get("title") ? text_encoding::wideLenient(item.get("title")->stringOr()) : std::wstring{},
                                    look && look->type == json::Value::Type::string && look->text.size() <= 32 ? look->text : std::string{}});
        }
    keepLast(memory.picks, pick_limit);
    return memory;
}

Memory loadMemory(const std::filesystem::path& config) {
    const auto bytes = protected_file::read(file(config), 4 * 1024 * 1024);
    if (!bytes) return {};
    return Memory::load(*bytes).value_or(Memory{});
}

bool saveMemory(const std::filesystem::path& config, const Memory& memory) {
    if (memory.empty()) return clearMemory(config);
    std::error_code ec;
    std::filesystem::create_directories(config, ec);
    return protected_file::write(file(config), memory.save(), L"Launchme companion memory");
}

bool clearMemory(const std::filesystem::path& config) {
    std::error_code ec;
    std::filesystem::remove(file(config), ec);
    return !ec;
}
}

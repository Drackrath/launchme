#pragma once
// Helpers shared by the companion mind's translation units (companion_mind*.cpp): text folding,
// claims, goals, episodes and curiosity bookkeeping. Not part of the mind's interface.
#include "companion_mind.hpp"
#include "companion_chats.hpp"
#include "../library/game_index.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <initializer_list>
#include <string>
#include <string_view>

namespace companion::mind::detail {
inline constexpr size_t expectation_limit = 40, goal_limit = 30, claim_limit = 120, marker_limit = 60;
inline constexpr uint64_t hour = 3600, day = 86400;

inline std::wstring clean(std::wstring_view text, size_t max_chars) {
    std::wstring out;
    for (const wchar_t ch : text) {
        const wchar_t c = ch == L'\n' || ch == L'\r' || ch == L'\t' ? L' ' : ch;
        if (c == L' ' && (out.empty() || out.back() == L' ')) continue;
        out += c;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    if (out.size() > max_chars) {
        const auto cut = out.rfind(L' ', max_chars);
        out.resize(cut == std::wstring::npos || cut < max_chars / 2 ? max_chars : cut);
        out += L"…";
    }
    return out;
}
inline std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
inline std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
inline float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
inline std::string newId(Self& self, char prefix) { return std::string(1, prefix) + std::to_string(self.next_id++); }
inline std::string bareId(std::string id) {
    while (!id.empty() && (id.front() == '#' || id.front() == ' ')) id.erase(0, 1);
    return id;
}
// The folded words of a message, each between spaces (" kocham cie "), for phrase matching.
inline std::wstring spaced(std::wstring_view text) {
    std::wstring out = L" ", word;
    const auto flush = [&] { if (!word.empty()) out += game_index::fold(word) + L" "; word.clear(); };
    for (const wchar_t ch : text) {
        if (std::iswalnum(ch)) word += ch;
        else flush();
    }
    flush();
    return out;
}
inline bool hasAny(const std::wstring& haystack, std::initializer_list<const wchar_t*> phrases) {
    for (const auto* phrase : phrases)
        if (haystack.find(std::wstring(L" ") + phrase + L" ") != std::wstring::npos) return true;
    return false;
}
inline bool hasRaw(std::wstring_view text, std::initializer_list<const wchar_t*> marks) {
    for (const auto* mark : marks)
        if (text.find(mark) != std::wstring_view::npos) return true;
    return false;
}
inline Marker* markerOf(Self& self, std::wstring_view subject) {
    const auto folded = game_index::fold(subject);
    if (folded.size() < 2) return nullptr;
    auto found = std::find_if(self.markers.begin(), self.markers.end(), [&](const Marker& m) { return game_index::fold(m.subject) == folded; });
    if (found != self.markers.end()) return &*found;
    if (self.markers.size() >= marker_limit) // The faintest gives way.
        self.markers.erase(std::min_element(self.markers.begin(), self.markers.end(), [](const Marker& a, const Marker& b) { return std::abs(a.valence) < std::abs(b.valence); }));
    self.markers.push_back({std::wstring(subject), 0, 0});
    return &self.markers.back();
}
inline void addClaim(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 160);
    if (game_index::fold(line).size() < 6) return;
    const auto folded = game_index::fold(line);
    std::erase_if(self.claims, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.claims.push_back({now, std::move(line)});
    if (self.claims.size() > claim_limit) self.claims.erase(self.claims.begin(), self.claims.end() - static_cast<std::ptrdiff_t>(claim_limit));
}
// How long from now an appraisal's "do" means.
inline uint64_t dueFrom(std::wstring_view when, uint64_t now) {
    const auto f = game_index::fold(when);
    if (f == L"rozmowa" || f == L"teraz" || f == L"conversation" || f == L"now") return now + 3 * hour;
    if (f == L"dzis" || f == L"dzisiaj" || f == L"today" || f == L"tonight" || f == L"wieczorem") return now + 14 * hour;
    if (f == L"tydzien" || f == L"week" || f == L"weekend") return now + 7 * day;
    if (f == L"miesiac" || f == L"month") return now + 30 * day;
    return now + 40 * hour; // "jutro", "tomorrow", anything else.
}
inline std::string sourceOf(std::wstring_view word) {
    const auto f = game_index::fold(word);
    if (f.starts_with(L"zauwaz") || f.starts_with(L"widzia") || f.starts_with(L"wiem") || f == L"observed" || f == L"saw" || f == L"seen") return "observed";
    if (f.starts_with(L"przypuszcz") || f.starts_with(L"zgadu") || f.starts_with(L"domysl") || f == L"guess" || f == L"inferred" || f == L"guessed") return "inferred";
    return "told";
}
inline uint64_t textHash(std::wstring_view text, uint64_t salt) {
    uint64_t hash = 1469598103934665603ull ^ salt;
    for (const wchar_t ch : game_index::fold(text)) { hash ^= static_cast<uint64_t>(ch); hash *= 1099511628211ull; }
    return hash;
}
inline void addMilestone(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 180);
    if (game_index::fold(line).size() < 6) return;
    const auto folded = game_index::fold(line);
    std::erase_if(self.milestones, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.milestones.push_back({now, std::move(line)});
    if (self.milestones.size() > 60) self.milestones.erase(self.milestones.begin());
}
// A new own goal, from now: it takes one to three weeks.
inline void startOwnGoal(Self& self, std::wstring text, int source, uint64_t now) {
    Goal goal;
    goal.id = newId(self, 'g');
    goal.span = 7 + static_cast<int>(textHash(text, now / day) % 15);
    goal.text = std::move(text);
    goal.kind = Goal::Kind::own;
    goal.made = goal.updated = now;
    goal.source = source;
    self.goals.push_back(std::move(goal));
}
inline void finishOwnGoal(Self& self, Goal& goal, const Context& context, bool english, std::wstring_view result = {}) {
    goal.status = Goal::Status::done;
    goal.updated = context.now;
    self.drives.pride = clamp01(self.drives.pride + 0.08f);
    self.affect.valence = std::clamp(self.affect.valence + 0.15f, -1.0f, 1.0f);
    self.affect.at = context.now;
    // A few days before she takes up the next one.
    self.goal_rest = context.now + (3 + textHash(goal.text, context.now) % 5) * day;
    const std::wstring achieved = !result.empty() ? std::wstring(result) : (english ? L"I managed to " : L"Udało mi się: ") + goal.text;
    addMilestone(self, achieved, context.now);
    addClaim(self, achieved, context.now);
}
// Her days since `from` by her own clock (her timezone), else by UTC days.
inline int herDaysSince(uint64_t from, const Context& context) {
    if (context.character) {
        const auto a = localSeconds(*context.character, from), b = localSeconds(*context.character, context.now);
        if (a && b && b >= a) return static_cast<int>(b / day - a / day);
    }
    return context.now > from ? static_cast<int>(context.now / day - from / day) : 0;
}
inline std::wstring percent(float p) { return std::to_wstring(static_cast<int>(std::lround(clamp01(p) * 100))) + L"%"; }

inline constexpr size_t episode_limit = 60;
// An episode's id: the same for the same moment and deed, so a shared one matches in both minds.
inline std::string episodeId(std::wstring_view what, uint64_t at) {
    uint64_t value = textHash(what, at / 60);
    std::string out = "e";
    for (int i = 0; i < 6; ++i) { out += "0123456789abcdefghijklmnopqrstuvwxyz"[value % 36]; value /= 36; }
    return out;
}
// One moment of her life: she does one thing at a time (under half an hour apart), and her diary
// restating a plan in other words, an hour or two off, is still that plan (most of its words).
inline bool sameMoment(const Episode& a, const Episode& b) {
    const uint64_t apart = a.at > b.at ? a.at - b.at : b.at - a.at;
    if (apart < 30 * 60) return true;
    if (apart > 3 * hour) return false;
    size_t shared = 0;
    const float part = chats::overlap(chats::searchWords(a.what, 16), chats::searchWords(b.what, 16), &shared);
    return part >= 0.5f && shared >= 2;
}
inline void keepEpisodes(Self& self) {
    const auto byTime = [](const Episode& a, const Episode& b) { return a.at < b.at; };
    std::stable_sort(self.episodes.begin(), self.episodes.end(), byTime);
    // One entry a moment: the one shared with another companion, told or opened stays; else the first.
    const auto rank = [](const Episode& e) { return (e.partner.empty() ? 0 : 4) + (e.told ? 2 : 0) + (e.peeked ? 1 : 0); };
    for (size_t i = 0; i < self.episodes.size(); ++i)
        for (size_t j = i + 1; j < self.episodes.size() && self.episodes[j].at <= self.episodes[i].at + 3 * hour;) {
            if (!sameMoment(self.episodes[i], self.episodes[j])) { ++j; continue; }
            if (rank(self.episodes[j]) > rank(self.episodes[i])) std::swap(self.episodes[i], self.episodes[j]);
            self.episodes.erase(self.episodes.begin() + static_cast<std::ptrdiff_t>(j));
        }
    std::stable_sort(self.episodes.begin(), self.episodes.end(), byTime);
    while (self.episodes.size() > episode_limit) {
        const auto old = std::find_if(self.episodes.begin(), self.episodes.end(), [](const Episode& e) { return e.state != Episode::State::planned; });
        self.episodes.erase(old != self.episodes.end() ? old : self.episodes.begin());
    }
}
inline bool withPlayer(const Context& context) {
    return context.role == RelationRole::romantic || context.role == RelationRole::secret_romance || context.stance == RelationStance::broken;
}
// "wczoraj wieczorem — z bratem, targ Enfants Rouges: zakupy na kolację".
inline std::wstring episodeLine(const Episode& e, const Context& context) {
    std::wstring out = episodeWhen(e, context);
    if (!e.with.empty()) out += L" — " + e.with;
    if (!e.where.empty()) out += (e.with.empty() ? L" — " : L", ") + e.where;
    out += L": " + e.what;
    return out;
}

inline constexpr size_t curious_limit = 16;
// The last question in her words ("Czekaj, chodzi ci o pracę czy o grę?"): its sentence, short.
inline std::wstring lastQuestion(std::wstring_view text) {
    const auto mark = text.rfind(L'?');
    if (mark == std::wstring_view::npos) return {};
    size_t from = 0;
    for (size_t i = mark; i-- > 0;)
        if (text[i] == L'.' || text[i] == L'!' || text[i] == L'?' || text[i] == L'\n' || text[i] == L'…') { from = i + 1; break; }
    auto question = clean(text.substr(from, mark - from + 1), 140);
    while (!question.empty() && (question.front() == L' ' || question.front() == L'-' || question.front() == L'—')) question.erase(0, 1);
    return game_index::fold(question).size() >= 3 ? question : std::wstring{};
}
// She asks now: the question she set aside earlier when her urge brought it back, else a new act.
// What her urge carried comes true with it: a follow-up asked, an unanswered question returned to,
// a thread taken up (a set-aside question becomes the act itself).
inline CuriousAct& askNow(Self& self, CuriousMove move, std::wstring about, std::wstring why, uint64_t now, bool english) {
    const auto& urge = self.urge;
    if (urge.carry == CuriousCarry::follow_up) why = (english ? L"she remembered what you had coming up: " : L"pamiętała, co ‹miałeś|miałaś›: ") + urge.carried;
    else if (urge.carry == CuriousCarry::returning) why = english ? L"back to a question you left unanswered" : L"wraca do pytania, na które nie ‹odpowiedziałeś|odpowiedziałaś›";
    else if (urge.carry == CuriousCarry::thread) why = english ? L"back to a thread you started" : L"wraca do wątku, który ‹zacząłeś|zaczęłaś›";
    if (urge.carry == CuriousCarry::question && !urge.carry_id.empty())
        for (auto& c : self.curious)
            if (c.id == urge.carry_id && c.deferred()) {
                c.at = now;
                if (!about.empty()) c.about = std::move(about);
                return c;
            }
    if (urge.carry == CuriousCarry::follow_up)
        for (auto& f : self.follow_ups)
            if (f.id == urge.carry_id && f.state == 0) { f.state = 1; f.settled = now; }
    if (urge.carry == CuriousCarry::returning)
        for (auto& c : self.curious)
            if (c.id == urge.carry_id) c.returned = true;
    if (urge.carry == CuriousCarry::thread)
        for (auto& t : self.threads)
            if (t.text == urge.carried) t.used = true;
    CuriousAct act;
    act.id = newId(self, 'c');
    act.move = move;
    act.about = std::move(about);
    act.why = std::move(why);
    act.at = now;
    self.curious.push_back(std::move(act));
    return self.curious.back();
}
inline void keepCurious(Self& self) {
    while (self.curious.size() > curious_limit) {
        const auto old = std::find_if(self.curious.begin(), self.curious.end(), [](const CuriousAct& c) { return c.outcome != -1; });
        self.curious.erase(old != self.curious.end() ? old : self.curious.begin());
    }
}
// Her bids' ledger fades: half of it in a week.
inline void fadeBids(Bids& bids, uint64_t now) {
    if (bids.at && now > bids.at) {
        const float keep = static_cast<float>(std::pow(0.5, static_cast<double>(now - bids.at) / (7.0 * day)));
        bids.toward *= keep; bids.away *= keep; bids.against *= keep;
    }
    bids.at = now;
}
// The first sentence of her words (a reflection has no question mark), short.
inline std::wstring firstSentence(std::wstring_view text) {
    const auto end = text.find_first_of(L".!?…\n");
    auto sentence = clean(text.substr(0, end == std::wstring_view::npos ? text.size() : end + 1), 140);
    return game_index::fold(sentence).size() >= 3 ? sentence : std::wstring{};
}
// Her local wall-clock seconds at `at` (UTC when her timezone is unknown).
inline int64_t localAt(const Context& context, uint64_t at) {
    if (context.character)
        if (const auto local = localSeconds(*context.character, at)) return static_cast<int64_t>(local);
    return static_cast<int64_t>(at);
}
} // namespace companion::mind::detail

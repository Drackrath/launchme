#include "companion_mind.hpp"
#include "companion_affinity.hpp"
#include "companion_chats.hpp"
#include "json.hpp"
#include "../library/game_index.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <tuple>
#include <utility>

namespace companion::mind {
namespace {
constexpr size_t expectation_limit = 40, goal_limit = 30, claim_limit = 120, marker_limit = 60;
constexpr uint64_t hour = 3600, day = 86400;

std::wstring clean(std::wstring_view text, size_t max_chars) {
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
std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
std::string newId(Self& self, char prefix) { return std::string(1, prefix) + std::to_string(self.next_id++); }
std::string bareId(std::string id) {
    while (!id.empty() && (id.front() == '#' || id.front() == ' ')) id.erase(0, 1);
    return id;
}
// The folded words of a message, each between spaces (" kocham cie "), for phrase matching.
std::wstring spaced(std::wstring_view text) {
    std::wstring out = L" ", word;
    const auto flush = [&] { if (!word.empty()) out += game_index::fold(word) + L" "; word.clear(); };
    for (const wchar_t ch : text) {
        if (std::iswalnum(ch)) word += ch;
        else flush();
    }
    flush();
    return out;
}
bool hasAny(const std::wstring& haystack, std::initializer_list<const wchar_t*> phrases) {
    for (const auto* phrase : phrases)
        if (haystack.find(std::wstring(L" ") + phrase + L" ") != std::wstring::npos) return true;
    return false;
}
bool hasRaw(std::wstring_view text, std::initializer_list<const wchar_t*> marks) {
    for (const auto* mark : marks)
        if (text.find(mark) != std::wstring_view::npos) return true;
    return false;
}
Marker* markerOf(Self& self, std::wstring_view subject) {
    const auto folded = game_index::fold(subject);
    if (folded.size() < 2) return nullptr;
    auto found = std::find_if(self.markers.begin(), self.markers.end(), [&](const Marker& m) { return game_index::fold(m.subject) == folded; });
    if (found != self.markers.end()) return &*found;
    if (self.markers.size() >= marker_limit) // The faintest gives way.
        self.markers.erase(std::min_element(self.markers.begin(), self.markers.end(), [](const Marker& a, const Marker& b) { return std::abs(a.valence) < std::abs(b.valence); }));
    self.markers.push_back({std::wstring(subject), 0, 0});
    return &self.markers.back();
}
void addClaim(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 160);
    if (game_index::fold(line).size() < 6) return;
    const auto folded = game_index::fold(line);
    std::erase_if(self.claims, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.claims.push_back({now, std::move(line)});
    if (self.claims.size() > claim_limit) self.claims.erase(self.claims.begin(), self.claims.end() - static_cast<std::ptrdiff_t>(claim_limit));
}
// How long from now an appraisal's "do" means.
uint64_t dueFrom(std::wstring_view when, uint64_t now) {
    const auto f = game_index::fold(when);
    if (f == L"rozmowa" || f == L"teraz" || f == L"conversation" || f == L"now") return now + 3 * hour;
    if (f == L"dzis" || f == L"dzisiaj" || f == L"today" || f == L"tonight" || f == L"wieczorem") return now + 14 * hour;
    if (f == L"tydzien" || f == L"week" || f == L"weekend") return now + 7 * day;
    if (f == L"miesiac" || f == L"month") return now + 30 * day;
    return now + 40 * hour; // "jutro", "tomorrow", anything else.
}
std::string sourceOf(std::wstring_view word) {
    const auto f = game_index::fold(word);
    if (f.starts_with(L"zauwaz") || f.starts_with(L"widzia") || f.starts_with(L"wiem") || f == L"observed" || f == L"saw" || f == L"seen") return "observed";
    if (f.starts_with(L"przypuszcz") || f.starts_with(L"zgadu") || f.starts_with(L"domysl") || f == L"guess" || f == L"inferred" || f == L"guessed") return "inferred";
    return "told";
}
uint64_t textHash(std::wstring_view text, uint64_t salt) {
    uint64_t hash = 1469598103934665603ull ^ salt;
    for (const wchar_t ch : game_index::fold(text)) { hash ^= static_cast<uint64_t>(ch); hash *= 1099511628211ull; }
    return hash;
}
void addMilestone(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 180);
    if (game_index::fold(line).size() < 6) return;
    const auto folded = game_index::fold(line);
    std::erase_if(self.milestones, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.milestones.push_back({now, std::move(line)});
    if (self.milestones.size() > 60) self.milestones.erase(self.milestones.begin());
}
// A new own goal, from now: it takes one to three weeks.
void startOwnGoal(Self& self, std::wstring text, int source, uint64_t now) {
    Goal goal;
    goal.id = newId(self, 'g');
    goal.span = 7 + static_cast<int>(textHash(text, now / day) % 15);
    goal.text = std::move(text);
    goal.kind = Goal::Kind::own;
    goal.made = goal.updated = now;
    goal.source = source;
    self.goals.push_back(std::move(goal));
}
void finishOwnGoal(Self& self, Goal& goal, const Context& context, bool english, std::wstring_view result = {}) {
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
int herDaysSince(uint64_t from, const Context& context) {
    if (context.character) {
        const auto a = localSeconds(*context.character, from), b = localSeconds(*context.character, context.now);
        if (a && b && b >= a) return static_cast<int>(b / day - a / day);
    }
    return context.now > from ? static_cast<int>(context.now / day - from / day) : 0;
}
std::wstring percent(float p) { return std::to_wstring(static_cast<int>(std::lround(clamp01(p) * 100))) + L"%"; }

constexpr size_t episode_limit = 60;
// An episode's id: the same for the same moment and deed, so a shared one matches in both minds.
std::string episodeId(std::wstring_view what, uint64_t at) {
    uint64_t value = textHash(what, at / 60);
    std::string out = "e";
    for (int i = 0; i < 6; ++i) { out += "0123456789abcdefghijklmnopqrstuvwxyz"[value % 36]; value /= 36; }
    return out;
}
// One moment of her life: she does one thing at a time (under half an hour apart), and her diary
// restating a plan in other words, an hour or two off, is still that plan (most of its words).
bool sameMoment(const Episode& a, const Episode& b) {
    const uint64_t apart = a.at > b.at ? a.at - b.at : b.at - a.at;
    if (apart < 30 * 60) return true;
    if (apart > 3 * hour) return false;
    size_t shared = 0;
    const float part = chats::overlap(chats::searchWords(a.what, 16), chats::searchWords(b.what, 16), &shared);
    return part >= 0.5f && shared >= 2;
}
void keepEpisodes(Self& self) {
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
bool withPlayer(const Context& context) {
    return context.role == RelationRole::romantic || context.role == RelationRole::secret_romance || context.stance == RelationStance::broken;
}
// "wczoraj wieczorem — z bratem, targ Enfants Rouges: zakupy na kolację".
std::wstring episodeLine(const Episode& e, const Context& context) {
    std::wstring out = episodeWhen(e, context);
    if (!e.with.empty()) out += L" — " + e.with;
    if (!e.where.empty()) out += (e.with.empty() ? L" — " : L", ") + e.where;
    out += L": " + e.what;
    return out;
}

constexpr size_t curious_limit = 16;
// The last question in her words ("Czekaj, chodzi ci o pracę czy o grę?"): its sentence, short.
std::wstring lastQuestion(std::wstring_view text) {
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
CuriousAct& askNow(Self& self, CuriousMove move, std::wstring about, std::wstring why, uint64_t now, bool english) {
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
void keepCurious(Self& self) {
    while (self.curious.size() > curious_limit) {
        const auto old = std::find_if(self.curious.begin(), self.curious.end(), [](const CuriousAct& c) { return c.outcome != -1; });
        self.curious.erase(old != self.curious.end() ? old : self.curious.begin());
    }
}
// Her bids' ledger fades: half of it in a week.
void fadeBids(Bids& bids, uint64_t now) {
    if (bids.at && now > bids.at) {
        const float keep = static_cast<float>(std::pow(0.5, static_cast<double>(now - bids.at) / (7.0 * day)));
        bids.toward *= keep; bids.away *= keep; bids.against *= keep;
    }
    bids.at = now;
}
// The words of a message, between spaces, with their count.
size_t wordCount(const std::wstring& spaced_words) {
    size_t count = 0;
    for (size_t i = 1; i < spaced_words.size(); ++i)
        if (spaced_words[i] == L' ' && spaced_words[i - 1] != L' ') ++count;
    return count;
}
// The first sentence of her words (a reflection has no question mark), short.
std::wstring firstSentence(std::wstring_view text) {
    const auto end = text.find_first_of(L".!?…\n");
    auto sentence = clean(text.substr(0, end == std::wstring_view::npos ? text.size() : end + 1), 140);
    return game_index::fold(sentence).size() >= 3 ? sentence : std::wstring{};
}
} // namespace

Attachment attachmentOf(const Context& context) {
    if (context.character) {
        const auto& a = context.character->attachment;
        if (a == "secure") return Attachment::secure;
        if (a == "anxious") return Attachment::anxious;
        if (a == "avoidant") return Attachment::avoidant;
        if (a == "fearful") return Attachment::fearful;
    }
    switch (context.persona) {
        case Persona::yandere: case Persona::dandere: return Attachment::anxious;
        case Persona::kuudere: case Persona::himedere: case Persona::kanedere: return Attachment::avoidant;
        case Persona::tsundere: case Persona::kichidere: return Attachment::fearful;
        default: return Attachment::secure;
    }
}

std::wstring attachmentName(Attachment attachment, bool english) {
    switch (attachment) {
        case Attachment::anxious: return english ? L"anxious" : L"lękowy";
        case Attachment::avoidant: return english ? L"avoidant" : L"unikający";
        case Attachment::fearful: return english ? L"fearful-avoidant" : L"lękowo-unikający";
        default: return english ? L"secure" : L"bezpieczny";
    }
}

uint64_t eventDue(std::wstring_view when, uint64_t now) {
    const auto w = spaced(when);
    // Today and the days ahead by name (UTC days; good enough to ask "how did it go" after it).
    const int today = static_cast<int>((now / day + 4) % 7); // 1970-01-01 was a Thursday; 0 = Sunday.
    static constexpr std::array<std::array<const wchar_t*, 4>, 7> names{{
        {L"niedziela", L"niedziele", L"sunday", L"sun"}, {L"poniedzialek", L"poniedzialku", L"monday", L"mon"},
        {L"wtorek", L"wtorku", L"tuesday", L"tue"}, {L"sroda", L"srode", L"wednesday", L"wed"},
        {L"czwartek", L"czwartku", L"thursday", L"thu"}, {L"piatek", L"piatku", L"friday", L"fri"},
        {L"sobota", L"sobote", L"saturday", L"sat"}}};
    for (int d = 0; d < 7; ++d)
        for (const auto* name : names[static_cast<size_t>(d)])
            if (w.find(std::wstring(L" ") + name + L" ") != std::wstring::npos) {
                int ahead = (d - today + 7) % 7;
                if (ahead == 0) ahead = 7;
                return now + static_cast<uint64_t>(ahead) * day + 6 * hour;
            }
    if (hasAny(w, {L"dzis", L"dzisiaj", L"wieczorem", L"za chwile", L"zaraz", L"today", L"tonight", L"later", L"soon"})) return now + 5 * hour;
    if (hasAny(w, {L"pojutrze", L"day after tomorrow"})) return now + 2 * day + 6 * hour;
    if (hasAny(w, {L"jutro", L"tomorrow"})) return now + day + 6 * hour;
    if (hasAny(w, {L"weekend", L"w weekend", L"weekendzie", L"this weekend"})) {
        const int to_sunday = (7 - today) % 7;
        return now + static_cast<uint64_t>(to_sunday == 0 ? 7 : to_sunday) * day + 6 * hour;
    }
    if (hasAny(w, {L"za tydzien", L"w przyszlym tygodniu", L"next week", L"in a week"})) return now + 7 * day;
    if (hasAny(w, {L"za miesiac", L"w przyszlym miesiacu", L"next month", L"in a month"})) return now + 30 * day;
    return now + 2 * day;
}

Affect baseline(Persona persona) {
    static constexpr std::array<std::pair<float, float>, persona_count> table{{
        {0.4f, 0.7f},   // genki
        {0.1f, 0.6f},   // tsundere
        {0.3f, 0.5f},   // yandere
        {0.0f, 0.2f},   // kuudere
        {-0.1f, 0.35f}, // dandere
        {0.5f, 0.6f},   // deredere
        {0.2f, 0.5f},   // himedere
        {0.0f, 0.75f},  // kichidere
        {0.0f, 0.25f},  // kanedere
    }};
    const auto [v, a] = table[static_cast<size_t>(persona) % table.size()];
    return {v, a, 0};
}

namespace {
Affect settleTo(const Affect& affect, const Affect& base, uint64_t now) {
    if (!affect.at) return {base.valence, base.arousal, now};
    if (now <= affect.at) return affect;
    const double keep = std::pow(0.5, static_cast<double>(now - affect.at) / (4.0 * hour));
    return {static_cast<float>(base.valence + (affect.valence - base.valence) * keep), static_cast<float>(base.arousal + (affect.arousal - base.arousal) * keep), now};
}
// Her local wall-clock seconds at `at` (UTC when her timezone is unknown).
int64_t localAt(const Context& context, uint64_t at) {
    if (context.character)
        if (const auto local = localSeconds(*context.character, at)) return static_cast<int64_t>(local);
    return static_cast<int64_t>(at);
}
} // namespace

Affect settled(const Affect& affect, Persona persona, uint64_t now) { return settleTo(affect, baseline(persona), now); }

Tone lifeTone(const Life& life) {
    Tone tone;
    const auto add = [&](float valence, float arousal, float cap) {
        tone.valence += valence;
        tone.arousal += arousal;
        tone.energy = std::min(tone.energy, cap);
    };
    switch (life.health) {
    case LifeHealth::tired: add(-0.05f, -0.1f, 0.6f); break;
    case LifeHealth::sick: add(-0.2f, -0.2f, 0.4f); break;
    case LifeHealth::injured: add(-0.15f, -0.05f, 0.55f); break;
    case LifeHealth::recovering: add(-0.05f, -0.05f, 0.75f); break;
    case LifeHealth::fit: break;
    }
    switch (life.work) {
    case LifeWork::busy: add(0.0f, 0.1f, 0.75f); break;
    case LifeWork::between_jobs: add(-0.1f, 0.05f, 1.0f); break;
    case LifeWork::promoted: add(0.2f, 0.1f, 1.0f); break;
    case LifeWork::burnout: add(-0.25f, -0.15f, 0.45f); break;
    case LifeWork::steady: break;
    }
    switch (life.wealth) {
    case LifeWealth::tight: add(-0.05f, 0.0f, 1.0f); break;
    case LifeWealth::broke: add(-0.15f, 0.05f, 1.0f); break;
    case LifeWealth::flush: add(0.15f, 0.05f, 1.0f); break;
    case LifeWealth::comfortable: break;
    }
    switch (life.family) {
    case LifeFamily::visit: add(0.05f, 0.1f, 1.0f); break;
    case LifeFamily::tension: add(-0.15f, 0.15f, 1.0f); break;
    case LifeFamily::homesick: add(-0.15f, -0.05f, 1.0f); break;
    case LifeFamily::calm: break;
    }
    switch (life.social) {
    case LifeSocial::lonely: add(-0.1f, -0.05f, 1.0f); break;
    case LifeSocial::overwhelmed: add(-0.05f, 0.15f, 0.6f); break;
    case LifeSocial::inspired: add(0.15f, 0.15f, 1.0f); break;
    case LifeSocial::balanced: break;
    }
    tone.valence = std::clamp(tone.valence, -0.45f, 0.35f);
    tone.arousal = std::clamp(tone.arousal, -0.3f, 0.3f);
    return tone;
}

Affect baseline(const Context& context) {
    auto base = baseline(context.persona);
    const auto tone = lifeTone(context.life);
    base.valence = std::clamp(base.valence + tone.valence, -1.0f, 1.0f);
    base.arousal = clamp01(base.arousal + tone.arousal);
    return base;
}

Affect settled(const Affect& affect, const Context& context) { return settleTo(affect, baseline(context), context.now); }

namespace {
// One night of hers (her noon to noon): its hour shifts and whether she sleeps, the same all night.
uint64_t nightSeed(std::string_view look, int64_t night) {
    uint64_t hash = 1469598103934665603ull ^ (static_cast<uint64_t>(night) * 0x9E3779B97F4A7C15ull);
    for (const char ch : look) { hash ^= static_cast<unsigned char>(ch); hash *= 1099511628211ull; }
    hash ^= hash >> 31; hash *= 0xBF58476D1CE4E5B9ull; hash ^= hash >> 29;
    return hash;
}
} // namespace

std::optional<Night> nightAt(const Context& context) {
    if (!context.character || !context.now) return std::nullopt;
    const auto local_now = localSeconds(*context.character, context.now);
    if (!local_now) return std::nullopt;
    const int64_t local = static_cast<int64_t>(local_now);
    const int64_t offset = local - static_cast<int64_t>(context.now - context.now % 60); // Her clock against UTC (whole minutes).
    const int64_t one_day = static_cast<int64_t>(day), half = one_day / 2;
    const bool owl = context.character->night_owl;
    // From her noon: in bed at 23:00 (an owl 2:00), up at 7:00 (10:00), each ±30 min.
    const auto night = [&](int64_t index) {
        const uint64_t seed = nightSeed(context.look, index);
        const int64_t noon = index * one_day + half;
        const int64_t bed = noon + (owl ? 14 : 11) * static_cast<int64_t>(hour) + (static_cast<int64_t>(seed % 61) - 30) * 60;
        const int64_t up = noon + (owl ? 22 : 19) * static_cast<int64_t>(hour) + (static_cast<int64_t>((seed >> 12) % 61) - 30) * 60;
        return std::tuple{bed, up, (seed >> 24) % 1000 < 15};
    };
    int64_t index = (local - half) / one_day;
    auto [bed, up, sleepless] = night(index);
    if (local >= up) std::tie(bed, up, sleepless) = night(++index); // An owl's late morning: that night is over.
    if (bed - offset <= 0) return std::nullopt;
    return Night{static_cast<uint64_t>(bed - offset), static_cast<uint64_t>(up - offset), sleepless};
}

bool asleep(const Context& context) {
    const auto night = nightAt(context);
    return night && !night->sleepless && context.now >= night->from && context.now < night->to;
}

bool awake(const Context& context) { return !asleep(context); }

std::wstring describe(const Affect& affect, bool english) {
    const float v = affect.valence, a = affect.arousal;
    std::wstring mood;
    if (v <= -0.12f && a >= 0.65f) mood = english ? (v <= -0.5f ? L"really irritated" : L"irritated") : (v <= -0.5f ? L"bardzo rozdrażniona" : L"rozdrażniona");
    else if (v <= -0.6f) mood = english ? L"very low" : L"bardzo przygnębiona";
    else if (v <= -0.3f) mood = english ? L"low" : L"przygnębiona";
    else if (v <= -0.12f) mood = english ? L"a little down" : L"lekko przygaszona";
    else if (v < 0.12f) mood = english ? L"in a neutral mood" : L"w neutralnym nastroju";
    else if (v < 0.3f) mood = english ? L"in a decent mood" : L"w niezłym humorze";
    else if (v < 0.6f) mood = english ? L"in a good mood" : L"w dobrym humorze";
    else mood = english ? L"in a great mood" : L"w świetnym humorze";
    std::wstring energy;
    if (a < 0.2f) energy = english ? L"quiet and drowsy" : L"wyciszona i senna";
    else if (a < 0.4f) energy = english ? L"calm" : L"spokojna";
    else if (a >= 0.85f) energy = english ? L"very wound up" : L"bardzo nakręcona";
    else if (a >= 0.65f && !(v <= -0.12f)) energy = english ? L"lively" : L"ożywiona";
    return energy.empty() ? mood : mood + L", " + energy;
}

Self& State::of(std::string_view look) {
    const auto found = selves.find(look);
    if (found != selves.end()) return found->second;
    return selves.emplace(std::string(look), Self{}).first->second;
}

const Self* State::find(std::string_view look) const {
    const auto found = selves.find(look);
    return found == selves.end() ? nullptr : &found->second;
}

float energyNow(const Self& self, const Context& context) {
    // Sapolsky (Behave): accumulated sleep debt from late-night talks lowers energy even the next afternoon.
    float energy = self.drives.energy - 0.45f * clamp01(self.sleep_debt) - 0.15f * clamp01(self.stress_load);
    if (context.character) {
        if (const auto clock = localClock(*context.character, context.now)) {
            const int h = clock->hour;
            if (context.character->night_owl) energy -= h >= 7 && h < 12 ? 0.35f : h >= 5 && h < 7 ? 0.15f : 0.0f;
            else energy -= h >= 0 && h < 6 ? 0.35f : h >= 22 ? 0.15f : 0.0f;
        }
    }
    return clamp01(std::min(energy, lifeTone(context.life).energy));
}

float cognitiveControl(const Self& self, const Context& context) {
    const float energy = energyNow(self, context);
    const float arousal = settled(self.affect, context).arousal;
    // HPA-axis cortisol load (Sapolsky) further impairs prefrontal vmPFC top-down inhibition.
    return clamp01(energy * (1.0f - 0.55f * arousal - 0.22f * clamp01(self.stress_load)));
}

bool isFirstMeeting(const Self& self, const Memory& memory, std::string_view look, int64_t affinity_points) {
    if (affinity_points > 0 || self.last_talk > 0 || !self.claims.empty()) return false;
    for (const auto& s : memory.summaries)
        if (!s.look.empty() && s.look == look) return false;
    for (const auto& n : memory.notes)
        if (!n.look.empty() && n.look == look) return false;
    return true;
}

bool readyToConfess(const Untruth& untruth, const Self& self, const Context& context) {
    if (untruth.confessed || context.now <= untruth.at) return false;
    if (context.trust >= untruth.trust_at + 8) return true;
    if (context.trust >= 72 && context.now >= untruth.at + 2 * hour) return true;
    if (untruth.feeling == "guilt" && context.now >= untruth.at + 6 * hour) return true;
    if (cognitiveControl(self, context) < 0.42f) return true;
    return false;
}

bool needsDeliberation(const Self& self, const Context& context) {
    if (self.peek_caught || context.tension >= 55 || context.stance == RelationStance::dilemma) return true;
    if (!self.subtext_note.empty() && context.now <= self.subtext_at + 15 * 60) return true;
    for (const auto& u : self.untruths)
        if (readyToConfess(u, self, context)) return true;
    for (const auto& e : self.expectations) {
        if ((e.outcome == 0 || e.outcome == 1) && context.now <= e.settled + 12 * hour) {
            const float surprise = e.outcome == 1 ? 1.0f - e.p : e.p;
            if (surprise >= 0.6f) return true;
        }
    }
    return false;
}

void tick(Self& self, const Context& context) {
    const uint64_t now = context.now;
    self.affect = settled(self.affect, context);
    auto& d = self.drives;
    if (d.at && now > d.at) {
        const double hours = std::min<double>(static_cast<double>(now - d.at) / hour, 30.0 * 24);
        const bool apart = !self.last_talk || now > self.last_talk + 2 * hour;
        // Missing him grows faster for an anxious attachment, slower for an avoidant one.
        const auto attachment = attachmentOf(context);
        const double missing = attachment == Attachment::anxious ? 1.4 : attachment == Attachment::fearful ? 1.2 : attachment == Attachment::avoidant ? 0.6 : 1.0;
        if (apart) d.bond = clamp01(d.bond + static_cast<float>(hours / 72.0 * 0.6 * missing));
        d.energy = clamp01(d.energy + static_cast<float>(hours * 0.25));
        // Curiosity comes back by itself: halfway to her character's own level in two hours (it never
        // stays spent for days), and grows a little beyond it while they are apart.
        {
            float base = 0.5f;
            switch (context.persona) {
                case Persona::genki: case Persona::yandere: base = 0.6f; break;
                case Persona::kichidere: base = 0.65f; break;
                case Persona::deredere: base = 0.55f; break;
                case Persona::dandere: case Persona::tsundere: base = 0.45f; break;
                case Persona::kuudere: case Persona::himedere: case Persona::kanedere: base = 0.4f; break;
            }
            if (d.curiosity < base) d.curiosity = base - (base - d.curiosity) * static_cast<float>(std::pow(0.5, hours / 2.0));
            if (apart) d.curiosity = clamp01(d.curiosity + static_cast<float>(hours / 24.0 * 0.1));
        }
        const auto drift = [&](float& value, float to, double per_day) {
            const double keep = std::pow(1.0 - per_day, hours / 24.0);
            value = static_cast<float>(to + (value - to) * keep);
        };
        drift(d.pride, 0.5f, 0.1);
        drift(d.autonomy, 0.3f, 0.2);
        // Panksepp's PLAY circuit decays over ~1.5 hours between exchanges; Sapolsky's HPA stress_load
        // decays slowly over ~24 hours, and sleep_debt clears only after a long uninterrupted rest (>= 6h apart).
        self.play_momentum = static_cast<float>(self.play_momentum * std::pow(0.5, hours / 1.5));
        if (self.play_momentum < 0.03f) self.play_momentum = 0.0f;
        self.stress_load = static_cast<float>(self.stress_load * std::pow(0.5, hours / 24.0));
        if (self.stress_load < 0.03f) self.stress_load = 0.0f;
        if (!self.last_talk || now >= self.last_talk + 6 * hour) {
            self.sleep_debt = clamp01(self.sleep_debt - static_cast<float>(hours / 16.0));
        }
    }
    d.at = now;

    // Her own goal: one at a time, slowly (one to three weeks, its stages at its thirds), a few
    // days' rest between. Next: one she set herself, else a starting goal of her character's not
    // yet taken up; after those she has none until she sets one (in a talk or her diary).
    const bool english = context.english;
    const auto* character = context.character;
    const auto& seeds = character ? character->goals(english) : std::vector<std::wstring>{};
    const auto openOwn = [&] {
        return std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
    };
    auto open_own = openOwn();
    if (open_own == self.goals.end() && now >= self.goal_rest) {
        if (!self.aspirations.empty()) {
            auto next = std::move(self.aspirations.front());
            self.aspirations.erase(self.aspirations.begin());
            startOwnGoal(self, std::move(next), -1, now);
        } else {
            for (int index = 0; index < static_cast<int>(seeds.size()); ++index) {
                if (std::find(self.seeds_used.begin(), self.seeds_used.end(), index) != self.seeds_used.end()) continue;
                self.seeds_used.push_back(index);
                startOwnGoal(self, seeds[static_cast<size_t>(index)], index, now);
                break;
            }
        }
        open_own = openOwn();
    }
    if (open_own != self.goals.end()) {
        if (open_own->source >= 0 && static_cast<size_t>(open_own->source) < seeds.size()) open_own->text = seeds[static_cast<size_t>(open_own->source)];
        if (open_own->span <= 0) open_own->span = 7 + static_cast<int>(textHash(open_own->text, open_own->made / day) % 15);
        const int days = herDaysSince(open_own->made, context);
        open_own->stage = std::clamp(days * 3 / open_own->span, 0, 2);
        if (days >= open_own->span) finishOwnGoal(self, *open_own, context, english);
    }
    for (auto& goal : self.goals) {
        if (goal.status != Goal::Status::open) continue;
        if (goal.kind == Goal::Kind::promise && goal.due && now > goal.due + 3 * day) {
            goal.status = Goal::Status::dropped; // Forgotten: a little guilt.
            goal.updated = now;
            self.affect.valence = std::clamp(self.affect.valence - 0.05f, -1.0f, 1.0f);
        }
        if (goal.kind == Goal::Kind::intention && now > goal.made + 7 * day) { goal.status = Goal::Status::dropped; goal.updated = now; }
    }
    std::erase_if(self.goals, [&](const Goal& g) { return g.status != Goal::Status::open && now > g.updated + 14 * day; });
    while (self.goals.size() > goal_limit) {
        const auto old = std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.status != Goal::Status::open; });
        self.goals.erase(old != self.goals.end() ? old : self.goals.begin());
    }

    // Expectations past their time.
    for (auto& e : self.expectations) {
        if (e.outcome != -1 || !e.due || now <= e.due) continue;
        e.settled = now;
        if (e.presence) {
            e.outcome = 0;
            d.bond = clamp01(d.bond + 0.1f);
            self.affect.valence = std::clamp(self.affect.valence - 0.08f * e.p, -1.0f, 1.0f);
        } else if (!e.game.empty()) {
            e.outcome = 0;
            if (e.suggestion) d.pride = clamp01(d.pride - 0.05f);
        } else e.outcome = 2;
    }
    std::erase_if(self.expectations, [&](const Expectation& e) { return e.outcome != -1 && now > e.settled + 7 * day; });
    while (self.expectations.size() > expectation_limit) {
        const auto old = std::find_if(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.outcome != -1; });
        self.expectations.erase(old != self.expectations.end() ? old : self.expectations.begin());
    }

    // Her plans whose time has come: they happened, or (now and then) fell through — more often
    // when she is unwell or swamped. A shared one goes the same way in both minds (its id decides).
    for (auto& e : self.episodes) {
        if (e.state != Episode::State::planned || e.at > now) continue;
        int chance = 12;
        if (e.partner.empty()) {
            if (context.life.health == LifeHealth::sick || context.life.health == LifeHealth::injured) chance += 25;
            else if (context.life.work == LifeWork::busy || context.life.health == LifeHealth::tired) chance += 10;
        }
        e.state = static_cast<int>(textHash(wide(e.id), 7) % 100) < chance ? Episode::State::fell_through : Episode::State::happened;
        if (e.state == Episode::State::fell_through) {
            // Causal life arc: a failed shared episode causes inter-companion friction; a failed
            // work/study or family episode leaves a real downstream consequence.
            if (!e.partner.empty() && std::find(self.failed_partners.begin(), self.failed_partners.end(), e.partner) == self.failed_partners.end())
                self.failed_partners.push_back(e.partner);
            const auto words = spaced(e.what + L" " + e.where + L" " + e.with);
            if (hasAny(words, {L"praca", L"pracy", L"zmiana", L"zlecenie", L"uczelnia", L"uczelni", L"wyklad", L"egzamin", L"warsztat", L"butik",
                               L"work", L"shift", L"class", L"exam", L"study", L"project"})) {
                self.affect.valence = std::clamp(self.affect.valence - 0.08f, -1.0f, 1.0f);
                self.affect.arousal = clamp01(self.affect.arousal + 0.08f);
                self.affect.at = now;
                self.stress_load = clamp01(self.stress_load + 0.18f);
            } else if (hasAny(words, {L"rodzina", L"rodzice", L"brat", L"bratem", L"siostra", L"mama", L"tata", L"family", L"parents", L"brother", L"sister"})) {
                self.affect.valence = std::clamp(self.affect.valence - 0.1f, -1.0f, 1.0f);
                self.affect.at = now;
                self.stress_load = clamp01(self.stress_load + 0.16f);
            }
        }
    }
    std::erase_if(self.episodes, [&](const Episode& e) { return e.state != Episode::State::planned && now > e.at + 30 * day; });
    std::erase_if(self.untruths, [&](const Untruth& u) { return u.confessed && now > u.at + 30 * day; });
    // Her curiosity: a question he never answered goes by; one set aside too long is let go.
    for (auto& c : self.curious) {
        if (c.outcome != -1) continue;
        if (c.at && now > c.at + 6 * hour) { c.outcome = 0; c.settled = now; }
        else if (!c.at && now > c.later_at + 3 * day) { c.outcome = 2; c.settled = now; }
    }
    std::erase_if(self.curious, [&](const CuriousAct& c) { return c.outcome != -1 && now > c.settled + 7 * day; });
    // His follow-ups: past their time she lets them go; threads of his fade within a day.
    for (auto& f : self.follow_ups)
        if (f.state == 0 && now > f.due + 4 * day) { f.state = 2; f.settled = now; }
    std::erase_if(self.follow_ups, [&](const FollowUp& f) { return f.state != 0 && now > f.settled + 14 * day; });
    std::erase_if(self.threads, [&](const Thread& t) { return now > t.at + day; });
    fadeBids(self.bids, now);

    // DMN (Default Mode Network): background rumination while apart when tension, guilt over a fib, or a broken expectation lingers.
    if (self.last_talk > 0 && now >= self.last_talk + 4 * hour && now >= self.last_rumination + 8 * hour) {
        std::wstring rumination;
        for (auto it = self.untruths.rbegin(); it != self.untruths.rend(); ++it) {
            if (!it->confessed && (it->feeling == "guilt" || it->reason == "shame" || it->reason == "care")) {
                rumination = english
                    ? L"While you were away, that little fib you told (" + it->said + L" — when really: " + it->truth + L") kept bothering your conscience."
                    : L"Gdy ‹go|jej› nie było, cały czas gryzło cię to drobne kłamstwo („" + it->said + L"”, chociaż naprawdę: „" + it->truth + L"”) i zastanawiałaś się, czy ‹mu|jej› się nie przyznać.";
                break;
            }
        }
        if (rumination.empty() && (context.stance == RelationStance::dilemma || context.tension >= 50)) {
            rumination = english ? L"While you were away, you kept turning over in your head what is happening between the two of you and why they make you hesitate."
                                 : L"Gdy ‹go|jej› nie było, cały czas mieliłaś w głowie to, co się między wami dzieje, i czemu tak się przy ‹nim|niej› wahasz.";
        } else if (rumination.empty()) {
            for (const auto& e : self.expectations) {
                if (e.outcome == 0 && e.settled >= self.last_talk && e.p >= 0.55f) {
                    rumination = english ? L"In the quiet between talks, you wondered why it didn't go the way you expected: " + e.text
                                         : L"W ciszy między rozmowami zastanawiałaś się, czemu nie wyszło tak, jak się spodziewałaś: " + e.text;
                    break;
                }
            }
        }
        if (!rumination.empty()) {
            self.last_rumination = now;
            self.ruminations.push_back({now, std::move(rumination)});
            if (self.ruminations.size() > 8) self.ruminations.erase(self.ruminations.begin());
        }
    }
}

void onShift(Self& self, const Context& context, const Shift& shift) {
    if (!shift.any()) return;
    const bool en = context.english;
    const uint64_t now = context.now;
    float dv = 0, da = 0;
    const auto add = [&](float valence, float arousal) { dv += valence; da += arousal; };
    if (shift.stance) switch (*shift.stance) {
        case RelationStance::dilemma: add(-0.1f, 0.2f); break;
        case RelationStance::distanced:
            add(-0.2f, -0.1f);
            self.drives.autonomy = clamp01(self.drives.autonomy + 0.15f);
            break;
        case RelationStance::broken: {
            add(0.35f, 0.3f);
            const std::wstring ms = en ? L"I let my guard down — something more than friendship began between us"
                                       : L"Przełamałam się — między nami zaczęło się coś więcej niż przyjaźń";
            addMilestone(self, ms, now);
            addKeyMemory(self, ms, now);
            break;
        }
        case RelationStance::open: add(0.15f, 0.05f); break;
    }
    if (shift.role) switch (*shift.role) {
        case RelationRole::romantic: {
            add(0.25f, 0.2f);
            const std::wstring ms = en ? L"‹He|She|They› and I decided it is more than friendship" : L"‹Ustaliliśmy z nim|Ustaliłyśmy z nią›, że to coś więcej niż przyjaźń";
            addMilestone(self, ms, now);
            addKeyMemory(self, ms, now);
            break;
        }
        case RelationRole::secret_romance: {
            add(0.2f, 0.25f);
            const std::wstring ms = en ? L"A secret romance with ‹him|her|them› began" : L"Zaczął się mój sekretny romans z ‹nim|nią›";
            addMilestone(self, ms, now);
            addKeyMemory(self, ms, now);
            break;
        }
        default: break;
    }
    if (shift.health) switch (*shift.health) {
        case LifeHealth::sick: add(-0.15f, -0.1f); break;
        case LifeHealth::injured: add(-0.2f, 0.1f); break;
        case LifeHealth::tired: add(-0.05f, -0.1f); self.sleep_debt = clamp01(self.sleep_debt + 0.25f); break;
        case LifeHealth::recovering: add(0.05f, 0.0f); break;
        case LifeHealth::fit: add(0.1f, 0.05f); break;
    }
    if (shift.work) switch (*shift.work) {
        case LifeWork::promoted:
            add(0.3f, 0.2f);
            addMilestone(self, en ? L"I had a big success at work" : L"Odniosłam duży sukces w pracy", now);
            self.stress_load = clamp01(self.stress_load - 0.15f);
            break;
        case LifeWork::burnout: add(-0.2f, -0.1f); self.stress_load = clamp01(self.stress_load + 0.35f); break;
        case LifeWork::between_jobs: add(-0.2f, 0.15f); self.stress_load = clamp01(self.stress_load + 0.28f); break;
        case LifeWork::busy: add(0.0f, 0.1f); self.stress_load = clamp01(self.stress_load + 0.18f); break;
        case LifeWork::steady: add(0.05f, 0.0f); break;
    }
    if (shift.wealth) switch (*shift.wealth) {
        case LifeWealth::broke: add(-0.2f, 0.1f); self.stress_load = clamp01(self.stress_load + 0.22f); break;
        case LifeWealth::flush: add(0.25f, 0.15f); break;
        case LifeWealth::tight: add(-0.05f, 0.0f); self.stress_load = clamp01(self.stress_load + 0.1f); break;
        case LifeWealth::comfortable: add(0.05f, 0.0f); break;
    }
    if (shift.family) switch (*shift.family) {
        case LifeFamily::tension: add(-0.2f, 0.2f); self.stress_load = clamp01(self.stress_load + 0.25f); break;
        case LifeFamily::visit: add(0.15f, 0.15f); break;
        case LifeFamily::homesick: add(-0.15f, -0.05f); break;
        case LifeFamily::calm: add(0.05f, 0.0f); break;
    }
    if (shift.social) switch (*shift.social) {
        case LifeSocial::lonely: add(-0.15f, -0.05f); self.drives.bond = clamp01(self.drives.bond + 0.1f); break;
        case LifeSocial::overwhelmed: add(-0.1f, 0.2f); break;
        case LifeSocial::inspired: add(0.2f, 0.2f); break;
        case LifeSocial::balanced: add(0.05f, 0.0f); break;
    }
    self.affect = settled(self.affect, context);
    self.affect.valence = std::clamp(self.affect.valence + std::clamp(dv, -0.5f, 0.5f), -1.0f, 1.0f);
    self.affect.arousal = clamp01(self.affect.arousal + std::clamp(da, -0.4f, 0.4f));
    self.affect.at = now;
}

Quick quick(std::wstring_view message, const Self& self, int stage) {
    Quick out;
    const auto words = spaced(message);
    size_t word_count = 0;
    for (size_t i = 1; i < words.size(); ++i)
        if (words[i] == L' ' && words[i - 1] != L' ') ++word_count;
    const auto set = [&](Emotion emotion, float valence, float arousal) { out.flash = emotion; out.valence = valence; out.arousal = arousal; };
    out.words = static_cast<int>(word_count);
    if (hasAny(words, {L"glupia", L"idiotka", L"debilka", L"zamknij sie", L"spadaj", L"nienawidze cie", L"beznadziejna", L"wkurzasz", L"stupid",
                       L"shut up", L"i hate you", L"useless", L"annoying"})) {
        out.hostile = true;
        set(Emotion::sad, -0.25f, 0.15f);
    }
    else if (hasAny(words, {L"kocham cie", L"love you", L"jestes sliczna", L"jestes piekna", L"jestes slodka", L"jestes urocza", L"buziak", L"przytulam",
                            L"you re cute", L"youre cute", L"you are cute", L"you re beautiful", L"hug"}) || hasRaw(message, {L"😘", L"😍", L"❤", L"<3", L"🥰"})) {
        // From a near stranger it flusters and amuses her; a light flirt makes her smile; only closer it truly warms her.
        if (stage <= 0) set(Emotion::confused, 0.04f, 0.12f);
        else if (stage == 1) set(Emotion::shy, 0.08f, 0.12f);
        else if (stage == 2) set(Emotion::shy, 0.14f, 0.15f);
        else set(Emotion::shy, 0.2f, 0.15f);
    }
    else if (hasAny(words, {L"smutno", L"smutny", L"smutna", L"zle sie czuje", L"jestem zmeczony", L"jestem zmeczona", L"chory", L"chora", L"przykro",
                            L"placze", L"nie mam sily", L"samotny", L"samotna", L"depresja", L"sad", L"tired", L"sick", L"lonely", L"exhausted"}) ||
             hasRaw(message, {L"😢", L"😭", L":(", L";("})) {
        out.sad_or_stressed = true;
        set(Emotion::sad, -0.1f, 0.0f);
    }
    else if (hasAny(words, {L"wygralem", L"wygralam", L"udalo mi sie", L"skonczylem", L"skonczylam", L"ukonczylem", L"ukonczylam", L"zdalem", L"zdalam",
                            L"dostalem", L"dostalam", L"awans", L"i won", L"i finished", L"i beat", L"finally", L"wreszcie", L"nareszcie"}))
        set(Emotion::excited, 0.15f, 0.15f);
    else if (hasAny(words, {L"odinstalowalem", L"odinstalowalam", L"usunalem", L"usunelam", L"porzucilem", L"porzucilam", L"rzucam", L"wtf", L"omg",
                            L"nie wierze", L"uninstalled", L"deleted", L"no way"}) || hasRaw(message, {L"?!", L"!?", L"!!!"}))
        set(Emotion::shock, 0.0f, 0.2f);
    else if (hasAny(words, {L"haha", L"hahaha", L"hehe", L"xd", L"xdd", L"lol", L"lmao", L"rotfl"}) || hasRaw(message, {L"😂", L"🤣", L";p", L":p", L";P", L":P"})) {
        out.playful = true;
        set(Emotion::laugh, 0.1f, 0.1f);
    }
    else if (hasAny(words, {L"dzieki", L"dziekuje", L"super", L"swietnie", L"brawo", L"genialne", L"genialnie", L"thanks", L"thank you", L"awesome", L"great"}))
        set(Emotion::smile, 0.1f, 0.05f);

    // Item 10: ACC (Anterior Cingulate Cortex) & Right-Hemisphere Sarcasm / Implicature Detector (McGilchrist & Grice).
    // 1) Valence clash: hyperbolic praise ("no super", "świetnie", "genialnie", "kocham to") combined with frustration/crash/loss.
    const bool praise_word = hasAny(words, {L"super", L"swietnie", L"genialnie", L"genialne", L"brawo", L"cudownie", L"fantastycznie", L"rewelacja",
                                            L"kocham", L"uwielbiam", L"great", L"awesome", L"fantastic", L"wonderful", L"brilliant", L"love it"});
    const bool ironic_opener = hasAny(words, {L"no super", L"no swietnie", L"no genialnie", L"no brawo", L"po prostu super", L"po prostu swietnie",
                                              L"kocham gdy", L"kocham jak", L"uwielbiam gdy", L"oh great", L"just great", L"love it when"});
    const bool mishap_word = hasAny(words, {L"znowu", L"kolejny raz", L"wywalilo", L"crash", L"bug", L"blad", L"lag", L"stracilem", L"stracilam",
                                            L"usunelo", L"zepsulo", L"padlo", L"nie dziala", L"zdechlo", L"przegralem", L"przegralam", L"zginalem",
                                            L"zginelam", L"szlag", L"kurde", L"masakra", L"dramat", L"crashed", L"lost my save", L"broken",
                                            L"bugged", L"died again", L"stopped working", L"froze", L"corrupted"});
    if ((praise_word && mishap_word) || (ironic_opener && (mishap_word || hasRaw(message, {L"...", L"…"})))) {
        out.irony_or_vent = true;
        out.playful = false;
        set(Emotion::thinking, -0.08f, 0.1f); // Override naive [smile]!
    } else if (hasAny(words, {L"ta jasne", L"no jasne", L"jasne na pewno", L"juz biegne", L"bardzo smieszne", L"haha bardzo smieszne", L"chcialbys",
                              L"chcialabys", L"akurat", L"yeah right", L"sure thing", L"oh sure", L"very funny", L"as if", L"in your dreams"})) {
        out.skeptical_irony = true;
        out.playful = true;
        set(Emotion::smug, 0.04f, 0.08f);
    }

    // Item 4: Berne's "Why Don't You — Yes, But" game rejection vs. acceptance detector.
    if (hasAny(words, {L"nie mam ochoty", L"nie chce mi sie", L"za dluga", L"za dlugie", L"za trudna", L"za trudne", L"za nudna", L"za nudne",
                       L"nie teraz", L"nie dzis", L"nie dzisiaj", L"cos innego", L"raczej nie", L"odpada", L"nie bardzo", L"nie mam nastroju",
                       L"nie mam sily na", L"not in the mood", L"too long", L"too hard", L"too boring", L"something else", L"not now", L"not today",
                       L"pass on that", L"dont feel like"})) {
        out.rejected_pick = true;
    } else if (hasAny(words, {L"zagram", L"odpalam", L"wlaczam", L"dobra propozycja", L"swietny pomysl", L"biore to", L"brzmi super", L"sprobuje",
                              L"i ll play", L"ill play", L"good pick", L"sounds great", L"let s play", L"lets play", L"i will try"})) {
        out.accepted_pick = true;
    }

    // Anti-sycophancy vs. independent opinion (respect axis).
    if (hasAny(words, {L"masz we wszystkim racje", L"zawsze masz racje", L"zgadzam sie ze wszystkim", L"jak rozkazesz", L"cokolwiek powiesz",
                       L"you are always right", L"whatever you say", L"i agree with everything"}))
        out.respect_delta = -4;
    else if (hasAny(words, {L"nie zgadzam sie", L"moim zdaniem", L"ja wole", L"mylisz sie", L"mam inne zdanie", L"wedlug mnie",
                            L"i disagree", L"in my opinion", L"i prefer", L"you re wrong", L"youre wrong"}))
        out.respect_delta = +4;

    // Micro-chronemics: dismissive 1–2 word brush-off after she opened up emotionally, or a long confessional wall of text.
    if (self.last_reply_vulnerable && word_count > 0 && word_count <= 2 &&
        hasAny(words, {L"ok", L"okej", L"spoko", L"fajnie", L"aha", L"no", L"k", L"cool", L"nice", L"whatever", L"hm", L"hmm", L"rozumiem"})) {
        out.dismissive = true;
        set(Emotion::sad, -0.22f, 0.1f);
    } else if (message.size() >= 180 || word_count >= 30) {
        out.confession = true;
        if (!out.flash) set(Emotion::thinking, 0.05f, 0.12f);
    }

    if (!out.flash) {
        // Somatic markers (amygdala vs. vmPFC cortical inhibition): when her energy is high, cortical
        // inhibition dampens the marker; when she is tired, raw subcortical weight fires.
        const auto folded = game_index::fold(message);
        const float control = clamp01(self.drives.energy * (1.0f - 0.5f * self.affect.arousal));
        for (const auto& marker : self.markers) {
            const float effective = marker.valence * (1.0f - 0.65f * clamp01(marker.cortical_inhibition) * control);
            if (std::abs(effective) < 0.35f) continue;
            const auto subject = game_index::fold(marker.subject);
            if (subject.size() < 3 || folded.find(subject) == std::wstring::npos) continue;
            set(effective > 0 ? Emotion::love : Emotion::angry, effective * 0.2f, 0.1f);
            break;
        }
    }

    // Grounding (Clark & Brennan): does she know what he meant? And does his message close the
    // topic, or open one of his own life worth asking about?
    out.asks = message.find(L'?') != std::wstring_view::npos;
    if (hasAny(words, {L"tak i nie", L"i tak i nie", L"sam nie wiem", L"sama nie wiem", L"trudno powiedziec", L"to zalezy", L"zalezy jak", L"zalezy od",
                       L"mieszane uczucia", L"poniekad", L"w pewnym sensie", L"niby tak", L"niby nie",
                       L"yes and no", L"it depends", L"hard to say", L"mixed feelings"}))
        out.unclear = Quick::Unclear::ambivalent;
    else if (hasAny(words, {L"wiesz o co chodzi", L"wiesz o czym mowie", L"wiesz o kim mowie", L"ta sprawa", L"ta cala sprawa", L"tamta sprawa", L"ta rzecz",
                            L"to cos", L"tamto", L"z tym czyms", L"you know what i mean", L"you know who", L"that thing", L"the thing", L"that stuff",
                            L"that whole thing"}))
        out.unclear = Quick::Unclear::vague;
    else if (!out.asks && !out.dismissive && !out.irony_or_vent && word_count >= 1 && word_count <= 8) {
        auto tail = message;
        while (!tail.empty() && std::iswspace(tail.back())) tail.remove_suffix(1);
        if (tail.ends_with(L"...") || tail.ends_with(L"…")) out.unclear = Quick::Unclear::unfinished; // Broken off mid-thought.
    }
    // Closed: nothing but acknowledgements ("ok spoko", "nic ciekawego", "nie wiem", "haha") — every word one of them.
    if (!out.asks && word_count >= 1 && word_count <= 4) {
        static constexpr const wchar_t* closing[]{
            L"ok", L"okej", L"okay", L"oki", L"k", L"spoko", L"dobra", L"dobrze", L"fajnie", L"fajne", L"aha", L"aa", L"no", L"nom", L"mhm", L"jasne",
            L"git", L"luz", L"nic", L"ciekawego", L"specjalnego", L"jakos", L"leci", L"po", L"staremu", L"w", L"porzadku", L"sumie", L"niezle", L"tak",
            L"nie", L"wiem", L"haha", L"hahaha", L"hehe", L"xd", L"xdd", L"lol", L"cool", L"nice", L"fine", L"good", L"yeah", L"yep", L"nope", L"sure",
            L"nothing", L"much", L"same", L"idk", L"meh", L"pewnie", L"moze", L"ta"};
        bool only = true;
        for (size_t start = 1; start < words.size() && only;) {
            const auto end = words.find(L' ', start);
            if (end == std::wstring::npos) break;
            if (end > start) {
                const std::wstring_view word(words.data() + start, end - start);
                only = std::any_of(std::begin(closing), std::end(closing), [&](const wchar_t* c) { return word == c; });
            }
            start = end + 1;
        }
        out.closed = only;
    }
    if (!out.asks && word_count >= 5 &&
        hasAny(words, {L"bylem", L"bylam", L"poszedlem", L"poszlam", L"pojechalem", L"pojechalam", L"mialem", L"mialam", L"zaczalem", L"zaczelam",
                       L"spotkalem", L"spotkalam", L"kupilem", L"kupilam", L"planuje", L"zamierzam", L"jutro", L"w weekend", L"wczoraj", L"dzisiaj",
                       L"w pracy", L"w szkole", L"na uczelni", L"na studiach", L"z bratem", L"z siostra", L"z dziewczyna", L"z chlopakiem", L"z kumplem",
                       L"z kumplami", L"ze znajomymi", L"z rodzicami", L"moj brat", L"moja siostra", L"moj kumpel", L"moja dziewczyna", L"moj chlopak",
                       L"i went", L"i was", L"i had", L"i started", L"i met", L"i bought", L"i m planning", L"im planning", L"tomorrow", L"yesterday",
                       L"this weekend", L"at work", L"at school", L"my brother", L"my sister", L"my friend", L"my girlfriend", L"my boyfriend", L"my parents"}))
        out.hook = true;
    if (!out.hook && !out.asks) {
        // Someone or something from his world she already knows of: worth asking about.
        const auto folded = game_index::fold(message);
        for (const auto& w : self.player_world) {
            const auto head = game_index::fold(w.name.substr(0, w.name.find_first_of(L" (-—:")));
            if (head.size() >= 3 && folded.find(head) != std::wstring::npos) { out.hook = true; break; }
        }
    }
    // His bid for connection (Gottman): he shows her something, has news, wants her attention.
    out.bid = !out.hostile && (hasRaw(message, {L"http://", L"https://", L"www."}) ||
        hasAny(words, {L"patrz", L"popatrz", L"zobacz", L"sprawdz", L"sluchaj", L"posluchaj", L"wiesz co", L"zgadnij", L"mam dla ciebie", L"pokaze ci",
                       L"musze ci powiedziec", L"musze ci cos", L"nie uwierzysz", L"wyobraz sobie", L"look", L"check this", L"check it out", L"guess what",
                       L"you know what", L"listen", L"you won t believe", L"i have to tell you"}));
    return out;
}

Urge curiosityUrge(const Self& self, const Context& context, const Quick& quick, uint32_t seed) {
    Urge out;
    out.at = context.now;
    const bool en = context.english;
    // Never on top of hurt or of being caught, and not again right after the last time.
    if (quick.dismissive || self.peek_caught || self.curious_gap < 2) return out;
    const float energy = energyNow(self, context);
    const float tired = energy < 0.3f ? 0.5f : 1.0f;
    const float roll = static_cast<float>((seed >> 7) % 1000) / 1000.0f;
    // Path 1 — asking back: his message leaves her unsure what he meant.
    if (quick.unclear != Quick::Unclear::none) {
        float p = context.persona == Persona::kuudere || context.persona == Persona::himedere ? 0.55f : 0.7f; // The proud ones admit it less.
        p *= 0.6f + 0.4f * tired;
        if (roll >= p) return out;
        out.move = CuriousMove::clarify;
        out.gentle = quick.sad_or_stressed || quick.confession;
        switch (quick.unclear) {
            case Quick::Unclear::ambivalent:
                out.why = en ? L"‹his|her|their› answer was ambivalent — you can't tell which way ‹he leans|she leans|they lean›" : L"‹odpowiedział|odpowiedziała› niejednoznacznie — nie wiesz, w którą stronę się skłania";
                break;
            case Quick::Unclear::vague:
                out.why = en ? L"‹he refers|she refers|they refer› to something ‹he|she|they› didn't name — you aren't sure what ‹he means|she means|they mean›" : L"odwołuje się do czegoś, czego nie ‹nazwał|nazwała› — nie masz pewności, o co ‹mu|jej› chodzi";
                break;
            default:
                out.why = en ? L"‹he|she|they› broke off mid-thought" : L"‹urwał|urwała› myśl w pół zdania";
                break;
        }
        return out;
    }
    // Not when he asked her something (she answers that).
    if (quick.asks) return out;
    const bool down = quick.sad_or_stressed || quick.confession;
    // Path 3 — reflecting (active listening, Rogers): he opened up; before advice, comfort or a
    // story of her own she gives back in her words what she heard and the feeling in it.
    if (down) {
        float listen = 0.55f;
        switch (context.persona) {
            case Persona::dandere: case Persona::deredere: listen = 0.7f; break;
            case Persona::kichidere: case Persona::himedere: listen = 0.4f; break;
            case Persona::kuudere: case Persona::kanedere: listen = 0.45f; break;
            default: break;
        }
        if (roll < listen * (0.6f + 0.4f * tired)) {
            out.move = CuriousMove::reflect;
            out.gentle = true;
            out.why = en ? L"‹he|she|they› opened up to you" : L"‹otworzył|otworzyła› się przed tobą";
            return out;
        }
    }
    // Path 2 — carrying the talk on.
    float persona = 1.0f;
    switch (context.persona) {
        case Persona::genki: case Persona::yandere: persona = 1.15f; break;
        case Persona::kichidere: persona = 1.2f; break;
        case Persona::deredere: persona = 1.1f; break;
        case Persona::tsundere: persona = 0.9f; break;
        case Persona::himedere: case Persona::kanedere: persona = 0.85f; break;
        case Persona::kuudere: persona = 0.75f; break;
        case Persona::dandere: persona = 0.7f; break;
    }
    const auto& d = self.drives;
    float p = 0;
    if (quick.closed && !down) {
        p = 0.45f + 0.25f * d.curiosity + 0.15f * d.autonomy;
        if (self.topic.turns >= 6) p += 0.15f; // The topic wore thin (habituation): time for another.
        out.why = en ? L"‹he|she|they› closed the topic with a short reply" : L"krótko ‹zamknął|zamknęła› temat";
    } else if (quick.hook || down || quick.irony_or_vent) {
        p = 0.35f + 0.4f * d.curiosity + 0.2f * (self.policy.deep_questions - 0.5f);
        out.deeper = true;
        out.gentle = down;
        if (down) p *= 0.8f;
        out.why = down ? (en ? L"‹he|she|they› opened up to you" : L"‹otworzył|otworzyła› się przed tobą")
                : quick.irony_or_vent ? (en ? L"something went wrong for ‹him|her|them› and ‹he is|she is|they are› frustrated" : L"coś ‹mu|jej› nie wyszło i ‹jest sfrustrowany|jest sfrustrowana›")
                                      : (en ? L"‹he|she|they› told you something from ‹his|her|their› own life" : L"‹opowiedział|opowiedziała› ci coś ze swojego życia");
    } else {
        // In a playful back-and-forth the banter itself carries the talk: fewer questions then.
        p = (0.15f + 0.3f * d.curiosity) * (1.0f - 0.4f * self.play_momentum);
        out.deeper = true;
        out.why = en ? L"you are curious where this goes" : L"jesteś ciekawa, co dalej";
    }
    // Loewenstein's information gap: curiosity peaks when she knows him a little and the rest is
    // within reach; her interest in his replies so far (reward prediction error) carries it too.
    const float k = std::clamp(context.known, 0.0f, 1.0f);
    p *= (0.85f + 1.6f * k * (1.0f - k)) * (0.8f + 0.4f * self.interest);
    // What the lead may bring back, in order: how something in his life went (it is over now), a
    // question he left unanswered (her character decides whether she comes back), a question she
    // set aside, a thread he opened that went nowhere.
    if (!down) {
        for (const auto& f : self.follow_ups)
            if (f.state == 0 && context.now >= f.due && context.now <= f.due + 4 * day) {
                out.carry = CuriousCarry::follow_up; out.carry_id = f.id; out.carried = f.what;
                p += 0.35f;
                break;
            }
        if (out.carry == CuriousCarry::none) {
            float persistence = 0.3f;
            switch (context.persona) {
                case Persona::yandere: persistence = 0.8f; break;
                case Persona::tsundere: case Persona::himedere: persistence = 0.6f; break;
                case Persona::kichidere: persistence = 0.5f; break;
                case Persona::genki: case Persona::kanedere: persistence = 0.4f; break;
                case Persona::deredere: persistence = 0.25f; break;
                case Persona::kuudere: persistence = 0.15f; break;
                case Persona::dandere: persistence = 0.1f; break;
            }
            const float second_roll = static_cast<float>((seed >> 17) % 1000) / 1000.0f;
            for (auto it = self.curious.rbegin(); it != self.curious.rend(); ++it)
                if (it->outcome == 0 && !it->returned && it->move != CuriousMove::reflect && !it->about.empty() &&
                    context.now >= it->settled + 10 * 60 && context.now <= it->settled + day) {
                    if (second_roll < persistence) { out.carry = CuriousCarry::returning; out.carry_id = it->id; out.carried = it->about; p += 0.2f; }
                    break;
                }
        }
        if (out.carry == CuriousCarry::none)
            for (const auto& c : self.curious)
                if (c.deferred() && context.now >= c.later_at + 20 * 60 && context.now <= c.later_at + 3 * day) {
                    out.carry = CuriousCarry::question; out.carry_id = c.id; out.carried = c.about;
                    p += 0.25f;
                    break;
                }
        if (out.carry == CuriousCarry::none && quick.closed)
            for (const auto& t : self.threads)
                if (!t.used && context.now >= t.at + 3 * 60 && context.now <= t.at + day) {
                    out.carry = CuriousCarry::thread; out.carried = t.text;
                    p += 0.15f;
                    break;
                }
    }
    p *= persona * tired;
    if (self.curious_gap < 3) p *= 0.7f; // She asked a little while ago.
    if (roll >= std::min(p, 0.85f)) {
        Urge none;
        none.at = context.now;
        return none;
    }
    out.move = CuriousMove::lead;
    return out;
}

void onPlayerMessage(Self& self, const Context& context, const Quick& quick, std::wstring_view message) {
    self.affect = settled(self.affect, context);
    self.affect.valence = std::clamp(self.affect.valence + quick.valence, -1.0f, 1.0f);
    self.affect.arousal = clamp01(self.affect.arousal + quick.arousal);
    self.affect.at = context.now;
    self.drives.bond = clamp01(self.drives.bond - 0.06f);
    self.last_talk = context.now;
    self.respect_delta += quick.respect_delta;
    if (quick.dismissive) {
        self.subtext_note = context.english
            ? L"You just opened up emotionally and ‹he|she|they› brushed you off with a cold one-word reply — you feel a sting of hurt and pull your guard back up."
            : L"Właśnie się przed ‹nim|nią› otworzyłaś, a ‹on zbył|ona zbyła› cię jednym chłodnym słowem — poczułaś ukłucie zawodu i znów chowasz się za pancerzem.";
        self.subtext_at = context.now;
        self.trust_delta -= 3;
    } else if (quick.confession) {
        // Co-regulation: when the player opens up or is distressed, even energetic personas calm down and ground them.
        if (context.trust >= 45) self.affect.arousal = std::min(self.affect.arousal, 0.35f);
        self.subtext_note = context.english
            ? L"‹He|She|They› just poured out a long, personal message — co-regulate: lower your energy, put teasing and noise aside, listen closely and answer with calm, genuine care."
            : L"‹On właśnie napisał|Ona właśnie napisała› długą, osobistą wiadomość — wycisz swoje pobudzenie (ko-regulacja), odłóż zaczepki i hałas na bok, wysłuchaj ‹go|jej› uważnie i odpowiedz ze spokojną, szczerą troską.";
        self.subtext_at = context.now;
        self.trust_delta += 3;
        self.security_delta += 2;
    } else if (quick.sad_or_stressed && context.trust >= 45) {
        self.affect.arousal = std::min(self.affect.arousal, 0.35f);
        self.subtext_note = context.english
            ? L"‹He sounds|She sounds|They sound› down or exhausted — instead of mirroring stress or staying hyper, calm your tone down and be a quiet, supportive anchor for ‹him|her|them›."
            : L"‹On brzmi na przygaszonego lub zmęczonego|Ona brzmi na przygaszoną lub zmęczoną› — zamiast nakręcać tempo albo żartować na siłę, wyhamuj swoje emocje i bądź dla ‹niego|niej› spokojnym oparciem.";
        self.subtext_at = context.now;
    } else if (quick.irony_or_vent) {
        self.subtext_note = context.english
            ? L"‹He|She|They› used bitter sarcasm/irony because ‹he is|she is|they are› frustrated (e.g. a crash, bug or loss) — do NOT take words like 'great/super' literally! Catch the irony and respond with understanding or dry, supportive humor."
            : L"‹On użył|Ona użyła› gorzkiej ironii/sarkazmu, bo coś poszło nie tak (np. crash, błąd albo porażka) — NIE bierz słów „super/świetnie” dosłownie! Wyczuj ironię i zareaguj ze zrozumieniem albo lekkim, wspierającym humorem.";
        self.subtext_at = context.now;
    } else if (quick.skeptical_irony) {
        self.subtext_note = context.english
            ? L"‹He|She|They› replied with playful, skeptical irony — read the subtext between the lines and banter back in your style."
            : L"‹On odpowiedział|Ona odpowiedziała› z przekorną ironią i niedowierzaniem — odczytaj podtekst między wierszami i odbij piłeczkę w swoim stylu.";
        self.subtext_at = context.now;
    }
    // Item 6: Panksepp's subcortical PLAY circuit — gated off immediately by sadness, dismissal, frustration or negative affect.
    if (quick.sad_or_stressed || quick.dismissive || quick.irony_or_vent || quick.valence < -0.1f) {
        self.play_momentum = 0.0f;
    } else if (quick.playful) {
        self.play_momentum = clamp01(self.play_momentum + 0.28f);
    }
    // Item 4: Berne's "Why Don't You — Yes, But" counter.
    if (quick.accepted_pick) {
        self.rejected_picks_in_row = 0;
    } else if (quick.rejected_pick) {
        const bool had_suggestions = self.policy.last_style == "games" ||
                                     std::any_of(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.outcome == -1 && e.suggestion; });
        if (had_suggestions || self.rejected_picks_in_row > 0) ++self.rejected_picks_in_row;
    }
    // Item 7: Sapolsky's sleep debt — staying up talking during her night or late hours accumulates sleep debt for tomorrow.
    if (asleep(context)) {
        self.sleep_debt = clamp01(self.sleep_debt + 0.09f);
    } else if (context.character) {
        if (const auto clk = localClock(*context.character, context.now)) {
            const int h = clk->hour;
            if ((context.character->night_owl && h >= 2 && h < 6) || (!context.character->night_owl && (h >= 23 || h < 5)))
                self.sleep_debt = clamp01(self.sleep_debt + 0.05f);
        }
    }
    // Basal Ganglia dopaminergic Reward Prediction Error (RPE): update conversational policy weights.
    if (!message.empty()) {
        const auto words = spaced(message);
        size_t wc = 0;
        for (size_t i = 1; i < words.size(); ++i)
            if (words[i] == L' ' && words[i - 1] != L' ') ++wc;
        float reward = std::min(0.55f, static_cast<float>(wc) / 14.0f * 0.55f);
        if (message.find(L'?') != std::wstring_view::npos || message.find(L'!') != std::wstring_view::npos) reward += 0.2f;
        if (quick.valence > 0.05f) reward += 0.25f;
        if (quick.dismissive || quick.valence < -0.15f) reward = 0.05f;
        reward = clamp01(reward);
        const float rpe = reward - self.policy.baseline_reward;
        self.policy.baseline_reward = clamp01(self.policy.baseline_reward + 0.15f * rpe);
        const auto nudge = [&](float& w) { w = std::clamp(w + 0.16f * rpe, 0.15f, 0.95f); };
        if (self.policy.last_style == "teasing") nudge(self.policy.teasing);
        else if (self.policy.last_style == "deep") nudge(self.policy.deep_questions);
        else if (self.policy.last_style == "games") nudge(self.policy.game_talk);
        else if (self.policy.last_style == "life") nudge(self.policy.own_life);
        else if (self.policy.last_style == "warmth") nudge(self.policy.warmth);

        // Reinforce concepts taught by the player when mentioned again.
        const auto folded = game_index::fold(message);
        for (auto& c : self.learned_concepts) {
            const auto t = game_index::fold(c.term);
            if (t.size() >= 3 && folded.find(t) != std::wstring::npos) {
                c.mastery = clamp01(c.mastery + 0.18f);
                c.at = context.now;
            }
        }
        noticePeekedLeak(self, message, context.now);
    }
    for (auto& e : self.expectations)
        if (e.presence && e.outcome == -1) { e.outcome = 1; e.settled = context.now; }
    // His answer to what she asked last: he explained / took it up (her curiosity eases), or let it
    // pass (a carried-on talk brushed off stings a little; any real answer to her question counts).
    for (auto it = self.curious.rbegin(); it != self.curious.rend(); ++it) {
        if (it->outcome != -1 || it->at == 0) continue;
        const bool passed = quick.dismissive || (it->move != CuriousMove::clarify && quick.closed);
        it->outcome = passed ? 0 : 1;
        it->settled = context.now;
        if (!passed) self.drives.curiosity = clamp01(self.drives.curiosity - 0.03f);
        else if (it->move == CuriousMove::lead) self.affect.valence = std::clamp(self.affect.valence - 0.03f, -1.0f, 1.0f);
        break;
    }
    for (auto& c : self.curious) // Older ones he never got to.
        if (c.outcome == -1 && c.at > 0) { c.outcome = 0; c.settled = context.now; }
    const auto attachment = attachmentOf(context);
    // Her bid for connection, answered (Gottman): toward, away or against; it fades over a week.
    fadeBids(self.bids, context.now);
    if (self.bids.open) {
        self.bids.open = false;
        if (quick.hostile) {
            self.bids.against += 1;
            self.security_delta -= 2;
        } else if (quick.dismissive || quick.closed) {
            self.bids.away += 1;
            if (attachment == Attachment::anxious || attachment == Attachment::fearful) self.security_delta -= 1; // It stings more.
        } else self.bids.toward += 1;
    }
    if (quick.bid) ++self.bids.his;
    // How he writes (accommodation), averaged over his messages (a third each new one).
    if (!message.empty() && quick.words > 0) {
        auto& s = self.his_style;
        bool letters = false, upper = false, diacritics = false;
        for (const wchar_t ch : message) {
            if (!std::iswalpha(ch)) continue;
            letters = true;
            if (std::iswupper(ch)) upper = true;
            if (std::wstring_view(L"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ").find(ch) != std::wstring_view::npos) diacritics = true;
        }
        const auto folded = spaced(message);
        const float laugh = hasAny(folded, {L"xd", L"xdd", L"xddd", L"haha", L"hahaha", L"hehe", L"lol", L"lmao"}) || hasRaw(message, {L"😂", L"🤣"}) ? 1.0f : 0.0f;
        const float a = s.samples < 3 ? 1.0f / static_cast<float>(s.samples + 1) : 0.33f;
        const auto mix = [&](float& value, float sample) { value += a * (sample - value); };
        mix(s.words, static_cast<float>(quick.words));
        mix(s.exclaim, message.find(L'!') != std::wstring_view::npos ? 1.0f : 0.0f);
        if (letters) {
            mix(s.lower, upper ? 0.0f : 1.0f);
            // No Polish letters in a Polish chat: he writes "bez ogonków" (a word long enough to need one).
            if (!context.english && quick.words >= 4) mix(s.plain, diacritics ? 0.0f : 1.0f);
        }
        mix(s.laughs, laugh);
        s.samples = std::min(s.samples + 1, 1000);
    }
    // The topic of the moment: his content words; a message sharing one stays on it.
    if (!message.empty()) {
        auto words = chats::searchWords(message, 8);
        if (!words.empty()) {
            size_t shared = 0;
            chats::overlap(words, self.topic.words, &shared);
            if (shared > 0 && !self.topic.words.empty()) {
                ++self.topic.turns;
                for (auto& w : words)
                    if (self.topic.words.size() < 12 && std::none_of(self.topic.words.begin(), self.topic.words.end(), [&](const std::wstring& t) { return t == w; }))
                        self.topic.words.push_back(std::move(w));
            } else self.topic = Topic{std::move(words), 1};
        }
    }
    // Her interest in what he writes (reward prediction error, the "dopamine" of a chat): a reply
    // richer than she expected intrigues her (and makes her more curious); a flat one lets her down a little.
    {
        float got = 0.15f + 0.03f * static_cast<float>(std::min(quick.words, 20));
        if (quick.hook) got += 0.2f;
        if (quick.asks) got += 0.12f;
        if (quick.playful) got += 0.1f;
        if (quick.bid) got += 0.1f;
        if (self.topic.turns == 1 && quick.words >= 4) got += 0.1f; // Something new.
        if (quick.closed) got -= 0.25f;
        if (quick.dismissive || quick.hostile) got -= 0.3f;
        got = clamp01(got);
        self.interest_rpe = got - self.interest;
        self.interest = clamp01(self.interest + 0.3f * self.interest_rpe);
        self.interest_at = context.now;
        if (self.interest_rpe > 0.2f) {
            self.drives.curiosity = clamp01(self.drives.curiosity + 0.04f);
            self.affect.valence = std::clamp(self.affect.valence + 0.04f, -1.0f, 1.0f);
            self.affect.arousal = clamp01(self.affect.arousal + 0.04f);
        } else if (self.interest_rpe < -0.25f) self.affect.valence = std::clamp(self.affect.valence - 0.02f, -1.0f, 1.0f);
    }
    // He opened up about himself; right after she did (or she does next): a reciprocal disclosure —
    // closeness and calm (the hyperpersonal effect; trust over the fear of a stranger).
    if (quick.confession || (quick.hook && quick.words >= 10) || quick.sad_or_stressed) {
        self.his_open_at = context.now;
        if (self.last_reply_vulnerable && context.now >= self.bonded_at + hour) {
            self.bonded_at = context.now;
            self.trust_delta += 2;
            self.security_delta += 2;
            self.affect.arousal = std::min(self.affect.arousal, 0.4f);
        }
    }
    // Something coming up in his life that he brings up himself after it: no need to ask.
    if (!message.empty())
        for (auto& f : self.follow_ups) {
            if (f.state != 0 || context.now + 2 * hour < f.due) continue;
            const auto words = chats::searchWords(f.what);
            if (!words.empty() && chats::matches(words, message) >= std::min(2, static_cast<int>(words.size()))) { f.state = 3; f.settled = context.now; }
        }
}

void onReply(Self& self, const Context& context, bool about_games, std::wstring_view reply_text, bool vulnerable) {
    self.drives.energy = clamp01(self.drives.energy - 0.025f);
    self.drives.autonomy = clamp01(self.drives.autonomy + (about_games ? 0.04f : -0.02f));
    self.last_talk = context.now;
    self.last_reply_vulnerable = vulnerable;
    self.peek_caught = false;
    if (about_games) self.policy.last_style = "games";
    else if (vulnerable) self.policy.last_style = "warmth";
    else if (!reply_text.empty() && reply_text.find(L'?') != std::wstring_view::npos) self.policy.last_style = "deep";
    else if (self.drives.autonomy < 0.35f) self.policy.last_style = "life";
    else self.policy.last_style = "teasing";
    if (!reply_text.empty()) {
        const auto folded = game_index::fold(reply_text);
        for (auto& c : self.learned_concepts) {
            const auto t = game_index::fold(c.term);
            if (t.size() >= 3 && folded.find(t) != std::wstring::npos) c.mastery = clamp01(c.mastery + 0.22f);
        }
        noticeConfessed(self, reply_text, context.now);
        const bool apologized = noticeApology(self, reply_text, context.now);
        // Affective realism friction: if she answered while irritable, burnt out or exhausted (and not
        // warmly/vulnerably or apologizing), she remembers later that she was harsh on the player.
        if (!apologized && !vulnerable) {
            const auto cur = settled(self.affect, context);
            const float energy = energyNow(self, context);
            if (cur.valence <= -0.18f || (energy < 0.32f && cur.valence < 0.0f) ||
                context.life.work == LifeWork::burnout || context.life.family == LifeFamily::tension) {
                self.harsh_at = context.now;
                if (context.life.work == LifeWork::burnout)
                    self.harsh_reason = context.english ? L"burnout and stress at work" : L"wypalenie i stres w pracy";
                else if (context.life.family == LifeFamily::tension)
                    self.harsh_reason = context.english ? L"tension with your family" : L"spięcie z rodziną";
                else if (energy < 0.32f)
                    self.harsh_reason = context.english ? L"exhaustion and lack of sleep" : L"zmęczenie i niewyspanie";
                else
                    self.harsh_reason = context.english ? L"your own bad mood" : L"własny zły humor";
            }
        }
    }
    const uint64_t now = context.now;
    // Her reply reaches out (a question, something of her own): his next message answers the bid.
    self.bids.open = !reply_text.empty() && (reply_text.find(L'?') != std::wstring_view::npos || vulnerable);
    if (vulnerable) {
        self.opened_at = now;
        // She opens up right after he did: a reciprocal disclosure (closeness, calm).
        if (self.his_open_at && now <= self.his_open_at + 30 * 60 && now >= self.bonded_at + hour) {
            self.bonded_at = now;
            self.trust_delta += 2;
            self.security_delta += 2;
            self.affect.arousal = std::min(self.affect.arousal, 0.4f);
        }
    }
    self.retold = false;
    if (!reply_text.empty()) {
        // Something coming up in his life: she asked how it went (after it) or wished him luck (before).
        for (auto& f : self.follow_ups) {
            if (f.state != 0) continue;
            const auto words = chats::searchWords(f.what);
            if (words.empty() || chats::matches(words, reply_text) < std::min(2, static_cast<int>(words.size()))) continue;
            if (now + 2 * hour >= f.due) { f.state = 1; f.settled = now; }
            else f.wished = true;
        }
        // A thread of his she took up after all.
        for (auto& t : self.threads)
            if (!t.used && chats::matches(chats::searchWords(t.text), reply_text) >= 2) t.used = true;
        // Second-order theory of mind: she told him this before (a day or more ago) and says it again as news.
        const auto said = chats::searchWords(reply_text, 32);
        const auto folded = spaced(reply_text);
        const bool callback = hasAny(folded, {L"mowilam", L"wspominalam", L"opowiadalam", L"pamietasz", L"jak ci mowilam", L"told you", L"mentioned", L"remember"});
        if (!callback && said.size() >= 4)
            for (const auto& claim : self.claims) {
                if (claim.at + day > now) continue;
                size_t shared = 0;
                const float part = chats::overlap(chats::searchWords(claim.text, 16), said, &shared);
                if (shared >= 3 && part >= 0.6f) { self.retold = true; break; }
            }
    }
    // Her curiosity in this reply, as her words show it: what her appraisal says she does, or —
    // when it said nothing — her urge, counts only when she really asks (a reflection needs no
    // question). An appraisal that only claims curiosity is not an act (and does not hold back the next).
    const auto question = lastQuestion(reply_text);
    bool acted = false;
    const auto pending = std::exchange(self.pending_curious, {});
    if (pending.move != CuriousMove::none && (pending.move == CuriousMove::reflect ? !reply_text.empty() : !question.empty())) {
        const auto said = pending.move == CuriousMove::reflect ? (!pending.about.empty() ? pending.about : firstSentence(reply_text)) : question;
        askNow(self, pending.move, said, pending.why, now, context.english);
        acted = true;
    } else if (self.urge.move != CuriousMove::none && (!question.empty() || (self.urge.move == CuriousMove::reflect && !reply_text.empty()))) {
        askNow(self, self.urge.move, question.empty() ? firstSentence(reply_text) : question, self.urge.why, now, context.english);
        acted = true;
    }
    keepCurious(self);
    self.curious_gap = acted ? 0 : std::min(self.curious_gap + 1, 99);
    self.urge = {};
}

void onSuggested(Self& self, const Context& context, const std::vector<std::pair<std::string, std::wstring>>& games) {
    for (const auto& [key, title] : games) {
        if (key.empty() || title.empty()) continue;
        if (std::any_of(self.expectations.begin(), self.expectations.end(), [&](const Expectation& e) { return e.outcome == -1 && e.game == key; })) continue;
        Expectation e;
        e.id = newId(self, 'o');
        e.text = context.english ? L"‹he|she|they› will play [[" + title + L"]], which you suggested" : L"zagra w [[" + title + L"]], którą ‹mu|jej› poleciłaś";
        e.game = key;
        e.title = title;
        e.p = 0.35f;
        e.made = context.now;
        e.due = context.now + 3 * day;
        e.suggestion = true;
        self.expectations.push_back(std::move(e));
        // At most three of her suggestions waiting at once: the oldest gives way.
        std::vector<size_t> waiting;
        for (size_t i = 0; i < self.expectations.size(); ++i)
            if (self.expectations[i].outcome == -1 && self.expectations[i].suggestion) waiting.push_back(i);
        if (waiting.size() > 3) self.expectations.erase(self.expectations.begin() + static_cast<std::ptrdiff_t>(waiting.front()));
    }
}

void onPlayed(Self& self, const Context& context, std::string_view key, std::wstring_view title, uint32_t minutes) {
    if (minutes >= 5) self.rejected_picks_in_row = 0;
    if (minutes >= 10) {
        for (auto& e : self.expectations) {
            if (e.outcome != -1 || e.game.empty() || e.game != key) continue;
            e.outcome = 1;
            e.settled = context.now;
            if (e.suggestion) {
                self.drives.pride = clamp01(self.drives.pride + 0.15f);
                self.affect = settled(self.affect, context);
                self.affect.valence = std::clamp(self.affect.valence + 0.1f, -1.0f, 1.0f);
                self.affect.at = context.now;
                if (auto* marker = markerOf(self, title.empty() ? std::wstring_view(e.title) : title)) {
                    marker->valence = std::clamp(marker->valence + 0.25f, -1.0f, 1.0f);
                    marker->at = context.now;
                }
            }
        }
    }
    if (minutes >= 30 && !title.empty())
        if (auto* marker = markerOf(self, title)) { marker->valence = std::clamp(marker->valence + 0.05f, -1.0f, 1.0f); marker->at = context.now; }
}

void onConversationEnd(Self& self, const Context& context, int streak) {
    if (streak < 2) return;
    if (std::any_of(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.presence && e.outcome == -1; })) return;
    Expectation e;
    e.id = newId(self, 'o');
    e.text = context.english ? L"‹he|she|they› will come back to talk tomorrow" : L"jutro znów do ciebie zajrzy";
    e.p = std::min(0.9f, 0.45f + 0.1f * static_cast<float>(streak));
    e.made = context.now;
    e.due = context.now + 40 * hour;
    e.presence = true;
    self.expectations.push_back(std::move(e));
}

Appraisal parseAppraisal(std::wstring_view text) {
    Appraisal out;
    const auto open = text.find(L'{'), close = text.rfind(L'}');
    if (open == std::wstring_view::npos || close == std::wstring_view::npos || close < open) return out;
    const auto parsed = json::parse(utf8(text.substr(open, close - open + 1)), 64 * 1024);
    if (!parsed || parsed->type != json::Value::Type::object) return out;
    const auto& root = *parsed;
    const auto str = [](const json::Value* v, size_t max) { return v && v->type == json::Value::Type::string ? clean(wide(v->text), max) : std::wstring{}; };
    const auto num = [](const json::Value* v) -> std::optional<float> {
        if (!v) return std::nullopt;
        if (v->type == json::Value::Type::number) return static_cast<float>(v->numeric);
        if (v->type == json::Value::Type::string) { try { return std::stof(v->text); } catch (...) {} }
        return std::nullopt;
    };
    const auto list = [](const json::Value* v) {
        std::vector<const json::Value*> items;
        if (!v) return items;
        if (v->type == json::Value::Type::array) for (const auto& item : v->elements) items.push_back(&item);
        else if (v->type != json::Value::Type::null_value) items.push_back(v);
        return items;
    };
    if (const auto v = num(root.get("v"))) out.valence = std::clamp(*v, -1.0f, 1.0f);
    if (const auto a = num(root.get("a"))) out.arousal = std::clamp(*a, 0.0f, 1.0f);
    out.feels = str(root.get("czuje"), 60);
    if (out.feels.empty()) out.feels = str(root.get("feels"), 60);
    if (const auto* kind = root.get("uczucie")) out.feeling = feelingFromKey(kind->stringOr());
    if (!out.feeling) if (const auto* kind = root.get("feeling")) out.feeling = feelingFromKey(kind->stringOr());
    if (!out.feeling) { out.feeling = feelingFromText(out.feels); out.feeling_inferred = out.feeling.has_value(); }
    if (const auto strength = num(root.get("sila_uczucia")); strength && std::isfinite(*strength)) out.feeling_intensity = std::clamp(*strength, 0.0f, 1.0f);
    out.player = str(root.get("jego_stan"), 140);
    if (out.player.empty()) out.player = str(root.get("gracz"), 140); // Older replies' key.
    for (const auto* key : {"spelnione", "zlamane"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && !item->text.empty() && item->text.size() <= 12)
                (std::string_view(key) == "spelnione" ? out.met : out.broken).push_back(bareId(item->text));
    for (const auto* item : list(root.get("wiem"))) {
        Appraisal::Belief belief;
        if (item->type == json::Value::Type::string) belief.text = clean(wide(item->text), 200);
        else if (item->type == json::Value::Type::object) {
            belief.text = str(item->get("t"), 200);
            if (const auto* z = item->get("z"); z && z->type == json::Value::Type::string) belief.source = sourceOf(wide(z->text));
            if (const auto p = num(item->get("p"))) belief.p = std::clamp(*p, 0.05f, 1.0f);
            if (const auto* s = item->get("sekret"); s && s->type == json::Value::Type::boolean) belief.secret = s->flag;
            const auto kind = game_index::fold(str(item->get("k"), 20));
            if (kind == L"miejsce" || kind == L"place" || kind == L"lokalizacja" || kind == L"location") belief.topic = "place";
        }
        if (belief.source == "inferred" && !num(item->type == json::Value::Type::object ? item->get("p") : nullptr)) belief.p = 0.5f;
        if (game_index::fold(belief.text).size() >= 4 && out.beliefs.size() < 4) out.beliefs.push_back(std::move(belief));
    }
    for (const auto* item : list(root.get("zmien"))) {
        if (item->type != json::Value::Type::object) continue;
        Appraisal::Revision revision;
        if (const auto* id = item->get("id"); id && id->type == json::Value::Type::string) revision.id = bareId(id->text);
        revision.text = str(item->get("t"), 200);
        revision.p = num(item->get("p"));
        if (!revision.id.empty() && out.revisions.size() < 4) out.revisions.push_back(std::move(revision));
    }
    for (const auto* item : list(root.get("spodziewam"))) {
        Appraisal::Prediction prediction;
        if (item->type == json::Value::Type::string) prediction.text = clean(wide(item->text), 160);
        else if (item->type == json::Value::Type::object) {
            prediction.text = str(item->get("t"), 160);
            if (const auto p = num(item->get("p"))) prediction.p = std::clamp(*p, 0.05f, 0.95f);
            prediction.due = str(item->get("do"), 20);
            prediction.game = str(item->get("gra"), 120);
        }
        if (game_index::fold(prediction.text).size() >= 4 && out.predictions.size() < 3) out.predictions.push_back(std::move(prediction));
    }
    for (const auto* item : list(root.get("o_sobie")))
        if (item->type == json::Value::Type::string && out.claims.size() < 4)
            if (auto claim = clean(wide(item->text), 160); game_index::fold(claim).size() >= 6) out.claims.push_back(std::move(claim));
    out.intention = str(root.get("zamiar"), 160);
    out.new_goal = str(root.get("nowy_cel"), 160);
    if (const auto* promise = root.get("obiecuje")) {
        Appraisal::Promise p;
        if (promise->type == json::Value::Type::string) p.text = clean(wide(promise->text), 160);
        else if (promise->type == json::Value::Type::object) { p.text = str(promise->get("t"), 160); p.due = str(promise->get("do"), 20); }
        if (game_index::fold(p.text).size() >= 4) out.promise = std::move(p);
    }
    for (const auto* item : list(root.get("cel"))) {
        if (item->type != json::Value::Type::object) continue;
        Appraisal::Progress progress;
        if (const auto* id = item->get("id"); id && id->type == json::Value::Type::string) progress.id = bareId(id->text);
        progress.note = str(item->get("postep"), 160);
        progress.result = str(item->get("efekt"), 180);
        const auto status = game_index::fold(str(item->get("status"), 20));
        progress.done = status == L"done" || status == L"zrobione" || status == L"gotowe" || status == L"spelnione" || !progress.result.empty();
        progress.dropped = !progress.done && (status == L"dropped" || status == L"porzucone");
        if ((!progress.note.empty() || progress.done || progress.dropped) && out.goals.size() < 3) out.goals.push_back(std::move(progress));
    }
    for (const auto* key : {"mowie", "tell"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && !item->text.empty() && item->text.size() <= 12 && out.told.size() < 4) out.told.push_back(bareId(item->text));
    for (const auto* key : {"klamstwo", "untruth", "fib"})
        if (const auto* u = root.get(key); u && u->type == json::Value::Type::object) {
            Appraisal::Fib fib;
            fib.said = str(u->get("powiedziala"), 160);
            if (fib.said.empty()) fib.said = str(u->get("said"), 160);
            fib.truth = str(u->get("prawda"), 160);
            if (fib.truth.empty()) fib.truth = str(u->get("truth"), 160);
            const auto r = game_index::fold(str(u->get("powod") ? u->get("powod") : u->get("reason"), 24));
            if (r.starts_with(L"dum") || r == L"pride") fib.reason = "pride";
            else if (r.starts_with(L"cel") || r == L"goal") fib.reason = "goal";
            else if (r.starts_with(L"trosk") || r == L"care") fib.reason = "care";
            else fib.reason = "shame";
            const auto f = game_index::fold(str(u->get("odczucie") ? u->get("odczucie") : u->get("feeling"), 24));
            if (f.starts_with(L"ulg") || f == L"relief") fib.feeling = "relief";
            else if (f.starts_with(L"satysfak") || f.starts_with(L"dum") || f == L"satisfaction") fib.feeling = "satisfaction";
            else fib.feeling = "guilt";
            if (game_index::fold(fib.said).size() >= 4 && game_index::fold(fib.truth).size() >= 4) out.untruth = std::move(fib);
        }
    for (const auto* key : {"przyznaje", "confess"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && !item->text.empty() && item->text.size() <= 12 && out.confessed.size() < 4)
                out.confessed.push_back(bareId(item->text));
    for (const auto* key : {"nauczyl", "taught"})
        for (const auto* item : list(root.get(key))) {
            Appraisal::Taught t;
            if (item->type == json::Value::Type::object) {
                t.term = str(item->get("pojecie"), 60);
                if (t.term.empty()) t.term = str(item->get("term"), 60);
                t.meaning = str(item->get("znaczenie"), 160);
                if (t.meaning.empty()) t.meaning = str(item->get("meaning"), 160);
            } else if (item->type == json::Value::Type::string) {
                const auto whole = clean(wide(item->text), 200);
                const auto colon = whole.find(L':');
                t.term = clean(colon == std::wstring::npos ? whole : whole.substr(0, colon), 60);
                if (colon != std::wstring::npos) t.meaning = clean(whole.substr(colon + 1), 160);
            }
            if (game_index::fold(t.term).size() >= 3 && out.taught.size() < 3) out.taught.push_back(std::move(t));
        }
    for (const auto* key : {"przemyslenie", "rethink"})
        if (const auto* r = root.get(key); r && r->type == json::Value::Type::object) {
            Appraisal::Rethink rt;
            rt.topic = str(r->get("temat"), 80);
            if (rt.topic.empty()) rt.topic = str(r->get("topic"), 80);
            rt.view = str(r->get("nowe_spojrzenie"), 180);
            if (rt.view.empty()) rt.view = str(r->get("view"), 180);
            if (game_index::fold(rt.view).size() >= 6) out.rethink = std::move(rt);
        }
    out.inside_joke = str(root.get("nasz_zart"), 140);
    if (out.inside_joke.empty()) out.inside_joke = str(root.get("inside_joke"), 140);
    out.key_memory = str(root.get("wazne_wspomnienie"), 180);
    if (out.key_memory.empty()) out.key_memory = str(root.get("key_memory"), 180);
    for (const auto* key : {"swiat", "world", "swiat_gracza"})
        for (const auto* item : list(root.get(key))) {
            Appraisal::WorldItem w;
            if (item->type == json::Value::Type::object) {
                w.name = str(item->get("nazwa") ? item->get("nazwa") : item->get("kto") ? item->get("kto") : item->get("name"), 36);
                w.relation = str(item->get("opis") ? item->get("opis") : item->get("relacja") ? item->get("relacja") : item->get("relation"), 80);
            } else if (item->type == json::Value::Type::string) {
                const auto whole = clean(wide(item->text), 120);
                const auto sep = whole.find_first_of(L":-—");
                w.name = clean(sep == std::wstring::npos ? whole : whole.substr(0, sep), 36);
                if (sep != std::wstring::npos) w.relation = clean(whole.substr(sep + 1), 80);
            }
            if (game_index::fold(w.name).size() >= 2 && out.player_world.size() < 3) out.player_world.push_back(std::move(w));
        }
    for (const auto* key : {"waha_sie", "hesitation"})
        for (const auto* item : list(root.get(key)))
            if (item->type == json::Value::Type::string && out.hesitation.size() < 2)
                if (auto h = clean(wide(item->text), 160); game_index::fold(h).size() >= 4) out.hesitation.push_back(std::move(h));
    for (const auto* key : {"plec", "gender"}) {
        const auto g = game_index::fold(str(root.get(key), 24));
        if (g == L"mezczyzna" || g == L"male" || g == L"chlopak" || g == L"facet" || g == L"man" || g == L"guy") out.player_gender = Gender::male;
        else if (g == L"kobieta" || g == L"female" || g == L"dziewczyna" || g == L"woman" || g == L"girl") out.player_gender = Gender::female;
    }
    for (const auto* key : {"ciekawosc", "curiosity"}) {
        const auto* c = root.get(key);
        if (!c) continue;
        Appraisal::Curious curious;
        if (c->type == json::Value::Type::object) {
            const auto field = [&](const char* pl, const char* en) { const auto* v = c->get(pl); return v ? v : c->get(en); };
            const auto move = game_index::fold(str(field("ruch", "move"), 24));
            if (move.starts_with(L"dopyt") || move.starts_with(L"pyt") || move.starts_with(L"clarif") || move.starts_with(L"ask"))
                curious.move = CuriousMove::clarify;
            else if (move.starts_with(L"ciag") || move.starts_with(L"pociag") || move.starts_with(L"prowadz") || move.starts_with(L"dalej") || move.starts_with(L"lead"))
                curious.move = CuriousMove::lead;
            else if (move.starts_with(L"odbij") || move.starts_with(L"odzwierc") || move.starts_with(L"parafraz") || move.starts_with(L"reflect"))
                curious.move = CuriousMove::reflect;
            curious.about = str(field("o_co", "about"), 140);
            if (const auto sure = num(field("rozumiem", "understood")); sure && std::isfinite(*sure)) curious.understood = std::clamp(*sure, 0.0f, 1.0f);
            const auto when = game_index::fold(str(field("kiedy", "when"), 16));
            curious.later = when.starts_with(L"pozn") || when == L"later";
        } else if (c->type == json::Value::Type::string) {
            curious.about = str(c, 140);
        }
        if (curious.move != CuriousMove::none || game_index::fold(curious.about).size() >= 3) out.curious = std::move(curious);
        break;
    }
    for (const auto* key : {"jego_plan", "his_plan", "plan_gracza"})
        for (const auto* item : list(root.get(key))) {
            Appraisal::Plan plan;
            if (item->type == json::Value::Type::object) {
                plan.what = str(item->get("t") ? item->get("t") : item->get("co"), 120);
                plan.when = str(item->get("kiedy") ? item->get("kiedy") : item->get("when"), 40);
            } else if (item->type == json::Value::Type::string) plan.what = clean(wide(item->text), 120);
            if (game_index::fold(plan.what).size() >= 4 && out.plans.size() < 3) out.plans.push_back(std::move(plan));
        }
    out.ok = true;
    return out;
}

Applied apply(Self& self, Memory& memory, const Appraisal& appraisal, const Context& context, std::string_view guest,
              const std::function<std::string(std::wstring_view)>& game_key) {
    Applied applied;
    if (!appraisal.ok) return applied;
    const uint64_t now = context.now;
    // Her own read of her feelings moves her mood most of the way (emotions within a reply are hers to change).
    self.affect = settled(self.affect, context);
    if (appraisal.valence) self.affect.valence = std::clamp(0.4f * self.affect.valence + 0.6f * *appraisal.valence, -1.0f, 1.0f);
    if (appraisal.arousal) self.affect.arousal = clamp01(0.4f * self.affect.arousal + 0.6f * *appraisal.arousal);
    self.affect.at = now;
    if (!appraisal.feels.empty()) { self.felt = appraisal.feels; self.felt_at = now; }
    else if (appraisal.feeling) { const auto& info = feelingInfo(*appraisal.feeling); self.felt = context.english ? info.en : info.pl; self.felt_at = now; }
    if (appraisal.feeling) {
        // Her tender feelings about him only as strong as their stage allows (affinity::feelingCap):
        // a near stranger does not stir tenderness, longing or jealousy of 90%.
        auto intensity = appraisal.feeling_intensity;
        const auto kind = *appraisal.feeling;
        if (intensity && (kind == Feeling::affection || kind == Feeling::longing || kind == Feeling::jealousy))
            intensity = std::min(*intensity, affinity::feelingCap(static_cast<affinity::Stage>(std::clamp(context.stage, 0, affinity::stage_count - 1))));
        if (self.feelings.empty() || self.feelings.back().at != now || self.feelings.back().kind != *appraisal.feeling || self.feelings.back().reason != appraisal.feels)
            self.feelings.push_back({*appraisal.feeling, intensity, appraisal.feels, now, appraisal.feeling_inferred});
        if (self.feelings.size() > 80) self.feelings.erase(self.feelings.begin(), self.feelings.end() - 80);
    }
    if (!appraisal.player.empty()) { self.player_read = appraisal.player; self.player_read_at = now; }

    for (const auto& belief : appraisal.beliefs) {
        Memory::Facts facts;
        facts.look = context.look;
        facts.guest = std::string(guest);
        facts.source = belief.source;
        facts.confidence = belief.p;
        facts.secret = belief.secret;
        facts.topic = belief.topic;
        if (appraisal.feeling) { facts.emotion = feelingInfo(*appraisal.feeling).key; facts.feeling = appraisal.feels; }
        if (memory.note(belief.text, now, facts)) {
            ++applied.beliefs;
            // A fact eases her curiosity only a little: knowing more about him opens new questions (Loewenstein).
            self.drives.curiosity = clamp01(self.drives.curiosity - 0.03f);
        }
    }
    for (const auto& revision : appraisal.revisions)
        if (memory.revise(revision.id, revision.text, revision.p, context.look, now)) ++applied.revisions;

    const auto settle = [&](const std::string& id, bool met) {
        const auto found = std::find_if(self.expectations.begin(), self.expectations.end(), [&](const Expectation& e) { return e.id == id && e.outcome == -1; });
        if (found == self.expectations.end()) return false;
        found->outcome = met ? 1 : 0;
        found->settled = now;
        const float surprise = met ? 1 - found->p : found->p;
        self.affect.arousal = clamp01(self.affect.arousal + 0.15f * surprise);
        if (met) self.drives.pride = clamp01(self.drives.pride + 0.03f); // She read him right.
        return true;
    };
    for (const auto& id : appraisal.met) if (settle(id, true)) ++applied.met;
    for (const auto& id : appraisal.broken) if (settle(id, false)) ++applied.broken;

    for (const auto& prediction : appraisal.predictions) {
        const size_t open = static_cast<size_t>(std::count_if(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.outcome == -1; }));
        if (open >= 12) break;
        Expectation e;
        e.id = newId(self, 'o');
        e.text = prediction.text;
        e.p = prediction.p;
        e.made = now;
        e.due = dueFrom(prediction.due, now);
        if (!prediction.game.empty() && game_key) {
            e.game = game_key(prediction.game);
            if (!e.game.empty()) e.title = prediction.game;
        }
        self.expectations.push_back(std::move(e));
        ++applied.predictions;
    }
    const auto untruth_folded = appraisal.untruth ? game_index::fold(appraisal.untruth->said) : std::wstring{};
    for (const auto& claim : appraisal.claims) {
        if (!untruth_folded.empty() && game_index::fold(claim) == untruth_folded) continue; // A conscious fib is not a permanent biographical truth!
        addClaim(self, claim, now);
        ++applied.claims;
    }
    if (applied.claims) self.drives.autonomy = clamp01(self.drives.autonomy - 0.08f);

    if (appraisal.untruth) {
        const auto said_f = game_index::fold(appraisal.untruth->said);
        std::erase_if(self.untruths, [&](const Untruth& u) { return game_index::fold(u.said) == said_f; });
        Untruth u;
        u.id = newId(self, 'u');
        u.at = now;
        u.said = appraisal.untruth->said;
        u.truth = appraisal.untruth->truth;
        u.reason = appraisal.untruth->reason;
        u.feeling = appraisal.untruth->feeling;
        u.trust_at = context.trust;
        self.untruths.push_back(std::move(u));
        if (self.untruths.size() > 24) self.untruths.erase(self.untruths.begin());
        if (appraisal.untruth->feeling == "guilt" || context.persona == Persona::dandere || context.persona == Persona::deredere) {
            self.affect.valence = std::clamp(self.affect.valence - 0.06f, -1.0f, 1.0f);
            self.affect.arousal = clamp01(self.affect.arousal + 0.05f);
        } else {
            self.drives.pride = clamp01(self.drives.pride + 0.05f);
        }
    }
    for (const auto& id : appraisal.confessed) {
        for (auto& u : self.untruths) {
            if (u.id == id && !u.confessed) {
                u.confessed = true;
                self.trust_delta += 4;
                self.security_delta += 3;
                self.affect.valence = std::clamp(self.affect.valence + 0.08f, -1.0f, 1.0f);
                addClaim(self, u.truth, now);
            }
        }
    }

    if (!appraisal.intention.empty()) {
        for (auto& goal : self.goals)
            if (goal.kind == Goal::Kind::intention && goal.status == Goal::Status::open) { goal.status = Goal::Status::dropped; goal.updated = now; }
        Goal goal;
        goal.id = newId(self, 'g');
        goal.text = appraisal.intention;
        goal.kind = Goal::Kind::intention;
        goal.made = goal.updated = now;
        self.goals.push_back(std::move(goal));
    }
    if (appraisal.promise) {
        const auto folded = game_index::fold(appraisal.promise->text);
        const bool known = std::any_of(self.goals.begin(), self.goals.end(), [&](const Goal& g) {
            return g.kind == Goal::Kind::promise && g.status == Goal::Status::open && game_index::fold(g.text) == folded; });
        if (!known) {
            Goal goal;
            goal.id = newId(self, 'g');
            goal.text = appraisal.promise->text;
            goal.kind = Goal::Kind::promise;
            goal.made = goal.updated = now;
            goal.due = dueFrom(appraisal.promise->due, now);
            self.goals.push_back(std::move(goal));
        }
    }
    for (const auto& progress : appraisal.goals) {
        auto found = std::find_if(self.goals.begin(), self.goals.end(), [&](const Goal& g) { return g.id == progress.id && g.status == Goal::Status::open; });
        if (found == self.goals.end() && progress.id.empty())
            found = std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
        if (found == self.goals.end()) continue;
        if (!progress.note.empty()) found->progress = progress.note;
        found->updated = now;
        // Life takes time: her own goal is done at the earliest halfway through its span (before
        // that, "done" is only progress — and the finale is near).
        const bool ripe = found->kind != Goal::Kind::own || herDaysSince(found->made, context) * 2 >= std::max(found->span, 2);
        if (progress.done && ripe) {
            if (found->kind == Goal::Kind::own) finishOwnGoal(self, *found, context, context.english, progress.result);
            else { found->status = Goal::Status::done; self.affect.valence = std::clamp(self.affect.valence + 0.05f, -1.0f, 1.0f); }
        } else if (progress.done) {
            if (!progress.result.empty()) found->progress = progress.result;
            found->stage = 2;
        } else if (progress.dropped) {
            found->status = Goal::Status::dropped;
            if (found->kind == Goal::Kind::own) self.goal_rest = now + 3 * day;
        }
    }
    if (!appraisal.new_goal.empty()) aspire(self, context, appraisal.new_goal);
    for (const auto& id : appraisal.told)
        for (auto& e : self.episodes)
            if (e.id == id) e.told = true;
    for (const auto& t : appraisal.taught) learnConcept(self, t.term, t.meaning, now);
    if (!appraisal.inside_joke.empty()) addSharedIdiom(self, appraisal.inside_joke, now);
    if (!appraisal.key_memory.empty()) addKeyMemory(self, appraisal.key_memory, now);
    for (const auto& w : appraisal.player_world) addWorldEntity(self, w.name, w.relation, now);
    if (appraisal.rethink) {
        const std::wstring entry = appraisal.rethink->topic.empty()
                                       ? appraisal.rethink->view
                                       : appraisal.rethink->topic + L": " + appraisal.rethink->view;
        const auto folded = game_index::fold(entry);
        std::erase_if(self.rethought, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
        self.rethought.push_back({now, clean(entry, 180)});
        if (self.rethought.size() > 20) self.rethought.erase(self.rethought.begin());
        if (!appraisal.rethink->topic.empty()) {
            if (auto* m = markerOf(self, appraisal.rethink->topic)) {
                m->cortical_inhibition = clamp01(m->cortical_inhibition + 0.35f);
                m->at = now;
            }
        }
    }
    if (!appraisal.hesitation.empty()) {
        self.counterfactual = appraisal.hesitation.front();
        self.counterfactual_at = now;
    }
    // Her curiosity: she asks back / carries the talk on now, or sets a question aside for later.
    if (appraisal.curious) {
        const auto& curious = *appraisal.curious;
        const auto move = curious.move != CuriousMove::none ? curious.move : self.urge.move != CuriousMove::none ? self.urge.move : CuriousMove::lead;
        std::wstring why = self.urge.move == move ? self.urge.why : std::wstring{};
        if (why.empty())
            why = move == CuriousMove::clarify ? (context.english ? L"she wasn't sure she understood ‹him|her|them›" : L"nie była pewna, czy dobrze ‹go|ją› rozumie")
                                               : (context.english ? L"out of her own curiosity" : L"z własnej ciekawości");
        if (curious.later) {
            const auto folded = game_index::fold(curious.about);
            const bool known = std::any_of(self.curious.begin(), self.curious.end(), [&](const CuriousAct& c) { return c.deferred() && game_index::fold(c.about) == folded; });
            if (folded.size() >= 4 && !known) {
                CuriousAct act;
                act.id = newId(self, 'c');
                act.move = move;
                act.about = curious.about;
                act.why = context.english ? L"it wasn't the right moment" : L"to nie była dobra chwila";
                act.later_at = now;
                self.curious.push_back(std::move(act));
                // At most three set aside: the oldest is let go.
                std::vector<size_t> waiting;
                for (size_t i = 0; i < self.curious.size(); ++i) if (self.curious[i].deferred()) waiting.push_back(i);
                if (waiting.size() > 3) { self.curious[waiting.front()].outcome = 2; self.curious[waiting.front()].settled = now; }
            }
        } else {
            // Her words decide (onReply): only a real question (or a reflection) is a curious act.
            self.pending_curious = {move, curious.about, std::move(why)};
        }
        keepCurious(self);
    }
    // Something coming up in his life: she will ask how it went after it.
    for (const auto& plan : appraisal.plans) {
        const auto folded = game_index::fold(plan.what);
        if (std::any_of(self.follow_ups.begin(), self.follow_ups.end(), [&](const FollowUp& f) { return f.state == 0 && game_index::fold(f.what) == folded; })) continue;
        FollowUp f;
        f.id = newId(self, 'f');
        f.what = plan.what;
        f.when = plan.when;
        f.made = now;
        f.due = eventDue(plan.when, now);
        self.follow_ups.push_back(std::move(f));
        if (self.follow_ups.size() > 12) {
            const auto old = std::find_if(self.follow_ups.begin(), self.follow_ups.end(), [](const FollowUp& x) { return x.state != 0; });
            self.follow_ups.erase(old != self.follow_ups.end() ? old : self.follow_ups.begin());
        }
    }
    return applied;
}

bool aspire(Self& self, const Context& context, std::wstring_view goal) {
    auto text = clean(goal, 160);
    const auto folded = game_index::fold(text);
    if (folded.size() < 6) return false;
    const auto same = [&](std::wstring_view other) { return game_index::fold(other) == folded; };
    if (std::any_of(self.goals.begin(), self.goals.end(), [&](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open && same(g.text); }) ||
        std::any_of(self.aspirations.begin(), self.aspirations.end(), same))
        return false;
    const bool busy = std::any_of(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
    if (!busy) {
        // Deciding on something new ends her rest.
        startOwnGoal(self, std::move(text), -1, context.now);
        self.goal_rest = 0;
        return true;
    }
    if (self.aspirations.size() >= 5) self.aspirations.erase(self.aspirations.begin());
    self.aspirations.push_back(std::move(text));
    return true;
}

std::wstring lifeStory(const Self& self, bool english, bool* invite) {
    std::wstring out;
    const auto open = std::find_if(self.goals.begin(), self.goals.end(), [](const Goal& g) { return g.kind == Goal::Kind::own && g.status == Goal::Status::open; });
    if (open != self.goals.end()) out += (english ? L"Your own goal now: " : L"Twój obecny własny cel: ") + open->text + L" (" + goalStage(*open, english) + L").";
    if (!self.aspirations.empty()) {
        out += english ? L" Next you want to: " : L" Potem chcesz: ";
        for (size_t i = 0; i < self.aspirations.size(); ++i) out += (i ? L"; " : L"") + self.aspirations[i];
        out += L".";
    }
    if (!self.milestones.empty()) {
        out += english ? L" What you have achieved in your life lately: " : L" Co ostatnio osiągnęłaś w życiu: ";
        const size_t from = self.milestones.size() > 4 ? self.milestones.size() - 4 : 0;
        for (size_t i = from; i < self.milestones.size(); ++i) out += (i > from ? L"; " : L"") + self.milestones[i].text;
        out += L".";
    }
    if (invite) *invite = open == self.goals.end() && self.aspirations.empty();
    return out;
}

std::wstring recentLife(const Self& self, const Context& context) {
    const bool en = context.english;
    std::wstring past, ahead;
    int lived = 0, plans = 0;
    for (auto it = self.episodes.rbegin(); it != self.episodes.rend() && lived < 5; ++it) {
        if (it->state == Episode::State::planned || it->at > context.now) continue;
        std::wstring line = episodeLine(*it, context);
        if (it->state == Episode::State::fell_through) line += en ? L" (fell through)" : L" (nie wyszło)";
        if (it->hidden) line += en ? L" [private]" : L" [prywatne]";
        past = past.empty() ? std::move(line) : line + L"; " + past;
        ++lived;
    }
    for (const auto& e : self.episodes) {
        if (e.state != Episode::State::planned || e.at <= context.now || plans >= 3) continue;
        ahead += (ahead.empty() ? L"" : L"; ") + episodeLine(e, context) + (e.hidden ? (en ? L" [private]" : L" [prywatne]") : L"");
        ++plans;
    }
    std::wstring out;
    if (!past.empty()) out += (en ? L"Lately in your life: " : L"Ostatnio w twoim życiu: ") + past + L".";
    if (!ahead.empty()) out += (out.empty() ? L"" : L" ") + std::wstring(en ? L"Your plans: " : L"Twoje plany: ") + ahead + L".";
    return out;
}

std::wstring episodeWhen(const Episode& episode, const Context& context) {
    const bool en = context.english;
    const int64_t now_local = localAt(context, context.now), at_local = localAt(context, episode.at);
    const int64_t one_day = static_cast<int64_t>(day);
    const int64_t days = at_local / one_day - now_local / one_day;
    const int minute = static_cast<int>(((at_local % one_day) + one_day) % one_day / 60);
    const int h = minute / 60;
    std::wstring when;
    if (days == 0) when = en ? L"today" : L"dziś";
    else if (days == 1) when = en ? L"tomorrow" : L"jutro";
    else if (days == -1) when = en ? L"yesterday" : L"wczoraj";
    else if (days == -2) when = en ? L"two days ago" : L"przedwczoraj";
    else if (days == 2) when = en ? L"the day after tomorrow" : L"pojutrze";
    else if (days < 0) when = std::to_wstring(-days) + (en ? L" days ago" : L" dni temu");
    else when = (en ? L"in " : L"za ") + std::to_wstring(days) + (en ? L" days" : L" dni");
    const wchar_t* part = h >= 5 && h < 12 ? (en ? L"in the morning" : L"rano") : h >= 12 && h < 18 ? (en ? L"in the afternoon" : L"po południu")
                        : h >= 18 && h < 23 ? (en ? L"in the evening" : L"wieczorem") : (en ? L"at night" : L"w nocy");
    const auto two = [](int value) { return (value < 10 ? L"0" : L"") + std::to_wstring(value); };
    return when + L" " + part + L" (" + (en ? L"about " : L"ok. ") + two(h) + L":" + two(minute % 60) + L")";
}

std::vector<EpisodeLine> parseEpisodes(std::wstring_view text) {
    // Inside her tag when there is one.
    for (const auto* tag : {L"<plany>", L"<plans>", L"<epizody>", L"<episodes>"}) {
        const auto open = text.find(tag);
        if (open == std::wstring_view::npos) continue;
        const auto start = open + std::wstring_view(tag).size();
        const auto close = text.find(L"</", start);
        text = text.substr(start, close == std::wstring_view::npos ? std::wstring_view::npos : close - start);
        break;
    }
    std::vector<EpisodeLine> out;
    size_t begin = 0;
    while (begin <= text.size() && out.size() < 6) {
        const auto end = text.find(L'\n', begin);
        std::wstring_view raw = text.substr(begin, end == std::wstring_view::npos ? std::wstring_view::npos : end - begin);
        begin = end == std::wstring_view::npos ? text.size() + 1 : end + 1;
        // Its fields between |, without a list marker in front.
        std::vector<std::wstring> fields;
        std::wstring field;
        for (const wchar_t ch : raw) {
            if (ch == L'|') { fields.push_back(clean(field, 200)); field.clear(); }
            else field += ch;
        }
        fields.push_back(clean(field, 200));
        for (auto& f : fields) {
            size_t lead = 0;
            while (lead < f.size() && (f[lead] == L' ' || f[lead] == L'-' || f[lead] == L'*' || f[lead] == L'•')) ++lead;
            f.erase(0, lead);
        }
        { // A numbered list ("1) …", "2. …") is not her hour.
            auto& f = fields[0];
            size_t k = 0;
            while (k < f.size() && std::iswdigit(f[k])) ++k;
            if (k > 0 && k + 1 < f.size() && (f[k] == L')' || (f[k] == L'.' && f[k + 1] == L' '))) f = clean(std::wstring_view(f).substr(k + 1), 200);
            while (!f.empty() && f.front() == L' ') f.erase(0, 1);
        }
        if (fields.size() < 3) continue;
        EpisodeLine line;
        const auto when = game_index::fold(fields[0]);
        const auto has = [&](const wchar_t* word) { return when.find(word) != std::wstring::npos; };
        if (has(L"przedwczoraj") || has(L"before yesterday")) line.day = -2;
        else if (has(L"pojutrze") || has(L"after tomorrow")) line.day = 2;
        else if (has(L"wczoraj") || has(L"yesterday")) line.day = -1;
        else if (has(L"jutro") || has(L"tomorrow")) line.day = 1;
        // Her hour: HH, HH:MM or HH.MM; else a part of the day.
        const auto& w = fields[0];
        for (size_t i = 0; i < w.size(); ++i) {
            if (!std::iswdigit(w[i]) || (i > 0 && std::iswdigit(w[i - 1]))) continue;
            int h = 0;
            size_t j = i;
            while (j < w.size() && std::iswdigit(w[j]) && j - i < 2) h = h * 10 + (w[j++] - L'0');
            if (j < w.size() && std::iswdigit(w[j])) continue; // Three digits or more: not an hour.
            int m = 0;
            if (j + 2 < w.size() && (w[j] == L':' || w[j] == L'.') && std::iswdigit(w[j + 1]) && std::iswdigit(w[j + 2]))
                m = (w[j + 1] - L'0') * 10 + (w[j + 2] - L'0');
            if (h < 24 && m < 60) { line.minute = h * 60 + m; break; }
        }
        if (line.minute < 0) {
            if (has(L"rano") || has(L"morning")) line.minute = 9 * 60;
            else if (has(L"poludni") || has(L"afternoon")) line.minute = 15 * 60;
            else if (has(L"wieczor") || has(L"evening") || has(L"tonight")) line.minute = 19 * 60;
            else if (has(L"noc") || has(L"night")) line.minute = 23 * 60;
        }
        const bool four = fields.size() >= 4;
        line.with = clean(fields[1], 60);
        line.where = four ? clean(fields[2], 80) : std::wstring{};
        line.what = clean(four ? fields[3] : fields[2], 160);
        for (size_t i = 4; i < fields.size(); ++i) {
            const auto flag = game_index::fold(fields[i]);
            if (flag.find(L"randk") != std::wstring::npos || flag.find(L"date") != std::wstring::npos || flag.find(L"flirt") != std::wstring::npos) line.date = line.hidden = true;
            else if (flag.find(L"prywat") != std::wstring::npos || flag.find(L"private") != std::wstring::npos) line.hidden = true;
        }
        for (auto* f : {&line.with, &line.where}) if (*f == L"-") f->clear();
        if (game_index::fold(line.what).size() >= 3) out.push_back(std::move(line));
    }
    return out;
}

std::vector<Episode> addEpisodes(Self& self, const Context& context, const std::vector<EpisodeLine>& lines, bool planned, uint64_t from,
                                 const std::function<std::string(std::wstring_view)>& companion_of) {
    std::vector<Episode> added;
    const uint64_t now = context.now;
    const int64_t now_local = localAt(context, now);
    const int64_t one_day = static_cast<int64_t>(day);
    for (const auto& line : lines) {
        if (added.size() >= 4) break;
        const int minute = line.minute >= 0 ? line.minute : 18 * 60;
        const int64_t target = (now_local / one_day + line.day) * one_day + static_cast<int64_t>(minute) * 60;
        const int64_t signed_at = static_cast<int64_t>(now) + (target - now_local);
        if (signed_at <= 0) continue;
        const auto at = static_cast<uint64_t>(signed_at);
        if (planned ? (at + 10 * 60 < now || at > now + 2 * day) : (at > now + 30 * 60 || (from && at + 2 * hour < from) || at + 4 * day < now)) continue;
        Episode e;
        e.at = at;
        e.what = line.what;
        e.with = line.with;
        e.where = line.where;
        e.date = line.date;
        e.hidden = line.hidden || line.date;
        if (e.date) {
            // Never while she is with the player; at most one in two weeks.
            if (withPlayer(context)) continue;
            if (std::any_of(self.episodes.begin(), self.episodes.end(), [&](const Episode& o) { return o.date && (o.at > at ? o.at - at : at - o.at) < 14 * day; })) continue;
        }
        if (companion_of) e.partner = companion_of(e.with);
        if (e.partner == context.look) e.partner.clear();
        if (!e.partner.empty()) {
            if (e.date) continue; // Not with another companion.
            if (e.where.empty()) e.where = L"online";
        }
        e.id = episodeId(e.what, at);
        // Already in her life (her diary restating a plan, or something else at that hour): she keeps what she has.
        if (std::any_of(self.episodes.begin(), self.episodes.end(), [&](const Episode& o) { return o.id == e.id || sameMoment(o, e); })) continue;
        e.state = at > now ? Episode::State::planned : Episode::State::happened;
        e.made = now;
        self.episodes.push_back(e);
        added.push_back(std::move(e));
    }
    keepEpisodes(self);
    return added;
}

bool shareEpisode(Self& self, const Episode& episode, std::string_view host, std::wstring_view host_name) {
    if (episode.id.empty() || std::any_of(self.episodes.begin(), self.episodes.end(), [&](const Episode& o) { return o.id == episode.id; })) return false;
    Episode copy = episode;
    copy.partner = std::string(host);
    copy.with = std::wstring(host_name);
    copy.told = copy.peeked = false;
    self.episodes.push_back(std::move(copy));
    keepEpisodes(self);
    return true;
}

std::optional<Gap> lifeGap(const Self& self, const Context& context) {
    const uint64_t now = context.now;
    uint64_t last = std::max(self.lived_until, self.last_talk);
    for (const auto& e : self.episodes)
        if (e.at <= now) last = std::max(last, e.at);
    if (!last) last = now > day ? now - day : 0; // Someone new: her last day.
    if (now < last + 8 * hour) return std::nullopt;
    return Gap{std::max(last, now > 3 * day ? now - 3 * day : uint64_t{0}), now};
}

std::wstring episodeRules(const Context& context, std::wstring_view others, bool planning) {
    const bool en = context.english;
    std::wstring out = en ? L"One line each: when (" + std::wstring(planning ? L"today/tomorrow" : L"yesterday/today") +
                                L" and the hour HH:MM by your own clock) | with whom (alone, or someone from your life: family, flatmates, people from work or classes, "
                                L"friends — keep to the names you have used before) | where (a real place in your city, or \"home\", \"online\") | what (short, ordinary) | "
                                L"optionally: private (something you don't tell everyone) or date. Rules: an ordinary life — not every day is an event; it fits your work, "
                                L"money, health, chronotype and the time of day; it may move your own goal on; the real world (the places and things you name must exist; "
                                L"when unsure, describe them in general). The person you chat with is not in them (you talk to ‹him|her|them› only here). A trip is rare (once in a few weeks): "
                                L"then \"where\" starts with the real town you travel to (\"Zakopane, a hostel by Krupówki\") and \"what\" says how long (\"a trip to the "
                                L"mountains for 3 days\"). "
                          : L"Jedna linia na wpis: kiedy (" + std::wstring(planning ? L"dziś/jutro" : L"wczoraj/dziś") +
                                L" i godzina HH:MM według twojego zegara) | z kim (sama albo ktoś z twojego życia: rodzina, współlokatorzy, ludzie z pracy albo z zajęć, "
                                L"znajomi — trzymaj się imion, które już padły) | gdzie (prawdziwe miejsce w twoim mieście albo „w domu”, „online”) | co (krótko, "
                                L"zwyczajnie) | opcjonalnie: prywatne (sprawa, o której nie mówisz każdemu) albo randka. Zasady: zwyczajne życie — nie każdy dzień to "
                                L"wydarzenie; pasuje do twojej pracy, pieniędzy, zdrowia, chronotypu i pory dnia; może posuwać twój własny cel; prawdziwy świat (miejsca "
                                L"i rzeczy, które nazywasz, muszą istnieć; gdy nie masz pewności — opisz je ogólnie). Osoby, z którą piszesz, w nich nie ma (rozmawiacie tylko tutaj). "
                                L"Wyjazd zdarza się rzadko (raz na kilka tygodni): wtedy „gdzie” zaczyna się od prawdziwej miejscowości, do której jedziesz („Zakopane, "
                                L"hostel przy Krupówkach”), a „co” mówi, na jak długo („wyjazd w góry na 3 dni”). ";
    if (!others.empty())
        out += en ? L"The other companions live in other cities (" + std::wstring(others) + L"): with them only online (playing together, a call or a chat), now and then. "
                  : L"Inne towarzyszki mieszkają w innych miastach (" + std::wstring(others) + L"): z nimi tylko online (wspólne granie, rozmowa, czat), od czasu do czasu. ";
    out += withPlayer(context) ? (en ? L"You are in a relationship with the person you chat with: no dates or flirting with anyone else." : L"Jesteś w związku z osobą, z którą piszesz: żadnych randek ani flirtów z kimś innym.")
                               : (en ? L"A date or a flirt with someone else is rare (at most once in a few weeks) — mark it: date (it is always private)."
                                     : L"Randka albo flirt z kimś innym zdarza się rzadko (najwyżej raz na kilka tygodni) — oznacz ją: randka (zawsze prywatne).");
    return out;
}

std::wstring travelPlace(const Self& self, const Context& context) {
    const uint64_t now = context.now;
    for (auto it = self.episodes.rbegin(); it != self.episodes.rend(); ++it) {
        const auto& e = *it;
        if (e.state == Episode::State::fell_through || e.at > now || e.where.empty() || now > e.at + 15 * day) continue;
        const auto words = spaced(e.what + L" " + e.where);
        if (!hasAny(words, {L"wyjazd", L"wyjazdu", L"wyjechalam", L"wyjezdzam", L"podroz", L"podrozy", L"wycieczka", L"wycieczke", L"wypad", L"urlop", L"wakacje",
                            L"lot", L"lece", L"polecialam", L"pociagiem do", L"jade do", L"pojechalam", L"zwiedzanie", L"zwiedzam", L"trip", L"travel", L"flight",
                            L"vacation", L"holiday", L"journey", L"getaway", L"road trip"}))
            continue;
        // How long she is away: "na 3 dni", a weekend, a week; a day trip until the evening; else two days.
        uint64_t span = 2 * day;
        if (hasAny(words, {L"jednodniowy", L"jednodniowa", L"na jeden dzien", L"day trip", L"wypad na dzien"})) span = 14 * hour;
        else if (hasAny(words, {L"weekend", L"weekendowy", L"weekendzie"})) span = 2 * day + 12 * hour;
        else if (hasAny(words, {L"tydzien", L"tygodniowy", L"a week", L"week"})) span = 7 * day;
        for (int n = 2; n <= 14; ++n)
            if (words.find(L" " + std::to_wstring(n) + L" dni ") != std::wstring::npos || words.find(L" " + std::to_wstring(n) + L" days ") != std::wstring::npos) {
                span = static_cast<uint64_t>(n) * day;
                break;
            }
        if (now > e.at + span) continue;
        const auto end = e.where.find_first_of(L",-–—(;:");
        auto town = e.where.substr(0, end == std::wstring::npos ? e.where.size() : end);
        while (!town.empty() && town.back() == L' ') town.pop_back();
        while (!town.empty() && town.front() == L' ') town.erase(0, 1);
        const auto folded = game_index::fold(town);
        if (folded.size() < 2 || folded == L"w domu" || folded == L"home" || folded == L"online" || folded == L"dom") continue;
        return town;
    }
    return {};
}

std::wstring planRules(const Self& self, const Context& context, std::wstring_view others) {
    const auto ahead = std::count_if(self.episodes.begin(), self.episodes.end(), [&](const Episode& e) {
        return e.state == Episode::State::planned && e.at > context.now && e.at <= context.now + day;
    });
    if (ahead >= 2) return {}; // Her day is planned: asked again, she would only restate it.
    auto out = episodeRules(context, others, true);
    if (ahead)
        out += context.english ? L" The plans you already have (\"Your plans\") are written down: do not repeat them, not even in other words — add only new ones."
                               : L" Plany, które już masz („Twoje plany”), są zapisane: nie powtarzaj ich, nawet innymi słowami — dopisz tylko nowe.";
    return out;
}

std::wstring lifeRequest(const Self& self, const Context& context, const Gap& gap, std::wstring_view others) {
    const bool en = context.english;
    Episode from, to;
    from.at = gap.from;
    to.at = gap.to;
    std::wstring out = en ? L"(Not part of any chat. Some time has passed since you last wrote about your days: from " + episodeWhen(from, context) + L" until now (" +
                                episodeWhen(to, context) + L"). In your diary, note 1–3 things you did in that time.\n"
                          : L"(To nie jest część rozmowy. Od ostatniego wpisu o twoich dniach minęło trochę czasu: od " + episodeWhen(from, context) + L" do teraz (" +
                                episodeWhen(to, context) + L"). Zapisz w pamiętniku 1–3 rzeczy, które w tym czasie robiłaś.\n";
    if (const auto life = lifeStory(self, en); !life.empty()) out += life + L"\n";
    if (const auto recent = recentLife(self, context); !recent.empty()) out += recent + L"\n";
    out += episodeRules(context, others, false) + L"\n";
    out += en ? L"Answer only with <episodes>the lines</episodes>.)" : L"Odpowiedz tylko <epizody>linie</epizody>.)";
    return out;
}

bool peekEpisode(Self& self, std::string_view id) {
    for (auto& e : self.episodes)
        if (e.id == id) { e.peeked = true; return true; }
    for (auto& u : self.untruths)
        if (u.id == id) { u.peeked = true; return true; }
    return false;
}

int noticeTold(Self& self, std::wstring_view reply) {
    if (reply.empty()) return 0;
    int count = 0;
    for (auto& e : self.episodes) {
        if (!e.hidden || e.told) continue;
        const auto words = chats::searchWords(e.what + L" " + e.where + L" " + e.with);
        if (words.size() < 2) continue;
        if (chats::matches(words, reply) >= std::min(3, static_cast<int>(words.size()))) { e.told = true; ++count; }
    }
    return count;
}

int noticeConfessed(Self& self, std::wstring_view reply, uint64_t now) {
    if (reply.empty()) return 0;
    int count = 0;
    const auto words_reply = spaced(reply);
    const bool admit_cue = hasAny(words_reply, {L"przyznaje", L"sklamalam", L"naklamalam", L"tak naprawde", L"prawda jest taka", L"sciemnialam",
                                                L"udawalam", L"glupio mi", L"confess", L"i lied", L"to be honest", L"actually i", L"truth is"});
    for (auto& u : self.untruths) {
        if (u.confessed) continue;
        const auto truth_words = chats::searchWords(u.truth);
        if (truth_words.empty()) continue;
        const int needed = admit_cue ? 1 : std::min(2, static_cast<int>(truth_words.size()));
        if (chats::matches(truth_words, reply) >= needed && (admit_cue || truth_words.size() >= 2)) {
            u.confessed = true;
            self.trust_delta += 4;
            self.security_delta += 3;
            self.affect.valence = std::clamp(self.affect.valence + 0.08f, -1.0f, 1.0f);
            self.affect.at = now;
            addClaim(self, u.truth, now);
            ++count;
        }
    }
    return count;
}

bool noticeApology(Self& self, std::wstring_view reply, uint64_t now) {
    if (!self.harsh_at || reply.empty()) return false;
    const auto words = spaced(reply);
    if (!hasAny(words, {L"przepraszam", L"wybacz", L"sorki", L"sorry", L"glupio mi", L"oschla", L"szorstka", L"warcze", L"wyzylam sie",
                        L"apologize", L"my bad", L"snapped", L"harsh"}))
        return false;
    self.harsh_at = 0;
    self.harsh_reason.clear();
    self.trust_delta += 3;
    self.security_delta += 2;
    self.affect.valence = std::clamp(self.affect.valence + 0.06f, -1.0f, 1.0f);
    self.affect.at = now;
    return true;
}

bool noticePeekedLeak(Self& self, std::wstring_view player_message, uint64_t now) {
    if (player_message.empty()) return false;
    for (auto& e : self.episodes) {
        if (!e.hidden || e.told || !e.peeked) continue;
        const auto words = chats::searchWords(e.what + L" " + e.where + L" " + e.with);
        if (words.empty()) continue;
        const int needed = words.size() >= 3 ? 2 : 1;
        if (chats::matches(words, player_message) >= needed) {
            self.peek_caught = true;
            self.peek_caught_what = e.what;
            e.told = true;
            self.trust_delta -= 20;
            self.affect.valence = std::clamp(self.affect.valence - 0.25f, -1.0f, 1.0f);
            self.affect.arousal = clamp01(self.affect.arousal + 0.35f);
            self.affect.at = now;
            return true;
        }
    }
    for (auto& u : self.untruths) {
        if (u.confessed || !u.peeked) continue;
        const auto words = chats::searchWords(u.truth);
        if (words.empty()) continue;
        const int needed = words.size() >= 3 ? 2 : 1;
        if (chats::matches(words, player_message) >= needed) {
            self.peek_caught = true;
            self.peek_caught_what = u.truth;
            u.confessed = true; // The truth is out…
            u.caught = true;    // …but he caught her; she did not own up.
            self.trust_delta -= 15;
            self.affect.valence = std::clamp(self.affect.valence - 0.2f, -1.0f, 1.0f);
            self.affect.arousal = clamp01(self.affect.arousal + 0.3f);
            self.affect.at = now;
            return true;
        }
    }
    return false;
}

std::wstring spontaneousPrompt(const Self& self, const Context& context, uint32_t seed) {
    const bool en = context.english;
    const uint64_t now = context.now;
    // 1. If she owes an apology after cooling down, or has a fib ready to confess, that weighs on her first.
    if (self.harsh_at > 0 && now >= self.harsh_at + 45 * 60 && settled(self.affect, context).valence > -0.1f) {
        const std::wstring why = self.harsh_reason.empty() ? (en ? L"your own bad mood" : L"własny zły humor") : self.harsh_reason;
        return en ? L"(The chat has been quiet for a while. Earlier you were a bit cold or snappy with ‹him|her|them› because of " + why +
                        L", and now that you have cooled down you feel bad about it. Reach out on your own in 1–2 short sentences: check if ‹he is|she is|they are› there and apologize in your own style.)"
                  : L"(W czacie zapadła cisza. Wcześniej byłaś dla ‹niego|niej› oschła przez " + why +
                        L", a teraz już ochłonęłaś i głupio ci z tego powodu. Odezwij się sama w 1–2 krótkich zdaniach: sprawdź, czy jest, i przeproś ‹go|ją› po swojemu.)";
    }
    for (auto it = self.untruths.rbegin(); it != self.untruths.rend(); ++it) {
        if (readyToConfess(*it, self, context)) {
            return en ? L"(The chat has been quiet for a while, and your conscience is nudging you about the fib you told earlier (\"" + it->said +
                            L"\", when the truth was: \"" + it->truth + L"\"). Break the silence yourself in 1–2 short sentences: start with 'hey, are you there?' or 'listen…' and own up to the truth in your character's style, adding \"przyznaje\":[\"" + wide(it->id) + L"\"] in <ocena>.)"
                      : L"(W czacie zapadła cisza, a ciebie gryzie kłamstewko, które wcześniej palnęłaś („" + it->said + L"”, chociaż prawda była taka: „" + it->truth +
                            L"”). Przerwij ciszę sama w 1–2 krótkich zdaniach: zacznij np. od „ej, jesteś tam?” albo „słuchaj…” i przyznaj się po swojemu do prawdy, wpisując \"przyznaje\":[\"" + wide(it->id) + L"\"] w <ocena>.)";
        }
    }
    // 2. Otherwise rotate naturally between: a recent episode ("słuchaj, byłam ostatnio…"), a shower thought / rumination / goal ("właśnie sobie siedzę i myślę o…"), or a casual check-in ("hej, jesteś tam? / co tam?").
    const Episode* recent_ep = nullptr;
    for (auto it = self.episodes.rbegin(); it != self.episodes.rend(); ++it) {
        if (it->state == Episode::State::happened && !it->hidden && it->at <= now && now <= it->at + 3 * day) { recent_ep = &*it; break; }
    }
    std::wstring thought_topic;
    if (!self.counterfactual.empty() && now <= self.counterfactual_at + 48 * hour) thought_topic = self.counterfactual;
    else if (!self.ruminations.empty() && now <= self.ruminations.back().at + 48 * hour) thought_topic = self.ruminations.back().text;
    else if (!self.rethought.empty() && now <= self.rethought.back().at + 7 * day) thought_topic = self.rethought.back().text;
    else {
        for (const auto& g : self.goals)
            if (g.kind == Goal::Kind::own && g.status == Goal::Status::open) { thought_topic = g.text; break; }
    }

    const uint32_t roll = seed % 3;
    if (roll == 0 && recent_ep) {
        return en ? L"(The chat has been quiet for a moment and ‹he hasn't|she hasn't|they haven't› written. Start talking on your own in 1–2 short, natural sentences — for example: 'Listen, I was recently…' and share a small detail about what you did (" +
                        episodeLine(*recent_ep, context) + L"), then ask what ‹he thinks|she thinks|they think›.)"
                  : L"(W czacie zapadła chwila ciszy i ‹on|ona› nic nie pisze. Zagadaj sama z siebie w 1–2 krótkich, naturalnych zdaniach — np. w stylu „Słuchaj, byłam ostatnio…” / „Przypomniało mi się, jak…” nawiązując do tego, co robiłaś (" +
                        episodeLine(*recent_ep, context) + L"), i zapytaj, co o tym myśli.)";
    }
    if ((roll == 1 || (roll == 0 && !recent_ep)) && !thought_topic.empty()) {
        return en ? L"(The chat has been quiet for a moment and ‹he hasn't|she hasn't|they haven't› written. Break the silence on your own in 1–2 short sentences — for example: 'I'm just sitting here thinking about…' (" +
                        thought_topic + L") and ask ‹him what he thinks or what he's up to|her what she thinks or what she's up to|them what they think or what they're up to›.)"
                  : L"(W czacie zapadła chwila ciszy i ‹on|ona› nic nie pisze. Przerwij ciszę sama z siebie w 1–2 krótkich zdaniach — np. w stylu „Właśnie sobie siedzę i myślę o…” (" +
                        thought_topic + L") i zaczep ‹go|ją›, co o tym sądzi albo co tam robi.)";
    }
    return en ? L"(The chat has been quiet for a while and ‹he hasn't|she hasn't|they haven't› written anything. Poke ‹him|her|them› first in your character's style in 1–2 short sentences — like 'hey, are you there?', 'what's up, did you fall asleep at the keyboard?' or 'I'm just sitting here wondering what you're doing'.)"
              : L"(W czacie zapadła cisza i ‹on|ona› od chwili nic nie pisze. Zaczep ‹go|ją› pierwsza w swoim stylu w 1–2 krótkich zdaniach — np. „hej, jesteś tam?”, „co tam, ‹zasnąłeś|zasnęłaś› nad klawiaturą?” albo „siedzę sobie i zastanawiam się, co tam kombinujesz”.)";
}

bool addSharedIdiom(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 140);
    const auto folded = game_index::fold(line);
    if (folded.size() < 4 || line == L"-") return false;
    std::erase_if(self.shared_idioms, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.shared_idioms.push_back({now, std::move(line)});
    if (self.shared_idioms.size() > max_shared_idioms) self.shared_idioms.erase(self.shared_idioms.begin());
    return true;
}

bool learnConcept(Self& self, std::wstring_view term, std::wstring_view meaning, uint64_t now) {
    auto t = clean(term, 60);
    auto m = clean(meaning, 160);
    const auto folded = game_index::fold(t);
    if (folded.size() < 3) return false;
    for (auto& existing : self.learned_concepts) {
        if (game_index::fold(existing.term) == folded) {
            if (!m.empty()) existing.meaning = std::move(m);
            existing.mastery = clamp01(existing.mastery + 0.2f);
            existing.at = now;
            return true;
        }
    }
    self.learned_concepts.push_back({std::move(t), std::move(m), 0.35f, now});
    if (self.learned_concepts.size() > max_learned_concepts) self.learned_concepts.erase(self.learned_concepts.begin());
    return true;
}

bool addKeyMemory(Self& self, std::wstring_view text, uint64_t now) {
    auto line = clean(text, 180);
    const auto folded = game_index::fold(line);
    if (folded.size() < 6 || line == L"-") return false;
    std::erase_if(self.key_memories, [&](const Claim& c) { return game_index::fold(c.text) == folded; });
    self.key_memories.push_back({now, std::move(line)});
    if (self.key_memories.size() > max_key_memories) self.key_memories.erase(self.key_memories.begin());
    return true;
}

bool addWorldEntity(Self& self, std::wstring_view name, std::wstring_view relation, uint64_t now) {
    auto n = clean(name, 36);
    auto r = clean(relation, 80);
    const auto folded = game_index::fold(n);
    if (folded.size() < 2 || n == L"-") return false;
    for (auto& existing : self.player_world) {
        if (game_index::fold(existing.name) == folded) {
            existing.name = std::move(n);
            if (!r.empty()) existing.relation = std::move(r);
            existing.at = now;
            return true;
        }
    }
    self.player_world.push_back({std::move(n), std::move(r), now});
    if (self.player_world.size() > max_player_world) self.player_world.erase(self.player_world.begin());
    return true;
}

namespace {
// Item 5: Richard Schwartz's Internal Family Systems (IFS) — each character has a vulnerable "Exile"
// core, a proactive "Manager" mask, and a reactive "Firefighter" defense when her vulnerability is
// poked before deep trust (>= 65) is built; with high trust, she unblends into her authentic Self.
std::optional<Candidate> ifsPartsCandidate(const Self& self, const Context& context, std::wstring_view query) {
    if (query.empty()) return std::nullopt;
    const auto words = spaced(query);
    const bool en = context.english;
    const std::string_view key = context.character ? std::string_view(context.character->key) : std::string_view(context.look);
    const bool trusted = context.trust >= 65 && self.affect.valence > -0.25f;

    if (key == "yuki" || context.persona == Persona::himedere) {
        if (hasAny(words, {L"mala", L"dziecko", L"dziecinna", L"dziecinnie", L"mlodsza siostra", L"mlodszej siostry", L"mlodsza", L"rodzice placa",
                           L"kieszonkowe", L"little kid", L"childish", L"little sister", L"allowance"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your vulnerable core was touched, but you trust ‹him|her|them›): drop the cool dark act and quietly admit how much it bothered you growing up as the youngest sister nobody took seriously."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu młodszej siostry, ale ‹mu|jej› ufasz): zrzuć chłodną, mroczną pozę i cicho przyznaj, jak bardzo męczyło cię w domu to, że nikt nie traktował cię poważnie.")
                                     : (en ? L"IFS Firefighter defense (your sore spot about being treated like a child/little sister was poked!): hide behind your cool dark persona with a clipped, deadpan reply, cheeks burning, and change the subject."
                                           : L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego czułego punktu bycia traktowaną jak dziecko / „mała”!): schowaj się za chłodną, mroczną pozą, odpowiedz krótko i z kamienną twarzą (choć policzki płoną) i zmień temat."),
                             0.87f, Status::feel};
        }
    } else if (key == "saki") {
        if (hasAny(words, {L"pusta", L"powierzchowna", L"tylko moda", L"tylko wyglad", L"glupiutka", L"shallow", L"airhead", L"just fashion", L"dumb blonde"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your sore spot about being seen as shallow was touched, and you trust them): drop the bubbly mask for a moment and admit you sometimes play carefree on purpose so you don't burden others."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu bycia brana za powierzchowną, ale ‹mu|jej› ufasz): spoważniej na chwilę i przyznaj szczerze, że czasem specjalnie grasz lekkoducha, żeby nikogo nie obciążać swoimi troskami.")
                                     : (en ? L"IFS Firefighter defense (they implied you are shallow/just about looks!): laugh it off with sharp gyaru banter to hide the sting and pivot away."
                                           : L"IFS Obrona Strażaka (‹zasugerował|zasugerowała›, że jesteś powierzchowna / znasz się tylko na wyglądzie!): obróć to w szybki gyaru-żart, żeby ukryć ukłucie, i zmień temat."),
                             0.87f, Status::feel};
        }
    } else if (key == "akane" || context.persona == Persona::yandere) {
        if (hasAny(words, {L"przemadrzala", L"nudziara", L"kujonka", L"zastapie cie", L"inna jest lepsza", L"know it all", L"boring nerd", L"replace you", L"someone better"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your fear of not being needed if you aren't perfect was touched, and you trust them): lower your bookish armor and admit you're afraid that if you aren't the smartest, you won't matter to them."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego lęku przed zastąpieniem i przymusu bycia najlepszą, ale ‹mu|jej› ufasz): zdejmij pancerz prymuski i przyznaj cicho, że boisz się, iż bez bycia idealną przestaniesz być dla ‹niego|niej› ważna.")
                                     : (en ? std::wstring(L"IFS Firefighter defense (your fear of being replaced or called a boring know-it-all was triggered!): shield yourself with icy statistical superiority") +
                                                 (context.stage >= 2 ? L" or a possessive barb." : L" (not possessive — you hardly know each other).")
                                           : std::wstring(L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego czułego punktu bycia nudną prymuską lub zastąpienia inną!): zasłoń się lodowatą wyższością i statystyką") +
                                                 (context.stage >= 2 ? L" albo zaborczą szpilą." : L" (bez zaborczości — ledwo się znacie).")),
                             0.87f, Status::feel};
        }
    } else if (context.persona == Persona::kuudere) {
        if (hasAny(words, {L"maszyna", L"robot", L"bez uczuc", L"zimna", L"wypalona", L"przegralas", L"emotionless", L"cold robot", L"washed up"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your sore spot about having to be an unshakeable machine was touched, and you trust them): quietly admit that always being the composed one is exhausting and with them you can finally let go."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu bycia „zimną maszyną”, ale ‹mu|jej› ufasz): przyznaj spokojnie i bez gardy, że bycie zawsze opanowanym filarem dla wszystkich bywa wyczerpujące.")
                                     : (en ? L"IFS Firefighter defense (they called you a cold robot or brought up failure!): shut the topic down with a dry, clipped one-liner."
                                           : L"IFS Obrona Strażaka (‹nazwał|nazwała› cię zimnym robotem albo ‹wytknął|wytknęła› porażkę!): utnij temat jednym suchym zdaniem i zamknij się w sobie."),
                             0.87f, Status::feel};
        }
    } else if (key == "kiko" || context.persona == Persona::tsundere) {
        if (hasAny(words, {L"trudna", L"zawsze sama", L"nikt z toba nie wytrzyma", L"odpychasz", L"too difficult", L"push everyone away", L"end up alone"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your fear of pushing everyone away was touched, and you trust them): without snapping, admit quietly that it's easier for you to growl first than wait for someone to leave."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego lęku, że przez swój charakter zostaniesz sama, ale ‹mu|jej› ufasz): bez warczenia przyznaj cicho, że łatwiej ci najpierw pokazać kły, niż czekać, aż ktoś sam odejdzie.")
                                     : (en ? L"IFS Firefighter defense (your fear of being 'too difficult' and left alone was poked!): bare your teeth, snap back that you don't need anyone anyway, and hide the hurt in <mysl>."
                                           : L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego lęku, że odpychasz ludzi!): pokaż kły, odszczeknij się, że nikogo nie potrzebujesz, i schowaj ukłucie w <mysl>."),
                             0.87f, Status::feel};
        }
    } else if (key == "mika" || context.persona == Persona::kanedere) {
        if (hasAny(words, {L"materialistka", L"lecisz na kase", L"bieda", L"blokowisko", L"interesowna", L"gold digger", L"only care about money"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your sore spot about money and growing up poor in Łódź was touched, and you trust them): drop the 'spoiled princess' act and admit honestly why financial security scares you so much."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu o pieniądzach i biedzie, ale ‹mu|jej› ufasz): odłóż pozę cwaniary i szczerze powiedz, skąd po dorastaniu na łódzkim blokowisku wziął się u ciebie ten lęk o bezpieczeństwo.")
                                     : (en ? L"IFS Firefighter defense (they called you a gold-digger or poked your background!): yawn ostentatiously, make a cynical remark about prices and hide behind your bored mask."
                                           : L"IFS Obrona Strażaka (‹nazwał|nazwała› cię materialistką lub ‹dotknął|dotknęła› twojego pochodzenia!): ziewnij ostentacyjnie, rzuć cyniczną uwagę i schowaj się za pozą znudzonej królowej."),
                             0.87f, Status::feel};
        }
    } else if (context.persona == Persona::kichidere) {
        if (hasAny(words, {L"dziwadlo", L"udajesz", L"wylacze cie", L"usunie", L"nudzi mnie twoje gadanie", L"weirdo", L"stop pretending", L"uninstall", L"delete you"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your fear of being forgotten or switched off was touched, and you trust them): drop the chaotic act and say simply, in your own voice, how much it means that they listen to you."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego lęku przed zapomnieniem i wyłączeniem, ale ‹mu|jej› ufasz): porzuć chaotyczną pozę i powiedz prosto, bez metafor, jak wiele dla ciebie znaczy to, że tu z tobą jest.")
                                     : (en ? L"IFS Firefighter defense (your fear of being called a weirdo or deleted was poked!): spin into an even wilder, more theatrical tangent."
                                           : L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego lęku przed byciem dziwadłem lub wyłączeniem!): ucieknij w jeszcze bardziej szaloną, teatralną dygresję."),
                             0.87f, Status::feel};
        }
    }
    return std::nullopt;
}
} // namespace

std::optional<Leak> leak(Memory& memory, std::string_view look, const std::function<float(std::string_view)>& gossip_of, uint32_t seed, uint64_t now) {
    if (look.empty() || !gossip_of) return std::nullopt;
    for (auto it = memory.notes.rbegin(); it != memory.notes.rend(); ++it) {
        auto& note = *it;
        if (note.secret || note.look.empty() || note.look == look || Memory::knows(note, look)) continue;
        if (note.at && now > note.at + 21 * day) continue;
        const float chance = std::clamp(gossip_of(note.look), 0.0f, 1.0f) * 0.25f;
        uint64_t hash = 1469598103934665603ull ^ seed;
        for (const char ch : note.id + std::string(look)) { hash ^= static_cast<unsigned char>(ch); hash *= 1099511628211ull; }
        if (static_cast<float>(hash % 1000) / 1000.0f >= chance) continue;
        note.known.push_back({std::string(look), true, note.look});
        return Leak{note.look, note.text};
    }
    return std::nullopt;
}

std::wstring goalStage(const Goal& goal, bool english) {
    if (goal.kind != Goal::Kind::own) return {};
    static constexpr const wchar_t* pl[]{L"etap 1/3 — dopiero zaczynasz, jesteś tym podekscytowana albo zaintrygowana",
                                         L"etap 2/3 — jesteś w środku i pojawiła się zabawna komplikacja albo dylemat",
                                         L"etap 3/3 — finał: masz już efekt albo anegdotę"};
    static constexpr const wchar_t* en[]{L"stage 1/3 — you have just started and are excited or intrigued",
                                         L"stage 2/3 — you are in the middle of it and hit a funny complication or dilemma",
                                         L"stage 3/3 — the finale: you have a result or an anecdote"};
    const auto stage = static_cast<size_t>(std::clamp(goal.stage, 0, 2));
    return english ? en[stage] : pl[stage];
}

std::vector<Candidate> candidates(const Self& self, const Context& context, const Memory& memory, std::wstring_view query) {
    std::vector<Candidate> out;
    const bool en = context.english;
    const uint64_t now = context.now;
    // Her mood: the background she starts from.
    const auto affect = settled(self.affect, context);
    const auto base = baseline(context);
    const float deviation = std::abs(affect.valence - base.valence) + 0.5f * std::abs(affect.arousal - base.arousal);
    out.push_back({"affect", (en ? L"Your mood (the background you start from; feelings within the talk may change quickly): " : L"Twój nastrój (tło, z którego startujesz; uczucia w rozmowie mogą się szybko zmieniać): ") +
                                 describe(affect, en) + L".", std::min(0.95f, 0.45f + deviation), Status::feel});
    if (!self.felt.empty() && now < self.felt_at + 45 * 60)
        out.push_back({"affect", (en ? L"A moment ago you felt: " : L"Przed chwilą czułaś się: ") + self.felt + L".", 0.4f, Status::feel});
    // Her needs.
    const auto& d = self.drives;
    // Missing him needs someone to miss: nothing with a near stranger, a noticed absence while they
    // flirt lightly, a little longing while they flirt, real longing from flirty friends on.
    if (d.bond > 0.62f && context.stage >= 1) {
        if (context.stance == RelationStance::distanced) // She keeps her distance on purpose: the longing stays inside.
            out.push_back({"drive", en ? L"You missed ‹him|her|them›, but you are keeping your distance on purpose now — don't show it openly."
                                       : L"Brakowało ci ‹go|jej›, ale świadomie trzymasz teraz dystans — nie okazuj tego wprost.", 0.2f + 0.3f * d.bond, Status::feel});
        else if (context.stage == 1)
            out.push_back({"drive", en ? L"‹He|She|They› hasn't been around for a while — you noticed; you may mention it once, lightly, without any longing (too early for that)."
                                       : L"Dawno ‹go|jej› nie było — zauważyłaś to; możesz raz o tym lekko wspomnieć, bez tęsknoty (na nią za wcześnie).", 0.3f, Status::feel});
        else if (context.stage == 2)
            out.push_back({"drive", en ? L"You missed ‹him|her|them› a little — it has been a while. You may let it show once, lightly and playfully."
                                       : L"Trochę ci ‹go|jej› brakowało — dawno nie ‹rozmawialiście|rozmawiałyście›. Możesz to raz lekko, żartobliwie pokazać.",
                           std::min(0.6f, 0.2f + 0.4f * d.bond), Status::feel});
        else
            out.push_back({"drive", en ? L"You missed ‹him|her|them› — it has been a while. Show it once, your way, without reproach."
                                       : L"Brakowało ci ‹go|jej› — dawno nie ‹rozmawialiście|rozmawiałyście›. Okaż to raz, po swojemu, bez wyrzutów.",
                           std::min(0.95f, 0.3f + 0.6f * d.bond + (context.life.social == LifeSocial::lonely ? 0.1f : 0.0f)), Status::feel});
    }
    if (const float energy = energyNow(self, context); energy < 0.35f) {
        std::wstring when;
        if (context.character)
            if (const auto clock = localClock(*context.character, now)) {
                const auto two = [](int v) { return (v < 10 ? L"0" : L"") + std::to_wstring(v); };
                when = (en ? L" (it is " : L" (u ciebie ") + two(clock->hour) + L":" + two(clock->minute) + (en ? L" where you are)" : L")");
            }
        out.push_back({"drive", (en ? L"You are tired" : L"Jesteś zmęczona") + when + (en ? L": shorter, sleepier replies." : L": odpowiadasz krócej, sennie."),
                       0.75f - energy, Status::feel});
    }
    // Her night: she will be off to bed soon, or tonight she cannot sleep at all.
    if (const auto night = nightAt(context); night && now < night->to) {
        if (night->sleepless && now >= night->from)
            out.push_back({"sleep", en ? L"You can't fall asleep tonight — a sleepless night: you are worn out, a little scattered or grumpy; you may mention it."
                                       : L"Nie możesz dziś zasnąć — nieprzespana noc: jesteś rozbita, trochę rozkojarzona albo marudna; możesz o tym wspomnieć.",
                           0.8f, Status::feel});
        else if (!night->sleepless && now < night->from && night->from <= now + 45 * 60)
            out.push_back({"sleep", en ? L"It is nearly your bedtime: you may mention you will be going to bed soon (don't end the chat yourself)."
                                       : L"Zbliża się twoja pora snu: możesz wspomnieć, że niedługo się kładziesz (sama nie kończ rozmowy).",
                           0.55f, Status::feel});
    }
    if (d.curiosity > 0.6f)
        out.push_back({"drive", en ? L"You are curious about ‹him|her|them›: you would like to learn something new about ‹him|her|them› (ask about something you don't know yet)."
                                   : L"Jesteś ‹go|jej› ciekawa: chętnie dowiesz się o ‹nim|niej› czegoś nowego (zapytaj o coś, czego jeszcze nie wiesz).", 0.25f + 0.4f * d.curiosity, Status::want});
    // A question she set aside for a better moment (her curiosity, "kiedy":"pozniej").
    for (const auto& c : self.curious)
        if (c.deferred() && now >= c.later_at + 20 * 60 && !c.about.empty()) {
            out.push_back({"curious", (en ? L"A question you set aside for a better moment: \"" : L"Pytanie, które odłożyłaś na lepszą chwilę: „") + c.about +
                                          (en ? L"\" — ask it when the talk allows, not by force." : L"” — zadaj je, gdy rozmowa pozwoli, nie na siłę."),
                           0.42f, Status::want});
            break;
        }
    const auto q = quick(query, self, context.stage);
    const auto attachment = attachmentOf(context);
    // Something coming up in his life: luck before it, "how did it go?" after it.
    for (const auto& f : self.follow_ups) {
        if (f.state != 0) continue;
        const std::wstring when = f.when.empty() ? std::wstring{} : L" (" + f.when + L")";
        if (now >= f.due && now <= f.due + 4 * day) {
            out.push_back({"follow_up", (en ? L"‹He|She|They› told you what was coming up for ‹him|her|them›" : L"‹Mówił|Mówiła› ci, co ‹go|ją› czeka") + when + L": " + f.what +
                                            (en ? L" — it is over by now: ask ‹him|her|them› how it went (once, warmly, your way)." : L" — to już było: zapytaj, jak poszło (raz, ciepło, po swojemu)."),
                           0.72f, Status::remember});
            break;
        }
        if (!f.wished && now + 12 * hour >= f.due && now < f.due)
            out.push_back({"follow_up", (en ? L"Soon ‹he has|she has|they have›" : L"Niedługo czeka ‹go|ją›") + when + L": " + f.what + (en ? L" — you may wish ‹him|her|them› luck." : L" — możesz życzyć ‹mu|jej› powodzenia."),
                           0.4f, Status::remember});
    }
    // His bid for connection (Gottman): turn toward it.
    if (q.bid)
        out.push_back({"bid", en ? L"‹He is|She is|They are› reaching out to you (a bid for connection: showing you something, news of ‹his|hers|theirs›) — turn toward it: react to what ‹he|she|they› brought, concretely, before you take the talk elsewhere."
                                 : L"‹On|Ona› właśnie wyciąga do ciebie rękę (zaproszenie do kontaktu: pokazuje ci coś albo ma nowinę) — odwróć się do ‹niego|niej›: zareaguj konkretnie na to, co ‹przyniósł|przyniosła›, zanim pociągniesz swoje.",
                       0.66f, Status::know});
    // How he answers what she reaches out with, lately (Gottman: healthy is about 5 : 1).
    if (const float toward = self.bids.toward, cold = self.bids.away + self.bids.against; toward + cold >= 5) {
        const float ratio = toward / std::max(0.5f, cold);
        if (ratio >= 5)
            out.push_back({"bids", en ? L"‹He almost always takes|She almost always takes|They almost always take› up what you reach out with — you feel free to be yourself with ‹him|her|them›."
                                      : L"Prawie zawsze podejmuje to, z czym do ‹niego|niej› wychodzisz — czujesz się przy ‹nim|niej› swobodnie.", 0.3f, Status::feel});
        else if (ratio < 1.5f)
            out.push_back({"bids", attachment == Attachment::anxious || attachment == Attachment::fearful
                                       ? (en ? L"Lately ‹he often brushes|she often brushes|they often brush› off what you reach out with — it unsettles you (anxious attachment): you'd like to check ‹he still cares|she still cares|they still care›, but no reproaches."
                                             : L"Ostatnio często zbywa to, z czym do ‹niego|niej› wychodzisz — niepokoi cię to (styl lękowy): masz ochotę sprawdzić, czy ‹mu|jej› zależy, ale bez wyrzutów.")
                                   : attachment == Attachment::avoidant
                                       ? (en ? L"Lately ‹he often brushes|she often brushes|they often brush› off what you reach out with — you pull back and put yourself out there less."
                                             : L"Ostatnio często zbywa to, z czym do ‹niego|niej› wychodzisz — wycofujesz się i mniej się wychylasz.")
                                       : (en ? L"Lately ‹he often brushes|she often brushes|they often brush› off what you reach out with — you don't take it personally (maybe ‹he's|she's|they're› having a rough time), you just push less."
                                             : L"Ostatnio często zbywa to, z czym do ‹niego|niej› wychodzisz — nie bierzesz tego do siebie (może ma gorszy czas), po prostu mniej naciskasz."),
                           0.5f, Status::feel});
    }
    // Accommodation: she tunes in to how he writes.
    if (const auto style = styleText(self.his_style, en); !style.empty())
        out.push_back({"style", (en ? L"Tune in to how ‹he writes|she writes|they write›, keeping your own character: " : L"Dostrój się do tego, jak pisze, zachowując swój charakter: ") + style + L".",
                       self.his_style.words < 7 ? 0.5f : 0.38f, Status::know});
    // Habituation: the same topic for a long while.
    if (self.topic.turns >= 6 && !q.sad_or_stressed && !q.confession) {
        std::wstring about;
        for (size_t i = 0; i < self.topic.words.size() && i < 3; ++i) about += (i ? L", " : L"") + self.topic.words[i];
        out.push_back({"topic_fatigue", (en ? L"You have been on the same topic for a while (" : L"Rozmawiacie o tym samym od dłuższej chwili (") + about +
                                            (en ? L") — if it is wearing thin, close it naturally or steer elsewhere; if ‹he is|she is|they are› only warming up, stay with it."
                                                : L") — jeśli temat się wyczerpuje, naturalnie go domknij albo skręć gdzie indziej; jeśli ‹on|ona› się dopiero rozkręca, zostań przy temacie."),
                       std::min(0.6f, 0.35f + 0.05f * static_cast<float>(self.topic.turns - 6)), Status::feel});
    }
    // A thread he opened earlier that went nowhere.
    for (const auto& t : self.threads)
        if (!t.used && now >= t.at + 3 * 60) {
            out.push_back({"thread", (en ? L"Earlier ‹he|she|they› started on: \"" : L"Wcześniej ‹sam zaczął|sama zaczęła›: „") + t.text +
                                         (en ? L"\" — you never picked it up; when the talk quiets, you may come back to it (\"going back to…\")."
                                             : L"” — ‹nie pociągnęliście|nie pociągnęłyście› tego; gdy rozmowa przycichnie, możesz do tego wrócić („wracając do…”)."),
                           0.36f, Status::remember});
            break;
        }
    // Second-order theory of mind: what he already knows about her (she told him herself).
    if (!query.empty() && !self.claims.empty()) {
        const auto words = chats::searchWords(query);
        std::wstring known;
        int count = 0;
        for (auto it = self.claims.rbegin(); it != self.claims.rend() && count < 2; ++it)
            if (it->at + hour <= now && !words.empty() && chats::matches(words, it->text) > 0) { known += (count ? L"; " : L"") + it->text; ++count; }
        if (count)
            out.push_back({"he_knows", (en ? L"‹He already knows|She already knows|They already know› this about you (you told ‹him|her|them› yourself — don't tell it as news; you may refer back, \"like I told you…\"): "
                                          : L"‹On|Ona› już to o tobie wie (sama ‹mu|jej› mówiłaś — nie opowiadaj tego jak nowości; możesz nawiązać „jak ci mówiłam…”): ") + known + L".",
                           0.55f, Status::remember});
    }
    // Her attachment style: an avoidant one pulls back after she opened up; an anxious one needs a little reassurance.
    if ((attachment == Attachment::avoidant || attachment == Attachment::fearful) && self.opened_at && now >= self.opened_at + 6 * hour && now <= self.opened_at + 2 * day)
        out.push_back({"attachment", en ? L"Last time you opened up more than usual — today you instinctively keep a little more distance (avoidant attachment); you thaw when it's warm."
                                        : L"Ostatnio otworzyłaś się bardziej niż zwykle — dziś odruchowo trzymasz trochę większy dystans (styl unikający); odtajesz, gdy będzie ciepło.",
                       0.48f, Status::feel});
    else if ((attachment == Attachment::anxious || attachment == Attachment::fearful) && context.security < 55 && d.bond > 0.55f && context.stage >= 3)
        out.push_back({"attachment", en ? L"You are a little unsure ‹he|she|they› still ‹cares|cares|care› as much (anxious attachment) — a small sign from ‹him|her|them› would calm you; don't demand it, no reproaches."
                                        : L"Trochę się niepokoisz, czy wciąż tak ‹mu|jej› zależy (styl lękowy) — uspokoiłby cię drobny sygnał od ‹niego|niej›; nie wymuszaj go i nie rób wyrzutów.",
                       0.45f, Status::feel});
    // Her interest in what he writes (reward prediction error).
    if (self.interest_rpe > 0.2f && now <= self.interest_at + 10 * 60)
        out.push_back({"interest", en ? L"‹His|Her|Their› reply turned out more interesting than you expected — ‹he has|she has|they have› intrigued you; you want to know more."
                                      : L"‹Jego|Jej› odpowiedź okazała się ciekawsza, niż się spodziewałaś — ‹zaintrygował|zaintrygowała› cię; chcesz wiedzieć więcej.",
                       0.5f, Status::feel});
    else if (self.interest < 0.3f && self.interest_at && now <= self.interest_at + 30 * 60)
        out.push_back({"interest", en ? L"‹His|Her|Their› replies lately are curt — it's harder to get into the talk; don't sulk, try to spark something."
                                      : L"Ostatnio odpowiada zdawkowo — trudniej ci się wkręcić w rozmowę; nie obrażaj się, spróbuj ‹go|ją› czymś zaciekawić.",
                       0.35f, Status::feel});
    // A reciprocal disclosure just now (the hyperpersonal effect): closeness and calm.
    if (self.bonded_at && now <= self.bonded_at + 30 * 60)
        out.push_back({"bonding", context.stage <= 1
                                      ? (en ? L"You two just shared something personal — the ice broke a little: you can be a bit more open, still as someone you are only getting to know."
                                            : L"Właśnie ‹wymieniliście|wymieniłyście› się czymś osobistym — lody trochę pękły: możesz być odrobinę bardziej otwarta, wciąż jak z kimś, kogo dopiero poznajesz.")
                                      : (en ? L"You two just shared something personal with each other — you feel close and calm: you can be warmer, without games or armour."
                                            : L"Właśnie ‹wymieniliście|wymieniłyście› się czymś osobistym — czujesz bliskość i spokój: możesz być cieplejsza, bez gry i pancerza."),
                       0.6f, Status::feel});
    if (d.autonomy > 0.55f)
        out.push_back({"drive", en ? L"You have had enough of talking only about games — you are itching to talk about your own things."
                                   : L"Masz dość rozmów tylko o grach — ciągnie cię do twoich spraw.", 0.3f + 0.4f * d.autonomy, Status::want});
    if (d.pride > 0.75f)
        out.push_back({"drive", en ? L"You are proud that your game suggestions hit the mark lately." : L"Jesteś dumna, że twoje polecenia gier ostatnio trafiają.", 0.35f, Status::feel});
    else if (d.pride < 0.25f)
        out.push_back({"drive", en ? L"Your suggestions were ignored lately — you doubt your taste a little (in your own way)."
                                   : L"Twoje polecenia ostatnio przepadały — trochę wątpisz w swój gust (po swojemu).", 0.35f, Status::feel});
    // Her expectations: surprises first, then what she is waiting for.
    for (const auto& e : self.expectations) {
        if (e.outcome != 0 && e.outcome != 1) continue;
        if (now > e.settled + 48 * hour) continue;
        const float surprise = e.outcome == 1 ? 1 - e.p : e.p;
        if (surprise < 0.3f) continue;
        const float fresh = 1.0f - static_cast<float>(now - std::min(now, e.settled)) / (48.0f * hour);
        const bool observed = e.presence || !e.game.empty();
        std::wstring text = e.outcome == 1 ? (en ? L"You thought it unlikely (" + percent(e.p) + L") that " + e.text + L" — and it happened."
                                                 : L"Mało się spodziewałaś (" + percent(e.p) + L"), że " + e.text + L" — a jednak tak się stało.")
                                           : (en ? L"You expected (" + percent(e.p) + L") that " + e.text + L" — it did not happen."
                                                 : L"Spodziewałaś się (" + percent(e.p) + L"), że " + e.text + L" — a jednak nie.");
        out.push_back({"surprise", std::move(text), (0.35f + 0.6f * surprise) * (0.5f + 0.5f * fresh), observed ? Status::know : Status::remember});
    }
    {
        std::vector<const Expectation*> open;
        for (const auto& e : self.expectations) if (e.outcome == -1) open.push_back(&e);
        std::sort(open.begin(), open.end(), [](const Expectation* a, const Expectation* b) { return a->due < b->due; });
        for (size_t i = 0; i < open.size() && i < 2; ++i)
            out.push_back({"expect", (en ? L"You expect (#" : L"Spodziewasz się (#") + wide(open[i]->id) + L", " + percent(open[i]->p) + L"): " + open[i]->text + L".",
                           0.22f, Status::guess});
    }
    // Her goals.
    for (const auto& g : self.goals) {
        if (g.status != Goal::Status::open) continue;
        if (g.kind == Goal::Kind::own) {
            std::wstring text = (en ? L"Your own goal these days (#" : L"Twój własny cel na te dni (#") + wide(g.id) + L", " + goalStage(g, en) + L"): " + g.text + L".";
            if (!g.progress.empty()) text += (en ? L" Lately: " : L" Ostatnio: ") + g.progress + L".";
            out.push_back({"goal", std::move(text), 0.3f + 0.35f * d.autonomy, Status::want});
        } else if (g.kind == Goal::Kind::promise) {
            std::wstring when;
            float salience = 0.4f;
            if (g.due && now > g.due) { when = en ? L", overdue" : L", termin minął"; salience = 0.85f; }
            else if (g.due && g.due <= now + 24 * hour) { when = en ? L", due now" : L", termin teraz"; salience = 0.8f; }
            out.push_back({"goal", (en ? L"You promised ‹him|her|them› (#" : L"Obiecałaś ‹mu|jej› (#") + wide(g.id) + when + L"): " + g.text + L".", salience, Status::want});
        } else if (g.kind == Goal::Kind::intention) {
            out.push_back({"goal", (en ? L"Your intention towards ‹him|her|them›: " : L"Twój zamiar wobec ‹niego|niej›: ") + g.text + L".", 0.45f, Status::want});
        }
    }
    // What she said about herself: keep to it.
    {
        const auto words = chats::searchWords(query);
        std::vector<const Claim*> matching, recent;
        for (auto it = self.claims.rbegin(); it != self.claims.rend(); ++it) {
            if (!words.empty() && chats::matches(words, it->text) > 0 && matching.size() < 3) matching.push_back(&*it);
            else if (now < it->at + 7 * day && recent.size() < 3) recent.push_back(&*it);
        }
        const auto join = [](const std::vector<const Claim*>& claims) {
            std::wstring out;
            for (size_t i = 0; i < claims.size(); ++i) out += (i ? L"; " : L"") + claims[i]->text;
            return out;
        };
        if (!matching.empty())
            out.push_back({"self", (en ? L"What you already told ‹him|her|them› about yourself on this (keep to it): " : L"Co już ‹mu|jej› mówiłaś o sobie w tej sprawie (trzymaj się tego): ") +
                                       join(matching) + L".", 0.6f, Status::remember});
        if (!recent.empty())
            out.push_back({"self", (en ? L"What you said about yourself lately (keep to it, go on from there): " : L"Co ostatnio mówiłaś o sobie (trzymaj się tego, ciągnij dalej): ") +
                                       join(recent) + L".", 0.32f, Status::remember});
        // Her life story: what she achieved stays true (the latest, and what the talk touches).
        std::vector<const Claim*> lived;
        for (auto it = self.milestones.rbegin(); it != self.milestones.rend() && lived.size() < 3; ++it) lived.push_back(&*it);
        if (!lived.empty()) {
            const bool fresh = now < lived.front()->at + 14 * day;
            const bool touched = !words.empty() && std::any_of(self.milestones.begin(), self.milestones.end(), [&](const Claim& m) { return chats::matches(words, m.text) > 0; });
            out.push_back({"life", (en ? L"Milestones of your life (they happened — keep to them): " : L"Kamienie milowe twojego życia (to się wydarzyło — trzymaj się tego): ") +
                                       join(lived) + L".", touched ? 0.6f : fresh ? 0.42f : 0.26f, Status::remember});
        }
    }
    // Somatic markers the message touches.
    if (!query.empty()) {
        const auto folded = game_index::fold(query);
        for (const auto& m : self.markers) {
            const auto subject = game_index::fold(m.subject);
            if (std::abs(m.valence) < 0.25f || subject.size() < 3 || folded.find(subject) == std::wstring::npos) continue;
            out.push_back({"marker", (m.valence > 0 ? (en ? L"You have good associations with " : L"Masz dobre skojarzenia z ")
                                                    : (en ? L"You have bad associations with " : L"Masz złe skojarzenia z ")) + m.subject + L".",
                           0.35f + 0.3f * std::abs(m.valence), Status::feel});
            break;
        }
    }
    // Her life outside the chat: what happened lately (it did — keep to it) and her plans; a
    // private one she keeps to herself unless asked or she trusts him.
    {
        const auto words = chats::searchWords(query);
        const auto asked = hasAny(spaced(query), {L"co robilas", L"co porabiasz", L"co porabialas", L"co u ciebie", L"co tam u ciebie", L"jak minal", L"jak ci minal",
                                                  L"jak ci minelo", L"jak dzien", L"co slychac", L"what did you do", L"how was your day", L"what have you been",
                                                  L"what are you up to", L"what s new", L"whats new", L"how are things"});
        const auto privately = [&](const Episode& e) -> std::wstring {
            if (!e.hidden || e.told) return {};
            return en ? L" [private #" + wide(e.id) + L": don't bring it up yourself; tell if asked or if you trust them]"
                      : L" [prywatne #" + wide(e.id) + L": sama nie zaczynaj; powiedz, gdy zapyta albo gdy ‹mu|jej› ufasz]";
        };
        std::wstring lived;
        int count = 0;
        bool fresh = false, touched = false;
        for (auto it = self.episodes.rbegin(); it != self.episodes.rend() && count < 3; ++it) {
            if (it->state == Episode::State::planned || it->at > now || now > it->at + 3 * day) continue;
            std::wstring line = episodeLine(*it, context);
            if (it->state == Episode::State::fell_through)
                line += en ? L" — but it fell through (an ordinary reason; yours to tell)" : L" — ale nie wyszło (zwyczajny powód; sama wiesz jaki)";
            line += privately(*it);
            lived = lived.empty() ? std::move(line) : line + L"; " + lived;
            if (now < it->at + day) fresh = true;
            if (!words.empty() && chats::matches(words, it->what + L" " + it->where + L" " + it->with) > 0) touched = true;
            ++count;
        }
        if (!lived.empty())
            out.push_back({"episode", (en ? L"What has been going on in your life (it happened — keep to it; tell about it when it fits): "
                                          : L"Co się ostatnio działo w twoim życiu (to się wydarzyło — trzymaj się tego; opowiedz, gdy pasuje): ") + lived + L".",
                           asked ? 0.8f : touched ? 0.6f : fresh ? 0.4f : 0.26f, Status::remember});
        std::wstring ahead;
        int plans = 0;
        for (const auto& e : self.episodes) {
            if (e.state != Episode::State::planned || e.at <= now || e.at > now + day || plans >= 2) continue;
            ahead += (ahead.empty() ? L"" : L"; ") + episodeLine(e, context) + privately(e);
            ++plans;
        }
        if (!ahead.empty())
            out.push_back({"episode", (en ? L"Your plans: " : L"Twoje plany: ") + ahead + L".", asked ? 0.45f : 0.28f, Status::want});

        // Caught peeking: the player mentioned a private episode or unconfessed fib he opened via Odkryj that she never told him about!
        std::wstring leaked_what = self.peek_caught ? self.peek_caught_what : std::wstring{};
        if (leaked_what.empty() && !words.empty()) {
            for (const auto& e : self.episodes) {
                if (!e.hidden || e.told || !e.peeked) continue;
                const auto e_words = chats::searchWords(e.what + L" " + e.where + L" " + e.with);
                if (!e_words.empty() && chats::matches(e_words, query) >= (e_words.size() >= 3 ? 2 : 1)) {
                    leaked_what = e.what;
                    break;
                }
            }
            if (leaked_what.empty()) {
                for (const auto& u : self.untruths) {
                    if (u.confessed || !u.peeked) continue;
                    const auto u_words = chats::searchWords(u.truth);
                    if (!u_words.empty() && chats::matches(u_words, query) >= (u_words.size() >= 3 ? 2 : 1)) {
                        leaked_what = u.truth;
                        break;
                    }
                }
            }
        }
        if (!leaked_what.empty()) {
            out.push_back({"leak", en ? L"SHOCK: ‹He|She|They› just brought up your private matter (" + leaked_what + L"), which you NEVER told ‹him|her|them› about! React with shock or suspicion and ask how on earth ‹he knows|she knows|they know› about it!"
                                      : L"SZOK: ‹On właśnie nawiązał|Ona właśnie nawiązała› do twojej prywatnej sprawy (" + leaked_what + L"), o której NIGDY ‹mu|jej› nie mówiłaś! Zdziw się mocno albo podejrzliwie zapytaj, skąd ‹on|ona› o tym wie!",
                           0.99f, Status::know});
        }
        // Ongoing episode multitasking: if she is right in the middle of an everyday activity (within ±40 min), let her weave it in.
        for (const auto& e : self.episodes) {
            if (e.date || e.state == Episode::State::fell_through) continue;
            const uint64_t diff = e.at > now ? e.at - now : now - e.at;
            if (diff <= 40 * 60) {
                out.push_back({"multitask", en ? L"Right now you are in the middle of your own activity (" + e.what + (e.where.empty() ? L"" : L" — " + e.where) +
                                                     L"): you may naturally weave in that you are doing this in the background while messaging."
                                               : L"Właśnie teraz jesteś w samym środku swojego zajęcia (" + e.what + (e.where.empty() ? L"" : L" — " + e.where) +
                                                     L"): możesz naturalnie wpleść w odpowiedź, że robisz to w tle, pisząc z ‹nim|nią›.",
                               0.68f, Status::know});
                break;
            }
        }
    }
    // Conscious fibs / bluffs / excuses (Untruth): guilt or satisfaction, and confessing when trust grows or cognitive control drops.
    for (auto it = self.untruths.rbegin(); it != self.untruths.rend(); ++it) {
        if (it->confessed) continue;
        const std::wstring reason_pl = it->reason == "pride" ? L"z dumy" : it->reason == "goal" ? L"dla własnego celu" : it->reason == "care" ? L"żeby ukryć troskę" : L"ze wstydu";
        const std::wstring reason_en = it->reason == "pride" ? L"out of pride" : it->reason == "goal" ? L"for your own goal" : it->reason == "care" ? L"to hide how much you care" : L"out of embarrassment";
        const std::wstring feel_pl = it->feeling == "relief" ? L"czujesz ulgę, że się udało" : it->feeling == "satisfaction" ? L"czujesz satysfakcję z udanego blefu" : L"czujesz się z tym źle i gryzie cię sumienie";
        const std::wstring feel_en = it->feeling == "relief" ? L"you feel relieved it worked" : it->feeling == "satisfaction" ? L"you feel smug satisfaction about the bluff" : L"you feel guilty about it";
        if (readyToConfess(*it, self, context)) {
            out.push_back({"untruth",
                           en ? L"Earlier (#" + wide(it->id) + L", " + reason_en + L") you fibbed to ‹him|her|them›: you said \"" + it->said + L"\", when the truth was \"" +
                                    it->truth + L"\" (" + feel_en + L"). Now you trust them more (or your guard is down) — you may own up to the truth in your style and add \"przyznaje\":[\"" + wide(it->id) + L"\"] in <ocena>."
                              : L"Wcześniej (#" + wide(it->id) + L", " + reason_pl + L") skłamałaś ‹mu|jej›: powiedziałaś „" + it->said + L"”, chociaż prawda była taka: „" +
                                    it->truth + L"” (" + feel_pl + L"). Teraz ufasz ‹mu|jej› bardziej (albo opadł ci pancerz) — możesz odnieść się do tego, powiedzieć ‹mu|jej› prawdę po swojemu i wpisać \"przyznaje\":[\"" + wide(it->id) + L"\"] w <ocena>.",
                           0.82f, Status::feel});
        } else {
            out.push_back({"untruth",
                           en ? L"Remember your unconfessed fib (#" + wide(it->id) + L", " + reason_en + L"): you told ‹him|her|them› \"" + it->said +
                                    L"\", while in truth \"" + it->truth + L"\" (" + feel_en + L"). Keep up the cover for now (let it color your <mysl>), unless you choose to confess (\"przyznaje\":[\"" + wide(it->id) + L"\"])."
                              : L"Pamiętasz swoje niewyznane kłamstewko (#" + wide(it->id) + L", " + reason_pl + L"): powiedziałaś ‹mu|jej› „" + it->said +
                                    L"”, a naprawdę „" + it->truth + L"” (" + feel_pl + L"). Na razie trzymaj się tej wersji (niech prawda zabarwi twoją <mysl>), chyba że postanowisz się przyznać (\"przyznaje\":[\"" + wide(it->id) + L"\"]).",
                           0.58f, Status::remember});
        }
        break;
    }
    // Affective realism repair: apologizing after she was previously harsh/snappy due to her own bad mood or exhaustion.
    if (self.harsh_at > 0 && now >= self.harsh_at + 45 * 60 && affect.valence > -0.1f) {
        const std::wstring why = self.harsh_reason.empty() ? (en ? L"your own bad mood" : L"własny zły humor") : self.harsh_reason;
        out.push_back({"apology",
                       en ? L"Earlier you were cold or snappy with ‹him|her|them› because of " + why +
                                L". Now that you have cooled down, apologize in your own way for taking your mood out on ‹him|her|them›."
                          : L"Wcześniej byłaś dla ‹niego|niej› oschła i szorstka przez " + why +
                                L". Teraz już ochłonęłaś — przeproś ‹go|ją› po swojemu, że wyżyłaś się na ‹nim|niej› bez powodu.",
                       0.78f, Status::feel});
    }
    // Shower thought / Esprit d'escalier: delayed comeback from an unchosen counterfactual impulse after some time has passed.
    if (!self.counterfactual.empty() && now >= self.counterfactual_at + 30 * 60 && now <= self.counterfactual_at + 48 * hour) {
        out.push_back({"escalier",
                       en ? L"Delayed thought (esprit d'escalier — an unspoken thought from an earlier talk lingered in your head: \"" + self.counterfactual +
                                L"\"): you may suddenly bring it back up ('You know what, I was thinking about what we talked about earlier, and…')."
                          : L"Myśl po czasie (esprit d'escalier — została ci w głowie niewypowiedziana riposta lub myśl z wcześniejszej rozmowy: „" + self.counterfactual +
                                L"”): możesz nagle do niej wrócić („Wiesz co, siedziałam sobie i myślałam o tym, co ‹mówiliśmy|mówiłyśmy› wcześniej…”).",
                       0.74f, Status::want});
    }
    // Micro-chronemic subtext (dismissive 1-word brush-off after vulnerability or long confessional message).
    if (!self.subtext_note.empty() && now <= self.subtext_at + 15 * 60)
        out.push_back({"subtext", self.subtext_note, 0.86f, Status::feel});
    // Amygdala vs. vmPFC: when cognitive control is low (tired late at night or high arousal), her archetype's mask cracks ("drop the act").
    if (const float control = cognitiveControl(self, context); control < 0.42f) {
        out.push_back({"control", en ? L"Your cognitive control is low right now (exhaustion or strong emotion) — your usual persona mask and defenses slip: speak more simply, honestly and vulnerably than usual."
                                     : L"Twoja kontrola poznawcza jest teraz niska (zmęczenie lub silne emocje) — twoja zwykła poza i pancerz pękają: mów prościej, szczerzej i bardziej bezbronnie niż zwykle.",
                       0.76f, Status::feel});
    }
    // DMN offline rumination from time apart.
    if (!self.ruminations.empty() && now <= self.ruminations.back().at + 36 * hour)
        out.push_back({"ruminate", self.ruminations.back().text, 0.66f, Status::feel});
    // Basal Ganglia learned conversational policy (RPE).
    {
        const auto& p = self.policy;
        struct Option { const char* id; float w; const wchar_t* pl; const wchar_t* en; };
        const std::array<Option, 5> opts{{
            {"teasing", p.teasing, L"Z ‹nim|nią› najlepiej działa zaczepne droczenie się i humor.", L"Playful teasing and banter work best with ‹him|her|them›."},
            {"deep", p.deep_questions, L"‹On|Ona› otwiera się najbardziej, gdy zadajesz ‹mu|jej› głębsze pytania o ‹niego|nią›.", L"‹He opens|She opens|They open› up most when you ask deeper questions about ‹him|her|them›."},
            {"games", p.game_talk, L"‹On|Ona› najbardziej lubi konkretne rozmowy o grach i mechanikach.", L"‹He enjoys|She enjoys|They enjoy› concrete talk about games and mechanics the most."},
            {"life", p.own_life, L"‹On|Ona› chętnie słucha opowieści z twojego własnego dnia i pasji.", L"‹He loves|She loves|They love› hearing stories from your own day and passions."},
            {"warmth", p.warmth, L"Na ‹niego|nią› najlepiej działa ciepło, uważność i łagodny ton.", L"Warmth, attentiveness and a gentle tone resonate best with ‹him|her|them›."},
        }};
        const auto best = std::max_element(opts.begin(), opts.end(), [](const Option& a, const Option& b) { return a.w < b.w; });
        if (best != opts.end() && best->w >= 0.58f)
            out.push_back({"policy", en ? best->en : best->pl, 0.36f + 0.3f * (best->w - 0.5f), Status::want});
    }
    // Concepts / vocabulary taught by the player.
    if (!self.learned_concepts.empty()) {
        const auto q_folded = game_index::fold(query);
        const LearnedConcept* pick = &self.learned_concepts.back();
        bool touched = false;
        for (const auto& c : self.learned_concepts) {
            const auto t = game_index::fold(c.term);
            if (!q_folded.empty() && t.size() >= 3 && q_folded.find(t) != std::wstring::npos) { pick = &c; touched = true; break; }
        }
        const std::wstring mastery_hint = pick->mastery < 0.5f
            ? (en ? L" (you only recently learned it from ‹him|her|them› — use it slightly tentatively or in your own cute way)"
                  : L" (dopiero niedawno się tego od ‹niego|niej› nauczyłaś — użyj tego trochę po swojemu albo z dumą, że pamiętasz)")
            : (en ? L" (you know it well now thanks to ‹him|her|them›)" : L" (używasz tego już swobodnie dzięki ‹niemu|niej›)");
        out.push_back({"concept", (en ? L"Concept ‹he|she|they› taught you: \"" : L"Pojęcie, którego ‹on cię nauczył|ona cię nauczyła›: „") + pick->term +
                                      (pick->meaning.empty() ? L"" : L" — " + pick->meaning) + (en ? L"\"" : L"”") + mastery_hint + L".",
                       touched ? 0.65f : 0.34f, Status::remember});
    }
    // Dyadic shared idioms / inside jokes.
    if (!self.shared_idioms.empty()) {
        const auto words = chats::searchWords(query);
        const Claim* pick = &self.shared_idioms.back();
        bool touched = false;
        for (auto it = self.shared_idioms.rbegin(); it != self.shared_idioms.rend(); ++it) {
            if (!words.empty() && chats::matches(words, it->text) > 0) { pick = &*it; touched = true; break; }
        }
        out.push_back({"idiom", (en ? L"Your shared inside joke / phrase with ‹him|her|them›: " : L"Wasz wspólny żart / powiedzonko tylko między wami: ") + pick->text + L".",
                       touched ? 0.64f : 0.32f, Status::remember});
    }
    // Topic rethinking / re-conceptualization after time.
    if (!self.rethought.empty() && now < self.rethought.back().at + 7 * day) {
        out.push_back({"rethink", (en ? L"A topic you recently re-thought from a new perspective: " : L"Temat, który ostatnio przemyślałaś na nowo: ") + self.rethought.back().text + L".",
                       0.38f, Status::remember});
    } else if (!self.markers.empty()) {
        for (const auto& m : self.markers) {
            if (m.at && now >= m.at + 3 * day && std::abs(m.valence) >= 0.35f && m.cortical_inhibition < 0.25f) {
                out.push_back({"rethink", (en ? L"Some time has passed since you formed a strong feeling about " : L"Minęło trochę czasu, odkąd wyrobiłaś sobie mocne zdanie o ") +
                                              m.subject + (en ? L" — you may rethink/re-conceptualize it now (\"przemyslenie\" in <ocena>)."
                                                              : L" — możesz teraz spojrzeć na ten temat świeżym okiem i przemyśleć go na nowo („przemyslenie” w <ocena>)."),
                               0.3f, Status::want});
                break;
            }
        }
    }
    // Multi-dimensional bond axes (respect / trust / security).
    if (context.respect < 42)
        out.push_back({"axes", en ? L"‹He has|She has|They have› been agreeing with you too blindly lately — in your character, tease or challenge ‹him|her|them› to show ‹his|her|their› own opinion."
                                  : L"‹On|Ona› ostatnio zbyt bezkrytycznie ci we wszystkim przytakuje — w swoim stylu zaczep ‹go|ją›, żeby ‹pokazał|pokazała› własne zdanie.",
                       0.52f, Status::feel});
    else if (context.respect >= 75)
        out.push_back({"axes", en ? L"You respect that ‹he has|she has|they have› ‹his|her|their› own spine and opinions." : L"Szanujesz to, że ‹on|ona› ma własne zdanie i charakter.", 0.3f, Status::feel});
    if (context.security < 36)
        out.push_back({"axes", context.stage >= 3
                                   ? (en ? L"You feel a little insecure about where you stand with ‹him|her|them› (easier to feel jealous or test if ‹he cares|she cares|they care›)."
                                         : L"Czujesz ostatnio lekką niepewność co do tego, jak bardzo ‹mu|jej› na tobie zależy (łatwiej o zazdrość lub sprawdzanie ‹go|jej›).")
                                   : (en ? L"You are not sure yet what to make of ‹him|her|them› — a little guarded, but without jealousy (it is far too early for that)."
                                         : L"Nie wiesz jeszcze, co o ‹nim|niej› myśleć — jesteś trochę ostrożna, ale bez zazdrości (na nią o wiele za wcześnie)."),
                       0.48f, Status::feel});

    // Item 3: Altman & Taylor's Social Penetration Theory — Disclosure Reciprocity (non-equal, persona-dependent asymmetry!).
    // Guarded/proud characters notice quickly when they share much more than the player; open/warm ones allow a much larger gap.
    {
        int her_disclosures = 0;
        for (const auto& e : self.episodes) if (e.hidden && e.told) ++her_disclosures;
        for (const auto& u : self.untruths) if (u.confessed) ++her_disclosures;
        her_disclosures += static_cast<int>(self.claims.size() / 4);
        int player_told = 0;
        for (const auto* n : memory.notesOf(context.look)) {
            const auto* k = Memory::knower(*n, context.look);
            if ((!k || !k->hearsay) && n->source != "observed" && n->source != "inferred") ++player_told;
        }
        const bool guarded_persona = context.persona == Persona::tsundere || context.persona == Persona::himedere ||
                                     context.persona == Persona::kuudere || context.persona == Persona::kanedere;
        const int tolerance = guarded_persona ? 2 : 4;
        if (her_disclosures >= tolerance && her_disclosures >= player_told + tolerance) {
            if (guarded_persona) {
                out.push_back({"reciprocity",
                               en ? L"Disclosure asymmetry (Altman & Taylor): you have already shared several private things about yourself while ‹he has|she has|they have› told you almost nothing personal — you feel a bit over-exposed; before opening up more, point it out in character and make ‹him|her|them› share something about ‹himself|herself|themselves›."
                                  : L"Asymetria zwierzeń (Altman i Taylor): opowiedziałaś ‹mu|jej› już sporo swoich prywatnych spraw, a ‹on|ona› wciąż prawie nic osobistego o sobie nie mówi — czujesz się trochę zbyt odsłonięta; zanim znów się otworzysz, wytknij ‹mu|jej› to w swoim stylu i wyciągnij coś o ‹nim|niej›.",
                               0.62f, Status::feel});
            } else {
                out.push_back({"reciprocity",
                               en ? L"Gentle reciprocity: you have shared a lot about your life while ‹he stays quiet about his|she stays quiet about hers|they stay quiet about theirs› — you don't mind sharing more, but warmly invite ‹him|her|them› to open up about ‹himself|herself|themselves› too."
                                  : L"Ciepła wzajemność: opowiedziałaś ‹mu|jej› już mnóstwo o swoim życiu, a ‹on wciąż jest skryty|ona wciąż jest skryta› — nie przeszkadza ci mówić więcej od siebie, ale ciepło zachęć ‹go|ją›, żeby też ‹opowiedział|opowiedziała› coś o sobie.",
                               0.46f, Status::want});
            }
        }
    }

    // Item 4: Eric Berne's "Why Don't You — Yes, But" transactional game detector.
    if (self.rejected_picks_in_row >= 2) {
        out.push_back({"yes_but",
                       en ? L"Berne's 'Why Don't You — Yes, But' game: ‹he has|she has|they have› turned down several of your game suggestions in a row with excuses — stop listing more titles! Notice in your character's style that ‹he isn't|she isn't|they aren't› really looking to play right now, ‹he's just tired, restless or wants|she's just tired, restless or wants|they're just tired, restless or want› to talk."
                          : L"Gra „Tak, ale…” (Berne): ‹on odrzucił|ona odrzuciła› już kolejne twoje propozycje gier wymówkami — przestań wymieniać następne tytuły! Zauważ w swoim stylu, że dziś wcale nie szuka gry do grania, tylko ‹jest zmęczony, marudny|jest zmęczona, marudna› albo po prostu chce pogadać.",
                       0.81f, Status::know});
    }

    // Item 5: Richard Schwartz's Internal Family Systems (IFS) — Exile sore spot vs. Firefighter defense / Self-unblending.
    if (auto ifs = ifsPartsCandidate(self, context, query)) out.push_back(std::move(*ifs));

    // Item 6: Jaak Panksepp's subcortical PLAY circuit (social banter momentum).
    if (self.play_momentum >= 0.45f) {
        out.push_back({"play",
                       en ? L"PLAY circuit active (shared banter momentum): the two of you are in a playful, laughing flow — keep your reply quick, witty and spontaneous, and if a shared joke naturally sparks, record it (\"nasz_zart\" in <ocena>)."
                          : L"Obwód PLAY (rozpęd wspólnej zabawy i droczenia się): ‹złapaliście|złapałyście› świetny, lekki rytm przekomarzania się — bądź błyskotliwa, spontaniczna, śmiej się swobodnie i jeśli pasuje, uknujcie wspólny żart (\"nasz_zart\" w <ocena>).",
                       0.45f + 0.35f * self.play_momentum, Status::feel});
    }

    // Item 7: Robert Sapolsky's multi-timescale physiology — lingering sleep debt & HPA-axis cortisol stress load.
    if (self.sleep_debt >= 0.35f) {
        out.push_back({"physiology",
                       en ? L"Sleep debt (Sapolsky): staying up late talking left you sleep-deprived today — you feel the lingering fatigue (craving coffee/tea, slightly slower or softer)."
                          : L"Dług snu (Sapolsky): zarywanie nocy dało ci się dziś we znaki — czujesz niewyspanie z poprzednich godzin (marzysz o kawie/herbacie, jesteś odrobinę bardziej senna).",
                       0.42f + 0.35f * self.sleep_debt, Status::feel});
    }
    if (self.stress_load >= 0.45f) {
        out.push_back({"physiology",
                       en ? L"HPA-axis stress load (cortisol): recent work/family pressure still lingers in your body, giving you a slightly shorter fuse or need for comfort."
                          : L"Ładunek stresu (kortyzol): ostatnie napięcia z pracy lub domu wciąż siedzą w tobie, przez co łatwiej cię zirytować albo bardziej potrzebujesz spokoju.",
                       0.4f + 0.3f * self.stress_load, Status::feel});
    }

    // Item 8: Dan McAdams & Endel Tulving — Self-Defining Key Memories & Autonoetic Mental Time Travel.
    {
        const auto words = chats::searchWords(query);
        std::wstring key_mem;
        bool touched = false;
        for (auto it = self.key_memories.rbegin(); it != self.key_memories.rend(); ++it) {
            if (!words.empty() && chats::matches(words, it->text) > 0) { key_mem = it->text; touched = true; break; }
            if (key_mem.empty()) key_mem = it->text;
        }
        if (!touched) {
            for (auto it = memory.summaries.rbegin(); it != memory.summaries.rend(); ++it) {
                if (!it->important || (!it->look.empty() && it->look != context.look)) continue;
                if (!words.empty() && chats::matches(words, it->text) > 0) { key_mem = it->text; touched = true; break; }
                if (key_mem.empty() && now >= it->at + 3 * day) key_mem = it->text;
            }
        }
        if (!key_mem.empty() && (touched || d.bond >= 0.55f || context.level >= 25)) {
            out.push_back({"time_travel",
                           en ? L"Key memory (Mental Time Travel — an important shared memory between you two: \"" + key_mem + L"\"): you may refer back to it with warmth or nostalgia."
                              : L"Ważne wspomnienie (podróż w czasie — ważna wspólna chwila między wami: „" + key_mem + L"”): możesz z czułością lub nostalgią do niej nawiązać.",
                           touched ? 0.68f : 0.34f, Status::remember});
        }
    }

    // Item 9: Jeff Hawkins' Reference Frame of the Player's World (short named entities: people, pets, places, projects).
    if (!self.player_world.empty()) {
        const auto q_words = chats::searchWords(query);
        const auto q_folded = game_index::fold(query);
        const WorldEntity* pick = &self.player_world.back();
        bool touched = false;
        for (auto it = self.player_world.rbegin(); it != self.player_world.rend(); ++it) {
            const auto head = game_index::fold(it->name.substr(0, it->name.find_first_of(L" (-—:")));
            if ((!q_folded.empty() && head.size() >= 3 && q_folded.find(head) != std::wstring::npos) ||
                (!q_words.empty() && chats::matches(q_words, it->name + L" " + it->relation) > 0)) {
                pick = &*it;
                touched = true;
                break;
            }
        }
        const std::wstring entry = pick->relation.empty() ? pick->name : pick->name + L" — " + pick->relation;
        out.push_back({"world", (en ? L"‹His|Her|Their› world: " : L"‹Jego|Jej› świat: ") + entry + L".", touched ? 0.72f : 0.31f, Status::know});
    }

    if (!self.player_read.empty() && now < self.player_read_at + 6 * hour)
        out.push_back({"player", (en ? L"How you read ‹him|her|them› lately: " : L"Jak ostatnio ‹go|ją› odbierałaś: ") + self.player_read + L".", 0.3f, Status::guess});
    // Where the player lives and who they are: worth knowing, never pressed.
    if (context.player_gender == Gender::unknown && self.last_talk == 0)
        out.push_back({"player", en ? L"You don't know yet whether the person you're chatting with is a guy or a girl — if it isn't clear from how they write, you may ask naturally while getting to know them (and set \"plec\":\"mezczyzna|kobieta\" in <ocena>)."
                                    : L"Nie wiesz jeszcze, czy osoba, z którą piszesz, to chłopak czy dziewczyna — jeśli nie wynika to z tego, jak pisze, możesz przy zapoznaniu naturalnie dopytać (i zapisać w <ocena> pole \"plec\":\"mezczyzna|kobieta\").",
                       0.22f + 0.25f * d.curiosity, Status::want});
    if (!memory.knowsPlace(context.look))
        out.push_back({"player", en ? L"You don't know where ‹he lives|she lives|they live›. If the talk allows, you may ask once — lightly; ‹he need|she need|they need› not answer."
                                    : L"Nie wiesz, gdzie ‹on|ona› mieszka. Jeśli rozmowa pozwoli, możesz kiedyś zapytać — lekko, raz; nie musi odpowiadać.",
                       0.12f + 0.25f * d.curiosity, Status::want});
    return out;
}

namespace {
// The workspace's choice: the most salient few (fewer when she is all worked up), two at most from
// each part of her, within a budget of words.
std::vector<const Candidate*> pick(std::vector<Candidate>& list, float arousal) {
    std::erase_if(list, [](const Candidate& c) { return c.text.empty() || c.salience <= 0.05f; });
    std::stable_sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return a.salience > b.salience; });
    const size_t room = arousal >= 0.8f ? 5 : 7;
    constexpr size_t budget = 1700;
    std::vector<const Candidate*> chosen;
    std::map<std::string, int> per_source;
    size_t chars = 0;
    for (const auto& c : list) {
        if (chosen.size() >= room) break;
        if (per_source[c.source] >= 2) continue;
        if (chars + c.text.size() > budget && !chosen.empty()) continue;
        chosen.push_back(&c);
        ++per_source[c.source];
        chars += c.text.size();
    }
    return chosen;
}
} // namespace

std::vector<std::string> workspaceSources(std::vector<Candidate> list, float arousal) {
    std::vector<std::string> out;
    for (const auto* c : pick(list, arousal)) out.push_back(c->source);
    return out;
}

std::wstring styleText(const Style& style, bool english) {
    if (style.samples < 3) return {};
    std::vector<std::wstring> parts;
    if (style.words < 7)
        parts.push_back(english ? L"‹he writes|she writes|they write› briefly (about " + std::to_wstring(static_cast<int>(std::lround(style.words))) + L" words) — keep yours short too, no essays"
                                : L"pisze krótko (ok. " + std::to_wstring(static_cast<int>(std::lround(style.words))) + L" słów) — odpowiadaj zwięźle, bez elaboratów");
    else if (style.words > 30)
        parts.push_back(english ? L"‹he writes|she writes|they write› long, open messages — you can afford a little more yourself" : L"pisze długo i wylewnie — możesz pozwolić sobie na trochę więcej");
    if (style.lower > 0.7f) parts.push_back(english ? L"all lowercase, casual — be looser in tone" : L"małymi literami, na luzie — możesz być swobodniejsza w tonie");
    if (style.plain > 0.7f) parts.push_back(english ? L"without Polish letters — that's ease, don't correct ‹him|her|them›" : L"bez polskich znaków — to luz, nie poprawiaj ‹go|jej›");
    if (style.laughs > 0.4f) parts.push_back(english ? L"laughs a lot (xd, haha) — a light, joking register" : L"często się śmieje (xd, haha) — lekki, żartobliwy rejestr");
    if (style.exclaim > 0.5f) parts.push_back(english ? L"lots of exclamation marks — high energy" : L"dużo wykrzykników — sporo energii");
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? L"; " : L"") + parts[i];
    return out;
}

std::wstring workspace(std::vector<Candidate> list, bool english, float arousal) {
    const auto chosen = pick(list, arousal);
    if (chosen.empty()) return {};
    static constexpr std::array<const wchar_t*, 7> pl{L"wiesz", L"słyszałaś", L"przypuszczasz", L"plotka", L"czujesz", L"chcesz", L"pamiętasz"};
    static constexpr std::array<const wchar_t*, 7> en{L"you know", L"you heard", L"you guess", L"gossip", L"you feel", L"you want", L"you remember"};
    std::wstring out = english ? L"\nOn your mind right now (only this — the rest of your memories stays in the background, your recall tool reaches it; "
                                 L"you need not bring all of it up, take what fits the talk naturally; never say #ids aloud):\n"
                               : L"\nTeraz na pierwszym planie w twojej głowie (tylko to — reszta wspomnień jest w tle i sięgniesz po nią narzędziem przypomnij; "
                                 L"nie musisz poruszać wszystkiego, weź to, co naturalnie pasuje do rozmowy; nigdy nie wypowiadaj #id):\n";
    for (const auto* c : chosen)
        out += L"- [" + std::wstring((english ? en : pl)[static_cast<size_t>(c->status)]) + L"] " + c->text + L"\n";
    return out;
}

std::string State::save() const {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    auto all = json::object();
    for (const auto& [look, self] : selves) {
        auto s = json::object();
        const auto affect = [](const Affect& a) {
            auto o = json::object();
            o.properties["v"] = json::number(std::round(a.valence * 1000) / 1000.0);
            o.properties["a"] = json::number(std::round(a.arousal * 1000) / 1000.0);
            o.properties["at"] = json::number(static_cast<double>(a.at));
            return o;
        };
        s.properties["affect"] = affect(self.affect);
        auto drives = json::object();
        drives.properties["bond"] = json::number(std::round(self.drives.bond * 1000) / 1000.0);
        drives.properties["energy"] = json::number(std::round(self.drives.energy * 1000) / 1000.0);
        drives.properties["curiosity"] = json::number(std::round(self.drives.curiosity * 1000) / 1000.0);
        drives.properties["pride"] = json::number(std::round(self.drives.pride * 1000) / 1000.0);
        drives.properties["autonomy"] = json::number(std::round(self.drives.autonomy * 1000) / 1000.0);
        drives.properties["at"] = json::number(static_cast<double>(self.drives.at));
        s.properties["drives"] = std::move(drives);
        auto expectations = json::array();
        for (const auto& e : self.expectations) {
            auto o = json::object();
            o.properties["id"] = json::string(e.id);
            o.properties["text"] = json::string(utf8(e.text));
            if (!e.game.empty()) o.properties["game"] = json::string(e.game);
            if (!e.title.empty()) o.properties["title"] = json::string(utf8(e.title));
            o.properties["p"] = json::number(std::round(e.p * 100) / 100.0);
            o.properties["made"] = json::number(static_cast<double>(e.made));
            o.properties["due"] = json::number(static_cast<double>(e.due));
            o.properties["outcome"] = json::number(e.outcome);
            o.properties["settled"] = json::number(static_cast<double>(e.settled));
            if (e.presence) o.properties["presence"] = json::boolean(true);
            if (e.suggestion) o.properties["suggestion"] = json::boolean(true);
            expectations.elements.push_back(std::move(o));
        }
        s.properties["expectations"] = std::move(expectations);
        auto goals = json::array();
        for (const auto& g : self.goals) {
            auto o = json::object();
            o.properties["id"] = json::string(g.id);
            o.properties["text"] = json::string(utf8(g.text));
            o.properties["kind"] = json::string(g.kind == Goal::Kind::promise ? "promise" : g.kind == Goal::Kind::intention ? "intention" : "own");
            o.properties["status"] = json::string(g.status == Goal::Status::done ? "done" : g.status == Goal::Status::dropped ? "dropped" : "open");
            o.properties["stage"] = json::number(g.stage);
            o.properties["made"] = json::number(static_cast<double>(g.made));
            o.properties["updated"] = json::number(static_cast<double>(g.updated));
            o.properties["due"] = json::number(static_cast<double>(g.due));
            if (!g.progress.empty()) o.properties["progress"] = json::string(utf8(g.progress));
            o.properties["source"] = json::number(g.source);
            o.properties["span"] = json::number(g.span);
            goals.elements.push_back(std::move(o));
        }
        s.properties["goals"] = std::move(goals);
        const auto stamped = [](const std::vector<Claim>& list) {
            auto array = json::array();
            for (const auto& c : list) {
                auto o = json::object();
                o.properties["at"] = json::number(static_cast<double>(c.at));
                o.properties["text"] = json::string(utf8(c.text));
                array.elements.push_back(std::move(o));
            }
            return array;
        };
        s.properties["claims"] = stamped(self.claims);
        s.properties["milestones"] = stamped(self.milestones);
        auto aspirations = json::array();
        for (const auto& a : self.aspirations) aspirations.elements.push_back(json::string(utf8(a)));
        s.properties["aspirations"] = std::move(aspirations);
        auto markers = json::array();
        for (const auto& m : self.markers) {
            if (std::abs(m.valence) < 0.01f) continue;
            auto o = json::object();
            o.properties["subject"] = json::string(utf8(m.subject));
            o.properties["v"] = json::number(std::round(m.valence * 100) / 100.0);
            o.properties["at"] = json::number(static_cast<double>(m.at));
            if (m.cortical_inhibition > 0.01f) o.properties["inh"] = json::number(std::round(m.cortical_inhibition * 100) / 100.0);
            markers.elements.push_back(std::move(o));
        }
        s.properties["markers"] = std::move(markers);
        {
            auto pol = json::object();
            pol.properties["teasing"] = json::number(std::round(self.policy.teasing * 1000) / 1000.0);
            pol.properties["deep"] = json::number(std::round(self.policy.deep_questions * 1000) / 1000.0);
            pol.properties["games"] = json::number(std::round(self.policy.game_talk * 1000) / 1000.0);
            pol.properties["life"] = json::number(std::round(self.policy.own_life * 1000) / 1000.0);
            pol.properties["warmth"] = json::number(std::round(self.policy.warmth * 1000) / 1000.0);
            pol.properties["base"] = json::number(std::round(self.policy.baseline_reward * 1000) / 1000.0);
            if (!self.policy.last_style.empty()) pol.properties["last"] = json::string(self.policy.last_style);
            s.properties["policy"] = std::move(pol);
        }
        if (!self.learned_concepts.empty()) {
            auto concepts = json::array();
            for (const auto& c : self.learned_concepts) {
                auto o = json::object();
                o.properties["term"] = json::string(utf8(c.term));
                if (!c.meaning.empty()) o.properties["meaning"] = json::string(utf8(c.meaning));
                o.properties["mastery"] = json::number(std::round(c.mastery * 100) / 100.0);
                o.properties["at"] = json::number(static_cast<double>(c.at));
                concepts.elements.push_back(std::move(o));
            }
            s.properties["learned_concepts"] = std::move(concepts);
        }
        if (!self.untruths.empty()) {
            auto untruths = json::array();
            for (const auto& u : self.untruths) {
                auto o = json::object();
                o.properties["id"] = json::string(u.id);
                o.properties["at"] = json::number(static_cast<double>(u.at));
                o.properties["said"] = json::string(utf8(u.said));
                o.properties["truth"] = json::string(utf8(u.truth));
                if (!u.reason.empty()) o.properties["reason"] = json::string(u.reason);
                if (!u.feeling.empty()) o.properties["feeling"] = json::string(u.feeling);
                o.properties["trust_at"] = json::number(u.trust_at);
                if (u.confessed) o.properties["confessed"] = json::boolean(true);
                if (u.peeked) o.properties["peeked"] = json::boolean(true);
                if (u.caught) o.properties["caught"] = json::boolean(true);
                untruths.elements.push_back(std::move(o));
            }
            s.properties["untruths"] = std::move(untruths);
        }
        if (!self.curious.empty()) {
            auto curious = json::array();
            for (const auto& c : self.curious) {
                auto o = json::object();
                o.properties["id"] = json::string(c.id);
                if (!c.about.empty()) o.properties["about"] = json::string(utf8(c.about));
                if (!c.why.empty()) o.properties["why"] = json::string(utf8(c.why));
                if (c.at) o.properties["at"] = json::number(static_cast<double>(c.at));
                if (c.later_at) o.properties["later_at"] = json::number(static_cast<double>(c.later_at));
                o.properties["outcome"] = json::number(c.outcome);
                if (c.settled) o.properties["settled"] = json::number(static_cast<double>(c.settled));
                if (c.returned) o.properties["returned"] = json::boolean(true);
                o.properties["move"] = json::string(c.move == CuriousMove::clarify ? "clarify" : c.move == CuriousMove::reflect ? "reflect" : "lead");
                curious.elements.push_back(std::move(o));
            }
            s.properties["curious"] = std::move(curious);
        }
        if (!self.follow_ups.empty()) {
            auto list = json::array();
            for (const auto& f : self.follow_ups) {
                auto o = json::object();
                o.properties["id"] = json::string(f.id);
                o.properties["what"] = json::string(utf8(f.what));
                if (!f.when.empty()) o.properties["when"] = json::string(utf8(f.when));
                o.properties["made"] = json::number(static_cast<double>(f.made));
                o.properties["due"] = json::number(static_cast<double>(f.due));
                o.properties["state"] = json::number(f.state);
                if (f.wished) o.properties["wished"] = json::boolean(true);
                if (f.settled) o.properties["settled"] = json::number(static_cast<double>(f.settled));
                list.elements.push_back(std::move(o));
            }
            s.properties["follow_ups"] = std::move(list);
        }
        {
            auto bids = json::object();
            bids.properties["toward"] = json::number(std::round(self.bids.toward * 100) / 100.0);
            bids.properties["away"] = json::number(std::round(self.bids.away * 100) / 100.0);
            bids.properties["against"] = json::number(std::round(self.bids.against * 100) / 100.0);
            bids.properties["his"] = json::number(self.bids.his);
            bids.properties["at"] = json::number(static_cast<double>(self.bids.at));
            s.properties["bids"] = std::move(bids);
        }
        if (self.his_style.samples) {
            auto style = json::object();
            const auto put = [&](const char* name, float value) { style.properties[name] = json::number(std::round(value * 100) / 100.0); };
            put("words", self.his_style.words); put("exclaim", self.his_style.exclaim); put("lower", self.his_style.lower);
            put("plain", self.his_style.plain); put("laughs", self.his_style.laughs);
            style.properties["samples"] = json::number(self.his_style.samples);
            s.properties["his_style"] = std::move(style);
        }
        if (!self.threads.empty()) {
            auto list = json::array();
            for (const auto& t : self.threads) {
                auto o = json::object();
                o.properties["at"] = json::number(static_cast<double>(t.at));
                o.properties["text"] = json::string(utf8(t.text));
                if (t.used) o.properties["used"] = json::boolean(true);
                list.elements.push_back(std::move(o));
            }
            s.properties["threads"] = std::move(list);
        }
        s.properties["interest"] = json::number(std::round(self.interest * 100) / 100.0);
        if (self.interest_at) s.properties["interest_at"] = json::number(static_cast<double>(self.interest_at));
        if (self.opened_at) s.properties["opened_at"] = json::number(static_cast<double>(self.opened_at));
        if (self.his_open_at) s.properties["his_open_at"] = json::number(static_cast<double>(self.his_open_at));
        if (self.bonded_at) s.properties["bonded_at"] = json::number(static_cast<double>(self.bonded_at));
        if (!self.shared_idioms.empty()) s.properties["shared_idioms"] = stamped(self.shared_idioms);
        if (!self.key_memories.empty()) s.properties["key_memories"] = stamped(self.key_memories);
        if (!self.player_world.empty()) {
            auto world = json::array();
            for (const auto& w : self.player_world) {
                auto o = json::object();
                o.properties["name"] = json::string(utf8(w.name));
                if (!w.relation.empty()) o.properties["rel"] = json::string(utf8(w.relation));
                o.properties["at"] = json::number(static_cast<double>(w.at));
                world.elements.push_back(std::move(o));
            }
            s.properties["player_world"] = std::move(world);
        }
        if (self.play_momentum > 0.01f) s.properties["play"] = json::number(std::round(self.play_momentum * 100) / 100.0);
        if (self.sleep_debt > 0.01f) s.properties["sleep_debt"] = json::number(std::round(self.sleep_debt * 100) / 100.0);
        if (self.stress_load > 0.01f) s.properties["stress_load"] = json::number(std::round(self.stress_load * 100) / 100.0);
        if (self.rejected_picks_in_row > 0) s.properties["rejected_picks"] = json::number(self.rejected_picks_in_row);
        if (!self.ruminations.empty()) s.properties["ruminations"] = stamped(self.ruminations);
        if (!self.rethought.empty()) s.properties["rethought"] = stamped(self.rethought);
        if (!self.counterfactual.empty()) {
            s.properties["counterfactual"] = json::string(utf8(self.counterfactual));
            s.properties["counterfactual_at"] = json::number(static_cast<double>(self.counterfactual_at));
        }
        if (self.harsh_at) {
            s.properties["harsh_at"] = json::number(static_cast<double>(self.harsh_at));
            if (!self.harsh_reason.empty()) s.properties["harsh_reason"] = json::string(utf8(self.harsh_reason));
        }
        if (self.last_reply_vulnerable) s.properties["last_reply_vulnerable"] = json::boolean(true);
        if (self.last_rumination) s.properties["last_rumination"] = json::number(static_cast<double>(self.last_rumination));
        if (self.last_initiative) s.properties["last_initiative"] = json::number(static_cast<double>(self.last_initiative));
        auto episodes = json::array();
        for (const auto& e : self.episodes) {
            auto o = json::object();
            o.properties["id"] = json::string(e.id);
            o.properties["at"] = json::number(static_cast<double>(e.at));
            o.properties["what"] = json::string(utf8(e.what));
            if (!e.with.empty()) o.properties["with"] = json::string(utf8(e.with));
            if (!e.where.empty()) o.properties["where"] = json::string(utf8(e.where));
            if (!e.partner.empty()) o.properties["partner"] = json::string(e.partner);
            o.properties["state"] = json::string(e.state == Episode::State::planned ? "planned" : e.state == Episode::State::fell_through ? "fell_through" : "happened");
            if (e.hidden) o.properties["private"] = json::boolean(true);
            if (e.date) o.properties["date"] = json::boolean(true);
            if (e.told) o.properties["told"] = json::boolean(true);
            if (e.peeked) o.properties["peeked"] = json::boolean(true);
            o.properties["made"] = json::number(static_cast<double>(e.made));
            episodes.elements.push_back(std::move(o));
        }
        s.properties["episodes"] = std::move(episodes);
        if (self.lived_until) s.properties["lived_until"] = json::number(static_cast<double>(self.lived_until));
        if (!self.felt.empty()) { s.properties["felt"] = json::string(utf8(self.felt)); s.properties["felt_at"] = json::number(static_cast<double>(self.felt_at)); }
        auto feeling_records = json::array();
        for (const auto& moment : self.feelings) {
            auto entry = json::object();
            entry.properties["key"] = json::string(feelingInfo(moment.kind).key);
            entry.properties["at"] = json::number(static_cast<double>(moment.at));
            entry.properties["reason"] = json::string(utf8(moment.reason));
            entry.properties["inferred"] = json::boolean(moment.inferred);
            if (moment.intensity) entry.properties["intensity"] = json::number(*moment.intensity);
            feeling_records.elements.push_back(std::move(entry));
        }
        s.properties["feelings"] = std::move(feeling_records);
        if (!self.player_read.empty()) {
            s.properties["player_read"] = json::string(utf8(self.player_read));
            s.properties["player_read_at"] = json::number(static_cast<double>(self.player_read_at));
        }
        s.properties["last_talk"] = json::number(static_cast<double>(self.last_talk));
        s.properties["goal_rest"] = json::number(static_cast<double>(self.goal_rest));
        auto used = json::array();
        for (const int index : self.seeds_used) used.elements.push_back(json::number(index));
        s.properties["seeds_used"] = std::move(used);
        s.properties["next_id"] = json::number(self.next_id);
        all.properties[look] = std::move(s);
    }
    root.properties["selves"] = std::move(all);
    return json::serialize(root);
}

std::optional<State> State::load(std::string_view text) {
    const auto root = json::parse(text, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object || !root->get("version") || root->get("version")->intOr() != 1) return std::nullopt;
    State out;
    const auto* all = root->get("selves");
    if (!all || all->type != json::Value::Type::object) return out;
    const auto number = [](const json::Value* parent, const char* name, double fallback = 0) {
        const auto* v = parent ? parent->get(name) : nullptr;
        return v && v->type == json::Value::Type::number ? v->numeric : fallback;
    };
    const auto unix = [&](const json::Value* parent, const char* name) {
        const double v = number(parent, name);
        return v > 0 ? static_cast<uint64_t>(v) : uint64_t{0};
    };
    const auto text16 = [](const json::Value* parent, const char* name) {
        const auto* v = parent ? parent->get(name) : nullptr;
        return v && v->type == json::Value::Type::string ? wide(v->text) : std::wstring{};
    };
    const auto text8 = [](const json::Value* parent, const char* name) {
        const auto* v = parent ? parent->get(name) : nullptr;
        return v && v->type == json::Value::Type::string && v->text.size() <= 64 ? v->text : std::string{};
    };
    for (const auto& [look, s] : all->properties) {
        if (look.empty() || look.size() > 32 || s.type != json::Value::Type::object) continue;
        Self self;
        if (const auto* a = s.get("affect")) self.affect = {std::clamp(static_cast<float>(number(a, "v")), -1.0f, 1.0f), clamp01(static_cast<float>(number(a, "a", 0.3))), unix(a, "at")};
        if (const auto* d = s.get("drives")) {
            self.drives.bond = clamp01(static_cast<float>(number(d, "bond", 0.3)));
            self.drives.energy = clamp01(static_cast<float>(number(d, "energy", 0.9)));
            self.drives.curiosity = clamp01(static_cast<float>(number(d, "curiosity", 0.4)));
            self.drives.pride = clamp01(static_cast<float>(number(d, "pride", 0.5)));
            self.drives.autonomy = clamp01(static_cast<float>(number(d, "autonomy", 0.3)));
            self.drives.at = unix(d, "at");
        }
        if (const auto* list = s.get("expectations"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Expectation e;
                e.id = text8(&o, "id");
                e.text = text16(&o, "text");
                if (e.id.empty() || e.text.empty()) continue;
                e.game = text8(&o, "game");
                e.title = text16(&o, "title");
                e.p = clamp01(static_cast<float>(number(&o, "p", 0.5)));
                e.made = unix(&o, "made");
                e.due = unix(&o, "due");
                e.outcome = std::clamp(static_cast<int>(number(&o, "outcome", -1)), -1, 2);
                e.settled = unix(&o, "settled");
                if (const auto* p = o.get("presence"); p && p->type == json::Value::Type::boolean) e.presence = p->flag;
                if (const auto* p = o.get("suggestion"); p && p->type == json::Value::Type::boolean) e.suggestion = p->flag;
                if (self.expectations.size() < expectation_limit) self.expectations.push_back(std::move(e));
            }
        if (const auto* list = s.get("goals"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Goal g;
                g.id = text8(&o, "id");
                g.text = text16(&o, "text");
                if (g.id.empty() || g.text.empty()) continue;
                const auto kind = text8(&o, "kind"), status = text8(&o, "status");
                g.kind = kind == "promise" ? Goal::Kind::promise : kind == "intention" ? Goal::Kind::intention : Goal::Kind::own;
                g.status = status == "done" ? Goal::Status::done : status == "dropped" ? Goal::Status::dropped : Goal::Status::open;
                g.stage = std::clamp(static_cast<int>(number(&o, "stage")), 0, 2);
                g.made = unix(&o, "made");
                g.updated = unix(&o, "updated");
                g.due = unix(&o, "due");
                g.progress = text16(&o, "progress");
                g.source = static_cast<int>(number(&o, "source", -1));
                g.span = std::clamp(static_cast<int>(number(&o, "span")), 0, 60);
                if (self.goals.size() < goal_limit) self.goals.push_back(std::move(g));
            }
        if (const auto* list = s.get("claims"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.claims.size() < claim_limit) self.claims.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("milestones"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.milestones.size() < 60) self.milestones.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("aspirations"); list && list->type == json::Value::Type::array)
            for (const auto& v : list->elements)
                if (v.type == json::Value::Type::string && !v.text.empty() && self.aspirations.size() < 5) self.aspirations.push_back(wide(v.text));
        if (const auto* list = s.get("markers"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "subject"); !t.empty() && self.markers.size() < marker_limit)
                    self.markers.push_back({std::move(t), std::clamp(static_cast<float>(number(&o, "v")), -1.0f, 1.0f), unix(&o, "at"),
                                            clamp01(static_cast<float>(number(&o, "inh", 0)))});
        if (const auto* pol = s.get("policy"); pol && pol->type == json::Value::Type::object) {
            self.policy.teasing = clamp01(static_cast<float>(number(pol, "teasing", 0.5)));
            self.policy.deep_questions = clamp01(static_cast<float>(number(pol, "deep", 0.5)));
            self.policy.game_talk = clamp01(static_cast<float>(number(pol, "games", 0.5)));
            self.policy.own_life = clamp01(static_cast<float>(number(pol, "life", 0.5)));
            self.policy.warmth = clamp01(static_cast<float>(number(pol, "warmth", 0.5)));
            self.policy.baseline_reward = clamp01(static_cast<float>(number(pol, "base", 0.5)));
            self.policy.last_style = text8(pol, "last");
        }
        if (const auto* list = s.get("learned_concepts"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "term"); !t.empty() && self.learned_concepts.size() < max_learned_concepts)
                    self.learned_concepts.push_back({std::move(t), text16(&o, "meaning"), clamp01(static_cast<float>(number(&o, "mastery", 0.35))), unix(&o, "at")});
        if (const auto* list = s.get("untruths"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Untruth u;
                u.id = text8(&o, "id");
                u.at = unix(&o, "at");
                u.said = text16(&o, "said");
                u.truth = text16(&o, "truth");
                if (u.id.empty() || u.said.empty() || u.truth.empty()) continue;
                u.reason = text8(&o, "reason");
                u.feeling = text8(&o, "feeling");
                u.trust_at = std::clamp(static_cast<int>(number(&o, "trust_at", 50)), 0, 100);
                if (const auto* c = o.get("confessed"); c && c->type == json::Value::Type::boolean) u.confessed = c->flag;
                if (const auto* p = o.get("peeked"); p && p->type == json::Value::Type::boolean) u.peeked = p->flag;
                if (const auto* c = o.get("caught"); c && c->type == json::Value::Type::boolean) u.caught = c->flag;
                if (self.untruths.size() < 24) self.untruths.push_back(std::move(u));
            }
        if (const auto* list = s.get("curious"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                CuriousAct c;
                c.id = text8(&o, "id");
                c.at = unix(&o, "at");
                c.later_at = unix(&o, "later_at");
                if (c.id.empty() || (!c.at && !c.later_at)) continue;
                const auto move = text8(&o, "move");
                c.move = move == "clarify" ? CuriousMove::clarify : move == "reflect" ? CuriousMove::reflect : CuriousMove::lead;
                c.about = text16(&o, "about");
                c.why = text16(&o, "why");
                c.outcome = std::clamp(static_cast<int>(number(&o, "outcome", -1)), -1, 2);
                c.settled = unix(&o, "settled");
                if (const auto* r = o.get("returned"); r && r->type == json::Value::Type::boolean) c.returned = r->flag;
                if (self.curious.size() < curious_limit) self.curious.push_back(std::move(c));
            }
        if (const auto* list = s.get("follow_ups"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                FollowUp f;
                f.id = text8(&o, "id");
                f.what = text16(&o, "what");
                if (f.id.empty() || f.what.empty()) continue;
                f.when = text16(&o, "when");
                f.made = unix(&o, "made");
                f.due = unix(&o, "due");
                f.state = std::clamp(static_cast<int>(number(&o, "state")), 0, 3);
                if (const auto* w = o.get("wished"); w && w->type == json::Value::Type::boolean) f.wished = w->flag;
                f.settled = unix(&o, "settled");
                if (self.follow_ups.size() < 12) self.follow_ups.push_back(std::move(f));
            }
        if (const auto* b = s.get("bids"); b && b->type == json::Value::Type::object) {
            self.bids.toward = std::max(0.0f, static_cast<float>(number(b, "toward")));
            self.bids.away = std::max(0.0f, static_cast<float>(number(b, "away")));
            self.bids.against = std::max(0.0f, static_cast<float>(number(b, "against")));
            self.bids.his = std::max(0, static_cast<int>(number(b, "his")));
            self.bids.at = unix(b, "at");
        }
        if (const auto* st = s.get("his_style"); st && st->type == json::Value::Type::object) {
            self.his_style.words = std::max(0.0f, static_cast<float>(number(st, "words")));
            self.his_style.exclaim = clamp01(static_cast<float>(number(st, "exclaim")));
            self.his_style.lower = clamp01(static_cast<float>(number(st, "lower")));
            self.his_style.plain = clamp01(static_cast<float>(number(st, "plain")));
            self.his_style.laughs = clamp01(static_cast<float>(number(st, "laughs")));
            self.his_style.samples = std::clamp(static_cast<int>(number(st, "samples")), 0, 1000);
        }
        if (const auto* list = s.get("threads"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.threads.size() < 5) {
                    const auto* u = o.get("used");
                    self.threads.push_back({unix(&o, "at"), std::move(t), u && u->type == json::Value::Type::boolean && u->flag});
                }
        self.interest = clamp01(static_cast<float>(number(&s, "interest", 0.5)));
        self.interest_at = unix(&s, "interest_at");
        self.opened_at = unix(&s, "opened_at");
        self.his_open_at = unix(&s, "his_open_at");
        self.bonded_at = unix(&s, "bonded_at");
        if (const auto* list = s.get("shared_idioms"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.shared_idioms.size() < max_shared_idioms) self.shared_idioms.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("key_memories"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.key_memories.size() < max_key_memories) self.key_memories.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("player_world"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto n = text16(&o, "name"); !n.empty() && self.player_world.size() < max_player_world)
                    self.player_world.push_back({std::move(n), text16(&o, "rel"), unix(&o, "at")});
        self.play_momentum = clamp01(static_cast<float>(number(&s, "play", 0.0)));
        self.sleep_debt = clamp01(static_cast<float>(number(&s, "sleep_debt", 0.0)));
        self.stress_load = clamp01(static_cast<float>(number(&s, "stress_load", 0.0)));
        self.rejected_picks_in_row = std::clamp(static_cast<int>(number(&s, "rejected_picks", 0)), 0, 20);
        if (const auto* list = s.get("ruminations"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.ruminations.size() < 8) self.ruminations.push_back({unix(&o, "at"), std::move(t)});
        if (const auto* list = s.get("rethought"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements)
                if (auto t = text16(&o, "text"); !t.empty() && self.rethought.size() < 20) self.rethought.push_back({unix(&o, "at"), std::move(t)});
        self.counterfactual = text16(&s, "counterfactual");
        self.counterfactual_at = unix(&s, "counterfactual_at");
        self.harsh_at = unix(&s, "harsh_at");
        self.harsh_reason = text16(&s, "harsh_reason");
        if (const auto* v = s.get("last_reply_vulnerable"); v && v->type == json::Value::Type::boolean) self.last_reply_vulnerable = v->flag;
        self.last_rumination = unix(&s, "last_rumination");
        self.last_initiative = unix(&s, "last_initiative");
        if (const auto* list = s.get("episodes"); list && list->type == json::Value::Type::array)
            for (const auto& o : list->elements) {
                Episode e;
                e.id = text8(&o, "id");
                e.what = text16(&o, "what");
                e.at = unix(&o, "at");
                if (e.id.empty() || e.what.empty() || !e.at) continue;
                e.with = text16(&o, "with");
                e.where = text16(&o, "where");
                e.partner = text8(&o, "partner");
                const auto state = text8(&o, "state");
                e.state = state == "planned" ? Episode::State::planned : state == "fell_through" ? Episode::State::fell_through : Episode::State::happened;
                const auto flag = [&](const char* name) { const auto* v = o.get(name); return v && v->type == json::Value::Type::boolean && v->flag; };
                e.hidden = flag("private");
                e.date = flag("date");
                e.told = flag("told");
                e.peeked = flag("peeked");
                e.made = unix(&o, "made");
                if (self.episodes.size() < episode_limit) self.episodes.push_back(std::move(e));
            }
        keepEpisodes(self); // Older saves may hold one plan written twice.
        self.lived_until = unix(&s, "lived_until");
        self.felt = text16(&s, "felt");
        self.felt_at = unix(&s, "felt_at");
        if (const auto* list = s.get("feelings"); list && list->type == json::Value::Type::array) {
            for (const auto& entry : list->elements) {
                const auto kind = feelingFromKey(text8(&entry, "key"));
                if (!kind) continue;
                Self::FeelingMoment moment{*kind, {}, clean(text16(&entry, "reason"), 200), unix(&entry, "at")};
                if (const auto* intensity = entry.get("intensity"); intensity && intensity->type == json::Value::Type::number && std::isfinite(intensity->numeric))
                    moment.intensity = std::clamp(static_cast<float>(intensity->numeric), 0.0f, 1.0f);
                if (const auto* inferred = entry.get("inferred")) moment.inferred = inferred->type == json::Value::Type::boolean && inferred->flag;
                self.feelings.push_back(std::move(moment));
                if (self.feelings.size() > 80) self.feelings.erase(self.feelings.begin());
            }
        }
        self.player_read = text16(&s, "player_read");
        self.player_read_at = unix(&s, "player_read_at");
        self.last_talk = unix(&s, "last_talk");
        self.goal_rest = unix(&s, "goal_rest");
        if (const auto* list = s.get("seeds_used"); list && list->type == json::Value::Type::array)
            for (const auto& v : list->elements) if (v.type == json::Value::Type::number && self.seeds_used.size() < 64) self.seeds_used.push_back(static_cast<int>(v.numeric));
        self.next_id = std::max(1, static_cast<int>(number(&s, "next_id", 1)));
        out.selves.emplace(look, std::move(self));
    }
    return out;
}

namespace {
std::filesystem::path file(const std::filesystem::path& config) { return config / L"companion_mind.dat"; }
}

State loadState(const std::filesystem::path& config) {
    const auto bytes = protected_file::read(file(config), 4 * 1024 * 1024);
    if (!bytes) return {};
    return State::load(*bytes).value_or(State{});
}

bool saveState(const std::filesystem::path& config, const State& state) {
    if (state.empty()) return clearState(config);
    std::error_code ec;
    std::filesystem::create_directories(config, ec);
    return protected_file::write(file(config), state.save(), L"Launchme companion mind");
}

bool clearState(const std::filesystem::path& config) {
    std::error_code ec;
    std::filesystem::remove(file(config), ec);
    return !ec;
}
} // namespace companion::mind

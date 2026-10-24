#include "companion_events.hpp"
#include "companion_affinity.hpp"

#include <algorithm>
#include <array>
#include <random>

namespace companion {
namespace {
std::wstring list(const std::vector<std::wstring>& titles, bool linked) {
    std::wstring out;
    for (size_t i = 0; i < titles.size(); ++i) out += (i ? L", " : L"") + (linked ? L"[[" + titles[i] + L"]]" : titles[i]);
    return out;
}
std::wstring two(int value) { return (value < 10 ? L"0" : L"") + std::to_wstring(value); }

// What happened, as facts for the model.
std::wstring facts(const Event& event, bool english) {
    const auto title = event.titles.empty() ? std::wstring{} : event.titles.front();
    const auto n = std::to_wstring(event.count);
    switch (event.kind) {
    case EventKind::launch:
        return english ? L"‹He is|She is|They are› launching [[" + title + L"]] right now. Wish ‹him|her|them› luck or quip about the game."
                       : L"‹On|Ona› właśnie uruchamia [[" + title + L"]]. Życz powodzenia albo rzuć komentarz o tej grze.";
    case EventKind::session: {
        std::wstring text = english ? L"‹He is|She is|They are› back from [[" + title + L"]] after " + std::to_wstring(event.minutes) + L" min of play."
                                    : L"‹Wrócił|Wróciła› z [[" + title + L"]] po " + std::to_wstring(event.minutes) + L" min grania.";
        const auto progress = std::to_wstring(event.unlocked) + L"/" + std::to_wstring(event.total);
        if (event.achievements) text += english ? L" New achievements this time: " + std::to_wstring(event.achievements) + L" (now " + progress + L")."
                                                : L" Nowe osiągnięcia tym razem: " + std::to_wstring(event.achievements) + L" (teraz " + progress + L").";
        if (event.completed) text += english ? L" That was the last one: every achievement unlocked, the game is 100% complete!"
                                             : L" To było ostatnie: wszystkie osiągnięcia zdobyte, gra ukończona na 100%!";
        if (event.milestone) text += english ? L" ‹His|Her|Their› total play time in it just passed " + std::to_wstring(event.milestone) + L" hours."
                                             : L" Łączny czas w tej grze właśnie przekroczył " + std::to_wstring(event.milestone) + L" godz.";
        if (event.story_left && !event.completed) {
            const auto left = event.story_left < 90 ? std::to_wstring(event.story_left) + L" min"
                                                    : std::to_wstring((event.story_left + 30) / 60) + (english ? L" h" : L" godz.");
            text += english ? L" About " + left + L" of the main story left (HowLongToBeat)." : L" Do końca głównej fabuły zostało około " + left + L" (HowLongToBeat).";
        }
        if (event.minutes < 3) text += english ? L" A very short session." : L" Bardzo krótka sesja.";
        return text + (english ? L" Comment on it." : L" Skomentuj to.");
    }
    case EventKind::added:
        if (event.count <= 1 && event.titles.size() == 1)
            return english ? L"A new game joined ‹his|her|their› library: [[" + title + L"]]." : L"Do ‹jego|jej› biblioteki doszła nowa gra: [[" + title + L"]].";
        if (event.titles.empty()) return english ? L"New games joined ‹his|her|their› library: " + n + L"." : L"Do ‹jego|jej› biblioteki doszły nowe gry: " + n + L".";
        return english ? L"New games joined ‹his|her|their› library (" + n + L" in total), among them: " + list(event.titles, true) + L"."
                       : L"Do ‹jego|jej› biblioteki doszły nowe gry (razem " + n + L"), m.in.: " + list(event.titles, true) + L".";
    case EventKind::installed:
        return english ? L"‹He|She|They› just installed: " + list(event.titles, true) + L"." : L"Właśnie ‹zainstalował|zainstalowała›: " + list(event.titles, true) + L".";
    case EventKind::uninstalled:
        return english ? L"‹He|She|They› just uninstalled: " + list(event.titles, true) + L"." : L"Właśnie ‹odinstalował|odinstalowała›: " + list(event.titles, true) + L".";
    case EventKind::premieres:
        return english ? L"Releasing today (not in ‹his|her|their› library): " + list(event.titles, true) + L". Tell ‹him|her|them› about it."
                       : L"Dziś premiera (tych gier nie ma w ‹jego|jej› bibliotece): " + list(event.titles, true) + L". Powiedz ‹mu|jej› o tym.";
    case EventKind::late_night: {
        const auto clock = std::to_wstring(std::max(event.hour, 0)) + L":" + two(event.minute);
        return english ? L"It is already " + clock + L" at night and ‹he is|she is|they are› still in the launcher. Suggest sleep in your style."
                       : L"Jest już " + clock + L" w nocy, a ‹on|ona› wciąż siedzi w launcherze. Zasugeruj sen w swoim stylu.";
    }
    case EventKind::level_up: {
        std::wstring text = english ? L"Your relationship with ‹him|her|them› just reached level " + n + L" of " + std::to_wstring(affinity::max_level) + L". Show a subtle warmth in your character's style, without mentioning levels or numbers out loud."
                                    : L"Twoja relacja z ‹nim|nią› właśnie wskoczyła na poziom " + n + L" na " + std::to_wstring(affinity::max_level) + L". Okaż to subtelnie cieplejszym tonem w swoim stylu, nigdy nie mówiąc na głos o poziomach ani liczbach.";
        if (!event.titles.empty()) text += english ? L" You also feel like trying on your outfit: " + list(event.titles, false) + L"."
                                                   : L" Masz też ochotę założyć swój strój: " + list(event.titles, false) + L".";
        return text;
    }
    }
    return {};
}

// Importance (higher first) and how long a remark stays worth saying.
struct Pace { int priority; uint64_t lifetime_ms; };
Pace pace(EventKind kind) {
    constexpr uint64_t minute = 60'000;
    switch (kind) {
    case EventKind::launch: return {6, minute};
    case EventKind::session: return {5, 30 * minute};
    case EventKind::installed: case EventKind::uninstalled: return {4, 30 * minute};
    case EventKind::added: return {3, 120 * minute};
    case EventKind::premieres: return {2, 360 * minute};
    case EventKind::late_night: return {1, 15 * minute};
    case EventKind::level_up: return {5, 60 * minute};
    }
    return {0, minute};
}
constexpr size_t max_titles = 5;
void merge(Event& into, const Event& from) {
    for (const auto& title : from.titles)
        if (into.titles.size() < max_titles && std::find(into.titles.begin(), into.titles.end(), title) == into.titles.end()) into.titles.push_back(title);
    into.count += from.count;
}
} // namespace

std::wstring eventKickoff(const Event& event, bool english) {
    return english ? L"(A quick remark for the home screen: 1-2 sentences, 22 words at most, in your style. " + facts(event, english) + L")"
                   : L"(Krótka uwaga na ekran główny: 1–2 zdania, maks. 22 słowa, w twoim stylu. " + facts(event, english) + L")";
}

uint32_t milestone(uint32_t before, uint32_t after) {
    static constexpr std::array<uint32_t, 9> hours{1, 5, 10, 25, 50, 100, 250, 500, 1000};
    uint32_t passed = 0;
    for (const auto mark : hours) if (before < mark * 60 && after >= mark * 60) passed = mark;
    return passed;
}

std::optional<Event> sessionEvent(std::wstring title, uint64_t played_ms, const Progress& before, const Progress& after) {
    if (played_ms < 60'000 || title.empty()) return std::nullopt;
    Event event;
    event.kind = EventKind::session;
    event.titles = {std::move(title)};
    event.minutes = static_cast<uint32_t>(std::min<uint64_t>(played_ms / 60'000, 100'000));
    if (after.total && after.total == before.total && after.unlocked > before.unlocked) {
        event.achievements = after.unlocked - before.unlocked;
        event.unlocked = after.unlocked; event.total = after.total;
        event.completed = after.unlocked == after.total;
    }
    if (before.minutes && after.minutes) event.milestone = milestone(*before.minutes, *after.minutes);
    return event;
}

std::vector<std::wstring> premieresToday(const std::vector<glance::Item>& calendar, int today, size_t limit) {
    std::vector<std::wstring> titles;
    for (const auto& item : calendar) {
        const auto& date = item.release;
        if (titles.size() >= limit) break;
        if (date.precision != glance::ReleasePrecision::day || date.year * 10000 + date.month * 100 + date.day != today || item.title.empty()) continue;
        if (std::find(titles.begin(), titles.end(), item.title) == titles.end()) titles.push_back(item.title);
    }
    return titles;
}

std::vector<Event> LibraryWatch::update(const std::vector<RosterEntry>& roster, const std::vector<std::string>& added_keys) {
    Event installed{EventKind::installed}, uninstalled{EventKind::uninstalled}, added{EventKind::added};
    const auto note = [](Event& event, const std::wstring& title) {
        ++event.count;
        if (!title.empty() && event.titles.size() < max_titles) event.titles.push_back(title);
    };
    std::unordered_map<std::string, const RosterEntry*> current;
    for (const auto& entry : roster) current.emplace(entry.key, &entry);
    // New to the library (and not merely newly shown, like a game installed from the backlog).
    for (const auto& key : added_keys) {
        if (installed_.contains(key)) continue;
        const auto found = current.find(key);
        note(added, found == current.end() ? std::wstring{} : found->second->title);
    }
    if (ready_)
        for (const auto& entry : roster) {
            const auto known = installed_.find(entry.key);
            if (known == installed_.end() || known->second == entry.installed || entry.title.empty()) continue;
            note(entry.installed ? installed : uninstalled, entry.title);
        }
    for (const auto& entry : roster) installed_[entry.key] = entry.installed;
    ready_ = true;
    // Dozens of games at once is an imported or newly connected account, not news.
    constexpr uint32_t bulk = 40;
    if (added.count > bulk) added.count = 0;
    std::vector<Event> events;
    for (auto* event : {&installed, &uninstalled, &added}) if (event->count) events.push_back(std::move(*event));
    return events;
}

void EventQueue::push(Event event, uint64_t now_ms) {
    const auto kind = event.kind;
    const auto same = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.event.kind == kind; });
    if (same != entries_.end()) {
        if (kind == EventKind::added || kind == EventKind::installed || kind == EventKind::uninstalled) { merge(same->event, event); return; }
        if (kind != EventKind::session) entries_.erase(same); // Only the latest launch, premiere list or late night counts.
    }
    entries_.push_back({std::move(event), now_ms, now_ms + pace(kind).lifetime_ms});
}

std::optional<Event> EventQueue::pop(uint64_t now_ms) {
    std::erase_if(entries_, [&](const Entry& entry) { return entry.expires <= now_ms; });
    if (entries_.empty()) return std::nullopt;
    const auto best = std::min_element(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        const int pa = pace(a.event.kind).priority, pb = pace(b.event.kind).priority;
        return pa != pb ? pa > pb : a.queued < b.queued;
    });
    auto event = std::move(best->event);
    entries_.erase(best);
    return event;
}

std::vector<size_t> spinReel(size_t pool_size, uint32_t seed, size_t length) {
    std::vector<size_t> reel;
    if (!pool_size || !length) return reel;
    std::mt19937 random(seed);
    std::uniform_int_distribution<size_t> pick(0, pool_size - 1);
    reel.reserve(length);
    while (reel.size() < length) {
        const auto next = pick(random);
        if (pool_size > 1 && !reel.empty() && reel.back() == next) continue;
        reel.push_back(next);
    }
    return reel;
}

std::wstring rouletteKickoff(const std::wstring& title, bool english) {
    return english ? L"(‹He|She|They› spun the game roulette in the chat and it landed on [[" + title + L"]]. Announce the result in your style, "
                     L"1-2 sentences, and encourage ‹him|her|them› to play it. Write the title exactly as [[" + title + L"]].)"
                   : L"(‹Zakręcił|Zakręciła› ruletką gier w czacie i wypadło: [[" + title + L"]]. Ogłoś wynik w swoim stylu, 1–2 zdania, "
                     L"i zachęć do zagrania. Podaj tytuł dokładnie jako [[" + title + L"]].)";
}
} // namespace companion

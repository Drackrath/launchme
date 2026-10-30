// Her life outside the chat: the story of her goals, episodes of her days and plans, what she
// tells or keeps, peeks and leaks between companions, and when she writes first.
#include "companion_mind_internal.hpp"

#include <algorithm>
#include <cwctype>
#include <functional>

namespace companion::mind {
using namespace detail;

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
} // namespace companion::mind

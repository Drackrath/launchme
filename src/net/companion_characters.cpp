#include "companion_characters.hpp"
#include "companion_affinity.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

namespace companion {
namespace {
// CMake: every res/companion/<key>/character.txt with its costume folders (CMakeLists.txt).
constexpr const char* manifest_text =
#include "companion_manifest.inc"
    ;

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
int hex(char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1; }

std::vector<Character>& registry() {
    static std::vector<Character> list = parseCharacters(manifest_text);
    return list;
}
uint64_t revision = 0;
} // namespace

bool characterKey(std::string_view key) {
    return !key.empty() && key.size() <= 32 && key != "day" && key != "streak" && key != "pick" && key != "card" && std::all_of(key.begin(), key.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'; });
}

std::string characterManifest(std::string_view key) {
    const std::string header = "[" + std::string(key) + "]";
    std::string out;
    bool taking = false;
    for (std::string_view rest = manifest_text; !rest.empty();) {
        const auto end = rest.find('\n');
        const auto line = rest.substr(0, end);
        const auto clean = trimmed(line);
        if (clean.starts_with("[") && clean.ends_with("]")) {
            if (taking) break;
            taking = clean == header;
        }
        if (taking) { out += line; out += '\n'; }
        rest = end == rest.npos ? std::string_view{} : rest.substr(end + 1);
    }
    return out;
}

std::vector<Character> parseCharacters(std::string_view manifest) {
    std::vector<Character> out;
    Character* current = nullptr;
    const auto finish = [&] {
        // Never dropped: her index picks her pictures. Without a name (CMake warns) her key names her.
        if (current && current->name.empty()) {
            current->name = wide(current->key);
            if (!current->name.empty() && current->name[0] >= L'a' && current->name[0] <= L'z') current->name[0] = static_cast<wchar_t>(current->name[0] - L'a' + L'A');
        }
        current = nullptr;
    };
    while (!manifest.empty()) {
        const auto end = manifest.find('\n');
        auto raw = manifest.substr(0, end);
        if (raw.starts_with("\xEF\xBB\xBF")) raw.remove_prefix(3); // A BOM from Notepad.
        const auto line = trimmed(raw);
        manifest = end == std::string_view::npos ? std::string_view{} : manifest.substr(end + 1);
        if (line.empty() || line.front() == '#' || line.starts_with("//")) continue;
        if (line.front() == '[' && line.back() == ']') {
            finish();
            const auto key = line.substr(1, line.size() - 2);
            if (!characterKey(key) || std::any_of(out.begin(), out.end(), [&](const Character& c) { return c.key == key; })) continue;
            out.push_back({});
            current = &out.back();
            current->key = std::string(key);
            continue;
        }
        const auto equals = line.find('=');
        if (!current || equals == std::string_view::npos) continue;
        const auto key = trimmed(line.substr(0, equals));
        const auto value = trimmed(line.substr(equals + 1));
        auto& c = *current;
        if (key == "name") c.name = wide(value);
        else if (key == "card_pl") c.card_pl = wide(value);
        else if (key == "card_en") c.card_en = wide(value);
        else if (key == "you_pl") c.you_pl = wide(value);
        else if (key == "her_pl") c.her_pl = wide(value);
        else if (key == "you_en") c.you_en = wide(value);
        else if (key == "her_en") c.her_en = wide(value);
        else if (key == "about_pl") c.about_pl = wide(value);
        else if (key == "about_en") c.about_en = wide(value);
        else if (key == "about_her_pl") c.about_her_pl = wide(value);
        else if (key == "about_her_en") c.about_her_en = wide(value);
        else if (key == "topics_pl" || key == "topics_en") {
            auto& list = key == "topics_pl" ? c.topics_pl : c.topics_en;
            list.clear();
            for (auto rest = value; !rest.empty();) { // "a | b | c"
                const auto bar = rest.find('|');
                const auto topic = trimmed(rest.substr(0, bar));
                if (!topic.empty()) list.push_back(wide(topic));
                rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
            }
        }
        else if (key == "example_pl" || key == "example_en") { // One exchange per line, as many as she has.
            auto& list = key == "example_pl" ? c.examples_pl : c.examples_en;
            if (!value.empty() && list.size() < 8) list.push_back(wide(value));
        }
        else if (key == "home_pl") c.home_pl = wide(value);
        else if (key == "home_en") c.home_en = wide(value);
        else if (key == "timezone") { // "+09:00", "-05:00", "+1"
            auto rest = value;
            int sign = 1;
            if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) { sign = rest.front() == '-' ? -1 : 1; rest.remove_prefix(1); }
            int hours = 0, minutes = 0;
            const auto colon = rest.find(':');
            const auto h = rest.substr(0, colon);
            if (!h.empty() && std::from_chars(h.data(), h.data() + h.size(), hours).ec == std::errc{} && hours <= 14) {
                if (colon != std::string_view::npos) {
                    const auto m = rest.substr(colon + 1);
                    if (std::from_chars(m.data(), m.data() + m.size(), minutes).ec != std::errc{} || minutes >= 60) minutes = 0;
                }
                c.utc_offset = sign * (hours * 60 + minutes);
                c.zoned = true;
            }
        }
        else if (key == "dst") c.dst = value == "eu" ? Character::Dst::eu : value == "us" ? Character::Dst::us : Character::Dst::none;
        else if (key == "chronotype") c.night_owl = value == "night";
        else if (key == "attachment") { if (value == "secure" || value == "anxious" || value == "avoidant" || value == "fearful") c.attachment = std::string(value); }
        else if (key == "location") {
            const auto text = std::string(value);
            const auto comma = text.find(',');
            if (comma != std::string::npos) {
                try {
                    const double lat = std::stod(text.substr(0, comma)), lon = std::stod(text.substr(comma + 1));
                    if (std::abs(lat) <= 90 && std::abs(lon) <= 180) c.location = std::pair{lat, lon};
                } catch (...) {}
            }
        }
        else if (key == "gossip") {
            double share = 0;
            const auto text = std::string(value);
            try { share = std::stod(text); } catch (...) { share = -1; }
            if (share >= 0 && share <= 1) c.gossip = static_cast<float>(share);
        }
        else if (key == "gender") { if (const auto g = genderFromKey(value); g && *g != Gender::unknown) c.gender = *g; }
        else if (key == "attraction") { if (const auto a = attractionFromKey(value)) c.attraction = *a; }
        else if (key == "attraction_flex") { if (const auto f = attractionFlexFromKey(value)) c.attraction_flex = *f; }
        else if (key == "style") { if (const auto s = relationStyleFromKey(value)) c.style = *s; }
        else if (key == "exclusivity") { if (const auto e = exclusivityFromKey(value)) c.exclusivity = *e; }
        else if (key == "role" || key == "default_role") { if (const auto r = relationRoleFromKey(value)) c.default_role = *r; }
        else if (key == "wealth") { if (const auto w = lifeWealthFromKey(value)) c.default_wealth = *w; }
        else if (key == "work") { if (const auto w = lifeWorkFromKey(value)) c.default_work = *w; }
        else if (key == "origin_pl") c.origin_pl = wide(value);
        else if (key == "origin_en") c.origin_en = wide(value);
        else if (key == "occupation_pl") c.occupation_pl = wide(value);
        else if (key == "occupation_en") c.occupation_en = wide(value);
        else if (key == "elevenlabs_voice" || key == "elevenlabs_voice_free") { // "<voice ID> <its name>"
            const auto space = value.find_first_of(" \t");
            const auto id = value.substr(0, space);
            const bool valid = !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); });
            if (!valid) continue;
            const auto label = space == std::string_view::npos ? std::string_view{} : trimmed(value.substr(space + 1));
            (key == "elevenlabs_voice" ? c.voice.voice : c.voice.fallback) = std::string(id);
            (key == "elevenlabs_voice" ? c.voice.voice_name : c.voice.fallback_name) = wide(label);
        }
        else if (key == "elevenlabs_model") {
            if (value.size() <= 64 && std::all_of(value.begin(), value.end(), [](char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-'; }))
                c.voice.model = std::string(value);
        }
        else if (key == "elevenlabs_settings") c.voice.settings = std::string(value.substr(0, 200));
        else if (key == "elevenlabs_tag") c.voice.delivery = wide(value.substr(0, 120));
        else if (key == "favorites_pl" || key == "favorites_en" || key == "goals_pl" || key == "goals_en") {
            auto& list = key == "favorites_pl" ? c.favorites_pl : key == "favorites_en" ? c.favorites_en : key == "goals_pl" ? c.goals_pl : c.goals_en;
            list.clear();
            for (auto rest = value; !rest.empty();) { // "a | b | c"
                const auto bar = rest.find('|');
                const auto part = trimmed(rest.substr(0, bar));
                if (!part.empty() && list.size() < 16) list.push_back(wide(part));
                rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
            }
        }
        else if (key == "likes" || key == "dislikes") {
            auto& list = key == "likes" ? c.likes : c.dislikes;
            list.clear();
            for (auto rest = value; !rest.empty();) { // "a, b | c"
                const auto cut = rest.find_first_of(",|");
                const auto tag = trimmed(rest.substr(0, cut));
                if (!tag.empty() && list.size() < 8) list.push_back(wide(tag));
                rest = cut == std::string_view::npos ? std::string_view{} : rest.substr(cut + 1);
            }
        }
        else if (key.starts_with("costume.")) { // The costume.txt of the costume just listed.
            if (c.costumes.empty()) continue;
            auto& costume = c.costumes.back();
            const auto field = key.substr(8);
            if (field == "name_pl") costume.name_pl = wide(value);
            else if (field == "name_en") costume.name_en = wide(value);
            else if (field == "you_pl") costume.you_pl = wide(value);
            else if (field == "you_en") costume.you_en = wide(value);
            else if (field.starts_with("face_")) { // faces.txt: "face_<emotion>=<x> <y> <size>" (canvas pixels).
                const auto emotion = emotionFromKey(field.substr(5));
                float numbers[3]{};
                auto rest = value;
                bool ok = emotion.has_value();
                for (float& number : numbers) {
                    rest = trimmed(rest);
                    const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), number);
                    if (!ok || parsed.ec != std::errc{}) { ok = false; break; }
                    rest.remove_prefix(static_cast<size_t>(parsed.ptr - rest.data()));
                }
                if (ok && numbers[2] > 0) costume.faces[static_cast<size_t>(*emotion)] = {numbers[0], numbers[1], numbers[2]};
            }
        }
        else if (key == "status") c.soon = value == "soon" || value == "wip";
        else if (key == "persona") { if (const auto persona = personaFromKey(value)) c.persona = *persona; }
        else if (key == "color" && value.size() == 7 && value[0] == '#') {
            int channel[3]{};
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                const int high = hex(value[1 + 2 * i]), low = hex(value[2 + 2 * i]);
                ok = ok && high >= 0 && low >= 0;
                channel[i] = high * 16 + low;
            }
            if (ok) { c.r = channel[0] / 255.0f; c.g = channel[1] / 255.0f; c.b = channel[2] / 255.0f; }
        } else if (key == "costume") {
            int numbers[2]{};
            auto rest = value;
            bool ok = true;
            for (int& number : numbers) {
                const auto space = rest.find(' ');
                if (space == std::string_view::npos || std::from_chars(rest.data(), rest.data() + space, number).ec != std::errc{}) { ok = false; break; }
                rest.remove_prefix(space + 1);
            }
            if (ok && !rest.empty() && numbers[0] >= 0 && numbers[0] < 32 && numbers[1] >= 0 && numbers[1] <= affinity::max_level)
                c.costumes.push_back({static_cast<Look>(out.size() - 1), numbers[0], numbers[1], std::string(rest)});
        }
    }
    finish();
    for (auto& c : out) // The basic outfit first, then by level and name.
        std::stable_sort(c.costumes.begin(), c.costumes.end(), [](const costumes::Costume& a, const costumes::Costume& b) {
            if ((a.slot == 0) != (b.slot == 0)) return a.slot == 0;
            return a.level != b.level ? a.level < b.level : a.folder < b.folder;
        });
    // Costumes follow their character (a skipped bad section moved the ones after it).
    for (size_t i = 0; i < out.size(); ++i) for (auto& costume : out[i].costumes) costume.look = static_cast<Look>(i);
    return out;
}

const std::vector<Character>& characters() { return registry(); }
void setCharacters(std::vector<Character> list) {
    for (size_t i = 0; i < list.size(); ++i) for (auto& costume : list[i].costumes) costume.look = static_cast<Look>(i);
    registry() = std::move(list);
    ++revision;
}
uint64_t charactersRevision() { return revision; }

const Character* character(Look look) {
    const auto& list = characters();
    const auto index = static_cast<size_t>(look);
    return index < list.size() ? &list[index] : nullptr;
}

std::vector<Look> availableLooks() {
    std::vector<Look> out;
    const auto& list = characters();
    for (size_t i = 0; i < list.size(); ++i) if (!list[i].soon) out.push_back(static_cast<Look>(i));
    return out;
}

int lookCount() { return static_cast<int>(characters().size()); }
std::string_view lookKey(Look look) {
    const auto* c = character(look);
    return c ? std::string_view(c->key) : std::string_view{};
}
std::optional<Look> lookFromKey(std::string_view key) {
    const auto& list = characters();
    for (size_t i = 0; i < list.size(); ++i) if (list[i].key == key) return static_cast<Look>(i);
    return std::nullopt;
}
const wchar_t* name(Look look) {
    const auto* c = character(look);
    return c ? c->name.c_str() : L"?";
}

namespace {
// Days since 1970-01-01 of a civil date and back (Howard Hinnant's algorithms).
int64_t daysFrom(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
Clock clockOf(int64_t seconds) {
    const int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const int64_t rest = seconds - days * 86400;
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    Clock out;
    out.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    out.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    out.year = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (out.month <= 2));
    out.weekday = static_cast<int>(((days % 7) + 11) % 7); // 1970-01-01 was a Thursday.
    out.hour = static_cast<int>(rest / 3600);
    out.minute = static_cast<int>(rest % 3600 / 60);
    return out;
}
// The day of the month of the n-th (1-based; 0: last) Sunday of a month.
int sunday(int year, int month, int n) {
    const int last = month == 2 ? (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0) ? 29 : 28) : month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
    if (n == 0) {
        const int weekday = static_cast<int>(((daysFrom(year, static_cast<unsigned>(month), static_cast<unsigned>(last)) % 7) + 11) % 7);
        return last - weekday;
    }
    const int weekday = static_cast<int>(((daysFrom(year, static_cast<unsigned>(month), 1) % 7) + 11) % 7);
    return 1 + (7 - weekday) % 7 + 7 * (n - 1);
}
} // namespace

std::optional<Clock> localClock(const Character& character, uint64_t utc) {
    if (!character.zoned || !utc) return std::nullopt;
    int64_t local = static_cast<int64_t>(utc) + static_cast<int64_t>(character.utc_offset) * 60;
    if (character.dst != Character::Dst::none) {
        const auto standard = clockOf(local);
        const int year = standard.year;
        bool summer = false;
        if (character.dst == Character::Dst::eu) {
            // The last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC.
            const int64_t start = daysFrom(year, 3, static_cast<unsigned>(sunday(year, 3, 0))) * 86400 + 3600;
            const int64_t end = daysFrom(year, 10, static_cast<unsigned>(sunday(year, 10, 0))) * 86400 + 3600;
            summer = static_cast<int64_t>(utc) >= start && static_cast<int64_t>(utc) < end;
        } else {
            // The second Sunday of March 02:00 to the first Sunday of November 01:00, local standard time.
            const int64_t start = daysFrom(year, 3, static_cast<unsigned>(sunday(year, 3, 2))) * 86400 + 2 * 3600;
            const int64_t end = daysFrom(year, 11, static_cast<unsigned>(sunday(year, 11, 1))) * 86400 + 3600;
            summer = local >= start && local < end;
        }
        if (summer) local += 3600;
    }
    return clockOf(local);
}

uint64_t localSeconds(const Character& character, uint64_t utc) {
    const auto clock = localClock(character, utc);
    if (!clock) return 0;
    return static_cast<uint64_t>(daysFrom(clock->year, static_cast<unsigned>(clock->month), static_cast<unsigned>(clock->day))) * 86400 +
           static_cast<uint64_t>(clock->hour) * 3600 + static_cast<uint64_t>(clock->minute) * 60;
}
} // namespace companion

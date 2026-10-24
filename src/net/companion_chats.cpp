#include "companion_chats.hpp"
#include "companion_affinity.hpp"
#include "json.hpp"
#include "../library/game_index.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <iterator>

namespace companion::chats {
namespace {
constexpr size_t file_limit = 4 * 1024 * 1024;
constexpr size_t line_limit = 200, turn_limit = context_turn_limit;

struct Civil { int year, month, day, hour, minute, second; };
// Howard Hinnant's days_from_civil / civil_from_days (proleptic Gregorian).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
Civil civil(uint64_t seconds) {
    const int64_t z = static_cast<int64_t>(seconds / 86400) + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const int y = static_cast<int>(static_cast<int64_t>(yoe) + era * 400) + (m <= 2);
    const auto rest = seconds % 86400;
    return {y, static_cast<int>(m), static_cast<int>(d), static_cast<int>(rest / 3600), static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60)};
}
std::wstring two(int value) { return (value < 10 ? L"0" : L"") + std::to_wstring(value); }

const json::Value* field(const json::Value& object, const char* name, json::Value::Type type) {
    const auto* value = object.get(name);
    return value && value->type == type ? value : nullptr;
}
std::wstring text(const json::Value& object, const char* name) {
    const auto* value = field(object, name, json::Value::Type::string);
    return value ? text_encoding::wideLenient(value->text) : std::wstring{};
}
bool flag(const json::Value& object, const char* name) {
    const auto* value = field(object, name, json::Value::Type::boolean);
    return value && value->flag;
}
uint64_t seconds(const json::Value& object, const char* name) {
    const auto* value = field(object, name, json::Value::Type::number);
    return value && value->numeric > 0 ? static_cast<uint64_t>(value->numeric) : 0;
}
}

std::string Conversation::save() const {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    root.properties["look"] = json::string(std::string(lookKey(look)));
    root.properties["started"] = json::number(static_cast<double>(started));
    root.properties["updated"] = json::number(static_cast<double>(updated));
    root.properties["summarized"] = json::number(static_cast<double>(summarized));
    root.properties["ended"] = json::boolean(ended);
    root.properties["rest_until"] = json::number(static_cast<double>(rest_until));
    if (!photo.empty()) root.properties["photo"] = json::string(text_encoding::utf8Lenient(photo));
    if (!photo_description.empty()) root.properties["photo_description"] = json::string(text_encoding::utf8Lenient(photo_description));
    auto list = json::array();
    for (const auto& line : lines) {
        auto item = json::object();
        item.properties["user"] = json::boolean(line.user);
        if (!line.text.empty()) item.properties["text"] = json::string(text_encoding::utf8Lenient(line.text));
        if (!line.user) item.properties["speaker"] = json::string(std::string(lookKey(line.speaker)));
        if (line.error) item.properties["error"] = json::boolean(true);
        if (line.photo) item.properties["photo"] = json::boolean(true);
        if (line.free) item.properties["free"] = json::boolean(true);
        if (line.roulette) {
            item.properties["roulette"] = json::string(text_encoding::utf8Lenient(*line.roulette));
            item.properties["roulette_key"] = json::string(line.roulette_key);
        }
        if (!line.emotions.empty()) {
            auto marks = json::array();
            for (const auto& segment : line.emotions) {
                auto mark = json::array();
                mark.elements.push_back(json::number(static_cast<double>(segment.at)));
                mark.elements.push_back(json::number(static_cast<double>(static_cast<int>(segment.emotion))));
                marks.elements.push_back(std::move(mark));
            }
            item.properties["emotions"] = std::move(marks);
        }
        if (!line.user && !line.mentions.empty()) {
            auto mentions = json::array();
            for (const auto& mention : line.mentions) {
                auto range = json::array();
                range.elements.push_back(json::number(static_cast<double>(mention.at)));
                range.elements.push_back(json::number(static_cast<double>(mention.length)));
                mentions.elements.push_back(std::move(range));
            }
            item.properties["mentions"] = std::move(mentions);
        }
        list.elements.push_back(std::move(item));
    }
    root.properties["lines"] = std::move(list);
    auto context = json::array();
    for (const auto& turn : turns) {
        auto item = json::object();
        item.properties["user"] = json::boolean(turn.user);
        item.properties["text"] = json::string(text_encoding::utf8Lenient(turn.text));
        context.elements.push_back(std::move(item));
    }
    root.properties["turns"] = std::move(context);
    return json::serialize(root);
}

std::optional<Conversation> Conversation::load(std::string_view source) {
    const auto root = json::parse(source, file_limit);
    if (!root || root->type != json::Value::Type::object || !root->get("version") || root->get("version")->intOr() != 1) return std::nullopt;
    const auto* key = field(*root, "look", json::Value::Type::string);
    const auto look = key ? lookFromKey(key->text) : std::nullopt;
    if (!look) return std::nullopt;
    Conversation out;
    out.look = *look;
    out.started = seconds(*root, "started");
    out.updated = std::max(out.started, seconds(*root, "updated"));
    out.summarized = static_cast<size_t>(seconds(*root, "summarized"));
    out.ended = flag(*root, "ended");
    out.rest_until = seconds(*root, "rest_until");
    out.photo = text(*root, "photo");
    out.photo_description = text(*root, "photo_description");
    if (const auto* list = field(*root, "lines", json::Value::Type::array))
        for (const auto& item : list->elements) {
            if (item.type != json::Value::Type::object || out.lines.size() >= line_limit) continue;
            Line line;
            line.user = flag(item, "user");
            line.text = text(item, "text");
            line.error = flag(item, "error");
            line.photo = flag(item, "photo");
            line.free = flag(item, "free");
            if (const auto* speaker = field(item, "speaker", json::Value::Type::string)) line.speaker = lookFromKey(speaker->text).value_or(out.look);
            else line.speaker = out.look;
            if (const auto* pick = field(item, "roulette", json::Value::Type::string)) {
                line.roulette = text_encoding::wideLenient(pick->text);
                if (const auto* pick_key = field(item, "roulette_key", json::Value::Type::string)) line.roulette_key = pick_key->text;
            }
            if (const auto* marks = field(item, "emotions", json::Value::Type::array))
                for (const auto& mark : marks->elements) {
                    if (mark.type != json::Value::Type::array || mark.elements.size() != 2) continue;
                    const int emotion = mark.elements[1].intOr(-1);
                    const double at = mark.elements[0].type == json::Value::Type::number ? mark.elements[0].numeric : -1;
                    if (emotion < 0 || emotion >= emotion_count || at < 0 || at > static_cast<double>(line.text.size())) continue;
                    line.emotions.push_back({static_cast<size_t>(at), static_cast<Emotion>(emotion)});
                }
            if (line.text.empty() && !line.photo && !line.roulette) continue;
            if (!line.user)
                if (const auto* mentions = field(item, "mentions", json::Value::Type::array))
                    for (const auto& range : mentions->elements) {
                        if (range.type != json::Value::Type::array || range.elements.size() != 2) continue;
                        const int at = range.elements[0].intOr(-1), length = range.elements[1].intOr(-1);
                        if (at < 0 || length <= 0 || static_cast<size_t>(at) > line.text.size() ||
                            static_cast<size_t>(length) > line.text.size() - static_cast<size_t>(at)) continue;
                        if (!line.mentions.empty() && static_cast<size_t>(at) < line.mentions.back().at + line.mentions.back().length) continue;
                        line.mentions.push_back({static_cast<size_t>(at), static_cast<size_t>(length)});
                    }
            out.lines.push_back(std::move(line));
        }
    if (const auto* list = field(*root, "turns", json::Value::Type::array))
        for (const auto& item : list->elements) {
            if (item.type != json::Value::Type::object) continue;
            auto said = text(item, "text");
            if (said.empty()) continue;
            out.turns.push_back({flag(item, "user"), std::move(said)});
        }
    if (out.turns.size() > turn_limit) out.turns.erase(out.turns.begin() + 1, out.turns.end() - static_cast<std::ptrdiff_t>(turn_limit - 1));
    out.summarized = std::min(out.summarized, out.turns.size());
    return out;
}

std::wstring fileStem(uint64_t started) {
    const auto at = civil(started);
    return std::to_wstring(at.year) + two(at.month) + two(at.day) + L"-" + two(at.hour) + two(at.minute) + two(at.second);
}

std::optional<uint64_t> parseStem(std::wstring_view stem) {
    if (stem.size() != 15 || stem[8] != L'-') return std::nullopt;
    int digits[14]{};
    for (size_t i = 0, d = 0; i < stem.size(); ++i) {
        if (i == 8) continue;
        if (stem[i] < L'0' || stem[i] > L'9') return std::nullopt;
        digits[d++] = stem[i] - L'0';
    }
    const auto number = [&](int from, int count) { int value = 0; for (int i = from; i < from + count; ++i) value = value * 10 + digits[i]; return value; };
    const int year = number(0, 4), month = number(4, 2), day = number(6, 2), hour = number(8, 2), minute = number(10, 2), second = number(12, 2);
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) return std::nullopt;
    return localSeconds(year, month, day, hour, minute, second);
}

std::filesystem::path folder(const std::filesystem::path& config, Look look) {
    return config / L"companion" / L"chats" / text_encoding::wideLenient(std::string(lookKey(look)));
}

std::filesystem::path file(const std::filesystem::path& config, Look look, uint64_t started) {
    return folder(config, look) / (fileStem(started) + L".dat");
}

bool write(const std::filesystem::path& config, const Conversation& conversation) {
    if (!conversation.started || lookKey(conversation.look).empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(folder(config, conversation.look), ec);
    return protected_file::write(file(config, conversation.look, conversation.started), conversation.save(), L"Launchme companion chat");
}

std::optional<Conversation> read(const std::filesystem::path& path) {
    const auto bytes = protected_file::read(path, file_limit);
    return bytes ? Conversation::load(*bytes) : std::nullopt;
}

std::vector<uint64_t> list(const std::filesystem::path& config, Look look) {
    std::vector<uint64_t> found;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder(config, look), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != L".dat") continue;
        if (const auto started = parseStem(it->path().stem().wstring())) found.push_back(*started);
    }
    std::sort(found.begin(), found.end(), std::greater<>());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

std::optional<Conversation> latest(const std::filesystem::path& config, Look look) {
    for (const auto started : list(config, look))
        if (auto conversation = read(file(config, look, started)); conversation && conversation->look == look) {
            conversation->started = started; // The name is the identity.
            return conversation;
        }
    return std::nullopt;
}

void prune(const std::filesystem::path& config, Look look, size_t count) {
    const auto all = list(config, look);
    for (size_t i = count; i < all.size(); ++i) {
        std::error_code ec;
        std::filesystem::remove(file(config, look, all[i]), ec);
    }
}

bool commonWord(std::wstring_view folded) {
    // Words that say nothing about a topic (Polish and English, folded): two notes that share only
    // these are not about the same thing, and they never seed or link her associations.
    static constexpr std::wstring_view common[]{
        L"nie", L"sie", L"jak", L"jest", L"sa", L"ale", L"tak", L"czy", L"bo", L"ze", L"to", L"ten", L"ta", L"te", L"tego", L"tej", L"tym",
        L"jego", L"jej", L"ich", L"mnie", L"mi", L"ci", L"cie", L"go", L"mu", L"nam", L"was", L"wam", L"juz", L"jeszcze", L"tylko", L"bardzo",
        L"tez", L"takze", L"albo", L"lub", L"oraz", L"gdy", L"kiedy", L"jesli", L"jezeli", L"gdzie", L"tam", L"tu", L"tutaj", L"dla", L"przez",
        L"przy", L"pod", L"nad", L"bez", L"od", L"do", L"po", L"na", L"za", L"we", L"ze", L"co", L"kto", L"ktory", L"ktora", L"ktore",
        L"ktorzy", L"jaki", L"jaka", L"jakie", L"moze", L"musi", L"ma", L"mam", L"masz", L"mamy", L"maja", L"byl", L"byla", L"bylo",
        L"byli", L"bedzie", L"jestem", L"jestes", L"jestesmy", L"sobie", L"siebie", L"swoj", L"swoja", L"swoje", L"swoich", L"moj", L"moja",
        L"moje", L"twoj", L"twoja", L"twoje", L"jednak", L"wiec", L"zawsze", L"nigdy", L"czesto", L"teraz", L"dzis", L"dzisiaj", L"wczoraj",
        L"jutro", L"bardziej", L"wszystko", L"wszyscy", L"cos", L"ktos", L"nic", L"nikt", L"jakis", L"jakas", L"ostatnio", L"chyba",
        L"gracz", L"gracza", L"graczowi", L"graczem", L"graczu", L"player", L"players",
        L"the", L"and", L"but", L"for", L"with", L"that", L"this", L"these", L"those", L"are", L"was", L"were", L"been", L"being",
        L"has", L"have", L"had", L"not", L"you", L"your", L"his", L"her", L"him", L"she", L"they", L"them", L"their", L"its", L"from",
        L"into", L"about", L"when", L"what", L"which", L"who", L"how", L"why", L"then", L"than", L"there", L"here", L"also", L"just",
        L"very", L"really", L"some", L"any", L"all", L"can", L"will", L"would", L"should", L"could", L"does", L"did", L"doing", L"yet",
        L"still", L"never", L"always", L"often", L"now", L"today", L"yesterday", L"tomorrow", L"lately", L"maybe"};
    return std::find(std::begin(common), std::end(common), folded) != std::end(common);
}

std::vector<std::wstring> searchWords(std::wstring_view text, size_t limit) {
    std::vector<std::wstring> words;
    std::wstring word;
    const auto flush = [&] {
        auto folded = game_index::fold(word);
        word.clear();
        if (folded.size() < 3 || commonWord(folded)) return;
        if (folded.size() >= 6) folded.resize(folded.size() - 2); // "wiedźmina" and "wiedźminem" share a stem.
        if (std::find(words.begin(), words.end(), folded) == words.end() && words.size() < limit) words.push_back(std::move(folded));
    };
    for (const wchar_t ch : text) {
        if (std::iswalnum(ch)) word += ch;
        else flush();
    }
    flush();
    return words;
}

bool sameStem(std::wstring_view a, std::wstring_view b) {
    // Stems of one word cut at different lengths ("deszc", "desz"): one starts the other.
    return a.size() >= 3 && b.size() >= 3 && (a.starts_with(b) || b.starts_with(a));
}

int matches(const std::vector<std::wstring>& words, std::wstring_view text) {
    if (words.empty()) return 0;
    const auto stems = searchWords(text, 64);
    int score = 0;
    for (const auto& word : words)
        if (std::any_of(stems.begin(), stems.end(), [&](const std::wstring& stem) { return sameStem(word, stem); })) ++score;
    return score;
}

float overlap(const std::vector<std::wstring>& a, const std::vector<std::wstring>& b, size_t* shared_out) {
    size_t shared = 0;
    for (const auto& word : a)
        if (std::any_of(b.begin(), b.end(), [&](const std::wstring& other) { return sameStem(word, other); })) ++shared;
    if (shared_out) *shared_out = shared;
    const size_t smaller = std::min(a.size(), b.size());
    return smaller ? static_cast<float>(shared) / static_cast<float>(smaller) : 0.0f;
}

std::vector<Found> search(const std::filesystem::path& config, Look look, std::wstring_view topic, uint64_t skip, size_t files, size_t limit) {
    std::vector<Found> found;
    const auto words = searchWords(topic);
    if (words.empty()) return found;
    size_t read_files = 0;
    for (const auto started : list(config, look)) {
        if (started == skip) continue;
        if (read_files++ >= files) break;
        const auto conversation = read(file(config, look, started));
        if (!conversation) continue;
        for (const auto& line : conversation->lines) {
            if (line.text.empty()) continue;
            const int score = matches(words, line.text);
            if (score) found.push_back({started, line.user, line.text.size() > 300 ? line.text.substr(0, 300) + L"…" : line.text, score});
        }
    }
    // The best first; among equals the newest (list() reads the newest first, and the sort is stable).
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.score > b.score; });
    if (found.size() > limit) found.resize(limit);
    return found;
}

uint64_t localSeconds(int year, int month, int day, int hour, int minute, int second) {
    const auto days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return days < 0 ? 0 : static_cast<uint64_t>(days) * 86400 + static_cast<uint64_t>(hour * 3600 + minute * 60 + second);
}

uint64_t dayOf(uint64_t local_seconds) { return (local_seconds > day_starts ? local_seconds - day_starts : 0) / 86400; }

std::wstring label(uint64_t local_seconds, bool english) {
    static constexpr const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    const auto at = civil(local_seconds);
    const auto time = two(at.hour) + L":" + two(at.minute);
    if (english) return std::wstring(months[std::clamp(at.month, 1, 12) - 1]) + L" " + std::to_wstring(at.day) + L" · " + time;
    return two(at.day) + L"." + two(at.month) + L" · " + time;
}

std::wstring ago(uint64_t then, uint64_t now, bool english) {
    const auto a = dayOf(then), b = dayOf(now);
    const uint64_t days = b > a ? b - a : 0;
    if (english) return days == 0 ? L"today" : days == 1 ? L"yesterday" : std::to_wstring(days) + L" days ago";
    return days == 0 ? L"dziś" : days == 1 ? L"wczoraj" : std::to_wstring(days) + L" dni temu";
}

std::wstring duration(uint64_t seconds, bool english) {
    const uint64_t minutes = std::max<uint64_t>(1, (seconds + 59) / 60);
    const uint64_t hours = minutes / 60, rest = minutes % 60;
    if (!hours) return std::to_wstring(minutes) + L" min";
    const std::wstring h = std::to_wstring(hours) + (english ? L" h" : L" godz.");
    return rest ? h + L" " + std::to_wstring(rest) + L" min" : h;
}

uint64_t restSeconds(Persona persona, int level, uint32_t seed) {
    const double fond = (std::clamp(level, 1, affinity::max_level) - 1) / static_cast<double>(affinity::max_level - 1);
    const double base = 60 - 45 * fond; // Minutes: an hour at first, a quarter of an hour when she adores the player.
    double pace = 1;
    switch (persona) {
    case Persona::genki: pace = 0.7; break;     // Back on her feet in no time.
    case Persona::deredere: pace = 0.6; break;  // Misses the player at once.
    case Persona::yandere: pace = 0.45; break;  // Cannot stay away for long.
    case Persona::tsundere: pace = 1.2; break;  // Makes the player wait (on purpose).
    case Persona::kuudere: pace = 1.3; break;   // Takes her quiet time.
    case Persona::dandere: pace = 1.4; break;   // Talking tires the shy one out.
    case Persona::himedere: pace = 1.5; break;  // A princess keeps the player waiting.
    case Persona::kanedere: pace = 1.1; break;  // Busy with her own affairs.
    case Persona::kichidere: pace = 0.4 + (seed >> 11) % 121 / 100.0; break; // Anything from a moment to ages.
    }
    const double mood = 0.75 + (seed % 51) / 100.0; // Her mood today: ±25%.
    const double minutes = std::clamp(base * pace * mood, 5.0, 120.0);
    return static_cast<uint64_t>(minutes * 60);
}

std::wstring farewellKickoff(bool english, bool bedtime) {
    if (bedtime)
        return english ? L"(The conversation is coming to an end: this is your last answer in it. It is late where you are and you are going to bed now. Answer "
                         L"‹his|her|their› message, then say goodnight briefly and in your own way — you will be back in the morning. Ask no more questions.)"
                       : L"(Rozmowa dobiega końca: to twoja ostatnia odpowiedź w niej. U ciebie jest już późno i idziesz teraz spać. Odpowiedz na ‹jego|jej› wiadomość, "
                         L"a potem powiedz krótko i po swojemu dobranoc — wrócisz rano. Nie zadawaj już pytań.)";
    // A thread left open for next time (the Zeigarnik effect): a talk that ends with something to come back to.
    return english ? L"(The conversation is coming to an end: this is your last answer in it. Answer ‹his|her|their› message, then say goodbye briefly and in your own "
                     L"way — you need to rest now and will be back later. Leave one small thing open for next time (something you'll tell ‹him|her|them› or want to hear about "
                     L"later — as a promise, not a question) and note it in <ocena> as \"obiecuje\" or \"ciekawosc\" with \"kiedy\":\"pozniej\". Ask no more questions.)"
                   : L"(Rozmowa dobiega końca: to twoja ostatnia odpowiedź w niej. Odpowiedz na ‹jego|jej› wiadomość, a potem pożegnaj się krótko i po swojemu — "
                     L"musisz teraz odpocząć, wrócisz później. Zostaw jedną drobną rzecz otwartą na następny raz (coś, co ‹mu|jej› opowiesz albo o czym chcesz potem "
                     L"usłyszeć — jako zapowiedź, nie pytanie) i zapisz ją w <ocena> jako \"obiecuje\" albo \"ciekawosc\" z \"kiedy\":\"pozniej\". Nie zadawaj już pytań.)";
}

std::wstring bedtimeKickoff(bool english) {
    return english ? L"(‹He has|She has|They have› gone quiet, and it is late where you are: you are going to bed. Say goodnight briefly and in your own way — you will be back in "
                     L"the morning. This is your last message in this conversation; ask no more questions.)"
                   : L"(‹On|Ona› od chwili milczy, a u ciebie zrobiło się późno: idziesz spać. Powiedz krótko i po swojemu dobranoc — wrócisz rano. To twoja ostatnia "
                     L"wiadomość w tej rozmowie; nie zadawaj już pytań.)";
}

std::wstring farewellLine(Persona persona, bool english, uint32_t seed, bool bedtime) {
    struct Goodnight { const wchar_t* pl; const wchar_t* en; };
    if (bedtime) { // Goodnight, in her style.
        static constexpr Goodnight night[]{
            {L"[sleepy]Oczy mi się kleją... [wave]Idę spać — dobranoc! Jutro gadamy dalej!", L"[sleepy]My eyes are closing... [wave]Off to bed — goodnight! We'll talk more tomorrow!"},           // genki
            {L"[angry]Już późno, idę spać! [shy]...d-dobranoc. Nie siedź za długo.", L"[angry]It's late, I'm going to bed! [shy]...g-goodnight. Don't stay up too long."},                         // tsundere
            {L"[sleepy]Muszę już spać... [smile]ale będziesz mi się śnić. Dobranoc~", L"[sleepy]I have to sleep now... [smile]but I'll dream of you. Goodnight~"},                                  // yandere
            {L"[cool]Pora spać. Dobranoc. Do rana.", L"[cool]Time to sleep. Goodnight. See you in the morning."},                                                                                      // kuudere
            {L"[shy]U-um... robi się późno... [sleepy]pójdę spać. Dobranoc...", L"[shy]U-um... it's getting late... [sleepy]I'll go to sleep. Goodnight..."},                                         // dandere
            {L"[love]Dziękuję za dzisiaj! [sleepy]Idę spać — śpij dobrze i do jutra!", L"[love]Thank you for today! [sleepy]Off to bed — sleep well, see you tomorrow!"},                            // deredere
            {L"[smug]Księżniczka udaje się na spoczynek. [smile]Dobranoc, ‹poddany|poddana›.", L"[smug]The princess retires for the night. [smile]Goodnight, subject."},                                       // himedere
            {L"[sleepy]Mój mózg się wyłącza... [playful]idę śnić o kosmicznych kanapkach. Dobranoc~", L"[sleepy]My brain is shutting down... [playful]off to dream of space sandwiches. Night~"},  // kichidere
            {L"[smug]Sen to najlepsza inwestycja. [sleepy]Dobranoc — rano wracam do interesów.", L"[smug]Sleep is the best investment. [sleepy]Goodnight — back to business in the morning."}};     // kanedere
        const auto& line = night[static_cast<size_t>(persona) % std::size(night)];
        return english ? line.en : line.pl;
    }
    struct Pair { const wchar_t* pl; const wchar_t* en; };
    static constexpr Pair genki[] = {
        {L"[laugh]Uff, ale się nagadałam! [wave]Lecę naładować baterie — zaraz wracam z nowymi pomysłami!", L"[laugh]Phew, I talked so much! [wave]Off to recharge — back soon with new ideas!"},
        {L"[excited]To była super rozmowa! [wave]Muszę chwilę odsapnąć. Pa, pa!", L"[excited]That was a great talk! [wave]I need a breather. Bye-bye!"}};
    static constexpr Pair tsundere[] = {
        {L"[angry]Dobra, wystarczy na dziś! [shy]N-nie żebym się z tobą dobrze bawiła czy coś... Pa.", L"[angry]Okay, that's enough! [shy]N-not that I had fun with you or anything... Bye."},
        {L"[smug]Gadasz za dużo. Idę odpocząć. [shy]...wróć później, jeśli musisz.", L"[smug]You talk too much. I'm off to rest. [shy]...come back later, if you must."}};
    static constexpr Pair yandere[] = {
        {L"[smile]Muszę na chwilkę odpocząć... [playful]ale będę o tobie myśleć. Cały czas. Nie odchodź daleko~", L"[smile]I have to rest for a moment... [playful]but I'll be thinking of you. All the time. Don't go far~"},
        {L"[shy]Już koniec? [smile]Dobrze... odpocznę i zaraz znów będę tylko twoja.", L"[shy]Over already? [smile]Fine... I'll rest, and then I'm all yours again."}};
    static constexpr Pair kuudere[] = {
        {L"[cool]Na teraz wystarczy. Odpocznę. Wrócimy do tego później.", L"[cool]That will do for now. I'll rest. We'll continue later."},
        {L"[thinking]Rozmowa zakończona. [smile]Było... przyjemnie. Do później.", L"[thinking]Conversation over. [smile]It was... pleasant. Later."}};
    static constexpr Pair dandere[] = {
        {L"[shy]U-um... muszę trochę odpocząć... [smile]dziękuję, że ze mną ‹rozmawiałeś|rozmawiałaś›...", L"[shy]U-um... I need to rest a little... [smile]thank you for talking with me..."},
        {L"[shy]To było dużo słów jak na mnie... [sleepy]pójdę się położyć. Pa...", L"[shy]That was a lot of words for me... [sleepy]I'll go lie down. Bye..."}};
    static constexpr Pair deredere[] = {
        {L"[love]Kocham nasze rozmowy! [smile]Odpocznę chwilkę i zaraz znowu pogadamy, dobrze?", L"[love]I love our talks! [smile]I'll rest a bit and then we'll talk again, okay?"},
        {L"[smile]Ojej, już tyle ‹napisaliśmy|napisałyśmy›! [wave]Idę odpocząć — tęsknij za mną!", L"[smile]Oh, we've written so much! [wave]Off to rest — miss me!"}};
    static constexpr Pair himedere[] = {
        {L"[smug]Księżniczka udziela audiencji tylko do pewnej pory. [smile]Możesz wrócić później, ‹poddany|poddana›.", L"[smug]A princess grants audiences only so long. [smile]You may return later, subject."},
        {L"[smug]Wystarczy na dziś mojej łaskawości. [smile]Odpoczywam. Czekaj cierpliwie.", L"[smug]Enough of my kindness for now. [smile]I'm resting. Wait patiently."}};
    static constexpr Pair kichidere[] = {
        {L"[playful]Mój mózg właśnie się przegrzał! [excited]Idę go schłodzić w lodówce. Hehe~", L"[playful]My brain just overheated! [excited]Going to cool it in the fridge. Hehe~"},
        {L"[excited]Koniec gadania, czas na drzemkę na suficie! [playful]Nie szukaj mnie~", L"[excited]Talking's over, nap time on the ceiling! [playful]Don't look for me~"}};
    static constexpr Pair kanedere[] = {
        {L"[smug]Czas to pieniądz, a ty ‹zużyłeś|zużyłaś› dziś sporo mojego. [smile]Odpocznę — następna rozmowa na twój rachunek.", L"[smug]Time is money, and you've spent a lot of mine. [smile]I'll rest — the next talk is on your tab."},
        {L"[thinking]Limit rozmowy wyczerpany. [smug]Wróć później, może dam ci zniżkę.", L"[thinking]Conversation limit reached. [smug]Come back later, maybe I'll give you a discount."}};
    const auto pick = [&](const Pair (&lines)[2]) { const auto& line = lines[seed % 2]; return std::wstring(english ? line.en : line.pl); };
    switch (persona) {
    case Persona::genki: return pick(genki);
    case Persona::tsundere: return pick(tsundere);
    case Persona::yandere: return pick(yandere);
    case Persona::kuudere: return pick(kuudere);
    case Persona::dandere: return pick(dandere);
    case Persona::deredere: return pick(deredere);
    case Persona::himedere: return pick(himedere);
    case Persona::kichidere: return pick(kichidere);
    case Persona::kanedere: return pick(kanedere);
    }
    return pick(genki);
}

std::wstring returnNote(uint64_t away_seconds, bool english) {
    const auto away = duration(away_seconds, english);
    return english ? L"(‹He has|She has|They have› come back to the chat after " + away + L". You may notice it briefly.)"
                   : L"(‹Wrócił|Wróciła› do rozmowy po " + away + L" przerwy. Możesz to krótko zauważyć.)";
}
}

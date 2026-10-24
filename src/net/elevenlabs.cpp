#include "elevenlabs.hpp"
#include "json.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace elevenlabs {
namespace {
using companion::Emotion;
using companion::Persona;

std::filesystem::path keyFile(const std::filesystem::path& config) { return config / L"companion_elevenlabs_key.dat"; }

std::wstring trimmed(std::wstring_view text) {
    const auto first = text.find_first_not_of(L" \t\r\n　");
    if (first == std::wstring_view::npos) return {};
    return std::wstring(text.substr(first, text.find_last_not_of(L" \t\r\n　") - first + 1));
}
std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
bool endsSentence(std::wstring_view text, size_t i) {
    const wchar_t ch = text[i];
    if (ch == L'\n' || ch == L'!' || ch == L'?' || ch == L'…' || ch == L'。' || ch == L'！' || ch == L'？') return true;
    if (ch != L'.') return false;
    // Not decimal numbers ("3.5") nor inside an ellipsis ("...").
    if (i > 0 && i + 1 < text.size() && std::iswdigit(text[i - 1]) && std::iswdigit(text[i + 1])) return false;
    if (i + 1 < text.size() && text[i + 1] == L'.') return false;
    return true;
}
// Two decimals: JSON prints 0.45, not 0.449999988.
double rounded(float value) { return std::round(static_cast<double>(value) * 100.0) / 100.0; }
Settings clamped(Settings s) {
    s.stability = std::clamp(s.stability, 0.0f, 1.0f);
    s.similarity = std::clamp(s.similarity, 0.0f, 1.0f);
    s.style = std::clamp(s.style, 0.0f, 1.0f);
    s.speed = std::clamp(s.speed, 0.7f, 1.2f);
    return s;
}
std::string bodyText(const HttpResponse& response) { return std::string(response.body.begin(), response.body.end()); }
// ElevenLabs' reason in a refusal: {"detail":{"status":"quota_exceeded","code":…}} (or a plain string).
std::string reason(const HttpResponse& response) {
    const auto root = json::parse(bodyText(response));
    const auto* detail = root ? root->get("detail") : nullptr;
    if (!detail) return {};
    if (detail->type == json::Value::Type::string) return detail->text;
    std::string out;
    for (const char* field : {"status", "code", "type"})
        if (const auto* value = detail->get(field)) out += value->stringOr() + " ";
    return out;
}
} // namespace

std::wstring storedKey(const std::filesystem::path& config) {
    const auto text = protected_file::read(keyFile(config), 4096);
    if (!text || text->empty()) return {};
    return trimmed(text_encoding::wideLenient(*text));
}

bool saveKey(const std::filesystem::path& config, std::wstring_view key) {
    const auto clean = trimmed(key);
    std::error_code error;
    if (clean.empty()) { std::filesystem::remove(keyFile(config), error); return !error; }
    if (clean.find_first_of(L" \t\r\n") != std::wstring::npos || clean.size() > 512) return false;
    std::filesystem::create_directories(config, error);
    return protected_file::write(keyFile(config), text_encoding::utf8Lenient(clean), L"Launchme ElevenLabs key");
}

std::wstring key(const std::filesystem::path& config, bool* from_environment) {
    if (from_environment) *from_environment = false;
    if (auto saved = storedKey(config); !saved.empty()) return saved;
    wchar_t value[1025]{};
    const DWORD length = GetEnvironmentVariableW(environment, value, static_cast<DWORD>(std::size(value)));
    if (!length || length >= std::size(value)) return {};
    if (from_environment) *from_environment = true;
    return trimmed(std::wstring_view(value, length));
}

Model model(std::string_view id) {
    for (const auto& item : models) if (id == item.id) return item;
    return {nullptr, nullptr, false, true};
}
bool modelId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-'; });
}
bool voiceId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); });
}

Settings parseSettings(std::string_view text, Settings base) {
    for (auto rest = text; !rest.empty();) {
        const auto cut = rest.find(',');
        const auto part = trimmed(rest.substr(0, cut));
        rest = cut == std::string_view::npos ? std::string_view{} : rest.substr(cut + 1);
        const auto equals = part.find('=');
        if (equals == std::string_view::npos) continue;
        const auto name = trimmed(part.substr(0, equals));
        const auto value = std::string(trimmed(part.substr(equals + 1)));
        double number = 0;
        try { size_t used = 0; number = std::stod(value, &used); if (used != value.size()) continue; } catch (...) { continue; }
        const auto in = [&](double low, double high) { return number >= low && number <= high; };
        if (name == "stability" && in(0, 1)) base.stability = static_cast<float>(number);
        else if ((name == "similarity" || name == "similarity_boost") && in(0, 1)) base.similarity = static_cast<float>(number);
        else if (name == "style" && in(0, 1)) base.style = static_cast<float>(number);
        else if (name == "speed" && in(0.7, 1.2)) base.speed = static_cast<float>(number);
        else if ((name == "speaker_boost" || name == "boost") && (number == 0 || number == 1)) base.speaker_boost = number == 1;
    }
    return base;
}

Settings personaSettings(Persona persona) {
    switch (persona) {
    case Persona::genki: return {0.35f, 0.75f, 0.45f, 1.06f};
    case Persona::tsundere: return {0.40f, 0.75f, 0.40f, 1.04f};
    case Persona::yandere: return {0.45f, 0.80f, 0.35f, 0.97f};
    case Persona::kuudere: return {0.65f, 0.80f, 0.10f, 0.95f};
    case Persona::dandere: return {0.55f, 0.80f, 0.20f, 0.93f};
    case Persona::deredere: return {0.40f, 0.75f, 0.40f, 1.03f};
    case Persona::himedere: return {0.45f, 0.80f, 0.40f, 0.98f};
    case Persona::kichidere: return {0.30f, 0.70f, 0.55f, 1.07f};
    case Persona::kanedere: return {0.50f, 0.75f, 0.25f, 0.92f};
    }
    return {};
}

const wchar_t* personaDelivery(Persona persona) {
    switch (persona) {
    case Persona::genki: return L"bubbly, energetic";
    case Persona::tsundere: return L"sharp, cocky";
    case Persona::yandere: return L"sweet, a little obsessive";
    case Persona::kuudere: return L"calm, flat, matter-of-fact";
    case Persona::dandere: return L"soft, shy";
    case Persona::deredere: return L"warm, affectionate";
    case Persona::himedere: return L"haughty, regal";
    case Persona::kichidere: return L"whimsical, chaotic";
    case Persona::kanedere: return L"sleepy, drawling";
    }
    return L"";
}

const wchar_t* emotionTag(Emotion emotion) {
    switch (emotion) {
    case Emotion::smile: return L"happy";
    case Emotion::angry: return L"annoyed";
    case Emotion::sad: return L"sad";
    case Emotion::excited: return L"excited";
    case Emotion::shock: return L"surprised";
    case Emotion::thinking: return L"thoughtful";
    case Emotion::smug: return L"smug";
    case Emotion::shy: return L"shyly";
    case Emotion::fear: return L"nervous";
    case Emotion::sleepy: return L"sleepy, yawning";
    case Emotion::playful: return L"playfully";
    case Emotion::love: return L"affectionately";
    case Emotion::laugh: return L"laughs";
    case Emotion::cry: return L"crying";
    case Emotion::confused: return L"confused";
    case Emotion::determined: return L"determined";
    case Emotion::wave: return L"cheerfully";
    case Emotion::gaming: return L"focused";
    case Emotion::cool: case Emotion::drink: return nullptr;
    }
    return nullptr;
}

Settings tuned(Settings s, Emotion emotion) {
    switch (emotion) {
    case Emotion::excited: case Emotion::laugh: case Emotion::shock: case Emotion::playful: case Emotion::wave: case Emotion::gaming:
        s.stability -= 0.10f; s.style += 0.15f; s.speed *= 1.05f; break;
    case Emotion::angry: case Emotion::determined: s.stability -= 0.10f; s.style += 0.15f; s.speed *= 1.03f; break;
    case Emotion::sad: case Emotion::cry: s.stability += 0.05f; s.style += 0.10f; s.speed *= 0.92f; break;
    case Emotion::sleepy: s.stability += 0.10f; s.speed *= 0.88f; break;
    case Emotion::shy: case Emotion::fear: s.stability -= 0.05f; s.speed *= 0.96f; break;
    case Emotion::thinking: case Emotion::confused: s.speed *= 0.96f; break;
    case Emotion::smug: case Emotion::love: case Emotion::smile: s.style += 0.10f; break;
    case Emotion::cool: case Emotion::drink: break;
    }
    return clamped(s);
}

const Builtin& builtinVoice(Persona persona) {
    switch (persona) {
    case Persona::genki: return builtin_voices[0];     // Jessica: playful, bright.
    case Persona::tsundere: return builtin_voices[1];  // Laura: quirky attitude.
    case Persona::yandere: return builtin_voices[4];   // Lily: velvety.
    case Persona::kuudere: return builtin_voices[2];   // Sarah: mature, confident.
    case Persona::dandere: return builtin_voices[6];   // Matilda.
    case Persona::deredere: return builtin_voices[5];  // Bella: bright, warm.
    case Persona::himedere: return builtin_voices[3];  // Alice: clear, British.
    case Persona::kichidere: return builtin_voices[1];
    case Persona::kanedere: return builtin_voices[5];
    }
    return builtin_voices[0];
}

std::optional<std::vector<Voice>> parseVoices(std::string_view text) {
    const auto root = json::parse(text, 8 * 1024 * 1024);
    const auto* list = root ? root->get("voices") : nullptr;
    if (!list || list->type != json::Value::Type::array) return std::nullopt;
    std::vector<Voice> out;
    for (const auto& item : list->elements) {
        const auto field = [&](const json::Value* value, const char* name) { const auto* f = value ? value->get(name) : nullptr; return f ? f->stringOr() : std::string{}; };
        Voice voice{field(&item, "voice_id"), text_encoding::wideLenient(field(&item, "name")), field(&item, "category")};
        const auto* labels = item.get("labels");
        voice.gender = field(labels, "gender"); voice.age = field(labels, "age"); voice.language = field(labels, "language");
        if (voiceId(voice.id) && !voice.name.empty()) out.push_back(std::move(voice));
    }
    return out;
}

std::wstring shortName(std::wstring_view name) {
    auto cut = name.find(L" - ");
    if (cut == std::wstring_view::npos) cut = name.find(L" – ");
    if (cut == std::wstring_view::npos) cut = name.find(L" — ");
    return trimmed(name.substr(0, cut));
}

Speaker speaker(const companion::Character* character, Persona persona, std::string_view chosen_voice, std::string_view model_override,
                const std::vector<Voice>& known) {
    Speaker out;
    const auto add = [&](std::string_view id, std::wstring_view name) {
        if (!voiceId(id) || std::find(out.voices.begin(), out.voices.end(), id) != out.voices.end()) return;
        std::wstring label = shortName(name);
        if (label.empty())
            for (const auto& voice : known) if (voice.id == id) { label = shortName(voice.name); break; }
        out.voices.emplace_back(id);
        out.names.push_back(std::move(label));
    };
    add(chosen_voice, {});
    const companion::VoiceProfile* profile = character ? &character->voice : nullptr;
    if (profile) { add(profile->voice, profile->voice_name); add(profile->fallback, profile->fallback_name); }
    const auto& builtin = builtinVoice(persona);
    add(builtin.id, builtin.name);
    out.model = modelId(model_override) ? std::string(model_override) : profile && modelId(profile->model) ? profile->model : std::string(default_model);
    out.settings = clamped(parseSettings(profile ? std::string_view(profile->settings) : std::string_view{}, personaSettings(persona)));
    out.delivery = profile && !profile->delivery.empty() ? profile->delivery : std::wstring(personaDelivery(persona));
    return out;
}

std::vector<std::wstring> sentences(std::wstring_view text) {
    std::vector<std::wstring> pieces;
    std::wstring current;
    for (size_t i = 0; i < text.size(); ++i) {
        current += text[i];
        // A run of closing marks ("?!", "…") ends one sentence.
        if (endsSentence(text, i) && (i + 1 >= text.size() || !endsSentence(text, i + 1))) {
            if (auto piece = trimmed(current); !piece.empty()) pieces.push_back(std::move(piece));
            current.clear();
        }
    }
    if (auto piece = trimmed(current); !piece.empty()) pieces.push_back(std::move(piece));
    // Very short pieces ("Hm?") sound odd alone: they join the next sentence.
    std::vector<std::wstring> merged;
    for (auto& piece : pieces) {
        if (!merged.empty() && merged.back().size() < 6) merged.back() += L" " + piece;
        else merged.push_back(std::move(piece));
    }
    return merged;
}

std::wstring speakable(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch >= 0xD800 && ch <= 0xDFFF) continue;                                   // Emoji and other astral symbols.
        if ((ch >= 0x2600 && ch <= 0x27BF) || (ch >= 0x2B00 && ch <= 0x2BFF) || (ch >= 0x2190 && ch <= 0x21FF)) continue; // Symbols, arrows.
        if (ch == 0xFE0F || ch == 0xFE0E || ch == 0x200D || ch == 0x200B) continue;   // Variation selectors, joiners.
        if (ch == L'*' || ch == L'~' || ch == L'_' || ch == L'#' || ch == L'[' || ch == L']' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'^') continue;
        const bool space = ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == 0x3000;
        if (space) { if (!out.empty() && out.back() != L' ') out += L' '; continue; }
        out += ch;
    }
    return trimmed(out);
}

namespace {
// A cue as an audio tag's words ("nervous laugh"): no brackets of its own, at most 48 characters.
std::wstring cueTag(std::wstring_view cue) {
    std::wstring out;
    for (const wchar_t ch : cue) if (ch != L'[' && ch != L']' && ch != L'{' && ch != L'}') out += ch;
    out = trimmed(out);
    return out.size() > 48 ? out.substr(0, 48) : out;
}
// How she sounds, then the parts of her feeling it does not already say ("sleepy, drawling" with
// "sleepy, yawning" → "sleepy, drawling, yawning"; "playful" covers "playfully").
std::wstring joinDirections(std::wstring_view delivery, std::wstring_view feeling) {
    const auto lower = [](std::wstring_view text) { std::wstring out(text); for (auto& ch : out) ch = static_cast<wchar_t>(std::towlower(ch)); return out; };
    const auto said = lower(delivery);
    std::wstring out(trimmed(delivery));
    for (auto rest = feeling; !rest.empty();) {
        const auto cut = rest.find(L',');
        const auto part = trimmed(rest.substr(0, cut));
        rest = cut == std::wstring_view::npos ? std::wstring_view{} : rest.substr(cut + 1);
        if (part.empty()) continue;
        const auto stem = lower(part).substr(0, 5);
        if (said.find(stem) != std::wstring::npos) continue;
        out += (out.empty() ? L"" : L", ") + part;
    }
    return out;
}
} // namespace

std::vector<Chunk> chunks(const std::vector<companion::Line>& lines, const std::function<Speaker(companion::Look)>& speaker_of) {
    std::vector<Chunk> out;
    for (const auto& line : lines) {
        if (line.text.empty()) continue;
        const auto who = speaker_of ? speaker_of(line.speaker) : Speaker{};
        const bool tags = model(who.model).tags;
        const auto& text = line.text;
        // Her feeling at an offset of her message.
        const auto feeling = [&](size_t at) {
            Emotion now = line.emotions.empty() ? companion::calm : line.emotions.front().emotion;
            for (const auto& segment : line.emotions) {
                if (segment.at <= at) now = segment.emotion;
                else break;
            }
            return now;
        };
        // Her sentences as offsets: each owns the text up to the next one (a cue after its full stop is hers).
        struct Piece { size_t begin, end; Emotion emotion; };
        std::vector<Piece> pieces;
        size_t cursor = 0;
        for (const auto& part : sentences(text)) {
            const auto found = text.find(part, cursor);
            if (found == std::wstring::npos) continue; // A merged piece with its space tidied: its words are in the next search.
            if (!speakable(part).empty()) pieces.push_back({found, found + part.size(), feeling(found)});
            cursor = found + part.size();
        }
        if (pieces.empty()) {
            if (auto words = speakable(text); !words.empty()) pieces.push_back({0, text.size(), feeling(0)});
            else continue;
        }
        for (size_t i = 0; i < pieces.size(); ++i) pieces[i].end = i + 1 < pieces.size() ? pieces[i + 1].begin : text.size();
        pieces.front().begin = 0;
        const auto plain = [&](size_t from, size_t to) { return from >= to ? std::wstring{} : speakable(std::wstring_view(text).substr(from, to - from)); };
        for (size_t start = 0; start < pieces.size();) {
            // The first sentence alone; then as much of the rest as fits (one feeling per chunk without tags).
            size_t end = start + 1;
            if (start > 0)
                while (end < pieces.size() && pieces[end].end - pieces[start].begin < 400 && (tags || pieces[end].emotion == pieces[start].emotion)) ++end;
            const size_t from = pieces[start].begin, to = pieces[end - 1].end;
            Chunk chunk{line.speaker, {}, plain(0, from), plain(to, text.size()), pieces[start].emotion, start == 0};
            if (chunk.previous.size() > 600) chunk.previous.erase(0, chunk.previous.size() - 600);
            if (chunk.next.size() > 600) chunk.next.resize(600);
            if (!tags) { // No acting: her words alone; her feeling goes into the settings.
                chunk.text = plain(from, to);
                if (!chunk.text.empty()) out.push_back(std::move(chunk));
                start = end;
                continue;
            }
            // Where her voice acts inside this chunk: a new feeling, and the cues she wrote.
            struct Mark { size_t at; std::wstring tag; };
            std::vector<Mark> marks;
            Emotion was = pieces[start].emotion;
            for (const auto& segment : line.emotions) {
                if (segment.at <= from || segment.at >= to) continue;
                if (segment.emotion != was)
                    if (const auto* tag = emotionTag(segment.emotion)) marks.push_back({segment.at, tag});
                was = segment.emotion;
            }
            for (const auto& cue : line.cues)
                if (cue.at >= from && (cue.at < to || (to == text.size() && cue.at == to)))
                    if (auto tag = cueTag(cue.text); !tag.empty()) marks.push_back({cue.at, std::move(tag)});
            std::stable_sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.at < b.at; });
            // How she sounds and feels from the start, then her words with each mark in its place.
            const auto* opening = emotionTag(pieces[start].emotion);
            const std::wstring direction = joinDirections(who.delivery, opening ? opening : L"");
            if (!direction.empty()) chunk.text = L"[" + direction + L"]";
            size_t at = from;
            std::wstring last = opening ? opening : L""; // A cue that only repeats her feeling at the start: once.
            for (const auto& mark : marks) {
                const auto words = plain(at, std::max(at, mark.at));
                if (!words.empty()) { chunk.text += (chunk.text.empty() ? L"" : L" ") + words; last.clear(); }
                if (mark.tag != last) chunk.text += (chunk.text.empty() ? L"[" : L" [") + mark.tag + L"]"; // The same mark twice in a row: once.
                last = mark.tag;
                at = std::max(at, mark.at);
            }
            if (const auto words = plain(at, to); !words.empty()) chunk.text += (chunk.text.empty() ? L"" : L" ") + words;
            if (!plain(from, to).empty()) out.push_back(std::move(chunk));
            start = end;
        }
    }
    return out;
}

Sample sample(Persona persona, bool english) {
    static constexpr std::array<Sample, companion::persona_count> polish{{
        {L"Hejka! {giggles} W co dzisiaj gramy? Odpalaj coś fajnego!", Emotion::excited},
        {L"N-nie żebym tu na ciebie specjalnie czekała! {scoffs} Wybieraj wreszcie jakąś grę!", Emotion::angry},
        {L"Czekałam na ciebie cały dzień... {whispers} Obiecaj, że dziś rozmawiasz tylko ze mną, dobrze?", Emotion::playful},
        {L"Witaj z powrotem. {sighs} Statystycznie twoja kupka wstydu znów urosła. Zaczynajmy.", Emotion::cool},
        {L"E-em... {nervous laugh} przepraszam... jeśli masz chwilkę, to może pogramy dzisiaj razem?", Emotion::shy},
        {L"Tak się cieszę, że już jesteś! {laughs} Chodź, wybierzemy razem najlepszą grę na dzisiaj!", Emotion::smile},
        {L"Ohohoho! {laughs} Bądź wdzięczny, że Wasza Wysokość raczy dziś osobiście wybrać ci grę!", Emotion::smug},
        {L"Hehe, a wiesz, że twoje niegrane gry szepczą o tobie w nocy? {whispers} Policzyłam je wszystkie co do jednej!", Emotion::playful},
        {L"{yawns} Nudzi mi się... Ile kosztowała ta gra? Jak coś drogiego, to łaskawie dotrzymam ci towarzystwa.", Emotion::sleepy},
    }};
    static constexpr std::array<Sample, companion::persona_count> english_samples{{
        {L"Hiya! {giggles} What are we playing today? Fire up something fun!", Emotion::excited},
        {L"I-it's not like I was waiting here just for you! {scoffs} Hurry up and pick a game already!", Emotion::angry},
        {L"I've been waiting for you all day... {whispers} Promise you'll only talk to me today, okay?", Emotion::playful},
        {L"Welcome back. {sighs} Statistically, your backlog has grown again. Let us begin.", Emotion::cool},
        {L"U-um... {nervous laugh} sorry... if you have a moment, maybe we could play something together?", Emotion::shy},
        {L"I'm so happy you're here! {laughs} Come on, let's pick the best game for today together!", Emotion::smile},
        {L"Ohohoho! {laughs} Be grateful that Her Highness deigns to choose a game for you herself!", Emotion::smug},
        {L"Hehe, did you know your unplayed games whisper about you at night? {whispers} I counted every single one!", Emotion::playful},
        {L"{yawns} I'm bored... How much did that game cost? If it was pricey, I'll graciously keep you company.", Emotion::sleepy},
    }};
    return (english ? english_samples : polish)[static_cast<size_t>(persona)];
}

HttpResponse request(const std::wstring& key, const std::wstring& path, const std::string* body) {
    const std::wstring url = std::wstring(api) + path;
    const std::wstring headers = L"xi-api-key: " + key + L"\r\n" +
                                 (body ? L"Content-Type: application/json\r\nAccept: audio/wav\r\n" : L"Accept: application/json\r\n");
    return httpApi(url, headers, body, 16 * 1024 * 1024, body ? 30000 : 10000);
}

Problem problem(const HttpResponse& response) {
    if (response.status == 200) return Problem::none;
    if (response.status == 0) return Problem::offline;
    const auto why = reason(response);
    if (why.find("quota_exceeded") != std::string::npos) return Problem::quota;
    if (why.find("missing_permissions") != std::string::npos) return Problem::permission;
    if (response.status == 402 || why.find("paid_plan_required") != std::string::npos || why.find("payment_required") != std::string::npos) return Problem::paid_voice;
    if (response.status == 401 || response.status == 403) return Problem::key_refused;
    if (response.status == 429) return Problem::busy;
    return Problem::failed;
}

Status check(const Request& request, const std::wstring& key) {
    Status out;
    if (key.empty()) { out.problem = Problem::no_key; return out; }
    const auto response = request(key, L"v1/voices", nullptr);
    out.status = response.status;
    if (response.status == 200) {
        out.ok = true;
        out.voices = parseVoices(bodyText(response)).value_or(std::vector<Voice>{});
        return out;
    }
    out.problem = problem(response);
    // A key that may speak but not list voices still speaks.
    out.ok = out.problem == Problem::permission;
    if (out.ok) out.problem = Problem::none;
    return out;
}

std::string body(const Chunk& chunk, std::string_view model_id, const Settings& settings, bool english, bool stitched) {
    const auto chosen = model(model_id);
    const auto voice = chosen.tags ? clamped(settings) : tuned(settings, chunk.emotion);
    auto root = json::object();
    root.properties["text"] = json::string(text_encoding::utf8Lenient(chunk.text));
    root.properties["model_id"] = json::string(std::string(model_id));
    root.properties["language_code"] = json::string(english ? "en" : "pl");
    auto values = json::object();
    values.properties["stability"] = json::number(rounded(voice.stability));
    values.properties["similarity_boost"] = json::number(rounded(voice.similarity));
    values.properties["style"] = json::number(rounded(voice.style));
    values.properties["speed"] = json::number(rounded(voice.speed));
    values.properties["use_speaker_boost"] = json::boolean(voice.speaker_boost);
    root.properties["voice_settings"] = std::move(values);
    if (stitched && chosen.stitching) {
        if (!chunk.previous.empty()) root.properties["previous_text"] = json::string(text_encoding::utf8Lenient(chunk.previous));
        if (!chunk.next.empty()) root.properties["next_text"] = json::string(text_encoding::utf8Lenient(chunk.next));
    }
    return json::serialize(root);
}

Spoken speak(const Request& request, const std::wstring& key, const Speaker& speaker, const Chunk& chunk, bool english,
             const std::set<std::string>& blocked) {
    Spoken out;
    if (key.empty()) { out.problem = Problem::no_key; return out; }
    bool tried = false;
    for (const auto& voice : speaker.voices) {
        if (blocked.contains(voice) || std::find(out.refused.begin(), out.refused.end(), voice) != out.refused.end()) continue;
        tried = true;
        const std::wstring path = L"v1/text-to-speech/" + text_encoding::wideLenient(voice) + L"?output_format=wav_24000";
        auto payload = body(chunk, speaker.model, speaker.settings, english);
        auto response = request(key, path, &payload);
        if (response.status == 429) { Sleep(700); response = request(key, path, &payload); } // Too many at once: once more.
        if (response.status == 400) { payload = body(chunk, speaker.model, speaker.settings, english, false); response = request(key, path, &payload); }
        out.status = response.status;
        if (response.status == 200 && response.body.size() > 44 && std::string_view(reinterpret_cast<const char*>(response.body.data()), 4) == "RIFF") {
            out.wav = std::move(response.body);
            out.voice = voice;
            out.problem = Problem::none;
            return out;
        }
        const auto why = problem(response);
        const auto detail = reason(response);
        // Her plan cannot use this voice, or it is gone: the next one in her chain.
        if (why == Problem::paid_voice || response.status == 404 || detail.find("voice_not_found") != std::string::npos) {
            out.refused.push_back(voice);
            out.problem = Problem::paid_voice;
            continue;
        }
        out.problem = why == Problem::none ? Problem::failed : why;
        return out;
    }
    if (!tried) out.problem = Problem::paid_voice;
    return out;
}
} // namespace elevenlabs

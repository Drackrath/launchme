// The companion's vocabulary and connection: emotion/persona/relation keys, models and providers,
// the AI service connection and its stored token, and the library digest for the prompt. Prompts:
// companion_prompt*.cpp; replies: companion_reply.cpp; requests: companion_transport.cpp.
#include "companion.hpp"
#include "companion_internal.hpp"
#include "../platform/system.hpp"
#include "companion_characters.hpp"
#include "json.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <random>

namespace companion {
using namespace detail;
namespace {
constexpr std::array<const char*, emotion_count> emotion_keys{"smile", "angry", "sad", "excited", "shock", "thinking", "smug", "shy", "fear", "sleepy", "playful", "cool",
                                                             "love", "laugh", "cry", "confused", "determined", "wave", "drink", "gaming"};
// Letters and digits only, lower case: "Portal 2" == "portal-2" == "PORTAL™ 2".
std::wstring folded(std::wstring_view text) {
    std::wstring result;
    for (const wchar_t ch : text) if (std::iswalnum(ch)) result += static_cast<wchar_t>(std::towlower(ch));
    return result;
}
std::wstring hostOf(std::wstring_view url) {
    const auto scheme = url.find(L"://");
    if (scheme == std::wstring_view::npos) return {};
    const auto rest = url.substr(scheme + 3);
    const auto authority = rest.substr(0, rest.find(L'/'));
    if (authority.empty()) return {};
    return std::wstring(url.substr(0, scheme)) + L"://" + std::wstring(authority) + L"/";
}
// JSONC (opencode.jsonc): comments and trailing commas removed outside strings.
std::string stripJsonc(std::string_view text) {
    std::string out;
    bool string = false, escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (string) {
            out += ch;
            if (escaped) escaped = false; else if (ch == '\\') escaped = true; else if (ch == '"') string = false;
            continue;
        }
        if (ch == '"') { string = true; out += ch; continue; }
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/') { while (i < text.size() && text[i] != '\n') ++i; out += '\n'; continue; }
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            ++i; continue;
        }
        if (ch == ']' || ch == '}') {
            auto end = out.find_last_not_of(" \t\r\n");
            if (end != std::string::npos && out[end] == ',') out.erase(end, 1);
        }
        out += ch;
    }
    return out;
}
bool tokenText(std::wstring_view token) {
    if (token.size() < 20 || token.size() > 8192) return false;
    return std::all_of(token.begin(), token.end(), [](wchar_t ch) {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'_' || ch == L'-' || ch == L'~' || ch == L'+' || ch == L'/' || ch == L'=';
    });
}
} // namespace

const char* emotionKey(Emotion emotion) { return emotion_keys[static_cast<size_t>(emotion)]; }
std::optional<Emotion> emotionFromKey(std::string_view key) {
    for (size_t i = 0; i < emotion_keys.size(); ++i) if (key == emotion_keys[i]) return static_cast<Emotion>(i);
    return std::nullopt;
}
const char* personaKey(Persona persona) { return archetypes[static_cast<size_t>(persona)].key; }
std::optional<Persona> personaFromKey(std::string_view key) {
    for (size_t i = 0; i < archetypes.size(); ++i) if (key == archetypes[i].key) return static_cast<Persona>(i);
    return std::nullopt;
}
const wchar_t* personaName(Persona persona) { return archetypes[static_cast<size_t>(persona)].name; }

const char* genderKey(Gender gender) {
    switch (gender) {
    case Gender::female: return "female";
    case Gender::male: return "male";
    default: return "unknown";
    }
}
std::optional<Gender> genderFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "female" || k == "f" || k == "kobieta") return Gender::female;
    if (k == "male" || k == "m" || k == "mezczyzna") return Gender::male;
    if (k == "unknown" || k == "auto" || k == "none") return Gender::unknown;
    return std::nullopt;
}
const char* attractionKey(Attraction attraction) {
    switch (attraction) {
    case Attraction::male: return "male";
    case Attraction::female: return "female";
    case Attraction::none: return "none";
    default: return "any";
    }
}
std::optional<Attraction> attractionFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "any" || k == "bi" || k == "pan" || k == "both") return Attraction::any;
    if (k == "male" || k == "m" || k == "men") return Attraction::male;
    if (k == "female" || k == "f" || k == "women") return Attraction::female;
    if (k == "none" || k == "aro" || k == "aromantic") return Attraction::none;
    return std::nullopt;
}
const char* attractionFlexKey(AttractionFlex flex) {
    return flex == AttractionFlex::strict ? "strict" : "rare_exception";
}
std::optional<AttractionFlex> attractionFlexFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "strict" || k == "hard") return AttractionFlex::strict;
    if (k == "rare_exception" || k == "exception" || k == "flexible") return AttractionFlex::rare_exception;
    return std::nullopt;
}
const char* relationStyleKey(RelationStyle style) {
    switch (style) {
    case RelationStyle::traditional: return "traditional";
    case RelationStyle::casual: return "casual";
    default: return "balanced";
    }
}
std::optional<RelationStyle> relationStyleFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "traditional" || k == "tradycyjny" || k == "formal") return RelationStyle::traditional;
    if (k == "casual" || k == "luzny") return RelationStyle::casual;
    if (k == "balanced" || k == "normal" || k == "auto") return RelationStyle::balanced;
    return std::nullopt;
}
const char* relationRoleKey(RelationRole role) {
    switch (role) {
    case RelationRole::friend_role: return "friend";
    case RelationRole::coworker: return "coworker";
    case RelationRole::romantic: return "romantic";
    case RelationRole::secret_romance: return "secret_romance";
    default: return "auto";
    }
}
std::optional<RelationRole> relationRoleFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "auto" || k == "default") return RelationRole::auto_role;
    if (k == "friend" || k == "platonic" || k == "przyjaciel" || k == "przyjazn") return RelationRole::friend_role;
    if (k == "coworker" || k == "work" || k == "professional" || k == "praca") return RelationRole::coworker;
    if (k == "romantic" || k == "romance" || k == "partner" || k == "romans") return RelationRole::romantic;
    if (k == "secret_romance" || k == "secret") return RelationRole::secret_romance;
    return std::nullopt;
}
const char* relationStanceKey(RelationStance stance) {
    switch (stance) {
    case RelationStance::dilemma: return "dilemma";
    case RelationStance::distanced: return "distanced";
    case RelationStance::broken: return "broken";
    default: return "open";
    }
}
std::optional<RelationStance> relationStanceFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "open" || k == "otwarta") return RelationStance::open;
    if (k == "dilemma" || k == "dylemat") return RelationStance::dilemma;
    if (k == "distanced" || k == "distance" || k == "dystans") return RelationStance::distanced;
    if (k == "broken" || k == "breakthrough" || k == "przelamanie") return RelationStance::broken;
    return std::nullopt;
}
const char* exclusivityKey(Exclusivity exclusivity) {
    switch (exclusivity) {
    case Exclusivity::open: return "open";
    case Exclusivity::fluid: return "fluid";
    default: return "monogamous";
    }
}
std::optional<Exclusivity> exclusivityFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "monogamous" || k == "mono" || k == "monogamiczna") return Exclusivity::monogamous;
    if (k == "open" || k == "poly" || k == "polyamorous" || k == "poligamiczna" || k == "otwarta") return Exclusivity::open;
    if (k == "fluid" || k == "undecided" || k == "plynna" || k == "niezdecydowana") return Exclusivity::fluid;
    return std::nullopt;
}
const char* lifeHealthKey(LifeHealth v) {
    switch (v) {
    case LifeHealth::tired: return "tired";
    case LifeHealth::sick: return "sick";
    case LifeHealth::injured: return "injured";
    case LifeHealth::recovering: return "recovering";
    default: return "fit";
    }
}
std::optional<LifeHealth> lifeHealthFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "fit" || k == "zdrowa" || k == "ok") return LifeHealth::fit;
    if (k == "tired" || k == "przemeczona" || k == "zmeczona") return LifeHealth::tired;
    if (k == "sick" || k == "chora" || k == "przeziebiona") return LifeHealth::sick;
    if (k == "injured" || k == "kontuzja" || k == "uraz") return LifeHealth::injured;
    if (k == "recovering" || k == "zdrowieje") return LifeHealth::recovering;
    return std::nullopt;
}
const char* lifeWorkKey(LifeWork v) {
    switch (v) {
    case LifeWork::busy: return "busy";
    case LifeWork::between_jobs: return "between_jobs";
    case LifeWork::promoted: return "promoted";
    case LifeWork::burnout: return "burnout";
    default: return "steady";
    }
}
std::optional<LifeWork> lifeWorkFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "steady" || k == "stabilna_praca" || k == "stabilnie") return LifeWork::steady;
    if (k == "busy" || k == "nawal_pracy" || k == "sesja" || k == "deadline") return LifeWork::busy;
    if (k == "between_jobs" || k == "brak_pracy" || k == "bezrobocie" || k == "szuka_pracy") return LifeWork::between_jobs;
    if (k == "promoted" || k == "awans" || k == "sukces") return LifeWork::promoted;
    if (k == "burnout" || k == "wypalenie" || k == "kryzys") return LifeWork::burnout;
    return std::nullopt;
}
const char* lifeWealthKey(LifeWealth v) {
    switch (v) {
    case LifeWealth::tight: return "tight";
    case LifeWealth::broke: return "broke";
    case LifeWealth::flush: return "flush";
    default: return "comfortable";
    }
}
std::optional<LifeWealth> lifeWealthFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "comfortable" || k == "stabilnie" || k == "normal") return LifeWealth::comfortable;
    if (k == "tight" || k == "krucho" || k == "oszczedza") return LifeWealth::tight;
    if (k == "broke" || k == "splukana" || k == "dolek") return LifeWealth::broke;
    if (k == "flush" || k == "premia" || k == "bogato") return LifeWealth::flush;
    return std::nullopt;
}
const char* lifeFamilyKey(LifeFamily v) {
    switch (v) {
    case LifeFamily::visit: return "visit";
    case LifeFamily::tension: return "tension";
    case LifeFamily::homesick: return "homesick";
    default: return "calm";
    }
}
std::optional<LifeFamily> lifeFamilyFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "calm" || k == "rodzina_spokoj") return LifeFamily::calm;
    if (k == "visit" || k == "rodzina_wizyta" || k == "rodzina") return LifeFamily::visit;
    if (k == "tension" || k == "rodzina_spiecie" || k == "klotnia") return LifeFamily::tension;
    if (k == "homesick" || k == "tesknota") return LifeFamily::homesick;
    return std::nullopt;
}
const char* lifeSocialKey(LifeSocial v) {
    switch (v) {
    case LifeSocial::lonely: return "lonely";
    case LifeSocial::overwhelmed: return "overwhelmed";
    case LifeSocial::inspired: return "inspired";
    default: return "balanced";
    }
}
std::optional<LifeSocial> lifeSocialFromKey(std::string_view key) {
    const auto k = lower(key);
    if (k == "balanced" || k == "rownowaga") return LifeSocial::balanced;
    if (k == "lonely" || k == "samotna") return LifeSocial::lonely;
    if (k == "overwhelmed" || k == "przebodzcowana") return LifeSocial::overwhelmed;
    if (k == "inspired" || k == "wena") return LifeSocial::inspired;
    return std::nullopt;
}
DayPart dayPart(int hour) {
    hour = ((hour % 24) + 24) % 24;
    return hour >= 5 && hour < 12 ? DayPart::morning : hour >= 12 && hour < 18 ? DayPart::afternoon : hour >= 18 && hour < 23 ? DayPart::evening : DayPart::night;
}
std::wstring modelLabel(std::string_view id) {
    for (const auto& model : models) if (id == model.id) return model.label;
    for (const auto& model : xai_models) if (id == model.id || id == dottedModel(model.id)) return model.label;
    return text_encoding::wideLenient(id);
}

std::string geminiModel(std::string_view id) {
    std::string out(id);
    // A hyphen between two digits is the version's dot: gemini-3-8-flash → gemini-3.8-flash.
    for (size_t i = 1; i + 1 < out.size(); ++i)
        if (out[i] == '-' && std::isdigit(static_cast<unsigned char>(out[i - 1])) && std::isdigit(static_cast<unsigned char>(out[i + 1]))) { out[i] = '.'; break; }
    return out;
}

const char* providerKey(Provider provider) {
    switch (provider) {
    case Provider::gemini: return "gemini";
    case Provider::claude: return "claude";
    case Provider::ollama: return "ollama";
    case Provider::xai: return "xai";
    default: return "foundry";
    }
}
std::optional<Provider> providerFromKey(std::string_view key) {
    for (int i = 0; i < provider_count; ++i) if (key == providerKey(static_cast<Provider>(i))) return static_cast<Provider>(i);
    return std::nullopt;
}
const wchar_t* providerName(Provider provider) {
    switch (provider) {
    case Provider::gemini: return L"Gemini";
    case Provider::claude: return L"Claude";
    case Provider::ollama: return L"Ollama";
    case Provider::xai: return L"xAI";
    default: return L"Foundry";
    }
}

bool Connection::ready() const {
    switch (provider) {
    case Provider::foundry: return !host.empty() && !token.empty();
    case Provider::ollama: return !host.empty();
    default: return !token.empty();
    }
}

Dialect Connection::dialect() const {
    switch (provider) {
    case Provider::claude: return Dialect::claude;
    case Provider::ollama: return Dialect::ollama;
    case Provider::xai: return Dialect::responses;
    case Provider::foundry: return grokModel(model) ? Dialect::responses : Dialect::gemini;
    default: return Dialect::gemini;
    }
}

std::string Connection::wireModel() const {
    if (provider == Provider::xai) return dottedModel(model);
    if (provider == Provider::foundry && !model.starts_with("ri.")) return "ri.language-model-service..language-model." + model;
    return model;
}

std::wstring Connection::endpoint() const {
    switch (provider) {
    case Provider::gemini:
        return L"https://generativelanguage.googleapis.com/v1beta/models/" + text_encoding::wideLenient(geminiModel(model)) + L":generateContent";
    case Provider::claude: return L"https://api.anthropic.com/v1/messages";
    case Provider::ollama: return host + L"api/chat";
    case Provider::xai: return L"https://api.x.ai/v1/responses";
    default: {
        if (grokModel(model)) return host + L"api/v2/llm/proxy/xai/v1/responses";
        const auto id = text_encoding::wideLenient(model);
        const std::wstring rid = id.starts_with(L"ri.") ? id : L"ri.language-model-service..language-model." + id;
        return host + L"api/v2/llm/proxy/google/v1/models/" + rid + L":generateContent";
    }
    }
}

std::wstring Connection::headers() const {
    std::wstring out;
    switch (provider) {
    case Provider::gemini: out = L"x-goog-api-key: " + token + L"\r\n"; break;
    case Provider::claude: out = L"x-api-key: " + token + L"\r\nanthropic-version: 2023-06-01\r\n"; break;
    case Provider::ollama: if (!token.empty()) out = L"Authorization: Bearer " + token + L"\r\n"; break;
    default: out = L"Authorization: Bearer " + token + L"\r\n"; break;
    }
    return out + L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
}

std::wstring Connection::label() const {
    switch (provider) {
    case Provider::claude:
        for (const auto& item : claude_models) if (model == item.id) return std::wstring(item.label) + L" · Anthropic";
        return text_encoding::wideLenient(model) + L" · Anthropic";
    case Provider::ollama: return (model.empty() ? std::wstring(L"Ollama") : text_encoding::wideLenient(model)) + L" · Ollama";
    case Provider::gemini: return modelLabel(model) + L" · Google";
    case Provider::xai: return modelLabel(model) + L" · xAI";
    default: return modelLabel(model) + L" · Foundry";
    }
}

std::wstring normalizeHost(std::wstring_view text) {
    auto value = trim(text);
    if (value.empty()) return {};
    if (value.find(L"://") == std::wstring::npos) value = L"https://" + value;
    if (!value.starts_with(L"https://")) return {};
    auto host = hostOf(value);
    if (host.size() <= 9 || host.find_first_of(L" \t\"'<>\\") != std::wstring::npos) return {};
    return host;
}

std::wstring normalizeServer(std::wstring_view text) {
    auto value = trim(text);
    if (value.empty()) return {};
    if (value.find(L"://") == std::wstring::npos) {
        // No scheme: plain HTTP for this machine and the home network (Ollama's default), else HTTPS.
        const auto authority = value.substr(0, value.find(L'/'));
        const auto colon = authority.rfind(L':');
        value = (lanHost(colon == std::wstring::npos ? authority : authority.substr(0, colon)) ? L"http://" : L"https://") + value;
    }
    if (!value.starts_with(L"https://") && !value.starts_with(L"http://")) return {};
    auto host = hostOf(value);
    if (host.size() <= 9 || host.find_first_of(L" \t\"'<>\\") != std::wstring::npos) return {};
    return host;
}

Probe systemProbe() {
    Probe probe;
    probe.environment = [](const wchar_t* name) { return system_info::environment(name, 16383).value_or(std::wstring{}); };
    probe.read_file = [](const std::filesystem::path& file) -> std::optional<std::string> {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0 || input.tellg() > 1024 * 1024) return std::nullopt;
        std::string bytes(static_cast<size_t>(input.tellg()), '\0');
        input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
        return bytes;
    };
    const auto config = probe.environment(L"XDG_CONFIG_HOME");
    const auto home = probe.environment(L"USERPROFILE");
    if (!config.empty()) probe.opencode_dir = std::filesystem::path(config) / L"opencode";
    else if (!home.empty()) probe.opencode_dir = std::filesystem::path(home) / L".config" / L"opencode";
    return probe;
}

Connection resolve(const std::wstring& stored_host, const std::wstring& stored_token, std::string_view model, const Probe& probe) {
    Connection result;
    result.model = model.empty() ? std::string(default_model) : std::string(model);
    const auto env = [&](const wchar_t* name) { return probe.environment ? trim(probe.environment(name)) : std::wstring{}; };
    if (const auto host = normalizeHost(stored_host); !host.empty()) { result.host = host; result.host_source = Source::settings; }
    else if (const auto from_env = normalizeHost(env(L"FOUNDRY_HOST")); !from_env.empty()) { result.host = from_env; result.host_source = Source::environment; }
    if (tokenText(trim(stored_token))) { result.token = trim(stored_token); result.token_source = Source::settings; }
    else if (const auto from_env = env(L"FOUNDRY_TOKEN"); tokenText(from_env)) { result.token = from_env; result.token_source = Source::environment; }
    if (result.ready() || probe.opencode_dir.empty() || !probe.read_file) return result;
    // opencode: the Foundry connection plugin keeps a literal token; providers name the host.
    if (result.token.empty()) {
        for (const auto* name : {L"plugins/connection.ts", L"plugin/connection.ts", L"plugins/connection.js"}) {
            const auto text = probe.read_file(probe.opencode_dir / name);
            if (!text) continue;
            const auto at = text->find("FOUNDRY_TOKEN");
            for (size_t search = at; search != std::string::npos; search = text->find("FOUNDRY_TOKEN", search + 1)) {
                const auto equal = text->find('=', search);
                if (equal == std::string::npos || equal - search > 13 + 6) continue; // "FOUNDRY_TOKEN = …" only.
                const auto open = text->find_first_of("\"'`", equal);
                if (open == std::string::npos || open - equal > 4) continue;
                const auto close = text->find((*text)[open], open + 1);
                if (close == std::string::npos) continue;
                const auto token = text_encoding::wideLenient(std::string_view(*text).substr(open + 1, close - open - 1));
                if (tokenText(token)) { result.token = token; result.token_source = Source::opencode; break; }
            }
            if (!result.token.empty()) break;
        }
    }
    for (const auto* name : {L"opencode.json", L"opencode.jsonc"}) {
        if (result.ready()) break;
        const auto text = probe.read_file(probe.opencode_dir / name);
        if (!text) continue;
        const auto parsed = json::parse(stripJsonc(*text));
        const auto* providers = parsed ? parsed->get("provider") : nullptr;
        if (!providers || providers->type != json::Value::Type::object) continue;
        // foundry-google first: the same proxy family this client calls.
        std::vector<std::pair<std::string, const json::Value*>> ordered;
        for (const auto& [key, value] : providers->properties) ordered.emplace_back(key, &value);
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            const bool ga = a.first == "foundry-google", gb = b.first == "foundry-google";
            return ga != gb ? ga : a.first < b.first;
        });
        for (const auto& [key, provider] : ordered) {
            const auto* options = provider->get("options");
            const auto base = options && options->get("baseURL") ? text_encoding::wideLenient(options->get("baseURL")->stringOr()) : std::wstring{};
            if (base.find(L"palantirfoundry") == std::wstring::npos && key.find("foundry") == std::string::npos) continue;
            if (result.host.empty()) if (const auto host = normalizeHost(base); !host.empty()) { result.host = host; result.host_source = Source::opencode; }
            if (result.token.empty() && options && options->get("apiKey")) {
                const auto key_text = text_encoding::wideLenient(options->get("apiKey")->stringOr());
                if (key_text.starts_with(L"{env:") && key_text.ends_with(L"}")) {
                    const auto variable = key_text.substr(5, key_text.size() - 6);
                    if (const auto value = env(variable.c_str()); tokenText(value)) { result.token = value; result.token_source = Source::environment; }
                } else if (tokenText(key_text)) { result.token = key_text; result.token_source = Source::opencode; }
            }
        }
    }
    return result;
}

Connection resolve(Provider provider, const std::wstring& stored_host, const std::wstring& stored_token, std::string_view model, const Probe& probe) {
    if (provider == Provider::foundry) return resolve(stored_host, stored_token, model, probe);
    Connection result;
    result.provider = provider;
    result.model = std::string(model);
    const auto env = [&](const wchar_t* name) { return probe.environment ? trim(probe.environment(name)) : std::wstring{}; };
    const auto key = [&](std::initializer_list<const wchar_t*> names) {
        if (tokenText(trim(stored_token))) { result.token = trim(stored_token); result.token_source = Source::settings; return; }
        for (const auto* name : names)
            if (const auto value = env(name); tokenText(value)) { result.token = value; result.token_source = Source::environment; return; }
    };
    switch (provider) {
    case Provider::gemini: // Grok is Foundry's or xAI's, never Google's.
        if (result.model.empty() || grokModel(result.model)) result.model = default_model;
        key({L"GEMINI_API_KEY", L"GOOGLE_API_KEY"});
        break;
    case Provider::claude:
        if (result.model.empty()) result.model = default_claude_model;
        key({L"ANTHROPIC_API_KEY"});
        break;
    case Provider::xai:
        if (result.model.empty() || !grokModel(result.model)) result.model = default_xai_model;
        key({L"XAI_API_KEY"});
        break;
    default: // Ollama: its server (this machine by default); a key only for cloud servers.
        if (const auto host = normalizeServer(stored_host); !host.empty()) { result.host = host; result.host_source = Source::settings; }
        else if (const auto from_env = normalizeServer(env(L"OLLAMA_HOST")); !from_env.empty()) { result.host = from_env; result.host_source = Source::environment; }
        else result.host = L"http://127.0.0.1:11434/";
        key({L"OLLAMA_API_KEY"});
        break;
    }
    return result;
}

namespace {
std::filesystem::path tokenFile(const std::filesystem::path& config, Provider provider) {
    switch (provider) {
    case Provider::gemini: return config / L"companion_gemini_key.dat";
    case Provider::claude: return config / L"companion_claude_key.dat";
    case Provider::ollama: return config / L"companion_ollama_key.dat";
    case Provider::xai: return config / L"companion_xai_key.dat";
    default: return config / L"companion_token.dat";
    }
}
}

std::wstring storedToken(const std::filesystem::path& config, Provider provider) {
    const auto bytes = protected_file::read(tokenFile(config, provider), 16384);
    if (!bytes || bytes->empty()) return {};
    return text_encoding::wideLenient(*bytes);
}
bool saveToken(const std::filesystem::path& config, std::wstring_view token, Provider provider) {
    const auto value = trim(token);
    const auto file = tokenFile(config, provider);
    if (value.empty()) { std::error_code ec; std::filesystem::remove(file, ec); return !ec; }
    if (!tokenText(value)) return false;
    std::error_code ec;
    std::filesystem::create_directories(config, ec);
    return protected_file::write(file, text_encoding::utf8(value), L"Launchme companion");
}
bool hasStoredToken(const std::filesystem::path& config, Provider provider) {
    std::error_code ec;
    return std::filesystem::file_size(tokenFile(config, provider), ec) > 0 && !ec;
}

std::vector<const GameFact*> Digest::all() const {
    std::vector<const GameFact*> games;
    for (const auto* list : {&recent, &untouched, &backlog}) for (const auto& game : *list) games.push_back(&game);
    return games;
}
const GameFact* Digest::find(std::wstring_view title) const {
    const auto games = all();
    for (const auto* game : games) if (game->title == title) return game;
    for (const auto* game : games) if (equalsIgnoringCase(game->title, title)) return game;
    const auto key = folded(title);
    if (key.empty()) return nullptr;
    for (const auto* game : games) if (folded(game->title) == key) return game;
    return nullptr;
}

Digest digest(std::vector<GameFact> games, uint32_t seed, std::vector<std::string> resting) {
    Digest result;
    result.resting = std::move(resting);
    const auto rests = [&](const GameFact& game) { return std::find(result.resting.begin(), result.resting.end(), game.key) != result.resting.end(); };
    std::erase_if(games, [](const GameFact& game) { return trim(game.title).empty(); });
    result.total = games.size();
    for (const auto& game : games) {
        if (game.installed) ++result.installed;
        if (!game.last_played && (!game.minutes || !*game.minutes)) ++result.never_played;
    }
    std::mt19937 random(seed);
    std::vector<GameFact> played, untouched, backlog;
    for (auto& game : games) {
        if (game.last_played) played.push_back(std::move(game));
        else if (game.installed) untouched.push_back(std::move(game));
        else backlog.push_back(std::move(game));
    }
    std::sort(played.begin(), played.end(), [](const auto& a, const auto& b) { return a.last_played > b.last_played; });
    if (played.size() > 10) played.resize(10);
    std::shuffle(untouched.begin(), untouched.end(), random);
    std::stable_partition(untouched.begin(), untouched.end(), [&](const GameFact& game) { return !rests(game); });
    if (untouched.size() > 8) untouched.resize(8);
    std::shuffle(backlog.begin(), backlog.end(), random);
    std::stable_partition(backlog.begin(), backlog.end(), [&](const GameFact& game) { return !rests(game); });
    if (backlog.size() > 8) backlog.resize(8);
    result.recent = std::move(played); result.untouched = std::move(untouched); result.backlog = std::move(backlog);
    return result;
}

std::wstring Digest::text(bool english, uint64_t now) const {
    const auto ago = [&](uint64_t when) -> std::wstring {
        if (when > 100000000000ull) when /= 1000; // Milliseconds.
        const uint64_t days = now > when ? (now - when) / 86400 : 0;
        if (english) {
            if (days == 0) return L"today";
            if (days == 1) return L"yesterday";
            if (days < 14) return std::to_wstring(days) + L" days ago";
            if (days < 60) return std::to_wstring(days / 7) + L" weeks ago";
            if (days < 365) return std::to_wstring(days / 30) + L" months ago";
            return L"over a year ago";
        }
        if (days == 0) return L"dziś";
        if (days == 1) return L"wczoraj";
        if (days < 14) return std::to_wstring(days) + L" dni temu";
        if (days < 60) return std::to_wstring(days / 7) + L" tyg. temu";
        if (days < 365) return std::to_wstring(days / 30) + L" mies. temu";
        return L"ponad rok temu";
    };
    const auto played = [&](const GameFact& game) -> std::wstring {
        if (!game.minutes) return english ? L"play time unknown" : L"czas gry nieznany";
        const auto minutes = *game.minutes;
        if (minutes < 60) return std::to_wstring(minutes) + L" min";
        const auto tenths = (minutes * 10 + 30) / 60;
        auto hours = std::to_wstring(tenths / 10);
        if (tenths % 10 && tenths < 1000) hours += (english ? L"." : L",") + std::to_wstring(tenths % 10);
        return hours + (english ? L" h" : L" godz.");
    };
    std::wstring out;
    if (english) out += L"Library: " + std::to_wstring(total) + L" games, " + std::to_wstring(installed) + L" installed, " +
                        std::to_wstring(never_played) + L" never started.\n";
    else out += L"Biblioteka: " + std::to_wstring(total) + L" gier, zainstalowanych " + std::to_wstring(installed) +
                L", nigdy nieuruchomionych " + std::to_wstring(never_played) + L".\n";
    if (!recent.empty()) {
        out += english ? L"Recently played (newest first):\n" : L"Ostatnio grane (od najnowszej):\n";
        for (const auto& game : recent) {
            out += L"- " + game.title + L" [" + game.store + L"] — " + ago(game.last_played) + L", " + played(game);
            if (game.achievements_total) {
                out += english ? L", achievements " : L", osiągnięcia ";
                out += std::to_wstring(game.achievements_unlocked) + L"/" + std::to_wstring(game.achievements_total) +
                       L" (" + std::to_wstring(game.achievements_unlocked * 100 / game.achievements_total) + L"%)";
            }
            if (!game.installed) out += english ? L", uninstalled" : L", odinstalowana";
            out += L"\n";
        }
    }
    const auto list = [&](const std::vector<GameFact>& games, const wchar_t* pl, const wchar_t* en) {
        if (games.empty()) return;
        out += english ? en : pl;
        for (size_t i = 0; i < games.size(); ++i) out += (i ? L", " : L" ") + games[i].title;
        out += L"\n";
    };
    list(untouched, L"Zainstalowane, ale nigdy nieuruchomione:", L"Installed but never started:");
    list(backlog, L"Kupione, nigdy nieuruchomione (próbka):", L"Owned, never started (a sample):");
    if (!total) out += english ? L"The library is empty.\n" : L"Biblioteka jest pusta.\n";
    return out;
}
} // namespace companion

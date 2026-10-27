// Settings → Asystent: her rows (the switch, look, model and connection, voice
// and memory; her photos' rows are in companion_photos.cpp) and their actions.
#include "launcher.hpp"
#include "../net/web_search.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <chrono>
using namespace settings_ui;

namespace {
// Temporarily hidden controls; their preferences, persistence and runtime behavior remain available.
constexpr bool show_advanced_controls = false;
// The models offered for a provider (Ollama has a name field instead): Foundry's Gemini and Grok,
// Google's Gemini, Claude's families, xAI's Grok.
std::vector<companion::ModelInfo> companionModelChoices(companion::Provider provider) {
    using companion::Provider;
    std::vector<companion::ModelInfo> out;
    if (provider == Provider::claude) for (const auto& item : companion::claude_models) out.push_back({item.id, item.label});
    else if (provider == Provider::xai) out.assign(companion::xai_models.begin(), companion::xai_models.end());
    else {
        out.assign(companion::models.begin(), companion::models.end());
        if (provider == Provider::foundry) out.push_back(companion::xai_models.front()); // Foundry's xAI proxy has Grok 4.7.
    }
    return out;
}
bool english() { return i18n::language() == i18n::Language::en; }
uint64_t unixNow() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
// "UTC−8", "UTC+5:30", "UTC" (standard time).
std::wstring utcLabel(int minutes) {
    if (minutes == 0) return L"UTC";
    std::wstring out = minutes < 0 ? L"UTC−" : L"UTC+";
    const int value = minutes < 0 ? -minutes : minutes;
    out += std::to_wstring(value / 60);
    if (value % 60) out += L":" + std::to_wstring(value % 60);
    return out;
}
}


// Asystent: the switch and her look, who she talks with and how she chats; her model, voice and
// phone access behind their tiles (each on its own page); her memory, copy and photos.
std::vector<settings_ui::Row> Launcher::companionSettingsRows() const {
    using namespace settings_ui;
    if (settings_state_.at(Page::companion_model)) return companionModelRows();
    if (settings_state_.at(Page::companion_voice)) return companionVoiceRows();
    if (settings_state_.at(Page::companion_remote)) return companionRemoteRows();
    std::vector<Row> rows;
    const auto section = [&](const wchar_t* title) { rows.back().section = title; }; // On the row just added.
    rows.push_back({Id::companion, Control::toggle, tr(L"Towarzyszka AI"),
        tr(L"Animowana postać na ekranie głównym (w Flix pasek u góry) i czat z nią: C, L2/LT albo kliknięcie. Reaguje na twoje gry; odpowiada wybrany model (Foundry, Gemini, Claude albo Ollama)."), {},
        settings_.companion});
    section(tr(L"Towarzyszka AI"));
    if (!settings_.companion) return rows;
    // Each companion is who she is: her character (-dere) is fixed and shown on her card.
    Row look{Id::companion_look, Control::avatar, tr(L"Postać"), tr(L"Kto z tobą rozmawia (←/→): każda ma swój stały charakter, zainteresowania, emocje i stroje.")};
    for (const auto& character : companion::characters()) look.choices.push_back({character.name});
    look.choice = static_cast<int>(companionLook());
    look.value = companion::name(companionLook());
    rows.push_back(std::move(look));
    // Who she talks with: she calls you by your name or nickname and says "on"/"ona", never "gracz".
    rows.push_back({Id::companion_user_name, Control::text, tr(L"Twoje imię lub ksywka"),
        tr(L"Jak ma się do ciebie zwracać. Przy pierwszym poznaniu widzi to jako twój nick w komunikatorze. Pusty: nie zna twojego imienia, dopóki go nie podasz w rozmowie."),
        settings_.companion_user_name.empty() ? std::wstring(tr(L"Nie podano")) : settings_.companion_user_name});
    section(tr(L"O tobie"));
    {
        static constexpr std::array<const wchar_t*, 3> genders{trKey(L"Automatycznie (z rozmowy)"), trKey(L"Mężczyzna"), trKey(L"Kobieta")};
        Row gender{Id::companion_user_gender, Control::choice, tr(L"Twoja płeć"),
            tr(L"Żeby mówiła i myślała o tobie „on” albo „ona”. Automatycznie: wyczyta to z tego, jak piszesz.")};
        for (const auto* item : genders) gender.choices.push_back({tr(item)});
        gender.choice = std::clamp(static_cast<int>(settings_.companion_user_gender), 0, 2);
        gender.value = gender.choices[gender.choice].label;
        if (settings_.companion_user_gender == AppSettings::CompanionUserGender::automatic) {
            const auto learnt = companionPerson().gender;
            if (learnt != companion::Gender::unknown) gender.value += std::wstring(L": ") + tr(learnt == companion::Gender::male ? L"Mężczyzna" : L"Kobieta");
        }
        rows.push_back(std::move(gender));
    }
    {
        // Where they live: one city for all of them, then the chosen one's own over it. The city's
        // clock decides when she sleeps; her corner of it the model makes up.
        const bool en = english();
        const auto label = [&](const companion::City& city) { return city.home(en) + L" (" + utcLabel(city.utc_offset) + L")"; };
        Row all{Id::companion_city, Control::choice, tr(L"Wspólne miasto"),
            tr(L"Jedno miasto dla wszystkich: jego strefa czasowa ustala, kiedy śpią, a dzielnicę i mieszkanie wymyślają same. Każda u siebie: jak w jej opisie (Europa i Tokio).")};
        all.choices.push_back({tr(L"Każda u siebie")});
        all.choice = 0;
        for (const auto& city : companion::cities()) {
            if (city.key == settings_.companion_city) all.choice = static_cast<int>(all.choices.size());
            all.choices.push_back({label(city)});
        }
        all.value = all.choices[all.choice].label;
        rows.push_back(std::move(all));
        section(tr(L"Gdzie mieszkają"));
        const auto chosen = companionLook();
        const auto key = std::string(companion::lookKey(chosen));
        const auto* her = companion::character(chosen);
        const auto own = settings_.companion_cities.find(key);
        Row city{Id::companion_city_own, Control::choice, std::wstring(tr(L"Miasto: ")) + companion::name(chosen)};
        std::wstring hers; // Her own city, from her character.txt.
        if (her) {
            const auto& home = en && !her->home_en.empty() ? her->home_en : her->home_pl;
            hers = home.substr(0, std::min(home.find(L','), home.find(L" -")));
        }
        city.choices.push_back({tr(L"Jak wszystkie")});
        city.choices.push_back({hers.empty() ? std::wstring(tr(L"U siebie")) : std::wstring(tr(L"U siebie: ")) + hers});
        city.choice = 0;
        if (own != settings_.companion_cities.end() && own->second == companion::own_home) city.choice = 1;
        for (const auto& item : companion::cities()) {
            if (own != settings_.companion_cities.end() && item.key == own->second) city.choice = static_cast<int>(city.choices.size());
            city.choices.push_back({label(item)});
        }
        city.value = city.choices[city.choice].label;
        city.description = tr(L"Tylko dla niej, ponad wspólnym wyborem.");
        if (her)
            if (const auto clock = companion::localClock(*her, unixNow())) {
                const auto two = [](int value) { return (value < 10 ? L"0" : L"") + std::to_wstring(value); };
                const auto& home = her->home(en);
                city.description += std::wstring(tr(L" Teraz: ")) + home.substr(0, std::min(home.find(L','), home.find(L" -"))) + tr(L", godz. ") +
                                    two(clock->hour) + L":" + two(clock->minute) +
                                    (companionAsleep(chosen) ? std::wstring(tr(L" (śpi)")) : std::wstring{}) + L".";
            }
        rows.push_back(std::move(city));
    }
    {
        const auto& style = companion::replyStyle(settings_.companion_reply_length);
        Row length{Id::companion_reply_length, Control::slider, tr(L"Długość odpowiedzi w czacie"),
            std::wstring(tr(L"Od następnej odpowiedzi, bez resetu czatu. 100%: obecnie; −33%: zwięźle; −66%: krótko (orientacyjnie). Próg przerwy: ")) +
                std::to_wstring(style.messageLimit(companion::chats::message_cap)) + tr(L" wiadomości obu stron."),
            style.percent == 100 ? L"100%" : style.percent == 67 ? L"−33%" : L"−66%"};
        length.level = companion::replyLengthIndex(settings_.companion_reply_length) / 2.0f;
        rows.push_back(std::move(length));
        section(tr(L"Czat"));
    }
    rows.push_back({Id::companion_typing, Control::toggle, tr(L"„Pisze…” jak w komunikatorze"),
        tr(L"Bez pojawiających się słów: kropki „pisze…”, gdy odpisuje, potem cała wiadomość naraz. Dłuższa wiadomość pisze się dłużej."), {},
        settings_.companion_typing});
    rows.push_back({Id::companion_weather, Control::toggle, tr(L"Prawdziwa pogoda u niej"),
        tr(L"Zna pogodę tam, gdzie jest: w swoim mieście albo na wyjeździe, i nie wymyśla innej. Wysyłana jest tylko nazwa jej miasta. Dane: Open-Meteo.com (CC BY 4.0)."), {},
        settings_.companion_weather});
    if (show_advanced_controls) rows.push_back({Id::companion_guests, Control::toggle, tr(L"Goście w czacie"),
        tr(L"Druga towarzyszka może wpaść do czatu: sama raz dziennie po kilku wiadomościach albo na zawołanie z menu „…”. Wyłączone: zawsze rozmawiasz tylko z jedną."), {},
        settings_.companion_guests});
    if (show_advanced_controls) rows.push_back({Id::companion_gamification, Control::toggle, tr(L"Grywalizacja"),
        tr(L"Poziom sympatii każdej towarzyszki (do 50, każdy kolejny trochę trudniej — szczyt to ok. trzy miesiące codziennych rozmów). Rośnie za rozmowy, codzienne wizyty z rzędu i granie w gry, które poleciła, albo z kupki wstydu. Wyznacza etap waszej relacji: obcy, od 5 lekki flirt, od 10 śmielszy flirt, od 20 przyjaciele z flirtem, od 30 zauroczenie, a na 50 związek na odległość. Odblokowuje stroje i katalogi zdjęć. Poniżej poziomu 30 mocne przegięcie (nie flirt) daje żółtą kartkę, a kolejne czerwoną i małą karę, która nigdy nie zabiera poziomu. Reset: menu „…” w czacie."), {},
        settings_.companion_gamification});
    // Her services, each a tile with its page: the model that answers, her voice and the phone.
    const auto tile = [&](Page page, const wchar_t* title, std::wstring description, std::wstring value) {
        Row row{Id::page, Control::page, title, std::move(description), std::move(value)};
        row.page = page;
        row.key = std::to_string(static_cast<int>(page));
        rows.push_back(std::move(row));
    };
    {
        static constexpr std::array<const wchar_t*, companion::provider_count> names{L"Foundry", L"Gemini", L"Claude", L"Ollama", L"Grok"};
        const auto access = companionAccess();
        const auto connection = access.resolve();
        tile(Page::companion_model, tr(L"Model AI"),
             connection.ready() ? connection.label() + L" · " + tr(L"dostawca, model, klucz i wyszukiwanie w sieci.") :
                                  std::wstring(tr(L"Brak klucza lub adresu — mówi wtedy z głowy, bez modelu.")),
             names[std::clamp(static_cast<int>(access.provider), 0, companion::provider_count - 1)]);
        section(tr(L"Usługi"));
    }
    {
        static constexpr std::array<const wchar_t*, 3> places{trKey(L"Wyłączony"), trKey(L"W czacie"), trKey(L"W czacie i na ekranie głównym")};
        tile(Page::companion_voice, tr(L"Głos (ElevenLabs)"), companionVoiceRows().front().description,
             tr(places[std::clamp(static_cast<int>(settings_.companion_voice), 0, 2)]));
    }
    tile(Page::companion_remote, tr(L"Dostęp z telefonu"),
         settings_.companion_remote && remote_failed_ ? tr(L"Port zajęty: ") + std::to_wstring(settings_.companion_remote_port) :
             std::wstring(tr(L"Czat z nią i Launchfeed w przeglądarce telefonu, najlepiej przez Tailscale.")),
         settings_.companion_remote ? tr(L"Włączony") : tr(L"Wyłączony"));
    // Her memory: kept on this computer, but sent to the model with her instructions.
    const size_t memory_rows = rows.size();
    {
        std::wstring value;
        if (companion_.semantic) {
            const auto progress = companion_.semantic->progress();
            value = progress.state == companion::semantic::Progress::State::unavailable ? tr(L"Niedostępna — działa pamięć słów") :
                !progress.error.empty() ? tr(L"Indeks działa, ale zapis na dysku się nie powiódł.") :
                std::wstring(tr(L"Zaindeksowane wspomnienia: ")) + std::to_wstring(progress.indexed) + L" / " + std::to_wstring(progress.total);
        }
        rows.push_back({Id::companion_semantic_memory, Control::toggle, tr(L"Pamięć semantyczna"),
            std::wstring(tr(L"Wyszukuje po znaczeniu, lokalnie. Wymaga pamięci rozmów i notatek. ")) + value,
            {}, settings_.companion_semantic_memory});
    }
    if (show_advanced_controls) rows.push_back({Id::companion_memory, Control::toggle, tr(L"Pamięć rozmów"),
        tr(L"Pamięta, co ostatnio mówiła i jakie gry proponowała, wraca do dawnych rozmów, gdy o nie zapytasz, ma swoje humory (tęskni, bywa zazdrosna) i raz w tygodniu zagaduje o twoim graniu. Zapisane lokalnie (zaszyfrowane), ale fragmenty trafiają do modelu razem z instrukcjami."), {},
        settings_.companion_memory});
    if (show_advanced_controls) rows.push_back({Id::companion_notes, Control::toggle, tr(L"Notatki o tobie"),
        tr(L"Zapamiętuje krótkie fakty o tobie (ulubione gatunki, plany) i podsumowania czatów. Też trafiają do modelu w instrukcjach."), {},
        settings_.companion_notes});
    if (const auto& memory = companion_.memory; !memory || !memory->empty() || settings_.companion_memory || settings_.companion_notes)
        rows.push_back({Id::companion_forget, Control::action, tr(L"Wyczyść pamięć"),
            tr(L"Usuwa wszystko, co zapamiętała: ostatnie wypowiedzi, propozycje, notatki i podsumowania."), tr(L"Wyczyść")});
    rows.push_back({Id::companion_backup, Control::action, tr(L"Pełna kopia postaci"),
        tr(L"Samodzielny folder: osobowość i wygląd, rozmowy, pamięć, uczucia, relacja, zdjęcia i Launchfeed. Open-copy.cmd otwiera niezależną kopię. Zawiera prywatne rozmowy w czytelnym formacie; klucze API nie są kopiowane."),
        companion_.backup_busy ? tr(L"Tworzenie kopii…") : tr(L"Wybierz folder")});
    rows[memory_rows].section = tr(L"Pamięć i kopia");
    for (auto& row : companionPhotoRows()) rows.push_back(std::move(row));
    return rows;
}

// Phone access (companion_remote.cpp): the switch, then one tile with every address to type into
// the phone's browser (each with its key), the most likely one first; its button copies that one.
std::vector<settings_ui::Row> Launcher::companionRemoteRows() const {
    using namespace settings_ui;
    std::vector<Row> rows;
    std::wstring about = tr(L"Czat z nią i Launchfeed w przeglądarce telefonu: ta sama rozmowa co tutaj, bez uruchamiania gier. Najlepiej przez Tailscale; adres zawiera klucz, nie udostępniaj go.");
    const auto port = std::to_wstring(settings_.companion_remote_port);
    if (settings_.companion_remote && remote_failed_) about = tr(L"Port zajęty: ") + port;
    rows.push_back({Id::companion_remote, Control::toggle, tr(L"Dostęp z telefonu"), std::move(about), {}, settings_.companion_remote});
    if (!settings_.companion_remote || remote_failed_) return rows;
    const auto suffix = L":" + port + L"/?k=" + text_encoding::wideLenient(settings_.companion_remote_key);
    // Tailscale reaches the phone anywhere; the home network only at home; a public address only
    // with a port forwarded on the router.
    std::vector<std::pair<std::wstring, std::wstring>> addresses; // Label, address.
    for (const auto kind : {remote::Address::Kind::tailscale, remote::Address::Kind::local, remote::Address::Kind::global})
        for (const auto& address : remote_addresses_) if (address.kind == kind)
            addresses.push_back({kind == remote::Address::Kind::tailscale ? std::wstring(L"Tailscale") :
                                 kind == remote::Address::Kind::local ? std::wstring(tr(L"Sieć lokalna")) : std::wstring(tr(L"Adres publiczny")),
                                 L"http://" + address.ip + suffix});
    if (!remote_public_.empty())
        addresses.push_back({std::wstring(tr(L"Adres publiczny")) + tr(L" (wymaga przekierowania portu, niezalecany)"), L"http://" + remote_public_ + suffix});
    if (addresses.empty()) {
        rows.push_back({Id::companion_remote_address, Control::action, tr(L"Adres strony"),
            tr(L"Brak sieci: połącz komputer z siecią lokalną albo z Tailscale."), tr(L"Kopiuj")});
        rows.back().locked = true;
        return rows;
    }
    Row tile{Id::companion_remote_address, Control::action, tr(L"Adres strony: ") + addresses.front().first, {}, tr(L"Kopiuj")};
    for (const auto& [label, url] : addresses) {
        if (!tile.description.empty()) tile.description += L"\n";
        tile.description += label + L": " + url;
    }
    tile.key = text_encoding::utf8(addresses.front().second); // What its button copies.
    tile.lines = static_cast<int>(addresses.size()); // One line per address.
    rows.push_back(std::move(tile));
    return rows;
}

// Her provider, its model and credentials (as the next request will find them, never the key
// itself), the connection test and whether the chat names the model.
std::vector<settings_ui::Row> Launcher::companionModelRows() const {
    using namespace settings_ui;
    using companion::Provider;
    std::vector<Row> rows;
    const auto access = companionAccess();
    static constexpr std::array<const wchar_t*, companion::provider_count> providers{
        trKey(L"Foundry — Gemini lub Grok przez proxy"), trKey(L"Gemini — klucz API Google"), trKey(L"Claude — klucz API Anthropic"), trKey(L"Ollama — własny serwer"),
        trKey(L"Grok — klucz API xAI")};
    Row provider{Id::companion_provider, Control::choice, tr(L"Dostawca modelu"),
        tr(L"Kto za nią odpowiada: Foundry, twój klucz Gemini, Claude lub xAI (Grok) albo serwer Ollama (na tym komputerze, w sieci domowej lub w chmurze).")};
    for (const auto* item : providers) provider.choices.push_back({tr(item)});
    provider.choice = static_cast<int>(access.provider);
    provider.value = provider.choices[provider.choice].label;
    rows.push_back(std::move(provider));
    // The model: Gemini's list (Foundry and Google), Claude's families, or an Ollama model's name.
    if (access.provider == Provider::ollama) {
        rows.push_back({Id::companion_ollama_model, Control::text, tr(L"Model"),
            tr(L"Nazwa jak w „ollama list” (np. llama3.1:8b, qwen3). Pusty: pierwszy model serwera. Narzędzia wymagają modelu z obsługą tools."),
            access.model.empty() ? std::wstring(tr(L"Automatycznie")) : text_encoding::wideLenient(access.model)});
    } else {
        const bool claude = access.provider == Provider::claude;
        Row model{Id::companion_model, Control::choice, tr(L"Model"),
            claude ? tr(L"Najnowszy model wybranej rodziny, jaki daje twój klucz.") :
            access.provider == Provider::gemini ? tr(L"Gemini przez API Google (AI Studio).") :
            access.provider == Provider::xai ? tr(L"Grok przez API xAI; myśli krótko (reasoning: low), więc odpowiada w kilka sekund.") :
            tr(L"Gemini albo Grok przez proxy modeli językowych Foundry.")};
        model.choice = -1;
        for (const auto& item : companionModelChoices(access.provider)) {
            if (access.model == item.id) model.choice = static_cast<int>(model.choices.size());
            model.choices.push_back({item.label});
        }
        if (model.choice < 0) { model.choice = static_cast<int>(model.choices.size()); model.choices.push_back({text_encoding::wideLenient(access.model)}); }
        model.value = model.choices[model.choice].label;
        rows.push_back(std::move(model));
    }
    const auto connection = access.resolve();
    const auto origin = [](companion::Source source) -> std::wstring {
        switch (source) {
        case companion::Source::settings: return tr(L"zapisany tutaj");
        case companion::Source::environment: return tr(L"zmienna środowiskowa");
        case companion::Source::opencode: return L"opencode";
        default: return {};
        }
    };
    const auto key = [&](const wchar_t* title, const wchar_t* description, const wchar_t* variable, bool optional) {
        rows.push_back({Id::companion_token, Control::secret, tr(title), tr(description),
            connection.token.empty() ? std::wstring(optional ? tr(L"Brak (niepotrzebny lokalnie)") : tr(L"Wpisz klucz")) :
            connection.token_source == companion::Source::environment ? std::wstring(variable) : std::wstring(tr(L"Klucz: ")) + origin(connection.token_source)});
    };
    const auto shown = [](std::wstring host) {
        if (host.starts_with(L"https://")) host.erase(0, 8);
        if (!host.empty() && host.back() == L'/') host.pop_back();
        return host;
    };
    switch (access.provider) {
    case Provider::gemini:
        key(trKey(L"Klucz API Gemini"), trKey(L"Z Google AI Studio (aistudio.google.com). Bez niego: GEMINI_API_KEY albo GOOGLE_API_KEY. Wpisany tutaj jest szyfrowany dla twojego konta Windows."),
            L"GEMINI_API_KEY", false);
        break;
    case Provider::claude:
        key(trKey(L"Klucz API Claude"), trKey(L"Z konsoli Anthropic (console.anthropic.com). Bez niego: ANTHROPIC_API_KEY. Wpisany tutaj jest szyfrowany dla twojego konta Windows."),
            L"ANTHROPIC_API_KEY", false);
        break;
    case Provider::xai:
        key(trKey(L"Klucz API xAI"), trKey(L"Z konsoli xAI (console.x.ai). Bez niego: XAI_API_KEY. Wpisany tutaj jest szyfrowany dla twojego konta Windows."),
            L"XAI_API_KEY", false);
        break;
    case Provider::ollama:
        rows.push_back({Id::companion_host, Control::text, tr(L"Adres Ollama"),
            std::wstring(tr(L"Pusty: OLLAMA_HOST albo ten komputer (127.0.0.1:11434). Na tym komputerze i w sieci domowej może być http://. Teraz: ")) +
                (connection.host_source == companion::Source::none ? std::wstring(tr(L"domyślny")) : origin(connection.host_source)) + L".",
            shown(connection.host)});
        key(trKey(L"Klucz API Ollama"), trKey(L"Tylko dla serwerów w chmurze (np. ollama.com). Bez niego: OLLAMA_API_KEY."), L"OLLAMA_API_KEY", true);
        break;
    default: {
        rows.push_back({Id::companion_token, Control::secret, tr(L"Token Foundry"),
            tr(L"Jak w Pythonie i opencode: FOUNDRY_TOKEN albo token z konfiguracji opencode. Wpisany tutaj jest szyfrowany dla twojego konta Windows."),
            connection.token.empty() ? std::wstring(tr(L"Wpisz token")) : connection.token_source == companion::Source::environment ? std::wstring(L"FOUNDRY_TOKEN") :
            std::wstring(tr(L"Token: ")) + origin(connection.token_source)});
        const auto host = shown(connection.host);
        rows.push_back({Id::companion_host, Control::text, tr(L"Adres Foundry"),
            host.empty() ? std::wstring(tr(L"Nie znaleziono: wpisz adres stacka Foundry albo ustaw FOUNDRY_HOST.")) :
                           std::wstring(tr(L"Pusty: FOUNDRY_HOST albo adres z opencode. Teraz: ")) + origin(connection.host_source) + L".",
            host.empty() ? std::wstring(tr(L"Wpisz adres")) : host});
        break;
    }
    }
    rows.push_back({Id::companion_test, Control::action, tr(L"Sprawdź połączenie"),
        connection.ready() ? tr(L"Jedno krótkie pytanie do modelu; odpowiedź usłyszysz od niej.") : tr(L"Brak klucza lub adresu — mówi wtedy z głowy, bez modelu."),
        tr(L"Sprawdź")});
    if (show_advanced_controls) rows.push_back({Id::companion_model_info, Control::toggle, tr(L"Pokaż model w czacie"),
        tr(L"Nazwa modelu i dostawcy w nagłówku czatu. Problemy z połączeniem są pokazywane zawsze."), {}, settings_.companion_model_info});
    {
        bool from_environment = false;
        const auto web = web_search::key(userConfigDirectory(), &from_environment);
        rows.push_back({Id::companion_web_key, Control::secret, tr(L"Klucz Tavily (wyszukiwanie w sieci)"),
            tr(L"Opcjonalny, z tavily.com. Z nim na trudniejsze pytania albo prośbę „sprawdź w necie” szuka w internecie. Bez niego: TAVILY_API_KEY. Wpisany tutaj jest szyfrowany dla twojego konta Windows."),
            web.empty() ? std::wstring(tr(L"Brak - bez wyszukiwania")) : from_environment ? std::wstring(web_search::environment) : std::wstring(tr(L"Klucz zapisany"))});
    }
    return rows;
}

// Her voice from ElevenLabs: where she speaks, the key, the model and her voice; the service's state in the description.
std::vector<settings_ui::Row> Launcher::companionVoiceRows() const {
    using namespace settings_ui;
    using Engine = CompanionVoice::Engine;
    const auto& voice = companion_.voice;
    const bool on = settings_.companion_voice != AppSettings::CompanionVoice::off;
    const auto key = companion_.voice_key();
    std::vector<Row> rows;
    std::wstring status;
    if (!on) status = tr(L"Mówi głosem z ElevenLabs (elevenlabs.io) w języku interfejsu, w duecie każda swoim. Potrzebny klucz API.");
    else if (key.empty()) status = tr(L"Brak klucza API ElevenLabs — wpisz go poniżej albo ustaw ELEVENLABS_API_KEY.");
    else if (voice.engine == Engine::ready) status = tr(L"ElevenLabs działa: mówi w języku interfejsu, w duecie każda swoim głosem.");
    else if (voice.engine == Engine::failed) status = std::wstring(L"ElevenLabs: ") + companionVoiceProblem() + L".";
    else status = tr(L"Sprawdzanie ElevenLabs…");
    static constexpr std::array<const wchar_t*, 3> places{trKey(L"Wyłączony"), trKey(L"W czacie"), trKey(L"W czacie i na ekranie głównym")};
    Row spoken{Id::companion_voice, Control::choice, tr(L"Głos (ElevenLabs)"), status};
    for (const auto* place : places) spoken.choices.push_back({tr(place)});
    spoken.choice = std::clamp(static_cast<int>(settings_.companion_voice), 0, 2);
    spoken.value = spoken.choices[spoken.choice].label;
    rows.push_back(std::move(spoken));
    if (!on) return rows;
    {
        [[maybe_unused]] bool from_environment = false;
#ifdef LAUNCHME_UI_TEST
        const bool stored = !key.empty();
#else
        const bool stored = !elevenlabs::key(userConfigDirectory(), &from_environment).empty();
#endif
        rows.push_back({Id::companion_voice_key, Control::secret, tr(L"Klucz API ElevenLabs"),
            tr(L"Z elevenlabs.io (Developers → API Keys) z uprawnieniem Text to Speech, najlepiej też Voices: read. Bez niego: ELEVENLABS_API_KEY. Wpisany tutaj jest szyfrowany dla twojego konta Windows."),
            !stored ? std::wstring(tr(L"Wpisz klucz")) : from_environment ? std::wstring(elevenlabs::environment) : std::wstring(tr(L"Klucz zapisany"))});
    }
    // The model: each companion's own (character.txt, else v4 Turbo), or one for all of them.
    static constexpr std::array<const wchar_t*, elevenlabs::models.size()> model_notes{
        trKey(L"szybki, z emocjami (polecany)"), trKey(L"najlepsza jakość, wolniejszy"), trKey(L"bardzo ekspresyjny, najwolniejszy"),
        trKey(L"stabilny, bez tagów emocji"), trKey(L"najszybszy i najtańszy, bez tagów emocji")};
    Row model{Id::companion_voice_model, Control::choice, tr(L"Model głosu"),
        tr(L"Automatycznie: każda swój z character.txt (elevenlabs_model), inaczej Eleven v4 Turbo. Modele v3 i v4 grają jej emocje tagami."), {}};
    model.choices.push_back({tr(L"Automatycznie (wg postaci)")});
    model.choice = 0;
    for (size_t i = 0; i < elevenlabs::models.size(); ++i) {
        if (settings_.companion_voice_model == elevenlabs::models[i].id) model.choice = static_cast<int>(model.choices.size());
        model.choices.push_back({std::wstring(elevenlabs::models[i].label) + L" — " + tr(model_notes[i])});
    }
    if (!settings_.companion_voice_model.empty() && model.choice == 0) { // An ID of hers not on the list.
        model.choice = static_cast<int>(model.choices.size());
        model.choices.push_back({text_encoding::wideLenient(settings_.companion_voice_model)});
    }
    model.value = model.choices[model.choice].label;
    rows.push_back(std::move(model));
    // Her voice: automatic (her character.txt's chain) first, then the account's voices.
    const auto look = companionLook();
    const auto speaker = companionVoiceSpeaker(look);
    const auto chosen = settings_.companion_voice_ids.find(companion::lookKey(look));
    const auto hers = elevenlabs::speaker(companion::character(look), companionPersonaOf(look), {}, settings_.companion_voice_model, voice.voices);
    Row style{Id::companion_voice_style, Control::choice, tr(L"Głos postaci")};
    std::wstring automatic = tr(L"Automatycznie");
    if (!hers.voices.empty()) {
        automatic += L": " + (hers.names[0].empty() ? text_encoding::wideLenient(hers.voices[0]) : hers.names[0]);
        if (hers.voices.size() > 1) automatic += std::wstring(tr(L" (zapasowy: ")) + (hers.names[1].empty() ? text_encoding::wideLenient(hers.voices[1]) : hers.names[1]) + L")";
    }
    style.choices.push_back({automatic});
    style.choice = 0;
    for (const auto& item : voice.voices) {
        if (chosen != settings_.companion_voice_ids.end() && chosen->second == item.id) style.choice = static_cast<int>(style.choices.size());
        style.choices.push_back({item.name});
    }
    if (chosen != settings_.companion_voice_ids.end() && style.choice == 0) { // Picked earlier, no longer listed.
        style.choice = static_cast<int>(style.choices.size());
        style.choices.push_back({text_encoding::wideLenient(chosen->second)});
    }
    style.value = style.choices[style.choice].label;
    const auto now = companionVoiceName(look);
    style.description = (now.empty() ? std::wstring(tr(L"Głos z jej character.txt (elevenlabs_voice).")) : std::wstring(tr(L"Teraz mówi głosem: ")) + now + L".") +
        tr(L" Głosy z Biblioteki ElevenLabs działają przez API tylko w płatnym planie; inaczej mówi zapasowym (elevenlabs_voice_free).");
    if (std::any_of(speaker.voices.begin(), speaker.voices.end(), [&](const std::string& id) { return voice.blocked.contains(id); }))
        style.description += tr(L" Twój plan odrzucił jej głos z biblioteki.");
    if (voice.engine == Engine::ready && voice.voices.empty()) style.description += tr(L" Lista głosów konta: klucz bez uprawnienia Voices: read.");
    rows.push_back(std::move(style));
    rows.push_back({Id::companion_voice_test, Control::action, tr(L"Posłuchaj"), tr(L"Krótka próbka jej głosu w wybranym charakterze."), tr(L"Odtwórz")});
    return rows;
}

void Launcher::companionApplyHomes() {
    std::vector<std::pair<std::string, std::string>> each(settings_.companion_cities.begin(), settings_.companion_cities.end());
    companion::setHomeCities(settings_.companion_city, each);
    companion_.weather_tried = 0; // A new city's weather comes with the next check, not in ten minutes.
}

// Enter (A, a click) on one of her rows; false: not hers.
bool Launcher::companionSetting(const Row& row) {
    if (row.id == Id::companion_test) { companionTest(); audio_.context(); return true; }
    if (row.id == Id::companion_voice_test) { companionVoiceTest(); audio_.context(); return true; }
    if (row.id == Id::companion_forget) { companionForget(); audio_.context(); return true; }
    if (row.id == Id::companion_backup) { companionBackup(); return true; }
    if (row.id == Id::companion_remote_address) { // The phone's address with its key, to paste anywhere.
        const auto url = text_encoding::wideLenient(row.key);
        bool copied = false;
        if (!url.empty() && OpenClipboard(hwnd_)) {
            EmptyClipboard();
            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (url.size() + 1) * sizeof(wchar_t))) {
                if (auto* data = static_cast<wchar_t*>(GlobalLock(memory))) {
                    std::copy(url.c_str(), url.c_str() + url.size() + 1, data);
                    GlobalUnlock(memory);
                    copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                    if (!copied) GlobalFree(memory);
                } else GlobalFree(memory);
            }
            CloseClipboard();
        }
        if (copied) showNotification(NotificationKind::launch, tr(L"Skopiowano adres"), url);
        audio_.context();
        return true;
    }
    if (companionPhotoSetting(row)) return true; // Her photos' rows (companion_photos.cpp).
    if (row.id == Id::companion_voice_key) { // ElevenLabs: encrypted like the model's keys; saved empty, it is forgotten.
        if (settings_state_.editing) {
            if (!elevenlabs::saveKey(userConfigDirectory(), settings_secret_input_)) {
                showNotification(NotificationKind::error, tr(L"Nie udało się zapisać klucza"), tr(L"Sprawdź klucz i dostęp do katalogu konfiguracji."));
                return true;
            }
            clearSecretInput();
            settings_state_.editing = false;
            // Another key, maybe another plan: check it, and her refused voices may speak again.
            auto& voice = companion_.voice;
            companionHush();
            voice.blocked.clear(); voice.spoke.clear(); voice.voices.clear();
            voice.problem = elevenlabs::Problem::none; voice.engine = CompanionVoice::Engine::unknown;
            requestCompanionVoiceStatus();
            buildSettingsRows();
        } else {
            clearSecretInput();
            settings_secret_input_ = elevenlabs::storedKey(userConfigDirectory());
            settings_secret_selected_ = true;
            settings_state_.editing = true;
        }
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (row.id == Id::companion_host) {
        // Foundry's stack (https) or the Ollama server (http on this machine and the home network too).
        const bool ollama = settings_.companion_provider == AppSettings::CompanionProvider::ollama;
        auto& stored = ollama ? settings_.companion_ollama_host : settings_.companion_host;
        settingsTextRow(stored, [&](const std::wstring& input) {
            const auto host = ollama ? companion::normalizeServer(input) : companion::normalizeHost(input);
            if (input.find_first_not_of(L" \t") != std::wstring::npos && host.empty()) {
                if (ollama) showNotification(NotificationKind::error, tr(L"Nieprawidłowy adres Ollama"), tr(L"Podaj adres serwera, np. 127.0.0.1:11434 albo https://…"));
                else showNotification(NotificationKind::error, tr(L"Nieprawidłowy adres Foundry"), tr(L"Podaj adres https://… swojego stacka Foundry."));
                return false;
            }
            stored = host; // Empty: the environment (FOUNDRY_HOST / OLLAMA_HOST) or the default again.
            companion_.status.clear();
            return true;
        }, false);
        return true;
    }
    if (row.id == Id::companion_user_name) {
        settingsTextRow(settings_.companion_user_name, [&](const std::wstring& input) {
            settings_.companion_user_name = companionUserName(input); // Empty: she doesn't know it.
            companionSyncPerson();
            return true;
        });
        return true;
    }
    if (row.id == Id::companion_ollama_model) {
        settingsTextRow(text_encoding::wideLenient(settings_.companion_ollama_model), [&](const std::wstring& name) {
            const bool valid = name.size() < 160 && std::all_of(name.begin(), name.end(), [](wchar_t ch) {
                return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'.' || ch == L'_' || ch == L':' || ch == L'/'; });
            if (!valid) {
                showNotification(NotificationKind::error, tr(L"Nieprawidłowa nazwa modelu"), tr(L"Np. llama3.1:8b albo qwen3 — jak w „ollama list”."));
                return false;
            }
            settings_.companion_ollama_model = text_encoding::utf8(name); // Empty: the first one the server has.
            companion_.model_label.clear();
            return true;
        });
        return true;
    }
    if (row.id == Id::companion_web_key) { // Tavily: encrypted like the model's keys; saved empty, it is forgotten.
        if (settings_state_.editing) {
            if (!web_search::saveKey(userConfigDirectory(), settings_secret_input_)) {
                showNotification(NotificationKind::error, tr(L"Nie udało się zapisać klucza"), tr(L"Sprawdź klucz i dostęp do katalogu konfiguracji."));
                return true;
            }
            clearSecretInput();
            settings_state_.editing = false;
            buildSettingsRows();
        } else {
            clearSecretInput();
            settings_secret_input_ = web_search::storedKey(userConfigDirectory());
            settings_secret_selected_ = true;
            settings_state_.editing = true;
        }
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (row.id == Id::companion_token) { // The chosen provider's key, or Foundry's token.
        if (settings_state_.editing) {
            if (!companion::saveToken(userConfigDirectory(), settings_secret_input_, companionAccess().provider)) {
                showNotification(NotificationKind::error, tr(L"Nie udało się zapisać klucza"), tr(L"Sprawdź klucz i dostęp do katalogu konfiguracji."));
                return true;
            }
            clearSecretInput();
            settings_state_.editing = false;
            buildSettingsRows();
        } else {
            clearSecretInput();
            settings_secret_input_ = companion::storedToken(userConfigDirectory(), companionAccess().provider);
            settings_secret_selected_ = true;
            settings_state_.editing = true;
        }
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const bool choice = row.control == Control::choice &&
        (row.id == Id::companion_model || row.id == Id::companion_photos || row.id == Id::companion_user_gender ||
         row.id == Id::companion_provider || row.id == Id::companion_voice || row.id == Id::companion_voice_model || row.id == Id::companion_voice_style ||
         row.id == Id::companion_city || row.id == Id::companion_city_own);
    if (choice && !settings_state_.editing) { settings_state_.choice = row.choice; settings_state_.editing = true; }
    else if (choice) {
        if (row.id == Id::companion_city || row.id == Id::companion_city_own) {
            const auto& list = companion::cities();
            const bool own = row.id == Id::companion_city_own;
            const int index = settings_state_.choice - (own ? 2 : 1); // Before the cities: as all / her own (each her own).
            const std::string city = index >= 0 && index < static_cast<int>(list.size()) ? list[static_cast<size_t>(index)].key : std::string{};
            if (!own) settings_.companion_city = city;
            else {
                const auto key = std::string(companion::lookKey(companionLook()));
                if (settings_state_.choice == 0) settings_.companion_cities.erase(key);
                else settings_.companion_cities[key] = city.empty() ? std::string(companion::own_home) : city;
            }
            companionApplyHomes();
        }
        else if (row.id == Id::companion_model) {
            const auto provider = companionAccess().provider;
            const auto choices = companionModelChoices(provider);
            if (settings_state_.choice >= 0 && settings_state_.choice < static_cast<int>(choices.size())) {
                const std::string id = choices[static_cast<size_t>(settings_state_.choice)].id;
                (provider == companion::Provider::claude ? settings_.companion_claude_model :
                 provider == companion::Provider::xai ? settings_.companion_xai_model : settings_.companion_model) = id;
            }
            companion_.model_label.clear();
        }
        else if (row.id == Id::companion_photos)
            settings_.companion_photos = static_cast<AppSettings::CompanionPhotos>(std::clamp(settings_state_.choice, 0, 1));
        else if (row.id == Id::companion_user_gender) {
            settings_.companion_user_gender = static_cast<AppSettings::CompanionUserGender>(std::clamp(settings_state_.choice, 0, 2));
            companionSyncPerson();
        }
        else if (row.id == Id::companion_provider) {
            settings_.companion_provider = static_cast<AppSettings::CompanionProvider>(std::clamp(settings_state_.choice, 0, companion::provider_count - 1));
            companion_.status.clear(); companion_.model_label.clear(); // The next answer tells how the new one does.
        }
        else if (row.id == Id::companion_voice) {
            settings_.companion_voice = static_cast<AppSettings::CompanionVoice>(std::clamp(settings_state_.choice, 0, 2));
            if (settings_.companion_voice == AppSettings::CompanionVoice::off) companionHush();
            else if (companion_.voice.engine != CompanionVoice::Engine::ready) requestCompanionVoiceStatus();
        }
        else if (row.id == Id::companion_voice_model) {
            const int index = settings_state_.choice - 1; // 0: each one's own.
            if (index < 0) settings_.companion_voice_model.clear();
            else if (index < static_cast<int>(elevenlabs::models.size())) settings_.companion_voice_model = elevenlabs::models[static_cast<size_t>(index)].id;
            companionHush();
        }
        else {
            const auto& voices = companion_.voice.voices;
            const int index = settings_state_.choice - 1; // 0: automatic, her character.txt's.
            const auto key = std::string(companion::lookKey(companionLook()));
            if (index < 0) settings_.companion_voice_ids.erase(key);
            else if (index < static_cast<int>(voices.size())) settings_.companion_voice_ids[key] = voices[static_cast<size_t>(index)].id;
            companion_.voice.spoke.erase(static_cast<int>(companionLook()));
            companionHush();
        }
        settings_state_.editing = false;
        settingsChanged();
    }
    else if (row.control == Control::avatar) { // Enter: the next companion that can be chosen.
        const auto available = companion::availableLooks();
        const auto current = std::find(available.begin(), available.end(), companionLook());
        if (available.size() < 2) { navigationBoundary(); return true; }
        const auto next = current == available.end() || current + 1 == available.end() ? available.front() : *(current + 1);
        settings_.companion_look = std::string(companion::lookKey(next));
        settingsChanged();
    }
    else if (row.id == Id::companion) {
        settings_.companion = !settings_.companion;
        if (!settings_.companion) companionReset();
        companionRemoteSync();
        // Metro and XMB rebuild their home: her tile joins or leaves the layout's own.
        settingsChanged(homeWidgets() == home_widgets::Placement::tiles); audio_.context(); return true;
    }
    else if (row.id == Id::companion_remote) {
        settings_.companion_remote = !settings_.companion_remote;
        companionRemoteSync();
        settingsChanged(); audio_.context(); return true;
    }
    else if (row.id == Id::companion_memory || row.id == Id::companion_notes || row.id == Id::companion_semantic_memory || row.id == Id::companion_model_info || row.id == Id::companion_guests ||
             row.id == Id::companion_gamification || row.id == Id::companion_typing || row.id == Id::companion_weather) {
        auto& flag = row.id == Id::companion_semantic_memory ? settings_.companion_semantic_memory : row.id == Id::companion_memory ? settings_.companion_memory : row.id == Id::companion_notes ? settings_.companion_notes :
                     row.id == Id::companion_guests ? settings_.companion_guests : row.id == Id::companion_gamification ? settings_.companion_gamification :
                     row.id == Id::companion_typing ? settings_.companion_typing : row.id == Id::companion_weather ? settings_.companion_weather :
                     settings_.companion_model_info;
        flag = !flag;
        companionSemantic(false);
        settingsChanged(); audio_.context(); return true;
    }
    else return false;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

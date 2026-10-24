#include "companion_weather.hpp"
#include "json.hpp"
#include "url_encoding.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace companion::weather {
namespace {
std::wstring whole(double value) { return std::to_wstring(static_cast<int>(std::lround(value))); }
double numberOf(const json::Value* parent, const char* name, double fallback = 0) {
    const auto* v = parent ? parent->get(name) : nullptr;
    return v && v->type == json::Value::Type::number ? v->numeric : fallback;
}
// The n-th number of a daily array.
double dailyOf(const json::Value* daily, const char* name, size_t index, double fallback = 0) {
    const auto* v = daily ? daily->get(name) : nullptr;
    if (!v || v->type != json::Value::Type::array || index >= v->elements.size() || v->elements[index].type != json::Value::Type::number) return fallback;
    return v->elements[index].numeric;
}
bool dailyHas(const json::Value* daily, const char* name, size_t index) {
    const auto* v = daily ? daily->get(name) : nullptr;
    return v && v->type == json::Value::Type::array && index < v->elements.size() && v->elements[index].type == json::Value::Type::number;
}
} // namespace

std::wstring homeCity(const Character& character) {
    const auto& home = !character.home_en.empty() ? character.home_en : character.home_pl;
    const auto end = home.find_first_of(L",-–—(;:");
    std::wstring city = home.substr(0, end == std::wstring::npos ? home.size() : end);
    while (!city.empty() && city.back() == L' ') city.pop_back();
    while (!city.empty() && city.front() == L' ') city.erase(0, 1);
    return city;
}

std::wstring geocodeUrl(std::wstring_view city, bool english) {
    // The place's name comes back in the interface's language ("Kraków", "Tokio").
    return L"https://geocoding-api.open-meteo.com/v1/search?name=" + url_encoding::component(city) + L"&count=1&language=" +
           (english ? L"en" : L"pl") + L"&format=json";
}

std::wstring forecastUrl(double lat, double lon) {
    // A dot as the decimal separator whatever the process locale (to_chars ignores it).
    const auto fixed = [](double value) {
        char buffer[32];
        const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 4).ptr;
        return std::wstring(buffer, end);
    };
    return L"https://api.open-meteo.com/v1/forecast?latitude=" + fixed(lat) + L"&longitude=" + fixed(lon) +
           L"&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,weather_code,wind_speed_10m,is_day"
           L"&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum&timezone=auto&forecast_days=2";
}

std::optional<Place> parsePlace(std::string_view text, bool english) {
    const auto root = json::parse(text, 256 * 1024);
    if (!root) return std::nullopt;
    const auto* results = root->get("results");
    if (!results || results->type != json::Value::Type::array || results->elements.empty()) return std::nullopt;
    const auto& first = results->elements.front();
    const auto* lat = first.get("latitude");
    const auto* lon = first.get("longitude");
    if (!lat || !lon || lat->type != json::Value::Type::number || lon->type != json::Value::Type::number) return std::nullopt;
    Place place;
    place.lat = lat->numeric;
    place.lon = lon->numeric;
    place.name = text_encoding::wideLenient(first.get("name") ? first.get("name")->stringOr() : std::string{});
    place.country = text_encoding::wideLenient(first.get("country") ? first.get("country")->stringOr() : std::string{});
    (void)english;
    if (std::abs(place.lat) > 90 || std::abs(place.lon) > 180) return std::nullopt;
    return place;
}

std::optional<Now> parseForecast(std::string_view text) {
    const auto root = json::parse(text, 512 * 1024);
    if (!root) return std::nullopt;
    const auto* current = root->get("current");
    if (!current || current->type != json::Value::Type::object || !current->get("temperature_2m")) return std::nullopt;
    Now now;
    now.temperature = numberOf(current, "temperature_2m");
    now.feels_like = numberOf(current, "apparent_temperature", now.temperature);
    now.humidity = static_cast<int>(numberOf(current, "relative_humidity_2m"));
    now.precipitation = numberOf(current, "precipitation");
    now.code = static_cast<int>(numberOf(current, "weather_code"));
    now.wind = numberOf(current, "wind_speed_10m");
    now.day = numberOf(current, "is_day", 1) != 0;
    const auto* daily = root->get("daily");
    now.today_min = dailyOf(daily, "temperature_2m_min", 0, now.temperature);
    now.today_max = dailyOf(daily, "temperature_2m_max", 0, now.temperature);
    if (dailyHas(daily, "temperature_2m_max", 1)) {
        now.tomorrow = true;
        now.tomorrow_min = dailyOf(daily, "temperature_2m_min", 1);
        now.tomorrow_max = dailyOf(daily, "temperature_2m_max", 1);
        now.tomorrow_code = static_cast<int>(dailyOf(daily, "weather_code", 1));
        now.tomorrow_precipitation = dailyOf(daily, "precipitation_sum", 1);
    }
    return now;
}

std::wstring describe(int code, bool english) {
    struct Words { int code; const wchar_t* pl; const wchar_t* en; };
    static constexpr Words words[]{
        {0, L"bezchmurnie", L"clear sky"}, {1, L"przeważnie pogodnie", L"mainly clear"}, {2, L"częściowe zachmurzenie", L"partly cloudy"},
        {3, L"pochmurno", L"overcast"}, {45, L"mgła", L"fog"}, {48, L"mgła z szadzią", L"freezing fog"},
        {51, L"lekka mżawka", L"light drizzle"}, {53, L"mżawka", L"drizzle"}, {55, L"gęsta mżawka", L"dense drizzle"},
        {56, L"marznąca mżawka", L"freezing drizzle"}, {57, L"gęsta marznąca mżawka", L"dense freezing drizzle"},
        {61, L"lekki deszcz", L"light rain"}, {63, L"deszcz", L"rain"}, {65, L"ulewny deszcz", L"heavy rain"},
        {66, L"marznący deszcz", L"freezing rain"}, {67, L"silny marznący deszcz", L"heavy freezing rain"},
        {71, L"lekki śnieg", L"light snow"}, {73, L"śnieg", L"snow"}, {75, L"intensywny śnieg", L"heavy snow"}, {77, L"ziarna śniegu", L"snow grains"},
        {80, L"przelotne opady deszczu", L"rain showers"}, {81, L"przelotne ulewy", L"heavy rain showers"}, {82, L"gwałtowne ulewy", L"violent rain showers"},
        {85, L"przelotny śnieg", L"snow showers"}, {86, L"intensywne opady śniegu", L"heavy snow showers"},
        {95, L"burza", L"thunderstorm"}, {96, L"burza z gradem", L"thunderstorm with hail"}, {99, L"silna burza z gradem", L"severe thunderstorm with hail"}};
    for (const auto& w : words)
        if (w.code == code) return english ? w.en : w.pl;
    return english ? L"changeable" : L"zmiennie";
}

std::wstring text(const Report& report, bool english) {
    if (!report.at) return {};
    const auto& n = report.now;
    const std::wstring where = report.place.name.empty() ? report.asked : report.place.name;
    std::wstring out = english
        ? (report.travelling ? L"\nThe real weather where you are on your trip (" : L"\nThe real weather where you are (") + where + L", just checked): "
        : (report.travelling ? L"\nPrawdziwa pogoda tam, gdzie jesteś na wyjeździe (" : L"\nPrawdziwa pogoda tam, gdzie jesteś (") + where + L", świeżo sprawdzona): ";
    out += (english ? L"now " : L"teraz ") + whole(n.temperature) + L"°C";
    if (std::abs(n.feels_like - n.temperature) >= 3) out += (english ? L" (feels like " : L" (odczuwalnie ") + whole(n.feels_like) + L"°C)";
    out += L", " + describe(n.code, english);
    if (!n.day) out += english ? L", night" : L", noc";
    if (n.wind >= 25) out += (english ? L", wind " : L", wiatr ") + whole(n.wind) + L" km/h";
    out += english ? L"; today " + whole(n.today_min) + L"–" + whole(n.today_max) + L"°C" : L"; dziś " + whole(n.today_min) + L"–" + whole(n.today_max) + L"°C";
    if (n.tomorrow)
        out += (english ? L"; tomorrow " : L"; jutro ") + describe(n.tomorrow_code, english) + L", " + whole(n.tomorrow_min) + L"–" + whole(n.tomorrow_max) + L"°C";
    out += english ? L". Speak of the weather only as it really is (never invent other weather or old clichés about your city), and only when it fits — the way a "
                     L"person mentions it (the rain on the window, the cold on the way home), not like a forecast."
                   : L". O pogodzie mów tylko zgodnie z tym (nie wymyślaj innej ani stereotypów o swoim mieście) i tylko, gdy pasuje — tak, jak wspomina ją człowiek "
                     L"(deszcz za oknem, zimno w drodze do domu), a nie jak prognoza.";
    return out;
}

std::wstring notable(const Report& report, bool english) {
    if (!report.at) return {};
    const auto& n = report.now;
    std::wstring what;
    if (n.code >= 95) what = describe(n.code, english);
    else if (n.code == 65 || n.code == 67 || n.code == 82) what = describe(n.code, english);
    else if (n.code == 75 || n.code == 86) what = describe(n.code, english);
    else if (n.code >= 71 && n.code <= 77) what = english ? L"it's snowing" : L"pada śnieg";
    else if (n.code == 45 || n.code == 48) what = english ? L"thick fog" : L"gęsta mgła";
    else if (n.temperature >= 30) what = english ? L"a heatwave, " + whole(n.temperature) + L"°C" : L"upał, " + whole(n.temperature) + L"°C";
    else if (n.temperature <= -8) what = english ? L"a hard frost, " + whole(n.temperature) + L"°C" : L"siarczysty mróz, " + whole(n.temperature) + L"°C";
    else if (n.wind >= 50) what = english ? L"a gale, " + whole(n.wind) + L" km/h" : L"wichura, " + whole(n.wind) + L" km/h";
    if (what.empty()) return {};
    return english ? L"Outside your window right now: " + what + L" — it's on your mind; you may mention it in your own way."
                   : L"Za twoim oknem właśnie: " + what + L" — siedzi ci to w głowie; możesz o tym po swojemu wspomnieć.";
}

std::optional<Report> fetch(const Get& get, std::wstring_view place, std::optional<std::pair<double, double>> coordinates, bool english, uint64_t now) {
    if (!get || (place.empty() && !coordinates)) return std::nullopt;
    Report report;
    report.asked = std::wstring(place);
    if (coordinates) {
        report.place.lat = coordinates->first;
        report.place.lon = coordinates->second;
        report.place.name = std::wstring(place);
    } else {
        const auto found = get(geocodeUrl(place, english));
        if (found.status != 200) return std::nullopt;
        const auto parsed = parsePlace(std::string_view(reinterpret_cast<const char*>(found.body.data()), found.body.size()), english);
        if (!parsed) return std::nullopt;
        report.place = *parsed;
    }
    const auto forecast = get(forecastUrl(report.place.lat, report.place.lon));
    if (forecast.status != 200) return std::nullopt;
    const auto parsed = parseForecast(std::string_view(reinterpret_cast<const char*>(forecast.body.data()), forecast.body.size()));
    if (!parsed) return std::nullopt;
    report.now = *parsed;
    report.at = now;
    return report;
}
} // namespace companion::weather

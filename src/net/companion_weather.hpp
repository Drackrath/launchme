#pragma once
// The real weather where she is (Open-Meteo: free, no API key; attribution "Weather data by
// Open-Meteo.com", CC BY 4.0): her home city from character.txt (location=lat,lon, else the city
// that starts home_en), or the town of a trip she is on (companion_mind.hpp travelPlace). Fetched
// in the background (src/app/companion_weather.cpp), never before her answer; only her city's
// name or coordinates leave this machine. Her prompt gets it worded, so she never makes up other
// weather, and notable weather (a storm, heavy snow, a heatwave) may come up on its own.
#include "companion_characters.hpp"
#include "http.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace companion::weather {
struct Place {
    std::wstring name;    // As Open-Meteo names it ("Kraków").
    std::wstring country; // "Polska" / "Poland".
    double lat = 0, lon = 0;
};
struct Now {
    double temperature = 0, feels_like = 0, wind = 0, precipitation = 0; // °C, °C, km/h, mm.
    int humidity = 0, code = 0;          // %; WMO weather code.
    bool day = true;
    double today_min = 0, today_max = 0, tomorrow_min = 0, tomorrow_max = 0, tomorrow_precipitation = 0;
    int tomorrow_code = 0;
    bool tomorrow = false;               // The forecast had a second day.
};
struct Report {
    Place place;
    Now now;
    std::wstring asked;   // The place as asked for (her city, or her trip's town): the cache key.
    bool travelling = false;
    uint64_t at = 0;      // Unix seconds when fetched.
};

// Her home city to look up: "Kraków, Kazimierz - …" → "Kraków" (home_en first, else home_pl).
std::wstring homeCity(const Character& character);
std::wstring geocodeUrl(std::wstring_view city, bool english);
std::wstring forecastUrl(double lat, double lon);
std::optional<Place> parsePlace(std::string_view json, bool english);
std::optional<Now> parseForecast(std::string_view json);
// A WMO weather code in words ("mżawka", "burza z gradem").
std::wstring describe(int code, bool english);
// Her prompt's line: the real weather there, checked at her local time (empty: no report).
std::wstring text(const Report& report, bool english);
// Weather worth a word by itself (a storm, heavy rain or snow, fog, a heatwave, frost, a gale);
// empty otherwise.
std::wstring notable(const Report& report, bool english);
// Looks the place up (or uses `coordinates`) and fetches its weather: nullopt when either fails.
using Get = std::function<HttpResponse(const std::wstring& url)>;
std::optional<Report> fetch(const Get& get, std::wstring_view place, std::optional<std::pair<double, double>> coordinates, bool english, uint64_t now);
} // namespace companion::weather

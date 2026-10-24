// Drawer dashboard temperatures: unit conversions, aggregation and "no sensor" handling,
// plus a smoke read of this machine's real sensors (values may legitimately be absent).
#include "../src/platform/sensors.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string>

int main() {
    using sensors::celsiusFromDeci;
    using sensors::celsiusFromKelvin;
    // Thermal zones report Kelvin; rounded to whole degrees.
    if (celsiusFromKelvin(300.15) != 27 || celsiusFromKelvin(320.0) != 47 || celsiusFromKelvin(330.4) != 57 ||
        celsiusFromKelvin(372.15) != 99 || celsiusFromKelvin(353.0) != 80) return 1;
    // Nothing usable: zero, negative, non-finite, a desktop firmware placeholder (290 K = 17 °C) or absurd heat.
    if (celsiusFromKelvin(0) || celsiusFromKelvin(-5) || celsiusFromKelvin(std::numeric_limits<double>::quiet_NaN()) ||
        celsiusFromKelvin(std::numeric_limits<double>::infinity()) || celsiusFromKelvin(290.0) ||
        celsiusFromKelvin(273.15 + sensors::cpu_plausible_min - 1) || !celsiusFromKelvin(273.15 + sensors::cpu_plausible_min) ||
        celsiusFromKelvin(273.15 + sensors::plausible_max + 1) || celsiusFromKelvin(700)) return 2;
    // Adapter perf data reports tenths of °C; 0 = the driver does not report a temperature.
    if (celsiusFromDeci(545) != 55 || celsiusFromDeci(544) != 54 || celsiusFromDeci(371) != 37 ||
        celsiusFromDeci(1) != 0 || celsiusFromDeci(1300) != 130) return 3;
    if (celsiusFromDeci(0) || celsiusFromDeci(1306) || celsiusFromDeci(UINT64_MAX)) return 4;
    // Hottest usable sensor wins; unusable ones never hide a real reading.
    const std::array<std::optional<int>, 4> zones{std::nullopt, 41, 63, std::nullopt};
    const std::array<std::optional<int>, 2> none{};
    const std::array<std::optional<int>, 3> adapters{celsiusFromDeci(0), celsiusFromDeci(655), celsiusFromDeci(482)};
    if (sensors::hottest(zones) != 63 || sensors::hottest(none) || sensors::hottest(std::span<const std::optional<int>>{}) ||
        sensors::hottest(adapters) != 66) return 5;
    // Display: whole degrees or an em dash.
    if (sensors::format(54) != L"54 \u00B0C" || sensors::format(std::nullopt) != L"\u2014") return 6;
    // Readings compare by value (the UI repaints only on change).
    const sensors::Reading a{true, 50, std::nullopt};
    auto b = a;
    if (!(a == b) || sensors::Reading{} == a) return 7;
    b.gpu = 60;
    if (a == b) return 7;

    // The real reader must never throw or report implausible values; missing sensors are fine.
    sensors::Reader reader;
    for (int poll = 0; poll < 2; ++poll) {
        const auto reading = reader.read();
        if (!reading.sampled) return 8;
        for (const auto& value : {reading.cpu, reading.gpu})
            if (value && (*value < 0 || *value > sensors::plausible_max)) return 9;
        std::cout << "CPU " << (reading.cpu ? std::to_string(*reading.cpu) + " C" : "no sensor")
                  << ", GPU " << (reading.gpu ? std::to_string(*reading.gpu) + " C" : "no sensor") << '\n';
    }
    return 0;
}

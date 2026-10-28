// Linux side of power.hpp: systemd-logind (org.freedesktop.login1, also elogind) over the system
// bus with GDBus, and the battery from /sys/class/power_supply. power.cpp is the Windows one.
#include "power.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <string>

namespace {
// One login1.Manager call; the reply (to unref), or null when logind or the bus is missing or refuses.
GVariant* logind(const char* method, GVariant* parameters, const GVariantType* reply, int timeout_ms) {
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        g_clear_error(&error);
        if (parameters) g_variant_unref(g_variant_ref_sink(parameters));
        return nullptr;
    }
    GVariant* result = g_dbus_connection_call_sync(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", method, parameters, reply, G_DBUS_CALL_FLAGS_NONE, timeout_ms, nullptr, &error);
    g_clear_error(&error);
    g_object_unref(bus);
    return result;
}

// "yes", "no", "challenge", "na"; empty when logind does not answer.
std::string can(const char* method) {
    GVariant* reply = logind(method, nullptr, G_VARIANT_TYPE("(s)"), 2000);
    if (!reply) return {};
    const char* answer = nullptr;
    g_variant_get(reply, "(&s)", &answer);
    std::string result = answer ? answer : "";
    g_variant_unref(reply);
    return result;
}

std::string firstLine(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::getline(input, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}
std::optional<uint64_t> number(const std::filesystem::path& path) {
    const auto text = firstLine(path);
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data()) return std::nullopt;
    return value;
}
} // namespace

bool logindAllows(std::string_view answer) { return answer == "yes" || answer == "challenge"; }

std::vector<PowerAction> availablePowerActions() {
    std::vector<PowerAction> actions;
    // As on Windows: without an answer, sleep, restart and shutdown are still offered (acting then fails).
    const auto offered = [](const std::string& answer) { return answer.empty() || logindAllows(answer); };
    if (offered(can("CanSuspend"))) actions.push_back(PowerAction::sleep);
    if (logindAllows(can("CanHibernate"))) actions.push_back(PowerAction::hibernate);
    if (offered(can("CanReboot"))) actions.push_back(PowerAction::restart);
    if (offered(can("CanPowerOff"))) actions.push_back(PowerAction::shutdown);
    return actions;
}

bool performPowerAction(PowerAction action) {
    const char* method = nullptr;
    switch (action) {
    case PowerAction::sleep: method = "Suspend"; break;
    case PowerAction::hibernate: method = "Hibernate"; break;
    case PowerAction::restart: method = "Reboot"; break;
    case PowerAction::shutdown: method = "PowerOff"; break;
    }
    if (!method) return false;
    // interactive=true: polkit may ask for a password (shared machines, remote sessions).
    GVariant* reply = logind(method, g_variant_new("(b)", TRUE), nullptr, 30000);
    if (!reply) return false;
    g_variant_unref(reply);
    return true;
}

std::optional<BatteryStatus> batteryStatus(const std::filesystem::path& power_supply) {
    // Several batteries (some ThinkPads) are weighted by their energy (or charge) when every one
    // reports it, else their capacity percentages are averaged.
    double now = 0, full = 0, capacity = 0;
    int batteries = 0, weighted = 0;
    bool charging = false;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(power_supply, error), end; !error && entry != end; entry.increment(error)) {
        const auto& path = entry->path();
        // scope "Device": a peripheral's battery (mouse, gamepad, headset), not the system's.
        if (firstLine(path / "type") != "Battery" || firstLine(path / "scope") == "Device" || number(path / "present") == 0u) continue;
        const auto percent = number(path / "capacity");
        auto energy_now = number(path / "energy_now"), energy_full = number(path / "energy_full");
        if (!energy_now || !energy_full) { energy_now = number(path / "charge_now"); energy_full = number(path / "charge_full"); }
        if (!percent && !(energy_now && energy_full && *energy_full)) continue;
        ++batteries;
        if (energy_now && energy_full && *energy_full) { ++weighted; now += static_cast<double>(*energy_now); full += static_cast<double>(*energy_full); }
        capacity += percent ? static_cast<double>(*percent) : 100.0 * static_cast<double>(*energy_now) / static_cast<double>(*energy_full);
        charging = charging || firstLine(path / "status") == "Charging";
    }
    if (!batteries) return std::nullopt;
    const double percent = weighted == batteries ? 100.0 * now / full : capacity / batteries;
    return BatteryStatus{static_cast<int>(std::clamp(std::lround(percent), 0L, 100L)), charging};
}

std::optional<BatteryStatus> batteryStatus() { return batteryStatus("/sys/class/power_supply"); }

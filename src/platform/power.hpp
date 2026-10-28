#pragma once
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

enum class PowerAction { sleep, hibernate, restart, shutdown };

// Actions this machine offers right now: sleep/hibernate follow the power
// capabilities (disabled S3, hibernation turned off), restart/shutdown always.
// On Linux systemd-logind (or elogind) answers (CanSuspend, CanHibernate, ...).
std::vector<PowerAction> availablePowerActions();
// Enables SE_SHUTDOWN_NAME for this process and asks Windows to act. Restart
// and shutdown go through the normal session end (WM_QUERYENDSESSION), so
// running apps and the launcher's own WM_ENDSESSION handling still apply.
// On Linux logind acts (Suspend, Hibernate, Reboot, PowerOff), honouring other
// apps' inhibitors; polkit may ask for a password.
bool performPowerAction(PowerAction action);

// The system battery's charge (percent) and whether it is charging; empty on a machine without
// one. Peripheral batteries (mice, gamepads) are not counted. Linux: /sys/class/power_supply.
struct BatteryStatus {
    int percent = 0;
    bool charging = false;
    bool operator==(const BatteryStatus&) const = default;
};
std::optional<BatteryStatus> batteryStatus();

// Linux, pure for tests: whether logind's Can<Action> answer offers the action ("yes", or
// "challenge" when polkit will ask), and the battery under a /sys/class/power_supply folder.
bool logindAllows(std::string_view answer);
std::optional<BatteryStatus> batteryStatus(const std::filesystem::path& power_supply);

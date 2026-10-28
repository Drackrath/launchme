#include "power.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <powrprof.h>
#endif

namespace {
bool enableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool found = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid);
    const bool adjusted = found && AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr) &&
                          GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return adjusted;
}
}

std::vector<PowerAction> availablePowerActions() {
    std::vector<PowerAction> actions;
    SYSTEM_POWER_CAPABILITIES capabilities{};
    const bool known = GetPwrCapabilities(&capabilities);
    // Modern Standby (S0 low power idle) machines sleep through SetSuspendState too.
    if (!known || capabilities.SystemS3 || capabilities.AoAc) actions.push_back(PowerAction::sleep);
    if (known && capabilities.SystemS4 && capabilities.HiberFilePresent) actions.push_back(PowerAction::hibernate);
    actions.push_back(PowerAction::restart);
    actions.push_back(PowerAction::shutdown);
    return actions;
}

bool performPowerAction(PowerAction action) {
    if (!enableShutdownPrivilege()) return false;
    constexpr DWORD reason = SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED;
    switch (action) {
    case PowerAction::sleep: return SetSuspendState(FALSE, FALSE, FALSE) != FALSE;
    case PowerAction::hibernate: return SetSuspendState(TRUE, FALSE, FALSE) != FALSE;
    // Hybrid shutdown matches the Start menu (fast startup) instead of a full cold boot.
    case PowerAction::shutdown: return InitiateShutdownW(nullptr, nullptr, 0, SHUTDOWN_POWEROFF | SHUTDOWN_HYBRID, reason) == ERROR_SUCCESS;
    case PowerAction::restart: return InitiateShutdownW(nullptr, nullptr, 0, SHUTDOWN_RESTART, reason) == ERROR_SUCCESS;
    }
    return false;
}

// The drawer clock's battery test (BatteryFlag 128: no system battery; 255: unknown).
std::optional<BatteryStatus> batteryStatus() {
    SYSTEM_POWER_STATUS power{};
    if (!GetSystemPowerStatus(&power) || power.BatteryFlag == 255 || (power.BatteryFlag & 128) || power.BatteryLifePercent > 100)
        return std::nullopt;
    return BatteryStatus{power.BatteryLifePercent, (power.BatteryFlag & 8) != 0};
}

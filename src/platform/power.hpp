#pragma once
#include <vector>

enum class PowerAction { sleep, hibernate, restart, shutdown };

// Actions this machine offers right now: sleep/hibernate follow the power
// capabilities (disabled S3, hibernation turned off), restart/shutdown always.
std::vector<PowerAction> availablePowerActions();
// Enables SE_SHUTDOWN_NAME for this process and asks Windows to act. Restart
// and shutdown go through the normal session end (WM_QUERYENDSESSION), so
// running apps and the launcher's own WM_ENDSESSION handling still apply.
bool performPowerAction(PowerAction action);

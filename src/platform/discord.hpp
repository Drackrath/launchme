#pragma once

#include "../model/game.hpp"

#include <optional>

// Discord tile of the drawer dashboard: starts or focuses the desktop client only.
// No Discord API or RPC client. Safe to call from any thread.
namespace discord {
// True while a Discord desktop client process (stable, PTB or Canary) runs.
bool running();
// Brings a visible top-level Discord window to the foreground; false when there is
// none (not running, or minimised to the tray).
bool focus();
// How to start the client: its Squirrel Update.exe --processStart under %LOCALAPPDATA%
// (Discord, DiscordPTB, DiscordCanary), else the registered discord:// protocol;
// empty when Discord is not installed.
std::optional<GameAction> launchAction();
} // namespace discord

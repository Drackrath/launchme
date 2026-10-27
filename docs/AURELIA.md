# Aurelia integration (Windows)

An optional, out-of-process integration with [Drackrath/Aurelia](https://github.com/Drackrath/Aurelia).
It is off by default and offered during first-run setup and in **Settings → System**.

**Primarily for single-player / offline games. Some games, especially multiplayer titles and those
using DRM or Steamworks, may require the Steam client to be running.** Availability of a standalone
launch depends on the game. The integration does not promise offline authentication or universal DRM compatibility.

## Setup

1. Open **Settings → System → Manage Aurelia**.
2. Choose **Install Aurelia**. Launchme selects `aurelia_windows_x86_64.exe` from the latest stable
   GitHub release, verifies its advertised SHA-256 and Windows x64 PE header, and publishes it atomically.
3. **Sign in using Aurelia's window**. Enter the password and any Steam Guard challenge in that console.
   Launchme does not read Aurelia's tokens or place passwords on command lines.
4. Enable **Extended Aurelia integration**, then close and reopen Launchme.

The executable lives in `%LOCALAPPDATA%\Launchme\helpers\aurelia\<release>\`. `current.txt` selects
the installed release. **Check and install update** follows the same validation path. An update uses a
new directory; the current session keeps its original executable until restart. A failed update retains
the previous selection. Binaries are downloaded on demand, not bundled with Launchme.

`AURELIA_CONFIG_DIR` is supplied only to the child process and points to `%LOCALAPPDATA%\Launchme\aurelia`.
This session is separate from standalone Aurelia and from the Steam client's remembered accounts.
The launcher never changes its own process environment to select that session.

## Games and downloads

When enabled, Steam's owned library, game metadata, achievements, install operations and friends
use Aurelia's JSON CLI. Other stores continue using their own adapters. Artwork, news, ratings and
other independent services keep their existing providers. Matching-account local Steam visibility,
collections and non-Steam shortcuts can supplement the library; no other account's local rules are imported.

Open a game's context menu → **Manage · Aurelia** in desktop or TV mode:

- Install into a selected library, including a manually entered absolute library path.
- Update, verify, uninstall after confirmation, or move the installation to another library.
- Follow transfers in Launchme's existing **Downloads** page, with bytes/progress and, when emitted by
  Aurelia, speed and ETA. Open a transfer to cancel it. Closing its panel leaves the transfer running.
- **Launch without Steam** is off by default per game. Off uses the normal Steam client launch path.
  On launches through Aurelia without `--steam`, for a compatible standalone game. The Aurelia path
  uses `--no-update` so launching cannot silently replace a deliberately selected build; updates are
  explicit actions. The global integration alone never reroutes an installed game's launch.

Aurelia's daemon owns background transfers. Closing Launchme detaches its CLI processes; it does not
terminate Steam or running games. Reopening the launcher discovers active transfers through `install list`.
Pause/resume is not exposed because this CLI currently exposes cancellation, not a pause operation.

## Cloud saves

The desktop details pane and TV game-details page show the cloud status. **Saves available** means files
were enumerated, not that local and remote saves match. **Synchronized** is shown only after a complete
successful sync. Conflicts, skipped paths, transfer failures and unknown state stay distinct.

**Cloud saves → Sync saves** uses Aurelia's conflict-detecting default. No conflict winner is selected
automatically. **Use cloud saves** or **Use local saves** requires confirmation. File/save operations are
blocked while the launcher knows that the game is running. Synchronization is an explicit action;
listing cloud status neither uploads nor overwrites saves.

## DLC, versions and Workshop

- **Manage DLC** lists ownership, installation and enabled state. Only owned DLC with known state can
  be installed/enabled/disabled. A running Steam client may need restarting to notice a DLC change.
- **Versions and pinning** pins the current manifests or releases the pin. Pinning controls Aurelia's
  updates; it cannot override the official Steam client's update policy.
- Enter `depot_id manifest_id` to downgrade and pin that depot. IDs are kept as exact 64-bit integers.
  The operation requires confirmation. Steam only exposes current manifest IDs; historical IDs must
  be obtained separately, for example from SteamDB. Multi-depot games may require more than one operation.
- **Beta branches** changes the selected branch; explicitly update afterward to fetch its files.
- **Steam Workshop** lists browse results and lets you subscribe and install. The ID field also accepts
  an item/collection ID to install, `+ID` to subscribe/install, or `-ID` to unsubscribe. Unsubscribing is
  not an uninstall. Workshop pagination and the upstream inventory/market/community-writing features
  are not presented in this UI.

## Friends and chat

The existing friends drawer uses Aurelia's roster. Selecting a friend opens a native conversation with
recent history, incoming messages, echoes from other clients and typing notifications. Messages are sent
as a separate bounded command; a failed send retains the draft. **F5** reconnects a dropped conversation.
Closing a conversation cancels its listener; late responses cannot change another conversation.

Keyboard: arrows, Enter, Esc, Tab for the text field, Ctrl+V, PgUp/PgDn. Gamepad: D-pad, A/B, X for the
on-screen keyboard, Y to refresh, LB/RB to scroll. Long messages scroll within the conversation.

## Implementation and verification

Contract reviewed against Aurelia **v0.1.40** (`a47e90bf42aa9f99818bd8d0de286a5c33da9b44`),
including the JSON command handlers and NDJSON progress/chat events. The installer deliberately selects
the latest stable release; incompatible responses are reported rather than guessed.

- `src/store/aurelia*`: typed requests/parsers, Win32 child-process transport, release installer.
- `src/app/aurelia*`: workers, account epochs, operation ownership, native pages and input.
- `src/ui/aurelia*`: shared desktop/TV presentation and geometry.
- `aurelia_tests`: parser/64-bit identity/installer tests and a real child-process fixture for output,
  errors, streaming and cancellation. No account or external service.
- `launcher_ui_tests --aurelia`: injected end-to-end UI flows and optional PNG captures through
  `LAUNCHME_UI_CAPTURE_DIR`.

Aurelia remains a separately downloaded GPL-3.0 program. Launchme's adapter communicates through its
public CLI and contains no copied Aurelia implementation. Steam and Valve trademarks belong to Valve;
Aurelia is an independent project.

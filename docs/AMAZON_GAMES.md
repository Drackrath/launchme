# Amazon Games (Windows)

Amazon Games is one of the PC platforms in **Settings → Library**. It works through
[Nile](https://github.com/imLinguin/nile), an open-source (GPL-3.0) command-line client that is **not bundled**:
Launchme downloads it only when asked.

## Setup

1. Switch **Amazon Games** on in **Settings → Library**. Without Nile its page opens with **Nile → Download**
   first; going back is fine, the button stays on that page.
2. **Download** fetches `nile_windows_x86_64.exe` of the pinned release v1.2.0 from GitHub, checks its size and
   SHA-256 and stores it as `%LOCALAPPDATA%\Launchme\tools\nile-1.2.0.exe`. **Remove** deletes it again.
3. **Account → Connect** opens Amazon's sign-in page in the browser. After signing in, copy the whole address
   the browser lands on (it contains `openid.oa2.authorization_code`) and paste it into **Address after signing
   in** (Ctrl+V), then confirm. Nile registers this PC as a device and downloads the library.

Nile runs hidden with its own configuration in `%LOCALAPPDATA%\Launchme\amazon` (`NILE_CONFIG_PATH`), readable
only by the current user. It never uses the Amazon Games app's or another Nile's sign-in. Launchme reads Nile's
files (`current_user.json`, `library.json`, `installed.json`) for the library; tokens stay in Nile's own files.
**Account connection → Sign out** deregisters the device and removes the session; installed games stay.

## Games

- Account games appear under Amazon Games (shown when not installed with *Show uninstalled games*), with
  Amazon's art where SteamGridDB has none. **Library → Refresh** (or F5 on the page) syncs again.
- **Install** opens the install dialog: one game folder (default `%USERPROFILE%\Games\Amazon-<title>`), Nile
  downloads straight into it. Progress appears in **Downloads**; **Cancel download** stops it. The game counts
  as installed only when Nile recorded it in that folder and its `fuel.json` names an executable inside it.
- **Play** starts the game's executable from its `fuel.json` (arguments and working folder included) with the
  variables the Amazon Games SDK expects (`FUEL_DIR`, `AMAZON_GAMES_SDK_PATH`, the entitlement, SKU and display
  name), as `nile launch` does, so the session, play time and Close game work like for any other game.
- **Uninstall** asks first, then Nile deletes the game's files.

## Limits

- Nile does not run a game's `PostInstall` steps (redistributables); most games already ship them or the
  system has them.
- Online features of games that rely on Amazon's FuelPump service are not guaranteed (Nile's README).
- Game updates are not offered yet (Nile's `list-updates` / `update` are the next step).

## Implementation

- `src/store/amazon.*`: parsers (sign-in JSON, pasted address, library/installs, JSON5 `fuel.json`, progress),
  the pinned helper download and the account/install worker.
- `src/platform/helper_process.*`: hidden helper processes in a kill-on-close job.
- `src/app/amazon.cpp`, `src/app/platform_settings.cpp`: the platform page, sign-in, install dialog.
- `amazon_tests` (fixture files and a fake Nile) and `launcher_ui_tests --amazon` (settings, sign-in, install
  dialog, uninstall question and launch; PNGs with `LAUNCHME_UI_CAPTURE_DIR`). No real account or helper.

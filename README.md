# launchme

A full-screen, console-style game launcher for Windows 10/11, built for a gamepad.
It merges Steam (collections and non-Steam shortcuts), GOG, Battle.net, EA app, Ubisoft Connect,
Epic Games, Xbox (PC) and your own programs into one library, with artwork from SteamGridDB.

Native C++20: Win32, Direct2D/DirectWrite/WIC, WinHTTP, XAudio2 and Media Foundation — no HTML UI.
The interface is available in Polish and English (Settings → System → Language).

## Download

Grab `launchme-<version>-win64.zip` from [Releases](../../releases), unpack it anywhere and run
`launchme.exe`. No installer and no Visual C++ Redistributable are needed. The bundled plugin
(`plugin-packages/`) installs itself on first start; manage plugins in Settings → Plugins.

## Where settings and cache are stored

Everything launchme writes goes to **`%LOCALAPPDATA%\Launchme`** (e.g. `C:\Users\<you>\AppData\Local\Launchme`),
never next to the EXE:

| What | Where |
| --- | --- |
| Settings | `settings.ini` |
| API keys | `*_api_key.txt` (plain text) or `*.dat` (encrypted with Windows DPAPI for your account) |
| Store accounts (GOG, Epic, EA) | `accounts\` (DPAPI) |
| Artwork cache | `covers`, `heroes`, `logos`, `posters`, `landscapes`, `stores`, `artwork` |
| Metadata and ratings cache | `metadata`, `ratings`, `knowledge`, `glance` |
| Downloaded game videos | `media\` (folder can be changed in Settings → Media & data) |
| Glance browser profile | `browser\` |
| Logs and crash dumps | `launchme.log`, `crashes\` |

Cache folders can be deleted — they are rebuilt from disk or the network — except your manual choices
(`artwork\selections`, `*.manual-focus` crops, `metadata\*.rawg.json`). Set `LAUNCHME_CONFIG_DIR`
to an absolute path to use a different data folder (e.g. a portable or test profile).
Settings → System → Help → **Open folder** (the version row) opens it.

## Features

- **Home layouts**: shelves (default), XMB, Flix, Metro, PSfive and snitch, with a PS5-like tile
  entrance, frosted glass, button colour themes and several focus-outline styles.
- **Desktop window mode** with a sidebar, search and a details pane; switch with `F11` or from the power menu.
- **Library views**: grid (squares, posters or landscape tiles), list, cinematic, detailed, poster,
  coverflow and novelty cartridge/floppy views; sorting, filters, search and remembered positions.
- **Stores**: local installs of every store above, Steam collections synced with the running client,
  GOG and Epic account libraries (Epic installs through Legendary, no Epic launcher needed),
  EA and Battle.net libraries imported from their store pages, Ubisoft from the signed-in client.
- **Artwork**: SteamGridDB covers, heroes, logos and posters with OCR-assisted cropping and a variant browser.
- **Details**: Steam metadata, playtime (also for non-Steam games), achievements, Steam and RAWG ratings,
  versions of the same game across stores, uninstall from the game menu.
- **Glance**: a news hub (your games' Steam announcements, new and upcoming releases, articles) with a
  built-in WebView2 browser window driven by the pad.
- **AI companion** (optional): a chat character that knows your library, with an optional voice and photo feed
  — see [AI companion](#ai-companion).
- **Widgets drawer**: clock, Steam friends, Discord, CPU/GPU temperatures, controllers, audio and Bluetooth.
- **In-game Guide overlay** (experimental): back to the dashboard, close the game, optional FPS from PresentMon.
- **Plugins** (ZIP + native DLL): HowLongToBeat ships with the build (completion times in game details);
  write your own with the [Plugin SDK](docs/plugins/README.md).
- Background music, screensaver, start with Windows, single instance, crash dumps.

## Controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Navigate | arrows | D-pad, left stick |
| Play / select | `Enter`, `Space` | A |
| Back | `Esc`, `Backspace` | B |
| Game menu | hold `Enter`, right click | Y, hold A |
| On-screen keyboard / search | — | X |
| Launcher menu (tap) / power (hold) | `Home` | Guide / PS |
| Widgets drawer (tap) / audio panel (hold) | `Tab` | Start / Options |
| Glance | `G` | RT / R2 |
| Refresh artwork and view | `F5` | — |
| Performance overlay | `F8` | — |
| Full screen ↔ window | `F11` | — |
| Hidden and private games | `Ctrl+H` | — |

`↑` from the main row opens the bottom bar (Search, All games, plugins, Downloads, Controllers, Sound,
Widgets, Settings, Power). Supported pads: XInput, DualSense, DualShock 4 and generic HID pads.

## API keys (all optional, all free)

Nothing is needed to start; without a key only the related feature is off. Paste keys in
**Settings → Media & data → Service keys** (or set the environment variable, which wins over the file).

| Service | How to get the key | Used for |
| --- | --- | --- |
| **SteamGridDB** (recommended) | Sign in at [steamgriddb.com](https://www.steamgriddb.com) → Preferences → API → *Generate API Key* | covers, heroes, logos, posters, the artwork browser |
| **Steam Web API** | Sign in at [steamcommunity.com/dev/apikey](https://steamcommunity.com/dev/apikey), enter any domain (e.g. `localhost`) | Steam friends, names of uninstalled games, full achievement lists |
| **RAWG** | Create an account at [rawg.io/apidocs](https://rawg.io/apidocs) → *Get API key* | ratings and metadata for non-Steam games |
| **ScreenScraper** | Create an account at [screenscraper.fr](https://www.screenscraper.fr); API developer credentials (*devid* / *devpassword*) are requested from ScreenScraper on their forum | emulated games: ROM identification, descriptions, artwork, videos |

Files and variables: `steamgriddb_api_key.txt` / `STEAMGRIDDB_API_KEY`, `steam_web_api_key.txt` /
`STEAM_WEB_API_KEY`, `rawg_api_key.txt` / `RAWG_API_KEY`. ScreenScraper has its own page with the
developer and user login (**Settings → Media & data → ScreenScraper**, variables `SCREENSCRAPER_DEV_ID`,
`SCREENSCRAPER_DEV_PASSWORD`, `SCREENSCRAPER_SSID`, `SCREENSCRAPER_SSPASSWORD`); passwords are stored with DPAPI.

Store accounts (GOG, Epic, EA) connect in **Settings → Library → Platforms → ⚙** and are stored with DPAPI.
Steam collection sync needs the Steam client started with `-cef-enable-debugging`.

## AI companion

The companion is an optional chat character on the home screen (**Settings → Assistant**) that knows
your library and playtime. It needs a language model — the easiest is a **free Gemini API key**:

1. Open [aistudio.google.com/apikey](https://aistudio.google.com/apikey), sign in with a Google account
   and click *Create API key* (the free tier is enough for chatting).
2. In launchme: **Settings → Assistant** — Gemini is the default model provider — paste the key into
   **Gemini API key** (or set `GEMINI_API_KEY`).

Other providers: Claude (`ANTHROPIC_API_KEY`), Grok (`XAI_API_KEY`) or a local [Ollama](https://ollama.com)
server with no key. Optional voice uses an [ElevenLabs](https://elevenlabs.io) key (`ELEVENLABS_API_KEY`).
Keys typed in Settings are encrypted with DPAPI for your Windows account.

## Building

Requirements: Windows 10/11 x64, Visual Studio 2026 (18) with the C++ workload and Windows SDK,
Git and network access on the first configure (miniz, PresentMon and WebView2 SDK are fetched).
The build uses the CMake bundled with Visual Studio.

```powershell
.\tools\build.ps1          # Release build, runtime published to build\vs
.\tools\build.ps1 -Test    # also builds and runs every CTest test
.\build\vs\launchme.exe
```

Close a running launcher before rebuilding. The C++ runtime is linked statically. Tests never launch
games, store clients, installers or change system settings; `*_live_test.exe` and `tools/` probes are
manual (network, real accounts).

## License

[MIT](LICENSE). Third-party licenses (miniz, PresentMon, WebView2 SDK) ship in
`plugin-packages/licenses/`.

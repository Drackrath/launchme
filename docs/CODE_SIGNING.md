# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

Status: the project has applied to SignPath Foundation. Until it is accepted, releases are published unsigned
(the release workflow skips signing; see [.signpath/README.md](../.signpath/README.md)).

## What is signed

The Windows release files built by [the release workflow](../.github/workflows/release.yml) on GitHub's runners
from the tagged source in this repository: `launchme.exe` (also inside `launchme-<version>-win64.zip`) and the
installer `launchme-<version>-win64-setup.exe`. Not signed with this certificate: third-party libraries shipped
next to the program (ONNX Runtime), the plugin DLLs inside `plugin-packages/*.zip` and the installer's
uninstaller. Each release is signed only after a manual approval. `SHA256SUMS` of every release asset is signed
with Sigstore by the same workflow.

## Team roles

launchme has one maintainer, who holds every role:

- Committers and reviewers: [uxandai](https://github.com/uxandai)
- Approvers: [uxandai](https://github.com/uxandai)

Changes proposed by anyone else (pull requests) are reviewed by the maintainer before they are merged. The
maintainer's GitHub and SignPath accounts use multi-factor authentication.

## Privacy policy

launchme has no telemetry, no analytics and no account of its own. It does not send your library, your play
history or anything about your computer anywhere. It connects only to the services its features use, and
only with what a lookup needs (a game's title or store ID):

- Without any setup: GitHub, to look for a new release (Settings → System → Updates, on by default); Steam's
  store and CDN, for artwork, details and trailers of games by their Steam app ID; HowLongToBeat, for the
  bundled plugin's play times (game titles); the news sites Glance reads (Glance can be switched off in
  Settings → System).
- Only after you add a key or sign in: SteamGridDB, RAWG, ScreenScraper and the Steam Web API (artwork,
  ratings, metadata, friends); your store accounts (GOG, Epic, EA, Amazon, Xbox, Battle.net); RetroAchievements;
  the language model provider you choose for the companion (Gemini, Claude, Grok or a local Ollama server, which
  receives your chat with her) and ElevenLabs for her voice.

Keys and store sessions stay on your computer, encrypted for your Windows account (DPAPI). Each of these
services has its own privacy policy.

## Removing launchme

Installed with the setup: uninstall it in Windows' Settings → Apps; your settings stay unless you choose to
remove them too. The ZIP: switch off *Start with Windows* (Settings → System), remove the shortcuts launchme
added outside itself (Tryb TV's Shortcuts page), then delete the program's folder and `%LOCALAPPDATA%\Launchme`.

# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

## What is signed

The Windows release (`launchme-<version>-win64.zip` on [Releases](../../../releases)): `launchme.exe` and the
plugins built from this repository (HowLongToBeat, Achievements). Every signed file is built from the tagged
source in this repository by its build scripts; third-party libraries shipped next to it (ONNX Runtime) are
not signed with this certificate. Each release is signed only after a manual approval.

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

There is no installer. Switch off *Start with Windows* (Settings → System) and remove the shortcuts launchme
added outside itself (Tryb TV's Shortcuts page), then delete the program's folder and `%LOCALAPPDATA%\Launchme`.

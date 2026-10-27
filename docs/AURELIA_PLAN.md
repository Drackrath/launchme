# Aurelia Windows integration: review plan

Approved for implementation and pull-request review on 2026-10-27. Target release: 0.4.0,
after review and merge. `feat/aurelia` is the integration branch; no release is cut by this work.

## Product contract

- Optional **Extended Aurelia integration**, off by default, highlighted in System settings and onboarding.
- Install/update the latest stable `aurelia_windows_x86_64.exe`, with digest verification and atomic publication.
- Link to <https://github.com/Drackrath/Aurelia> beside the integration controls.
- Explain: primarily single-player/offline games; multiplayer, DRM and Steamworks may need the Steam client.
- Per-game **Launch without Steam**, off by default. Off launches through the official client;
  on uses Aurelia's standalone Windows path. Enabling the global integration alone does not change launches.
- Desktop, TV, mouse, keyboard and controller use the same feature state and command paths.

## Review sequence

Each review branch is a cumulative snapshot with a PR against the preceding snapshot. The integration
PR against `main` contains the complete series. PRs remain unmerged for the owner's review.

| Branch suffix | Scope |
| --- | --- |
| `plan` | This plan and acceptance criteria |
| `runtime` | Typed C++ CLI contracts, exact integers, bounded JSON/NDJSON, Win32 processes, isolated daemon pipe and cancellation |
| `setup` | Verified helper installation, account controls, settings and onboarding, native shared UI foundation |
| `library` | Owned/installed library, account boundaries, metadata, achievements and offline-cache behavior |
| `launch` | Explicit standalone routing, launch-target checks, Steam/Family Sharing requirements and lifecycle |
| `downloads` | Library selection, install/update/verify/move/uninstall, progress, cancellation and recovery |
| `cloud` | Accurate save state, conflicts, explicit resolution, optional before/after standalone sync |
| `content` | DLC, pending changes, manifest pin/downgrade, branches and Workshop |
| `social` | Friends, presence, history and live native chat, reconnect and retained drafts |

Shared foundations may be grouped when splitting would leave an uncompilable intermediate state.
Any grouping and remaining upstream limitations are recorded in the affected PR.

## References and translation approach

- Heroic [PR #5679](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/pull/5679), reviewed at
  `d32b1365a0558da47e34880508075bb628a697c5`: `aurelia.ts`, command types, library/user/game managers,
  and `cloud_sync_status.ts`.
- Aurelia v0.1.40: `a47e90bf42aa9f99818bd8d0de286a5c33da9b44`; public CLI and JSON handlers.

Port the behavior into Launchme's C++20/Win32 architecture: CreateProcessW and explicit handles,
BackgroundWork, generation-guarded RemoteMessage delivery, typed domain state and Direct2D views.
The implementation is independently written against the public CLI; no TypeScript/Electron runtime
or copied upstream application source is linked into Launchme.

## Acceptance

- Isolated `AURELIA_CONFIG_DIR` **and** a per-user/profile Windows named pipe. No standalone-session reuse.
- No shell interpolation; bounded output; exit status and semantic result checked; exact uint64 identities.
- Account changes invalidate old library, cloud, chat and operation replies.
- Failed downloads/updates retain the existing helper. Failed sends retain drafts.
- File listing never implies successful cloud sync; failures, skips and conflicts remain visible.
- No automatic Steam-account switches, sign-ins or other real account actions during verification.
- Tests use synthetic processes and injected transports; build, relevant tests, then the complete suite.
- Visual changes require captured and inspected desktop/TV PNGs.
- Unrelated local changes and private plugins do not enter the public PRs.

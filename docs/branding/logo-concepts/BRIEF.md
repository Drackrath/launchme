# launchme — new logo directions

Stage: concepts to choose from, 2026-10-20.

## Assumptions

- Name in the mark: **launchme**, in lowercase, as in the current interface.
- Native Windows launcher: a shared library across many stores, a desktop window and a gamepad-controlled TV mode.
- Character: fast, orderly, friendly, console-like, without an aggressive esports aesthetic.
- Uses: Windows icon at 16–256 px, app bar, TV screen, README.
- Current mark: a mint “play” triangle in a dark rounded square. It is simple, but it resembles a media player control; the thin outline weakens at 16 px.
- Points of continuity: lowercase letters, a dark neutral background, optional mint. Final color after the shape is chosen.

Based on: `README.md`, `src/ui/desktop_renderer.cpp`, the existing screenshot `build/temp/desktop-library.png` and `res/launchme.ico`.

## Strategy

Promise: **Your games. One starting point.**

Association map: launch → rebound, breaking away, choice; library → tile, shelf, shared room; me → a place of one's own; couch → calm, comfort, readiness.

Rejected visual shortcuts: rocket, gamepad with buttons, lightning bolt, arbitrary neon gradient, a play triangle on its own, esports shield.

## Concept sketches

Designer's working scores: idea / distinctiveness / simplicity / fit / small size, each 1–5. They are not the result of user research.

| Idea | Type | Scores | Decision |
| --- | --- | --- | --- |
| Rebound: an active tile breaks away from the letter L | letter + module | 5/4/5/5/5 | A |
| Shared Library: two collections form an open entrance | abstract | 4/3/5/5/5 | B |
| LM: the name enclosed in a single geometric mark | monogram | 4/4/4/4/4 | C |
| Frame: an active corner breaks out of the frame | abstract | 3/2/5/4/5 | too similar to an expand control |
| Orbit: games converge on a single orbit | abstract | 3/2/4/4/4 | a common motif in the category |
| Slot: an open cartridge slot | pictogram | 4/3/4/3/4 | suggests emulation too strongly |
| Couch: two armrests and the player's seat | pictogram | 4/4/3/3/3 | desktop mode is just as important |
| Start: an L ending in an arrow | letter | 3/2/5/4/5 | too similar to an export icon |
| Portal: a door with a play symbol cut out | negative space | 3/2/4/4/4 | still a media player control |
| Player: a face made of two tiles | mascot | 4/3/4/3/4 | shifts the emphasis to the AI companion |

## Process source

The [logo-design-skill](https://github.com/kaankiziltug/logo-design-skill) specified by the user was used, commit `734090317a2155c5bf1ae8bd4cc85c6848a0128f`.

Library research: the `gaming` category contains only 5 entries, so it is not a complete market survey. The construction of Steam, Unity, Framer and Pagekit was reviewed; arrows and the negative-space technique were also searched for. The reference files were used to assess simplicity, geometry and similarity, not for copying.

Following the skill's process: first three directions, rendering and tests; the full icon set and implementation after a direction is chosen.

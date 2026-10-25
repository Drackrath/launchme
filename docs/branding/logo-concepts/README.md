# New launchme logo — choosing a direction

**Recommendation: A — Rebound.** The letter L and the tile breaking away from it refer both to the name and to launching a game. The mark works on its own, without an outline and without color.

## Preview

- [Open the comparison of the three directions](index.html) — standalone HTML, works locally, without a network connection; includes links to the SVGs.
- [PNG board](concepts.png).
- [Brief and idea selection](BRIEF.md).

| Direction | Mark alone | Mark with name | Character / trade-off |
| --- | --- | --- | --- |
| **A — Rebound** | [SVG](a-odskok-v2.svg) | [SVG](a-odskok-lockup.svg) | A distinctive initial and motion. The association with a tile breaking away is abstract. |
| B — Shared Library | [SVG](b-wspolna-biblioteka-v2.svg) | [SVG](b-wspolna-biblioteka-lockup.svg) | Calm, friendly; may be associated with a book or a reading app. |
| C — LM | [SVG](c-monogram-v2.svg) | [SVG](c-monogram-lockup.svg) | Compact and technical; the tightest counters at 16 px. |

The `*-v1.svg` files preserve the first iteration. `*-v2.svg` are the refined versions shown on the board.

## Construction

- Symbols: `viewBox="0 0 256 256"`; horizontal lockups: `0 0 1312 256`.
- Dark base color: `#111315`. Light color on the board: `#F2F3EE`; suggested mint for A: `#AAF0D1`.
- A: the letter's vertical and horizontal strokes are 56 units thick, the gaps to the separated module are 40 units, 45° chamfers, radii 16 and 32.
- B: two opening modules, a 32-unit gap, a height difference of 32, arcs 40/48; the bottom edges are tangent at a 15° angle, with values written to two decimal places.
- C: a single closed outline with 45° diagonals, a widened lower counter in the letter M and aligned top terminals.
- The [launchme wordmark](wordmark-study.svg) is drawn as custom paths. It does not require a font. The Segoe UI font in the HTML is used only for the board's captions.
- The source logo contains no SVG text, rasters, masks, filters or strokes that need to be expanded.

## Verification

Two iterations of drawing, rendering and evaluation were carried out:

1. `v1`: structure audit and a board of large marks with actual 64/32/16 px samples.
2. `v2`: correction of the horizontal stroke in A, asymmetry and arc tangency in B, opened counter and alignment in C; added lettering and horizontal lockups.
3. Reviewed Chromium renders: black mark on light, white on dark, 16/20/24/32/48/64 px, mirroring, 180° rotation and blur. In addition, actual 16 × 16 px renders enlarged 8× without interpolation.
4. Side-by-side comparison with Steam, Unity, Framer and Pagekit in terms of character, scale and geometry. This is a limited review, not a market-wide uniqueness study.

Observations: in A, the separated tile and the letter remain distinct at 16 px; in B, the gap between the modules is preserved; C retains its initials but has the smallest counters. The inverted versions may get a subtle optical correction after a direction is chosen.

Working boards and tests are in the ignored `build/temp/`:

- `launchme-logo-round-1.png`, `launchme-logo-round-2.png`;
- `launchme-logo-tests.html` and `.png` — the reviewed, compact overview;
- `launchme-logo-skill-tests.html` — an additional sheet generated with the skill's tool.

Process and tools: [logo-design-skill](https://github.com/kaankiziltug/logo-design-skill), the version specified in the brief. The SVG audit assesses the technical construction heuristically; it does not measure the quality of the idea or user preferences.

## Next stage

After choosing A, B or C: final geometry and colors, light/dark versions, a complete PNG set and a multi-size ICO for Windows, and implementation in launchme's resources and interface.

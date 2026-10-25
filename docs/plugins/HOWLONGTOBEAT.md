# HowLongToBeat

A separate plugin, `launchme.howlongtobeat` **1.1.0**, package `launchme.howlongtobeat.zip`.
Built and shipped together with the launcher, enabled by default on first installation.

## Usage

**Game details**: the HowLongToBeat block below playtime and achievements shows
`Your time / main story`, a percentage and a comparison bar. Example: `2 h / 8 h · 25%`.
Percentage = our playtime / main story time × 100; it can exceed 100%.
This is a time comparison, not actual story progress. A missing base time or
own playtime is shown as “—”, without guessing a percentage.

The **active-game widget** in the right drawer shows the game's name, the percentage and abbreviated
times (e.g. `150%`, `3h / 2h`). It applies to the running game, not the one selected in the library.
In Guide, the game under the overlay takes precedence; on the desktop, the most recent confirmed,
non-suspended session. Without an active game, it shows the corresponding state. Its size and position
can be changed using the standard widget rearrangement.

Click/Enter/A on the block or widget opens a drawer with our playtime, the main
story, the percentage, main story + extras and 100% completion. “Change match”
lets you pick the correct title; a single exact match is selected
automatically and does not override a manual choice. The link opens the game's page; F5
refreshes the data. The former HowLongToBeat entry in the context menu has been replaced
by the Details block and the widget.

No account or key is required. It sends the name of the selected/active game to HowLongToBeat;
it does not receive the whole library. Fetching happens on the plugin worker.
The choice and the results are saved in `plugins/data/launchme.howlongtobeat/` under
an encoded game key. The cache is valid for 7 days; on a network error, the last
valid copy is kept. A saved match is refreshed by game ID.
The widget reads the cache; fetching is delegated to a separate plugin data channel, with no HTTP in the
render frame. Reference times are queried at most once per minute, and widget values are
refreshed every 5 s and when the game changes. The percentage uses the host's current counter.
Steam: the client's local counter as the base, read when the session is created, plus the time
observed after the base was read, without
double counting. Non-Steam games: the saved Launchme counter. Quick Resume
does not increase the time. Changing the active game discards late results for the previous one.

## Implementation and verification

- `plugins/howlongtobeat/hltb.*`: reading the public search protocol,
  detecting the current path from the site's JS, anonymous search initialization,
  a JSON parser for results and for the game page's `__NEXT_DATA__`. Downloaded JS is never executed.
- `plugin.cpp`, `service.*`: the SDK v1 contract, match selection, cache and the isolated
  context of the open drawer (refreshing the widget for game B does not change the choice for game A).
- `src/app/plugin_game_views.cpp`, `src/plugins/time_comparison.hpp`: generic `details`
  slots, `activeGame` widgets, time data and comparison with the host counter.
- A change in the service's protocol may require a plugin update; a network error does not
  disable the plugin and does not replace a valid cache with empty data.
- `howlongtobeat_search_and_plugin_cache`: an offline test with an HTTP stub and
  the real DLL with match saving, automatic selection, 0/no data/>100%,
  two parallel game contexts.
- `hltb_details_and_active_game_widget`: native rendering, the block button,
  the comparison below achievements, the active-game widget, Steam without double-counted time,
  suspension, stale responses and plugin disabling. Screenshots `hltb-details.png`
  and `hltb-active-widget.png` under `build/temp`.
- Manually: `build/temp/vs/Release/howlongtobeat_tests.exe --live`.
  Verified with a real search for Portal 2 and the page of game 7231.

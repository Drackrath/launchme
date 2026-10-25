# Companion characters

Each character is a folder, and each of her costumes is a subfolder with the cut-out images:

```
res/companion/<key>/character.txt                        character (required)
res/companion/<key>/costume_00/<emotion>.png             basic costume (required, at least cool.png)
res/companion/<key>/costume_<NN>[_<name>]/<emotion>.png  further costumes
res/companion/<key>/costume_<NN>[_<name>]/costume.txt    optional costume description
```

New character = new folder with `character.txt` and `costume_00`; the code does not change. CMake (`tools/build.ps1`)
finds the characters and costumes on its own at configure time and embeds them in the exe (up to 20 characters with 32 costumes each), so
**after adding or changing a character you have to rebuild the launcher** (unlike photos, below).

The default character (nothing selected in Settings, or the selected one has disappeared) is the first one alphabetically by key.
`order=` only sets the order of the cards in Settings.

## Key

The folder name, e.g. `hana`: 1–32 characters, lowercase letters, digits, `_` or `-` (not at the start). It must not be
named `day`, `streak`, `pick` or `card` (those are lines of the sympathy file). The character is stored under this key in the settings,
in the sympathy file, in the chat history and in the photos folder (`<photos folder>/<key>/…`). Renaming the folder =
a new character (the old one's settings, level and conversations are lost).

## character.txt

UTF-8 (a BOM does no harm), `key=value` lines (spaces around `=` allowed), `#` is a comment:

```
order=8                       order of the cards in Settings (ascending; without it: at the end)
name=Hana                     name (in the chat, menu and prompt) — required
color=#FF9FC8                 her color: name, speech-bubble outline, glow (#RRGGBB only)
persona=kuudere               her fixed personality: genki tsundere yandere kuudere dandere deredere himedere kichidere kanedere
card_pl=Różowe włosy · mundurek       caption on the card in Settings
card_en=Pink hair · school uniform
you_pl=Masz krótkie różowe włosy …    appearance in the prompt when she is the one speaking
her_pl=Ma krótkie różowe włosy …      appearance in the prompt when she is a guest
you_en=You have short pink hair …
her_en=She has short pink hair …
about_pl=Interesujesz się …           who she is beyond the archetype: passions, family, quirks, tastes, pop culture,
about_en=You are into …               how she talks and what she thinks of the other companions (addressed to her, “you”)
about_her_pl=Interesuje się …         shorter, about her as a guest (“she”)
about_her_en=She is into …
topics_pl=temat 1 | temat 2 | …       what she has been thinking about lately: the prompt takes 2 per time of day (they change daily)
topics_en=topic 1 | topic 2 | …
example_pl=Gracz: Hej! || Ty: [smug]…  sample exchange (a model of style, length and tags); repeatable line, up to 8,
example_en=Player: Hi! || You: […]…    ideally 3; only when she talks alone (no guest); they are not quoted verbatim
likes=Horror, Souls-like, Gothic      her taste in games: tags as on Steam (up to 8, separated by `,` or `|`);
dislikes=Sports, Farming Sim          when he/she doesn't point to anything, she pulls her own way, but gives in
gender=female                 the character's gender: female | male (default female)
attraction=any                romantic preference: any | male | female | none (aromantic)
attraction_flex=rare_exception  strict (hard orientation boundary) | rare_exception (exception/dilemma at a high level)
style=traditional             relationship style: traditional (distance, exclusivity, rules; stages somewhat later) | balanced | casual (relaxed,
role=auto                     default relationship track: auto | friend (close friend) | coworker (professional/coworker) | romantic
wealth=comfortable            default financial situation: comfortable | tight (money is tight) | broke (rock bottom) | flush (living large)
work=steady                   default work situation: steady | busy (overload/exam season) | between_jobs (no steady job) | promoted | burnout
origin_pl=Pochodzisz z …      where she comes from and what her family home / roots are like
origin_en=You come from …
occupation_pl=Studiujesz …    what she does (job, studies, earnings, standard of living)
occupation_en=You study …
home_pl=Paryż, Le Marais — …  where she lives: a real city and district, the city first, up to the comma (her
home_en=Paris, Le Marais — …  searches for “what's happening in the city”, plans and episodes in real places)
timezone=+01:00               her time zone in winter time (the clock in the prompt, when she sleeps, when she browsed the web)
dst=eu                        daylight saving time: eu | us | none
chronotype=night              night: sleeps approx. 2:00–10:00 (sleepy in the morning); day: sleeps approx. 23:00–7:00 — her time, each time ±30 min
                              per night, 1.5% of nights sleepless; while asleep she doesn't chat (the chat waits until morning, in the evening she says
                              “good night”), doesn't drop in as a guest and doesn't comment on the home screen; without timezone= she doesn't sleep at all
favorites_pl=Muzyka: … | …    real things she loves (category: list | …); only existing bands, brands,
favorites_en=Music: … | …     films and places — examples in the prompt and topics of her own searches
goals_pl=cel 1 | cel 2 | …    her own starting goals outside games (the mind takes one at a time, each for 1–3 weeks;
goals_en=goal 1 | goal 2 | …  then she sets new ones herself); achieved ones remain as milestones of her life
gossip=0.4                    how readily she passes on to the other companions what she heard from him/her (0 never – 1 always;
                              secrets never)
status=soon                   optional: only a “Coming soon” card (she cannot be selected or called; may have no images)
comfy=1girl, solo, …          only for offline tools (tools/companion_gen), the app does not read it
elevenlabs_voice=<ID> <name>  her ElevenLabs voice: ID from the Voice Library (through the API only on a paid plan), after a space
                              the name for Settings (“5NJx3YreuBEwRpE2BG5G Zofia - Fresh and Upbeat”)
elevenlabs_voice_free=<ID> …  fallback when the plan rejects the library voice (402): one of the built-in voices of every
                              account (Jessica, Laura, Sarah, Alice, Lily, Bella, Matilda; IDs in src/net/elevenlabs.hpp)
elevenlabs_model=eleven_v4_turbo  her model (empty: Eleven v4 Turbo); a model selected in Settings takes precedence
elevenlabs_settings=stability=0.4, similarity=0.8, style=0.35, speed=1.05, speaker_boost=1
                              voice settings (0–1, speed 0.7–1.2); none = her personality's values
elevenlabs_tag=bubbly, upbeat how she sounds (in English, without brackets): v3/v4 models' audio tag at the start of her lines,
                              together with the emotion tag (“[bubbly, upbeat, excited] …”); none = her personality's default
```

The `_en` versions are optional (missing = Polish). Unknown keys are ignored. The personality (-dere) is fixed for the
character (card in Settings: name, personality below it); `about_*` refines it and wins in case of a conflict.
A new kind of personality (outside the list) requires code changes (prompt, offline lines, default voice in `src/net/elevenlabs.cpp`).
Voice: in order, the voice selected in Settings, `elevenlabs_voice`, `elevenlabs_voice_free`, the built-in voice of her personality
(`src/net/elevenlabs.cpp`).
Every value on one line; a `[…]` or `costume=…` line in `character.txt` would break the manifest.

Templates: any of the existing characters, e.g. `saki/character.txt`.

## What CMake will not accept (a warning at configure time, the character is skipped)

- a key outside the rules above (including `day`, `streak`, `pick`, `card`),
- no `name=` line,
- no `costume_00/cool.png` (unless `status=soon`) — every fallback for a missing image ends at this face,
- the 21st character and beyond, the 33rd costume and beyond, a costume folder with an invalid name.

A skipped character does not shift the other characters' images (character index = her images in the exe).

## Costumes

- `costume_00` – the basic costume (required; in the menu always “Basic”, its `costume.txt` is ignored).
- `NN` – two digits 01–50: the sympathy level (1–50) from which the costume unlocks when gamification
  is on. Without gamification all costumes are available. `costume_01` = right away.
- `_<name>` – optional, letters/digits/`_`/`-` only; without `costume.txt` it becomes the name in the menu
  (`costume_10_school-uniform` → “School uniform”; without a name: “Outfit 10”).
- Selection: in the chat “…” → “Change outfit” — tiles with a thumbnail (her `smile` in that costume) and the name,
  locked ones greyed out with “from level NN”, the one being worn with an accent outline and “Wearing”.

### costume.txt (optional)

```
name_pl=Kimono w żurawie        name in the menu and prompt (with Polish characters)
name_en=Crane kimono
you_pl=Masz na sobie granatowe kimono w białe żurawie i czerwony pas obi.
you_en=You wear a navy kimono with white cranes and a red obi sash.
```

`you_pl`/`you_en` goes into the prompt, so the companion knows what she is wearing.

## Files in the costume folder

20 emotions, exactly these names: `smile angry sad excited shock thinking smug shy fear sleepy
playful cool` (classic) and `love laugh cry confused determined wave drink gaming` (with a can
of drink, with a controller) → `smile.png`, `angry.png`, … Other names are ignored.

- Optionally `<emotion>_talk.png` (open mouth) and `<emotion>_blink.png` (closed eyes).
- Optionally `pose.png`: the whole character in this costume (any size, transparent background, cropped to the
  character). When the chat opens and after a costume change it fills the left column of the screen for ~3 s (entrance 0.35 s,
  exit 0.5 s), standing on the bottom edge, then gives way to the bust.
- A missing emotion in a costume: that costume's `cool` (the costume does not change); without it, that emotion from `costume_00`, and
  finally `cool` from `costume_00`. So a costume can start with just a few emotions, preferably including `cool`.
- `faces.txt` (generated): where her face is on each image (`face_<emotion>=<x> <y> <size>` in canvas
  pixels), so that the round avatar (the card in the desktop-mode sidebar, the Flix pill) has the face in the center. After new
  images: `python tools/companion_faces.py [--only <key>]` (OpenCV 4.x, details in `--help`) and a rebuild.
  Without the file the avatar uses the center of the bust.

Format: transparent PNG **560×560** (a shared square canvas, `companion_ui::sprite_width/height`; any other size
will be stretched). The bust, from the top of the head to the waist (with room for a can in hand and a waving hand), stands with its flat
bottom cut on the bottom edge, centered. Manpu marks and head rotation are measured from the face in `faces.txt`.
Generation (ComfyUI, the same models and LoRA as the photos; a local tool, not published in this repository): `tools/companion_gen/feed_v2.py` — `poses`, `bases`,
`emotions`, `pack` (background of any color cut out, `pose.png` with its feet on the bottom edge).

## Photos from her (no rebuild)

Settings → Assistant → **Photos and feed folder**. Point it at a shared directory, e.g. `companion_import`;
the dialog shows instructions, and **Browse…** opens a folder picker (Windows' own in desktop mode, a controller-friendly browser in TV mode).
The chosen path goes into the field; **Save** confirms it. Photos work automatically once the files are there. The directory
is read at runtime, nothing needs to be built:

```
companion_import/photos/<key>/<file>.png              her photos, available right away
companion_import/photos/<key>/<from>-<to>/<file>.png  unlock from level <from> (e.g. 00-09, 10-19 … 50-59; the top level is 50)
companion_import/photos/<key>/…/<file>.txt            optional photo description (one line, ≤ 4 KB): she talks about the photo
```

- PNG, JPEG, WebP, GIF, BMP, up to 16 MiB; a folder named other than a range is skipped.
- Each character sends only photos from **her own** subfolder; without one she sends none. Loose files
  in the directory and subfolders without a character key are never sent.
- Without gamification all ranges starting at or below level 50 (the top one) are open.
- At most one photo a day, once per conversation (after the player's 2nd, 3rd or 5th message); the last 30 do not come back while there are others.

## Separate photos for Launchfeed

In the same import directory, put pairs `feed/saki/1.png` / `feed/saki/1.txt`.
The TXT describes what is visible and what she was doing; the LLM writes the caption in her voice.
No TXT = the photo waits. At most 2 posts a day per active character, no photo is reused.
The phone icon opens the animated Launchfeed. Photos keep their full aspect ratio, even when that requires
scrolling. Under a post: **heart**, **comments** (your own and generated ones) and **Priv**, which ties a conversation to the post.
Format, storage and the link to memory: `src/net/companion_feed.hpp`.

## From ready-made transparent PNGs

Separate files of any size, with the emotion name in the file name (`01_angry_transparent.png`,
`smile.png`, `smile_talk.png`…):

```
python tools/companion_sprites.py --images <folder> --look hana --costume costume_10_halloween --out res/companion
```

The tool scales all of them by one common factor (so the head does not change size between emotions),
places each bust with its bottom edge at the bottom of a 557×475 canvas, centered, and saves them under
the correct names. It lists the missing emotions.

## From a sprite sheet

Instead of cutting by hand, you can draw a 4×3 sheet on a green background (2752×1536, in the order of the classic
emotions above) and cut it with the tool (Pillow + NumPy):

```
python tools/companion_sprites.py --look hana --costume costume_00 sheet.png --out res/companion
```

The tool removes the green and the captions, places each bust on a 557×475 canvas and saves 12 files
to `res/companion/hana/costume_00/`.

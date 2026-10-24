# Postacie towarzyszek

Każda postać to folder, a każdy jej strój to podfolder z pociętymi obrazkami:

```
res/companion/<klucz>/character.txt                       postać (wymagany)
res/companion/<klucz>/costume_00/<emocja>.png              podstawowy strój (wymagany, co najmniej cool.png)
res/companion/<klucz>/costume_<NN>[_<nazwa>]/<emocja>.png  kolejne stroje
res/companion/<klucz>/costume_<NN>[_<nazwa>]/costume.txt   opcjonalny opis stroju
```

Nowa postać = nowy folder z `character.txt` i `costume_00`; kod się nie zmienia. CMake (`tools/build.ps1`)
sam znajduje postacie i stroje przy konfiguracji i osadza je w exe (do 20 postaci po 32 stroje), więc
**po dodaniu albo zmianie postaci trzeba przebudować launcher** (w przeciwieństwie do zdjęć, niżej).

Domyślna postać (nic nie wybrano w Ustawieniach albo wybrana zniknęła) to pierwsza alfabetycznie po kluczu.
`order=` ustala tylko kolejność kart w Ustawieniach.

## Klucz

Nazwa folderu, np. `hana`: 1–32 znaki, małe litery, cyfry, `_` albo `-` (nie na początku). Nie może się
nazywać `day`, `streak`, `pick` ani `card` (to linie pliku sympatii). Tym kluczem postać jest zapisana w ustawieniach,
w pliku sympatii, w historii czatów i w folderze zdjęć (`<katalog zdjęć>/<klucz>/…`). Zmiana nazwy folderu =
nowa postać (ustawienia, poziom i rozmowy starej przepadają).

## character.txt

UTF-8 (BOM nie przeszkadza), linie `klucz=wartość` (spacje wokół `=` dozwolone), `#` to komentarz:

```
order=8                       kolejność kart w Ustawieniach (rosnąco; bez niej: na końcu)
name=Hana                     imię (w czacie, menu i prompcie) — wymagane
color=#FF9FC8                 jej kolor: imię, obwódka dymków, poświata (tylko #RRGGBB)
persona=kuudere               jej stały charakter: genki tsundere yandere kuudere dandere deredere himedere kichidere kanedere
card_pl=Różowe włosy · mundurek       podpis na karcie w Ustawieniach
card_en=Pink hair · school uniform
you_pl=Masz krótkie różowe włosy …    wygląd w prompcie, gdy to ona mówi
her_pl=Ma krótkie różowe włosy …      wygląd w prompcie, gdy jest gościem
you_en=You have short pink hair …
her_en=She has short pink hair …
about_pl=Interesujesz się …           kim jest poza archetypem: pasje, rodzina, dziwactwa, gusta, popkultura,
about_en=You are into …               jak mówi i co myśli o innych towarzyszkach (do niej, „ty”)
about_her_pl=Interesuje się …         krócej, o niej jako gościu („ona”)
about_her_en=She is into …
topics_pl=temat 1 | temat 2 | …       o czym ostatnio myśli: prompt bierze 2 na porę dnia (zmieniają się codziennie)
topics_en=topic 1 | topic 2 | …
example_pl=Gracz: Hej! || Ty: [smug]…  przykładowa wymiana (wzór stylu, długości i tagów); linia powtarzalna, do 8,
example_en=Player: Hi! || You: […]…    najlepiej 3; tylko gdy rozmawia sama (bez gościa); nie są cytowane dosłownie
likes=Horror, Souls-like, Gothic      jej gust w grach: tagi jak na Steamie (do 8, rozdzielane `,` albo `|`);
dislikes=Sports, Farming Sim          gdy on/ona nic nie wskaże, ciągnie w swoją stronę, ale ustępuje
gender=female                 płeć postaci: female | male (domyślnie female)
attraction=any                preferencja romantyczna: any | male | female | none (aromantyczna)
attraction_flex=rare_exception  strict (twarda granica orientacji) | rare_exception (wyjątek/dylemat na wysokim poziomie)
style=traditional             styl relacji: traditional (dystans, wyłączność, zasady; etapy trochę później) | balanced | casual (luz,
role=auto                     domyślny tor relacji: auto | friend (przyjaciel) | coworker (służbowy/współpracownik) | romantic
wealth=comfortable            domyślna sytuacja finansowa: comfortable | tight (krucho) | broke (dołek) | flush (na bogato)
work=steady                   domyślna sytuacja zawodowa: steady | busy (nawał/sesja) | between_jobs (brak stałej pracy) | promoted | burnout
origin_pl=Pochodzisz z …      skąd pochodzi i jaki ma dom rodzinny / korzenie
origin_en=You come from …
occupation_pl=Studiujesz …    czym się zajmuje (praca, studia, zarobki, stopa życiowa)
occupation_en=You study …
home_pl=Paryż, Le Marais — …  gdzie mieszka: prawdziwe miasto i dzielnica, najpierw miasto do przecinka (jej
home_en=Paris, Le Marais — …  wyszukiwania „co się dzieje w mieście”, plany i epizody w prawdziwych miejscach)
timezone=+01:00               jej strefa w czasie zimowym (zegar w prompcie, kiedy śpi, kiedy przeglądała sieć)
dst=eu                        czas letni: eu | us | none
chronotype=night              night: śpi ok. 2:00–10:00 (rano senna); day: śpi ok. 23:00–7:00 — jej czasu, każda pora ±30 min
                              na noc, 1,5% nocy nieprzespanych; śpiąca nie rozmawia (czat czeka do rana, wieczorem żegna się
                              „dobranoc”), nie wpada w gości i nie komentuje na ekranie głównym; bez timezone= nie śpi wcale
favorites_pl=Muzyka: … | …    prawdziwe rzeczy, które kocha (kategoria: lista | …); tylko istniejące zespoły, marki,
favorites_en=Music: … | …     filmy i miejsca — przykłady w prompcie i tematy jej własnych wyszukiwań
goals_pl=cel 1 | cel 2 | …    jej własne cele startowe poza grami (umysł bierze po jednym, każdy na 1–3 tygodnie;
goals_en=goal 1 | goal 2 | …  potem sama stawia sobie nowe); spełnione zostają kamieniami milowymi jej życia
gossip=0.4                    jak chętnie przekazuje innym towarzyszkom, co usłyszała od niego/niej (0 nigdy – 1 zawsze;
                              sekrety nigdy)
status=soon                   opcjonalnie: tylko karta „Wkrótce” (nie da się jej wybrać ani zawołać; może nie mieć obrazków)
comfy=1girl, solo, …          tylko dla narzędzi offline (tools/companion_gen), aplikacja tego nie czyta
elevenlabs_voice=<ID> <nazwa> jej głos ElevenLabs: ID z Biblioteki głosów (przez API tylko w płatnym planie), po spacji
                              nazwa do Ustawień („5NJx3YreuBEwRpE2BG5G Zofia - Fresh and Upbeat”)
elevenlabs_voice_free=<ID> …  zapasowy, gdy plan odrzuci głos z biblioteki (402): jeden z wbudowanych głosów każdego
                              konta (Jessica, Laura, Sarah, Alice, Lily, Bella, Matilda; ID w src/net/elevenlabs.hpp)
elevenlabs_model=eleven_v4_turbo  jej model (pusty: Eleven v4 Turbo); model wybrany w Ustawieniach ma pierwszeństwo
elevenlabs_settings=stability=0.4, similarity=0.8, style=0.35, speed=1.05, speaker_boost=1
                              ustawienia głosu (0–1, speed 0.7–1.2); brak = wartości jej charakteru
elevenlabs_tag=bubbly, upbeat jak brzmi (po angielsku, bez nawiasów): tag audio modeli v3/v4 na początku jej kwestii,
                              razem z tagiem emocji („[bubbly, upbeat, excited] …”); brak = domyślny jej charakteru
```

Wersje `_en` są opcjonalne (brak = polska). Nieznane klucze są ignorowane. Charakter (-dere) jest stały dla
postaci (karta w Ustawieniach: imię, pod nim charakter); `about_*` go uszczegóławia i przy sprzeczności wygrywa.
Nowy rodzaj charakteru (spoza listy) wymaga zmian w kodzie (prompt, kwestie offline, domyślny głos w `src/net/elevenlabs.cpp`).
Głos: kolejno głos wybrany w Ustawieniach, `elevenlabs_voice`, `elevenlabs_voice_free`, wbudowany głos jej charakteru
(`src/net/elevenlabs.cpp`).
Każda wartość w jednej linii; linia `[…]` albo `costume=…` w `character.txt` zepsułaby manifest.

Wzory: dowolna z obecnych postaci, np. `saki/character.txt`.

## Czego CMake nie weźmie (ostrzeżenie przy konfiguracji, postać pominięta)

- klucz spoza reguł wyżej (także `day`, `streak`, `pick`, `card`),
- brak linii `name=`,
- brak `costume_00/cool.png` (chyba że `status=soon`) — na tej minie kończy się każde zastępstwo brakującego obrazka,
- 21. postać i dalsze, 33. strój i dalsze, folder stroju o złej nazwie.

Pominięta postać nie przesuwa obrazków pozostałych (indeks postaci = jej obrazki w exe).

## Stroje

- `costume_00` – podstawowy strój (wymagany; w menu zawsze „Podstawowy”, jego `costume.txt` jest ignorowany).
- `NN` – dwie cyfry 01–50: poziom sympatii (1–50), od którego strój się odblokowuje przy włączonej
  grywalizacji. Bez grywalizacji wszystkie stroje są dostępne. `costume_01` = od razu.
- `_<nazwa>` – opcjonalna, tylko litery/cyfry/`_`/`-`; bez `costume.txt` staje się nazwą w menu
  (`costume_10_school-uniform` → „School uniform”; bez nazwy: „Strój 10”).
- Wybór: w czacie „…” → „Zmień strój” — kafelki z miniaturą (jej `smile` w tym stroju) i nazwą,
  zablokowane wyszarzone z „od poziomu NN”, założony z obwódką akcentu i „Założony”.

### costume.txt (opcjonalny)

```
name_pl=Kimono w żurawie        nazwa w menu i prompcie (z polskimi znakami)
name_en=Crane kimono
you_pl=Masz na sobie granatowe kimono w białe żurawie i czerwony pas obi.
you_en=You wear a navy kimono with white cranes and a red obi sash.
```

`you_pl`/`you_en` trafia do promptu, więc towarzyszka wie, co ma na sobie.

## Pliki w folderze stroju

20 emocji, dokładnie te nazwy: `smile angry sad excited shock thinking smug shy fear sleepy
playful cool` (klasyczne) oraz `love laugh cry confused determined wave drink gaming` (z puszką
napoju, z padem) → `smile.png`, `angry.png`, … Inne nazwy są ignorowane.

- Opcjonalnie `<emocja>_talk.png` (otwarte usta) i `<emocja>_blink.png` (zamknięte oczy).
- Opcjonalnie `pose.png`: cała postać w tym stroju (dowolny rozmiar, przezroczyste tło, przycięta do
  postaci). Przy otwarciu czatu i po zmianie stroju wypełnia lewą kolumnę ekranu na ~3 s (wejście 0,35 s,
  wyjście 0,5 s), stojąc na dolnej krawędzi, potem ustępuje popiersiu.
- Brakująca emocja stroju: jego `cool` (strój się nie zmienia); bez niego ta emocja z `costume_00`, a na
  końcu `cool` z `costume_00`. Strój można więc zacząć od kilku emocji, najlepiej z `cool`.
- `faces.txt` (generowany): gdzie na każdym obrazku jest jej twarz (`face_<emocja>=<x> <y> <rozmiar>` w pikselach
  płótna), żeby okrągły awatar (karta w pasku bocznym trybu desktop, pigułka Flix) miał twarz na środku. Po nowych
  obrazkach: `python tools/companion_faces.py [--only <klucz>]` (OpenCV 4.x, szczegóły w `--help`) i przebudowa.
  Bez pliku awatar bierze środek popiersia.

Format: przezroczysty PNG **560×560** (wspólne kwadratowe płótno, `companion_ui::sprite_width/height`; inny rozmiar
zostanie rozciągnięty). Popiersie od czubka głowy do pasa (mieści się puszka w ręce i machająca dłoń) stoi płaskim
dolnym cięciem na dolnej krawędzi, wyśrodkowane. Znaczki manpu i obrót głowy liczą się od twarzy z `faces.txt`.
Generowanie (ComfyUI, te same modele i LoRA co zdjęcia): `tools/companion_gen/feed_v2.py` — `poses`, `bases`,
`emotions`, `pack` (tło dowolnego koloru wycięte, `pose.png` stopami na dolnej krawędzi).

## Zdjęcia od niej (bez przebudowy)

Ustawienia → Asystent → **Folder zdjęć i feedu**. Wskaż wspólny katalog, np. `companion_import`;
dialog pokazuje instrukcję, a **Przeglądaj…** otwiera wybór folderu (Windows w desktopie, przeglądarka na pada w TV).
Wybrana ścieżka trafia do pola; **Zapisz** ją zatwierdza. Zdjęcia działają automatycznie, gdy są pliki. Katalog
jest czytany w trakcie działania, nic nie trzeba budować:

```
companion_import/photos/<klucz>/<plik>.png            jej zdjęcia dostępne od razu
companion_import/photos/<klucz>/<od>-<do>/<plik>.png  otwierają się od poziomu <od> (np. 00-09, 10-19 … 50-59; najwyższy poziom to 50)
companion_import/photos/<klucz>/…/<plik>.txt          opcjonalny opis zdjęcia (jedna linia, ≤ 4 KB): ona o nim opowiada
```

- PNG, JPEG, WebP, GIF, BMP, do 16 MiB; folder o innej nazwie niż zakres jest pomijany.
- Każda postać wysyła tylko zdjęcia ze **swojego** podkatalogu; bez niego nie wysyła żadnych. Pliki luzem
  w katalogu i podkatalogi bez klucza postaci nie są nigdy wysyłane.
- Bez grywalizacji otwarte są wszystkie zakresy zaczynające się do poziomu 50 (najwyższego).
- Najwyżej jedno zdjęcie dziennie, raz na rozmowę (po 2., 3. albo 5. wiadomości gracza); ostatnie 30 nie wracają, dopóki są inne.

## Osobne zdjęcia do Launchfeed

W tym samym katalogu importu umieść pary `feed/saki/1.png` / `feed/saki/1.txt`.
TXT opisuje, co widać i co robiła; podpis tworzy LLM jej głosem.
Brak TXT = zdjęcie czeka. Maksymalnie 2 posty dziennie na aktywną postać, zdjęcia bez powtórek.
Ikonka telefonu otwiera animowany Launchfeed. Zdjęcia zachowują pełne proporcje, także gdy wymagają
przewijania. Pod postem: **serce**, **komentarze** (własne i generowane) oraz **Priv**, które wiąże rozmowę z postem.
Format, zapis i powiązanie z pamięcią: `src/net/companion_feed.hpp`.

## Z gotowych przezroczystych PNG

Osobne pliki dowolnej wielkości, z nazwą emocji w nazwie pliku (`01_angry_transparent.png`,
`smile.png`, `smile_talk.png`…):

```
python tools/companion_sprites.py --images <folder> --look hana --costume costume_10_halloween --out res/companion
```

Narzędzie skaluje wszystkie jedną skalą (żeby głowa nie zmieniała wielkości między emocjami),
stawia każde popiersie dolną krawędzią na dole płótna 557×475, wyśrodkowane, i zapisuje pod
właściwymi nazwami. Wypisuje brakujące emocje.

## Z arkusza (sprite sheet)

Zamiast ciąć ręcznie można narysować arkusz 4×3 na zielonym tle (2752×1536, kolejność jak klasyczne
emocje wyżej) i pociąć go narzędziem (Pillow + NumPy):

```
python tools/companion_sprites.py --look hana --costume costume_00 arkusz.png --out res/companion
```

Narzędzie usuwa zieleń i podpisy, układa każde popiersie na płótnie 557×475 i zapisuje 12 plików
do `res/companion/hana/costume_00/`.

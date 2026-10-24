# Nowe logo launchme — wybór kierunku

**Rekomendacja: A — Odskok.** Litera L i odrywający się kafel odnoszą się jednocześnie do nazwy oraz startu gry. Znak działa samodzielnie, bez obwódki i bez koloru.

## Podgląd

- [Otwórz porównanie trzech kierunków](index.html) — samodzielny HTML, działa lokalnie, bez sieci; zawiera odnośniki do SVG.
- [Plansza PNG](concepts.png).
- [Brief i selekcja pomysłów](BRIEF.md).

| Kierunek | Sam znak | Znak z nazwą | Charakter / kompromis |
| --- | --- | --- | --- |
| **A — Odskok** | [SVG](a-odskok-v2.svg) | [SVG](a-odskok-lockup.svg) | Wyrazisty inicjał i ruch. Skojarzenie odrywającego się kafla jest abstrakcyjne. |
| B — Wspólna biblioteka | [SVG](b-wspolna-biblioteka-v2.svg) | [SVG](b-wspolna-biblioteka-lockup.svg) | Spokojny, przyjazny; może kojarzyć się z książką lub aplikacją do czytania. |
| C — LM | [SVG](c-monogram-v2.svg) | [SVG](c-monogram-lockup.svg) | Zwarty i techniczny; najciaśniejsze światła przy 16 px. |

Pliki `*-v1.svg` zachowują pierwszą iterację. `*-v2.svg` to dopracowane wersje pokazane na planszy.

## Konstrukcja

- Symbole: `viewBox="0 0 256 256"`; poziome zestawienia: `0 0 1312 256`.
- Ciemny kolor bazowy: `#111315`. Na planszy jasny: `#F2F3EE`; sugestia mięty dla A: `#AAF0D1`.
- A: pion i poziom litery o grubości 56 jednostek, odstępy od oddzielonego modułu 40 jednostek, ścięcia 45°, promienie 16 i 32.
- B: dwa otwierające się moduły, odstęp 32 jednostki, różnica wysokości 32, łuki 40/48; dolne krawędzie styczne pod kątem 15° z dokładnością zapisu do dwóch miejsc.
- C: jeden zamknięty kontur z diagonalami 45°, poszerzone dolne światło litery M i wyrównane górne zakończenia.
- [Napis launchme](wordmark-study.svg) jest narysowany jako własne ścieżki. Nie wymaga fontu. Font Segoe UI w HTML służy tylko opisom planszy.
- Źródłowe logo nie zawiera tekstu SVG, rastrów, masek, filtrów ani obrysów wymagających rozwinięcia.

## Weryfikacja

Wykonano dwie iteracje rysowania, renderowania i oceny:

1. `v1`: audyt struktury i plansza dużych znaków z rzeczywistymi próbkami 64/32/16 px.
2. `v2`: korekta poziomu w A, asymetria i styczność łuków w B, otwarcie światła i wyrównanie C; dodane liternictwo i poziome zestawienia.
3. Obejrzane rendery Chromium: czarny/jasny znak, biały/ciemny, 16/20/24/32/48/64 px, odbicie, obrót 180° i rozmycie. Dodatkowo rzeczywiste 16 × 16 px powiększone 8× bez interpolacji.
4. Porównanie obok Steam, Unity, Framer i Pagekit pod kątem charakteru, skali i geometrii. To ograniczony przegląd, a nie badanie unikalności na całym rynku.

Obserwacje: w A oddzielony kafel i litera pozostają rozdzielone przy 16 px; w B zachowuje się szczelina między modułami; C zachowuje inicjały, ale ma najmniejsze światła. Wersje odwrócone mogą dostać subtelną korektę optyczną po wyborze kierunku.

Robocze plansze i testy znajdują się w ignorowanym `build/temp/`:

- `launchme-logo-round-1.png`, `launchme-logo-round-2.png`;
- `launchme-logo-tests.html` i `.png` — obejrzany, zwarty przegląd;
- `launchme-logo-skill-tests.html` — dodatkowy arkusz wygenerowany narzędziem skilla.

Proces i narzędzia: [logo-design-skill](https://github.com/kaankiziltug/logo-design-skill), wersja wskazana w briefie. Audyt SVG ocenia techniczną konstrukcję heurystycznie; nie mierzy jakości pomysłu ani preferencji użytkowników.

## Następny etap

Po wyborze A, B lub C: finalna geometria i kolory, wersje jasna/ciemna, komplet PNG i wielorozmiarowe ICO dla Windows oraz wdrożenie w zasobach i interfejsie launchme.

# launchme — kierunki nowego logo

Etap: koncepcje do wyboru, 2026-10-20.

## Założenia

- Nazwa w znaku: **launchme**, małymi literami, jak w obecnym interfejsie.
- Natywny launcher Windows: wspólna biblioteka wielu sklepów, okno desktopowe i tryb TV sterowany padem.
- Charakter: szybki, uporządkowany, przyjazny, konsolowy, bez agresywnej estetyki esportowej.
- Zastosowania: ikona Windows 16–256 px, pasek aplikacji, ekran TV, README.
- Obecny znak: miętowy trójkąt „play” w ciemnym zaokrąglonym kwadracie. Jest prosty, ale przypomina kontrolkę odtwarzacza; cienka obwódka słabnie przy 16 px.
- Punkt ciągłości: małe litery, ciemne neutralne tło, opcjonalna mięta. Ostateczny kolor po wyborze kształtu.

Podstawa: `README.md`, `src/ui/desktop_renderer.cpp`, istniejący zrzut `build/temp/desktop-library.png` i `res/launchme.ico`.

## Strategia

Obietnica: **Twoje gry. Jeden punkt startu.**

Mapa skojarzeń: launch → odskok, oderwanie, wybór; biblioteka → kafel, półka, wspólny pokój; me → własne miejsce; kanapa → spokój, wygoda, gotowość.

Odrzucone skróty wizualne: rakieta, pad z przyciskami, piorun, przypadkowy neonowy gradient, sam trójkąt play, tarcza esportowa.

## Szkice ideowe

Oceny robocze projektanta: pomysł / odróżnialność / prostota / dopasowanie / mały rozmiar, po 1–5. Nie są wynikiem badań użytkowników.

| Pomysł | Typ | Oceny | Decyzja |
| --- | --- | --- | --- |
| Odskok: od litery L odrywa się aktywny kafel | litera + moduł | 5/4/5/5/5 | A |
| Wspólna biblioteka: dwie kolekcje tworzą otwarte wejście | abstrakcyjny | 4/3/5/5/5 | B |
| LM: nazwa zamknięta w jednym geometrycznym znaku | monogram | 4/4/4/4/4 | C |
| Kadr: aktywny narożnik wyłamuje się z ramki | abstrakcyjny | 3/2/5/4/5 | zbyt podobne do kontrolki powiększania |
| Orbit: gry zbiegają się na jednej orbicie | abstrakcyjny | 3/2/4/4/4 | częsty motyw w kategorii |
| Slot: otwarta szczelina na kartridż | piktogram | 4/3/4/3/4 | zbyt mocna sugestia emulacji |
| Kanapa: dwa podłokietniki i miejsce gracza | piktogram | 4/4/3/3/3 | tryb desktopowy jest równie ważny |
| Start: L zakończone strzałką | litera | 3/2/5/4/5 | zbyt podobne do ikony eksportu |
| Portal: drzwi z wyciętym play | negatyw | 3/2/4/4/4 | nadal kontrolka odtwarzacza |
| Gracz: twarz z dwóch kafli | maskotka | 4/3/4/3/4 | przesuwa akcent na towarzyszkę AI |

## Źródło procesu

Użyto wskazanego przez użytkownika [logo-design-skill](https://github.com/kaankiziltug/logo-design-skill), commit `734090317a2155c5bf1ae8bd4cc85c6848a0128f`.

Badanie biblioteki: kategoria `gaming` zawiera tylko 5 pozycji, więc nie jest pełnym przeglądem rynku. Przejrzano konstrukcję Steam, Unity, Framer i Pagekit; szukano także strzałek i techniki negative-space. Pliki referencyjne służyły do oceny prostoty, geometrii i podobieństwa, nie do kopiowania.

Zgodnie z procesem skilla: najpierw trzy kierunki, render i testy; pełny zestaw ikon i wdrożenie po wyborze kierunku.

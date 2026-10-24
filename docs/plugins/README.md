# Pluginy Launchme — SDK v1 (Windows x64)

Plugin to **ZIP z manifestem `plugin.json` i natywną DLL**. Launcher zapewnia instalację,
aktualizację, włączanie/wyłączanie, ustawienia, nawigację padem i natywne rysowanie UI.
Kod pluginu komunikuje się przez małe ABI C z komunikatami JSON UTF-8.

## Obsługa

**Ustawienia → Pluginy**: przycisk „Zainstaluj plugin z ZIP”, odstęp, a pod nim sekcja
„Dostępne pluginy”. Każdy plugin ma jeden wiersz: nazwa, opis, przełącznik oraz **⚙**.
Ikona otwiera osobną stronę z ustawieniami tego pluginu i odinstalowaniem; nie ma kluczy
ani komend przeplatanych z listą pluginów. Esc/B wraca do listy. Plugin bez ustawień
ma w tej stronie tylko odinstalowanie.

Nowa ręczna instalacja ZIP jest wyłączona; włącz ją przełącznikiem przy nazwie.
Ustawienia są dostępne również przy wyłączonym pluginie.
Ponowna instalacja ZIP o tym samym `id` aktualizuje plugin i zachowuje
zgodne ustawienia oraz stan włączenia. Nie trzeba restartować launchera.

`Odinstaluj` wyrejestrowuje rozszerzenie, wywołuje `deactivate`, zwalnia DLL, usuwa
paczki i zapisane ustawienia. Katalog danych/cache pluginu pozostaje na dysku.
Odinstalowany plugin nie wraca po ponownym uruchomieniu.

Po budowaniu paczki są w `build/vs/plugin-packages/`:

| Paczka | Funkcje |
| --- | --- |
| `launchme.howlongtobeat.zip` | blok pod czasem gry/osiągnięciami w Szczegółach, procent względem głównej fabuły, widget aktywnej gry i szuflada dopasowania; [obsługa](HOWLONGTOBEAT.md) |

**Paczki z `plugin-packages/` obok EXE są przy pierwszym uruchomieniu automatycznie
instalowane i włączone** (nazwa pliku `<id>.zip` musi zgadzać się z `id` manifestu).
Rejestr zapamiętuje tę operację oddzielnie dla każdego ID: późniejsze wyłączenie lub
odinstalowanie nie jest cofane po restarcie. Już zainstalowane pluginy zachowują swój stan.

Nowsza dołączona paczka (numeryczne `major.minor.patch`) aktualizuje istniejącą starszą
wersję przy starcie, zachowując ustawienia i stan włączenia. Nie przywraca odinstalowanego
pluginu i nie obniża wersji zainstalowanej ręcznie.

Pole manifestu `tags` dodaje do kafla etykiety: `widget`, `screen` (ekran), `integration`
(integracja), `menu`, `context` (kontekst), `drawer` (szuflada). Etykiety są opisowe,
bez efektów ubocznych: rejestracja funkcji nadal wynika wyłącznie z `contributions`.

Opcjonalne `icon` wskazuje PNG w paczce (do 256 KiB, ścieżka jak `entry`), a `iconText`
to monogram 1–3 znaków ASCII (litery/cyfry), również jako fallback. Widget rysuje ikonę
w kole. Odczyt ikon odbywa się na workerze pluginów, dekodowanie na workerze obrazów.
Reorganizacja w nagłówku szuflady zmienia rozmiary i kolejność widgetów; układ należy
do hosta, jest wspólny z Guide i nie wymaga zmian po stronie DLL.

`tools/build.ps1` buduje moduły, pakuje ZIP i publikuje gotowy Runtime w `build/vs`.
Roboczy katalog CMake to `build/temp/vs`. Zmiana samego manifestu również odświeża paczkę.
Komponent instalacyjny `Runtime` zawiera EXE, ZIP HowLongToBeat i licencje miniz/PresentMon/WebView2.
Własny plugin dodany do buildu przez `launchme_plugin(... COMPONENT <nazwa>)` trafia do osobnego komponentu.

## Jak zrobić następny plugin

1. Skopiuj [`sdk/examples/hello`](../../sdk/examples/hello). Przykład jest niezależnym
   projektem CMake, bez zależności od bibliotek i struktur C++ launchera.
2. Zmień `id`, nazwę, wersję i rozszerzenia UI w `plugin.json`.
3. Wyeksportuj `launchme_plugin_v1` z nagłówka [`sdk/launchme_plugin.h`](../../sdk/launchme_plugin.h).
4. Zaimplementuj metody opisane w [API.md](API.md), zwracając JSON przez callback `emit`.
5. Zbuduj DLL **x64**, spakuj manifest i DLL w głównym katalogu ZIP, zainstaluj w profilu
   testowym (`LAUNCHME_CONFIG_DIR` ustawione na ścieżkę absolutną).

```powershell
$vs = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
& "$vs\cmake.exe" -S sdk/examples/hello -B build/vs/plugin-hello -G 'Visual Studio 18 2026' -A x64
& "$vs\cmake.exe" --build build/vs/plugin-hello --config Release
# ZIP: build/vs/plugin-hello/Release/example.hello.zip
```

Rzeczywisty przykład: [`plugins/howlongtobeat`](../../plugins/howlongtobeat). C++ i Rust są odpowiednie, jeśli implementują
to samo ABI C; nie jest wymagana zgodność kompilatora C++ ani przekazywanie STL.

## Format i granice v1

- `plugin.json` w korzeniu ZIP, maks. 64 KiB. Schemat: [manifest.schema.json](manifest.schema.json).
- `apiVersion: 1`, `architecture: "windows-x64"`, `entry` wskazuje DLL wewnątrz paczki.
- Nowa paczka nie wykonuje kodu podczas instalacji. Host sprawdza manifest oraz PE/x64;
  zgodność eksportu ABI i `activate` sprawdza dopiero podczas włączania. Przy aktualizacji
  aktywnego pluginu nowy moduł jest sprawdzany przed zatwierdzeniem zmiany.
- ZIP: maks. 64 MiB, 256 wpisów, 32 MiB na plik i 64 MiB łącznie po rozpakowaniu;
  stored/deflate, weryfikacja CRC. Bez haseł, dowiązań, ścieżek absolutnych, `..`, ADS,
  nazw urządzeń Windows i duplikatów ścieżek (także różniących się tylko wielkością liter).
- Ścieżki wewnątrz ZIP: ASCII litery/cyfry, `-`, `_`, `.`, `/`; bez końcowych kropek,
  pustych segmentów i segmentu `.`. Maks. 200 znaków. Nazwa ZIP i katalog profilu mogą mieć Unicode.
- Maks. 64 zainstalowane pluginy, 32 ustawienia i 12 rozszerzeń na manifest.
- Sloty UI: pierwsze **4** pozycje pluginów w dolnym menu, **3** pasujące akcje kontekstowe
  i **64** widgety w przewijanej szufladzie, pierwszy pasujący dostawca `details` w Szczegółach.
  Widget może deklarować `activeGame: true`. Domyślna kolejność: `id` pluginu, potem kolejność manifestu.
  Manifest powinien deklarować tylko potrzebne sloty; nadmiar nie mieści się w UI v1.
- `catalog` w v1 obsługuje gry identyfikowane przez Steam AppID; zwykłe listy pluginów
  mogą zawierać dowolne odnośniki HTTP(S). Nie ma własnego HTML, renderowania Direct2D
  przez DLL, hooków klatek, hotkeyów ani publicznego serwera HTTP.

Pluginy natywne działają w procesie launchera z uprawnieniami użytkownika — **to nie
jest sandbox**. `library: true` kontroluje przekazywanie danych przez SDK, nie uprawnienia
systemowe DLL. Błędny JSON/API wyłącza dany moduł; awaria pamięci lub zawieszony kod
natywny nie mogą być bezpiecznie izolowane przez ten host. Autor powinien ograniczać
czas I/O, nie blokować `deactivate` i nie zostawiać własnych wątków przy zwalnianiu DLL.

## Dane i cykl życia

W `%LOCALAPPDATA%\Launchme\plugins` (lub profilu `LAUNCHME_CONFIG_DIR`):

```text
registry.dat                    # atomowy rejestr i ustawienia, zaszyfrowane DPAPI
packages/<id>/<revision>/        # niezmienne wydanie ZIP, manifest i DLL
data/<id>/                      # dane/cache należące do pluginu
```

Instalacja rozpakowuje do nowego katalogu staging, waliduje, a następnie atomowo zmienia
rejestr. Błąd ekstrakcji, ABI aktywnej aktualizacji lub zapisu pozostawia poprzednie
wydanie i ustawienia aktywne. Stare pliki wydania są usuwane po zwolnieniu jego DLL.
Zapis sekretów jest szyfrowany DPAPI; snapshot UI zawiera tylko informację o ich obecności.

Cała ekstrakcja, ładowanie i wywołania DLL odbywają się na jednym serializowanym workerze.
UI używa snapshotów; wyniki starej generacji po zmianie stanu pluginu są odrzucane.
Wyłączenie nie przerywa siłą trwającej funkcji DLL: czeka na jej powrót na workerze,
a następnie wywołuje `deactivate` i zwalnia moduł. UI w tym czasie pozostaje responsywne.
Podczas aktualizacji dwie rewizje mogą być przez chwilę załadowane; `activate` powinno
przygotowywać zasoby, a nie wykonywać nieodwracalnych operacji.

## Testy

- `native_plugin_package_lifecycle`: rzeczywiste DLL, instalacja/aktualizacja/usunięcie ZIP,
  aktywacja/dezaktywacja, restart, DPAPI, ustawienia, niezgodne ABI, rollback,
  niebezpieczne nazwy, uszkodzone paczki i błędne odpowiedzi.
- `native_settings_store_details_flow`: prawdziwy ZIP/DLL HowLongToBeat, natywne ustawienia
  i lista pluginów; mock wykonawcy URI, bez uruchamiania przeglądarki.
- `plugin_library_supplement_merge`: deduplikacja, pierwszeństwo instalacji/kolekcji, ukrycie,
  filtr niezainstalowanych, wyłączenie/usunięcie pluginu i odrzucanie starych wyników.
- `howlongtobeat_search_and_plugin_cache`: parser, cache i prawdziwa DLL HowLongToBeat bez sieci.

Nie jest potrzebny OpenAPI: interfejs nie jest HTTP. Kontrakty maszynowe to JSON Schema
([manifest](manifest.schema.json), [komunikaty](api.schema.json)) oraz nagłówek ABI C.

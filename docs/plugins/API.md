# Kontrakt SDK v1

## ABI i własność pamięci

Eksport DLL: `const LaunchmePluginV1* __cdecl launchme_plugin_v1(uint32_t host_abi)`.
Zwróć stabilny wskaźnik do tabeli `{size, abi, invoke}` albo `NULL`, gdy wersja hosta
nie jest obsługiwana. `size >= sizeof(LaunchmePluginV1)`, `abi == 1`.

```c
int32_t invoke(const char* json, uint32_t length, LaunchmeEmitV1 emit, void* context);
void emit(void* context, const char* json, uint32_t length);
```

- UTF-8; długości w bajtach, bez NUL, limit request/response 2 MiB.
- `invoke` zwraca 0; wywołuje `emit` **dokładnie raz, synchronicznie**, przed powrotem.
- Host kopiuje odpowiedź podczas callbacku. Bufory wejściowe i `context` są ważne tylko
  podczas wywołania. Nie zachowuj ich ani nie wywołuj `emit` z innego wątku.
- Każda strona zarządza własną pamięcią. Bez STL, wyjątków, HWND i alokacji CRT przez ABI.
- Wszystkie wywołania DLL są serializowane na workerze. Bez reentrancji do UI.
- Kod ≠ 0, brak/wielokrotny callback, niepoprawny JSON lub `error` oznaczają błąd
  protokołu: host dezaktywuje plugin i zapisuje stan wyłączony.

## Żądanie

```json
{
  "method": "command",
  "locale": "pl",
  "settings": {"api_key": "...", "available_only": false},
  "params": {
    "command": "browse",
    "refresh": false,
    "game": {"key": "steam:620", "name": "Portal 2", "source": "steam", "appId": 620,
             "installed": true, "available": false, "shortcut": false}
  }
}
```

Ustawienia są aktualnym snapshotem przy **każdym** wywołaniu. Ich zmiana nie wymaga
restartu DLL ani osobnego eventu. `locale` to `pl` lub `en`; lifecycle używa `en`.

| Metoda | `params` | Odpowiedź |
| --- | --- | --- |
| `activate` | `dataDirectory`: absolutna ścieżka UTF-8 do istniejącego katalogu pluginu | `{}` |
| `deactivate` | `{}`; ustawienia mogą być puste | `{}`; zwolnij zasoby i zakończ własne wątki |
| `command` | `command`, `game`, `refresh`; opcjonalnie `library` | efekty poniżej |
| `search` | `query`: tekst wpisany w katalogu | `games`, opcjonalne `message` |
| `widget` | `command`; opcjonalnie `library` | `text`: krótki tekst widgetu |
| `details` | `command`, `game`, `refresh`, `playtimeSeconds` | `comparison`: referencyjne czasy gry |
| `library` | `command` z rozszerzenia `library` | `steamAppIds`: uzupełniające identyfikatory gier |
| `downloads` | `command` z rozszerzenia `downloads` | `downloads`: pełny snapshot zadań, bez efektów UI |

`activate` jest wywoływane przy włączeniu, starcie z włączonym pluginem oraz walidacji
aktywnej aktualizacji. Nawet po częściowo nieudanym `activate` host próbuje `deactivate`.
Nowo zainstalowany wyłączony plugin nie dostaje żadnego z tych wywołań.

`widget` jest odpytywany przy otwieraniu szuflady, nie w każdej klatce. Kliknięcie widgetu
wysyła **`command` z tym samym identyfikatorem komendy**. Do odświeżenia otwartej listy
służy F5: powtarza jej komendę z `refresh: true`.
Odpowiedź `command` może również zawierać `text`: odświeża wtedy tekst widgetu,
z którego otwarto widok.

`details` to odczyt, bez efektów URI/otwierania okien. Host odpytuje dostawcę w tle
przy otwarciu Szczegółów albo dla aktywnego widgetu, z cache do 60 s. Zmiana gry,
języka lub wersji pluginu ma własną tożsamość odpowiedzi.

### Snapshot biblioteki

Manifest z `"library": true` dostaje `params.library` przy `command` i `widget` — tablicę
DTO w formacie `game` z przykładu. Zawiera publiczne gry, również niezainstalowane;
bez ukrytych/prywatnych wpisów, tokenów kont, ścieżek dyskowych i danych osobowych.
`shortcut: true` oznacza skrót non-Steam/emulację. `appId` jest identyfikatorem Steam
tylko dla prawdziwych gier Steam; nigdy nie używaj go zamiast `key` dla innych źródeł.
Budżet serializacji biblioteki to 1500 KiB; bardzo duży snapshot może być skrócony.

## Rozszerzenia manifestu

```json
{"id":"notes","kind":"widget","title":{"pl":"Notatki","en":"Notes"},
 "text":{"pl":"Otwórz katalog","en":"Open catalog"},"command":"browse"}
```

`id` jest lokalne dla pluginu, `command` to identyfikator przekazywany DLL. Teksty mogą
być jednym stringiem albo obiektem `{pl, en}`. `text` widgetu stanowi treść początkową.

Opcjonalne `tags` manifestu: tablica do 6 unikalnych wartości `widget`, `screen`,
`integration`, `menu`, `context`, `drawer`, `details`. Host tłumaczy je i rysuje jako etykiety
na kaflu pluginu. Nie wpływają na routing, dostęp do biblioteki ani uprawnienia.

- `menu`: dolny pasek.
- `menu` może ustawić `glyph: "server-download"` (serwer ze strzałką) lub `"store"`
  (domyślna torba sklepu). Symbol zachowuje kontrast pod przesuwanym fokusem.
- `context`: menu gry. Opcjonalny `source` (`steam`, `gog`, `epic`, `ea`, `battlenet`,
  `ubisoft`, `xbox`, `custom`, `drmfree`) filtruje gry. `steam` pomija skróty non-Steam. Akcje
  kontekstowe nie pojawiają się na pozycjach katalogu.
- `widget`: dodatkowy kafel prawej szuflady, tekst + kliknięcie.
- `widget` z `"activeGame": true`: dostaje `params.game` i `params.playtimeSeconds`
  dla potwierdzonej aktywnej gry. Bez gry DTO ma pusty klucz. Odświeżanie przy
  zmianie gry i co 5 s w otwartej szufladzie; kliknięcie używa tej samej aktywnej
  gry. W Guide wybiera grę pod nakładką. Snapshot nie zawiera ścieżek ani kont.
- `details`: jeden dostawca na plugin; pierwszy pasujący dostaje natywny blok
  pod czasem gry i osiągnięciami. Kliknięcie wywołuje jego `command` z grą.
- `catalog`: jeden na plugin; `command` i `title` definiują akcję główną katalogu.
  Do otwarcia katalogu inna komenda zwraca `openCatalog`.
- `downloads`: jeden na plugin. Host odpytuje metodę `downloads` co sekundę niezależnie
  od otwartej strony. Zadania widoczne są we wspólnych „Pobieraniach”.
- `library`: jeden na plugin, z `source: "steam"`; dodatkowe identyfikatory gier,
  bez własnego slotu UI. To odrębna funkcja od flagi `library: true` przekazującej snapshot do DLL.

## Odpowiedzi i efekty

### Zadania pobierania

```json
{"downloads":[{"id":"17","title":"Gra","phase":"converting","status":"Rozpakowywanie",
 "destination":"D:\\Pobrane\\gra.zip","fraction":0.6,"command":"job.17"}]}
```

Pełny snapshot (do 1000 pozycji), stabilne `id` w obrębie pluginu. Etapy: `queued`,
`downloading`, `converting`, `verifying`, `paused`, `done`, `failed`, `cancelled`. `fraction` 0–1 dotyczy bieżącego
etapu; brak/null = postęp nieznany. `destination` to aktualny plik wynikowy. Kliknięcie
wiersza wysyła `command` do właściciela; odpytywanie nie zmienia strony ani fokusu.
Wyłączenie/usunięcie pluginu usuwa jego snapshot; odpowiedzi starej epoki są ignorowane.

Komenda dodająca zadania zwraca `queued: {"count":1,"title":"Gra"}` tylko po faktycznym
dodaniu, co daje wspólny toast „Dodano do kolejki pobierania”. Może też zwrócić `downloads`,
żeby zarejestrować od razu nawet bardzo krótkie zadania. Obserwowane przejście do `done`
daje toast „Pobieranie zakończone”, z tytułem i dostępną okładką. Sam pierwszy snapshot
historii zakończonych zadań, usunięcie zadania, `failed` ani `cancelled` nie oznaczają sukcesu.
Konwersja jest częścią zadania: plugin deklaruje `done` dopiero po jej sukcesie.

Odpowiedź jawnej komendy może zawierać `revealFile` ze ścieżką absolutną. Host otwiera
folder w Eksploratorze i zaznacza istniejący plik (PIDL, bez składania polecenia powłoki).
F5, odświeżanie w tle i spóźnione odpowiedzi nie wykonują tego efektu.

Opcjonalne pola `artworkName`, `platform`, `titleId` opisują grę dla miniatury listy/toastu.
Aktualizacje i DLC przekazują nazwę i Title ID gry bazowej. Host korzysta z cache, potem
SteamGridDB dla `PC`/`Windows` (tylko z kluczem) albo ScreenScraper dla znanego systemu
(tylko z konfiguracją). Brak platformy nie oznacza PC. Lookup nie skanuje plików ROM
i nie dodaje pozycji do biblioteki. Sieć, odczyt kluczy, zapis i dekodowanie są poza UI.

### Uzupełnienie biblioteki

```json
{"steamAppIds":[400,620]}
```

Wyłącznie wskazówki identyfikacyjne, **nie źródło informacji o posiadaniu lub instalacji**.
Host scala je z grami wykrytymi normalnie, deduplikuje po AppID, respektuje ukrycie/prywatność
i filtr niezainstalowanych. Manifesty Steama mają pierwszeństwo. Nazwy i metadane pobiera
istniejący mechanizm hosta; plugin nie podaje ani nie nadpisuje tych danych.

Metoda jest odpytywana na workerze po odczycie biblioteki (także po powrocie do okna),
po włączeniu/aktualizacji pluginu i zmianie ustawień. Host wywołuje ją tylko dla aktywnego
rozszerzenia i znanego konta Steam. Wyniki są przypisane do pluginu, konta i generacji.
Wyłączenie, usunięcie lub błąd pluginu usuwa tylko jego wskazówki; gry nadal obecne
w manifestach lub kolekcjach pozostają. Zmiana konta odrzuca poprzedni snapshot.

Maks. 20 000 całkowitych AppID z zakresu 1–2147483647 (bez identyfikatorów skrótów).
Pusta tablica zastępuje wcześniejsze wskazówki pustym zbiorem. Przejściowy błąd odczytu
zwraca samo `message`, bez `steamAppIds`: host zachowuje poprzedni snapshot do kolejnego
odczytu. Efekty URI/otwierania okien nie są obsługiwane przez tę metodę.

### Porównanie czasu gry

```json
{"comparison":{"gameKey":"steam:620","mainSeconds":28800,
 "extraSeconds":43200,"completionSeconds":72000,"status":""}}
```

Sekundy są liczbami całkowitymi 0–4294967295; brak pola lub 0 = brak referencji.
`gameKey` musi odpowiadać grze żądania. `status` (do 1024 znaków) opisuje brak danych
lub potrzebę wybrania dopasowania. Host rysuje główną fabułę wobec własnego czasu
i oblicza procent bez ograniczania wartości do 100%; pasek mieści się w 100% szerokości.
Przy nieznanym liczniku/referencji procent pozostaje nieznany. `playtimeSeconds`
żądania to aktualny czas hosta lub `null`; jest dostępny także w `command`.

Odpowiedź `comparison` jest wspólna dla `details`, aktywnego `widget` i `command`
(np. po ręcznym dopasowaniu). Odświeżanie samego widgetu nie przedłuża cache
dostawcy. Komendy pozycji szuflady i F5 zachowują grę, dla której otwarto szufladę,
nawet jeśli w tle zmieniła się aktywna sesja. Automatyczny widget nie powinien
zmieniać prywatnego stanu dopasowania otwartej strony.

### Powiadomienie / URI

```json
{"message":"Operacja zakończona"}
```
```json
{"uri":"https://example.org/game/portal-2","message":"Otwarto stronę"}
```

Efekty są wykonywane na UI po sprawdzeniu generacji i stanu pluginu. `uri` musi mieć
schemat; nie może być ścieżką EXE, zawierać spacji, cudzysłowu ani NUL/CR/LF.
`file:`, `javascript:` i `data:` są odrzucane. `message` wraz z URI jest wyświetlane
dopiero po pomyślnym przekazaniu do systemu. Błędy sieci/użytkownika zgłaszaj przez
`message`, a **nie** `error` (które wyłącza plugin).

### Katalog Steam

```json
{"openCatalog":true}
```

Host otwiera wyszukiwarkę właściciela; debounce 400 ms, stare wyniki są odrzucane.
Odpowiedź `search` (maks. 100 pozycji):

```json
{"games":[{"appId":620,"name":"Portal 2","available":true}],"message":""}
```

Host rozstrzyga lokalną instalację z własnej biblioteki, wyświetla grafiki i Szczegóły.
`available` decyduje o dostępności akcji katalogowej. `command` tej akcji dostaje grę,
zwraca np. URI protokołu. Host blokuje duplikaty kliknięcia, a po poprawnym handoffie
zapamiętuje potwierdzenie i udostępnia instalację Steam. Wyłączenie pluginu usuwa jego
wejścia do UI i zamyka aktywny katalog; metadane zwykłych gier nadal działają.

### Widget

```json
{"text":"12 do Twoich gier"}
```

Wyświetlane jest do 256 znaków. Nie wykonuj akcji zewnętrznych w `widget`; ich efekty
nie są obsługiwane w tej metodzie.

### Natywna lista: ekran lub szuflada

Odpowiedź `drawer` ma ten sam format co `page` poniżej, ale podmienia zawartość prawej
szuflady, tak jak lista znajomych. Biblioteka pozostaje w tle z niezmienioną pozycją
i grafikami. Esc/B wraca do widgetów; kolejne Esc/B zamyka szufladę. Miniatury szuflady
mają własne sloty renderera (nie nadpisują kart biblioteki).

```json
{
    "page": {
    "title": "Notatki",
    "status": "",
    "sections": [
      {"title":"Do Twoich gier","empty":"Brak dopasowań","items":[
        {"title":"Portal 2","subtitle":"Portal 2","gameKey":"steam:620",
         "uri":"https://example.org/gra/portal_2"}
      ]},
      {"title":"Wszystkie notatki","items":[
        {"title":"Portal 2","uri":"https://example.org/gra/portal_2"}
      ]}
    ]
  }
}
```

Kolejność sekcji i pozycji ustala plugin. `gameKey` dodaje małą kwadratową grafikę z
publicznej biblioteki launchera. Nieznany/prywatny klucz nie pozwala odczytać grafiki.
Alternatywnie `downloadId` wskazuje zadanie w snapshotcie `downloads` tego samego pluginu.
Wiersz korzysta ze wspólnej miniatury „Pobierań”, także gdy dotrze ona asynchronicznie.
Nieznane ID daje placeholder; nie rozwiązuje się go przez zadania innego pluginu.
`gameKey` i `downloadId` wzajemnie się wykluczają. Bez obu pól wiersz nie ma miniatury.
Nie przekazuje się własnych plików obrazów.

Pozycja może zamiast `uri` zawierać `command`, np.:

```json
{"title":"Załaduj","command":"load","enabled":true}
```

`command`: 1–64 znaki ASCII `a-zA-Z0-9_.-`, wysyłane wyłącznie do właściciela
strony jako metoda `command`. Nie wymaga osobnego slotu w `contributions`.
`uri` i `command` wzajemnie się wykluczają. Bez obu pól wiersz jest informacyjny.
`enabled: false` wyłącza akcję, pomija ją w nawigacji i hit-teście. Brak pola oznacza
`true`. Powtórne kliknięcie podczas trwającej komendy jest pomijane.
F5 nadal wywołuje komendę **otwierającą** widok, a nie ostatnią akcję wiersza.

Kliknięcie/Enter/A wykonuje komendę albo otwiera `uri` HTTP(S) w domyślnej przeglądarce.
Strzałki/D-pad oraz kółko przewijają, nagłówki i wiersze informacyjne są pomijane
przez fokus; Esc/B wraca do ekranu głównego.
Limit: 8 sekcji, 4000 wierszy łącznie (wliczając nagłówki), tytuł strony 256 znaków,
tytuł pozycji 512, opis 1024, URI 2048. Grafiki są pobierane tylko dla widocznych pozycji.

Opcjonalne pola strony (dodane w v1, starsze pluginy ich nie potrzebują):

```json
{"title":"Katalog","back":"back","refresh":1,
 "search":{"command":"find","placeholder":"Szukaj nazwy","query":"portal"},
 "sections":[...]}
```

- `search`: pole wyszukiwania nad listą (tylko ekran, w szufladzie pomijane). Fokus
  startuje na pierwszym wierszu listy, a pole jest nad nim (↑ albo X na padzie, który
  otwiera też klawiaturę ekranową). Wpisywanie jest lokalne; Enter / „Gotowe” wysyła
  `command` z `params.query` (do 100 znaków). `query` to tekst pokazany w polu.
  Backspace w pustym polu działa jak Esc/B.
- `back`: komenda (format jak `command` pozycji) wysyłana zamiast wyjścia z widoku przez
  Esc/B, przycisk Wstecz i Backspace w pustym polu — poziom wyżej wewnątrz pluginu.
  Bez pola Esc/B zamyka widok jak dotąd.
- `dialog: true`: wyśrodkowany panel decyzji. Nawigacja, przewijanie i hit-test używają
  geometrii panelu; `back` pozwala pominąć opcjonalne wybory bez anulowania pobierania.
- `refresh`: 1–60 s. Dopóki strona jest widoczna, host co tyle powtarza komendę
  otwierającą z `refresh: true` w tle (np. postęp pobierania). Nie blokuje to wierszy:
  wybrana w tym czasie komenda ma pierwszeństwo, a spóźniona odpowiedź odświeżenia
  jest pomijana. Pomijane podczas pisania w polu i przy otwartej klawiaturze.
  Odpowiedź F5 lub odświeżenia z tym samym tytułem zachowuje fokus (lub najbliższy
  aktywny wiersz), przewinięcie i wpisywany tekst. Plugin pamięta, który poziom pokazuje.

## Ustawienia

Typy `boolean`, `choice`, `text`, `secret`, `folder`; każde ma `id`, `title`, `default`.
`choice` ma maks. 12 wpisów `{value,title}` i default równy jednej z wartości.
Tekst/sekret/folder: do 512 bajtów UTF-8. Host waliduje typy, obsługuje kontrolki i zapisuje
ustawienia atomowo. Sekret jest maskowany i podczas ponownej edycji pole jest puste;
zatwierdzenie pustego pola czyści wartość. Snapshot ustawień UI nigdy nie zawiera sekretu.
`folder` to wiersz ze ścieżką i przyciskiem „Wybierz”/„Zmień”, który otwiera przeglądarkę
folderów launchera (pad, klawiatura, mysz, okno Windows); wartością jest ścieżka absolutna
albo pusty tekst. Plugin sprawdza ją sam przed użyciem.

## Kompatybilność

Wersja semantyczna pluginu (`version`) jest metadanymi paczki; instalacja tego samego
ID może też być downgrade'em. Wersja API/ABI (`apiVersion`, `abi`) to oddzielny kontrakt.
Host v1 odrzuca inne wersje. Nie zmieniaj znaczenia istniejących pól v1; przyszłe
niekompatybilne rozszerzenia wymagają nowej wersji. JSON Schema opisuje komunikaty,
a walidacja ścieżek, odpowiedzi oraz ograniczenia UI pozostają również w kodzie hosta.

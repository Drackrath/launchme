# HowLongToBeat

Osobny plugin `launchme.howlongtobeat` **1.1.0**, paczka `launchme.howlongtobeat.zip`.
Budowany i dołączany razem z launcherem, domyślnie włączony przy pierwszej instalacji.

## Obsługa

**Szczegóły gry**: blok HowLongToBeat pod czasem gry i osiągnięciami pokazuje
`Twój czas / główna fabuła`, procent i pasek porównania. Przykład: `2 godz. / 8 godz. · 25%`.
Procent = nasz czas / czas głównej fabuły × 100; może przekroczyć 100%.
To porównanie czasu, nie rzeczywisty postęp fabuły. Brak czasu bazowego lub
własnego czasu to „—”, bez zgadywania procentu.

**Widget aktywnej gry** w prawej szufladzie pokazuje jej nazwę, procent i skrócone
czasy (np. `150%`, `3h / 2h`). Dotyczy gry uruchomionej, nie zaznaczonej w bibliotece.
W Guide pierwszeństwo ma gra pod nakładką; na pulpicie najnowsza potwierdzona,
niewstrzymana sesja. Bez aktywnej gry pokazuje odpowiedni stan. Rozmiar i pozycję
można zmienić standardową reorganizacją widgetów.

Kliknięcie/Enter/A na bloku lub widżecie otwiera szufladę z naszym czasem, główną
fabułą, procentem, fabułą z dodatkami i ukończeniem 100%. „Zmień dopasowanie”
pozwala wybrać właściwy tytuł; pojedyncze dokładne dopasowanie jest wybierane
automatycznie, nie nadpisuje ręcznego wyboru. Odnośnik otwiera stronę gry, F5
odświeża dane. Dawna pozycja HowLongToBeat w menu kontekstowym została zastąpiona
blokiem Szczegółów i widgetem.

Nie wymaga konta ani klucza. Wysyła do HowLongToBeat nazwę wybranej/aktywnej gry;
nie pobiera całej biblioteki. Pobieranie odbywa się na workerze pluginów.
Wybór oraz wyniki są zapisywane w `plugins/data/launchme.howlongtobeat/` pod
zakodowanym kluczem gry. Cache jest ważny 7 dni; przy błędzie sieci pozostaje
ostatnia poprawna kopia. Zapisane dopasowanie jest odświeżane po ID gry.
Widget czyta cache; pobieranie zleca osobny kanał danych pluginu, bez HTTP w klatce
renderowania. Referencje są odpytywane najwyżej raz na minutę, a wartości widgetu
odświeżane co 5 s i przy zmianie gry. Procent korzysta z aktualnego licznika hosta.
Steam: lokalny licznik klienta jako baza odczytana przy utworzeniu sesji plus czas
zaobserwowany po odczycie bazy, bez
podwójnego dodawania. Gry poza Steam: zapisany licznik Launchme. Quick Resume
nie zwiększa czasu. Zmiana aktywnej gry odrzuca spóźnione wyniki dla poprzedniej.

## Implementacja i weryfikacja

- `plugins/howlongtobeat/hltb.*`: odczyt publicznego protokołu wyszukiwarki,
  wykrycie aktualnej ścieżki z JS strony, anonimowa inicjalizacja wyszukiwania,
  parser JSON wyników i `__NEXT_DATA__` strony gry. Bez wykonywania pobranego JS.
- `plugin.cpp`, `service.*`: kontrakt SDK v1, wybór dopasowania, cache i izolowany
  kontekst otwartej szuflady (odświeżenie widgetu B nie zmienia wyboru dla gry A).
- `src/app/plugin_game_views.cpp`, `src/plugins/time_comparison.hpp`: ogólne sloty
  `details`, widgety `activeGame`, dane czasowe i porównanie z licznikiem hosta.
- Zmiana protokołu serwisu może wymagać aktualizacji pluginu; błąd sieci nie
  wyłącza go i nie zastępuje poprawnego cache pustymi danymi.
- `howlongtobeat_search_and_plugin_cache`: test offline z atrapą HTTP oraz
  rzeczywistą DLL i zapisem dopasowania, automatyczny wybór, 0/brak danych/>100%,
  dwa równoległe konteksty gier.
- `hltb_details_and_active_game_widget`: natywne rysowanie, przycisk bloku,
  porównanie pod osiągnięciami, widget aktywnej gry, Steam bez podwójnego czasu,
  wstrzymanie, stare odpowiedzi i wyłączenie pluginu. Zrzuty `hltb-details.png`
  i `hltb-active-widget.png` pod `build/temp`.
- Ręcznie: `build/temp/vs/Release/howlongtobeat_tests.exe --live`.
  Sprawdzono rzeczywiste wyszukiwanie Portal 2 oraz stronę gry 7231.

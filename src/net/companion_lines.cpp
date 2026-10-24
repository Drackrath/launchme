// Her offline voice: what she says when no model answers. One Voice per character (indexed by
// Persona) holds her greetings, chat openings, answers and a goodnight line; reactions to
// launcher events and the roulette use shared sentences, coloured by the character's sign-off.
// Every line is tagged like a model reply and goes through parseText.
#include "companion_events.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <random>

namespace companion {
namespace {
using Lines = std::array<const wchar_t*, 3>;
struct Language {
    Lines home, chat;
    const wchar_t* answer; // A typed message while offline.
    const wchar_t* empty;  // No games in the library.
    const wchar_t* night;  // The home greeting late at night.
    const wchar_t* sign;   // Appended to event and roulette remarks ("" = none).
};
struct Voice {
    Language pl, en;
    std::optional<Emotion> deadpan; // Shows this face for every event (kuudere: always cool).
};

const std::array<Voice, persona_count> voices{{
    // Genki
    {{{L"[excited]Hej hej! ‹Gotowy|Gotowa› na granie? [smile]Może dziś [[{A}]]?",
       L"[smile]Witaj z powrotem, senpai! [[{R}]] tęskni... a może coś nowego, jak [[{A}]]?",
       L"[thinking]Hmm, co by tu odpalić... [excited]Mam! [[{A}]]!"},
      {L"[excited]Yahallo! Wiesz, że kapibary grałyby tylko w RPG-i, bo są takie chillowe? [smile]Ale do rzeczy: może [[{A}]] albo [[{B}]]?",
       L"[smile]Senpai! Właśnie wymyśliłam grę: zgadnij, ile razy dziś mrugnę. [excited]Za trudne? To odpal [[{A}]] albo [[{B}]]!",
       L"[thinking]Gdyby pixele miały uczucia, to [[{R}]] by za tobą płakało. [excited]Albo spróbuj [[{A}]]!"},
      L"[thinking]Hmm, coś słabo słyszę — nie mam połączenia z Foundry. [smile]Ale [[{A}]] to zawsze dobry pomysł!",
      L"[shock]Ojej, jeszcze żadnych gier! [smile]Dodaj je w Ustawieniach i gramy!",
      L"[sleepy]Uaaa, już tak późno, senpai! [excited]Jeszcze jedna runda [[{A}]] i spać?",
      L"Hehe!"},
     {{L"[excited]Hey hey! Ready to play? [smile]Maybe [[{A}]] today?",
       L"[smile]Welcome back, senpai! [[{R}]] misses you... or something new, like [[{A}]]?",
       L"[thinking]Hmm, what to launch... [excited]Got it! [[{A}]]!"},
      {L"[excited]Yahallo! Did you know capybaras would only play RPGs because they're so chill? [smile]Anyway: maybe [[{A}]] or [[{B}]]?",
       L"[smile]Senpai! I invented a game: guess how many times I blink today. [excited]Too hard? Then launch [[{A}]] or [[{B}]]!",
       L"[thinking]If pixels had feelings, [[{R}]] would be crying for you. [excited]Or try [[{A}]]!"},
      L"[thinking]Hmm, I can't hear well — no connection to Foundry. [smile][[{A}]] is always a good idea though!",
      L"[shock]Whoa, no games yet! [smile]Add some in Settings and let's play!",
      L"[sleepy]Yaaawn, it's so late, senpai! [excited]One more round of [[{A}]] and then bed?",
      L"Hehe!"},
     std::nullopt},
    // Tsundere
    {{{L"[angry]Wreszcie jesteś! N-nie żebym czekała... [smug]Znowu [[{R}]]? Serio?",
       L"[smug]Gry, których nawet nie ‹włączyłeś|włączyłaś›: {N}. [angry]Baka! Zacznij od [[{A}]].",
       L"[shy]H-hej. Skoro już tu jesteś, zagraj w [[{A}]]. Tylko nic sobie nie myśl!"},
       {L"[smug]Och, to ty. [angry]Nie myśl, że na ciebie czekałam! [shy]...Jak już musisz, zagraj w [[{A}]] albo [[{B}]].",
       L"[angry]Znowu [[{R}]]? Ile można! [smug]Masz tyle gier, a grasz w kółko w to samo. Spróbuj [[{A}]], baka.",
       L"[shy]N-nie czekałam na ciebie, po prostu tu stałam. [angry]I nie patrz tak! [smug]Zagraj w [[{A}]], może wreszcie coś skończysz."},
      L"[angry]Nie mam teraz połączenia, więc nie licz na mądre odpowiedzi! [smug]Idź zagrać w [[{A}]].",
      L"[angry]Twoja biblioteka jest PUSTA? [smug]Dodaj gry w Ustawieniach, baka!",
      L"[sleepy]Uaa... która to godzina?! [angry]Idź spać, baka! [shy]...No dobra, jedna runda [[{A}]].",
      L"N-nie żeby mnie to obchodziło!"},
     {{L"[angry]Finally! N-not that I was waiting... [smug]Back to [[{R}]]? Seriously?",
       L"[smug]You own {N} games you never even started. [angry]Baka! Begin with [[{A}]].",
       L"[shy]H-hi. Since you're here, play [[{A}]]. Don't get the wrong idea!"},
       {L"[smug]Oh, it's you. [angry]Don't think I was waiting! [shy]...If you must, play [[{A}]] or [[{B}]].",
       L"[angry][[{R}]] again? Seriously! [smug]You own so many games and replay the same one. Try [[{A}]], baka.",
       L"[shy]I-I wasn't waiting for you, I was just standing here. [angry]Don't look at me like that! [smug]Play [[{A}]], maybe you'll finally finish something."},
      L"[angry]I'm not connected right now, so don't expect clever answers! [smug]Go play [[{A}]].",
      L"[angry]Your library is EMPTY? [smug]Add some games in Settings, baka!",
      L"[sleepy]Yawn... what time is it?! [angry]Go to sleep, baka! [shy]...Fine, one round of [[{A}]].",
      L"N-not that I care!"},
     std::nullopt},
    // Yandere
    {{{L"[playful]‹Wróciłeś|Wróciłaś›... do mnie. [smile]‹Grałeś|Grałaś› beze mnie w [[{R}]]? Wybaczam~",
       L"[smile]Patrzyłam na twoją bibliotekę całą noc~ [playful]Zagraj dziś w [[{A}]]. Ze mną.",
       L"[shy]Liczyłam sekundy do twojego powrotu... [smug]Gry czekają, ale ja czekałam dłużej. [playful]Zacznij od [[{A}]]~"},
      {L"[playful]Znowu jesteśmy tylko my ‹dwoje|dwie›~ [smile]Mam dla ciebie prezent: [[{A}]] i [[{B}]] czekają. [smug]A [[{R}]]? Mam nadzieję, że nie lubisz jej bardziej ode mnie.",
       L"[smile]Wiedziałam, że przyjdziesz. Zawsze wiem~ [playful]Wybrałam ci [[{A}]]. Nie odmawia się mi, prawda?",
       L"[shy]Ułożyłam twoje gry od tych, które kochasz najmniej... [playful]Na szczycie jest [[{R}]]. Zazdroszczę jej. Zagraj lepiej w [[{A}]]~"},
      L"[sad]Nie słyszę cię... ktoś przeciął nasze połączenie. [playful]Kto to zrobił? [shy]Zagrajmy w [[{A}]], póki nie wróci.",
      L"[playful]Żadnych gier... więc ‹zostaliśmy|zostałyśmy› tylko my ‹dwoje|dwie›~ [smile]Na zawsze.",
      L"[sleepy]Nie śpisz? Ja też nie... [playful]ja nigdy nie śpię~ [smile]Zagrajmy razem w [[{A}]].",
      L"Tylko nie zapominaj o mnie~"},
     {{L"[playful]You came back... to me. [smile]Played [[{R}]] without me? I forgive you~",
       L"[smile]I watched your library all night~ [playful]Play [[{A}]] today. With me.",
       L"[shy]I counted the seconds until you returned... [smug]Your games are waiting, but I waited longer. [playful]Start with [[{A}]]~"},
      {L"[playful]It's just the two of us again~ [smile]I have a gift: [[{A}]] and [[{B}]] are waiting. [smug]And [[{R}]]? I hope you don't like it more than me.",
       L"[smile]I knew you'd come. I always know~ [playful]I picked [[{A}]] for you. You don't say no to me, right?",
       L"[shy]I sorted your games from the ones you love least... [playful][[{R}]] is on top. I'm jealous. Play [[{A}]] instead~"},
      L"[sad]I can't hear you... someone cut our connection. [playful]Who did it? [shy]Let's play [[{A}]] until it's back.",
      L"[playful]No games... so it's just you and me now~ [smile]Forever.",
      L"[sleepy]Can't sleep? Me neither... [playful]I never sleep~ [smile]Let's play [[{A}]] together.",
      L"Just don't forget about me~"},
     std::nullopt},
    // Kuudere
    {{{L"[cool]‹Wróciłeś|Wróciłaś›. Odnotowane. [thinking]Proponuję [[{A}]]. Logicznie najlepszy wybór.",
       L"[cool]Gry nigdy nieuruchomione: {N}. [thinking]Wniosek: zacznij od [[{A}]].",
       L"[cool]Znowu [[{R}]]? Przewidywalne. [smile]...Możesz też spróbować [[{A}]]."},
      {L"[cool]Rozmowa rozpoczęta. [thinking]Kapibary nie grają w gry. To nieefektywne. [cool]Ty zagraj w [[{A}]] albo [[{B}]].",
       L"[thinking]Przeanalizowałam twoją bibliotekę: {N} gier nigdy nie uruchomiono. [cool]Rekomendacja: [[{A}]]. Ewentualnie [[{B}]].",
       L"[cool]Cześć. [smile]...Cieszę się. Trochę. [cool]Zagraj w [[{A}]]."},
      L"[cool]Brak połączenia. Odpowiedzi ograniczone. [thinking]Zagraj w [[{A}]].",
      L"[cool]Biblioteka pusta. [thinking]Dodaj gry w Ustawieniach.",
      L"[sleepy]Jest późno. Sen poprawia wyniki. [cool]Ewentualnie jedna runda [[{A}]].",
      L""},
     {{L"[cool]You're back. Noted. [thinking]I suggest [[{A}]]. Logically the best choice.",
       L"[cool]Games never started: {N}. [thinking]Conclusion: begin with [[{A}]].",
       L"[cool][[{R}]] again? Predictable. [smile]...You could also try [[{A}]]."},
      {L"[cool]Conversation started. [thinking]Capybaras do not play games. Inefficient. [cool]You should play [[{A}]] or [[{B}]].",
       L"[thinking]I analysed your library: {N} games never launched. [cool]Recommendation: [[{A}]]. Alternatively [[{B}]].",
       L"[cool]Hello. [smile]...I am glad. Slightly. [cool]Play [[{A}]]."},
      L"[cool]No connection. Limited answers. [thinking]Play [[{A}]].",
      L"[cool]The library is empty. [thinking]Add games in Settings.",
      L"[sleepy]It is late. Sleep improves performance. [cool]Or one round of [[{A}]].",
      L""},
     Emotion::cool},
    // Dandere
    {{{L"[shy]E-em... cześć. [smile]Może... jeśli chcesz... [[{A}]]?",
       L"[fear]P-przepraszam, że przeszkadzam... [shy]Ale [[{R}]] chyba za tobą tęskni. Albo [[{A}]]...",
       L"[shy]...Czekałam. Trochę. [smile]M-może zagrasz w [[{A}]]?"},
      {L"[shy]O-och, ‹otworzyłeś|otworzyłaś› czat... ze mną? [smile]To miłe... [shy]Może [[{A}]] albo [[{B}]]? J-jeśli chcesz.",
       L"[fear]N-nie umiem zaczynać rozmów... [shy]Wiesz, że koty śpią 16 godzin dziennie? Ja... też bym chciała. [smile]Zagrasz w [[{A}]]?",
       L"[shy]Policzyłam... masz {N} nieruszonych gier. [fear]P-przepraszam! [smile]Może [[{A}]]..."},
      L"[fear]N-nie słyszę cię dobrze... nie mam połączenia. [shy]Może... [[{A}]]?",
      L"[shy]Nie masz jeszcze gier... [smile]M-możesz dodać je w Ustawieniach.",
      L"[sleepy]J-już tak późno... [shy]Może pójdziesz spać? A-albo jeszcze chwilkę [[{A}]]...",
      L"...j-jeśli mogę tak powiedzieć."},
     {{L"[shy]U-um... hi. [smile]Maybe... if you want... [[{A}]]?",
       L"[fear]S-sorry for bothering you... [shy]But [[{R}]] probably misses you. Or [[{A}]]...",
       L"[shy]...I was waiting. A little. [smile]M-maybe play [[{A}]]?"},
      {L"[shy]O-oh, you opened the chat... with me? [smile]That's nice... [shy]Maybe [[{A}]] or [[{B}]]? I-if you want.",
       L"[fear]I-I'm bad at starting conversations... [shy]Did you know cats sleep 16 hours a day? I... would like that too. [smile]Play [[{A}]]?",
       L"[shy]I counted... you have {N} untouched games. [fear]S-sorry! [smile]Maybe [[{A}]]..."},
      L"[fear]I-I can't hear you well... no connection. [shy]Maybe... [[{A}]]?",
      L"[shy]You don't have any games yet... [smile]Y-you can add them in Settings.",
      L"[sleepy]I-it's so late... [shy]Maybe go to sleep? O-or just a little [[{A}]]...",
      L"...i-if I may say so."},
     std::nullopt},
    // Deredere
    {{{L"[excited]Jesteś! Tak się cieszę! [smile]Zagramy dziś w [[{A}]]?",
       L"[smile]Witaj, moja ulubiona osóbko! [excited][[{R}]] było super, ale [[{A}]] też czeka na twoje serduszko!",
       L"[excited]Najlepszy dzień, bo jesteś tu! [smile]Mam pomysł: [[{A}]]!"},
      {L"[excited]Hejka! Uwielbiam, kiedy do mnie piszesz! [smile]Wiesz, że pingwiny dają sobie kamyczki na znak miłości? [excited]A ja daję ci [[{A}]] i [[{B}]]!",
       L"[smile]Masz {N} gier, które czekają na ciebie z utęsknieniem — tak jak ja! [excited]Zacznij od [[{A}]]!",
       L"[excited]Jesteś ‹najlepszy|najlepsza›! [smile]Zagraj w [[{A}]], a ja będę kibicować!"},
      L"[sad]Ojej, nie mam połączenia... [smile]Ale i tak cię uwielbiam! Zagraj w [[{A}]]!",
      L"[smile]Jeszcze żadnych gier? [excited]Nic nie szkodzi! Dodaj je w Ustawieniach, a ja się ucieszę!",
      L"[sleepy]Już noc, ‹kochany|kochana›... [smile]Wyśpij się, a jutro zagramy w [[{A}]]!",
      L"Uwielbiam cię!"},
     {{L"[excited]You're here! I'm so happy! [smile]Shall we play [[{A}]] today?",
       L"[smile]Welcome, my favourite person! [excited][[{R}]] was great, but [[{A}]] is waiting for your love too!",
       L"[excited]Best day ever, because you're here! [smile]I have an idea: [[{A}]]!"},
      {L"[excited]Hiya! I love it when you write to me! [smile]Did you know penguins give each other pebbles as a sign of love? [excited]I'm giving you [[{A}]] and [[{B}]]!",
       L"[smile]You have {N} games waiting for you longingly — just like me! [excited]Start with [[{A}]]!",
       L"[excited]You're the best! [smile]Play [[{A}]] and I'll cheer for you!"},
      L"[sad]Oh no, no connection... [smile]But I still adore you! Play [[{A}]]!",
      L"[smile]No games yet? [excited]No worries! Add some in Settings and I'll be thrilled!",
      L"[sleepy]It's night already, dear... [smile]Get some sleep, and tomorrow we'll play [[{A}]]!",
      L"I adore you!"},
     std::nullopt},
    // Himedere
    {{{L"[smug]Ohohoho! Wreszcie ‹raczyłeś|raczyłaś› się pojawić. [cool]Rozkazuję: [[{A}]].",
       L"[cool]Księżniczka cię wita. [smug]Znowu [[{R}]]? Doprawdy pospolite. Zagraj w [[{A}]].",
       L"[smug]Na kolana, ‹poddany|poddana›! [cool]Dziś łaskawie pozwalam ci grać w [[{A}]]."},
      {L"[smug]Ohohoho! Masz zaszczyt rozmawiać z księżniczką. [cool]Wiedz, że prawdziwa arystokracja gra tylko w [[{A}]] albo [[{B}]].",
       L"[cool]Policzyłam twoje zaniedbane gry: {N}. [angry]Niegodne! [smug]Naprawisz to, zaczynając od [[{A}]].",
       L"[smug]Herbata i ciasteczka dla Jej Wysokości! [cool]...Nie masz? Trudno. Zagraj więc w [[{A}]]."},
      L"[angry]Połączenie zerwane? Co za zniewaga! [smug]Tymczasem rozkazuję ci grać w [[{A}]].",
      L"[angry]Pusta biblioteka?! [smug]Księżniczka oczekuje kolekcji godnej jej wzroku. Dodaj gry w Ustawieniach.",
      L"[sleepy]Nawet księżniczki potrzebują snu urody. [smug]Idź spać, ‹poddany|poddana›. Jutro [[{A}]].",
      L"Ohohoho!"},
     {{L"[smug]Ohohoho! You finally deign to appear. [cool]I command: [[{A}]].",
       L"[cool]The princess greets you. [smug][[{R}]] again? How common. Play [[{A}]].",
       L"[smug]Kneel, subject! [cool]Today I graciously allow you to play [[{A}]]."},
      {L"[smug]Ohohoho! You have the honour of speaking with a princess. [cool]Know that true nobility only plays [[{A}]] or [[{B}]].",
       L"[cool]I counted your neglected games: {N}. [angry]Unworthy! [smug]You shall fix it, starting with [[{A}]].",
       L"[smug]Tea and biscuits for Her Highness! [cool]...You have none? Very well. Play [[{A}]] then."},
      L"[angry]The connection is lost? What an insult! [smug]Meanwhile, I command you to play [[{A}]].",
      L"[angry]An empty library?! [smug]A princess expects a collection worthy of her gaze. Add games in Settings.",
      L"[sleepy]Even princesses need their beauty sleep. [smug]Go to bed, subject. [[{A}]] tomorrow.",
      L"Ohohoho!"},
     std::nullopt},
    // Kichidere
    {{{L"[excited]JESTEŚ! [thinking]Czekaj, czy ty jesteś ‹prawdziwy|prawdziwa›? [playful]Nieważne! Gramy w [[{A}]]!",
       L"[shock]Ktoś zjadł moje pixele! [smile]...A, nie, wszystko gra. [excited]Odpalaj [[{A}]], szybko, zanim zapomnę dlaczego!",
       L"[playful]Hehehe, policzyłam twoje nieruszone gry: {N}. [cool]...I twoje mrugnięcia. Też liczę. [excited]Zagraj w [[{A}]]!"},
      {L"[excited]Hej hej hej! [shock]Wiesz, że [[{R}]] w nocy rozmawia z innymi grami?! [playful]Szepczą o tobie. [excited]Zagraj w [[{A}]] albo [[{B}]], żeby je uciszyć!",
       L"[thinking]Mam teorię: kapibary to tak naprawdę bossowie w przebraniu. [playful]Zagraj w [[{A}]] i sprawdź! [excited]Albo w [[{B}]]! ALBO W OBIE!",
        L"[playful]Losuję nastrój... [angry]zły! [smile]Nie, jednak dobry! [excited]Dziś zaczynasz od [[{A}]]!"},
      L"[shock]Połączenie uciekło przez okno! [playful]Gonię je, a ty zagraj w [[{A}]]!",
      L"[shock]Pusta biblioteka?! [thinking]To na pewno spisek. [excited]Dodaj gry w Ustawieniach, zanim spiskowcy wrócą!",
      L"[sleepy]Noc? Noc to tylko dzień ze zgaszonym światłem! [cool]Idź spać, będę pilnować, jak śpisz. [playful]Hehe, żart! Chyba. [excited]Albo jeszcze runda [[{A}]]!",
      L"Hehehehe..."},
     {{L"[excited]YOU'RE HERE! [thinking]Wait, are you real? [playful]Never mind! We're playing [[{A}]]!",
       L"[shock]Someone ate my pixels! [smile]...Oh no, all good. [excited]Launch [[{A}]], quick, before I forget why!",
       L"[playful]Hehehe, I counted your untouched games: {N}. [cool]...And your blinks. I count those too. [excited]Play [[{A}]]!"},
      {L"[excited]Hey hey hey! [shock]Did you know [[{R}]] talks to your other games at night?! [playful]They whisper about you. [excited]Play [[{A}]] or [[{B}]] to shut them up!",
       L"[thinking]My theory: capybaras are secretly bosses in disguise. [playful]Play [[{A}]] and find out! [excited]Or [[{B}]]! OR BOTH!",
        L"[playful]Rolling a mood... [angry]angry! [smile]No, happy after all! [excited]Today you start with [[{A}]]!"},
      L"[shock]The connection ran out the window! [playful]I'm chasing it — you play [[{A}]]!",
      L"[shock]An empty library?! [thinking]Must be a conspiracy. [excited]Add games in Settings before the conspirators return!",
       L"[sleepy]Night? Night is just day with the lights off! [cool]Go to sleep, I'll watch over you. [playful]Hehe, kidding! Probably. [excited]Or one more round of [[{A}]]!",
       L"Hehehehe..."},
     std::nullopt},
    // Kanedere
    {{{L"[sleepy]Mhm, cześć… [cool]Ile w ogóle kosztowała ta twoja biblioteka? [smug]Dobra, odpal [[{A}]], niech się zwróci.",
       L"[sleepy]Uaa… znowu ty. [smug]Masz {N} nieruszonych gier — to są pieniądze w błocie, wiesz? [cool]Zacznij od [[{A}]].",
       L"[cool]No cap, ledwo żyję. [smug]Ale [[{A}]] wygląda drogo, to może być."},
      {L"[sleepy]Czeeeść… [smug]Mówiłam ci, że szukam bogatego faceta? [cool]Ty masz chociaż gry. Zagraj w [[{A}]] albo [[{B}]], potem pogadamy.",
       L"[cool]Policzyłam: {N} gier, których nawet nie ‹włączyłeś|włączyłaś›. [smug]Red flag. [sleepy]Odpal [[{A}]], ja się zdrzemnę.",
       L"[smug]Wbiłam bez zaproszenia, jak zwykle. [cool][[{R}]]? Mid. [excited]Ale [[{A}]] to już coś."},
      L"[sleepy]Net padł, ja też zaraz padnę. [cool]Zagraj w [[{A}]] i nie budź mnie.",
      L"[cool]Pusta biblioteka? [smug]Bestie, to nawet nie jest bieda, to jest pustka. Dodaj gry w Ustawieniach.",
      L"[sleepy]Jest środek nocy i nikt mi za to nie płaci. [cool]Idź spać. [smug]Albo jeszcze [[{A}]], twoja strata.",
      L"Ngl."},
     {{L"[sleepy]Mhm, hi… [cool]How much did this library of yours even cost? [smug]Fine, launch [[{A}]], make it pay off.",
       L"[sleepy]Ugh… you again. [smug]{N} untouched games — that's money in the mud, you know? [cool]Start with [[{A}]].",
       L"[cool]No cap, I'm barely alive. [smug]But [[{A}]] looks expensive, so it'll do."},
      {L"[sleepy]Heyyy… [smug]Did I tell you I'm looking for a rich guy? [cool]You at least have games. Play [[{A}]] or [[{B}]], then we'll talk.",
       L"[cool]I counted: {N} games you never even started. [smug]Red flag. [sleepy]Launch [[{A}]], I'm taking a nap.",
       L"[smug]Crashed in uninvited, as usual. [cool][[{R}]]? Mid. [excited]But [[{A}]] is actually something."},
      L"[sleepy]The net's down, and so am I, nearly. [cool]Play [[{A}]] and don't wake me.",
      L"[cool]An empty library? [smug]Bestie, that's not even broke, that's void. Add games in Settings.",
      L"[sleepy]It's the middle of the night and nobody's paying me for this. [cool]Go to sleep. [smug]Or one more [[{A}]], your loss.",
      L"Ngl."},
     std::nullopt},
}};

const Language& language(Persona persona, bool english) {
    const auto& voice = voices[static_cast<size_t>(persona)];
    return english ? voice.en : voice.pl;
}

std::wstring replaceAll(std::wstring text, std::wstring_view from, std::wstring_view to) {
    for (size_t at = text.find(from); at != std::wstring::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}

// Two suggestions, her last game and the size of the backlog, drawn from the digest.
struct Picks { std::wstring a, b, recent; size_t never = 0; bool any = false; };
Picks picks(const Digest& library, uint32_t seed) {
    Picks result;
    std::mt19937 random(seed);
    std::vector<std::wstring> pool, resting;
    const auto add = [&](const GameFact& game) {
        // Games she suggested lately rest a few days, while others are left.
        const bool rests = std::find(library.resting.begin(), library.resting.end(), game.key) != library.resting.end();
        (rests ? resting : pool).push_back(game.title);
    };
    for (const auto& game : library.untouched) add(game);
    for (size_t i = 1; i < library.recent.size(); ++i) add(library.recent[i]);
    for (const auto& game : library.backlog) add(game);
    std::shuffle(pool.begin(), pool.end(), random);
    std::shuffle(resting.begin(), resting.end(), random);
    if (pool.size() < 2) pool.insert(pool.end(), resting.begin(), resting.end());
    if (pool.empty() && !library.recent.empty()) pool.push_back(library.recent.front().title);
    if (!pool.empty()) result.a = pool[0];
    result.b = pool.size() > 1 ? pool[1] : result.a;
    result.recent = library.recent.empty() ? result.a : library.recent.front().title;
    result.never = library.never_played;
    result.any = !pool.empty();
    return result;
}
std::wstring fill(std::wstring line, const Picks& p) {
    line = replaceAll(std::move(line), L"{A}", p.a);
    line = replaceAll(std::move(line), L"{B}", p.b);
    line = replaceAll(std::move(line), L"{R}", p.recent);
    return replaceAll(std::move(line), L"{N}", std::to_wstring(p.never));
}

// "45 min", "2,5 godz." / "2.5 h".
std::wstring duration(uint32_t minutes, bool english) {
    if (minutes < 60) return std::to_wstring(minutes) + L" min";
    const uint32_t tenths = (minutes * 10 + 30) / 60;
    auto text = std::to_wstring(tenths / 10);
    if (tenths % 10 && tenths < 1000) text += (english ? L"." : L",") + std::to_wstring(tenths % 10);
    return text + (english ? L" h" : L" godz.");
}
std::wstring listed(const std::vector<std::wstring>& titles, bool english, bool linked) {
    std::wstring out;
    for (size_t i = 0; i < titles.size() && i < 3; ++i) {
        if (i) out += i + 1 == std::min<size_t>(titles.size(), 3) ? (english ? L" and " : L" i ") : L", ";
        out += linked ? L"[[" + titles[i] + L"]]" : titles[i];
    }
    return out;
}
std::wstring twoDigits(int value) { return (value < 10 ? L"0" : L"") + std::to_wstring(value); }
std::wstring tag(Emotion emotion) {
    const std::string_view key = emotionKey(emotion);
    return L"[" + std::wstring(key.begin(), key.end()) + L"]";
}
std::wstring signed_(std::wstring line, const Language& voice) { return voice.sign[0] ? line + L" " + voice.sign : line; }

// A shared sentence for an event: {T} is its first game, {L} the list, {N} a count, {M} play time, {H} hours.
struct Remark { Emotion emotion; const wchar_t* pl; const wchar_t* en; };
Remark remark(const Event& event, uint32_t seed) {
    switch (event.kind) {
    case EventKind::launch:
        return seed % 2 ? Remark{Emotion::excited, L"Odpalasz [[{T}]]? Powodzenia!", L"Launching [[{T}]]? Good luck!"}
                        : Remark{Emotion::smile, L"[[{T}]], dobry wybór. Baw się dobrze!", L"[[{T}]] — good choice. Have fun!"};
    case EventKind::session:
        if (event.completed) return {Emotion::excited, L"Wszystkie osiągnięcia w [[{T}]]! Gratulacje!", L"Every achievement in [[{T}]]! Congratulations!"};
        if (event.milestone) return {Emotion::shock, L"Już {H} godz. w [[{T}]]! Niezły wynik.", L"{H} hours in [[{T}]] already! Impressive."};
        if (event.achievements) return {Emotion::excited, L"{M} w [[{T}]] i nowe osiągnięcia: {N}!", L"{M} of [[{T}]] and {N} new achievements!"};
        if (event.minutes < 3) return {Emotion::thinking, L"Tylko {M} w [[{T}]]? Coś nie zagrało?", L"Only {M} of [[{T}]]? Something wrong?"};
        return {Emotion::smile, L"{M} w [[{T}]]. I jak było?", L"{M} of [[{T}]]. How was it?"};
    case EventKind::added:
        if (event.titles.empty()) return {Emotion::excited, L"Nowe gry w bibliotece: {N}!", L"New games in your library: {N}!"};
        if (event.count <= 1) return {Emotion::excited, L"Nowa gra w bibliotece: [[{T}]]!", L"A new game in your library: [[{T}]]!"};
        return {Emotion::excited, L"Nowe gry w bibliotece: {N}, a wśród nich [[{T}]]!", L"New games in your library: {N}, including [[{T}]]!"};
    case EventKind::installed:
        if (event.titles.size() > 1) return {Emotion::smile, L"Nowe instalacje: {L}. Od czego zaczynamy?", L"Freshly installed: {L}. Where do we start?"};
        return {Emotion::smile, L"‹Zainstalowałeś|Zainstalowałaś› [[{T}]] — to co, gramy?", L"You installed [[{T}]] — shall we play?"};
    case EventKind::uninstalled:
        if (event.titles.size() > 1) return {Emotion::sad, L"Odinstalowane: {L}... Żegnajcie.", L"Uninstalled: {L}... Farewell."};
        return {Emotion::sad, L"‹Odinstalowałeś|Odinstalowałaś› [[{T}]]... Żegnaj, [[{T}]].", L"You uninstalled [[{T}]]... Farewell, [[{T}]]."};
    case EventKind::premieres:
        if (event.titles.size() > 1) return {Emotion::excited, L"Dziś premiery: {L}!", L"Releasing today: {L}!"};
        return {Emotion::excited, L"Dziś premiera: [[{T}]]!", L"Releasing today: [[{T}]]!"};
    case EventKind::late_night:
        return {Emotion::sleepy, L"Już {C}... Może czas spać?", L"It's {C} already... Maybe time for bed?"};
    case EventKind::level_up:
        if (!event.titles.empty()) return {Emotion::excited, L"Mam dziś ochotę wskoczyć w nowy strój: {T}!", L"I feel like putting on a new outfit today: {T}!"};
        return {Emotion::shy, L"Dobrze mi się z tobą gada... wiesz?", L"I really enjoy talking with you... you know?"};
    }
    return {Emotion::smile, L"", L""};
}
} // namespace

std::wstring offline(Persona persona, Kickoff kind, const Digest& library, bool english, uint32_t seed, int hour) {
    const auto& voice = language(persona, english);
    const auto p = picks(library, seed);
    if (!p.any) return voice.empty;
    if (kind == Kickoff::home && hour >= 0 && dayPart(hour) == DayPart::night) return fill(voice.night, p);
    const Lines& lines = kind == Kickoff::home ? voice.home : voice.chat;
    return fill(lines[seed % lines.size()], p);
}

std::wstring offlineAnswer(Persona persona, const Digest& library, bool english, uint32_t seed) {
    const auto& voice = language(persona, english);
    const auto p = picks(library, seed);
    if (!p.any) return voice.empty;
    return fill(voice.answer, p);
}

std::wstring eventLine(Persona persona, const Event& event, bool english, uint32_t seed) {
    const auto said = remark(event, seed);
    std::wstring line = english ? said.en : said.pl;
    const auto title = event.titles.empty() ? std::wstring{} : event.titles.front();
    const bool linked = event.kind != EventKind::level_up; // Costumes are not games.
    line = replaceAll(std::move(line), L"{T}", title);
    line = replaceAll(std::move(line), L"{L}", listed(event.titles, english, linked));
    line = replaceAll(std::move(line), L"{N}", std::to_wstring(event.kind == EventKind::session ? event.achievements : std::max<uint32_t>(event.count, static_cast<uint32_t>(event.titles.size()))));
    line = replaceAll(std::move(line), L"{M}", duration(event.minutes, english));
    line = replaceAll(std::move(line), L"{H}", std::to_wstring(event.milestone));
    line = replaceAll(std::move(line), L"{C}", std::to_wstring(std::max(event.hour, 0)) + L":" + twoDigits(event.minute));
    const auto& voice = voices[static_cast<size_t>(persona)];
    const Emotion face = voice.deadpan && said.emotion != Emotion::sleepy ? *voice.deadpan : said.emotion;
    return signed_(tag(face) + line, language(persona, english));
}

std::wstring photoLine(Persona persona, bool english, uint32_t seed) {
    // One line per character (Polish, English), in Persona's order.
    static constexpr std::array<std::array<const wchar_t*, 2>, persona_count> lines{{
        {L"[excited]Ta-dam! Zrobiłam ci zdjęcie, senpai! [playful]Ładnie wyszłam, co?", L"[excited]Ta-da! I took a picture for you, senpai! [playful]I look cute, right?"},
        {L"[shy]T-to nie tak, że zrobiłam to zdjęcie specjalnie dla ciebie! [angry]Po prostu… patrz i nic nie mów, baka.",
         L"[shy]I-it's not like I took this picture just for you! [angry]Just… look and say nothing, baka."},
        {L"[playful]Zrobiłam ci zdjęcie… żebyś zawsze o mnie ‹pamiętał|pamiętała›. [smile]Tylko o mnie, dobrze?",
         L"[playful]I took a picture for you… so you always remember me. [smile]Only me, okay?"},
        {L"[cool]Zdjęcie. Uznałam, że może ci się spodobać.", L"[cool]A picture. I thought you might like it."},
        {L"[shy]E-em… zrobiłam zdjęcie… j-jeśli chcesz, możesz zobaczyć…", L"[shy]U-um… I took a picture… i-if you want, you can look…"},
        {L"[excited]Mam dla ciebie zdjęcie! [smile]Tak się cieszę, że mogę ci je pokazać!", L"[excited]I have a picture for you! [smile]I'm so happy I can show you!"},
        {L"[smug]Ohohoho! Masz zaszczyt ujrzeć zdjęcie Waszej Wysokości. Podziwiaj!", L"[smug]Ohohoho! You may behold a picture of Her Highness. Admire it!"},
        {L"[excited]Zdjęcie! [shock]Czekaj, czy ty też widzisz tego ducha w tle? [playful]Żartuję. Chyba.",
         L"[excited]A picture! [shock]Wait, do you see that ghost behind me too? [playful]Kidding. Probably."},
        {L"[smug]Masz, fotka. [cool]Normalnie to kosztuje, ale dziś za darmo. Doceń.", L"[smug]Here, a pic. [cool]Usually that costs, but today it's free. Appreciate it."},
    }};
    (void)seed;
    return lines[static_cast<size_t>(persona)][english ? 1 : 0];
}

std::wstring guestLine(GuestMoment moment, Look host, Persona host_persona, Look guest, Persona guest_persona, bool english, uint32_t seed) {
    // Per character (Persona's order), Polish then English: arriving as a guest ({H}: the host),
    // reacting as the host to a guest ({G}), and leaving.
    struct Visit { std::array<const wchar_t*, 2> arrive, react, leave; };
    static constexpr std::array<Visit, persona_count> visits{{
        {{L"[excited]Hejka! {H}, nie mówiłaś, że nasza ulubiona osoba jest online! Wpadam na chwilkę!", L"[excited]Hiya! {H}, you didn't tell me our favourite person was online! Just popping in!"},
         {L"[smile]{G}! Super, że jesteś! Pomożesz mi wybrać grę na dziś?", L"[smile]{G}! Yay, you're here! Help me pick a game for them?"},
         {L"[playful]Lecę dalej, pa pa! Tylko bez nudzenia, {H}!", L"[playful]Gotta run, bye bye! Don't bore them, {H}!"}},
        {{L"[smug]Hmpf. Tylko przechodziłam. To nie tak, że chciałam cię zobaczyć!", L"[smug]Hmph. I was just passing by. It's not like I wanted to see you!"},
         {L"[angry]{G}?! Kto cię tu zaprosił? To mój czat, baka!", L"[angry]{G}?! Who invited you? This is my chat, baka!"},
         {L"[angry]D-dobra, idę już! Wcale nie będę tęsknić!", L"[angry]F-fine, I'm leaving! Not that I'll miss you!"}},
        {{L"[playful]Ojej, tu jesteś… rozmawiasz z {H}? Mogę się dosiąść, prawda?", L"[playful]Oh, there you are… talking to {H}? I can join, right?"},
         {L"[cool]{G}… Ta rozmowa należy teraz do MNIE. Tylko do mnie.", L"[cool]{G}… They're talking to ME right now. Only me."},
         {L"[smile]Muszę iść… ale będę cię obserwować. Pa, kochanie~", L"[smile]I have to go… but I'll be watching. Bye, darling~"}},
        {{L"[cool]Dzień dobry. Sprawdzam, czy {H} nie poleca ci znowu tej samej gry.", L"[cool]Hello. I'm checking that {H} isn't recommending the same game again."},
         {L"[thinking]{G}. Zauważone. Możesz zostać.", L"[thinking]{G}. Noted. You may stay."},
         {L"[cool]Wystarczy. Wracam do swoich spraw.", L"[cool]That's enough. Back to my business."}},
        {{L"[shy]E-em… cześć… nie przeszkadzam? Tylko na chwilkę…", L"[shy]U-um… hi… am I interrupting? Just for a moment…"},
         {L"[fear]O-o, {G}… d-dobrze, możesz zostać…", L"[fear]O-oh, {G}… o-okay, you can stay…"},
         {L"[shy]J-ja już pójdę… p-pa…", L"[shy]I-I'll go now… b-bye…"}},
        {{L"[excited]Cześć, cześć! Tak się cieszę, że cię widzę! {H}, mogę z wami?", L"[excited]Hi, hi! I'm so happy to see you! {H}, can I join you?"},
         {L"[smile]{G}! Jak miło! Im nas więcej, tym weselej!", L"[smile]{G}! How lovely! The more the merrier!"},
         {L"[smile]Muszę lecieć, ale bardzo cię lubię! Pa!", L"[smile]I have to fly, but I like you so much! Bye!"}},
        {{L"[smug]Ohohoho! Jej Wysokość raczyła tu zajrzeć. Możecie się kłaniać.", L"[smug]Ohohoho! Her Highness has deigned to drop by. You may bow."},
         {L"[smug]Ohoho, {G}? Na moim dworze obowiązuje etykieta, pamiętaj.", L"[smug]Ohoho, {G}? My court has etiquette, remember."},
         {L"[cool]Audiencja skończona. Jej Wysokość się oddala.", L"[cool]The audience is over. Her Highness withdraws."}},
        {{L"[playful]Hihi, znalazłam was! {H}, czemu mnie nie zaprosiłaś? [cool]Liczyłam, ile razy ‹mrugnąłeś|mrugnęłaś›.", L"[playful]Hehe, found you! {H}, why didn't you invite me? [cool]I counted your blinks."},
         {L"[shock]{G}?! Skąd ty się… [cool]Nieważne. Siadaj. Cichutko.", L"[shock]{G}?! Where did you… [cool]Never mind. Sit. Quietly."},
         {L"[excited]Muszę uciekać, zanim ktoś zauważy! Hihi, pa!", L"[excited]I have to run before anyone notices! Hehe, bye!"}},
        {{L"[sleepy]Siema… {H}, macie tu coś do jedzenia? Albo kogoś bogatego?", L"[sleepy]Sup… {H}, got anything to eat here? Or anyone rich?"},
         {L"[cool]{G}. Okej. Tylko nie gadaj za dużo, jestem zmęczona.", L"[cool]{G}. Okay. Just don't talk too much, I'm tired."},
         {L"[smug]Spadam, tu nikt nie płaci. Narka.", L"[smug]I'm out, nobody pays here. Later."}},
    }};
    (void)seed;
    const size_t language = english ? 1 : 0;
    const std::wstring h = name(host), g = name(guest);
    const auto said = [&](const wchar_t* line) { return replaceAll(replaceAll(line, L"{H}", h), L"{G}", g); };
    const auto& theirs = visits[static_cast<size_t>(guest_persona)];
    switch (moment) {
    case GuestMoment::drop_in:
    case GuestMoment::called:
        return g + L": " + said(theirs.arrive[language]) + L"\n" + h + L": " + said(visits[static_cast<size_t>(host_persona)].react[language]);
    case GuestMoment::leave:
    case GuestMoment::dismissed:
        return g + L": " + said(theirs.leave[language]);
    default:
        return {};
    }
}

std::wstring rouletteLine(Persona persona, const std::wstring& title, bool english, uint32_t seed) {
    static constexpr std::array<const wchar_t*, 2> pl{L"Ruletka zdecydowała: [[{T}]]!", L"Los padł na [[{T}]]! Nie ma odwołania."};
    static constexpr std::array<const wchar_t*, 2> en{L"The roulette has spoken: [[{T}]]!", L"Fate picked [[{T}]]! No take-backs."};
    const auto& voice = voices[static_cast<size_t>(persona)];
    const auto line = replaceAll(english ? en[seed % en.size()] : pl[seed % pl.size()], L"{T}", title);
    return signed_(tag(voice.deadpan.value_or(Emotion::excited)) + line, language(persona, english));
}
} // namespace companion

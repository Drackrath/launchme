#include "../src/library/search.hpp"

#include <iostream>

int main() {
    const std::vector<Game> public_games{
        Game{1, L"Inna ŻÓŁWIA gra"}, Game{2, L"Żółwia przygoda"}, Game{3, L"Niepasująca"}
    };
    const auto matches = searchPublicGames(public_games, L"żÓł");
    if (matches.size() != 2 || matches[0].app_id != 2 || matches[1].app_id != 1 ||
        !searchPublicGames(public_games, L"").empty()) {
        std::cerr << "Live search did not match UTF-16 titles or prioritize prefixes\n";
        return 1;
    }
    // Prefix matches first, each group by folded title (case-insensitive); private games never.
    Game hidden{14, L"Portal Ukryty"};
    hidden.visibility = GameVisibility::private_game;
    const std::vector<Game> portals{Game{10, L"The portal"}, Game{11, L"portal 2"}, Game{12, L"A Portal story"},
                                    Game{13, L"Portal"}, hidden};
    const auto ordered = searchPublicGames(portals, L"PORTAL");
    if (ordered.size() != 4 || ordered[0].app_id != 13 || ordered[1].app_id != 11 ||
        ordered[2].app_id != 12 || ordered[3].app_id != 10) {
        std::cerr << "Live search order or privacy changed\n";
        return 2;
    }
    // Folding: accents, punctuation, ™, Roman numerals, word order and words typed together.
    const std::vector<Game> folded{Game{20, L"Baldur's Gate 3"}, Game{21, L"DARK SOULS™ III"}, Game{22, L"Marvel's Spider-Man"},
                                   Game{23, L"Pokémon Legends"}, Game{24, L"Wiedźmin 3"}};
    const auto one = [&](const wchar_t* query, uint32_t id) {
        const auto found = searchPublicGames(folded, query);
        return found.size() == 1 && found[0].app_id == id;
    };
    if (!one(L"baldurs gate", 20) || !one(L"gate baldur", 20) || !one(L"dark souls 3", 21) || !one(L"dark souls iii", 21) ||
        !one(L"spiderman", 22) || !one(L"spider-man", 22) || !one(L"pokemon", 23) || !one(L"wiedzmin", 24)) {
        std::cerr << "Folded search missed a title\n";
        return 3;
    }
    if (!searchPublicGames(folded, L"zelda").empty()) return 4;
    // Order as people read titles: numbers by value, Polish letters in place.
    if (compareTitles(L"Game 2", L"Game 10") >= 0 || compareTitles(L"Łódź", L"Zelda") >= 0 || compareTitles(L"Lara", L"Łódź") >= 0 ||
        compareTitles(L"alpha", L"Beta") >= 0 || compareTitles(L"DOOM™", L"doom") != 0) {
        std::cerr << "Title order changed\n";
        return 5;
    }
    return 0;
}

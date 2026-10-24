#pragma once
// Two companions for the tests, whatever res/companion holds: the host Hana (himedere, the first
// by her key, so the default one) and her guest Rin (yandere), each with her basic outfit only.
// Cast installs them for its scope; their indices still pick the embedded pictures of whichever
// built-in characters are first (tests never count on those).
#include "../src/net/companion_characters.hpp"

#include <vector>

namespace companion_fixture {
inline constexpr companion::Look hana = static_cast<companion::Look>(0);
inline constexpr companion::Look rin = static_cast<companion::Look>(1);

inline std::vector<companion::Character> cast() {
    return companion::parseCharacters(
        "[hana]\nname=Hana\ncolor=#B3C4FF\npersona=himedere\ncard_pl=Srebrne włosy · gotycka sukienka\ncard_en=Silver hair · gothic dress\n"
        "you_pl=Masz srebrne włosy i czarną gotycką sukienkę.\nher_pl=Ma srebrne włosy i czarną gotycką sukienkę.\n"
        "you_en=You have silver hair and a black gothic dress.\nher_en=She has silver hair and a black gothic dress.\n"
        "about_pl=Uwielbiasz horrory i herbatę.\nabout_en=You love horror films and tea.\nabout_her_pl=Uwielbia horrory.\nabout_her_en=She loves horror films.\n"
        "topics_pl=nowy horror | herbata jaśminowa\ntopics_en=a new horror film | jasmine tea\ncostume=0 0 costume_00\n"
        "[rin]\nname=Rin\ncolor=#FF5A6E\npersona=yandere\ncard_pl=Karmazynowe włosy · okulary\ncard_en=Crimson hair · glasses\n"
        "you_pl=Masz karmazynowe włosy i okulary.\nher_pl=Ma karmazynowe włosy i okulary.\n"
        "you_en=You have crimson hair and glasses.\nher_en=She has crimson hair and glasses.\n"
        "about_pl=Czytasz książki o fizyce.\nabout_en=You read books on physics.\nabout_her_pl=Czyta książki o fizyce.\nabout_her_en=She reads books on physics.\n"
        "topics_pl=nowa książka | czarne dziury\ntopics_en=a new book | black holes\ncostume=0 0 costume_00\n");
}

// Hana and Rin for its scope; the built-in characters come back after.
struct Cast {
    std::vector<companion::Character> built_in = companion::characters();
    Cast() { companion::setCharacters(cast()); }
    ~Cast() { companion::setCharacters(built_in); }
    Cast(const Cast&) = delete;
    Cast& operator=(const Cast&) = delete;
};
} // namespace companion_fixture

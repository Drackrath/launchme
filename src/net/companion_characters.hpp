#pragma once
// The companions: any number of characters, each a folder res/companion/<key>/ with its
// character.txt and costume folders (res/companion/README.md). CMake compiles them into a manifest
// (generated/companion_manifest.inc); companion::Look is an index into characters(), in their
// `order=`, and the same index picks her embedded pictures (companion_ui::spriteId), so CMake
// leaves out every folder this parser would not take (characterKey, a name=). A character with
// status=soon only shows a "Wkrótce" card: she cannot be chosen or called in until her pictures arrive.
#include "companion.hpp"
#include "companion_costumes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace companion {
// Her ElevenLabs voice (character.txt elevenlabs_* lines, src/net/elevenlabs.hpp): a Voice Library
// voice (paid plans) and a built-in one for free accounts, each "<voice ID> <its name>", an
// optional model, voice settings ("stability=0.4, speed=1.05") and how she sounds (an audio tag
// for the v3/v4 models, "bubbly, energetic"). Empty: her character's defaults.
struct VoiceProfile {
    std::string voice, fallback, model, settings;
    std::wstring voice_name, fallback_name, delivery;
};
struct Character {
    std::string key;            // Her folder: settings, the affinity file and photo folders name her by it.
    std::wstring name;          // "Yuki"
    float r = 0.8f, g = 0.8f, b = 0.8f; // Her colour: her name, her bubbles' rim, her light.
    Persona persona = Persona::genki;    // Her character until the player picks another.
    std::wstring card_pl, card_en;       // Under her name on the Settings card.
    std::wstring you_pl, her_pl, you_en, her_en; // Her picture in words: to herself, about her as a guest.
    // Who she is beyond her archetype (character.txt about_*): interests, family, quirks, tastes and
    // how she sees the other companions. They stay whichever character (-dere) the player picks.
    std::wstring about_pl, about_en;         // To herself ("Interesujesz się…").
    std::wstring about_her_pl, about_her_en; // About her as a guest, shorter ("Interesuje się…").
    // What may be on her mind (topics_*: "a | b | c"): the prompt picks a couple a day, so her talk
    // wanders off games to her own world.
    std::vector<std::wstring> topics_pl, topics_en;
    // How she talks (example_pl / example_en, one line each, any number): a short exchange whose
    // turns "||" separates ("Gracz: Hej! || Ty: [smug]No hej."). Her prompt shows them as a model
    // of her style, length and tone, never to repeat word for word.
    std::vector<std::wstring> examples_pl, examples_en;
    // Her taste in games (likes / dislikes: Steam-style tags, "a, b | c"): she leans her
    // suggestions that way and may argue about it.
    std::vector<std::wstring> likes, dislikes;
    // Where she lives (home_*: a real city and her corner of it, in words) and its clock
    // (timezone=+09:00 standard time, dst=eu|us|none): she knows her own time of day.
    std::wstring home_pl, home_en;
    int utc_offset = 0;         // Minutes east of UTC, standard time.
    bool zoned = false;         // A timezone= line was given.
    enum class Dst { none, eu, us } dst = Dst::none;
    bool night_owl = false;     // chronotype=night: lively late, sleepy in the morning.
    // Real things she loves (favorites_*: "Muzyka: The Cure, Bauhaus | Miejsca: …", one category per
    // part): her prompt names a few a day, so what she talks about exists. Her own goals outside
    // games (goals_*: "a | b | c"): her mind takes them on one by one over days.
    std::vector<std::wstring> favorites_pl, favorites_en, goals_pl, goals_en;
    float gossip = 0.3f;        // gossip=0–1: how readily she passes on what the player told her.
    // attachment=secure|anxious|avoidant|fearful (Bowlby, Hazan & Shaver): how she reacts to his
    // absence, his brush-offs and her own openness; empty: her character's (-dere) default.
    std::string attachment;
    // location=50.0614,19.9366: her home's coordinates for the real weather there (companion_weather.hpp);
    // without it, the city that starts home_en is looked up.
    std::optional<std::pair<double, double>> location;
    Gender gender = Gender::female;                     // gender=female|male
    Attraction attraction = Attraction::any;            // attraction=any|male|female|none
    AttractionFlex attraction_flex = AttractionFlex::rare_exception; // attraction_flex=strict|rare_exception
    RelationStyle style = RelationStyle::balanced;      // style=balanced|traditional|casual
    Exclusivity exclusivity = Exclusivity::monogamous;  // exclusivity=monogamous|open|fluid (his other "pairs", her jealousy)
    RelationRole default_role = RelationRole::auto_role; // role=auto|friend|coworker|romantic
    LifeWealth default_wealth = LifeWealth::comfortable; // wealth=comfortable|tight|broke|flush
    LifeWork default_work = LifeWork::steady;           // work=steady|busy|between_jobs|promoted|burnout
    // Her roots, family background and baseline occupation/living situation (character.txt).
    std::wstring origin_pl, origin_en;
    std::wstring occupation_pl, occupation_en;
    VoiceProfile voice;         // elevenlabs_voice, elevenlabs_voice_free, elevenlabs_model, elevenlabs_settings, elevenlabs_tag.
    bool soon = false;          // A card only ("Wkrótce").
    std::vector<costumes::Costume> costumes; // costume_00 first (slot 0), then by level.
    const std::wstring& card(bool english) const { return english && !card_en.empty() ? card_en : card_pl; }
    const std::wstring& appearance(bool english, bool you) const {
        const auto& text = english ? (you ? you_en : her_en) : (you ? you_pl : her_pl);
        return text.empty() && english ? (you ? you_pl : her_pl) : text;
    }
    const std::wstring& about(bool english, bool you) const {
        const auto& text = english ? (you ? about_en : about_her_en) : (you ? about_pl : about_her_pl);
        return text.empty() && english ? (you ? about_pl : about_her_pl) : text;
    }
    const std::vector<std::wstring>& topics(bool english) const { return english && !topics_en.empty() ? topics_en : topics_pl; }
    const std::vector<std::wstring>& examples(bool english) const { return english && !examples_en.empty() ? examples_en : examples_pl; }
    const std::wstring& home(bool english) const { return english && !home_en.empty() ? home_en : home_pl; }
    const std::wstring& origin(bool english) const { return english && !origin_en.empty() ? origin_en : origin_pl; }
    const std::wstring& occupation(bool english) const { return english && !occupation_en.empty() ? occupation_en : occupation_pl; }
    const std::vector<std::wstring>& favorites(bool english) const { return english && !favorites_en.empty() ? favorites_en : favorites_pl; }
    const std::vector<std::wstring>& goals(bool english) const { return english && !goals_en.empty() ? goals_en : goals_pl; }
};
// A wall clock reading.
struct Clock { int year = 0, month = 0, day = 0, weekday = 0, hour = 0, minute = 0; }; // weekday: 0 = Sunday.
// Her local time at `utc` (Unix seconds): her timezone with its daylight saving; nullopt without one.
std::optional<Clock> localClock(const Character& character, uint64_t utc);
// Seconds since 1970 of her local wall clock (only compared with itself); 0 without a timezone.
uint64_t localSeconds(const Character& character, uint64_t utc);
// A character's key (her folder, settings, the affinity file, chat and photo folders): 1–32 of
// a-z 0-9 _ -, never a line of companion_affinity.txt (day, streak, pick, card).
bool characterKey(std::string_view key);
// The manifest's text: "[key]" sections of character.txt lines (key=value, # comments, a BOM
// ignored) and "costume=<slot> <level> <folder>" lines. Unknown keys are ignored; a section with a
// bad or repeated key is skipped (CMake never writes one); one without a name is named by her key.
std::vector<Character> parseCharacters(std::string_view manifest);
// Every character, as built in (tests may replace them).
const std::vector<Character>& characters();
std::string characterManifest(std::string_view key); // Exact authored section embedded in this executable.
void setCharacters(std::vector<Character> list); // Tests only.
uint64_t charactersRevision(); // Grows with each setCharacters: lists built from characters() check it.
const Character* character(Look look);
// The ones that can be chosen (not "soon").
std::vector<Look> availableLooks();
} // namespace companion

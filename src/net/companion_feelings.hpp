#pragma once
// Semantic feelings are independent of the 20 existing sprite/gesture slots. Calm is not
// indifference; concern for someone is not fear. Each can use an existing visual expression.
#include "companion.hpp"
#include "../platform/text_encoding.hpp"
#include <cwctype>
#include <algorithm>

namespace companion {
enum class Feeling { care, worry, indifference, calm, joy, sadness, anger, fear, curiosity,
    affection, gratitude, relief, disappointment, jealousy, longing, trust, embarrassment, pride, frustration };
struct FeelingInfo { const char* key; const wchar_t* pl; const wchar_t* en; Emotion sprite; };
inline constexpr std::array<FeelingInfo, 19> feelings{{
    {"care", L"Troska", L"Care", Emotion::smile}, {"worry", L"Obawa", L"Worry", Emotion::thinking},
    {"indifference", L"Obojętność", L"Indifference", Emotion::cool}, {"calm", L"Spokój", L"Calm", Emotion::cool},
    {"joy", L"Radość", L"Joy", Emotion::smile}, {"sadness", L"Smutek", L"Sadness", Emotion::sad},
    {"anger", L"Złość", L"Anger", Emotion::angry}, {"fear", L"Strach", L"Fear", Emotion::fear},
    {"curiosity", L"Ciekawość", L"Curiosity", Emotion::thinking}, {"affection", L"Czułość", L"Affection", Emotion::love},
    {"gratitude", L"Wdzięczność", L"Gratitude", Emotion::smile}, {"relief", L"Ulga", L"Relief", Emotion::cool},
    {"disappointment", L"Rozczarowanie", L"Disappointment", Emotion::sad}, {"jealousy", L"Zazdrość", L"Jealousy", Emotion::angry},
    {"longing", L"Tęsknota", L"Longing", Emotion::sad}, {"trust", L"Zaufanie", L"Trust", Emotion::smile},
    {"embarrassment", L"Zakłopotanie", L"Embarrassment", Emotion::shy}, {"pride", L"Duma", L"Pride", Emotion::smug},
    {"frustration", L"Frustracja", L"Frustration", Emotion::angry}
}};
inline const FeelingInfo& feelingInfo(Feeling feeling) { return feelings[std::clamp(static_cast<size_t>(feeling), size_t{0}, feelings.size() - 1)]; }
inline std::optional<Feeling> feelingFromKey(std::string_view key) {
    for (size_t i = 0; i < feelings.size(); ++i) if (key == feelings[i].key) return static_cast<Feeling>(i);
    return {};
}
inline std::optional<Feeling> feelingFromText(std::wstring_view text) {
    const auto lower = [](std::wstring value) { for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c)); return value; };
    const auto value = lower(std::wstring(text));
    for (size_t i = 0; i < feelings.size(); ++i)
        if (value == lower(feelings[i].pl) || value == lower(feelings[i].en)) return static_cast<Feeling>(i);
    for (const auto& [word, kind] : std::initializer_list<std::pair<const wchar_t*, Feeling>>{
        {L"troskliwa", Feeling::care}, {L"zatroskana", Feeling::care}, {L"zmartwiona", Feeling::worry}, {L"niepokój", Feeling::worry},
        {L"obojętna", Feeling::indifference}, {L"spokojna", Feeling::calm}, {L"ciekawa", Feeling::curiosity},
        {L"wdzięczna", Feeling::gratitude}, {L"rozczarowana", Feeling::disappointment}, {L"zazdrosna", Feeling::jealousy},
        {L"worried", Feeling::worry}, {L"caring", Feeling::care}, {L"indifferent", Feeling::indifference}})
        if (value == word) return kind;
    return {}; // Free text is not forced into an emotion by a valence/arousal guess.
}
}

#include "companion_fixture.hpp"
#include "../src/net/companion_mind.hpp"
#include <iostream>

int main() {
    using namespace companion;
    const companion_fixture::Cast cast;
    if (emotion_count != 20 || feelings.size() != 19) return 1;
    for (size_t i = 0; i < feelings.size(); ++i)
        if (feelingFromKey(feelings[i].key) != static_cast<Feeling>(i) || feelingInfo(static_cast<Feeling>(i)).sprite >= static_cast<Emotion>(emotion_count)) return 2;
    if (feelingFromText(L"obojętna") != Feeling::indifference || feelingFromText(L"troska") != Feeling::care ||
        feelingFromText(L"zmartwiona") != Feeling::worry || feelingFromKey("cool") || feelingFromText(L"nie jestem zmartwiona")) return 3;
    const auto care = mind::parseAppraisal(LR"({"uczucie":"care","sila_uczucia":0.7,"czuje":"Chcę o niego zadbać","wiem":["Lubi spokojne wieczory."]})");
    if (!care.ok || care.feeling != Feeling::care || care.feeling_inferred || !care.feeling_intensity || *care.feeling_intensity != 0.7f) return 4;
    mind::Context context; context.look = "hana"; context.character = character(companion_fixture::hana); context.now = 1'760'000'000;
    mind::State state; Memory memory;
    auto& self = state.of("hana");
    mind::apply(self, memory, care, context, {});
    if (self.feelings.size() != 1 || memory.notes.empty() || memory.notes[0].emotion != "care") return 5;
    mind::apply(self, memory, care, context, {});
    if (self.feelings.size() != 1) return 6; // Duplicate delivery of the same moment.
    const auto legacy = mind::parseAppraisal(LR"({"czuje":"obojętna"})");
    if (legacy.feeling != Feeling::indifference || !legacy.feeling_inferred || legacy.feeling_intensity) return 7;
    const auto invalid = mind::parseAppraisal(LR"({"uczucie":"made-up","czuje":"nie wiem","sila_uczucia":"nan"})");
    if (invalid.feeling || invalid.feeling_intensity) return 8;
    for (int i = 0; i < 100; ++i) { ++context.now; mind::apply(self, memory, care, context, {}); }
    if (self.feelings.size() != 80) return 9;
    const auto loaded = mind::State::load(state.save());
    if (!loaded || !loaded->find("hana") || loaded->find("hana")->feelings.size() != 80 ||
        loaded->find("hana")->feelings.back().kind != Feeling::care || loaded->find("hana")->feelings.back().intensity != 0.7f) return 10;
    PromptExtras extras; extras.inner = true;
    const auto prompt = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, 0, 0, 12, extras);
    if (prompt.find(L"care=troska") == prompt.npos || prompt.find(L"sila_uczucia") == prompt.npos) return 11;
    return 0;
}

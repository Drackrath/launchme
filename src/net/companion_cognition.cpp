#include "companion_cognition.hpp"
#include "companion_characters.hpp"
#include "companion_chats.hpp"
#include "json.hpp"
#include "../library/game_index.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <set>

namespace companion::cognition {
namespace {
constexpr size_t reflection_limit = 1200;
constexpr size_t thought_limit = 120;
constexpr size_t gossip_limit = 60;
constexpr size_t reading_limit = 60;
constexpr size_t synapse_limit = 4000;
constexpr size_t schema_limit = 400;

std::wstring cleanLine(std::wstring_view text, size_t max_chars) {
    std::wstring out;
    for (const wchar_t ch : text) {
        const wchar_t c = (ch == L'\n' || ch == L'\r' || ch == L'\t') ? L' ' : ch;
        if (c == L' ' && (out.empty() || out.back() == L' ')) continue;
        out += c;
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    if (out.size() > max_chars) {
        const auto cut = out.rfind(L' ', max_chars);
        out.resize(cut == std::wstring::npos || cut < max_chars / 2 ? max_chars : cut);
        out += L"…";
    }
    return out;
}

std::filesystem::path file(const std::filesystem::path& config) {
    return config / L"companion_cognition.dat";
}

// Ebbinghaus retention factor in [0.25 .. 1.0], half-life ~30 days.
float ebbinghaus(uint64_t at, uint64_t now) {
    if (!at || now <= at) return 1.0f;
    const double days = static_cast<double>(now - at) / 86400.0;
    return static_cast<float>(std::clamp(0.25 + 0.75 * std::exp(-days / 30.0), 0.25, 1.0));
}
} // namespace

void State::reflect(std::wstring_view text, uint64_t now, std::string_view look) {
    auto line = cleanLine(text, 280);
    if (line.size() < 6 || line == L"-") return;
    const auto stems = chats::searchWords(line, 16);
    std::wstring evolved;
    // An older reflection of hers on the same theme: at least two shared words that carry meaning
    // ("gracz", "nie", "się" do not), and a good part of the shorter one's.
    for (auto it = reflections.begin(); it != reflections.end(); ++it) {
        if (!look.empty() && !it->look.empty() && it->look != look) continue;
        if (game_index::fold(it->text) == game_index::fold(line)) {
            it->at = now;
            return;
        }
        const auto old_stems = chats::searchWords(it->text, 16);
        size_t shared = 0;
        const float part = chats::overlap(stems, old_stems, &shared);
        if (shared >= 2 && part >= 0.4f) {
            evolved = cleanLine(it->text, 140);
            reflections.erase(it);
            break;
        }
    }
    reflections.push_back({now, std::move(line), std::string(look), std::move(evolved)});
    if (reflections.size() > reflection_limit)
        reflections.erase(reflections.begin(), reflections.end() - static_cast<std::ptrdiff_t>(reflection_limit));
    learnHebbian(reflections.back().text, now);
}

void State::addSchema(std::wstring_view text, uint64_t now, std::string_view look) {
    auto line = cleanLine(text, 240);
    if (line.size() < 8 || line == L"-") return;
    const auto stems = chats::searchWords(line, 16);
    for (auto& existing : schemas) {
        if (!look.empty() && !existing.look.empty() && existing.look != look) continue;
        if (game_index::fold(existing.text) == game_index::fold(line)) {
            existing.at = now;
            return;
        }
        size_t shared = 0;
        if (chats::overlap(stems, chats::searchWords(existing.text, 16), &shared) >= 0.5f && shared >= 2) {
            existing.text = std::move(line);
            existing.at = now;
            learnHebbian(existing.text, now);
            return;
        }
    }
    schemas.push_back({now, std::move(line), std::string(look)});
    if (schemas.size() > schema_limit)
        schemas.erase(schemas.begin(), schemas.end() - static_cast<std::ptrdiff_t>(schema_limit));
    learnHebbian(schemas.back().text, now);
}

std::string State::peerKey(std::string_view a, std::string_view b) {
    if (a.empty() || b.empty() || a == b) return {};
    return a < b ? std::string(a) + ":" + std::string(b) : std::string(b) + ":" + std::string(a);
}

int State::peerTension(std::string_view a, std::string_view b) const {
    const auto k = peerKey(a, b);
    if (k.empty()) return 0;
    const auto found = peer_tension.find(k);
    return found == peer_tension.end() ? 0 : std::clamp(found->second, 0, 100);
}

void State::adjustPeerTension(std::string_view a, std::string_view b, int delta) {
    const auto k = peerKey(a, b);
    if (k.empty() || !delta) return;
    const int next = std::clamp(peerTension(a, b) + delta, 0, 100);
    if (next <= 0) peer_tension.erase(k);
    else peer_tension[k] = next;
}

void State::setDisposition(std::string_view look, std::wstring_view text) {
    if (look.empty()) return;
    auto line = cleanLine(text, 260);
    if (line.size() < 6 || line == L"-") return;
    dispositions[std::string(look)] = std::move(line);
}

std::wstring State::dispositionOf(std::string_view look) const {
    const auto found = dispositions.find(look);
    return found == dispositions.end() ? std::wstring{} : found->second;
}

void State::think(std::wstring_view text, Emotion emotion, uint64_t now, std::string_view look) {
    auto line = cleanLine(text, 220);
    if (line.size() < 4) return;
    if (!thoughts.empty() && thoughts.back().look == look && game_index::fold(thoughts.back().text) == game_index::fold(line)) {
        thoughts.back().at = now;
        thoughts.back().emotion = emotion;
        return;
    }
    thoughts.push_back({now, std::move(line), std::string(look), emotion});
    if (thoughts.size() > thought_limit)
        thoughts.erase(thoughts.begin(), thoughts.end() - static_cast<std::ptrdiff_t>(thought_limit));
}

void State::postGossip(std::string_view subject_look, std::string_view kind, std::wstring_view detail, uint64_t now, std::string_view other_look) {
    if (subject_look.empty()) return;
    auto clean = cleanLine(detail, 140);
    std::erase_if(gossip, [&](const Gossip& g) { return g.subject_look == subject_look && g.kind == kind && g.detail == clean; });
    gossip.push_back({now, std::string(subject_look), std::string(other_look), std::string(kind), std::move(clean)});
    if (gossip.size() > gossip_limit)
        gossip.erase(gossip.begin(), gossip.end() - static_cast<std::ptrdiff_t>(gossip_limit));
    if (!other_look.empty() && other_look != subject_look) {
        if (kind == "duo") adjustPeerTension(subject_look, other_look, 8);
        else if (kind == "secret") adjustPeerTension(subject_look, other_look, 6);
    }
}

void State::readOnline(std::string_view look, int today_yyyymmdd, std::wstring_view topic, std::wstring_view summary, uint64_t now, bool asked) {
    auto clean_topic = cleanLine(topic, 90);
    auto clean_sum = cleanLine(summary, 280);
    if (clean_sum.size() < 8) return;
    std::erase_if(readings, [&](const Reading& r) { return r.look == look && game_index::fold(r.summary) == game_index::fold(clean_sum); });
    readings.push_back({now, today_yyyymmdd, std::string(look), std::move(clean_topic), std::move(clean_sum), asked});
    if (readings.size() > reading_limit)
        readings.erase(readings.begin(), readings.end() - static_cast<std::ptrdiff_t>(reading_limit));
    learnHebbian(readings.back().summary, now);
}

bool State::dailyReadingDue(std::string_view look, int today_yyyymmdd, uint64_t now) const {
    if (today_yyyymmdd <= 0) return false;
    int count = 0;
    uint64_t last = 0;
    for (const auto& r : readings) {
        if (r.look != look || r.asked) continue;
        if (r.day == today_yyyymmdd) ++count;
        last = std::max(last, r.at);
    }
    return count < readings_per_day && (!last || now >= last + reading_gap);
}

void State::learnHebbian(std::wstring_view text, uint64_t now) {
    const auto words = chats::searchWords(text);
    if (words.size() < 2) return;
    for (size_t i = 0; i < words.size() && i < 6; ++i) {
        for (size_t j = i + 1; j < words.size() && j < 6; ++j) {
            std::wstring a = words[i], b = words[j];
            if (a == b) continue;
            if (b < a) std::swap(a, b);
            auto found = std::find_if(synapses.begin(), synapses.end(), [&](const HebbianSynapse& s) { return s.a == a && s.b == b; });
            if (found != synapses.end()) {
                found->weight = std::min(1.0f, found->weight + 0.22f); // Long-Term Potentiation.
                found->updated = now;
            } else {
                synapses.push_back({std::move(a), std::move(b), 0.38f, now});
            }
        }
    }
    if (synapses.size() > synapse_limit) {
        std::sort(synapses.begin(), synapses.end(), [&](const HebbianSynapse& x, const HebbianSynapse& y) {
            return x.weight * ebbinghaus(x.updated, now) > y.weight * ebbinghaus(y.updated, now);
        });
        synapses.resize(synapse_limit);
    }
}

bool State::removeReflection(uint64_t at, std::wstring_view text) {
    const auto before = reflections.size();
    std::erase_if(reflections, [&](const Reflection& r) { return (at && r.at == at && r.text == text) || (!at && r.text == text); });
    if (reflections.size() == before && !text.empty())
        std::erase_if(reflections, [&](const Reflection& r) { return r.text == text; });
    return reflections.size() != before;
}

bool State::removeThought(uint64_t at, std::wstring_view text) {
    const auto before = thoughts.size();
    std::erase_if(thoughts, [&](const Thought& t) { return (at && t.at == at && t.text == text) || (!at && t.text == text); });
    if (thoughts.size() == before && !text.empty())
        std::erase_if(thoughts, [&](const Thought& t) { return t.text == text; });
    return thoughts.size() != before;
}

bool State::removeReading(uint64_t at, std::wstring_view text) {
    const auto before = readings.size();
    std::erase_if(readings, [&](const Reading& r) { return (at && r.at == at) || (!at && r.summary == text); });
    if (readings.size() == before && !text.empty())
        std::erase_if(readings, [&](const Reading& r) { return r.summary == text || (r.topic + L": " + r.summary) == text; });
    return readings.size() != before;
}

bool State::removeSchema(uint64_t at, std::wstring_view text) {
    const auto before = schemas.size();
    std::erase_if(schemas, [&](const Schema& s) { return (at && s.at == at && s.text == text) || (!at && s.text == text); });
    if (schemas.size() == before && !text.empty())
        std::erase_if(schemas, [&](const Schema& s) { return s.text == text; });
    return schemas.size() != before;
}

std::vector<ActivationHit> State::spreadActivation(const Memory& memory, std::string_view look, std::wstring_view query, uint64_t now) const {
    std::vector<ActivationHit> results;
    const auto seeds = chats::searchWords(query);
    if (seeds.empty()) return results;

    // Hop 0 -> Hop 1 -> Hop 2 concept excitation map: stem -> (activation, bridge_stem), and the hop
    // at which each concept first lit up.
    std::map<std::wstring, std::pair<float, std::wstring>> concept_energy;
    std::map<std::wstring, int> lit_at;
    for (const auto& s : seeds) { concept_energy[s] = {1.0f, {}}; lit_at[s] = 0; }

    // Collect all candidate memory records (notes, summaries, reflections, picks) with their stems.
    struct Candidate {
        std::wstring text;
        uint64_t at = 0;
        float base_boost = 1.0f;
        std::vector<std::wstring> stems;
    };
    std::vector<Candidate> pool;
    for (const auto& n : memory.notes)
        if (Memory::knows(n, look)) pool.push_back({n.text, n.at, 1.05f, chats::searchWords(n.text, 16)});
    for (const auto& s : memory.summaries)
        if (look.empty() || s.look.empty() || s.look == look)
            pool.push_back({s.text, s.at, 1.0f, chats::searchWords(s.text, 16)});
    for (const auto& r : reflections)
        if (look.empty() || r.look.empty() || r.look == look)
            pool.push_back({r.text, r.at, 1.25f, chats::searchWords(r.text, 16)});
    for (const auto& rd : readings)
        if (look.empty() || rd.look.empty() || rd.look == look)
            pool.push_back({rd.topic + L": " + rd.summary, rd.at, 1.1f, chats::searchWords(rd.topic + L" " + rd.summary, 16)});
    for (const auto& sc : schemas)
        if (look.empty() || sc.look.empty() || sc.look == look)
            pool.push_back({sc.text, sc.at, 1.3f, chats::searchWords(sc.text, 16)});

    // Propagate concept energy across Hebbian synapses and co-occurring memory stems (2 hops).
    for (int hop = 1; hop <= 2; ++hop) {
        const float damping = hop == 1 ? 0.68f : 0.44f;
        auto next_energy = concept_energy;
        // 1) Hebbian edges.
        for (const auto& syn : synapses) {
            const float w = syn.weight * ebbinghaus(syn.updated, now) * damping;
            if (const auto it = concept_energy.find(syn.a); it != concept_energy.end()) {
                const float e = it->second.first * w;
                if (e > next_energy[syn.b].first) next_energy[syn.b] = {e, it->second.second.empty() ? syn.a : it->second.second};
            }
            if (const auto it = concept_energy.find(syn.b); it != concept_energy.end()) {
                const float e = it->second.first * w;
                if (e > next_energy[syn.a].first) next_energy[syn.a] = {e, it->second.second.empty() ? syn.b : it->second.second};
            }
        }
        // 2) Co-occurrence inside episodic/semantic nodes (HippoRAG bipartite propagation).
        for (const auto& item : pool) {
            float max_in = 0;
            std::wstring bridge;
            for (const auto& st : item.stems) {
                if (const auto it = concept_energy.find(st); it != concept_energy.end() && it->second.first > max_in) {
                    max_in = it->second.first;
                    bridge = it->second.second.empty() ? st : it->second.second;
                }
            }
            if (max_in > 0.2f) {
                const float spread = max_in * damping * ebbinghaus(item.at, now);
                for (const auto& st : item.stems) {
                    if (spread > next_energy[st].first) next_energy[st] = {spread, bridge};
                }
            }
        }
        for (const auto& [stem, energy] : next_energy)
            if (energy.first > 0 && !lit_at.contains(stem)) lit_at[stem] = hop;
        concept_energy = std::move(next_energy);
    }

    // Score every candidate node by total activated energy; its hop is the nearest a concept of it lit up at
    // (0: a word of the message itself; 1 or 2: an association).
    for (const auto& item : pool) {
        float total = 0;
        int min_hop = 3;
        std::wstring best_via;
        for (const auto& st : item.stems) {
            const auto it = concept_energy.find(st);
            if (it == concept_energy.end() || it->second.first <= 0) continue;
            total += it->second.first;
            const auto lit = lit_at.find(st);
            const int h = lit == lit_at.end() ? 2 : lit->second;
            if (h < min_hop) min_hop = h;
            if (h > 0 && best_via.empty()) best_via = it->second.second;
        }
        if (total > 0.25f) {
            const float score = total * item.base_boost * ebbinghaus(item.at, now);
            results.push_back({item.text, min_hop == 0 ? std::wstring{} : best_via, score, std::min(min_hop, 2)});
        }
    }
    // Most activated first: a direct hit outweighs a faint association.
    std::stable_sort(results.begin(), results.end(), [](const ActivationHit& a, const ActivationHit& b) { return a.score > b.score; });
    if (results.size() > 6) results.resize(6);
    return results;
}

std::wstring State::prompt(const Memory& memory, std::string_view look, std::wstring_view query, uint64_t now, bool english,
                           const std::function<std::wstring(std::string_view)>& look_name) const {
    std::wstring out;

    // 0. Her evolving long-term emotional disposition/attitude toward the player.
    if (const auto disp = dispositionOf(look); !disp.empty()) {
        out += english ? L"\nYour current long-term emotional attitude towards ‹him|her|them› (shaped by your history together): " + disp + L"\n"
                       : L"\nTwoje obecne długofalowe nastawienie emocjonalne do ‹niego|niej› (ukształtowane przez waszą historię): " + disp + L"\n";
    }

    // 1. Higher-order reflections & belief evolution (Sleep consolidation).
    std::vector<const Reflection*> own_refs;
    for (auto it = reflections.rbegin(); it != reflections.rend() && own_refs.size() < 4; ++it)
        if (look.empty() || it->look.empty() || it->look == look) own_refs.push_back(&*it);
    if (!own_refs.empty()) {
        out += english ? L"\nYour higher-order reflections and intuitions about ‹him|her|them› (built across past talks):\n"
                       : L"\nTwoje wnioski wyższego rzędu i intuicje o ‹nim|niej› (zbudowane na podstawie poprzednich rozmów):\n";
        for (auto it = own_refs.rbegin(); it != own_refs.rend(); ++it) {
            out += L"- " + (*it)->text;
            if (!(*it)->evolved_from.empty())
                out += (english ? L" (evolved from your earlier view: \"" : L" (zmieniłaś zdanie — wcześniej myślałaś: „") + (*it)->evolved_from + (english ? L"\")" : L"”)");
            out += L"\n";
        }
    }

    // 2. Her recent subconscious thoughts (<mysl>), so her inner monologue stays continuous.
    std::vector<const Thought*> own_thoughts;
    for (auto it = thoughts.rbegin(); it != thoughts.rend() && own_thoughts.size() < 2; ++it)
        if (look.empty() || it->look == look) own_thoughts.push_back(&*it);
    if (!own_thoughts.empty()) {
        out += english ? L"What you were secretly thinking lately: " : L"Co ostatnio myślałaś po cichu w duchu: ";
        for (size_t i = 0; i < own_thoughts.size(); ++i) out += (i ? L" | " : L"") + own_thoughts[i]->text;
        out += L"\n";
    }

    // 3. Spreading Activation associative chain triggered by `query`.
    if (!query.empty()) {
        const auto hits = spreadActivation(memory, look, query, now);
        std::vector<const ActivationHit*> bridges;
        for (const auto& h : hits) if (h.hop >= 1 && bridges.size() < 3) bridges.push_back(&h);
        if (!bridges.empty()) {
            out += english ? L"Associative memories that just lit up in your mind (you may connect them naturally if it fits):\n"
                           : L"Skojarzenia, które właśnie zapaliły się w twojej głowie (możesz do nich naturalnie nawiązać, jeśli pasuje):\n";
            for (const auto* b : bridges) {
                out += L"- ";
                if (!b->via.empty()) out += L"[" + b->via + L" →] ";
                out += b->text + L"\n";
            }
        }
    }

    // 4. Articles/entries she browsed on her own today or recently via Tavily.
    std::vector<const Reading*> own_readings;
    for (auto it = readings.rbegin(); it != readings.rend() && own_readings.size() < 2; ++it)
        if ((look.empty() || it->look == look) && (now <= it->at || now - it->at <= 3 * 86400)) own_readings.push_back(&*it);
    if (!own_readings.empty()) {
        out += english ? L"\nWhat you browsed and read online on your own lately (you may bring it up naturally as something you stumbled upon on the web):\n"
                       : L"\nCo ostatnio sama przeglądałaś i wyczytałaś w sieci (możesz o tym wspomnieć naturalnie jako o czymś, na co sama trafiłaś w necie):\n";
        for (const auto* rd : own_readings)
            out += L"- [" + rd->topic + L"] " + rd->summary + L"\n";
    }

    // 5. Shared Inter-Companion Gossip Blackboard (events from the last 5 days involving other companions).
    if (!look.empty()) {
        for (auto it = gossip.rbegin(); it != gossip.rend(); ++it) {
            if (it->subject_look == look || (now > it->at && now - it->at > 5 * 86400)) continue;
            const std::wstring who = look_name ? look_name(it->subject_look) : text_encoding::wideLenient(it->subject_look);
            if (it->kind == "level") {
                out += english ? L"\n(Gossip among companions: you heard ‹he|she|they› reached relationship level " + it->detail + L" with " + who + L".)\n"
                               : L"\n(Plotki wśród towarzyszek: obiło ci się o uszy, że ‹on wbił|ona wbiła› już poziom " + it->detail + L" u " + who + L".)\n";
                break;
            }
            if (it->kind == "card") {
                out += english ? L"\n(Gossip among companions: you heard " + who + L" recently gave the person you're chatting with a warning card for crossing a boundary.)\n"
                               : L"\n(Plotki wśród towarzyszek: słyszałaś, że " + who + L" musiała ostatnio postawić ‹mu|jej› granicę i dała ‹mu|jej› kartkę.)\n";
                break;
            }
            if (it->kind == "played") {
                out += english ? L"\n(Gossip among companions: " + who + L" bragged that the person you're chatting with has been playing [[" + it->detail + L"]] on her recommendation.)\n"
                               : L"\n(Plotki wśród towarzyszek: " + who + L" chwaliła się, że osoba, z którą piszesz, grała w [[" + it->detail + L"]] z jej polecenia.)\n";
                break;
            }
        }
    }
    return out;
}

std::vector<mind::Candidate> State::candidates(const Memory& memory, std::string_view look, std::wstring_view query, uint64_t now, bool english,
                                               const std::function<std::wstring(std::string_view)>& look_name) const {
    std::vector<mind::Candidate> out;
    using mind::Status;
    const auto who = [&](std::string_view key) { return look_name ? look_name(key) : text_encoding::wideLenient(key); };
    if (const auto disp = dispositionOf(look); !disp.empty())
        out.push_back({"attitude", (english ? L"Your long-term attitude towards ‹him|her|them› (from your history together): " : L"Twoje długofalowe nastawienie do ‹niego|niej› (z waszej historii): ") + disp,
                       0.5f, Status::feel});
    // Her latest reflections (the newest is the most alive), plus older views that may need re-conceptualization after time.
    int shown = 0;
    const auto q_words = chats::searchWords(query);
    for (auto it = reflections.rbegin(); it != reflections.rend(); ++it) {
        if (!look.empty() && !it->look.empty() && it->look != look) continue;
        if (shown < 2) {
            std::wstring text = (english ? L"Your insight about ‹him|her|them›: " : L"Twój wniosek o ‹nim|niej›: ") + it->text;
            if (!it->evolved_from.empty()) text += english ? L" (you changed your mind: earlier you thought \"" + it->evolved_from + L"\")"
                                                           : L" (zmieniłaś zdanie — wcześniej myślałaś: „" + it->evolved_from + L"”)";
            out.push_back({"reflection", std::move(text), shown == 0 ? 0.45f : 0.3f, Status::guess});
            ++shown;
        } else if (it->evolved_from.empty() && it->at && now >= it->at + 3 * 86400 &&
                   (q_words.empty() || chats::matches(q_words, it->text) > 0)) {
            out.push_back({"rethink", (english ? L"An older view of yours from days ago (rethink/re-conceptualize whether it still holds now): "
                                               : L"Twój dawniejszy wniosek sprzed kilku dni (przemyśl na nowo / skonceptualizuj, czy nadal tak uważasz): ") + it->text,
                           !q_words.empty() ? 0.48f : 0.28f, Status::guess});
            break;
        }
    }
    // CLS Neocortical causal schemas abstracted from past episodes.
    int shown_schemas = 0;
    for (auto it = schemas.rbegin(); it != schemas.rend() && shown_schemas < 2; ++it) {
        if (!look.empty() && !it->look.empty() && it->look != look) continue;
        const bool touched = !q_words.empty() && chats::matches(q_words, it->text) > 0;
        out.push_back({"schema", (english ? L"Pattern you noticed about ‹him|her|them› (causal schema): " : L"Wzorzec, który zauważyłaś u ‹niego|niej› (schemat przyczynowy): ") + it->text,
                       touched ? 0.58f : 0.36f, Status::know});
        ++shown_schemas;
    }
    // Dynamic 7x7 inter-companion peer tension (spats / rivalry).
    if (!look.empty()) {
        for (const auto& [pair_key, tension_val] : peer_tension) {
            if (tension_val < 35) continue;
            const auto colon = pair_key.find(':');
            if (colon == std::string::npos) continue;
            const auto a = std::string_view(pair_key).substr(0, colon), b = std::string_view(pair_key).substr(colon + 1);
            if (a != look && b != look) continue;
            const auto other_name = who(a == look ? b : a);
            out.push_back({"peer", english ? L"You currently have friction/tension with " + other_name + L" (a recent spat or rivalry — you may be prickly about her)."
                                           : L"Masz teraz na pieńku z " + other_name + L" (ostatnie spięcie lub rywalizacja — możesz być wobec niej uszczypliwa).",
                           0.32f + 0.004f * static_cast<float>(tension_val), Status::feel});
            break;
        }
    }
    for (auto it = thoughts.rbegin(); it != thoughts.rend(); ++it) {
        if (!look.empty() && it->look != look) continue;
        if (now > it->at + 2 * 86400) break;
        out.push_back({"thought", (english ? L"What you last thought to yourself: " : L"Co ostatnio pomyślałaś po cichu: ") + it->text, 0.3f, Status::remember});
        break;
    }
    // Associations the player's message just lit up.
    if (!query.empty()) {
        int bridges = 0;
        for (const auto& hit : spreadActivation(memory, look, query, now)) {
            if (hit.hop == 0 || bridges >= 2) continue;
            out.push_back({"association", (english ? L"An association that just lit up" : L"Skojarzenie, które właśnie ci się zapaliło") +
                                              (hit.via.empty() ? std::wstring{} : L" (" + hit.via + L" →)") + L": " + hit.text,
                           std::min(0.7f, 0.35f + 0.1f * hit.score), Status::remember});
            ++bridges;
        }
    }
    int read = 0;
    for (auto it = readings.rbegin(); it != readings.rend() && read < 2; ++it) {
        if ((!look.empty() && it->look != look) || now > it->at + 3 * 86400) continue;
        const std::wstring how = it->asked ? (english ? L"You looked it up online for ‹him|her|them› in a chat: [" : L"Sprawdziłaś w sieci dla ‹niego|niej› w rozmowie: [")
                                           : (english ? L"You read online on your own (bring it up as something you stumbled upon): ["
                                                      : L"Sama wyczytałaś w sieci (możesz wspomnieć jako o czymś, na co trafiłaś): [");
        out.push_back({"reading", how + it->topic + L"] " + it->summary, read == 0 ? 0.4f : 0.28f, Status::know});
        ++read;
    }
    // Gossip among the companions (the last five days, about the others).
    if (!look.empty()) {
        int heard = 0;
        for (auto it = gossip.rbegin(); it != gossip.rend() && heard < 2; ++it) {
            if (now > it->at && now - it->at > 5 * 86400) continue;
            const auto subject = who(it->subject_look);
            std::wstring text;
            float salience = 0.4f;
            if (it->kind == "secret") {
                if (it->other_look != look) continue;
                text = english ? subject + L" let slip to you something about the person you're chatting with: " + it->detail + L" (gossip — you are not sure it is true; you may give away that you know, or keep it)"
                               : subject + L" wygadała ci o ‹nim|niej›: " + it->detail + L" (plotka — nie wiesz, czy to prawda; możesz się zdradzić, że wiesz, albo to zachować)";
                salience = 0.6f;
            } else if (it->subject_look == look) continue;
            else if (it->kind == "level")
                text = english ? L"You heard ‹he|she|they› reached relationship level " + it->detail + L" with " + subject + L"."
                               : L"Obiło ci się o uszy, że ‹on wbił|ona wbiła› już poziom " + it->detail + L" u " + subject + L".";
            else if (it->kind == "card")
                text = english ? L"You heard " + subject + L" recently had to set the person you're chatting with a boundary." : L"Słyszałaś, że " + subject + L" musiała ostatnio postawić ‹mu|jej› granicę.";
            else if (it->kind == "played")
                text = english ? subject + L" bragged that the person you're chatting with has been playing [[" + it->detail + L"]] on her recommendation."
                               : subject + L" chwaliła się, że osoba, z którą piszesz, grała w [[" + it->detail + L"]] z jej polecenia.";
            else if (it->kind == "duo") {
                const auto other = who(it->other_look);
                if (it->other_look == look) continue;
                text = english ? L"You heard " + subject + L" and " + other + L" had a three-way chat with the person you're chatting with."
                               : L"Słyszałaś, że " + subject + L" i " + other + L" gadały niedawno z ‹nim|nią› we trójkę.";
                salience = 0.3f;
            } else if (it->kind == "stance") { // companion_affinity: her posture towards the player changed.
                if (it->detail == L"dilemma")
                    text = english ? L"You heard " + subject + L" has been torn about the person you're chatting with lately." : L"Słyszałaś, że " + subject + L" ma ostatnio rozterki co do ‹niego|niej›.";
                else if (it->detail == L"distanced")
                    text = english ? L"You heard " + subject + L" is keeping the person you're chatting with at a distance now." : L"Słyszałaś, że " + subject + L" trzyma się teraz od ‹niego|niej› na dystans.";
                else if (it->detail == L"broken") {
                    text = english ? L"You heard something sparked between " + subject + L" and the person you're chatting with." : L"Słyszałaś, że między " + subject + L" a ‹nim|nią› coś zaiskrzyło.";
                    salience = 0.5f;
                } else continue;
            } else continue;
            out.push_back({"gossip", std::move(text), salience, Status::gossip});
            ++heard;
        }
    }
    return out;
}

std::string State::save() const {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    auto refs = json::array();
    for (const auto& r : reflections) {
        auto item = json::object();
        item.properties["at"] = json::number(static_cast<double>(r.at));
        item.properties["text"] = json::string(text_encoding::utf8Lenient(r.text));
        if (!r.look.empty()) item.properties["look"] = json::string(r.look);
        if (!r.evolved_from.empty()) item.properties["evolved_from"] = json::string(text_encoding::utf8Lenient(r.evolved_from));
        refs.elements.push_back(std::move(item));
    }
    root.properties["reflections"] = std::move(refs);

    auto ths = json::array();
    for (const auto& t : thoughts) {
        auto item = json::object();
        item.properties["at"] = json::number(static_cast<double>(t.at));
        item.properties["text"] = json::string(text_encoding::utf8Lenient(t.text));
        if (!t.look.empty()) item.properties["look"] = json::string(t.look);
        item.properties["emotion"] = json::string(emotionKey(t.emotion));
        ths.elements.push_back(std::move(item));
    }
    root.properties["thoughts"] = std::move(ths);

    auto gos = json::array();
    for (const auto& g : gossip) {
        auto item = json::object();
        item.properties["at"] = json::number(static_cast<double>(g.at));
        item.properties["subject"] = json::string(g.subject_look);
        if (!g.other_look.empty()) item.properties["other"] = json::string(g.other_look);
        item.properties["kind"] = json::string(g.kind);
        item.properties["detail"] = json::string(text_encoding::utf8Lenient(g.detail));
        gos.elements.push_back(std::move(item));
    }
    root.properties["gossip"] = std::move(gos);

    auto rds = json::array();
    for (const auto& rd : readings) {
        auto item = json::object();
        item.properties["at"] = json::number(static_cast<double>(rd.at));
        item.properties["day"] = json::number(rd.day);
        if (!rd.look.empty()) item.properties["look"] = json::string(rd.look);
        item.properties["topic"] = json::string(text_encoding::utf8Lenient(rd.topic));
        item.properties["summary"] = json::string(text_encoding::utf8Lenient(rd.summary));
        if (rd.asked) item.properties["asked"] = json::boolean(true);
        rds.elements.push_back(std::move(item));
    }
    root.properties["readings"] = std::move(rds);

    auto syns = json::array();
    for (const auto& s : synapses) {
        auto item = json::object();
        item.properties["a"] = json::string(text_encoding::utf8Lenient(s.a));
        item.properties["b"] = json::string(text_encoding::utf8Lenient(s.b));
        item.properties["w"] = json::number(s.weight);
        item.properties["u"] = json::number(static_cast<double>(s.updated));
        syns.elements.push_back(std::move(item));
    }
    root.properties["synapses"] = std::move(syns);
    if (!schemas.empty()) {
        auto scs = json::array();
        for (const auto& sc : schemas) {
            auto item = json::object();
            item.properties["at"] = json::number(static_cast<double>(sc.at));
            item.properties["text"] = json::string(text_encoding::utf8Lenient(sc.text));
            if (!sc.look.empty()) item.properties["look"] = json::string(sc.look);
            scs.elements.push_back(std::move(item));
        }
        root.properties["schemas"] = std::move(scs);
    }
    if (!peer_tension.empty()) {
        auto pt = json::object();
        for (const auto& [k, v] : peer_tension)
            if (!k.empty() && v > 0) pt.properties[k] = json::number(v);
        root.properties["peer_tension"] = std::move(pt);
    }
    if (!dispositions.empty()) {
        auto disp = json::object();
        for (const auto& [k, v] : dispositions)
            if (!k.empty() && !v.empty()) disp.properties[k] = json::string(text_encoding::utf8Lenient(v));
        root.properties["dispositions"] = std::move(disp);
    }
    return json::serialize(root);
}

std::optional<State> State::load(std::string_view text) {
    const auto root = json::parse(text, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object || !root->get("version") || root->get("version")->intOr() != 1)
        return std::nullopt;
    State out;
    if (const auto* refs = root->get("reflections"); refs && refs->type == json::Value::Type::array) {
        for (const auto& item : refs->elements) {
            const auto* t = item.get("text");
            if (!t || t->type != json::Value::Type::string || t->text.empty()) continue;
            const auto* at = item.get("at");
            const auto* look = item.get("look");
            const auto* ev = item.get("evolved_from");
            out.reflections.push_back({
                at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0,
                text_encoding::wideLenient(t->text),
                look ? look->stringOr() : std::string{},
                ev ? text_encoding::wideLenient(ev->stringOr()) : std::wstring{},
            });
            if (out.reflections.size() >= reflection_limit) break;
        }
    }
    if (const auto* ths = root->get("thoughts"); ths && ths->type == json::Value::Type::array) {
        for (const auto& item : ths->elements) {
            const auto* t = item.get("text");
            if (!t || t->type != json::Value::Type::string || t->text.empty()) continue;
            const auto* at = item.get("at");
            const auto* look = item.get("look");
            const auto* em = item.get("emotion");
            out.thoughts.push_back({
                at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0,
                text_encoding::wideLenient(t->text),
                look ? look->stringOr() : std::string{},
                em ? emotionFromKey(em->stringOr()).value_or(calm) : calm,
            });
            if (out.thoughts.size() >= thought_limit) break;
        }
    }
    if (const auto* gos = root->get("gossip"); gos && gos->type == json::Value::Type::array) {
        for (const auto& item : gos->elements) {
            const auto* sub = item.get("subject");
            const auto* k = item.get("kind");
            if (!sub || !k) continue;
            const auto* at = item.get("at");
            const auto* oth = item.get("other");
            const auto* det = item.get("detail");
            out.gossip.push_back({
                at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0,
                sub->stringOr(),
                oth ? oth->stringOr() : std::string{},
                k->stringOr(),
                det ? text_encoding::wideLenient(det->stringOr()) : std::wstring{},
            });
            if (out.gossip.size() >= gossip_limit) break;
        }
    }
    if (const auto* rds = root->get("readings"); rds && rds->type == json::Value::Type::array) {
        for (const auto& item : rds->elements) {
            const auto* sum = item.get("summary");
            if (!sum || sum->type != json::Value::Type::string || sum->text.empty()) continue;
            const auto* at = item.get("at");
            const auto* day = item.get("day");
            const auto* look = item.get("look");
            const auto* top = item.get("topic");
            const auto* asked = item.get("asked");
            out.readings.push_back({
                at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0,
                day && day->type == json::Value::Type::number ? day->intOr() : 0,
                look ? look->stringOr() : std::string{},
                top ? text_encoding::wideLenient(top->stringOr()) : std::wstring{},
                text_encoding::wideLenient(sum->text),
                asked && asked->type == json::Value::Type::boolean && asked->flag,
            });
            if (out.readings.size() >= reading_limit) break;
        }
    }
    if (const auto* syns = root->get("synapses"); syns && syns->type == json::Value::Type::array) {
        for (const auto& item : syns->elements) {
            const auto* a = item.get("a");
            const auto* b = item.get("b");
            const auto* w = item.get("w");
            const auto* u = item.get("u");
            if (!a || !b || !w) continue;
            out.synapses.push_back({
                text_encoding::wideLenient(a->stringOr()),
                text_encoding::wideLenient(b->stringOr()),
                static_cast<float>(w->numeric),
                u && u->type == json::Value::Type::number && u->numeric > 0 ? static_cast<uint64_t>(u->numeric) : 0,
            });
            if (out.synapses.size() >= synapse_limit) break;
        }
    }
    if (const auto* scs = root->get("schemas"); scs && scs->type == json::Value::Type::array) {
        for (const auto& item : scs->elements) {
            const auto* t = item.get("text");
            if (!t || t->type != json::Value::Type::string || t->text.empty()) continue;
            const auto* at = item.get("at");
            const auto* look = item.get("look");
            out.schemas.push_back({
                at && at->type == json::Value::Type::number && at->numeric > 0 ? static_cast<uint64_t>(at->numeric) : 0,
                cleanLine(text_encoding::wideLenient(t->text), 240),
                look ? look->stringOr() : std::string{},
            });
            if (out.schemas.size() >= schema_limit) break;
        }
    }
    if (const auto* pt = root->get("peer_tension"); pt && pt->type == json::Value::Type::object) {
        for (const auto& [k, v] : pt->properties) {
            if (k.size() <= 68 && v.type == json::Value::Type::number && v.numeric > 0)
                out.peer_tension[k] = std::clamp(static_cast<int>(v.numeric), 0, 100);
        }
    }
    if (const auto* disp = root->get("dispositions"); disp && disp->type == json::Value::Type::object) {
        for (const auto& [k, v] : disp->properties) {
            if (k.size() <= 32 && v.type == json::Value::Type::string && !v.text.empty())
                out.dispositions[k] = cleanLine(text_encoding::wideLenient(v.text), 260);
        }
    }
    return out;
}

State loadState(const std::filesystem::path& config) {
    const auto bytes = protected_file::read(file(config), 4 * 1024 * 1024);
    if (!bytes) return {};
    return State::load(*bytes).value_or(State{});
}

bool saveState(const std::filesystem::path& config, const State& state) {
    if (state.empty()) return clearState(config);
    std::error_code ec;
    std::filesystem::create_directories(config, ec);
    return protected_file::write(file(config), state.save(), L"Launchme companion cognition");
}

bool clearState(const std::filesystem::path& config) {
    std::error_code ec;
    std::filesystem::remove(file(config), ec);
    return !ec;
}

std::wstring sleepConsolidationPrompt(bool english, std::wstring_view telemetry_hint, std::wstring_view current_disposition, std::wstring_view life,
                                      bool invite_goal, std::wstring_view plan_rules) {
    std::wstring extra(telemetry_hint);
    if (!extra.empty()) extra = L" " + extra;
    if (!current_disposition.empty())
        extra += (english ? L" (Your previous attitude towards ‹him|her|them› was: " : L" (Twoje dotychczasowe nastawienie do ‹niego|niej›: ") + std::wstring(current_disposition) + L")";
    if (!life.empty()) extra += L" (" + std::wstring(life) + L")";
    std::wstring goal;
    if (invite_goal)
        goal = english ? L"\n4) <new_goal>Only if it comes naturally: one new goal of your own for the coming weeks — from your life and what you have achieved, "
                         L"never about games, something real and doable (a skill, a purchase you save for, a trip, a project), one short sentence.</new_goal>"
                       : L"\n4) <nowy_cel>Tylko jeśli przyjdzie naturalnie: jeden nowy własny cel na najbliższe tygodnie — z twojego życia i tego, co już osiągnęłaś, "
                         L"nigdy o grach, coś realnego i wykonalnego (umiejętność, zakup, na który odkładasz, wyjazd, projekt), jedno krótkie zdanie.</nowy_cel>";
    if (!plan_rules.empty())
        goal += english ? L"\n5) <plans>1–3 of your plans for the coming day (what you are going to do outside this chat), each on its own line. " + std::wstring(plan_rules) + L"</plans>"
                        : L"\n5) <plany>1–3 twoje plany na najbliższą dobę (co będziesz robić poza tą rozmową), każdy w osobnej linii. " + std::wstring(plan_rules) + L"</plany>";
    goal += english ? L"\n6) <schema>Optional: if you noticed a recurring causal pattern in ‹his|her|their› behavior (\"When they X → it means Y\"), write 1 sentence.</schema>"
                      L"\n7) <inside_joke>Optional: if a shared joke, nickname or catchphrase between just the two of you was born in this talk, write it briefly.</inside_joke>"
                      L"\n8) <key_memory>Optional: only if this conversation was a true turning point, deep confession or unforgettable moment between you two, write 1 short sentence to keep forever as a key memory.</key_memory>"
                    : L"\n6) <schemat>Opcjonalnie: jeśli dostrzegłaś powtarzalny wzorzec przyczynowy w ‹jego|jej› zachowaniu („Gdy robi X → oznacza to Y”), zapisz 1 zdanie.</schemat>"
                      L"\n7) <nasz_zart>Opcjonalnie: jeśli w tej rozmowie narodził się wasz wspólny żart, przezwisko lub powiedzonko (tylko dla was dwojga), zapisz je krótko.</nasz_zart>"
                      L"\n8) <wazne_wspomnienie>Opcjonalnie: tylko jeśli ta rozmowa była prawdziwym punktem zwrotnym, głębokim zwierzeniem albo niezapomnianą chwilą między wami, zapisz ją w 1 krótkim zdaniu jako ważne wspomnienie na stałe.</wazne_wspomnienie>";
    if (english)
        return L"(The chat is over. Write in your diary, as yourself — your view, your feelings, your biases are fine. Consolidate your memory in these tags:\n"
               L"1) <summary>1-2 short sentences on what you talked about and what ‹he|she|they› said about ‹himself|herself|themselves› (name games and topics); remember it the way "
               L"people do: by its strongest moment and by how it ended, not by its average.</summary>\n"
               L"2) <reflection>1 short sentence of higher-order psychological insight about ‹his|her|their› current mood, habits, or any gap between what ‹he says|she says|they say› and what ‹he actually plays|she actually plays|they actually play› (or a re-conceptualization of an older topic after time).</reflection>\n"
               L"3) <attitude>1 short sentence in first person updating how you emotionally feel about ‹him|her|them› long-term after this talk (inside jokes, soft spots, jealousy, warmth).</attitude>" +
               goal + extra + L" No emotion tags, no [[ ]]. If nothing is worth remembering, answer just: -)";
    return L"(Rozmowa się skończyła. Zapisz to w swoim pamiętniku, jako ty — z twojej perspektywy, z twoimi uczuciami i uprzedzeniami. Skonsoliduj pamięć w znacznikach:\n"
           L"1) <podsumowanie>1–2 krótkie zdania: co ‹on powiedział|ona powiedziała› o sobie i o czym rozmawialiście (nazwij gry i tematy); zapamiętaj to tak jak ludzie — "
           L"przez najmocniejszy moment rozmowy i to, jak się skończyła, a nie przez średnią.</podsumowanie>\n"
           L"2) <refleksja>1 krótkie zdanie głębszego wniosku o ‹jego|jej› nastroju, nawykach, różnicy między deklaracjami a zachowaniem lub ponownego przemyślenia dawnego tematu po czasie.</refleksja>\n"
           L"3) <nastawienie>1 krótkie zdanie w pierwszej osobie aktualizujące twoje długofalowe emocjonalne nastawienie do ‹niego|niej› po tej rozmowie (wasze inside-joke'i, słabości, o co jesteś zazdrosna, co cię ujęło lub zirytowało).</nastawienie>" +
           goal + extra + L" Bez tagów emocji i bez [[ ]]. Jeśli nie ma nic wartego zapamiętania, odpowiedz tylko: -)";
}

Consolidated parseConsolidation(std::wstring_view raw) {
    Consolidated out;
    const auto extract = [&](std::wstring_view open_tag, std::wstring_view close_tag) -> std::wstring {
        const auto open = raw.find(open_tag);
        if (open == std::wstring_view::npos) return {};
        const auto start = open + open_tag.size();
        const auto close = raw.find(close_tag, start);
        return cleanLine(raw.substr(start, close == std::wstring_view::npos ? std::wstring_view::npos : close - start), 300);
    };
    out.summary = extract(L"<podsumowanie>", L"</podsumowanie>");
    if (out.summary.empty()) out.summary = extract(L"<summary>", L"</summary>");
    out.reflection = extract(L"<refleksja>", L"</refleksja>");
    if (out.reflection.empty()) out.reflection = extract(L"<reflection>", L"</reflection>");
    out.disposition = extract(L"<nastawienie>", L"</nastawienie>");
    if (out.disposition.empty()) out.disposition = extract(L"<attitude>", L"</attitude>");
    out.goal = extract(L"<nowy_cel>", L"</nowy_cel>");
    if (out.goal.empty()) out.goal = extract(L"<new_goal>", L"</new_goal>");
    if (out.goal == L"-") out.goal.clear();
    out.schema = extract(L"<schemat>", L"</schemat>");
    if (out.schema.empty()) out.schema = extract(L"<schema>", L"</schema>");
    if (out.schema == L"-") out.schema.clear();
    out.inside_joke = extract(L"<nasz_zart>", L"</nasz_zart>");
    if (out.inside_joke.empty()) out.inside_joke = extract(L"<inside_joke>", L"</inside_joke>");
    if (out.inside_joke == L"-") out.inside_joke.clear();
    out.key_memory = extract(L"<wazne_wspomnienie>", L"</wazne_wspomnienie>");
    if (out.key_memory.empty()) out.key_memory = extract(L"<key_memory>", L"</key_memory>");
    if (out.key_memory == L"-") out.key_memory.clear();
    // Her plans keep their lines (mind::parseEpisodes reads them).
    for (const auto& [open_tag, close_tag] : {std::pair{L"<plany>", L"</plany>"}, std::pair{L"<plans>", L"</plans>"}}) {
        const auto open = raw.find(open_tag);
        if (open == std::wstring_view::npos) continue;
        const auto start = open + std::wstring_view(open_tag).size();
        const auto close = raw.find(close_tag, start);
        out.plans = std::wstring(raw.substr(start, close == std::wstring_view::npos ? std::wstring_view::npos : close - start));
        if (out.plans.size() > 2000) out.plans.resize(2000);
        break;
    }
    if (out.summary.empty() && out.reflection.empty() && out.disposition.empty()) {
        std::wstring fallback(raw);
        if (const auto plans = fallback.find(L"<pl"); plans != std::wstring::npos) fallback.resize(plans); // Not her summary.
        for (const auto* tag : {L"<podsumowanie>", L"</podsumowanie>", L"<summary>", L"</summary>", L"<refleksja>", L"</refleksja>",
                                L"<reflection>", L"</reflection>", L"<nastawienie>", L"</nastawienie>", L"<attitude>", L"</attitude>",
                                L"<nowy_cel>", L"</nowy_cel>", L"<new_goal>", L"</new_goal>", L"<schemat>", L"</schemat>",
                                L"<schema>", L"</schema>", L"<nasz_zart>", L"</nasz_zart>", L"<inside_joke>", L"</inside_joke>",
                                L"<wazne_wspomnienie>", L"</wazne_wspomnienie>", L"<key_memory>", L"</key_memory>"}) {
            for (auto at = fallback.find(tag); at != std::wstring::npos; at = fallback.find(tag, at))
                fallback.erase(at, std::wstring_view(tag).size());
        }
        out.summary = cleanLine(fallback, 300);
    }
    return out;
}

std::wstring emotionFeelingLabel(Emotion emotion, bool english) {
    struct Label { const wchar_t* pl; const wchar_t* en; };
    static constexpr std::array<Label, emotion_count> labels{{
        {L"Uśmiechnięta", L"Happy"},
        {L"Zirytowana", L"Annoyed"},
        {L"Smutna", L"Sad"},
        {L"Podekscytowana", L"Excited"},
        {L"Zaskoczona", L"Shocked"},
        {L"Zamyślona", L"Thoughtful"},
        {L"Zadziorna", L"Smug"},
        {L"Zawstydzona", L"Flustered"},
        {L"Przestraszona", L"Scared"},
        {L"Senna", L"Sleepy"},
        {L"Rozbawiona", L"Playful"},
        {L"Opanowana", L"Calm"},
        {L"Rozczulona", L"Affectionate"},
        {L"Rozśmieszona", L"Laughing"},
        {L"Wzruszona", L"Tearful"},
        {L"Zdezorientowana", L"Confused"},
        {L"Zdeterminowana", L"Determined"},
        {L"Przyjazna", L"Welcoming"},
        {L"Zrelaksowana", L"Relaxed"},
        {L"Wkręcona w grę", L"Gaming"},
    }};
    const auto idx = static_cast<size_t>(emotion);
    if (idx >= labels.size()) return {};
    return english ? labels[idx].en : labels[idx].pl;
}

std::vector<std::wstring> dailyReadingQueries(Look look, const Memory& memory, std::wstring_view current_game, bool english, uint32_t seed,
                                              std::wstring_view goal) {
    std::vector<std::wstring> queries;
    // Whole words as written (a search engine wants "Kawasaki", not the stem "kawasa"), without the common ones.
    const auto keywords = [](std::wstring_view text, size_t count) {
        std::wstring q, word;
        size_t taken = 0;
        const auto flush = [&] {
            if (word.size() >= 3 && taken < count && !chats::commonWord(game_index::fold(word))) {
                q += (q.empty() ? L"" : L" ") + word;
                ++taken;
            }
            word.clear();
        };
        for (const wchar_t ch : text) {
            if (std::iswalnum(ch)) word += ch;
            else flush();
        }
        flush();
        return q;
    };
    const auto* c = character(look);
    if (c) {
        // Her own life first: the real things she loves, what is on in her city, her topics, her games.
        if (const auto& loved = c->favorites(english); !loved.empty())
            queries.push_back(cleanLine(loved[(seed >> 7) % loved.size()], 60) + (english ? L" news" : L" nowości"));
        if (const auto& home = c->home(english); !home.empty()) {
            auto city = home.substr(0, std::min(home.find(L','), home.find(L" -")));
            if (!city.empty() && city.size() <= 40) queries.push_back(cleanLine(city, 40) + (english ? L" what's on this week" : L" co się dzieje w tym tygodniu"));
        }
        if (const auto& topics = c->topics(english); !topics.empty())
            queries.push_back(cleanLine(topics[(seed >> 13) % topics.size()], 60));
        if (!c->likes.empty())
            queries.push_back(c->likes[seed % c->likes.size()] + (english ? L" games news" : L" gry nowości"));
    }
    if (const auto q = keywords(goal, 4); !q.empty()) queries.push_back(q);
    if (!current_game.empty()) {
        queries.push_back(std::wstring(current_game) + (english ? L" game news trivia" : L" gra ciekawostki aktualności"));
    } else if (const auto own = memory.notesOf(lookKey(look)); !own.empty()) {
        const auto& note = own[(seed >> 3) % own.size()]->text;
        if (const auto q = keywords(note, 3); !q.empty()) queries.push_back(q + (english ? L" gaming news" : L" gry ciekawostki"));
    } else if (!memory.picks.empty()) {
        const auto& pick = memory.picks.back().title;
        if (!pick.empty()) queries.push_back(pick + (english ? L" game trivia" : L" gra ciekawostki"));
    }
    // A different one first each time.
    if (!queries.empty()) std::rotate(queries.begin(), queries.begin() + static_cast<std::ptrdiff_t>((seed >> 3) % queries.size()), queries.end());
    return queries;
}

std::wstring summarizeTavilyResult(const json::Value& response) {
    if (response.get("error")) return {};
    if (const auto* ans = response.get("answer"); ans && ans->type == json::Value::Type::string && !ans->text.empty())
        return cleanLine(text_encoding::wideLenient(ans->text), 260);
    if (const auto* res = response.get("results"); res && res->type == json::Value::Type::array && !res->elements.empty()) {
        const auto& first = res->elements.front();
        const auto title = first.get("title") ? text_encoding::wideLenient(first.get("title")->stringOr()) : std::wstring{};
        const auto content = first.get("content") ? text_encoding::wideLenient(first.get("content")->stringOr()) : std::wstring{};
        if (!title.empty() && !content.empty()) return cleanLine(title + L" — " + content, 260);
        if (!content.empty()) return cleanLine(content, 260);
        if (!title.empty()) return cleanLine(title, 260);
    }
    return {};
}

} // namespace companion::cognition

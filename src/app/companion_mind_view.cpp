// The mind and memory screen of her chat (nodes from her memory, cognition, social model and
// mind, filters, reveal and deletion) and its telemetry.
#include "launcher.hpp"
#include "companion_app_internal.hpp"
#include "../platform/unix_time.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <string_view>

using namespace companion_app;

void Launcher::companionMind(bool open) {
    auto& chat = companion_.chat;
    if (chat.mind == open) return;
    const bool return_to_sidebar = !open && chat.mind_from_sidebar;
    chat.mind = open;
    chat.mind_from_sidebar = false;
    chat.mind_confirm = false;
    chat.hover = companion_ui::no_hit;
    chat.mind_hover = companion_ui::no_hit;
    chat.mind_dragging = false;
    if (return_to_sidebar) {
        chat.mind_motion = 0.0f;
        closeCompanion();
        return;
    }
    if (open) {
        closeKeyboard();
        chat.menu = false;
        chat.viewer = false;
        chat.mind_selected = 0;
        // Her hidden thoughts are masked again, and the sphere builds itself anew (at once without
        // screen transitions).
        chat.mind_revealed.clear();
        chat.mind_live_revealed = false;
        chat.mind_age = settings_.screen_transitions ? 0.0f : companion_ui::Layout::mind_build_seconds;
        chat.mind_yaw = 0.35f;
        chat.mind_pitch = 0.18f;
        chat.mind_yaw_spring.snap(chat.mind_yaw);
        chat.mind_pitch_spring.snap(chat.mind_pitch);
        chat.mind_zoom = 1;
        chat.mind_zoom_spring.snap(1);
        if (!companion_.memory) companionMemory();
        if (!companion_.cognition) companionCognition();
        if (!companion_.affinity) companionAffinity();
        audio_.context();
    } else audio_.navigate();
    if (!settings_.screen_transitions) chat.mind_motion = open ? 1.0f : 0.0f;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindDeleteConfirm(bool open) {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm == open) return;
    if (open) {
        const auto nodes = companionMindNodes();
        const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        if (nodes.empty() || !(nodes[static_cast<size_t>(idx)].deletable || nodes[static_cast<size_t>(idx)].locked)) { navigationBoundary(); return; }
        chat.mind_confirm = true;
        chat.mind_confirm_focus = companion_ui::confirm_cancel; // Default on Anuluj so accidental Enter keeps the memory.
        chat.mind_hover = companion_ui::no_hit;
        chat.mind_dragging = false;
        audio_.context();
    } else {
        chat.mind_confirm = false;
        audio_.navigate();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindDeletePick(bool remove) {
    auto& chat = companion_.chat;
    if (!chat.mind_confirm) return;
    chat.mind_confirm = false;
    if (remove) {
        const auto nodes = companionMindNodes();
        const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
        // Odkryj: her private matter opens for the player (she does not know he looked).
        if (!nodes.empty() && nodes[static_cast<size_t>(idx)].locked) {
            if (companionMindPeek(nodes[static_cast<size_t>(idx)].entry_key))
                showNotification(NotificationKind::launch, tr(L"Umysł i pamięć"), tr(L"Odkryte — ona nie wie, że zajrzałeś"));
            audio_.context();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!nodes.empty() && nodes[static_cast<size_t>(idx)].deletable) {
            const auto& node = nodes[static_cast<size_t>(idx)];
            auto& mem = companionMemory();
            bool removed = false;
            using Kind = CompanionView::MindNode::Kind;
            if (node.kind == Kind::note) removed = mem.removeNote(node.entry_at, node.detail);
            else if (node.kind == Kind::summary) removed = mem.removeSummary(node.entry_at, node.detail);
            else if (node.kind == Kind::pick) removed = mem.removePick(node.entry_key, node.entry_at);
            else if (node.kind == Kind::reflection) {
                removed = companionCognition().removeReflection(node.entry_at, node.detail) ||
                          companionCognition().removeSchema(node.entry_at, node.detail);
                if (removed) saveCompanionCognition();
            } else if (node.kind == Kind::thought) {
                removed = companionCognition().removeThought(node.entry_at, node.detail);
                if (removed) saveCompanionCognition();
            } else if (node.kind == Kind::reading) {
                removed = companionCognition().removeReading(node.entry_at, node.detail);
                if (removed) saveCompanionCognition();
            }
            if (removed) {
                if (node.kind == Kind::note || node.kind == Kind::summary || node.kind == Kind::pick) saveCompanionMemory();
                const auto remaining = companionMindNodes();
                chat.mind_selected = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(remaining.size()) - 1));
                showNotification(NotificationKind::launch, tr(L"Umysł i pamięć"), tr(L"Wspomnienie usunięte"));
            }
        }
        audio_.context();
    } else audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

namespace {
std::pair<uint64_t, size_t> thoughtKey(uint64_t at, std::wstring_view text) { return {at, std::hash<std::wstring_view>{}(text)}; }
}

bool Launcher::companionThoughtShown(uint64_t at, std::wstring_view text) const {
    const auto& shown = companion_.chat.mind_revealed;
    return std::find(shown.begin(), shown.end(), thoughtKey(at, text)) != shown.end();
}

void Launcher::companionMindReveal() {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm) return;
    const auto nodes = companionMindNodes();
    const int idx = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
    if (nodes.empty() || !nodes[static_cast<size_t>(idx)].revealable) { navigationBoundary(); return; }
    const auto& node = nodes[static_cast<size_t>(idx)];
    const auto key = thoughtKey(node.entry_at, node.detail);
    auto& shown = chat.mind_revealed;
    if (const auto found = std::find(shown.begin(), shown.end(), key); found != shown.end()) { shown.erase(found); audio_.navigate(); }
    else {
        shown.push_back(key);
        // Caught: a flustered moment, then what she really felt shows on her face.
        companion_.rig.flash(companion::Emotion::shy, 0.8f, node.emotion.value_or(companion::Emotion::shy));
        audio_.context();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindLiveReveal() {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm) return;
    chat.mind_live_revealed = !chat.mind_live_revealed;
    chat.mind_live_revealed ? audio_.context() : audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<CompanionView::MindNode> Launcher::companionMindNodes() const {
    using Node = CompanionView::MindNode;
    using Kind = Node::Kind;
    std::vector<Node> nodes;
    const auto look = companionLook();
    const auto look_key = std::string(companion::lookKey(look));
    const bool en = english();
    const auto* c = companion::character(look);
    const auto now_sec = unix_time::now();
    const auto ago = [&](uint64_t at) -> std::wstring {
        if (!at || now_sec < at) return en ? L"today" : L"dziś";
        const uint64_t seconds = now_sec - at, d = seconds / 86400;
        if (seconds < 3600) return en ? L"just now" : L"przed chwilą";
        if (seconds < 86400) return std::to_wstring(seconds / 3600) + (en ? L" h ago" : L" h temu");
        if (en) return d == 1 ? L"yesterday" : std::to_wstring(d) + L" days ago";
        return d == 1 ? L"wczoraj" : std::to_wstring(d) + L" dni temu";
    };

    // 0: Core node at the center of the sphere (includes her long-term emotional disposition towards the player).
    {
        Node core;
        core.kind = Kind::core;
        core.label = std::wstring(companion::name(look)) + L" · " + companion::personaName(companionPersonaOf(look));
        core.when = core.category = en ? L"Personality core" : L"Rdzeń osobowości";
        core.detail = c ? (en ? (c->card_en.empty() ? c->card_pl : c->card_en) : c->card_pl) : core.label;
        if (companion_.cognition) {
            if (const auto disp = companion_.cognition->dispositionOf(look_key); !disp.empty())
                core.detail += (en ? L" · Attitude towards you: " : L" · Nastawienie do ciebie: ") + disp;
        }
        if (!companion_.mood.empty()) core.detail += L" · " + trimmed(companion_.mood);
        core.x = 0; core.y = 0; core.z = 0; core.weight = 1.6f; core.parent = 0;
        nodes.push_back(std::move(core));
    }

    const auto placeOnShell = [](size_t index, size_t count, float radius, float phase, float& x, float& y, float& z) {
        const float n = static_cast<float>( std::max<size_t>(1, count));
        const float k = static_cast<float>(index) + 0.5f;
        const float phi = std::acos(1.0f - 2.0f * k / n);
        const float theta = phase + 2.3999632f * static_cast<float>(index);
        x = radius * std::sin(phi) * std::cos(theta);
        y = radius * std::cos(phi);
        z = radius * std::sin(phi) * std::sin(theta);
    };

    const auto shortLabel = [](std::wstring_view text) {
        auto clean = trimmed(text);
        if (clean.size() <= 30) return clean;
        const auto cut = clean.rfind(L' ', 28);
        clean.resize(cut == std::wstring::npos || cut < 12 ? 28 : cut);
        return clean + L"…";
    };

    const std::wstring stm_tier = en ? L"Short-term" : L"Pamięć krótkotrwała";
    const std::wstring ltm_tier = en ? L"Long-term" : L"Pamięć długotrwała";

    // Shell 1 (radius 0.45): Active session, her inner thoughts (<mysl>) and recent online readings (Short-term / Working memory).
    std::vector<Node> context_nodes;
    if (companion_.mind) if (const auto* self = companion_.mind->find(look_key)) {
        int events_shown = 0;
        for (auto it = self->social.events.rbegin(); it != self->social.events.rend() && events_shown < 6; ++it) {
            if (it->kind == "turn" || it->kind == "message") continue;
            Node item; item.kind = Kind::context; item.label = shortLabel(it->text);
            item.category = tr(L"Zdarzenie w relacji"); item.detail = it->text;
            if (it->delta.trust || it->delta.respect || it->delta.security)
                item.detail += L"\n" + std::wstring(tr(L"Zmiana więzi (zaufanie / szacunek / bezpieczeństwo): ")) +
                    std::to_wstring(it->delta.trust) + L" / " + std::to_wstring(it->delta.respect) + L" / " + std::to_wstring(it->delta.security);
            item.when = ago(it->at); item.entry_at = it->at; item.memory_tier = ltm_tier; item.weight = 1.1f; item.parent = 0;
            context_nodes.push_back(std::move(item)); ++events_shown;
        }
        int plans_shown = 0;
        for (auto it = self->social.commitments.rbegin(); it != self->social.commitments.rend() && plans_shown < 4; ++it) {
            if (it->status == companion::social::CommitmentStatus::done || it->status == companion::social::CommitmentStatus::cancelled || it->status == companion::social::CommitmentStatus::declined) continue;
            Node item; item.kind = Kind::context; item.label = shortLabel(it->text); item.detail = it->text;
            item.category = it->status == companion::social::CommitmentStatus::proposed ? tr(L"Propozycja do uzgodnienia") :
                it->status == companion::social::CommitmentStatus::deferred ? tr(L"Przełożony plan") : tr(L"Uzgodniony plan");
            item.when = ago(it->updated); item.entry_at = it->updated; item.memory_tier = stm_tier; item.weight = 1.1f; item.parent = 0;
            context_nodes.push_back(std::move(item)); ++plans_shown;
        }
        int shown = 0;
        for (auto it = self->feelings.rbegin(); it != self->feelings.rend() && shown < 16; ++it, ++shown) {
            const auto& info = companion::feelingInfo(it->kind);
            Node item; item.kind = Kind::feeling; item.feeling = it->kind; item.emotion = info.sprite;
            item.label = item.feeling_label = en ? info.en : info.pl;
            item.category = en ? L"Named feeling" : L"Nazwane uczucie";
            item.detail = item.label;
            if (it->intensity) item.detail += (en ? L" · Model-rated intensity: " : L" · Natężenie według modelu: ") + std::to_wstring(static_cast<int>(*it->intensity * 100)) + L"%";
            else item.detail += en ? L" · Intensity not specified" : L" · Natężenie niepodane";
            if (!it->reason.empty()) item.detail += L" — " + it->reason;
            if (it->inferred) item.detail += en ? L" (recognized from an older text label)" : L" (rozpoznane ze starszego opisu tekstowego)";
            item.memory_tier = now_sec <= it->at + 45 * 60 ? stm_tier : ltm_tier;
            item.when = ago(it->at); item.entry_at = it->at; item.parent = 0; item.weight = 1.3f;
            context_nodes.push_back(std::move(item));
        }
        if (!self->felt.empty() && (self->feelings.empty() || self->felt_at > self->feelings.back().at)) {
            Node item; item.kind = Kind::feeling; item.label = item.detail = self->felt;
            item.category = en ? L"Feeling (text description)" : L"Uczucie (opis tekstowy)";
            item.when = ago(self->felt_at); item.entry_at = self->felt_at; item.memory_tier = stm_tier;
            item.feeling = companion::feelingFromText(self->felt); item.weight = 1.3f;
            context_nodes.push_back(std::move(item));
        }
    }
    if (companion_.last_session && !companion_.last_session->titles.empty()) {
        Node item;
        item.kind = Kind::context;
        item.label = companion_.last_session->titles.front();
        item.when = item.category = en ? L"Last session" : L"Ostatnia sesja";
        item.memory_tier = stm_tier;
        item.detail = (en ? L"Played for " : L"Sesja gry: ") + std::to_wstring(companion_.last_session->minutes) + L" min — " + item.label;
        item.weight = 1.25f;
        item.parent = 0;
        context_nodes.push_back(std::move(item));
    }
    if (companion_.cognition) {
        // Her hidden subconscious thoughts (<mysl>) and what she looked up online (on her own or
        // for the player in a chat) on the inner shell, each with room of its own. A thought stays
        // masked until the player shows it (Pokaż), and again in his next visit.
        int thoughts = 0, searches = 0;
        for (size_t i = companion_.cognition->thoughts.size(); i-- > 0 && thoughts < 5;) {
            const auto& th = companion_.cognition->thoughts[i];
            if (!th.look.empty() && th.look != look_key) continue;
            ++thoughts;
            Node item;
            item.kind = Kind::thought;
            item.label = shortLabel(th.text);
            item.category = en ? L"Hidden thought" : L"Ukryta myśl";
            item.memory_tier = stm_tier;
            item.emotion = th.emotion;
            item.feeling_label = companion::cognition::emotionFeelingLabel(th.emotion, en);
            item.when = ago(th.at);
            item.detail = th.text;
            item.weight = 1.25f;
            item.parent = 0;
            item.deletable = true;
            item.revealable = true;
            item.masked = !companionThoughtShown(th.at, th.text);
            item.entry_at = th.at;
            context_nodes.push_back(std::move(item));
        }
        for (size_t i = companion_.cognition->readings.size(); i-- > 0 && searches < 6;) {
            const auto& rd = companion_.cognition->readings[i];
            if (!rd.look.empty() && rd.look != look_key) continue;
            ++searches;
            Node item;
            item.kind = Kind::reading;
            item.label = rd.topic.empty() ? shortLabel(rd.summary) : shortLabel(rd.topic);
            item.category = rd.asked ? (en ? L"Looked up for you" : L"Sprawdziła dla ciebie") : (en ? L"Searched online" : L"Szukała w sieci");
            item.memory_tier = stm_tier;
            item.when = ago(rd.at);
            item.detail = rd.topic.empty() ? rd.summary : (en ? L"Searched: " : L"Szukała: ") + rd.topic + L" — " + rd.summary;
            item.weight = 1.15f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = rd.at;
            context_nodes.push_back(std::move(item));
        }
    }
    // Her mind (companion_mind.cpp): her mood on the inner shell; her life — goals, promises,
    // milestones, what she expects, what she did and plans (a private one closed) — on its own.
    std::vector<Node> life_nodes;
    for (const auto& mind_item : companionMindItems(look)) {
        Node item;
        item.kind = mind_item.thought ? Kind::thought : Kind::context;
        item.label = shortLabel(mind_item.label);
        item.category = mind_item.category;
        item.memory_tier = mind_item.short_term ? stm_tier : ltm_tier;
        item.when = !mind_item.when.empty() ? mind_item.when : mind_item.at ? ago(mind_item.at) : mind_item.category;
        item.detail = mind_item.detail;
        item.weight = mind_item.locked ? 1.0f : mind_item.active ? 1.4f : 1.2f;
        item.parent = 0;
        item.entry_at = mind_item.at;
        item.entry_key = mind_item.key;
        item.locked = mind_item.locked;
        item.excited = mind_item.active;
        (mind_item.inner ? context_nodes : life_nodes).push_back(std::move(item));
    }
    for (size_t i = 0; i < context_nodes.size(); ++i) {
        placeOnShell(i, context_nodes.size(), 0.45f, 0.4f, context_nodes[i].x, context_nodes[i].y, context_nodes[i].z);
        nodes.push_back(std::move(context_nodes[i]));
    }
    for (size_t i = 0; i < life_nodes.size(); ++i) {
        placeOnShell(i, life_nodes.size(), 0.6f, 0.9f, life_nodes[i].x, life_nodes[i].y, life_nodes[i].z);
        nodes.push_back(std::move(life_nodes[i]));
    }

    // Shell 2 (radius 0.72): Higher-order Reflections ("Sleep" beliefs), Causal Schemas + Player notes (Long-term memory).
    std::vector<Node> note_nodes;
    if (companion_.cognition) {
        for (size_t i = companion_.cognition->schemas.size(); i-- > 0 && note_nodes.size() < 4;) {
            const auto& sc = companion_.cognition->schemas[i];
            if (!sc.look.empty() && sc.look != look_key) continue;
            Node item;
            item.kind = Kind::reflection;
            item.label = shortLabel(sc.text);
            item.category = en ? L"Causal schema" : L"Schemat przyczynowy";
            item.memory_tier = ltm_tier;
            item.when = ago(sc.at);
            item.detail = sc.text;
            item.weight = 1.35f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = sc.at;
            note_nodes.push_back(std::move(item));
        }
        for (size_t i = companion_.cognition->reflections.size(); i-- > 0 && note_nodes.size() < 12;) {
            const auto& ref = companion_.cognition->reflections[i];
            if (!ref.look.empty() && ref.look != look_key) continue;
            Node item;
            item.kind = Kind::reflection;
            item.label = shortLabel(ref.text);
            item.category = en ? L"Her intuition" : L"Wniosek o tobie";
            item.memory_tier = ltm_tier;
            item.when = ago(ref.at);
            item.detail = ref.text + (ref.evolved_from.empty() ? L"" : (en ? L" (earlier: " : L" (wcześniej: ") + ref.evolved_from + L")");
            item.weight = 1.3f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = ref.at;
            note_nodes.push_back(std::move(item));
        }
    }
    if (companion_.memory) {
        const auto& notes = companion_.memory->notes;
        for (size_t i = notes.size(); i-- > 0 && note_nodes.size() < 34;) {
            if (!companion::Memory::knows(notes[i], look_key)) continue; // Another companion's: not hers to show.
            const auto* knower = companion::Memory::knower(notes[i], look_key);
            Node item;
            item.kind = Kind::note;
            item.label = shortLabel(notes[i].text);
            item.category = notes[i].important ? (en ? L"Key memory" : L"Ważne wspomnienie")
                          : knower && knower->hearsay ? (en ? L"Gossip about you" : L"Plotka o tobie")
                                                      : (en ? L"Note about you" : L"Notatka o tobie");
            item.memory_tier = ltm_tier;
            if (const auto em = companion::emotionFromKey(notes[i].emotion)) {
                item.emotion = *em;
                item.feeling_label = companion::cognition::emotionFeelingLabel(*em, en);
            } else if (const auto feeling = companion::feelingFromKey(notes[i].emotion)) {
                item.feeling = *feeling;
                const auto& info = companion::feelingInfo(*feeling);
                item.emotion = info.sprite; item.feeling_label = en ? info.en : info.pl;
            }
            item.when = ago(notes[i].at);
            item.detail = notes[i].text;
            if (!notes[i].feeling.empty() && (notes[i].look.empty() || notes[i].look == look_key))
                item.detail += (en ? L" — How she felt: " : L" — Jak się poczuła: ") + notes[i].feeling;
            item.weight = notes[i].important ? 1.35f : 1.1f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = notes[i].at;
            note_nodes.push_back(std::move(item));
        }
    }
    for (size_t i = 0; i < note_nodes.size(); ++i) {
        placeOnShell(i, note_nodes.size(), 0.72f, 1.3f, note_nodes[i].x, note_nodes[i].y, note_nodes[i].z);
        nodes.push_back(std::move(note_nodes[i]));
    }

    // Shell 3 (radius 0.95): Chat summaries (newest first!), suggested games (picks), and taste vectors.
    std::vector<Node> outer_nodes;
    if (companion_.memory) {
        for (size_t i = companion_.memory->summaries.size(); i-- > 0 && outer_nodes.size() < 18;) {
            const auto& sum = companion_.memory->summaries[i];
            if (!sum.look.empty() && sum.look != look_key) continue;
            Node item;
            item.kind = Kind::summary;
            item.label = shortLabel(sum.text);
            item.category = sum.important ? (en ? L"Key memory" : L"Ważne wspomnienie") : (en ? L"Chat memory" : L"Wspomnienie rozmowy");
            item.memory_tier = ltm_tier;
            item.when = ago(sum.at);
            item.detail = sum.text;
            item.weight = sum.important ? 1.35f : 1.0f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = sum.at;
            outer_nodes.push_back(std::move(item));
        }
        for (size_t i = companion_.memory->picks.size(); i-- > 0 && outer_nodes.size() < 28;) {
            const auto& pick = companion_.memory->picks[i];
            if (!pick.look.empty() && pick.look != look_key) continue;
            Node item;
            item.kind = Kind::pick;
            item.label = pick.title;
            item.category = en ? L"Suggested game" : L"Polecona gra";
            item.memory_tier = now_sec <= pick.at + 3 * 86400 ? stm_tier : ltm_tier;
            item.when = ago(pick.at);
            item.detail = (en ? L"Suggested game: " : L"Polecona gra: ") + pick.title;
            item.weight = 0.95f;
            item.parent = 0;
            item.deletable = true;
            item.entry_at = pick.at;
            item.entry_key = pick.key;
            outer_nodes.push_back(std::move(item));
        }
    }
    if (c) {
        for (size_t i = 0; i < c->likes.size() && i < 5; ++i) {
            Node item;
            item.kind = Kind::taste;
            item.label = L"+ " + c->likes[i];
            item.when = item.category = en ? L"Loved tag" : L"Ulubiony tag";
            item.memory_tier = ltm_tier;
            item.detail = (en ? L"She loves games tagged with: " : L"Uwielbia gry z tagiem: ") + c->likes[i];
            item.weight = 0.9f;
            item.parent = 0;
            outer_nodes.push_back(std::move(item));
        }
        for (size_t i = 0; i < c->dislikes.size() && i < 3; ++i) {
            Node item;
            item.kind = Kind::taste;
            item.label = L"− " + c->dislikes[i];
            item.when = item.category = en ? L"Disliked tag" : L"Nielubiany tag";
            item.memory_tier = ltm_tier;
            item.detail = (en ? L"She avoids games tagged with: " : L"Nie przepada za tagiem: ") + c->dislikes[i];
            item.weight = 0.85f;
            item.parent = 0;
            outer_nodes.push_back(std::move(item));
        }
    }
    for (size_t i = 0; i < outer_nodes.size(); ++i) {
        placeOnShell(i, outer_nodes.size(), 0.95f, 2.2f, outer_nodes[i].x, outer_nodes[i].y, outer_nodes[i].z);
        nodes.push_back(std::move(outer_nodes[i]));
    }

    // Reverse hierarchy for the UI list: newest dated memories at the very top (index 0..),
    // followed by current session/topics, tastes, and the foundational personality core at the end.
    // Every node keeps its 3D (x, y, z) orbital position, and `parent` is remapped to the core's new index.
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        const bool a_dated = a.entry_at > 0;
        const bool b_dated = b.entry_at > 0;
        if (a_dated != b_dated) return a_dated;
        if (a_dated && a.entry_at != b.entry_at) return a.entry_at > b.entry_at;
        const auto rank = [](Kind k) {
            switch (k) {
            case Kind::feeling:    return -1;
            case Kind::thought:    return 0;
            case Kind::reflection: return 1;
            case Kind::reading:    return 2;
            case Kind::note:       return 3;
            case Kind::summary:    return 4;
            case Kind::pick:       return 5;
            case Kind::context:    return 6;
            case Kind::taste:      return 7;
            case Kind::core:       return 8;
            }
            return 9;
        };
        return rank(a.kind) < rank(b.kind);
    });
    int core_index = 0;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == Kind::core) { core_index = static_cast<int>(i); break; }
    int prev_sum = core_index;
    for (size_t i = nodes.size(); i-- > 0;) {
        if (static_cast<int>(i) == core_index) { nodes[i].parent = core_index; continue; }
        if (nodes[i].kind == Kind::summary) {
            nodes[i].parent = prev_sum;
            prev_sum = static_cast<int>(i);
        } else {
            nodes[i].parent = core_index;
        }
    }

    // Highlight nodes excited by Spreading Activation from the player's last message.
    if (companion_.cognition && companion_.memory && !companion_.last_query.empty()) {
        const auto hits = companion_.cognition->spreadActivation(*companion_.memory, look_key, companion_.last_query, now_sec);
        for (auto& node : nodes) {
            for (const auto& h : hits) {
                if (!h.text.empty() && node.detail.find(h.text) != std::wstring::npos) {
                    node.excited = true;
                    break;
                }
            }
        }
    }
    using Filter = companion_ui::MindFilter;
    for (auto& node : nodes) {
        switch (companion_.chat.mind_filter) {
        case Filter::all: node.matches_filter = true; break;
        case Filter::feelings: node.matches_filter = node.kind == Kind::feeling || node.feeling.has_value() || !node.feeling_label.empty(); break;
        case Filter::memories: node.matches_filter = node.kind == Kind::note || node.kind == Kind::summary; break;
        case Filter::thoughts: node.matches_filter = node.kind == Kind::thought; break;
        case Filter::reflections: node.matches_filter = node.kind == Kind::reflection; break;
        case Filter::life: node.matches_filter = node.kind == Kind::context || node.kind == Kind::reading || node.kind == Kind::core; break;
        case Filter::games: node.matches_filter = node.kind == Kind::pick || node.kind == Kind::taste; break;
        case Filter::active: node.matches_filter = node.excited; break;
        }
    }
    return nodes;
}

void Launcher::companionMindFilter(companion_ui::MindFilter filter) {
    auto& chat = companion_.chat;
    if (!chat.mind || chat.mind_confirm) return;
    chat.mind_filter = filter;
    const auto nodes = companionMindNodes();
    const auto first = std::find_if(nodes.begin(), nodes.end(), [](const auto& n) { return n.matches_filter; });
    if (first != nodes.end()) chat.mind_selected = static_cast<int>(first - nodes.begin());
    chat.mind_hover = companion_ui::no_hit;
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionMindNextFilter() {
    companionMindFilter(static_cast<companion_ui::MindFilter>((static_cast<int>(companion_.chat.mind_filter) + 1) % companion_ui::mind_filter_count));
}

CompanionView::MindTelemetry Launcher::companionMindTelemetry() const {
    CompanionView::MindTelemetry tel;
    const auto look = companionLook();
    const auto look_key = std::string(companion::lookKey(look));
    const bool en = english();
    if (const auto* c = companion::character(look)) {
        const auto card = en ? (c->card_en.empty() ? c->card_pl : c->card_en) : c->card_pl;
        const auto about = c->about(en, false);
        tel.bio = about.empty() ? card : card + L" — " + about;
        tel.likes = c->likes;
        tel.dislikes = c->dislikes;
    }
    const int64_t pts = companion_.affinity ? companion::affinity::pointsOf(companion_.affinity->points, look_key) : 0;
    const int lvl = companion::affinity::level(pts);
    tel.points = pts;
    tel.next_points = lvl >= companion::affinity::max_level ? pts : companion::affinity::threshold(lvl + 1);
    tel.costumes_unlocked = 0;
    tel.costumes_total = 0;
    for (const auto& costume : companionCostumes()) {
        if (costume.look != look) continue;
        ++tel.costumes_total;
        if (companionCostumeUnlocked(costume)) ++tel.costumes_unlocked;
    }
    if (tel.costumes_total == 0) { tel.costumes_total = 1; tel.costumes_unlocked = 1; }
    if (companion_.memory) {
        tel.notes_count = static_cast<int>(companion_.memory->notes.size());
        for (const auto& s : companion_.memory->summaries) if (s.look.empty() || s.look == look_key) ++tel.summaries_count;
        for (const auto& p : companion_.memory->picks) if (p.look.empty() || p.look == look_key) ++tel.picks_count;
    }
    if (!companion_.diagnostics.empty()) {
        uint64_t sum = 0;
        for (const auto& d : companion_.diagnostics) sum += d.total_ms;
        tel.avg_latency_ms = static_cast<uint32_t>(sum / companion_.diagnostics.size());
        tel.model_name = companion_.diagnostics.back().model;
    }
    if (tel.model_name.empty()) {
        companion::Connection shown;
        shown.provider = companionAccess().provider;
        shown.model = companionAccess().model;
        tel.model_name = shown.label();
    }
    // Her mood now (her settled affect), beside her profile.
    if (companion_.mind)
        if (const auto* self = companion_.mind->find(look_key)) tel.mood = companion::mind::describe(companion::mind::settled(self->affect, companionMindContext(look)), en);
    // Live thought indicator: only shown while she is genuinely thinking in flight (`companion_.busy`).
    // Her hidden thought there is masked too until the player clicks the bar.
    tel.live_busy = companion_.busy;
    if (companion_.busy) {
        tel.live_thought_kind = en ? L"Thinking now…" : L"Teraz myśli…";
        if (!companion_.heard.empty()) {
            const auto parsed = companion::parseText(companion_.heard);
            tel.live_hidden = !parsed.subconscious.empty();
            tel.live_thought = tel.live_hidden ? parsed.subconscious : parsed.text;
        }
        // Her curiosity for this reply, decided as he wrote: she is about to ask back or carry the talk on.
        if (tel.live_thought.empty() && companion_.mind)
            if (const auto* self = companion_.mind->find(look_key); self && self->urge.move != companion::CuriousMove::none)
                tel.live_thought = self->urge.move == companion::CuriousMove::clarify ? (en ? L"Wants to ask what you meant…" : L"Chce dopytać, co ‹miałeś|miałaś› na myśli…")
                                                                                     : (en ? L"Wants to carry the talk on…" : L"Chce sama pociągnąć rozmowę…");
        if (tel.live_thought.empty())
            tel.live_thought = en ? L"Formulating a reply…" : L"Formułuje odpowiedź…";
        tel.live_masked = tel.live_hidden && !companion_.chat.mind_live_revealed;
    }
    return tel;
}

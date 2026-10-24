// Gamification (Settings → "Grywalizacja"): each companion's sympathy for the person she talks
// with, as a level 1–50 and a small bar towards the next one (companion_affinity.hpp). Messages, the
// day's first one (more for a streak of days in a row), a session of a game she suggested or a
// first go at a never-played one raise the host's. A new stage or costume: a toast, a glowing badge
// and her remark (in the chat a hidden note for her next reply, else her home bubble); it may unlock
// costumes and photo folders. Below level 30, alone in the chat, her [granica] when he goes much too
// far (flirting is fine) gives a yellow card (nothing lost), then a red one (a small penalty, never
// a level); a toast and a card on her badge, and her prompt keeps her cooler while it is live. The
// chat's "…" menu resets it behind a dialog with a red button, and changes her costume
// (companion_costumes.hpp). Points and cards live in companion_affinity.txt, written on a worker in
// order. Who she talks with (name, gender) comes from Settings and what she learnt (companionPerson).
#include "launcher.hpp"
#include "../net/companion_person.hpp"
#include "../platform/atomic_file.hpp"

#include <fstream>
#include <sstream>

namespace {
bool english() { return i18n::language() == i18n::Language::en; }
int dateKey(const SYSTEMTIME& time) { return time.wYear * 10000 + time.wMonth * 100 + time.wDay; }
// Every character's costumes (companion_characters.hpp, from res/companion/), in one list; made
// again when a test replaces the characters.
const std::vector<companion::costumes::Costume>& builtInCostumes() {
    static std::vector<companion::costumes::Costume> costumes;
    static uint64_t revision = ~0ull;
    if (revision != companion::charactersRevision()) {
        revision = companion::charactersRevision();
        costumes.clear();
        for (const auto& character : companion::characters()) costumes.insert(costumes.end(), character.costumes.begin(), character.costumes.end());
    }
    return costumes;
}
}

companion::affinity::State& Launcher::companionAffinity() {
    if (!companion_.affinity) {
        std::ifstream input(userConfigDirectory() / L"companion_affinity.txt", std::ios::binary);
        std::ostringstream text;
        if (input) text << input.rdbuf();
        companion_.affinity = companion::affinity::parse(text.str());
    }
    return *companion_.affinity;
}

int Launcher::companionLevel(companion::Look look) const {
    return companion_.affinity ? companion::affinity::level(companion::affinity::pointsOf(companion_.affinity->points, companion::lookKey(look))) : 1;
}

companion::affinity::Stage Launcher::companionStage(companion::Look look) const {
    if (!settings_.companion_gamification || !companion_.affinity) return companion::affinity::unlevelled_stage;
    const auto* ch = companion::character(look);
    const auto rel = companion_.affinity->relationOf(companion::lookKey(look));
    return companion::affinity::effectiveStage(companionLevel(look), rel, ch ? ch->style : companion::RelationStyle::balanced);
}

companion::affinity::Jealousy Launcher::companionJealousy(companion::Look look) const {
    const auto* ch = companion::character(look);
    // An undecided one changes her mind by the day (her local date).
    const SYSTEMTIME local = companion_.clock();
    uint32_t day = static_cast<uint32_t>(local.wYear * 372 + local.wMonth * 31 + local.wDay);
    for (const char c : companion::lookKey(look)) day += static_cast<unsigned char>(c);
    return companion::affinity::jealousy(companionStage(look), ch ? ch->exclusivity : companion::Exclusivity::monogamous,
                                         settings_.companion_gamification ? companionLevel(look) : 20, day);
}

companion::Person Launcher::companionPerson() const {
    companion::Person person;
    person.name = settings_.companion_user_name;
    using UserGender = AppSettings::CompanionUserGender;
    if (settings_.companion_user_gender == UserGender::male) person.gender = companion::Gender::male;
    else if (settings_.companion_user_gender == UserGender::female) person.gender = companion::Gender::female;
    else if (companion_.affinity) person.gender = companion_.affinity->player_gender; // What she learnt from the chat.
    return person;
}

void Launcher::companionSyncPerson() const { companion::setPerson(companionPerson()); }

void Launcher::saveCompanionAffinity() {
    if (!companion_.affinity) return;
    // An older save never lands after a newer one.
    const auto generation = ++companion_.affinity_io->generation;
    work_.start([text = companion::affinity::serialize(*companion_.affinity), file = userConfigDirectory() / L"companion_affinity.txt",
                 io = companion_.affinity_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) atomic_file::write(file, text);
    });
}

void Launcher::companionAffinityGain(int64_t points, const wchar_t* reason) {
    companionAffinityGainFor(companionLook(), points, reason);
}

void Launcher::companionAffinityGainFor(companion::Look look, int64_t points, const wchar_t* reason) {
    if (!settings_.companion || !settings_.companion_gamification || points <= 0) return;
    auto& value = companionAffinity().points[std::string(companion::lookKey(look))];
    const int before = companion::affinity::level(value);
    const auto old = value;
    value = std::min(value + points, companion::affinity::max_points);
    if (value == old) return;
    saveCompanionAffinity();
    if (reason) showNotification(NotificationKind::launch, companion::name(look) + std::wstring(L" · +") + std::to_wstring(value - old) + L" " + tr(L"pkt sympatii"), tr(reason));
    if (const int after = companion::affinity::level(value); after > before) companionLevelUp(look, before, after);
}

void Launcher::companionLevelUp(companion::Look look, int from, int level) {
    companion_.affinity_pulse = 1;
    // Costumes unlocked on the way (a big gain may pass more than one level).
    std::vector<std::wstring> unlocked;
    for (const auto& costume : companionCostumes())
        if (costume.look == look && costume.slot != 0 && costume.level > from && costume.level <= level) unlocked.push_back(companionCostumeName(costume));
    std::wstring detail = tr(L"Wasza sympatia rośnie");
    if (!unlocked.empty()) {
        detail = tr(L"Nowy strój: ");
        for (size_t i = 0; i < unlocked.size(); ++i) detail += (i ? L", " : L"") + unlocked[i];
    }
    showNotification(NotificationKind::launch, companion::name(look) + std::wstring(L" · Lv ") + std::to_wstring(level), detail);
    // A new stage of theirs (her style's own thresholds: slow-burn later, casual sooner).
    const auto* ch = companion::character(look);
    const auto rel = companionAffinity().relationOf(std::string(companion::lookKey(look)));
    const auto style = ch ? ch->style : companion::RelationStyle::balanced;
    const bool stage_crossed = companion::affinity::effectiveStage(from, rel, style) != companion::affinity::effectiveStage(level, rel, style);
    if (stage_crossed && settings_.companion_memory && companion_.persist) {
        const auto now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        companionCognition().postGossip(companion::lookKey(look), "level", std::to_wstring(level), now_sec);
        saveCompanionCognition();
    }
    const bool en = english();
    if (companionOpen()) {
        // Subtle and natural: only when a whole stage threshold is crossed or an outfit unlocks,
        // and never mentioning numbers or "levels" out loud.
        if (stage_crossed || !unlocked.empty()) {
            std::wstring note = en ? L"(Subtle inner shift: you feel a little closer and more at ease with ‹him|her|them› now — let a touch more warmth show in your tone, as far as your stage goes, but NEVER mention levels, points or 'leveling up' out loud.)"
                                   : L"(Subtelna zmiana w tobie: czujesz, że znasz ‹go|ją› trochę lepiej i jest ci przy ‹nim|niej› swobodniej — niech w twoim tonie pojawi się odrobina więcej ciepła, na miarę waszego etapu, ale NIGDY nie wspominaj na głos o żadnych poziomach, punktach ani awansie relacji.)";
            if (!unlocked.empty())
                note += en ? L" (You also feel like wearing your outfit soon: " + unlocked.front() + L" — you may mention it casually as clothes from your wardrobe, never as an 'unlock'.)"
                           : L" (Masz też ochotę założyć niedługo swój strój: " + unlocked.front() + L" — możesz o nim lekko napomknąć jak o ciuchu z szafy, nigdy o „odblokowaniu”.)";
            companion_.turns.push_back({true, std::move(note)});
        }
    } else if (stage_crossed || !unlocked.empty()) {
        companion::Event event;
        event.kind = companion::EventKind::level_up;
        event.count = static_cast<uint32_t>(level);
        event.titles = std::move(unlocked);
        companionNotice(std::move(event));
    }
    audio_.context();
}

void Launcher::companionBoundary() {
    if (!settings_.companion || !settings_.companion_gamification) return;
    const auto look = companionLook();
    const auto booked = companionAffinity().book(std::string(companion::lookKey(look)), dateKey(companion_.clock()));
    if (booked.card == companion::affinity::Card::none) return; // From level 50 on she no longer minds.
    saveCompanionAffinity();
    companion_.affinity_card_pulse = 1;
    if (settings_.companion_memory && companion_.persist) {
        const auto now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        companionCognition().postGossip(companion::lookKey(look), "card", booked.card == companion::affinity::Card::red ? L"red" : L"yellow", now_sec);
        saveCompanionCognition();
    }
    const bool red = booked.card == companion::affinity::Card::red;
    // A yellow card costs nothing; a red one a little, never a level.
    std::wstring title = companion::name(look) + std::wstring(L" · ") + (red ? tr(L"Czerwona kartka") : tr(L"Żółta kartka"));
    if (booked.lost) title += L" · \u2212" + std::to_wstring(booked.lost) + L" " + tr(L"pkt sympatii");
    showNotification(NotificationKind::launch, std::move(title),
                     red ? tr(L"Znowu przesadzasz — jest wobec ciebie chłodniejsza") : tr(L"Za mocno się spoufalasz — następnym razem kosztuje sympatię"));
    audio_.context();
}

void Launcher::companionAffinityReset() {
    const std::string key(companion::lookKey(companionLook()));
    auto& state = companionAffinity();
    state.points[key] = 0;
    state.relations.erase(key);
    state.cards.erase(key);
    saveCompanionAffinity();
    showNotification(NotificationKind::launch, tr(L"Towarzyszka AI"), std::wstring(tr(L"Sympatia zresetowana: ")) + companion::name(companionLook()));
}

void Launcher::companionRelationshipShift(const companion::Reply& reply) {
    if (!settings_.companion || !settings_.companion_gamification) return;
    const bool life_shift = reply.health_change || reply.work_change || reply.wealth_change || reply.family_change || reply.social_change;
    if (!reply.stance_change && !reply.role_change && !reply.player_distance_change && !life_shift) return;
    const auto look = companionLook();
    const std::string key(companion::lookKey(look));
    const auto* ch = companion::character(look);
    auto& state = companionAffinity();
    bool changed = false;

    if (life_shift) {
        auto& rel = state.relation(key);
        const auto before = rel;
        if (reply.health_change) rel.health = *reply.health_change;
        if (reply.work_change) rel.work = *reply.work_change;
        if (reply.wealth_change) rel.wealth = *reply.wealth_change;
        if (reply.family_change) rel.family = *reply.family_change;
        if (reply.social_change) rel.social = *reply.social_change;
        if (!(rel == before)) {
            state.pruneRelation(key);
            changed = true;
        }
    }

    if (reply.role_change) {
        auto& rel = state.relation(key);
        if (rel.role != *reply.role_change) {
            rel.role = *reply.role_change;
            state.pruneRelation(key);
            changed = true;
            const wchar_t* role_label = *reply.role_change == companion::RelationRole::coworker     ? tr(L"Relacja służbowa")
                                        : *reply.role_change == companion::RelationRole::friend_role ? tr(L"Tylko przyjaźń")
                                                                                                     : tr(L"Relacja romantyczna");
            showNotification(NotificationKind::launch, companion::name(look) + std::wstring(L" · ") + role_label,
                             tr(L"Zmieniła się rama waszej relacji"));
        }
    }
    if (reply.player_distance_change) {
        auto& rel = state.relation(key);
        const int cap = *reply.player_distance_change ? companion::affinity::distanceCap(companionLevel(look)) : 0;
        if (rel.player_cap != cap) {
            rel.player_cap = cap;
            state.pruneRelation(key);
            changed = true;
            showNotification(NotificationKind::launch, companion::name(look) + std::wstring(L" · ") + (cap > 0 ? tr(L"Trzymasz dystans") : tr(L"Bez dystansu")),
                             cap > 0 ? tr(L"Rozwój relacji zatrzymany na obecnym etapie") : tr(L"Relacja znów może się rozwijać"));
        }
    }
    if (reply.stance_change && state.applyStance(key, *reply.stance_change, ch ? ch->default_role : companion::RelationRole::auto_role)) {
        changed = true;
        companion_.affinity_pulse = 1;
        const wchar_t* head = tr(L"Dylemat w relacji");
        const wchar_t* sub = tr(L"Waha się między dotychczasową granicą a uczuciami");
        if (*reply.stance_change == companion::RelationStance::distanced) {
            head = tr(L"Złapała dystans");
            sub = tr(L"Wycofała się o krok — potrzebuje czasu i spokoju");
        } else if (*reply.stance_change == companion::RelationStance::broken) {
            head = tr(L"Przełamanie granicy!");
            sub = tr(L"Dotychczasowa bariera między wami pękła");
        } else if (*reply.stance_change == companion::RelationStance::open) {
            head = tr(L"Znów się otwiera");
            sub = tr(L"Dystans minął — znów czuje się przy tobie swobodnie");
        }
        showNotification(NotificationKind::launch, companion::name(look) + std::wstring(L" · ") + head, sub);
        if (settings_.companion_memory && companion_.persist) {
            const auto now_sec = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            companionCognition().postGossip(key, "stance", text_encoding::wideLenient(companion::relationStanceKey(*reply.stance_change)), now_sec);
            saveCompanionCognition();
        }
    }
    if (changed) {
        saveCompanionAffinity();
        audio_.context();
    }
}

void Launcher::companionRelationMenu() {
    using Kind = companion_ui::MenuItem::Kind;
    auto& chat = companion_.chat;
    const auto look = companionLook();
    const std::string key(companion::lookKey(look));
    const auto& state = companionAffinity();
    const auto rel = state.relationOf(key);
    const auto* ch = companion::character(look);
    const auto def_role = ch ? ch->default_role : companion::RelationRole::auto_role;
    const auto active = companion::affinity::effectiveRole(rel.role, def_role, ch ? ch->attraction : companion::Attraction::any, state.player_gender, rel.stance);

    chat.menu_items.clear();
    const auto check = [](bool on) -> std::wstring { return on ? L"\u2713" : L""; };
    chat.menu_items.push_back({Kind::relation_pick, tr(L"Tor: Swobodny (domyślny)"), true, 0, check(rel.role == companion::RelationRole::auto_role)});
    chat.menu_items.push_back({Kind::relation_pick, tr(L"Tor: Tylko przyjaźń"), true, 1, check(rel.role == companion::RelationRole::friend_role || (rel.role == companion::RelationRole::auto_role && active == companion::RelationRole::friend_role))});
    chat.menu_items.push_back({Kind::relation_pick, tr(L"Tor: Służbowy (współpracownik)"), true, 2, check(rel.role == companion::RelationRole::coworker || (rel.role == companion::RelationRole::auto_role && active == companion::RelationRole::coworker))});
    chat.menu_items.push_back({Kind::relation_pick, tr(L"Tor: Romantyczny"), true, 3, check(rel.role == companion::RelationRole::romantic || rel.role == companion::RelationRole::secret_romance)});

    std::wstring dist_detail = rel.player_cap > 0 ? L"\u2713" : L"";
    if (rel.stance == companion::RelationStance::dilemma) dist_detail = tr(L"dylemat");
    else if (rel.stance == companion::RelationStance::distanced) dist_detail = tr(L"jej dystans");
    else if (rel.stance == companion::RelationStance::broken) dist_detail = tr(L"przełamana");
    chat.menu_items.push_back({Kind::relation_pick, rel.player_cap > 0 ? tr(L"Zdejmij mój dystans") : tr(L"Trzymaj dystans (zatrzymaj etap)"), true, 10, std::move(dist_detail)});

    // Your gender (the same as Settings → Asystent → "Twoja płeć"): what she says, "on" or "ona".
    const auto gender = companionPerson().gender;
    const wchar_t* gender_label = gender == companion::Gender::male     ? tr(L"Mężczyzna")
                                  : gender == companion::Gender::female ? tr(L"Kobieta")
                                                                        : tr(L"Nieokreślona");
    chat.menu_items.push_back({Kind::relation_pick, tr(L"Twoja płeć"), true, 20, gender_label});
    chat.menu_focus = 0;
    chat.menu = true;
    chat.focus = companion_ui::focus_more;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionRelationPick(int code) {
    const auto look = companionLook();
    const std::string key(companion::lookKey(look));
    auto& state = companionAffinity();
    auto& rel = state.relation(key);
    if (code >= 0 && code <= 3) {
        rel.role = code == 1 ? companion::RelationRole::friend_role
                 : code == 2 ? companion::RelationRole::coworker
                 : code == 3 ? companion::RelationRole::romantic
                             : companion::RelationRole::auto_role;
        if (rel.role != companion::RelationRole::romantic && rel.stance == companion::RelationStance::broken)
            rel.stance = companion::RelationStance::open;
    } else if (code == 10) {
        if (rel.player_cap > 0) rel.player_cap = 0;
        else rel.player_cap = companion::affinity::distanceCap(companionLevel(look));
    } else if (code == 20) { // Your gender, as Settings keep it: automatic → male → female → automatic.
        using UserGender = AppSettings::CompanionUserGender;
        auto& chosen = settings_.companion_user_gender;
        chosen = chosen == UserGender::automatic ? UserGender::male : chosen == UserGender::male ? UserGender::female : UserGender::automatic;
        settingsChanged();
        companionSyncPerson();
    }
    state.pruneRelation(key);
    saveCompanionAffinity();
    audio_.context();
}

void Launcher::companionResetConfirm(bool open) {
    auto& chat = companion_.chat;
    if (chat.confirm == open) return;
    chat.confirm = open;
    chat.menu = false;
    chat.hover = companion_ui::no_hit;
    chat.focus = companion_ui::focus_more;
    if (open) {
        closeKeyboard();
        chat.confirm_focus = companion_ui::confirm_cancel; // Enter by mistake keeps her sympathy.
        audio_.context();
    } else audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionResetPick(bool reset) {
    companionResetConfirm(false);
    if (reset) { companionAffinityReset(); audio_.context(); }
}

void Launcher::companionAffinitySuggested(const std::vector<std::wstring>& titles) {
    if (!settings_.companion || !settings_.companion_gamification || titles.empty()) return;
    auto& state = companionAffinity();
    const int today = dateKey(companion_.clock());
    bool changed = false;
    for (const auto& title : titles)
        if (auto key = companionGameKey(title); !key.empty()) { state.suggested(std::move(key), today); changed = true; }
    if (changed) saveCompanionAffinity();
}

void Launcher::companionAffinityPlayed(const std::string& key, uint64_t played_ms, const companion::Progress& before) {
    if (!settings_.companion || !settings_.companion_gamification || key.empty()) return;
    if (played_ms < static_cast<uint64_t>(companion::affinity::game_minutes) * 60'000) return;
    auto& state = companionAffinity();
    if (state.takeSuggestion(key, dateKey(companion_.clock()))) {
        saveCompanionAffinity();
        companionAffinityGain(companion::affinity::suggested_game, trKey(L"Grasz w grę, którą ci poleciła"));
    } else if (before.minutes && *before.minutes == 0)
        companionAffinityGain(companion::affinity::backlog_game, trKey(L"Pierwsze podejście do gry z kupki wstydu"));
}

companion::PromptExtras::Bond Launcher::companionBond() const {
    companion::PromptExtras::Bond bond;
    if (!settings_.companion_gamification || !companion_.affinity) return bond;
    const auto look = companionLook();
    const auto key = companion::lookKey(look);
    bond.level = companionLevel(look);
    const int today = dateKey(companion_.clock());
    bond.streak = companion_.affinity->currentStreak(today);
    bond.card = static_cast<int>(companion_.affinity->card(key, today));
    const auto rel = companion_.affinity->relationOf(key);
    bond.role = rel.role;
    bond.stance = rel.stance;
    bond.player_cap = rel.player_cap;
    bond.tension = rel.tension;
    bond.player_gender = companionPerson().gender;
    bond.health = rel.health;
    bond.work = rel.work;
    bond.wealth = rel.wealth;
    bond.family = rel.family;
    bond.social = rel.social;
    bond.trust = rel.trust;
    bond.respect = rel.respect;
    bond.security = rel.security;
    const int64_t pts = companion::affinity::pointsOf(companion_.affinity->points, key);
    const companion::mind::Self empty_self;
    const auto* self = companion_.mind ? companion_.mind->find(key) : nullptr;
    const companion::Memory empty_mem;
    const auto& mem = companion_.memory ? *companion_.memory : empty_mem;
    bond.first_meet = companion::mind::isFirstMeeting(self ? *self : empty_self, mem, key, pts);
    if (bond.first_meet) {
        for (const auto other : companion::availableLooks()) {
            if (other == look) continue;
            if (companion::affinity::pointsOf(companion_.affinity->points, companion::lookKey(other)) > 0) {
                bond.known_companion = companion::name(other);
                break;
            }
        }
    }
    if (const auto& guest = companion_.guest.look; guest && *guest != look) {
        bond.guest_level = companionLevel(*guest);
        bond.guest_role = companion_.affinity->relationOf(companion::lookKey(*guest)).role;
    }
    // His "pairs" with the others (flirty friends and closer): she knows of them from the girls.
    for (const auto other : companion::availableLooks())
        if (other != look) bond.others.push_back({other, companionLevel(other)});
    return bond;
}

// Her costumes ------------------------------------------------------------------------------------

const std::vector<companion::costumes::Costume>& Launcher::companionCostumes() const {
    return companion_.costumes ? *companion_.costumes : builtInCostumes();
}

bool Launcher::companionCostumeUnlocked(const companion::costumes::Costume& costume) const {
    return costume.slot == 0 || !settings_.companion_gamification || costume.level <= companionLevel(costume.look);
}

int Launcher::companionCostume(companion::Look look) const {
    const auto found = settings_.companion_costumes.find(companion::lookKey(look));
    if (found == settings_.companion_costumes.end() || found->second.empty()) return 0;
    const auto* costume = companion::costumes::find(companionCostumes(), look, found->second);
    return costume && companionCostumeUnlocked(*costume) ? costume->slot : 0;
}

std::wstring Launcher::companionCostumeName(const companion::costumes::Costume& costume) const {
    if (costume.slot == 0) return tr(L"Podstawowy");
    auto name = companion::costumes::name(costume, english());
    return name.empty() ? std::wstring(tr(L"Strój ")) + std::to_wstring(costume.level) : name;
}

void Launcher::companionCostumeMenu() {
    using Kind = companion_ui::MenuItem::Kind;
    auto& chat = companion_.chat;
    const auto look = companionLook();
    const int worn = companionCostume(look);
    chat.menu_items.clear();
    for (const auto& costume : companionCostumes()) {
        if (costume.look != look) continue;
        const bool open = companionCostumeUnlocked(costume);
        companion_ui::MenuItem item{Kind::wear, companionCostumeName(costume), open, costume.slot};
        item.detail = !open ? std::wstring(tr(L"od poziomu ")) + std::to_wstring(costume.level) : costume.slot == worn ? L"\u2713" : L"";
        chat.menu_items.push_back(std::move(item));
    }
    if (chat.menu_items.empty()) chat.menu_items.push_back({Kind::wear, tr(L"Podstawowy"), true, 0, L"\u2713"});
    const auto current = std::find_if(chat.menu_items.begin(), chat.menu_items.end(), [&](const auto& item) { return item.value == worn; });
    chat.menu_focus = current == chat.menu_items.end() ? 0 : static_cast<int>(current - chat.menu_items.begin());
    chat.menu = true;
    chat.focus = companion_ui::focus_more;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionWear(int slot) {
    const auto look = companionLook();
    std::string folder;
    for (const auto& costume : companionCostumes())
        if (costume.look == look && costume.slot == slot && companionCostumeUnlocked(costume)) folder = slot == 0 ? std::string{} : costume.folder;
    auto& chosen = settings_.companion_costumes[std::string(companion::lookKey(look))];
    if (chosen == folder && companionCostume(look) == slot) return;
    chosen = folder;
    if (folder.empty()) settings_.companion_costumes.erase(std::string(companion::lookKey(look)));
    settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now(); // Kept in settings.ini.
    companion_.rig.emote(companion::Emotion::playful); // A twirl in the new outfit.
    if (companionOpen()) companion_.chat.pose_age = 0; // And her full-figure pose in it (when it has one).
    audio_.context();
}

// Her mind in the app (src/net/companion_mind.hpp): the state that lives between and within her
// conversations — affect, drives, goals, expectations, what she said about herself — loaded on
// first use from companion_mind.dat (DPAPI) and saved on a worker in order. The moments that move
// it: the player's message (her quick read shows on her face before the model answers), her reply
// (her inner appraisal: mood, what she learned, what she expects, promises and goals), a
// conversation's start (the clock moves on; another companion may let something slip) and its end,
// a finished game session (expectations settled by telemetry). Her prompt gets the most salient
// few of everything she has on her mind (companionMindCandidates → mind::workspace). Her life
// goes on in the background, never before her answer: her web reading (up to six times a day,
// while she is up, pollCompanionLife), her diary's plans and, for a long gap, her diary filling
// in what she did (companionLifeFill); her relationship and her life's state
// (companion_affinity) move where her mood rests and her energy.
#include "launcher.hpp"
#include "../library/game_index.hpp"
#include "../net/web_search.hpp"

#include <algorithm>
#include <chrono>
#include <cwctype>

namespace {
bool english() { return i18n::language() == i18n::Language::en; }
int dateKey(const SYSTEMTIME& time) { return time.wYear * 10000 + time.wMonth * 100 + time.wDay; }
uint64_t unixNow() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
std::wstring nameOf(std::string_view key) {
    const auto look = companion::lookFromKey(key);
    return look ? std::wstring(companion::name(*look)) : text_encoding::wideLenient(key);
}
} // namespace

companion::mind::State& Launcher::companionMindState() {
    if (!companion_.mind) companion_.mind = companion_.persist ? companion::mind::loadState(userConfigDirectory()) : companion::mind::State{};
    return *companion_.mind;
}

void Launcher::saveCompanionMind() {
    companionSemantic(false);
    if (!companion_.mind || !companion_.persist || !settings_.companion_memory) return;
    // Writes run in order of their generation: an older save never lands after a newer one or a clearing.
    const auto generation = ++companion_.mind_io->generation;
    work_.start([state = *companion_.mind, config = userConfigDirectory(), io = companion_.mind_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) companion::mind::saveState(config, state);
    });
}

companion::mind::Context Launcher::companionMindContext(companion::Look look) const {
    companion::mind::Context context;
    context.look = std::string(companion::lookKey(look));
    context.persona = companionPersonaOf(look);
    context.character = companion::character(look);
    context.now = unixNow();
    context.english = english();
    // Her life now and her relationship with the player (companion_affinity; character.txt defaults).
    const auto* c = context.character;
    if (c) {
        context.life.work = c->default_work;
        context.life.wealth = c->default_wealth;
    }
    companion::affinity::Relation rel;
    if (companion_.affinity) rel = companion_.affinity->relationOf(context.look);
    const auto gender = companionPerson().gender;
    context.life.health = rel.health;
    if (rel.work != companion::LifeWork::steady) context.life.work = rel.work;
    if (rel.wealth != companion::LifeWealth::comfortable) context.life.wealth = rel.wealth;
    context.life.family = rel.family;
    context.life.social = rel.social;
    context.stance = rel.stance;
    context.tension = rel.tension;
    context.trust = rel.trust;
    context.respect = rel.respect;
    context.security = rel.security;
    context.level = companionLevel(look);
    context.stage = static_cast<int>(companionStage(look));
    context.player_gender = gender;
    // How much she knows about him (Loewenstein's information gap): her notes of him and his world.
    {
        size_t known = companion_.memory ? companion_.memory->notesOf(context.look).size() : 0;
        if (companion_.mind)
            if (const auto* self = companion_.mind->find(context.look)) known += self->player_world.size();
        context.known = std::min(1.0f, static_cast<float>(known) / 30.0f);
    }
    context.role = companion::affinity::effectiveRole(rel.role, c ? c->default_role : companion::RelationRole::auto_role,
                                                      c ? c->attraction : companion::Attraction::any, gender, rel.stance);
    return context;
}

std::optional<companion::mind::Night> Launcher::companionNight(companion::Look look) const {
    companion::mind::Context context; // Her clock is all a night needs.
    context.look = std::string(companion::lookKey(look));
    context.character = companion::character(look);
    context.now = unixNow();
    return companion::mind::nightAt(context);
}

bool Launcher::companionAsleep(companion::Look look) const {
    const auto night = companionNight(look);
    const auto now = unixNow();
    return night && !night->sleepless && now >= night->from && now < night->to;
}

bool Launcher::companionBedtime() const {
    const auto night = companionNight(companionLook());
    const auto now = unixNow();
    return night && !night->sleepless && now < night->to && now + 3600 >= night->from;
}

Launcher::CompanionAway Launcher::companionAway() const {
    CompanionAway away;
    // Her rest after an ended conversation (only today's: a new day, a new conversation).
    const auto local = companionNow();
    if (companion_.chat.ended && local < companion_.rest_until && companion::chats::dayOf(local) == companion::chats::dayOf(companion_.chat_started))
        away.seconds = companion_.rest_until - local;
    // Her night: in bed now, or gone to bed with her goodbye (her rest runs to her morning).
    const auto night = companionNight(companionLook());
    const auto now = unixNow();
    if (!night || night->sleepless || now >= night->to) return away;
    away.sleeping = now >= night->from || (away.seconds && now + away.seconds + 60 >= night->to);
    // Asleep before her rest is over: back only in the morning.
    if (away.sleeping || (away.seconds && now + away.seconds > night->from)) away.seconds = std::max(away.seconds, night->to - now);
    return away;
}

std::vector<companion::mind::Candidate> Launcher::companionMindCandidates(companion::Look look, const std::wstring& query, bool chatting) {
    using companion::mind::Candidate;
    using companion::mind::Status;
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    if (!self.failed_partners.empty()) {
        for (const auto& partner : self.failed_partners)
            companionCognition().adjustPeerTension(context.look, partner, 20);
        self.failed_partners.clear();
        saveCompanionCognition();
    }
    auto out = companion::mind::candidates(self, context, companionMemory(), query);
    if (settings_.companion_memory || settings_.companion_notes) {
        auto more = companionCognition().candidates(companionMemory(), context.look, query, context.now, context.english, nameOf);
        out.insert(out.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
    }
    // How she feels as this conversation started (companion_mood.hpp): strong, said once — in her
    // first reply of the conversation only.
    if (chatting && !companion_.mood.empty() && !companion_.mood_spent) out.push_back({"mood", companion_.mood, 0.7f, Status::feel});
    // What may be on her mind today (character.txt topics): stronger when she wants her own topics.
    if (const auto* c = companion::character(look)) {
        const auto& topics = c->topics(context.english);
        if (!topics.empty()) {
            const SYSTEMTIME local = companion_.clock();
            const uint64_t day = static_cast<uint64_t>(dateKey(local));
            uint64_t pick = (day * 4u + static_cast<uint64_t>(companion::dayPart(local.wHour))) * 0x9E3779B97F4A7C15ull + static_cast<uint64_t>(look) * 0xBF58476D1CE4E5B9ull;
            const size_t first = static_cast<size_t>((pick >> 11) % topics.size());
            const float want = 0.22f + 0.3f * self.drives.autonomy;
            out.push_back({"topic", (context.english ? L"On your mind lately (bring it up only when the talk allows): " : L"Ostatnio chodzi ci po głowie (wspomnij, gdy rozmowa pozwoli): ") +
                                        topics[first], want, Status::want});
            if (topics.size() > 1) {
                const size_t second = (first + 1 + static_cast<size_t>((pick >> 29) % (topics.size() - 1))) % topics.size();
                out.push_back({"topic", (context.english ? L"Also on your mind: " : L"Chodzi ci też po głowie: ") + topics[second], want - 0.08f, Status::want});
            }
        }
    }
    return out;
}

void Launcher::companionMindHeard(const std::wstring& text) {
    // System 1: her face reacts at once (the true feeling, a moment), then she thinks about her answer.
    const auto look = companionLook();
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    const auto quick = companion::mind::quick(text, self, context.stage);
    companion::mind::onPlayerMessage(self, context, quick, text);
    if ((self.trust_delta || self.respect_delta || self.security_delta) && settings_.companion_gamification) {
        if (companionAffinity().adjustAxes(context.look, self.trust_delta, self.respect_delta, self.security_delta))
            saveCompanionAffinity();
        self.trust_delta = self.respect_delta = self.security_delta = 0;
    }
    if (settings_.companion_notes && !text.empty()) {
        const std::string em = quick.flash ? companion::emotionKey(*quick.flash) : std::string{};
        if (companionMemory().reconsolidate(context.look, text, self.affect.valence, em, {}, context.now) > 0)
            saveCompanionMemory();
    }
    // Her curiosity for this reply (now and then): she is not sure she understood him — ask back —
    // or she carries the talk on herself. Not on her goodbye, not with a guest (requestCompanion).
    const uint32_t seed = (companion_.seed ^ static_cast<uint32_t>(context.now)) * 2654435761u + static_cast<uint32_t>(text.size());
    self.urge = companion_.closing ? companion::mind::Urge{} : companion::mind::curiosityUrge(self, context, quick, seed);
    // Something of his life she will not dig into now: a thread to come back to later.
    if (quick.hook && self.urge.move == companion::CuriousMove::none && text.size() >= 20) {
        auto thread = text.size() > 120 ? text.substr(0, 117) + L"…" : text;
        self.threads.push_back({context.now, std::move(thread), false});
        if (self.threads.size() > 5) self.threads.erase(self.threads.begin());
    }
    if (quick.flash) companion_.rig.flash(*quick.flash, 0.8f, companion::Emotion::thinking);
    else companion_.rig.emote(self.urge.move == companion::CuriousMove::clarify ? companion::Emotion::confused : companion::Emotion::thinking, false);
    // Item 1: Cerebellar / Goffman typing hesitation when she is in deliberation mode, dilemma or flustered.
    companion_.chat.hesitating = companion::mind::needsDeliberation(self, context) || quick.flash == companion::Emotion::shy;
    companion_.chat.hesitation_clock = 0.0f;
    saveCompanionMind();
}

void Launcher::companionMindReplied(const CompanionMessage& message) {
    if (message.purpose != CompanionMessage::Purpose::chat || !message.reply.ok || message.reply_look.empty()) return;
    const auto look = companion::lookFromKey(message.reply_look);
    if (!look) return;
    const auto context = companionMindContext(*look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    bool memory_changed = false;
    bool told_private = false;
    if (!message.reply.appraisal.empty()) {
        const auto appraisal = companion::mind::parseAppraisal(message.reply.appraisal);
        if (appraisal.ok && (settings_.companion_notes || settings_.companion_memory)) {
            const auto applied = companion::mind::apply(self, companionMemory(), appraisal, context, message.guest_look,
                                                        [this](std::wstring_view title) { return companionGameKey(std::wstring(title)); });
            memory_changed = !appraisal.beliefs.empty() || applied.revisions;
            told_private = !appraisal.told.empty();
            if (applied.met > 0 && settings_.companion_gamification) {
                if (companionAffinity().adjustAxes(context.look, 2, 0, 2)) saveCompanionAffinity();
            }
            // What she learnt of his or her gender (Settings → "Twoja płeć" still wins when set).
            if (appraisal.player_gender) {
                auto& aff = companionAffinity();
                if (aff.player_gender != *appraisal.player_gender) {
                    aff.player_gender = *appraisal.player_gender;
                    saveCompanionAffinity();
                    companionSyncPerson();
                }
            }
        }
    }
    // Her suggestions: she expects him to play them.
    std::vector<std::pair<std::string, std::wstring>> suggested;
    for (const auto& title : message.reply.games)
        if (auto key = companionGameKey(title); !key.empty()) suggested.push_back({std::move(key), title});
    companion::mind::onSuggested(self, context, suggested);
    // A private episode she just told him about (by its words, when her appraisal did not say).
    if (companion::mind::noticeTold(self, message.reply.text) > 0) told_private = true;
    bool vulnerable = told_private;
    for (const auto& seg : message.reply.emotions) {
        if (seg.emotion == companion::Emotion::shy || seg.emotion == companion::Emotion::sad ||
            seg.emotion == companion::Emotion::cry || seg.emotion == companion::Emotion::love) {
            vulnerable = true;
            break;
        }
    }
    companion::mind::onReply(self, context, !message.reply.games.empty(), message.reply.text, vulnerable);
    // Her conversation's measurements (Diagnostyka): length against his, questions, repeated
    // openings, retold stories, her curiosity and what she had in mind.
    {
        const auto count = [](std::wstring_view text) {
            int words = 0; bool in = false;
            for (const wchar_t ch : text) { const bool letter = std::iswalnum(ch) != 0; if (letter && !in) ++words; in = letter; }
            return words;
        };
        CompanionState::TurnMetric metric;
        metric.conversation = companion_.chat_started;
        metric.his_words = count(companion_.last_query);
        metric.her_words = count(message.reply.text);
        metric.question = message.reply.text.find(L'?') != std::wstring::npos;
        metric.retold = self.retold;
        metric.workspace = message.timing.workspace;
        for (const auto& act : self.curious) if (act.at == context.now) metric.curiosity = act.move;
        std::wstring opening;
        int taken = 0; bool in = false;
        for (const wchar_t ch : game_index::fold(message.reply.text)) {
            const bool letter = std::iswalnum(ch) != 0;
            if (letter && !in && ++taken > 2) break;
            if (letter) opening += ch; else if (in && taken < 2) opening += L' ';
            in = letter;
        }
        metric.opening = opening;
        auto& metrics = companion_.turn_metrics;
        metric.repeated_opening = !opening.empty() && std::any_of(metrics.begin(), metrics.end(), [&](const auto& m) {
            return m.conversation == metric.conversation && m.opening == metric.opening; });
        metrics.push_back(std::move(metric));
        if (metrics.size() > 120) metrics.erase(metrics.begin());
    }
    if ((self.trust_delta || self.respect_delta || self.security_delta) && settings_.companion_gamification) {
        if (companionAffinity().adjustAxes(context.look, self.trust_delta, self.respect_delta, self.security_delta))
            saveCompanionAffinity();
        self.trust_delta = self.respect_delta = self.security_delta = 0;
    }
    // Her relationship or her life turned in this reply (her tags): a jolt, maybe a milestone.
    companion::mind::Shift shift;
    shift.stance = message.reply.stance_change;
    shift.role = message.reply.role_change;
    shift.health = message.reply.health_change;
    shift.work = message.reply.work_change;
    shift.wealth = message.reply.wealth_change;
    shift.family = message.reply.family_change;
    shift.social = message.reply.social_change;
    companion::mind::onShift(self, context, shift);
    if (memory_changed) saveCompanionMemory();
    saveCompanionMind();
    companionDailyReading();
}

void Launcher::companionMindBegin() {
    const auto look = companionLook();
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    // Another companion may let slip what the player told her (never a secret; the gossipy ones more often).
    if (settings_.companion_notes && settings_.companion_memory) {
        const auto gossip_of = [](std::string_view key) {
            const auto other = companion::lookFromKey(key);
            const auto* c = other ? companion::character(*other) : nullptr;
            return c ? c->gossip : 0.0f;
        };
        if (const auto leaked = companion::mind::leak(companionMemory(), context.look, gossip_of, companion_.seed, context.now)) {
            companionCognition().postGossip(leaked->from, "secret", leaked->text, context.now, context.look);
            saveCompanionCognition();
            saveCompanionMemory();
        }
    }
    saveCompanionMind();
    companionDailyReading();
    companionLifeFill();
}

void Launcher::companionMindEnd() {
    const auto look = companion_.chat_look.value_or(companionLook());
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    int streak = 0;
    if (settings_.companion_gamification) streak = companionAffinity().currentStreak(dateKey(companion_.clock()));
    companion::mind::onConversationEnd(self, context, streak);
    saveCompanionMind();
}

void Launcher::companionMindPlayed(const std::string& key, uint64_t played_ms) {
    if (key.empty()) return;
    std::wstring title;
    for (const auto& game : public_library_)
        if (gameKey(game) == key) { title = game.name; break; }
    const auto minutes = static_cast<uint32_t>(played_ms / 60'000);
    const auto now = unixNow();
    // Every companion who expected it: each of their minds settles it.
    for (const auto look : companion::availableLooks()) {
        const auto context = companionMindContext(look);
        auto& self = companionMindState().of(context.look);
        companion::mind::onPlayed(self, context, key, title, minutes);
    }
    // Her suggestion played: she brags to the others (gossip).
    if (minutes >= 15 && !title.empty() && settings_.companion_memory)
        for (const auto& pick : companionMemory().picks)
            if (pick.key == key && !pick.look.empty() && pick.at + 14 * 86400 > now) {
                companionCognition().postGossip(pick.look, "played", title, now);
                saveCompanionCognition();
                break;
            }
    saveCompanionMind();
}

void Launcher::companionDailyReading() {
    // Her own web reading (Tavily key only), in the background: one search at a time, up to six a
    // day spread over it (cognition::dailyReadingDue), only while she is up by her own clock; what
    // she finds waits for her next replies.
    if (!settings_.companion || !companion_.persist || companion_.reading_busy) return;
    const auto now = unixNow();
    if (now < companion_.reading_tried + 30 * 60) return; // Not again so soon, found or not.
    const auto web_key = web_search::key(userConfigDirectory());
    if (web_key.empty()) return;
    const auto look = companionLook();
    const auto context = companionMindContext(look);
    if (!companion::mind::awake(context)) return;
    const int today = dateKey(companion_.clock());
    auto& cog = companionCognition();
    if (!cog.dailyReadingDue(context.look, today, now)) return;
    companion_.reading_tried = now;
    std::wstring current, goal;
    if (companion_.last_session && !companion_.last_session->titles.empty()) current = companion_.last_session->titles.front();
    if (const auto* self = companionMindState().find(context.look))
        for (const auto& g : self->goals)
            if (g.kind == companion::mind::Goal::Kind::own && g.status == companion::mind::Goal::Status::open) { goal = g.text; break; }
    companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    // One she has not looked up in the last three days.
    std::wstring query;
    for (const auto& q : companion::cognition::dailyReadingQueries(look, companionMemory(), current, context.english, companion_.seed, goal)) {
        const auto folded = game_index::fold(q);
        const bool recent = std::any_of(cog.readings.begin(), cog.readings.end(), [&](const companion::cognition::Reading& r) {
            return r.look == context.look && now < r.at + 3 * 86400 && game_index::fold(r.topic) == folded;
        });
        if (!recent) { query = q; break; }
    }
    if (query.empty()) return;
    companion_.reading_busy = true;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, web_key, query = std::move(query), look_key = context.look, today, now] {
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::reading;
        const auto summary = companion::cognition::summarizeTavilyResult(web_search::search(web_key, query));
        if (!summary.empty()) message->daily_readings.push_back({now, today, look_key, query, summary});
        post(std::move(message)); // Always: it ends her reading.
    });
}

std::wstring Launcher::companionWeatherPlace(companion::Look look, bool* travelling) const {
    if (travelling) *travelling = false;
    if (companion_.mind) {
        const auto context = companionMindContext(look);
        if (const auto* self = companion_.mind->find(context.look))
            if (auto town = companion::mind::travelPlace(*self, context); !town.empty()) {
                if (travelling) *travelling = true;
                return town;
            }
    }
    const auto* c = companion::character(look);
    return c ? companion::weather::homeCity(*c) : std::wstring{};
}

const companion::weather::Report* Launcher::companionWeatherNow(companion::Look look) const {
    if (!settings_.companion_weather || !companion_.weather) return nullptr;
    bool travelling = false;
    const auto place = companionWeatherPlace(look, &travelling);
    const auto& report = *companion_.weather;
    if (place.empty() || report.asked != place || unixNow() > report.at + 3 * 3600) return nullptr;
    return &report;
}

void Launcher::companionWeatherRefresh() {
    // In the background, never before her answer: when she has no report for where she is now, or
    // it is older than 45 minutes; one fetch at a time, not more often than every 10 minutes.
    if (!settings_.companion || !settings_.companion_weather || !companion_.persist || companion_.weather_busy) return;
    const auto now = unixNow();
    const auto look = companionLook();
    bool travelling = false;
    const auto place = companionWeatherPlace(look, &travelling);
    if (place.empty()) return;
    const bool fresh = companion_.weather && companion_.weather->asked == place && now < companion_.weather->at + 45 * 60;
    if (fresh || now < companion_.weather_tried + 10 * 60) return;
    companion_.weather_tried = now;
    companion_.weather_busy = true;
    // Her home's own coordinates when character.txt (or a city from Settings) gives them (no
    // lookup); a trip's town is looked up.
    std::optional<std::pair<double, double>> coordinates;
    if (!travelling)
        if (const auto* c = companion::character(look)) coordinates = c->coordinates();
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, get = companion_.weather_get, place, coordinates, travelling, en = english(), now] {
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::weather;
        message->weather = companion::weather::fetch(get, place, coordinates, en, now);
        if (message->weather) message->weather->travelling = travelling;
        post(std::move(message)); // Always: it ends her fetch.
    });
}

void Launcher::pollCompanionLife() {
    if (!settings_.companion || !companion_.persist) return;
    const uint64_t tick = GetTickCount64();
    if (!companion_.life_poll) { companion_.life_poll = tick + 2 * 60 * 1000; return; } // Not while the launcher starts.
    if (tick < companion_.life_poll) return;
    companion_.life_poll = tick + 10 * 60 * 1000;
    companionDailyReading();
    companionWeatherRefresh();
    // Spontaneous home-screen initiative (#21): if she is awake, the player hasn't talked to her for
    // a few hours and her bond/curiosity need is high, she may speak up in the home bubble on her own.
    if (!companionOpen() && !companion_.busy && !companion_.offline && companion_.bubble.alpha <= 0.01f && !companion_.bubble.waiting && tick >= companion_.quiet_until) {
        const auto look = companionLook();
        const auto context = companionMindContext(look);
        if (companion::mind::awake(context)) {
            auto& self = companionMindState().of(context.look);
            companion::mind::tick(self, context);
            const bool due = self.last_talk > 0 && context.now >= self.last_talk + 4 * 3600 && context.now >= self.last_initiative + 4 * 3600 &&
                             (self.drives.bond >= 0.62f || self.drives.curiosity >= 0.65f || self.harsh_at > 0);
            if (due) {
                self.last_initiative = context.now;
                saveCompanionMind();
                const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(tick);
                companion_.bubble = {};
                companion_.bubble.waiting = true;
                companion_.bubble.alpha = 1.0f;
                requestCompanion(CompanionMessage::Purpose::home, companion::mind::spontaneousPrompt(self, context, seed));
            }
        }
    }
}

bool Launcher::tickCompanionInitiative(float dt) {
    if (!companionOpen() || !settings_.companion || companion_.offline) {
        companion_.chat_quiet = 0;
        return false;
    }
    auto& chat = companion_.chat;
    // Only while the chat window is sitting idle (no overlays, no typing, no active voice/typewriter, no guest).
    const bool idle = !chat.waiting && !chat.talking() && !companion_.busy && chat.input.empty() && !chat.archive && !chat.mind && !chat.viewer && !chat.feed &&
                      !chat.confirm && !chat.menu && !companionRouletteSpinning() && !companion_.guest.look && !companionVoiceHolding();
    if (!idle) {
        companion_.chat_quiet = 0;
        return false;
    }
    // Her morning (or a new day of chats) with the chat left open over her night: the day's
    // conversation starts afresh, and she opens it.
    if (chat.ended && !companionResting() && (chat.log.empty() || companion::chats::dayOf(companion_.chat_started) != companion::chats::dayOf(companionNow()))) {
        companion_.chat_quiet = 0;
        companionLoadChat(); // Yesterday's goes; today's resumes, if there is one.
        if (chat.log.empty() && !chat.waiting) {
            chat.focus = companion_ui::focus_input;
            companionBegin();
        }
        return true;
    }
    // Either the conversation is active and the player went quiet after her reply, OR the conversation
    // ended earlier and her rest time is now over while the player still has the chat open.
    const bool active_quiet = !chat.ended && !chat.log.empty() && !chat.log.back().user;
    const bool rested_reopen = chat.ended && !companionResting();
    // Past her bedtime and the player went quiet: after a pause she says goodnight herself.
    const bool bedtime = active_quiet && companionAsleep(companionLook());
    // Someone she has only just met she writes to at most once when he goes quiet; later twice.
    const int nudges = companionStage(companionLook()) == companion::affinity::Stage::strangers ? 1 : 2;
    if ((!active_quiet && !rested_reopen) || (companion_.chat_nudges >= nudges && !bedtime)) {
        companion_.chat_quiet = 0;
        return false;
    }
    companion_.chat_quiet += dt;
    if (companion_.chat_quiet < companion_.chat_nudge_after) return false;
    companion_.chat_quiet = 0;
    const bool en = english();
    if (bedtime) {
        companionFlush();
        companion_.closing = true; // Her reply ends the conversation (companionEnd: she sleeps until her morning).
        companion_.guest_after = -1;
        companion_.turns.push_back({true, companion::chats::bedtimeKickoff(en)});
        companion_.next_reply_free = true;
        chat.waiting = true;
        chat.scroll = 0;
        requestCompanion(CompanionMessage::Purpose::chat, {}, companion::chats::farewellLine(companionPersona(), en, companion_.seed, true));
        companionSaveChat();
        return true;
    }
    ++companion_.chat_nudges;
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    companion_.chat_nudge_after = 95.0f + static_cast<float>((seed >> 6) % 50);
    const auto look = companionLook();
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    self.last_initiative = context.now;
    saveCompanionMind();
    if (rested_reopen) {
        chat.ended = false;
        companion_.closing = false;
        chat.focus = companion_ui::focus_input;
    }
    companionFlush();
    companion_.turns.push_back({true, companion::mind::spontaneousPrompt(self, context, seed)});
    companion_.next_reply_free = true;
    chat.waiting = true;
    chat.scroll = 0;
    requestCompanion(CompanionMessage::Purpose::chat);
    companionSaveChat();
    return true;
}

std::wstring Launcher::companionOthersText(companion::Look look) const {
    std::wstring out;
    const bool en = english();
    for (const auto other : companion::availableLooks()) {
        if (other == look) continue;
        std::wstring city;
        if (const auto* c = companion::character(other)) {
            const auto& home = c->home(en);
            city = home.substr(0, std::min(home.find(L','), home.find(L" -")));
        }
        out += (out.empty() ? L"" : L", ") + std::wstring(companion::name(other));
        if (!city.empty() && city.size() <= 40) out += L" (" + city + L")";
    }
    return out;
}

std::wstring Launcher::companionPlanRules(companion::Look look) const {
    const auto context = companionMindContext(look);
    const companion::mind::Self none{};
    const auto* self = companion_.mind ? companion_.mind->find(context.look) : nullptr;
    return companion::mind::planRules(self ? *self : none, context, companionOthersText(look));
}

void Launcher::companionMindEpisodes(std::string_view look_key, std::wstring_view lines, bool planned, uint64_t from) {
    const auto look = companion::lookFromKey(look_key);
    if (!look) return;
    const auto parsed = companion::mind::parseEpisodes(lines);
    if (parsed.empty()) return;
    const auto context = companionMindContext(*look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    // Another companion named in "with": online, and it is in her life too.
    const auto companion_of = [](std::wstring_view with) -> std::string {
        const auto folded = game_index::fold(with);
        for (const auto other : companion::availableLooks()) {
            const auto name = game_index::fold(companion::name(other));
            if (name.size() >= 3 && folded.find(name) != std::wstring::npos) return std::string(companion::lookKey(other));
        }
        return {};
    };
    const auto added = companion::mind::addEpisodes(self, context, parsed, planned, from, companion_of);
    for (const auto& episode : added)
        if (!episode.partner.empty()) companion::mind::shareEpisode(companionMindState().of(episode.partner), episode, context.look, companion::name(*look));
    if (!added.empty()) saveCompanionMind();
}

void Launcher::companionLifeFill() {
    // A long stretch since her last episode or talk: her diary fills in what she did (in the
    // background; her greeting does not wait for it).
    if (!settings_.companion || !companion_.persist || !settings_.companion_memory || companion_.life_busy) return;
    const auto look = companionLook();
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    const auto gap = companion::mind::lifeGap(self, context);
    if (!gap) return;
    companion_.life_busy = true;
    companion::PromptExtras extras;
    extras.utc = context.now;
    extras.bond.health = context.life.health;
    extras.bond.work = context.life.work;
    extras.bond.wealth = context.life.wealth;
    extras.bond.family = context.life.family;
    extras.bond.social = context.life.social;
    auto system = companion::diaryPrompt(companionPersonaOf(look), look, context.english, &extras);
    std::vector<companion::Turn> turns{{true, companion::mind::lifeRequest(self, context, *gap, companionOthersText(look))}};
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    companionSyncPerson(); // Her days speak of him or her, as he or she is.
    work_.start([post, system = std::move(system), turns = std::move(turns), access = companionAccess(), transport = companion_.post, alive = alive_,
                 look_key = context.look, gap = *gap] {
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::life;
        message->summary_look = look_key;
        message->life_from = gap.from;
        message->life_to = gap.to;
        const auto connection = access.resolve();
        if (connection.ready() && alive->load()) message->reply = companion::ask(connection, system, turns, transport, 300, 0.7f);
        post(std::move(message)); // Always: it ends her fill-in.
    });
}

bool Launcher::companionMindPeek(std::string_view id) {
    auto& self = companionMindState().of(companion::lookKey(companionLook()));
    if (!companion::mind::peekEpisode(self, id)) return false;
    saveCompanionMind();
    return true;
}

std::wstring Launcher::companionMindLife(companion::Look look, bool* invite) {
    const auto context = companionMindContext(look);
    auto& self = companionMindState().of(context.look);
    companion::mind::tick(self, context);
    auto story = companion::mind::lifeStory(self, context.english, invite);
    if (const auto recent = companion::mind::recentLife(self, context); !recent.empty()) story += (story.empty() ? L"" : L" ") + recent;
    return story;
}

std::vector<Launcher::CompanionMindItem> Launcher::companionMindItems(companion::Look look) const {
    std::vector<CompanionMindItem> out;
    if (companion_.feed.look == companion::lookKey(look)) {
        const auto& posts = companion_.feed.view.archive.posts;
        for (size_t i = 0; i < std::min<size_t>(posts.size(), 2); ++i)
            out.push_back({posts[i].text, english() ? L"Public Launchfeed post" : L"Publiczny post Launchfeed",
                           companion::feed::reference(posts[i], english()), L"Launchfeed", 0});
    }
    if (!companion_.mind) return out;
    const auto context = companionMindContext(look);
    const auto* self = companion_.mind->find(context.look);
    if (!self) return out;
    const bool en = context.english;
    const auto affect = companion::mind::settled(self->affect, context);
    {
        CompanionMindItem mood{en ? L"Mood" : L"Nastrój", en ? L"Her mood" : L"Jej nastrój", companion::mind::describe(affect, en), {}, 0};
        mood.inner = true;
        mood.short_term = true;
        out.push_back(std::move(mood));
    }
    if (!self->counterfactual.empty() && context.now <= self->counterfactual_at + 12 * 3600) {
        CompanionMindItem cf{self->counterfactual, en ? L"Unchosen impulse" : L"Wahanie (impuls)", self->counterfactual, {}, self->counterfactual_at};
        cf.inner = true;
        cf.short_term = true;
        out.push_back(std::move(cf));
    }
    if (!self->ruminations.empty() && context.now <= self->ruminations.back().at + 48 * 3600) {
        const auto& rum = self->ruminations.back();
        CompanionMindItem item{rum.text, en ? L"Rumination" : L"Ruminacja w tle", rum.text, {}, rum.at};
        item.inner = true;
        item.short_term = true;
        out.push_back(std::move(item));
    }
    if (self->harsh_at > 0 && context.now <= self->harsh_at + 24 * 3600) {
        const std::wstring why = self->harsh_reason.empty() ? (en ? L"her own bad mood" : L"własny zły humor") : self->harsh_reason;
        CompanionMindItem item{en ? L"Wants to apologize" : L"Chce przeprosić",
                               en ? L"Remorse for snapping" : L"Wyrzut za oschłość",
                               (en ? L"She feels bad for being cold earlier because of " : L"Głupio jej, że wcześniej była oschła przez ") + why + L".",
                               {},
                               self->harsh_at};
        item.inner = true;
        item.short_term = true;
        out.push_back(std::move(item));
    }
    // Her curiosity (asking back when unsure she understood him, carrying the talk on, reflecting
    // what he said): what she is about to do now, what she did and how he took it, a question for later.
    {
        using companion::CuriousMove;
        const auto category = [&](CuriousMove move, bool later) -> std::wstring {
            if (later) return en ? L"Curiosity · for later" : L"Ciekawość · na później";
            return move == CuriousMove::clarify ? (en ? L"Curiosity · asking back" : L"Ciekawość · dopytanie")
                 : move == CuriousMove::reflect ? (en ? L"Listening · reflecting" : L"Słuchanie · odbicie")
                                                : (en ? L"Curiosity · carrying the talk" : L"Ciekawość · ciągnie rozmowę");
        };
        if (self->urge.move != CuriousMove::none && context.now <= self->urge.at + 120) {
            using companion::CuriousCarry;
            const auto carry = self->urge.carry;
            std::wstring label = self->urge.move == CuriousMove::clarify ? (en ? L"I'll ask you (now)" : L"Dopytam cię (teraz)")
                               : self->urge.move == CuriousMove::reflect ? (en ? L"I'll reflect it back (now)" : L"Odbiję twoje słowa (teraz)")
                               : carry == CuriousCarry::follow_up ? (en ? L"I'll ask how it went (now)" : L"Zapytam, jak poszło (teraz)")
                               : carry == CuriousCarry::returning ? (en ? L"Back to my question (now)" : L"Wrócę do pytania (teraz)")
                                                                  : (en ? L"I'll carry the talk on (now)" : L"Pociągnę rozmowę (teraz)");
            CompanionMindItem item{std::move(label), category(self->urge.move, false), (en ? L"Why: " : L"Dlaczego: ") + self->urge.why, {}, self->urge.at};
            if (!self->urge.carried.empty())
                item.detail += (carry == CuriousCarry::follow_up ? (en ? L" · How did it go: \"" : L" · Jak poszło: „")
                              : carry == CuriousCarry::returning ? (en ? L" · The question you left unanswered: \"" : L" · Pytanie, na które nie ‹odpowiedziałeś|odpowiedziałaś›: „")
                              : carry == CuriousCarry::thread ? (en ? L" · Back to your thread: \"" : L" · Wraca do twojego wątku: „")
                                                              : (en ? L" · Back to the question she set aside: \"" : L" · Wraca do odłożonego pytania: „")) +
                               self->urge.carried + (en ? L"\"" : L"”");
            item.inner = item.short_term = item.thought = item.active = true;
            out.push_back(std::move(item));
        }
        int shown = 0;
        for (auto it = self->curious.rbegin(); it != self->curious.rend() && shown < 4; ++it) {
            const auto& c = *it;
            const uint64_t when = c.at ? c.at : c.later_at;
            if (!c.deferred() && when + 24 * 3600 < context.now) continue;
            const bool clarify = c.move == CuriousMove::clarify, reflect = c.move == CuriousMove::reflect;
            std::wstring label;
            if (c.deferred()) label = en ? L"I'll ask you (later)" : L"Zapytam cię (później)";
            else if (c.outcome == 2) label = en ? L"Never got to ask" : L"Nie zdążyła zapytać";
            else if (c.outcome == -1) label = clarify ? (en ? L"Asked back — waiting" : L"Dopytała — czeka na ciebie") : reflect ? (en ? L"Reflected — listening" : L"Odbiła twoje słowa — słucha")
                                                      : (en ? L"Carried on — waiting" : L"Ciągnie rozmowę — czeka");
            else if (c.outcome == 1) label = clarify ? (en ? L"Asked back — you explained" : L"Dopytała — już rozumie") : reflect ? (en ? L"Reflected — you went on" : L"Odbiła — usłyszała więcej")
                                                     : (en ? L"Carried on — you took it up" : L"Pociągnęła — temat podjęty");
            else label = clarify ? (en ? L"Asked back — no answer" : L"Dopytała — bez odpowiedzi") : reflect ? (en ? L"Reflected — you closed up" : L"Odbiła — temat zamknięty")
                                 : (en ? L"Carried on — you let it pass" : L"Pociągnęła — temat zbyty");
            if (c.returned && c.outcome == 0) label += en ? L" (came back to it)" : L" (wróciła)";
            std::wstring detail;
            if (!c.about.empty())
                detail = (c.deferred() || c.outcome == 2 ? (en ? L"Wants to ask: \"" : L"Chce zapytać: „") : clarify ? (en ? L"Asked: \"" : L"Dopytała: „")
                          : reflect ? (en ? L"Gave back: \"" : L"Oddała twoje słowa: „") : (en ? L"Carried the talk on: \"" : L"Pociągnęła rozmowę: „")) +
                         c.about + (en ? L"\"" : L"”");
            if (!c.why.empty()) detail += (detail.empty() ? L"" : L" · ") + std::wstring(en ? L"Why: " : L"Dlaczego: ") + c.why;
            if (c.deferred()) detail += en ? L" · Not the right moment: she set it aside for later." : L" · To nie była dobra chwila: odłożyła to pytanie na później.";
            else if (c.outcome == -1) detail += en ? L" · Waiting for your answer." : L" · Czeka na twoją odpowiedź.";
            else if (c.outcome == 1) detail += clarify ? (en ? L" · You explained; now she understands." : L" · ‹Wyjaśniłeś|Wyjaśniłaś› — teraz rozumie.")
                                             : reflect ? (en ? L" · She got you right: you told her more." : L" · Trafiła: ‹powiedziałeś|powiedziałaś› więcej.") : (en ? L" · You took it up." : L" · ‹Podjąłeś|Podjęłaś› temat.");
            else if (c.outcome == 0) detail += clarify ? (en ? L" · You didn't clear it up." : L" · Nie ‹wyjaśniłeś|wyjaśniłaś› tego.") : (en ? L" · You let it pass." : L" · ‹Zbyłeś|Zbyłaś› to krótko.");
            else detail += en ? L" · The right moment never came." : L" · Nie było dobrej chwili.";
            if (c.later_at && c.at) detail += en ? L" · She had set it aside earlier." : L" · Wcześniej odłożyła je na później.";
            CompanionMindItem item{std::move(label), category(c.move, c.deferred()), std::move(detail), {}, when};
            item.key = c.id;
            item.inner = item.short_term = item.thought = true;
            item.active = c.outcome == -1 && c.at > 0; // Asked and waiting for his answer.
            out.push_back(std::move(item));
            ++shown;
        }
    }
    // What is coming up in his life: she remembers it, wishes him luck, asks how it went after.
    for (const auto& f : self->follow_ups) {
        if (f.state == 2 || (f.state != 0 && f.settled + 2 * 86400 < context.now)) continue;
        const std::wstring when = f.when.empty() ? std::wstring{} : L" (" + f.when + L")";
        const bool due = f.state == 0 && context.now >= f.due;
        std::wstring label = f.state == 1 ? (en ? L"Asked how it went" : L"Zapytała, jak poszło") : f.state == 3 ? (en ? L"You told her how it went" : L"Wie od ciebie, jak poszło")
                           : due ? (en ? L"I'll ask how it went" : L"Zapytam, jak poszło") : (en ? L"Remembers what's ahead of you" : L"Pamięta, co cię czeka");
        CompanionMindItem item{std::move(label), en ? L"Your life · follow-up" : L"Twoje sprawy · do zapytania", f.what + when +
                               (f.wished ? (en ? L" · She wished you luck." : L" · Życzyła ci powodzenia.") : L""), {}, f.made};
        item.short_term = true;
        item.active = due;
        out.push_back(std::move(item));
    }
    // How he answers what she reaches out with (Gottman's bids), how he writes, a thread of his.
    if (const float toward = self->bids.toward, cold = self->bids.away + self->bids.against; toward + cold >= 2) {
        const float ratio = toward / std::max(0.5f, cold);
        const auto round = [](float v) { return std::to_wstring(static_cast<int>(std::lround(v))); };
        CompanionMindItem item{ratio >= 5 ? (en ? L"You take up her bids" : L"Podejmujesz jej zaproszenia") : ratio < 1.5f ? (en ? L"You often brush her off" : L"Często ją zbywasz")
                                          : (en ? L"Her bids: mixed" : L"Jej zaproszenia: różnie"),
                               en ? L"Bids for connection" : L"Zaproszenia do kontaktu",
                               (en ? L"Taken up ≈" : L"Podjęte ≈") + round(toward) + (en ? L" · brushed off ≈" : L" · zbyte ≈") + round(self->bids.away) +
                                   (en ? L" · pushed away ≈" : L" · odepchnięte ≈") + round(self->bids.against) + (en ? L" (lately; Gottman: healthy is about 5 : 1)" : L" (ostatnio; Gottman: zdrowo to ok. 5 : 1)"),
                               {}, self->bids.at};
        out.push_back(std::move(item));
    }
    if (const auto style = companion::mind::styleText(self->his_style, en); !style.empty())
        out.push_back({en ? L"How you write" : L"Jak piszesz", en ? L"She tunes in to you" : L"Dostraja się do ciebie", style, {}, 0});
    for (const auto& t : self->threads)
        if (!t.used) {
            CompanionMindItem item{t.text, en ? L"Your thread · to pick up" : L"Twój wątek · do podjęcia", (en ? L"You started: \"" : L"‹Zacząłeś|Zaczęłaś›: „") + t.text + (en ? L"\"" : L"”"), {}, t.at};
            item.short_term = true;
            out.push_back(std::move(item));
            break;
        }
    // What he knows about her (she told him herself): she keeps to it and does not retell it as news.
    if (!self->claims.empty()) {
        std::wstring known;
        int count = 0;
        for (auto it = self->claims.rbegin(); it != self->claims.rend() && count < 3; ++it, ++count) known += (count ? L"; " : L"") + it->text;
        out.push_back({(en ? L"You know " : L"Wiesz o niej ") + std::to_wstring(self->claims.size()) + (en ? L" things" : L" rzeczy"),
                       en ? L"What you know about her" : L"Co o niej wiesz", (en ? L"Lately she told you: " : L"Ostatnio ci mówiła: ") + known, {}, self->claims.back().at});
    }
    {
        const auto attachment = companion::mind::attachmentOf(context);
        out.push_back({(en ? L"Attachment: " : L"Przywiązanie: ") + companion::mind::attachmentName(attachment, en), en ? L"Her attachment style" : L"Jej styl przywiązania",
                       attachment == companion::mind::Attachment::anxious ? (en ? L"Misses you sooner and takes brush-offs harder; a small sign from you calms her." : L"Szybciej tęskni i mocniej przeżywa zbycie; uspokaja ją drobny sygnał od ciebie.")
                       : attachment == companion::mind::Attachment::avoidant ? (en ? L"Misses you less openly; after opening up she keeps a little distance for a while." : L"Mniej okazuje tęsknotę; po otwarciu się przez jakiś czas trzyma lekki dystans.")
                       : attachment == companion::mind::Attachment::fearful ? (en ? L"Wants closeness and fears it: misses you sooner, pulls back after opening up." : L"Chce bliskości i się jej boi: szybciej tęskni, po otwarciu się wycofuje.")
                                                                             : (en ? L"Steady: closeness and distance don't throw her." : L"Stabilnie: bliskość i dystans jej nie rozchwiewają."),
                       {}, 0});
    }
    // Her interest in what he writes, and a moment of closeness (a reciprocal disclosure).
    if (self->interest_at) {
        CompanionMindItem item{self->interest >= 0.65f ? (en ? L"Intrigued by you" : L"Zaintrygowana tobą") : self->interest < 0.3f ? (en ? L"Finds it hard to get into the talk" : L"Trudno jej się wkręcić")
                                                       : (en ? L"Interested" : L"Zaciekawiona"),
                               en ? L"Her interest" : L"Jej zaciekawienie",
                               (en ? L"Interest in your replies: " : L"Zaciekawienie twoimi odpowiedziami: ") + std::to_wstring(static_cast<int>(std::lround(self->interest * 100))) + L"%" +
                                   (self->interest_rpe > 0.2f ? (en ? L" · your last one surprised her" : L" · ostatnia ją zaskoczyła") : L""),
                               {}, self->interest_at};
        item.inner = item.short_term = true;
        out.push_back(std::move(item));
    }
    // The real weather where she is (Open-Meteo).
    if (const auto* report = companionWeatherNow(look)) {
        const auto& n = report->now;
        const auto deg = [](double v) { return std::to_wstring(static_cast<int>(std::lround(v))) + L"°C"; };
        const std::wstring where = report->place.name.empty() ? report->asked : report->place.name;
        std::wstring detail = where + L": " + deg(n.temperature) + L", " + companion::weather::describe(n.code, en) +
                              (en ? L" · today " : L" · dziś ") + deg(n.today_min) + L"–" + deg(n.today_max);
        if (n.tomorrow) detail += (en ? L" · tomorrow " : L" · jutro ") + companion::weather::describe(n.tomorrow_code, en) + L", " + deg(n.tomorrow_min) + L"–" + deg(n.tomorrow_max);
        detail += en ? L" · Weather data by Open-Meteo.com" : L" · Dane pogodowe: Open-Meteo.com";
        CompanionMindItem item{where + L" " + deg(n.temperature) + L", " + companion::weather::describe(n.code, en),
                               report->travelling ? (en ? L"Weather on her trip" : L"Pogoda na jej wyjeździe") : (en ? L"Weather where she lives" : L"Pogoda u niej"),
                               std::move(detail), {}, report->at};
        item.inner = item.short_term = true;
        out.push_back(std::move(item));
    }
    if (self->bonded_at && context.now <= self->bonded_at + 86400) {
        CompanionMindItem item{en ? L"A moment of closeness" : L"Chwila bliskości", en ? L"Reciprocal disclosure" : L"Wzajemne zwierzenie",
                               en ? L"You both shared something personal — she felt close and calm." : L"‹Oboje powiedzieliście|Obie powiedziałyście› coś osobistego — poczuła bliskość i spokój.", {}, self->bonded_at};
        item.inner = item.short_term = true;
        out.push_back(std::move(item));
    }
    int untruth_count = 0;
    for (auto it = self->untruths.rbegin(); it != self->untruths.rend() && untruth_count < 3; ++it) {
        const auto& u = *it;
        if (u.at + 14 * 86400 < context.now) continue;
        CompanionMindItem item;
        item.at = u.at;
        item.key = u.id;
        item.inner = !u.confessed;
        item.short_term = !u.confessed;
        if (!u.confessed && !u.peeked) {
            item.locked = true;
            item.label = en ? L"Hidden bluff" : L"Ukryta wymówka";
            item.category = en ? L"Her hidden bluff" : L"Jej ukryta wymówka";
            item.detail = (en ? L"She told you: \"" : L"Powiedziała ci: „") + u.said +
                          (en ? L"\" — but she is hiding the real truth." : L"” — ale ukrywa przed tobą, jak było naprawdę.");
        } else {
            item.label = u.said;
            item.category = u.caught ? (en ? L"Caught fib" : L"Przyłapana na kłamstewku")
                          : u.confessed ? (en ? L"Confessed fib" : L"Wyznane kłamstewko") : (en ? L"Uncovered bluff" : L"Odkryta wymówka");
            item.detail = (en ? L"She claimed: \"" : L"Powiedziała: „") + u.said + (en ? L"\" · Truth: " : L"” · Prawda: ") + u.truth;
            item.detail += u.caught ? (en ? L" · You confronted her with the truth; she didn't confess it herself." : L" · ‹Wytknąłeś|Wytknęłaś› jej prawdę — sama się nie przyznała.")
                         : u.confessed ? (en ? L" · She confessed the truth to you." : L" · Sama przyznała ci się do prawdy.")
                                       : (en ? L" · You uncovered it; she doesn't know." : L" · ‹Zajrzałeś|Zajrzałaś›; ona o tym nie wie.");
        }
        out.push_back(std::move(item));
        ++untruth_count;
    }
    for (const auto& goal : self->goals) {
        if (goal.status != companion::mind::Goal::Status::open) continue;
        using Kind = companion::mind::Goal::Kind;
        const std::wstring category = goal.kind == Kind::own ? (en ? L"Her goal" : L"Jej cel") : goal.kind == Kind::promise ? (en ? L"Her promise" : L"Jej obietnica")
                                                                                                                       : (en ? L"Her intention" : L"Jej zamiar");
        std::wstring detail = goal.text;
        if (goal.kind == Kind::own) detail += L" — " + companion::mind::goalStage(goal, en);
        if (!goal.progress.empty()) detail += (en ? L" · Lately: " : L" · Ostatnio: ") + goal.progress;
        CompanionMindItem item{goal.text, category, std::move(detail), {}, goal.updated};
        item.short_term = goal.kind != Kind::own; // Own multi-week goals are long-term; promises & turn intentions are short-term.
        out.push_back(std::move(item));
    }
    int key_mems = 0;
    for (auto it = self->key_memories.rbegin(); it != self->key_memories.rend() && key_mems < 3; ++it, ++key_mems)
        out.push_back({it->text, en ? L"Key memory" : L"Ważne wspomnienie", it->text, {}, it->at});
    int milestones = 0;
    for (auto it = self->milestones.rbegin(); it != self->milestones.rend() && milestones < 3; ++it, ++milestones)
        out.push_back({it->text, en ? L"Her milestone" : L"Jej osiągnięcie", it->text, {}, it->at});
    int world_items = 0;
    for (auto it = self->player_world.rbegin(); it != self->player_world.rend() && world_items < 4; ++it, ++world_items) {
        const std::wstring detail = it->relation.empty() ? it->name : it->name + L" — " + it->relation;
        out.push_back({it->name, en ? L"Your world" : L"Twój świat", detail, {}, it->at});
    }
    int idioms = 0;
    for (auto it = self->shared_idioms.rbegin(); it != self->shared_idioms.rend() && idioms < 2; ++it, ++idioms)
        out.push_back({it->text, en ? L"Inside joke" : L"Wspólny żart", it->text, {}, it->at});
    int concepts = 0;
    for (auto it = self->learned_concepts.rbegin(); it != self->learned_concepts.rend() && concepts < 2; ++it, ++concepts) {
        const std::wstring detail = it->meaning.empty() ? it->term : it->term + L" — " + it->meaning;
        out.push_back({it->term, en ? L"Learned from you" : L"Nauczone od ciebie", detail, {}, it->at});
    }
    if (!self->rethought.empty()) {
        const auto& rt = self->rethought.back();
        out.push_back({rt.text, en ? L"Re-conceptualized" : L"Przemyślane na nowo", rt.text, {}, rt.at});
    }
    int expected = 0;
    for (const auto& e : self->expectations) {
        if (e.outcome != -1 || expected >= 3) continue;
        CompanionMindItem item{e.text, en ? L"She expects" : L"Spodziewa się", e.text + L" (" + std::to_wstring(static_cast<int>(e.p * 100)) + L"%)", {}, e.made};
        item.short_term = true;
        out.push_back(std::move(item));
        ++expected;
    }
    // Her life outside the chat: the last week and her plans; a private one closed until she tells
    // the player about it or he opens it here.
    using State = companion::mind::Episode::State;
    int episodes = 0;
    for (auto it = self->episodes.rbegin(); it != self->episodes.rend() && episodes < 8; ++it) {
        const auto& e = *it;
        if (e.at + 7 * 86400 < context.now) continue;
        CompanionMindItem item;
        item.at = e.at;
        item.key = e.id;
        item.short_term = e.state == State::planned || (context.now <= e.at + 2 * 86400);
        const auto when = companion::mind::episodeWhen(e, context);
        if (e.state == State::planned && e.at > context.now) {
            const uint64_t hours = (e.at - context.now) / 3600;
            item.when = hours < 1 ? (en ? L"soon" : L"za chwilę") : hours < 24 ? (en ? L"in " : L"za ") + std::to_wstring(hours) + L" h" : (en ? L"later" : L"później");
        }
        if (e.hidden && !e.told && !e.peeked) {
            item.locked = true;
            item.label = en ? L"Private" : L"Prywatne";
            item.category = en ? L"Her private matter" : L"Jej prywatna sprawa";
            item.detail = (en ? L"Something private — " : L"Coś prywatnego — ") + when + (en ? L". She hasn't told you about it." : L". Nie mówiła ci o tym.");
        } else {
            item.label = e.what;
            item.category = e.state == State::planned ? (en ? L"Her plan" : L"Jej plan") : e.state == State::fell_through ? (en ? L"Fell through" : L"Nie wyszło")
                                                                                                                          : (en ? L"What she did" : L"Co robiła");
            item.detail = when;
            if (!e.with.empty()) item.detail += L" — " + e.with;
            if (!e.where.empty()) item.detail += (e.with.empty() ? L" — " : L", ") + e.where;
            item.detail += L": " + e.what;
            if (e.hidden) item.detail += e.told ? (en ? L" · Private — she told you herself." : L" · Prywatne — sama ci powiedziała.")
                                                : (en ? L" · Private — you looked; she doesn't know." : L" · Prywatne — ‹zajrzałeś|zajrzałaś›; ona o tym nie wie.");
        }
        out.push_back(std::move(item));
        ++episodes;
    }
    return out;
}

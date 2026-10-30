// The mind's fast reactions to one exchange (System 1): quick reads of his message, her urge to
// ask, and the bookkeeping after his message, her reply, a suggestion, a game played and a goodbye.
#include "companion_mind_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iterator>
#include <utility>

namespace companion::mind {
using namespace detail;

Quick quick(std::wstring_view message, const Self& self, int stage) {
    Quick out;
    const auto words = spaced(message);
    size_t word_count = 0;
    for (size_t i = 1; i < words.size(); ++i)
        if (words[i] == L' ' && words[i - 1] != L' ') ++word_count;
    const auto set = [&](Emotion emotion, float valence, float arousal) { out.flash = emotion; out.valence = valence; out.arousal = arousal; };
    out.words = static_cast<int>(word_count);
    if (hasAny(words, {L"glupia", L"idiotka", L"debilka", L"zamknij sie", L"spadaj", L"nienawidze cie", L"beznadziejna", L"wkurzasz", L"stupid",
                       L"shut up", L"i hate you", L"useless", L"annoying"})) {
        out.hostile = true;
        set(Emotion::sad, -0.25f, 0.15f);
    }
    else if (hasAny(words, {L"kocham cie", L"love you", L"jestes sliczna", L"jestes piekna", L"jestes slodka", L"jestes urocza", L"buziak", L"przytulam",
                            L"you re cute", L"youre cute", L"you are cute", L"you re beautiful", L"hug"}) || hasRaw(message, {L"😘", L"😍", L"❤", L"<3", L"🥰"})) {
        // From a near stranger it flusters and amuses her; a light flirt makes her smile; only closer it truly warms her.
        if (stage <= 0) set(Emotion::confused, 0.04f, 0.12f);
        else if (stage == 1) set(Emotion::shy, 0.08f, 0.12f);
        else if (stage == 2) set(Emotion::shy, 0.14f, 0.15f);
        else set(Emotion::shy, 0.2f, 0.15f);
    }
    else if (hasAny(words, {L"smutno", L"smutny", L"smutna", L"zle sie czuje", L"jestem zmeczony", L"jestem zmeczona", L"chory", L"chora", L"przykro",
                            L"placze", L"nie mam sily", L"samotny", L"samotna", L"depresja", L"sad", L"tired", L"sick", L"lonely", L"exhausted"}) ||
             hasRaw(message, {L"😢", L"😭", L":(", L";("})) {
        out.sad_or_stressed = true;
        set(Emotion::sad, -0.1f, 0.0f);
    }
    else if (hasAny(words, {L"wygralem", L"wygralam", L"udalo mi sie", L"skonczylem", L"skonczylam", L"ukonczylem", L"ukonczylam", L"zdalem", L"zdalam",
                            L"dostalem", L"dostalam", L"awans", L"i won", L"i finished", L"i beat", L"finally", L"wreszcie", L"nareszcie"}))
        set(Emotion::excited, 0.15f, 0.15f);
    else if (hasAny(words, {L"odinstalowalem", L"odinstalowalam", L"usunalem", L"usunelam", L"porzucilem", L"porzucilam", L"rzucam", L"wtf", L"omg",
                            L"nie wierze", L"uninstalled", L"deleted", L"no way"}) || hasRaw(message, {L"?!", L"!?", L"!!!"}))
        set(Emotion::shock, 0.0f, 0.2f);
    else if (hasAny(words, {L"haha", L"hahaha", L"hehe", L"xd", L"xdd", L"lol", L"lmao", L"rotfl"}) || hasRaw(message, {L"😂", L"🤣", L";p", L":p", L";P", L":P"})) {
        out.playful = true;
        set(Emotion::laugh, 0.1f, 0.1f);
    }
    else if (hasAny(words, {L"dzieki", L"dziekuje", L"super", L"swietnie", L"brawo", L"genialne", L"genialnie", L"thanks", L"thank you", L"awesome", L"great"}))
        set(Emotion::smile, 0.1f, 0.05f);

    // Item 10: ACC (Anterior Cingulate Cortex) & Right-Hemisphere Sarcasm / Implicature Detector (McGilchrist & Grice).
    // 1) Valence clash: hyperbolic praise ("no super", "świetnie", "genialnie", "kocham to") combined with frustration/crash/loss.
    const bool praise_word = hasAny(words, {L"super", L"swietnie", L"genialnie", L"genialne", L"brawo", L"cudownie", L"fantastycznie", L"rewelacja",
                                            L"kocham", L"uwielbiam", L"great", L"awesome", L"fantastic", L"wonderful", L"brilliant", L"love it"});
    const bool ironic_opener = hasAny(words, {L"no super", L"no swietnie", L"no genialnie", L"no brawo", L"po prostu super", L"po prostu swietnie",
                                              L"kocham gdy", L"kocham jak", L"uwielbiam gdy", L"oh great", L"just great", L"love it when"});
    const bool mishap_word = hasAny(words, {L"znowu", L"kolejny raz", L"wywalilo", L"crash", L"bug", L"blad", L"lag", L"stracilem", L"stracilam",
                                            L"usunelo", L"zepsulo", L"padlo", L"nie dziala", L"zdechlo", L"przegralem", L"przegralam", L"zginalem",
                                            L"zginelam", L"szlag", L"kurde", L"masakra", L"dramat", L"crashed", L"lost my save", L"broken",
                                            L"bugged", L"died again", L"stopped working", L"froze", L"corrupted"});
    if ((praise_word && mishap_word) || (ironic_opener && (mishap_word || hasRaw(message, {L"...", L"…"})))) {
        out.irony_or_vent = true;
        out.playful = false;
        set(Emotion::thinking, -0.08f, 0.1f); // Override naive [smile]!
    } else if (hasAny(words, {L"ta jasne", L"no jasne", L"jasne na pewno", L"juz biegne", L"bardzo smieszne", L"haha bardzo smieszne", L"chcialbys",
                              L"chcialabys", L"akurat", L"yeah right", L"sure thing", L"oh sure", L"very funny", L"as if", L"in your dreams"})) {
        out.skeptical_irony = true;
        out.playful = true;
        set(Emotion::smug, 0.04f, 0.08f);
    }

    // Item 4: Berne's "Why Don't You — Yes, But" game rejection vs. acceptance detector.
    if (hasAny(words, {L"nie mam ochoty", L"nie chce mi sie", L"za dluga", L"za dlugie", L"za trudna", L"za trudne", L"za nudna", L"za nudne",
                       L"nie teraz", L"nie dzis", L"nie dzisiaj", L"cos innego", L"raczej nie", L"odpada", L"nie bardzo", L"nie mam nastroju",
                       L"nie mam sily na", L"not in the mood", L"too long", L"too hard", L"too boring", L"something else", L"not now", L"not today",
                       L"pass on that", L"dont feel like"})) {
        out.rejected_pick = true;
    } else if (hasAny(words, {L"zagram", L"odpalam", L"wlaczam", L"dobra propozycja", L"swietny pomysl", L"biore to", L"brzmi super", L"sprobuje",
                              L"i ll play", L"ill play", L"good pick", L"sounds great", L"let s play", L"lets play", L"i will try"})) {
        out.accepted_pick = true;
    }

    // Anti-sycophancy vs. independent opinion (respect axis).
    if (hasAny(words, {L"masz we wszystkim racje", L"zawsze masz racje", L"zgadzam sie ze wszystkim", L"jak rozkazesz", L"cokolwiek powiesz",
                       L"you are always right", L"whatever you say", L"i agree with everything"}))
        out.respect_delta = -4;
    else if (hasAny(words, {L"nie zgadzam sie", L"moim zdaniem", L"ja wole", L"mylisz sie", L"mam inne zdanie", L"wedlug mnie",
                            L"i disagree", L"in my opinion", L"i prefer", L"you re wrong", L"youre wrong"}))
        out.respect_delta = +4;

    // Micro-chronemics: dismissive 1–2 word brush-off after she opened up emotionally, or a long confessional wall of text.
    if (self.last_reply_vulnerable && word_count > 0 && word_count <= 2 &&
        hasAny(words, {L"ok", L"okej", L"spoko", L"fajnie", L"aha", L"no", L"k", L"cool", L"nice", L"whatever", L"hm", L"hmm", L"rozumiem"})) {
        out.dismissive = true;
        set(Emotion::sad, -0.22f, 0.1f);
    } else if (message.size() >= 180 || word_count >= 30) {
        out.confession = true;
        if (!out.flash) set(Emotion::thinking, 0.05f, 0.12f);
    }

    if (!out.flash) {
        // Somatic markers (amygdala vs. vmPFC cortical inhibition): when her energy is high, cortical
        // inhibition dampens the marker; when she is tired, raw subcortical weight fires.
        const auto folded = game_index::fold(message);
        const float control = clamp01(self.drives.energy * (1.0f - 0.5f * self.affect.arousal));
        for (const auto& marker : self.markers) {
            const float effective = marker.valence * (1.0f - 0.65f * clamp01(marker.cortical_inhibition) * control);
            if (std::abs(effective) < 0.35f) continue;
            const auto subject = game_index::fold(marker.subject);
            if (subject.size() < 3 || folded.find(subject) == std::wstring::npos) continue;
            set(effective > 0 ? Emotion::love : Emotion::angry, effective * 0.2f, 0.1f);
            break;
        }
    }

    // Grounding (Clark & Brennan): does she know what he meant? And does his message close the
    // topic, or open one of his own life worth asking about?
    out.asks = message.find(L'?') != std::wstring_view::npos;
    if (hasAny(words, {L"tak i nie", L"i tak i nie", L"sam nie wiem", L"sama nie wiem", L"trudno powiedziec", L"to zalezy", L"zalezy jak", L"zalezy od",
                       L"mieszane uczucia", L"poniekad", L"w pewnym sensie", L"niby tak", L"niby nie",
                       L"yes and no", L"it depends", L"hard to say", L"mixed feelings"}))
        out.unclear = Quick::Unclear::ambivalent;
    else if (hasAny(words, {L"wiesz o co chodzi", L"wiesz o czym mowie", L"wiesz o kim mowie", L"ta sprawa", L"ta cala sprawa", L"tamta sprawa", L"ta rzecz",
                            L"to cos", L"tamto", L"z tym czyms", L"you know what i mean", L"you know who", L"that thing", L"the thing", L"that stuff",
                            L"that whole thing"}))
        out.unclear = Quick::Unclear::vague;
    else if (!out.asks && !out.dismissive && !out.irony_or_vent && word_count >= 1 && word_count <= 8) {
        auto tail = message;
        while (!tail.empty() && std::iswspace(tail.back())) tail.remove_suffix(1);
        if (tail.ends_with(L"...") || tail.ends_with(L"…")) out.unclear = Quick::Unclear::unfinished; // Broken off mid-thought.
    }
    // Closed: nothing but acknowledgements ("ok spoko", "nic ciekawego", "nie wiem", "haha") — every word one of them.
    if (!out.asks && word_count >= 1 && word_count <= 4) {
        static constexpr const wchar_t* closing[]{
            L"ok", L"okej", L"okay", L"oki", L"k", L"spoko", L"dobra", L"dobrze", L"fajnie", L"fajne", L"aha", L"aa", L"no", L"nom", L"mhm", L"jasne",
            L"git", L"luz", L"nic", L"ciekawego", L"specjalnego", L"jakos", L"leci", L"po", L"staremu", L"w", L"porzadku", L"sumie", L"niezle", L"tak",
            L"nie", L"wiem", L"haha", L"hahaha", L"hehe", L"xd", L"xdd", L"lol", L"cool", L"nice", L"fine", L"good", L"yeah", L"yep", L"nope", L"sure",
            L"nothing", L"much", L"same", L"idk", L"meh", L"pewnie", L"moze", L"ta"};
        bool only = true;
        for (size_t start = 1; start < words.size() && only;) {
            const auto end = words.find(L' ', start);
            if (end == std::wstring::npos) break;
            if (end > start) {
                const std::wstring_view word(words.data() + start, end - start);
                only = std::any_of(std::begin(closing), std::end(closing), [&](const wchar_t* c) { return word == c; });
            }
            start = end + 1;
        }
        out.closed = only;
    }
    if (!out.asks && word_count >= 5 &&
        hasAny(words, {L"bylem", L"bylam", L"poszedlem", L"poszlam", L"pojechalem", L"pojechalam", L"mialem", L"mialam", L"zaczalem", L"zaczelam",
                       L"spotkalem", L"spotkalam", L"kupilem", L"kupilam", L"planuje", L"zamierzam", L"jutro", L"w weekend", L"wczoraj", L"dzisiaj",
                       L"w pracy", L"w szkole", L"na uczelni", L"na studiach", L"z bratem", L"z siostra", L"z dziewczyna", L"z chlopakiem", L"z kumplem",
                       L"z kumplami", L"ze znajomymi", L"z rodzicami", L"moj brat", L"moja siostra", L"moj kumpel", L"moja dziewczyna", L"moj chlopak",
                       L"i went", L"i was", L"i had", L"i started", L"i met", L"i bought", L"i m planning", L"im planning", L"tomorrow", L"yesterday",
                       L"this weekend", L"at work", L"at school", L"my brother", L"my sister", L"my friend", L"my girlfriend", L"my boyfriend", L"my parents"}))
        out.hook = true;
    if (!out.hook && !out.asks) {
        // Someone or something from his world she already knows of: worth asking about.
        const auto folded = game_index::fold(message);
        for (const auto& w : self.player_world) {
            const auto head = game_index::fold(w.name.substr(0, w.name.find_first_of(L" (-—:")));
            if (head.size() >= 3 && folded.find(head) != std::wstring::npos) { out.hook = true; break; }
        }
    }
    // His bid for connection (Gottman): he shows her something, has news, wants her attention.
    out.bid = !out.hostile && (hasRaw(message, {L"http://", L"https://", L"www."}) ||
        hasAny(words, {L"patrz", L"popatrz", L"zobacz", L"sprawdz", L"sluchaj", L"posluchaj", L"wiesz co", L"zgadnij", L"mam dla ciebie", L"pokaze ci",
                       L"musze ci powiedziec", L"musze ci cos", L"nie uwierzysz", L"wyobraz sobie", L"look", L"check this", L"check it out", L"guess what",
                       L"you know what", L"listen", L"you won t believe", L"i have to tell you"}));
    return out;
}

Urge curiosityUrge(const Self& self, const Context& context, const Quick& quick, uint32_t seed) {
    Urge out;
    out.at = context.now;
    const bool en = context.english;
    // Never on top of hurt or of being caught, and not again right after the last time.
    if (quick.dismissive || self.peek_caught || self.curious_gap < 2) return out;
    const float energy = energyNow(self, context);
    const float tired = energy < 0.3f ? 0.5f : 1.0f;
    const float roll = static_cast<float>((seed >> 7) % 1000) / 1000.0f;
    // Path 1 — asking back: his message leaves her unsure what he meant.
    if (quick.unclear != Quick::Unclear::none) {
        float p = context.persona == Persona::kuudere || context.persona == Persona::himedere ? 0.55f : 0.7f; // The proud ones admit it less.
        p *= 0.6f + 0.4f * tired;
        if (roll >= p) return out;
        out.move = CuriousMove::clarify;
        out.gentle = quick.sad_or_stressed || quick.confession;
        switch (quick.unclear) {
            case Quick::Unclear::ambivalent:
                out.why = en ? L"‹his|her|their› answer was ambivalent — you can't tell which way ‹he leans|she leans|they lean›" : L"‹odpowiedział|odpowiedziała› niejednoznacznie — nie wiesz, w którą stronę się skłania";
                break;
            case Quick::Unclear::vague:
                out.why = en ? L"‹he refers|she refers|they refer› to something ‹he|she|they› didn't name — you aren't sure what ‹he means|she means|they mean›" : L"odwołuje się do czegoś, czego nie ‹nazwał|nazwała› — nie masz pewności, o co ‹mu|jej› chodzi";
                break;
            default:
                out.why = en ? L"‹he|she|they› broke off mid-thought" : L"‹urwał|urwała› myśl w pół zdania";
                break;
        }
        return out;
    }
    // Not when he asked her something (she answers that).
    if (quick.asks) return out;
    const bool down = quick.sad_or_stressed || quick.confession;
    // Path 3 — reflecting (active listening, Rogers): he opened up; before advice, comfort or a
    // story of her own she gives back in her words what she heard and the feeling in it.
    if (down) {
        float listen = 0.55f;
        switch (context.persona) {
            case Persona::dandere: case Persona::deredere: listen = 0.7f; break;
            case Persona::kichidere: case Persona::himedere: listen = 0.4f; break;
            case Persona::kuudere: case Persona::kanedere: listen = 0.45f; break;
            default: break;
        }
        if (roll < listen * (0.6f + 0.4f * tired)) {
            out.move = CuriousMove::reflect;
            out.gentle = true;
            out.why = en ? L"‹he|she|they› opened up to you" : L"‹otworzył|otworzyła› się przed tobą";
            return out;
        }
    }
    // Path 2 — carrying the talk on.
    float persona = 1.0f;
    switch (context.persona) {
        case Persona::genki: case Persona::yandere: persona = 1.15f; break;
        case Persona::kichidere: persona = 1.2f; break;
        case Persona::deredere: persona = 1.1f; break;
        case Persona::tsundere: persona = 0.9f; break;
        case Persona::himedere: case Persona::kanedere: persona = 0.85f; break;
        case Persona::kuudere: persona = 0.75f; break;
        case Persona::dandere: persona = 0.7f; break;
    }
    const auto& d = self.drives;
    float p = 0;
    if (quick.closed && !down) {
        p = 0.45f + 0.25f * d.curiosity + 0.15f * d.autonomy;
        if (self.topic.turns >= 6) p += 0.15f; // The topic wore thin (habituation): time for another.
        out.why = en ? L"‹he|she|they› closed the topic with a short reply" : L"krótko ‹zamknął|zamknęła› temat";
    } else if (quick.hook || down || quick.irony_or_vent) {
        p = 0.35f + 0.4f * d.curiosity + 0.2f * (self.policy.deep_questions - 0.5f);
        out.deeper = true;
        out.gentle = down;
        if (down) p *= 0.8f;
        out.why = down ? (en ? L"‹he|she|they› opened up to you" : L"‹otworzył|otworzyła› się przed tobą")
                : quick.irony_or_vent ? (en ? L"something went wrong for ‹him|her|them› and ‹he is|she is|they are› frustrated" : L"coś ‹mu|jej› nie wyszło i ‹jest sfrustrowany|jest sfrustrowana›")
                                      : (en ? L"‹he|she|they› told you something from ‹his|her|their› own life" : L"‹opowiedział|opowiedziała› ci coś ze swojego życia");
    } else {
        // In a playful back-and-forth the banter itself carries the talk: fewer questions then.
        p = (0.15f + 0.3f * d.curiosity) * (1.0f - 0.4f * self.play_momentum);
        out.deeper = true;
        out.why = en ? L"you are curious where this goes" : L"jesteś ciekawa, co dalej";
    }
    // Loewenstein's information gap: curiosity peaks when she knows him a little and the rest is
    // within reach; her interest in his replies so far (reward prediction error) carries it too.
    const float k = std::clamp(context.known, 0.0f, 1.0f);
    p *= (0.85f + 1.6f * k * (1.0f - k)) * (0.8f + 0.4f * self.interest);
    // What the lead may bring back, in order: how something in his life went (it is over now), a
    // question he left unanswered (her character decides whether she comes back), a question she
    // set aside, a thread he opened that went nowhere.
    if (!down) {
        for (const auto& f : self.follow_ups)
            if (f.state == 0 && context.now >= f.due && context.now <= f.due + 4 * day) {
                out.carry = CuriousCarry::follow_up; out.carry_id = f.id; out.carried = f.what;
                p += 0.35f;
                break;
            }
        if (out.carry == CuriousCarry::none) {
            float persistence = 0.3f;
            switch (context.persona) {
                case Persona::yandere: persistence = 0.8f; break;
                case Persona::tsundere: case Persona::himedere: persistence = 0.6f; break;
                case Persona::kichidere: persistence = 0.5f; break;
                case Persona::genki: case Persona::kanedere: persistence = 0.4f; break;
                case Persona::deredere: persistence = 0.25f; break;
                case Persona::kuudere: persistence = 0.15f; break;
                case Persona::dandere: persistence = 0.1f; break;
            }
            const float second_roll = static_cast<float>((seed >> 17) % 1000) / 1000.0f;
            for (auto it = self.curious.rbegin(); it != self.curious.rend(); ++it)
                if (it->outcome == 0 && !it->returned && it->move != CuriousMove::reflect && !it->about.empty() &&
                    context.now >= it->settled + 10 * 60 && context.now <= it->settled + day) {
                    if (second_roll < persistence) { out.carry = CuriousCarry::returning; out.carry_id = it->id; out.carried = it->about; p += 0.2f; }
                    break;
                }
        }
        if (out.carry == CuriousCarry::none)
            for (const auto& c : self.curious)
                if (c.deferred() && context.now >= c.later_at + 20 * 60 && context.now <= c.later_at + 3 * day) {
                    out.carry = CuriousCarry::question; out.carry_id = c.id; out.carried = c.about;
                    p += 0.25f;
                    break;
                }
        if (out.carry == CuriousCarry::none && quick.closed)
            for (const auto& t : self.threads)
                if (!t.used && context.now >= t.at + 3 * 60 && context.now <= t.at + day) {
                    out.carry = CuriousCarry::thread; out.carried = t.text;
                    p += 0.15f;
                    break;
                }
    }
    p *= persona * tired;
    if (self.curious_gap < 3) p *= 0.7f; // She asked a little while ago.
    if (roll >= std::min(p, 0.85f)) {
        Urge none;
        none.at = context.now;
        return none;
    }
    out.move = CuriousMove::lead;
    return out;
}

void onPlayerMessage(Self& self, const Context& context, const Quick& quick, std::wstring_view message) {
    self.affect = settled(self.affect, context);
    self.affect.valence = std::clamp(self.affect.valence + quick.valence, -1.0f, 1.0f);
    self.affect.arousal = clamp01(self.affect.arousal + quick.arousal);
    self.affect.at = context.now;
    self.drives.bond = clamp01(self.drives.bond - 0.06f);
    self.last_talk = context.now;
    self.respect_delta += quick.respect_delta;
    if (quick.dismissive) {
        self.subtext_note = context.english
            ? L"You just opened up emotionally and ‹he|she|they› brushed you off with a cold one-word reply — you feel a sting of hurt and pull your guard back up."
            : L"Właśnie się przed ‹nim|nią› otworzyłaś, a ‹on zbył|ona zbyła› cię jednym chłodnym słowem — poczułaś ukłucie zawodu i znów chowasz się za pancerzem.";
        self.subtext_at = context.now;
        self.trust_delta -= 3;
    } else if (quick.confession) {
        // Co-regulation: when the player opens up or is distressed, even energetic personas calm down and ground them.
        if (context.trust >= 45) self.affect.arousal = std::min(self.affect.arousal, 0.35f);
        self.subtext_note = context.english
            ? L"‹He|She|They› just poured out a long, personal message — co-regulate: lower your energy, put teasing and noise aside, listen closely and answer with calm, genuine care."
            : L"‹On właśnie napisał|Ona właśnie napisała› długą, osobistą wiadomość — wycisz swoje pobudzenie (ko-regulacja), odłóż zaczepki i hałas na bok, wysłuchaj ‹go|jej› uważnie i odpowiedz ze spokojną, szczerą troską.";
        self.subtext_at = context.now;
        self.trust_delta += 3;
        self.security_delta += 2;
    } else if (quick.sad_or_stressed && context.trust >= 45) {
        self.affect.arousal = std::min(self.affect.arousal, 0.35f);
        self.subtext_note = context.english
            ? L"‹He sounds|She sounds|They sound› down or exhausted — instead of mirroring stress or staying hyper, calm your tone down and be a quiet, supportive anchor for ‹him|her|them›."
            : L"‹On brzmi na przygaszonego lub zmęczonego|Ona brzmi na przygaszoną lub zmęczoną› — zamiast nakręcać tempo albo żartować na siłę, wyhamuj swoje emocje i bądź dla ‹niego|niej› spokojnym oparciem.";
        self.subtext_at = context.now;
    } else if (quick.irony_or_vent) {
        self.subtext_note = context.english
            ? L"‹He|She|They› used bitter sarcasm/irony because ‹he is|she is|they are› frustrated (e.g. a crash, bug or loss) — do NOT take words like 'great/super' literally! Catch the irony and respond with understanding or dry, supportive humor."
            : L"‹On użył|Ona użyła› gorzkiej ironii/sarkazmu, bo coś poszło nie tak (np. crash, błąd albo porażka) — NIE bierz słów „super/świetnie” dosłownie! Wyczuj ironię i zareaguj ze zrozumieniem albo lekkim, wspierającym humorem.";
        self.subtext_at = context.now;
    } else if (quick.skeptical_irony) {
        self.subtext_note = context.english
            ? L"‹He|She|They› replied with playful, skeptical irony — read the subtext between the lines and banter back in your style."
            : L"‹On odpowiedział|Ona odpowiedziała› z przekorną ironią i niedowierzaniem — odczytaj podtekst między wierszami i odbij piłeczkę w swoim stylu.";
        self.subtext_at = context.now;
    }
    // Item 6: Panksepp's subcortical PLAY circuit — gated off immediately by sadness, dismissal, frustration or negative affect.
    if (quick.sad_or_stressed || quick.dismissive || quick.irony_or_vent || quick.valence < -0.1f) {
        self.play_momentum = 0.0f;
    } else if (quick.playful) {
        self.play_momentum = clamp01(self.play_momentum + 0.28f);
    }
    // Item 4: Berne's "Why Don't You — Yes, But" counter.
    if (quick.accepted_pick) {
        self.rejected_picks_in_row = 0;
    } else if (quick.rejected_pick) {
        const bool had_suggestions = self.policy.last_style == "games" ||
                                     std::any_of(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.outcome == -1 && e.suggestion; });
        if (had_suggestions || self.rejected_picks_in_row > 0) ++self.rejected_picks_in_row;
    }
    // Item 7: Sapolsky's sleep debt — staying up talking during her night or late hours accumulates sleep debt for tomorrow.
    if (asleep(context)) {
        self.sleep_debt = clamp01(self.sleep_debt + 0.09f);
    } else if (context.character) {
        if (const auto clk = localClock(*context.character, context.now)) {
            const int h = clk->hour;
            if ((context.character->night_owl && h >= 2 && h < 6) || (!context.character->night_owl && (h >= 23 || h < 5)))
                self.sleep_debt = clamp01(self.sleep_debt + 0.05f);
        }
    }
    // Policy feedback is applied from the validated semantic appraisal, not message length.
    if (!message.empty()) {
        // Reinforce concepts taught by the player when mentioned again.
        const auto folded = game_index::fold(message);
        for (auto& c : self.learned_concepts) {
            const auto t = game_index::fold(c.term);
            if (t.size() >= 3 && folded.find(t) != std::wstring::npos) {
                c.mastery = clamp01(c.mastery + 0.18f);
                c.at = context.now;
            }
        }
        noticePeekedLeak(self, message, context.now);
    }
    for (auto& e : self.expectations)
        if (e.presence && e.outcome == -1) { e.outcome = 1; e.settled = context.now; }
    // His answer to what she asked last: he explained / took it up (her curiosity eases), or let it
    // pass (a carried-on talk brushed off stings a little; any real answer to her question counts).
    for (auto it = self.curious.rbegin(); it != self.curious.rend(); ++it) {
        if (it->outcome != -1 || it->at == 0) continue;
        const bool passed = quick.dismissive || (it->move != CuriousMove::clarify && quick.closed);
        it->outcome = passed ? 0 : 1;
        it->settled = context.now;
        if (!passed) self.drives.curiosity = clamp01(self.drives.curiosity - 0.03f);
        else if (it->move == CuriousMove::lead) self.affect.valence = std::clamp(self.affect.valence - 0.03f, -1.0f, 1.0f);
        break;
    }
    for (auto& c : self.curious) // Older ones he never got to.
        if (c.outcome == -1 && c.at > 0) { c.outcome = 0; c.settled = context.now; }
    const auto attachment = attachmentOf(context);
    // Her bid for connection, answered (Gottman): toward, away or against; it fades over a week.
    fadeBids(self.bids, context.now);
    if (self.bids.open) {
        self.bids.open = false;
        if (quick.hostile) {
            self.bids.against += 1;
            self.security_delta -= 2;
        } else if (quick.dismissive || quick.closed) {
            self.bids.away += 1;
            if (attachment == Attachment::anxious || attachment == Attachment::fearful) self.security_delta -= 1; // It stings more.
        } else self.bids.toward += 1;
    }
    if (quick.bid) ++self.bids.his;
    // How he writes (accommodation), averaged over his messages (a third each new one).
    if (!message.empty() && quick.words > 0) {
        auto& s = self.his_style;
        bool letters = false, upper = false, diacritics = false;
        for (const wchar_t ch : message) {
            if (!std::iswalpha(ch)) continue;
            letters = true;
            if (std::iswupper(ch)) upper = true;
            if (std::wstring_view(L"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ").find(ch) != std::wstring_view::npos) diacritics = true;
        }
        const auto folded = spaced(message);
        const float laugh = hasAny(folded, {L"xd", L"xdd", L"xddd", L"haha", L"hahaha", L"hehe", L"lol", L"lmao"}) || hasRaw(message, {L"😂", L"🤣"}) ? 1.0f : 0.0f;
        const float a = s.samples < 3 ? 1.0f / static_cast<float>(s.samples + 1) : 0.33f;
        const auto mix = [&](float& value, float sample) { value += a * (sample - value); };
        mix(s.words, static_cast<float>(quick.words));
        mix(s.exclaim, message.find(L'!') != std::wstring_view::npos ? 1.0f : 0.0f);
        if (letters) {
            mix(s.lower, upper ? 0.0f : 1.0f);
            // No Polish letters in a Polish chat: he writes "bez ogonków" (a word long enough to need one).
            if (!context.english && quick.words >= 4) mix(s.plain, diacritics ? 0.0f : 1.0f);
        }
        mix(s.laughs, laugh);
        s.samples = std::min(s.samples + 1, 1000);
    }
    // The topic of the moment: his content words; a message sharing one stays on it.
    if (!message.empty()) {
        auto words = chats::searchWords(message, 8);
        if (!words.empty()) {
            size_t shared = 0;
            chats::overlap(words, self.topic.words, &shared);
            if (shared > 0 && !self.topic.words.empty()) {
                ++self.topic.turns;
                for (auto& w : words)
                    if (self.topic.words.size() < 12 && std::none_of(self.topic.words.begin(), self.topic.words.end(), [&](const std::wstring& t) { return t == w; }))
                        self.topic.words.push_back(std::move(w));
            } else self.topic = Topic{std::move(words), 1};
        }
    }
    // Her interest in what he writes (reward prediction error, the "dopamine" of a chat): a reply
    // richer than she expected intrigues her (and makes her more curious); a flat one lets her down a little.
    {
        float got = 0.15f + 0.03f * static_cast<float>(std::min(quick.words, 20));
        if (quick.hook) got += 0.2f;
        if (quick.asks) got += 0.12f;
        if (quick.playful) got += 0.1f;
        if (quick.bid) got += 0.1f;
        if (self.topic.turns == 1 && quick.words >= 4) got += 0.1f; // Something new.
        if (quick.closed) got -= 0.25f;
        if (quick.dismissive || quick.hostile) got -= 0.3f;
        got = clamp01(got);
        self.interest_rpe = got - self.interest;
        self.interest = clamp01(self.interest + 0.3f * self.interest_rpe);
        self.interest_at = context.now;
        if (self.interest_rpe > 0.2f) {
            self.drives.curiosity = clamp01(self.drives.curiosity + 0.04f);
            self.affect.valence = std::clamp(self.affect.valence + 0.04f, -1.0f, 1.0f);
            self.affect.arousal = clamp01(self.affect.arousal + 0.04f);
        } else if (self.interest_rpe < -0.25f) self.affect.valence = std::clamp(self.affect.valence - 0.02f, -1.0f, 1.0f);
    }
    // He opened up about himself; right after she did (or she does next): a reciprocal disclosure —
    // closeness and calm (the hyperpersonal effect; trust over the fear of a stranger).
    if (quick.confession || (quick.hook && quick.words >= 10) || quick.sad_or_stressed) {
        self.his_open_at = context.now;
        if (self.last_reply_vulnerable && context.now >= self.bonded_at + hour) {
            self.bonded_at = context.now;
            self.trust_delta += 2;
            self.security_delta += 2;
            self.affect.arousal = std::min(self.affect.arousal, 0.4f);
        }
    }
    // Something coming up in his life that he brings up himself after it: no need to ask.
    if (!message.empty())
        for (auto& f : self.follow_ups) {
            if (f.state != 0 || context.now + 2 * hour < f.due) continue;
            const auto words = chats::searchWords(f.what);
            if (!words.empty() && chats::matches(words, message) >= std::min(2, static_cast<int>(words.size()))) { f.state = 3; f.settled = context.now; }
        }
}

void onReply(Self& self, const Context& context, bool about_games, std::wstring_view reply_text, bool vulnerable) {
    self.drives.energy = clamp01(self.drives.energy - 0.025f);
    self.drives.autonomy = clamp01(self.drives.autonomy + (about_games ? 0.04f : -0.02f));
    self.last_talk = context.now;
    self.last_reply_vulnerable = vulnerable;
    self.peek_caught = false;
    if (about_games) self.policy.last_style = "games";
    else if (vulnerable) self.policy.last_style = "warmth";
    else if (!reply_text.empty() && reply_text.find(L'?') != std::wstring_view::npos) self.policy.last_style = "deep";
    else if (self.drives.autonomy < 0.35f) self.policy.last_style = "life";
    else self.policy.last_style = "teasing";
    if (!reply_text.empty()) {
        const auto folded = game_index::fold(reply_text);
        for (auto& c : self.learned_concepts) {
            const auto t = game_index::fold(c.term);
            if (t.size() >= 3 && folded.find(t) != std::wstring::npos) c.mastery = clamp01(c.mastery + 0.22f);
        }
        noticeConfessed(self, reply_text, context.now);
        const bool apologized = noticeApology(self, reply_text, context.now);
        // Affective realism friction: if she answered while irritable, burnt out or exhausted (and not
        // warmly/vulnerably or apologizing), she remembers later that she was harsh on the player.
        if (!apologized && !vulnerable) {
            const auto cur = settled(self.affect, context);
            const float energy = energyNow(self, context);
            if (cur.valence <= -0.18f || (energy < 0.32f && cur.valence < 0.0f) ||
                context.life.work == LifeWork::burnout || context.life.family == LifeFamily::tension) {
                self.harsh_at = context.now;
                if (context.life.work == LifeWork::burnout)
                    self.harsh_reason = context.english ? L"burnout and stress at work" : L"wypalenie i stres w pracy";
                else if (context.life.family == LifeFamily::tension)
                    self.harsh_reason = context.english ? L"tension with your family" : L"spięcie z rodziną";
                else if (energy < 0.32f)
                    self.harsh_reason = context.english ? L"exhaustion and lack of sleep" : L"zmęczenie i niewyspanie";
                else
                    self.harsh_reason = context.english ? L"your own bad mood" : L"własny zły humor";
            }
        }
    }
    const uint64_t now = context.now;
    // Her reply reaches out (a question, something of her own): his next message answers the bid.
    self.bids.open = !reply_text.empty() && (reply_text.find(L'?') != std::wstring_view::npos || vulnerable);
    if (vulnerable) {
        self.opened_at = now;
        // She opens up right after he did: a reciprocal disclosure (closeness, calm).
        if (self.his_open_at && now <= self.his_open_at + 30 * 60 && now >= self.bonded_at + hour) {
            self.bonded_at = now;
            self.trust_delta += 2;
            self.security_delta += 2;
            self.affect.arousal = std::min(self.affect.arousal, 0.4f);
        }
    }
    self.retold = false;
    if (!reply_text.empty()) {
        // Something coming up in his life: she asked how it went (after it) or wished him luck (before).
        for (auto& f : self.follow_ups) {
            if (f.state != 0) continue;
            const auto words = chats::searchWords(f.what);
            if (words.empty() || chats::matches(words, reply_text) < std::min(2, static_cast<int>(words.size()))) continue;
            if (now + 2 * hour >= f.due) { f.state = 1; f.settled = now; }
            else f.wished = true;
        }
        // A thread of his she took up after all.
        for (auto& t : self.threads)
            if (!t.used && chats::matches(chats::searchWords(t.text), reply_text) >= 2) t.used = true;
        // Second-order theory of mind: she told him this before (a day or more ago) and says it again as news.
        const auto said = chats::searchWords(reply_text, 32);
        const auto folded = spaced(reply_text);
        const bool callback = hasAny(folded, {L"mowilam", L"wspominalam", L"opowiadalam", L"pamietasz", L"jak ci mowilam", L"told you", L"mentioned", L"remember"});
        if (!callback && said.size() >= 4)
            for (const auto& claim : self.claims) {
                if (claim.at + day > now) continue;
                size_t shared = 0;
                const float part = chats::overlap(chats::searchWords(claim.text, 16), said, &shared);
                if (shared >= 3 && part >= 0.6f) { self.retold = true; break; }
            }
    }
    // Her curiosity in this reply, as her words show it: what her appraisal says she does, or —
    // when it said nothing — her urge, counts only when she really asks (a reflection needs no
    // question). An appraisal that only claims curiosity is not an act (and does not hold back the next).
    const auto question = lastQuestion(reply_text);
    bool acted = false;
    const auto pending = std::exchange(self.pending_curious, {});
    if (pending.move != CuriousMove::none && (pending.move == CuriousMove::reflect ? !reply_text.empty() : !question.empty())) {
        const auto said = pending.move == CuriousMove::reflect ? (!pending.about.empty() ? pending.about : firstSentence(reply_text)) : question;
        askNow(self, pending.move, said, pending.why, now, context.english);
        acted = true;
    } else if (self.urge.move != CuriousMove::none && (!question.empty() || (self.urge.move == CuriousMove::reflect && !reply_text.empty()))) {
        askNow(self, self.urge.move, question.empty() ? firstSentence(reply_text) : question, self.urge.why, now, context.english);
        acted = true;
    }
    keepCurious(self);
    self.curious_gap = acted ? 0 : std::min(self.curious_gap + 1, 99);
    self.urge = {};
}

void onSuggested(Self& self, const Context& context, const std::vector<std::pair<std::string, std::wstring>>& games) {
    for (const auto& [key, title] : games) {
        if (key.empty() || title.empty()) continue;
        if (std::any_of(self.expectations.begin(), self.expectations.end(), [&](const Expectation& e) { return e.outcome == -1 && e.game == key; })) continue;
        Expectation e;
        e.id = newId(self, 'o');
        e.text = context.english ? L"‹he|she|they› will play [[" + title + L"]], which you suggested" : L"zagra w [[" + title + L"]], którą ‹mu|jej› poleciłaś";
        e.game = key;
        e.title = title;
        e.p = 0.35f;
        e.made = context.now;
        e.due = context.now + 3 * day;
        e.suggestion = true;
        self.expectations.push_back(std::move(e));
        // At most three of her suggestions waiting at once: the oldest gives way.
        std::vector<size_t> waiting;
        for (size_t i = 0; i < self.expectations.size(); ++i)
            if (self.expectations[i].outcome == -1 && self.expectations[i].suggestion) waiting.push_back(i);
        if (waiting.size() > 3) self.expectations.erase(self.expectations.begin() + static_cast<std::ptrdiff_t>(waiting.front()));
    }
}

void onPlayed(Self& self, const Context& context, std::string_view key, std::wstring_view title, uint32_t minutes) {
    if (minutes >= 5) self.rejected_picks_in_row = 0;
    if (minutes >= 10) {
        for (auto& e : self.expectations) {
            if (e.outcome != -1 || e.game.empty() || e.game != key) continue;
            e.outcome = 1;
            e.settled = context.now;
            if (e.suggestion) {
                self.drives.pride = clamp01(self.drives.pride + 0.15f);
                self.affect = settled(self.affect, context);
                self.affect.valence = std::clamp(self.affect.valence + 0.1f, -1.0f, 1.0f);
                self.affect.at = context.now;
                if (auto* marker = markerOf(self, title.empty() ? std::wstring_view(e.title) : title)) {
                    marker->valence = std::clamp(marker->valence + 0.25f, -1.0f, 1.0f);
                    marker->at = context.now;
                }
            }
        }
    }
    if (minutes >= 30 && !title.empty())
        if (auto* marker = markerOf(self, title)) { marker->valence = std::clamp(marker->valence + 0.05f, -1.0f, 1.0f); marker->at = context.now; }
}

void onConversationEnd(Self& self, const Context& context, int streak) {
    if (streak < 2) return;
    if (std::any_of(self.expectations.begin(), self.expectations.end(), [](const Expectation& e) { return e.presence && e.outcome == -1; })) return;
    Expectation e;
    e.id = newId(self, 'o');
    e.text = context.english ? L"‹he|she|they› will come back to talk tomorrow" : L"jutro znów do ciebie zajrzy";
    e.p = std::min(0.9f, 0.45f + 0.1f * static_cast<float>(streak));
    e.made = context.now;
    e.due = context.now + 40 * hour;
    e.presence = true;
    self.expectations.push_back(std::move(e));
}
} // namespace companion::mind

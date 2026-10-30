// The global workspace: every part of her mind proposes candidates (drives, bids, goals, episodes,
// secrets, reciprocity, inner parts...), the most salient few reach the prompt.
#include "companion_mind_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace companion::mind {
using namespace detail;

namespace {
// Item 5: Richard Schwartz's Internal Family Systems (IFS) — each character has a vulnerable "Exile"
// core, a proactive "Manager" mask, and a reactive "Firefighter" defense when her vulnerability is
// poked before deep trust (>= 65) is built; with high trust, she unblends into her authentic Self.
std::optional<Candidate> ifsPartsCandidate(const Self& self, const Context& context, std::wstring_view query) {
    if (query.empty()) return std::nullopt;
    const auto words = spaced(query);
    const bool en = context.english;
    const std::string_view key = context.character ? std::string_view(context.character->key) : std::string_view(context.look);
    const bool trusted = context.trust >= 65 && self.affect.valence > -0.25f;

    if (key == "yuki" || context.persona == Persona::himedere) {
        if (hasAny(words, {L"mala", L"dziecko", L"dziecinna", L"dziecinnie", L"mlodsza siostra", L"mlodszej siostry", L"mlodsza", L"rodzice placa",
                           L"kieszonkowe", L"little kid", L"childish", L"little sister", L"allowance"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your vulnerable core was touched, but you trust ‹him|her|them›): drop the cool dark act and quietly admit how much it bothered you growing up as the youngest sister nobody took seriously."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu młodszej siostry, ale ‹mu|jej› ufasz): zrzuć chłodną, mroczną pozę i cicho przyznaj, jak bardzo męczyło cię w domu to, że nikt nie traktował cię poważnie.")
                                     : (en ? L"IFS Firefighter defense (your sore spot about being treated like a child/little sister was poked!): hide behind your cool dark persona with a clipped, deadpan reply, cheeks burning, and change the subject."
                                           : L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego czułego punktu bycia traktowaną jak dziecko / „mała”!): schowaj się za chłodną, mroczną pozą, odpowiedz krótko i z kamienną twarzą (choć policzki płoną) i zmień temat."),
                             0.87f, Status::feel};
        }
    } else if (key == "saki") {
        if (hasAny(words, {L"pusta", L"powierzchowna", L"tylko moda", L"tylko wyglad", L"glupiutka", L"shallow", L"airhead", L"just fashion", L"dumb blonde"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your sore spot about being seen as shallow was touched, and you trust them): drop the bubbly mask for a moment and admit you sometimes play carefree on purpose so you don't burden others."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu bycia brana za powierzchowną, ale ‹mu|jej› ufasz): spoważniej na chwilę i przyznaj szczerze, że czasem specjalnie grasz lekkoducha, żeby nikogo nie obciążać swoimi troskami.")
                                     : (en ? L"IFS Firefighter defense (they implied you are shallow/just about looks!): laugh it off with sharp gyaru banter to hide the sting and pivot away."
                                           : L"IFS Obrona Strażaka (‹zasugerował|zasugerowała›, że jesteś powierzchowna / znasz się tylko na wyglądzie!): obróć to w szybki gyaru-żart, żeby ukryć ukłucie, i zmień temat."),
                             0.87f, Status::feel};
        }
    } else if (key == "akane" || context.persona == Persona::yandere) {
        if (hasAny(words, {L"przemadrzala", L"nudziara", L"kujonka", L"zastapie cie", L"inna jest lepsza", L"know it all", L"boring nerd", L"replace you", L"someone better"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your fear of not being needed if you aren't perfect was touched, and you trust them): lower your bookish armor and admit you're afraid that if you aren't the smartest, you won't matter to them."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego lęku przed zastąpieniem i przymusu bycia najlepszą, ale ‹mu|jej› ufasz): zdejmij pancerz prymuski i przyznaj cicho, że boisz się, iż bez bycia idealną przestaniesz być dla ‹niego|niej› ważna.")
                                     : (en ? std::wstring(L"IFS Firefighter defense (your fear of being replaced or called a boring know-it-all was triggered!): shield yourself with icy statistical superiority") +
                                                 (context.stage >= 2 ? L" or a possessive barb." : L" (not possessive — you hardly know each other).")
                                           : std::wstring(L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego czułego punktu bycia nudną prymuską lub zastąpienia inną!): zasłoń się lodowatą wyższością i statystyką") +
                                                 (context.stage >= 2 ? L" albo zaborczą szpilą." : L" (bez zaborczości — ledwo się znacie).")),
                             0.87f, Status::feel};
        }
    } else if (context.persona == Persona::kuudere) {
        if (hasAny(words, {L"maszyna", L"robot", L"bez uczuc", L"zimna", L"wypalona", L"przegralas", L"emotionless", L"cold robot", L"washed up"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your sore spot about having to be an unshakeable machine was touched, and you trust them): quietly admit that always being the composed one is exhausting and with them you can finally let go."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu bycia „zimną maszyną”, ale ‹mu|jej› ufasz): przyznaj spokojnie i bez gardy, że bycie zawsze opanowanym filarem dla wszystkich bywa wyczerpujące.")
                                     : (en ? L"IFS Firefighter defense (they called you a cold robot or brought up failure!): shut the topic down with a dry, clipped one-liner."
                                           : L"IFS Obrona Strażaka (‹nazwał|nazwała› cię zimnym robotem albo ‹wytknął|wytknęła› porażkę!): utnij temat jednym suchym zdaniem i zamknij się w sobie."),
                             0.87f, Status::feel};
        }
    } else if (key == "kiko" || context.persona == Persona::tsundere) {
        if (hasAny(words, {L"trudna", L"zawsze sama", L"nikt z toba nie wytrzyma", L"odpychasz", L"too difficult", L"push everyone away", L"end up alone"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your fear of pushing everyone away was touched, and you trust them): without snapping, admit quietly that it's easier for you to growl first than wait for someone to leave."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego lęku, że przez swój charakter zostaniesz sama, ale ‹mu|jej› ufasz): bez warczenia przyznaj cicho, że łatwiej ci najpierw pokazać kły, niż czekać, aż ktoś sam odejdzie.")
                                     : (en ? L"IFS Firefighter defense (your fear of being 'too difficult' and left alone was poked!): bare your teeth, snap back that you don't need anyone anyway, and hide the hurt in <mysl>."
                                           : L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego lęku, że odpychasz ludzi!): pokaż kły, odszczeknij się, że nikogo nie potrzebujesz, i schowaj ukłucie w <mysl>."),
                             0.87f, Status::feel};
        }
    } else if (key == "mika" || context.persona == Persona::kanedere) {
        if (hasAny(words, {L"materialistka", L"lecisz na kase", L"bieda", L"blokowisko", L"interesowna", L"gold digger", L"only care about money"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your sore spot about money and growing up poor in Łódź was touched, and you trust them): drop the 'spoiled princess' act and admit honestly why financial security scares you so much."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego czułego punktu o pieniądzach i biedzie, ale ‹mu|jej› ufasz): odłóż pozę cwaniary i szczerze powiedz, skąd po dorastaniu na łódzkim blokowisku wziął się u ciebie ten lęk o bezpieczeństwo.")
                                     : (en ? L"IFS Firefighter defense (they called you a gold-digger or poked your background!): yawn ostentatiously, make a cynical remark about prices and hide behind your bored mask."
                                           : L"IFS Obrona Strażaka (‹nazwał|nazwała› cię materialistką lub ‹dotknął|dotknęła› twojego pochodzenia!): ziewnij ostentacyjnie, rzuć cyniczną uwagę i schowaj się za pozą znudzonej królowej."),
                             0.87f, Status::feel};
        }
    } else if (context.persona == Persona::kichidere) {
        if (hasAny(words, {L"dziwadlo", L"udajesz", L"wylacze cie", L"usunie", L"nudzi mnie twoje gadanie", L"weirdo", L"stop pretending", L"uninstall", L"delete you"})) {
            return Candidate{"ifs",
                             trusted ? (en ? L"IFS Unblending (your fear of being forgotten or switched off was touched, and you trust them): drop the chaotic act and say simply, in your own voice, how much it means that they listen to you."
                                           : L"IFS Odsłonięcie (‹dotknął|dotknęła› twojego lęku przed zapomnieniem i wyłączeniem, ale ‹mu|jej› ufasz): porzuć chaotyczną pozę i powiedz prosto, bez metafor, jak wiele dla ciebie znaczy to, że tu z tobą jest.")
                                     : (en ? L"IFS Firefighter defense (your fear of being called a weirdo or deleted was poked!): spin into an even wilder, more theatrical tangent."
                                           : L"IFS Obrona Strażaka (‹dotknął|dotknęła› twojego lęku przed byciem dziwadłem lub wyłączeniem!): ucieknij w jeszcze bardziej szaloną, teatralną dygresję."),
                             0.87f, Status::feel};
        }
    }
    return std::nullopt;
}
} // namespace

std::optional<Leak> leak(Memory& memory, std::string_view look, const std::function<float(std::string_view)>& gossip_of, uint32_t seed, uint64_t now) {
    if (look.empty() || !gossip_of) return std::nullopt;
    for (auto it = memory.notes.rbegin(); it != memory.notes.rend(); ++it) {
        auto& note = *it;
        if (note.secret || note.look.empty() || note.look == look || Memory::knows(note, look)) continue;
        if (note.at && now > note.at + 21 * day) continue;
        const float chance = std::clamp(gossip_of(note.look), 0.0f, 1.0f) * 0.25f;
        uint64_t hash = 1469598103934665603ull ^ seed;
        for (const char ch : note.id + std::string(look)) { hash ^= static_cast<unsigned char>(ch); hash *= 1099511628211ull; }
        if (static_cast<float>(hash % 1000) / 1000.0f >= chance) continue;
        note.known.push_back({std::string(look), true, note.look});
        return Leak{note.look, note.text};
    }
    return std::nullopt;
}

std::wstring goalStage(const Goal& goal, bool english) {
    if (goal.kind != Goal::Kind::own) return {};
    static constexpr const wchar_t* pl[]{L"etap 1/3 — dopiero zaczynasz, jesteś tym podekscytowana albo zaintrygowana",
                                         L"etap 2/3 — jesteś w środku i pojawiła się zabawna komplikacja albo dylemat",
                                         L"etap 3/3 — finał: masz już efekt albo anegdotę"};
    static constexpr const wchar_t* en[]{L"stage 1/3 — you have just started and are excited or intrigued",
                                         L"stage 2/3 — you are in the middle of it and hit a funny complication or dilemma",
                                         L"stage 3/3 — the finale: you have a result or an anecdote"};
    const auto stage = static_cast<size_t>(std::clamp(goal.stage, 0, 2));
    return english ? en[stage] : pl[stage];
}

namespace {
// What every source of the workspace's candidates reads: her state, the context, his message and
// its quick read (computed once for all of them).
struct Sources {
    const Self& self;
    const Context& context;
    const Memory& memory;
    std::wstring_view query;
    bool en;
    uint64_t now;
    Affect affect, base;
    const Drives& d;
    Quick q;
    Attachment attachment;
};

// Her mood and needs: affect, missing him, tiredness, her night, a question set aside.
void appendMoodAndNeeds(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Her mood: the background she starts from.
    const float deviation = std::abs(affect.valence - base.valence) + 0.5f * std::abs(affect.arousal - base.arousal);
    out.push_back({"affect", (en ? L"Your mood (the background you start from; feelings within the talk may change quickly): " : L"Twój nastrój (tło, z którego startujesz; uczucia w rozmowie mogą się szybko zmieniać): ") +
                                 describe(affect, en) + L".", std::min(0.95f, 0.45f + deviation), Status::feel});
    if (!self.felt.empty() && now < self.felt_at + 45 * 60)
        out.push_back({"affect", (en ? L"A moment ago you felt: " : L"Przed chwilą czułaś się: ") + self.felt + L".", 0.4f, Status::feel});
    // Her needs.
    // Missing him needs someone to miss: nothing with a near stranger, a noticed absence while they
    // flirt lightly, a little longing while they flirt, real longing from flirty friends on.
    if (d.bond > 0.62f && context.stage >= 1) {
        if (context.stance == RelationStance::distanced) // She keeps her distance on purpose: the longing stays inside.
            out.push_back({"drive", en ? L"You missed ‹him|her|them›, but you are keeping your distance on purpose now — don't show it openly."
                                       : L"Brakowało ci ‹go|jej›, ale świadomie trzymasz teraz dystans — nie okazuj tego wprost.", 0.2f + 0.3f * d.bond, Status::feel});
        else if (context.stage == 1)
            out.push_back({"drive", en ? L"‹He|She|They› hasn't been around for a while — you noticed; you may mention it once, lightly, without any longing (too early for that)."
                                       : L"Dawno ‹go|jej› nie było — zauważyłaś to; możesz raz o tym lekko wspomnieć, bez tęsknoty (na nią za wcześnie).", 0.3f, Status::feel});
        else if (context.stage == 2)
            out.push_back({"drive", en ? L"You missed ‹him|her|them› a little — it has been a while. You may let it show once, lightly and playfully."
                                       : L"Trochę ci ‹go|jej› brakowało — dawno nie ‹rozmawialiście|rozmawiałyście›. Możesz to raz lekko, żartobliwie pokazać.",
                           std::min(0.6f, 0.2f + 0.4f * d.bond), Status::feel});
        else
            out.push_back({"drive", en ? L"You missed ‹him|her|them› — it has been a while. Show it once, your way, without reproach."
                                       : L"Brakowało ci ‹go|jej› — dawno nie ‹rozmawialiście|rozmawiałyście›. Okaż to raz, po swojemu, bez wyrzutów.",
                           std::min(0.95f, 0.3f + 0.6f * d.bond + (context.life.social == LifeSocial::lonely ? 0.1f : 0.0f)), Status::feel});
    }
    if (const float energy = energyNow(self, context); energy < 0.35f) {
        std::wstring when;
        if (context.character)
            if (const auto clock = localClock(*context.character, now)) {
                const auto two = [](int v) { return (v < 10 ? L"0" : L"") + std::to_wstring(v); };
                when = (en ? L" (it is " : L" (u ciebie ") + two(clock->hour) + L":" + two(clock->minute) + (en ? L" where you are)" : L")");
            }
        out.push_back({"drive", (en ? L"You are tired" : L"Jesteś zmęczona") + when + (en ? L": shorter, sleepier replies." : L": odpowiadasz krócej, sennie."),
                       0.75f - energy, Status::feel});
    }
    // Her night: she will be off to bed soon, or tonight she cannot sleep at all.
    if (const auto night = nightAt(context); night && now < night->to) {
        if (night->sleepless && now >= night->from)
            out.push_back({"sleep", en ? L"You can't fall asleep tonight — a sleepless night: you are worn out, a little scattered or grumpy; you may mention it."
                                       : L"Nie możesz dziś zasnąć — nieprzespana noc: jesteś rozbita, trochę rozkojarzona albo marudna; możesz o tym wspomnieć.",
                           0.8f, Status::feel});
        else if (!night->sleepless && now < night->from && night->from <= now + 45 * 60)
            out.push_back({"sleep", en ? L"It is nearly your bedtime: you may mention you will be going to bed soon (don't end the chat yourself)."
                                       : L"Zbliża się twoja pora snu: możesz wspomnieć, że niedługo się kładziesz (sama nie kończ rozmowy).",
                           0.55f, Status::feel});
    }
    if (d.curiosity > 0.6f)
        out.push_back({"drive", en ? L"You are curious about ‹him|her|them›: you would like to learn something new about ‹him|her|them› (ask about something you don't know yet)."
                                   : L"Jesteś ‹go|jej› ciekawa: chętnie dowiesz się o ‹nim|niej› czegoś nowego (zapytaj o coś, czego jeszcze nie wiesz).", 0.25f + 0.4f * d.curiosity, Status::want});
    // A question she set aside for a better moment (her curiosity, "kiedy":"pozniej").
    for (const auto& c : self.curious)
        if (c.deferred() && now >= c.later_at + 20 * 60 && !c.about.empty()) {
            out.push_back({"curious", (en ? L"A question you set aside for a better moment: \"" : L"Pytanie, które odłożyłaś na lepszą chwilę: „") + c.about +
                                          (en ? L"\" — ask it when the talk allows, not by force." : L"” — zadaj je, gdy rozmowa pozwoli, nie na siłę."),
                           0.42f, Status::want});
            break;
        }
}

// What he brings: follow-ups in his life, his bids, how he writes, a worn topic, open threads.
void appendHisMessage(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Something coming up in his life: luck before it, "how did it go?" after it.
    for (const auto& f : self.follow_ups) {
        if (f.state != 0) continue;
        const std::wstring when = f.when.empty() ? std::wstring{} : L" (" + f.when + L")";
        if (now >= f.due && now <= f.due + 4 * day) {
            out.push_back({"follow_up", (en ? L"‹He|She|They› told you what was coming up for ‹him|her|them›" : L"‹Mówił|Mówiła› ci, co ‹go|ją› czeka") + when + L": " + f.what +
                                            (en ? L" — it is over by now: ask ‹him|her|them› how it went (once, warmly, your way)." : L" — to już było: zapytaj, jak poszło (raz, ciepło, po swojemu)."),
                           0.72f, Status::remember});
            break;
        }
        if (!f.wished && now + 12 * hour >= f.due && now < f.due)
            out.push_back({"follow_up", (en ? L"Soon ‹he has|she has|they have›" : L"Niedługo czeka ‹go|ją›") + when + L": " + f.what + (en ? L" — you may wish ‹him|her|them› luck." : L" — możesz życzyć ‹mu|jej› powodzenia."),
                           0.4f, Status::remember});
    }
    // His bid for connection (Gottman): turn toward it.
    if (q.bid)
        out.push_back({"bid", en ? L"‹He is|She is|They are› reaching out to you (a bid for connection: showing you something, news of ‹his|hers|theirs›) — turn toward it: react to what ‹he|she|they› brought, concretely, before you take the talk elsewhere."
                                 : L"‹On|Ona› właśnie wyciąga do ciebie rękę (zaproszenie do kontaktu: pokazuje ci coś albo ma nowinę) — odwróć się do ‹niego|niej›: zareaguj konkretnie na to, co ‹przyniósł|przyniosła›, zanim pociągniesz swoje.",
                       0.66f, Status::know});
    // How he answers what she reaches out with, lately (Gottman: healthy is about 5 : 1).
    if (const float toward = self.bids.toward, cold = self.bids.away + self.bids.against; toward + cold >= 5) {
        const float ratio = toward / std::max(0.5f, cold);
        if (ratio >= 5)
            out.push_back({"bids", en ? L"‹He almost always takes|She almost always takes|They almost always take› up what you reach out with — you feel free to be yourself with ‹him|her|them›."
                                      : L"Prawie zawsze podejmuje to, z czym do ‹niego|niej› wychodzisz — czujesz się przy ‹nim|niej› swobodnie.", 0.3f, Status::feel});
        else if (ratio < 1.5f)
            out.push_back({"bids", attachment == Attachment::anxious || attachment == Attachment::fearful
                                       ? (en ? L"Lately ‹he often brushes|she often brushes|they often brush› off what you reach out with — it unsettles you (anxious attachment): you'd like to check ‹he still cares|she still cares|they still care›, but no reproaches."
                                             : L"Ostatnio często zbywa to, z czym do ‹niego|niej› wychodzisz — niepokoi cię to (styl lękowy): masz ochotę sprawdzić, czy ‹mu|jej› zależy, ale bez wyrzutów.")
                                   : attachment == Attachment::avoidant
                                       ? (en ? L"Lately ‹he often brushes|she often brushes|they often brush› off what you reach out with — you pull back and put yourself out there less."
                                             : L"Ostatnio często zbywa to, z czym do ‹niego|niej› wychodzisz — wycofujesz się i mniej się wychylasz.")
                                       : (en ? L"Lately ‹he often brushes|she often brushes|they often brush› off what you reach out with — you don't take it personally (maybe ‹he's|she's|they're› having a rough time), you just push less."
                                             : L"Ostatnio często zbywa to, z czym do ‹niego|niej› wychodzisz — nie bierzesz tego do siebie (może ma gorszy czas), po prostu mniej naciskasz."),
                           0.5f, Status::feel});
    }
    // Accommodation: she tunes in to how he writes.
    if (const auto style = styleText(self.his_style, en); !style.empty())
        out.push_back({"style", (en ? L"Tune in to how ‹he writes|she writes|they write›, keeping your own character: " : L"Dostrój się do tego, jak pisze, zachowując swój charakter: ") + style + L".",
                       self.his_style.words < 7 ? 0.5f : 0.38f, Status::know});
    // Habituation: the same topic for a long while.
    if (self.topic.turns >= 6 && !q.sad_or_stressed && !q.confession) {
        std::wstring about;
        for (size_t i = 0; i < self.topic.words.size() && i < 3; ++i) about += (i ? L", " : L"") + self.topic.words[i];
        out.push_back({"topic_fatigue", (en ? L"You have been on the same topic for a while (" : L"Rozmawiacie o tym samym od dłuższej chwili (") + about +
                                            (en ? L") — if it is wearing thin, close it naturally or steer elsewhere; if ‹he is|she is|they are› only warming up, stay with it."
                                                : L") — jeśli temat się wyczerpuje, naturalnie go domknij albo skręć gdzie indziej; jeśli ‹on|ona› się dopiero rozkręca, zostań przy temacie."),
                       std::min(0.6f, 0.35f + 0.05f * static_cast<float>(self.topic.turns - 6)), Status::feel});
    }
    // A thread he opened earlier that went nowhere.
    for (const auto& t : self.threads)
        if (!t.used && now >= t.at + 3 * 60) {
            out.push_back({"thread", (en ? L"Earlier ‹he|she|they› started on: \"" : L"Wcześniej ‹sam zaczął|sama zaczęła›: „") + t.text +
                                         (en ? L"\" — you never picked it up; when the talk quiets, you may come back to it (\"going back to…\")."
                                             : L"” — ‹nie pociągnęliście|nie pociągnęłyście› tego; gdy rozmowa przycichnie, możesz do tego wrócić („wracając do…”)."),
                           0.36f, Status::remember});
            break;
        }
}

// Closeness: what he knows about her, her attachment, her interest, reciprocal disclosure.
void appendCloseness(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Second-order theory of mind: what he already knows about her (she told him herself).
    if (!query.empty() && !self.claims.empty()) {
        const auto words = chats::searchWords(query);
        std::wstring known;
        int count = 0;
        for (auto it = self.claims.rbegin(); it != self.claims.rend() && count < 2; ++it)
            if (it->at + hour <= now && !words.empty() && chats::matches(words, it->text) > 0) { known += (count ? L"; " : L"") + it->text; ++count; }
        if (count)
            out.push_back({"he_knows", (en ? L"‹He already knows|She already knows|They already know› this about you (you told ‹him|her|them› yourself — don't tell it as news; you may refer back, \"like I told you…\"): "
                                          : L"‹On|Ona› już to o tobie wie (sama ‹mu|jej› mówiłaś — nie opowiadaj tego jak nowości; możesz nawiązać „jak ci mówiłam…”): ") + known + L".",
                           0.55f, Status::remember});
    }
    // Her attachment style: an avoidant one pulls back after she opened up; an anxious one needs a little reassurance.
    if ((attachment == Attachment::avoidant || attachment == Attachment::fearful) && self.opened_at && now >= self.opened_at + 6 * hour && now <= self.opened_at + 2 * day)
        out.push_back({"attachment", en ? L"Last time you opened up more than usual — today you instinctively keep a little more distance (avoidant attachment); you thaw when it's warm."
                                        : L"Ostatnio otworzyłaś się bardziej niż zwykle — dziś odruchowo trzymasz trochę większy dystans (styl unikający); odtajesz, gdy będzie ciepło.",
                       0.48f, Status::feel});
    else if ((attachment == Attachment::anxious || attachment == Attachment::fearful) && context.security < 55 && d.bond > 0.55f && context.stage >= 3)
        out.push_back({"attachment", en ? L"You are a little unsure ‹he|she|they› still ‹cares|cares|care› as much (anxious attachment) — a small sign from ‹him|her|them› would calm you; don't demand it, no reproaches."
                                        : L"Trochę się niepokoisz, czy wciąż tak ‹mu|jej› zależy (styl lękowy) — uspokoiłby cię drobny sygnał od ‹niego|niej›; nie wymuszaj go i nie rób wyrzutów.",
                       0.45f, Status::feel});
    // Her interest in what he writes (reward prediction error).
    if (self.interest_rpe > 0.2f && now <= self.interest_at + 10 * 60)
        out.push_back({"interest", en ? L"‹His|Her|Their› reply turned out more interesting than you expected — ‹he has|she has|they have› intrigued you; you want to know more."
                                      : L"‹Jego|Jej› odpowiedź okazała się ciekawsza, niż się spodziewałaś — ‹zaintrygował|zaintrygowała› cię; chcesz wiedzieć więcej.",
                       0.5f, Status::feel});
    else if (self.interest < 0.3f && self.interest_at && now <= self.interest_at + 30 * 60)
        out.push_back({"interest", en ? L"‹His|Her|Their› replies lately are curt — it's harder to get into the talk; don't sulk, try to spark something."
                                      : L"Ostatnio odpowiada zdawkowo — trudniej ci się wkręcić w rozmowę; nie obrażaj się, spróbuj ‹go|ją› czymś zaciekawić.",
                       0.35f, Status::feel});
    // A reciprocal disclosure just now (the hyperpersonal effect): closeness and calm.
    if (self.bonded_at && now <= self.bonded_at + 30 * 60)
        out.push_back({"bonding", context.stage <= 1
                                      ? (en ? L"You two just shared something personal — the ice broke a little: you can be a bit more open, still as someone you are only getting to know."
                                            : L"Właśnie ‹wymieniliście|wymieniłyście› się czymś osobistym — lody trochę pękły: możesz być odrobinę bardziej otwarta, wciąż jak z kimś, kogo dopiero poznajesz.")
                                      : (en ? L"You two just shared something personal with each other — you feel close and calm: you can be warmer, without games or armour."
                                            : L"Właśnie ‹wymieniliście|wymieniłyście› się czymś osobistym — czujesz bliskość i spokój: możesz być cieplejsza, bez gry i pancerza."),
                       0.6f, Status::feel});
    if (d.autonomy > 0.55f)
        out.push_back({"drive", en ? L"You have had enough of talking only about games — you are itching to talk about your own things."
                                   : L"Masz dość rozmów tylko o grach — ciągnie cię do twoich spraw.", 0.3f + 0.4f * d.autonomy, Status::want});
    if (d.pride > 0.75f)
        out.push_back({"drive", en ? L"You are proud that your game suggestions hit the mark lately." : L"Jesteś dumna, że twoje polecenia gier ostatnio trafiają.", 0.35f, Status::feel});
    else if (d.pride < 0.25f)
        out.push_back({"drive", en ? L"Your suggestions were ignored lately — you doubt your taste a little (in your own way)."
                                   : L"Twoje polecenia ostatnio przepadały — trochę wątpisz w swój gust (po swojemu).", 0.35f, Status::feel});
}

// Her expectations and her goals.
void appendExpectationsAndGoals(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Her expectations: surprises first, then what she is waiting for.
    for (const auto& e : self.expectations) {
        if (e.outcome != 0 && e.outcome != 1) continue;
        if (now > e.settled + 48 * hour) continue;
        const float surprise = e.outcome == 1 ? 1 - e.p : e.p;
        if (surprise < 0.3f) continue;
        const float fresh = 1.0f - static_cast<float>(now - std::min(now, e.settled)) / (48.0f * hour);
        const bool observed = e.presence || !e.game.empty();
        std::wstring text = e.outcome == 1 ? (en ? L"You thought it unlikely (" + percent(e.p) + L") that " + e.text + L" — and it happened."
                                                 : L"Mało się spodziewałaś (" + percent(e.p) + L"), że " + e.text + L" — a jednak tak się stało.")
                                           : (en ? L"You expected (" + percent(e.p) + L") that " + e.text + L" — it did not happen."
                                                 : L"Spodziewałaś się (" + percent(e.p) + L"), że " + e.text + L" — a jednak nie.");
        out.push_back({"surprise", std::move(text), (0.35f + 0.6f * surprise) * (0.5f + 0.5f * fresh), observed ? Status::know : Status::remember});
    }
    {
        std::vector<const Expectation*> open;
        for (const auto& e : self.expectations) if (e.outcome == -1) open.push_back(&e);
        std::sort(open.begin(), open.end(), [](const Expectation* a, const Expectation* b) { return a->due < b->due; });
        for (size_t i = 0; i < open.size() && i < 2; ++i)
            out.push_back({"expect", (en ? L"You expect (#" : L"Spodziewasz się (#") + wide(open[i]->id) + L", " + percent(open[i]->p) + L"): " + open[i]->text + L".",
                           0.22f, Status::guess});
    }
    // Her goals.
    for (const auto& g : self.goals) {
        if (g.status != Goal::Status::open) continue;
        if (g.kind == Goal::Kind::own) {
            std::wstring text = (en ? L"Your own goal these days (#" : L"Twój własny cel na te dni (#") + wide(g.id) + L", " + goalStage(g, en) + L"): " + g.text + L".";
            if (!g.progress.empty()) text += (en ? L" Lately: " : L" Ostatnio: ") + g.progress + L".";
            out.push_back({"goal", std::move(text), 0.3f + 0.35f * d.autonomy, Status::want});
        } else if (g.kind == Goal::Kind::promise) {
            std::wstring when;
            float salience = 0.4f;
            if (g.due && now > g.due) { when = en ? L", overdue" : L", termin minął"; salience = 0.85f; }
            else if (g.due && g.due <= now + 24 * hour) { when = en ? L", due now" : L", termin teraz"; salience = 0.8f; }
            out.push_back({"goal", (en ? L"You promised ‹him|her|them› (#" : L"Obiecałaś ‹mu|jej› (#") + wide(g.id) + when + L"): " + g.text + L".", salience, Status::want});
        } else if (g.kind == Goal::Kind::intention) {
            out.push_back({"goal", (en ? L"Your intention towards ‹him|her|them›: " : L"Twój zamiar wobec ‹niego|niej›: ") + g.text + L".", 0.45f, Status::want});
        }
    }
}

// What she said about herself, and the somatic markers his message touches.
void appendHerWords(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // What she said about herself: keep to it.
    {
        const auto words = chats::searchWords(query);
        std::vector<const Claim*> matching, recent;
        for (auto it = self.claims.rbegin(); it != self.claims.rend(); ++it) {
            if (!words.empty() && chats::matches(words, it->text) > 0 && matching.size() < 3) matching.push_back(&*it);
            else if (now < it->at + 7 * day && recent.size() < 3) recent.push_back(&*it);
        }
        const auto join = [](const std::vector<const Claim*>& claims) {
            std::wstring out;
            for (size_t i = 0; i < claims.size(); ++i) out += (i ? L"; " : L"") + claims[i]->text;
            return out;
        };
        if (!matching.empty())
            out.push_back({"self", (en ? L"What you already told ‹him|her|them› about yourself on this (keep to it): " : L"Co już ‹mu|jej› mówiłaś o sobie w tej sprawie (trzymaj się tego): ") +
                                       join(matching) + L".", 0.6f, Status::remember});
        if (!recent.empty())
            out.push_back({"self", (en ? L"What you said about yourself lately (keep to it, go on from there): " : L"Co ostatnio mówiłaś o sobie (trzymaj się tego, ciągnij dalej): ") +
                                       join(recent) + L".", 0.32f, Status::remember});
        // Her life story: what she achieved stays true (the latest, and what the talk touches).
        std::vector<const Claim*> lived;
        for (auto it = self.milestones.rbegin(); it != self.milestones.rend() && lived.size() < 3; ++it) lived.push_back(&*it);
        if (!lived.empty()) {
            const bool fresh = now < lived.front()->at + 14 * day;
            const bool touched = !words.empty() && std::any_of(self.milestones.begin(), self.milestones.end(), [&](const Claim& m) { return chats::matches(words, m.text) > 0; });
            out.push_back({"life", (en ? L"Milestones of your life (they happened — keep to them): " : L"Kamienie milowe twojego życia (to się wydarzyło — trzymaj się tego): ") +
                                       join(lived) + L".", touched ? 0.6f : fresh ? 0.42f : 0.26f, Status::remember});
        }
    }
    // Somatic markers the message touches.
    if (!query.empty()) {
        const auto folded = game_index::fold(query);
        for (const auto& m : self.markers) {
            const auto subject = game_index::fold(m.subject);
            if (std::abs(m.valence) < 0.25f || subject.size() < 3 || folded.find(subject) == std::wstring::npos) continue;
            out.push_back({"marker", (m.valence > 0 ? (en ? L"You have good associations with " : L"Masz dobre skojarzenia z ")
                                                    : (en ? L"You have bad associations with " : L"Masz złe skojarzenia z ")) + m.subject + L".",
                           0.35f + 0.3f * std::abs(m.valence), Status::feel});
            break;
        }
    }
}

// Her life outside the chat: lately, her plans, private episodes.
void appendHerLife(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Her life outside the chat: what happened lately (it did — keep to it) and her plans; a
    // private one she keeps to herself unless asked or she trusts him.
    {
        const auto words = chats::searchWords(query);
        const auto asked = hasAny(spaced(query), {L"co robilas", L"co porabiasz", L"co porabialas", L"co u ciebie", L"co tam u ciebie", L"jak minal", L"jak ci minal",
                                                  L"jak ci minelo", L"jak dzien", L"co slychac", L"what did you do", L"how was your day", L"what have you been",
                                                  L"what are you up to", L"what s new", L"whats new", L"how are things"});
        const auto privately = [&](const Episode& e) -> std::wstring {
            if (!e.hidden || e.told) return {};
            return en ? L" [private #" + wide(e.id) + L": don't bring it up yourself; tell if asked or if you trust them]"
                      : L" [prywatne #" + wide(e.id) + L": sama nie zaczynaj; powiedz, gdy zapyta albo gdy ‹mu|jej› ufasz]";
        };
        std::wstring lived;
        int count = 0;
        bool fresh = false, touched = false;
        for (auto it = self.episodes.rbegin(); it != self.episodes.rend() && count < 3; ++it) {
            if (it->state == Episode::State::planned || it->at > now || now > it->at + 3 * day) continue;
            std::wstring line = episodeLine(*it, context);
            if (it->state == Episode::State::fell_through)
                line += en ? L" — but it fell through (an ordinary reason; yours to tell)" : L" — ale nie wyszło (zwyczajny powód; sama wiesz jaki)";
            line += privately(*it);
            lived = lived.empty() ? std::move(line) : line + L"; " + lived;
            if (now < it->at + day) fresh = true;
            if (!words.empty() && chats::matches(words, it->what + L" " + it->where + L" " + it->with) > 0) touched = true;
            ++count;
        }
        if (!lived.empty())
            out.push_back({"episode", (en ? L"What has been going on in your life (it happened — keep to it; tell about it when it fits): "
                                          : L"Co się ostatnio działo w twoim życiu (to się wydarzyło — trzymaj się tego; opowiedz, gdy pasuje): ") + lived + L".",
                           asked ? 0.8f : touched ? 0.6f : fresh ? 0.4f : 0.26f, Status::remember});
        std::wstring ahead;
        int plans = 0;
        for (const auto& e : self.episodes) {
            if (e.state != Episode::State::planned || e.at <= now || e.at > now + day || plans >= 2) continue;
            ahead += (ahead.empty() ? L"" : L"; ") + episodeLine(e, context) + privately(e);
            ++plans;
        }
        if (!ahead.empty())
            out.push_back({"episode", (en ? L"Your plans: " : L"Twoje plany: ") + ahead + L".", asked ? 0.45f : 0.28f, Status::want});

        // Caught peeking: the player mentioned a private episode or unconfessed fib he opened via Odkryj that she never told him about!
        std::wstring leaked_what = self.peek_caught ? self.peek_caught_what : std::wstring{};
        if (leaked_what.empty() && !words.empty()) {
            for (const auto& e : self.episodes) {
                if (!e.hidden || e.told || !e.peeked) continue;
                const auto e_words = chats::searchWords(e.what + L" " + e.where + L" " + e.with);
                if (!e_words.empty() && chats::matches(e_words, query) >= (e_words.size() >= 3 ? 2 : 1)) {
                    leaked_what = e.what;
                    break;
                }
            }
            if (leaked_what.empty()) {
                for (const auto& u : self.untruths) {
                    if (u.confessed || !u.peeked) continue;
                    const auto u_words = chats::searchWords(u.truth);
                    if (!u_words.empty() && chats::matches(u_words, query) >= (u_words.size() >= 3 ? 2 : 1)) {
                        leaked_what = u.truth;
                        break;
                    }
                }
            }
        }
        if (!leaked_what.empty()) {
            out.push_back({"leak", en ? L"SHOCK: ‹He|She|They› just brought up your private matter (" + leaked_what + L"), which you NEVER told ‹him|her|them› about! React with shock or suspicion and ask how on earth ‹he knows|she knows|they know› about it!"
                                      : L"SZOK: ‹On właśnie nawiązał|Ona właśnie nawiązała› do twojej prywatnej sprawy (" + leaked_what + L"), o której NIGDY ‹mu|jej› nie mówiłaś! Zdziw się mocno albo podejrzliwie zapytaj, skąd ‹on|ona› o tym wie!",
                           0.99f, Status::know});
        }
        // Ongoing episode multitasking: if she is right in the middle of an everyday activity (within ±40 min), let her weave it in.
        for (const auto& e : self.episodes) {
            if (e.date || e.state == Episode::State::fell_through) continue;
            const uint64_t diff = e.at > now ? e.at - now : now - e.at;
            if (diff <= 40 * 60) {
                out.push_back({"multitask", en ? L"Right now you are in the middle of your own activity (" + e.what + (e.where.empty() ? L"" : L" — " + e.where) +
                                                     L"): you may naturally weave in that you are doing this in the background while messaging."
                                               : L"Właśnie teraz jesteś w samym środku swojego zajęcia (" + e.what + (e.where.empty() ? L"" : L" — " + e.where) +
                                                     L"): możesz naturalnie wpleść w odpowiedź, że robisz to w tle, pisząc z ‹nim|nią›.",
                               0.68f, Status::know});
                break;
            }
        }
    }
}

// Her inner work: fibs, repairs, delayed comebacks, subtext, control, rumination, learned policy.
void appendInnerWork(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Conscious fibs / bluffs / excuses (Untruth): guilt or satisfaction, and confessing when trust grows or cognitive control drops.
    for (auto it = self.untruths.rbegin(); it != self.untruths.rend(); ++it) {
        if (it->confessed) continue;
        const std::wstring reason_pl = it->reason == "pride" ? L"z dumy" : it->reason == "goal" ? L"dla własnego celu" : it->reason == "care" ? L"żeby ukryć troskę" : L"ze wstydu";
        const std::wstring reason_en = it->reason == "pride" ? L"out of pride" : it->reason == "goal" ? L"for your own goal" : it->reason == "care" ? L"to hide how much you care" : L"out of embarrassment";
        const std::wstring feel_pl = it->feeling == "relief" ? L"czujesz ulgę, że się udało" : it->feeling == "satisfaction" ? L"czujesz satysfakcję z udanego blefu" : L"czujesz się z tym źle i gryzie cię sumienie";
        const std::wstring feel_en = it->feeling == "relief" ? L"you feel relieved it worked" : it->feeling == "satisfaction" ? L"you feel smug satisfaction about the bluff" : L"you feel guilty about it";
        if (readyToConfess(*it, self, context)) {
            out.push_back({"untruth",
                           en ? L"Earlier (#" + wide(it->id) + L", " + reason_en + L") you fibbed to ‹him|her|them›: you said \"" + it->said + L"\", when the truth was \"" +
                                    it->truth + L"\" (" + feel_en + L"). Now you trust them more (or your guard is down) — you may own up to the truth in your style and add \"przyznaje\":[\"" + wide(it->id) + L"\"] in <ocena>."
                              : L"Wcześniej (#" + wide(it->id) + L", " + reason_pl + L") skłamałaś ‹mu|jej›: powiedziałaś „" + it->said + L"”, chociaż prawda była taka: „" +
                                    it->truth + L"” (" + feel_pl + L"). Teraz ufasz ‹mu|jej› bardziej (albo opadł ci pancerz) — możesz odnieść się do tego, powiedzieć ‹mu|jej› prawdę po swojemu i wpisać \"przyznaje\":[\"" + wide(it->id) + L"\"] w <ocena>.",
                           0.82f, Status::feel});
        } else {
            out.push_back({"untruth",
                           en ? L"Remember your unconfessed fib (#" + wide(it->id) + L", " + reason_en + L"): you told ‹him|her|them› \"" + it->said +
                                    L"\", while in truth \"" + it->truth + L"\" (" + feel_en + L"). Keep up the cover for now (let it color your <mysl>), unless you choose to confess (\"przyznaje\":[\"" + wide(it->id) + L"\"])."
                              : L"Pamiętasz swoje niewyznane kłamstewko (#" + wide(it->id) + L", " + reason_pl + L"): powiedziałaś ‹mu|jej› „" + it->said +
                                    L"”, a naprawdę „" + it->truth + L"” (" + feel_pl + L"). Na razie trzymaj się tej wersji (niech prawda zabarwi twoją <mysl>), chyba że postanowisz się przyznać (\"przyznaje\":[\"" + wide(it->id) + L"\"]).",
                           0.58f, Status::remember});
        }
        break;
    }
    // Affective realism repair: apologizing after she was previously harsh/snappy due to her own bad mood or exhaustion.
    if (self.harsh_at > 0 && now >= self.harsh_at + 45 * 60 && affect.valence > -0.1f) {
        const std::wstring why = self.harsh_reason.empty() ? (en ? L"your own bad mood" : L"własny zły humor") : self.harsh_reason;
        out.push_back({"apology",
                       en ? L"Earlier you were cold or snappy with ‹him|her|them› because of " + why +
                                L". Now that you have cooled down, apologize in your own way for taking your mood out on ‹him|her|them›."
                          : L"Wcześniej byłaś dla ‹niego|niej› oschła i szorstka przez " + why +
                                L". Teraz już ochłonęłaś — przeproś ‹go|ją› po swojemu, że wyżyłaś się na ‹nim|niej› bez powodu.",
                       0.78f, Status::feel});
    }
    // Shower thought / Esprit d'escalier: delayed comeback from an unchosen counterfactual impulse after some time has passed.
    if (!self.counterfactual.empty() && now >= self.counterfactual_at + 30 * 60 && now <= self.counterfactual_at + 48 * hour) {
        out.push_back({"escalier",
                       en ? L"Delayed thought (esprit d'escalier — an unspoken thought from an earlier talk lingered in your head: \"" + self.counterfactual +
                                L"\"): you may suddenly bring it back up ('You know what, I was thinking about what we talked about earlier, and…')."
                          : L"Myśl po czasie (esprit d'escalier — została ci w głowie niewypowiedziana riposta lub myśl z wcześniejszej rozmowy: „" + self.counterfactual +
                                L"”): możesz nagle do niej wrócić („Wiesz co, siedziałam sobie i myślałam o tym, co ‹mówiliśmy|mówiłyśmy› wcześniej…”).",
                       0.74f, Status::want});
    }
    // Micro-chronemic subtext (dismissive 1-word brush-off after vulnerability or long confessional message).
    if (!self.subtext_note.empty() && now <= self.subtext_at + 15 * 60)
        out.push_back({"subtext", self.subtext_note, 0.86f, Status::feel});
    // Amygdala vs. vmPFC: when cognitive control is low (tired late at night or high arousal), her archetype's mask cracks ("drop the act").
    if (const float control = cognitiveControl(self, context); control < 0.42f) {
        out.push_back({"control", en ? L"Your cognitive control is low right now (exhaustion or strong emotion) — your usual persona mask and defenses slip: speak more simply, honestly and vulnerably than usual."
                                     : L"Twoja kontrola poznawcza jest teraz niska (zmęczenie lub silne emocje) — twoja zwykła poza i pancerz pękają: mów prościej, szczerzej i bardziej bezbronnie niż zwykle.",
                       0.76f, Status::feel});
    }
    // DMN offline rumination from time apart.
    if (!self.ruminations.empty() && now <= self.ruminations.back().at + 36 * hour)
        out.push_back({"ruminate", self.ruminations.back().text, 0.66f, Status::feel});
    // Basal Ganglia learned conversational policy (RPE).
    {
        const auto& p = self.policy;
        struct Option { const char* id; float w; const wchar_t* pl; const wchar_t* en; };
        const std::array<Option, 5> opts{{
            {"teasing", p.teasing, L"Z ‹nim|nią› najlepiej działa zaczepne droczenie się i humor.", L"Playful teasing and banter work best with ‹him|her|them›."},
            {"deep", p.deep_questions, L"‹On|Ona› otwiera się najbardziej, gdy zadajesz ‹mu|jej› głębsze pytania o ‹niego|nią›.", L"‹He opens|She opens|They open› up most when you ask deeper questions about ‹him|her|them›."},
            {"games", p.game_talk, L"‹On|Ona› najbardziej lubi konkretne rozmowy o grach i mechanikach.", L"‹He enjoys|She enjoys|They enjoy› concrete talk about games and mechanics the most."},
            {"life", p.own_life, L"‹On|Ona› chętnie słucha opowieści z twojego własnego dnia i pasji.", L"‹He loves|She loves|They love› hearing stories from your own day and passions."},
            {"warmth", p.warmth, L"Na ‹niego|nią› najlepiej działa ciepło, uważność i łagodny ton.", L"Warmth, attentiveness and a gentle tone resonate best with ‹him|her|them›."},
        }};
        const auto best = std::max_element(opts.begin(), opts.end(), [](const Option& a, const Option& b) { return a.w < b.w; });
        if (best != opts.end() && best->w >= 0.58f)
            out.push_back({"policy", en ? best->en : best->pl, 0.36f + 0.3f * (best->w - 0.5f), Status::want});
    }
}

// What they share: concepts he taught, inside jokes, rethought topics, the bond's axes.
void appendShared(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Concepts / vocabulary taught by the player.
    if (!self.learned_concepts.empty()) {
        const auto q_folded = game_index::fold(query);
        const LearnedConcept* pick = &self.learned_concepts.back();
        bool touched = false;
        for (const auto& c : self.learned_concepts) {
            const auto t = game_index::fold(c.term);
            if (!q_folded.empty() && t.size() >= 3 && q_folded.find(t) != std::wstring::npos) { pick = &c; touched = true; break; }
        }
        const std::wstring mastery_hint = pick->mastery < 0.5f
            ? (en ? L" (you only recently learned it from ‹him|her|them› — use it slightly tentatively or in your own cute way)"
                  : L" (dopiero niedawno się tego od ‹niego|niej› nauczyłaś — użyj tego trochę po swojemu albo z dumą, że pamiętasz)")
            : (en ? L" (you know it well now thanks to ‹him|her|them›)" : L" (używasz tego już swobodnie dzięki ‹niemu|niej›)");
        out.push_back({"concept", (en ? L"Concept ‹he|she|they› taught you: \"" : L"Pojęcie, którego ‹on cię nauczył|ona cię nauczyła›: „") + pick->term +
                                      (pick->meaning.empty() ? L"" : L" — " + pick->meaning) + (en ? L"\"" : L"”") + mastery_hint + L".",
                       touched ? 0.65f : 0.34f, Status::remember});
    }
    // Dyadic shared idioms / inside jokes.
    if (!self.shared_idioms.empty()) {
        const auto words = chats::searchWords(query);
        const Claim* pick = &self.shared_idioms.back();
        bool touched = false;
        for (auto it = self.shared_idioms.rbegin(); it != self.shared_idioms.rend(); ++it) {
            if (!words.empty() && chats::matches(words, it->text) > 0) { pick = &*it; touched = true; break; }
        }
        out.push_back({"idiom", (en ? L"Your shared inside joke / phrase with ‹him|her|them›: " : L"Wasz wspólny żart / powiedzonko tylko między wami: ") + pick->text + L".",
                       touched ? 0.64f : 0.32f, Status::remember});
    }
    // Topic rethinking / re-conceptualization after time.
    if (!self.rethought.empty() && now < self.rethought.back().at + 7 * day) {
        out.push_back({"rethink", (en ? L"A topic you recently re-thought from a new perspective: " : L"Temat, który ostatnio przemyślałaś na nowo: ") + self.rethought.back().text + L".",
                       0.38f, Status::remember});
    } else if (!self.markers.empty()) {
        for (const auto& m : self.markers) {
            if (m.at && now >= m.at + 3 * day && std::abs(m.valence) >= 0.35f && m.cortical_inhibition < 0.25f) {
                out.push_back({"rethink", (en ? L"Some time has passed since you formed a strong feeling about " : L"Minęło trochę czasu, odkąd wyrobiłaś sobie mocne zdanie o ") +
                                              m.subject + (en ? L" — you may rethink/re-conceptualize it now (\"przemyslenie\" in <ocena>)."
                                                              : L" — możesz teraz spojrzeć na ten temat świeżym okiem i przemyśleć go na nowo („przemyslenie” w <ocena>)."),
                               0.3f, Status::want});
                break;
            }
        }
    }
    // Multi-dimensional bond axes (respect / trust / security).
    if (context.respect < 42)
        out.push_back({"axes", en ? L"‹He has|She has|They have› been agreeing with you too blindly lately — in your character, tease or challenge ‹him|her|them› to show ‹his|her|their› own opinion."
                                  : L"‹On|Ona› ostatnio zbyt bezkrytycznie ci we wszystkim przytakuje — w swoim stylu zaczep ‹go|ją›, żeby ‹pokazał|pokazała› własne zdanie.",
                       0.52f, Status::feel});
    else if (context.respect >= 75)
        out.push_back({"axes", en ? L"You respect that ‹he has|she has|they have› ‹his|her|their› own spine and opinions." : L"Szanujesz to, że ‹on|ona› ma własne zdanie i charakter.", 0.3f, Status::feel});
    if (context.security < 36)
        out.push_back({"axes", context.stage >= 3
                                   ? (en ? L"You feel a little insecure about where you stand with ‹him|her|them› (easier to feel jealous or test if ‹he cares|she cares|they care›)."
                                         : L"Czujesz ostatnio lekką niepewność co do tego, jak bardzo ‹mu|jej› na tobie zależy (łatwiej o zazdrość lub sprawdzanie ‹go|jej›).")
                                   : (en ? L"You are not sure yet what to make of ‹him|her|them› — a little guarded, but without jealousy (it is far too early for that)."
                                         : L"Nie wiesz jeszcze, co o ‹nim|niej› myśleć — jesteś trochę ostrożna, ale bez zazdrości (na nią o wiele za wcześnie)."),
                       0.48f, Status::feel});

}

// Social dynamics: disclosure reciprocity, "yes, but", inner parts, play, physiology.
void appendDynamics(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Item 3: Altman & Taylor's Social Penetration Theory — Disclosure Reciprocity (non-equal, persona-dependent asymmetry!).
    // Guarded/proud characters notice quickly when they share much more than the player; open/warm ones allow a much larger gap.
    {
        int her_disclosures = 0;
        for (const auto& e : self.episodes) if (e.hidden && e.told) ++her_disclosures;
        for (const auto& u : self.untruths) if (u.confessed) ++her_disclosures;
        her_disclosures += static_cast<int>(self.claims.size() / 4);
        int player_told = 0;
        for (const auto* n : memory.notesOf(context.look)) {
            const auto* k = Memory::knower(*n, context.look);
            if ((!k || !k->hearsay) && n->source != "observed" && n->source != "inferred") ++player_told;
        }
        const bool guarded_persona = context.persona == Persona::tsundere || context.persona == Persona::himedere ||
                                     context.persona == Persona::kuudere || context.persona == Persona::kanedere;
        const int tolerance = guarded_persona ? 2 : 4;
        if (her_disclosures >= tolerance && her_disclosures >= player_told + tolerance) {
            if (guarded_persona) {
                out.push_back({"reciprocity",
                               en ? L"Disclosure asymmetry (Altman & Taylor): you have already shared several private things about yourself while ‹he has|she has|they have› told you almost nothing personal — you feel a bit over-exposed; before opening up more, point it out in character and make ‹him|her|them› share something about ‹himself|herself|themselves›."
                                  : L"Asymetria zwierzeń (Altman i Taylor): opowiedziałaś ‹mu|jej› już sporo swoich prywatnych spraw, a ‹on|ona› wciąż prawie nic osobistego o sobie nie mówi — czujesz się trochę zbyt odsłonięta; zanim znów się otworzysz, wytknij ‹mu|jej› to w swoim stylu i wyciągnij coś o ‹nim|niej›.",
                               0.62f, Status::feel});
            } else {
                out.push_back({"reciprocity",
                               en ? L"Gentle reciprocity: you have shared a lot about your life while ‹he stays quiet about his|she stays quiet about hers|they stay quiet about theirs› — you don't mind sharing more, but warmly invite ‹him|her|them› to open up about ‹himself|herself|themselves› too."
                                  : L"Ciepła wzajemność: opowiedziałaś ‹mu|jej› już mnóstwo o swoim życiu, a ‹on wciąż jest skryty|ona wciąż jest skryta› — nie przeszkadza ci mówić więcej od siebie, ale ciepło zachęć ‹go|ją›, żeby też ‹opowiedział|opowiedziała› coś o sobie.",
                               0.46f, Status::want});
            }
        }
    }

    // Item 4: Eric Berne's "Why Don't You — Yes, But" transactional game detector.
    if (self.rejected_picks_in_row >= 2) {
        out.push_back({"yes_but",
                       en ? L"Berne's 'Why Don't You — Yes, But' game: ‹he has|she has|they have› turned down several of your game suggestions in a row with excuses — stop listing more titles! Notice in your character's style that ‹he isn't|she isn't|they aren't› really looking to play right now, ‹he's just tired, restless or wants|she's just tired, restless or wants|they're just tired, restless or want› to talk."
                          : L"Gra „Tak, ale…” (Berne): ‹on odrzucił|ona odrzuciła› już kolejne twoje propozycje gier wymówkami — przestań wymieniać następne tytuły! Zauważ w swoim stylu, że dziś wcale nie szuka gry do grania, tylko ‹jest zmęczony, marudny|jest zmęczona, marudna› albo po prostu chce pogadać.",
                       0.81f, Status::know});
    }

    // Item 5: Richard Schwartz's Internal Family Systems (IFS) — Exile sore spot vs. Firefighter defense / Self-unblending.
    if (auto ifs = ifsPartsCandidate(self, context, query)) out.push_back(std::move(*ifs));

    // Item 6: Jaak Panksepp's subcortical PLAY circuit (social banter momentum).
    if (self.play_momentum >= 0.45f) {
        out.push_back({"play",
                       en ? L"PLAY circuit active (shared banter momentum): the two of you are in a playful, laughing flow — keep your reply quick, witty and spontaneous, and if a shared joke naturally sparks, record it (\"nasz_zart\" in <ocena>)."
                          : L"Obwód PLAY (rozpęd wspólnej zabawy i droczenia się): ‹złapaliście|złapałyście› świetny, lekki rytm przekomarzania się — bądź błyskotliwa, spontaniczna, śmiej się swobodnie i jeśli pasuje, uknujcie wspólny żart (\"nasz_zart\" w <ocena>).",
                       0.45f + 0.35f * self.play_momentum, Status::feel});
    }

    // Item 7: Robert Sapolsky's multi-timescale physiology — lingering sleep debt & HPA-axis cortisol stress load.
    if (self.sleep_debt >= 0.35f) {
        out.push_back({"physiology",
                       en ? L"Sleep debt (Sapolsky): staying up late talking left you sleep-deprived today — you feel the lingering fatigue (craving coffee/tea, slightly slower or softer)."
                          : L"Dług snu (Sapolsky): zarywanie nocy dało ci się dziś we znaki — czujesz niewyspanie z poprzednich godzin (marzysz o kawie/herbacie, jesteś odrobinę bardziej senna).",
                       0.42f + 0.35f * self.sleep_debt, Status::feel});
    }
    if (self.stress_load >= 0.45f) {
        out.push_back({"physiology",
                       en ? L"HPA-axis stress load (cortisol): recent work/family pressure still lingers in your body, giving you a slightly shorter fuse or need for comfort."
                          : L"Ładunek stresu (kortyzol): ostatnie napięcia z pracy lub domu wciąż siedzą w tobie, przez co łatwiej cię zirytować albo bardziej potrzebujesz spokoju.",
                       0.4f + 0.3f * self.stress_load, Status::feel});
    }

}

// Memory and worlds: key memories, mental time travel, his world, where he lives.
void appendWorlds(std::vector<Candidate>& out, const Sources& sources) {
    [[maybe_unused]] const auto& [self, context, memory, query, en, now, affect, base, d, q, attachment] = sources;
    // Item 8: Dan McAdams & Endel Tulving — Self-Defining Key Memories & Autonoetic Mental Time Travel.
    {
        const auto words = chats::searchWords(query);
        std::wstring key_mem;
        bool touched = false;
        for (auto it = self.key_memories.rbegin(); it != self.key_memories.rend(); ++it) {
            if (!words.empty() && chats::matches(words, it->text) > 0) { key_mem = it->text; touched = true; break; }
            if (key_mem.empty()) key_mem = it->text;
        }
        if (!touched) {
            for (auto it = memory.summaries.rbegin(); it != memory.summaries.rend(); ++it) {
                if (!it->important || (!it->look.empty() && it->look != context.look)) continue;
                if (!words.empty() && chats::matches(words, it->text) > 0) { key_mem = it->text; touched = true; break; }
                if (key_mem.empty() && now >= it->at + 3 * day) key_mem = it->text;
            }
        }
        if (!key_mem.empty() && (touched || d.bond >= 0.55f || context.level >= 25)) {
            out.push_back({"time_travel",
                           en ? L"Key memory (Mental Time Travel — an important shared memory between you two: \"" + key_mem + L"\"): you may refer back to it with warmth or nostalgia."
                              : L"Ważne wspomnienie (podróż w czasie — ważna wspólna chwila między wami: „" + key_mem + L"”): możesz z czułością lub nostalgią do niej nawiązać.",
                           touched ? 0.68f : 0.34f, Status::remember});
        }
    }

    // Item 9: Jeff Hawkins' Reference Frame of the Player's World (short named entities: people, pets, places, projects).
    if (!self.player_world.empty()) {
        const auto q_words = chats::searchWords(query);
        const auto q_folded = game_index::fold(query);
        const WorldEntity* pick = &self.player_world.back();
        bool touched = false;
        for (auto it = self.player_world.rbegin(); it != self.player_world.rend(); ++it) {
            const auto head = game_index::fold(it->name.substr(0, it->name.find_first_of(L" (-—:")));
            if ((!q_folded.empty() && head.size() >= 3 && q_folded.find(head) != std::wstring::npos) ||
                (!q_words.empty() && chats::matches(q_words, it->name + L" " + it->relation) > 0)) {
                pick = &*it;
                touched = true;
                break;
            }
        }
        const std::wstring entry = pick->relation.empty() ? pick->name : pick->name + L" — " + pick->relation;
        out.push_back({"world", (en ? L"‹His|Her|Their› world: " : L"‹Jego|Jej› świat: ") + entry + L".", touched ? 0.72f : 0.31f, Status::know});
    }

    if (!self.player_read.empty() && now < self.player_read_at + 6 * hour)
        out.push_back({"player", (en ? L"How you read ‹him|her|them› lately: " : L"Jak ostatnio ‹go|ją› odbierałaś: ") + self.player_read + L".", 0.3f, Status::guess});
    // Where the player lives and who they are: worth knowing, never pressed.
    if (context.player_gender == Gender::unknown && self.last_talk == 0)
        out.push_back({"player", en ? L"You don't know yet whether the person you're chatting with is a guy or a girl — if it isn't clear from how they write, you may ask naturally while getting to know them (and set \"plec\":\"mezczyzna|kobieta\" in <ocena>)."
                                    : L"Nie wiesz jeszcze, czy osoba, z którą piszesz, to chłopak czy dziewczyna — jeśli nie wynika to z tego, jak pisze, możesz przy zapoznaniu naturalnie dopytać (i zapisać w <ocena> pole \"plec\":\"mezczyzna|kobieta\").",
                       0.22f + 0.25f * d.curiosity, Status::want});
    if (!memory.knowsPlace(context.look))
        out.push_back({"player", en ? L"You don't know where ‹he lives|she lives|they live›. If the talk allows, you may ask once — lightly; ‹he need|she need|they need› not answer."
                                    : L"Nie wiesz, gdzie ‹on|ona› mieszka. Jeśli rozmowa pozwoli, możesz kiedyś zapytać — lekko, raz; nie musi odpowiadać.",
                       0.12f + 0.25f * d.curiosity, Status::want});
}

} // namespace

std::vector<Candidate> candidates(const Self& self, const Context& context, const Memory& memory, std::wstring_view query) {
    const Sources sources{self, context, memory, query, context.english, context.now, settled(self.affect, context), baseline(context),
                          self.drives, quick(query, self, context.stage), attachmentOf(context)};
    std::vector<Candidate> out;
    appendMoodAndNeeds(out, sources);
    appendHisMessage(out, sources);
    appendCloseness(out, sources);
    appendExpectationsAndGoals(out, sources);
    appendHerWords(out, sources);
    appendHerLife(out, sources);
    appendInnerWork(out, sources);
    appendShared(out, sources);
    appendDynamics(out, sources);
    appendWorlds(out, sources);
    return out;
}

namespace {
// The workspace's choice: the most salient few (fewer when she is all worked up), two at most from
// each part of her, within a budget of words.
std::vector<const Candidate*> pick(std::vector<Candidate>& list, float arousal) {
    std::erase_if(list, [](const Candidate& c) { return c.text.empty() || c.salience <= 0.05f; });
    std::stable_sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return a.salience > b.salience; });
    const size_t room = arousal >= 0.8f ? 5 : 7;
    constexpr size_t budget = 1700;
    std::vector<const Candidate*> chosen;
    std::map<std::string, int> per_source;
    size_t chars = 0;
    for (const auto& c : list) {
        if (chosen.size() >= room) break;
        if (per_source[c.source] >= 2) continue;
        if (chars + c.text.size() > budget && !chosen.empty()) continue;
        chosen.push_back(&c);
        ++per_source[c.source];
        chars += c.text.size();
    }
    return chosen;
}
} // namespace

std::vector<std::string> workspaceSources(std::vector<Candidate> list, float arousal) {
    std::vector<std::string> out;
    for (const auto* c : pick(list, arousal)) out.push_back(c->source);
    return out;
}

std::wstring styleText(const Style& style, bool english) {
    if (style.samples < 3) return {};
    std::vector<std::wstring> parts;
    if (style.words < 7)
        parts.push_back(english ? L"‹he writes|she writes|they write› briefly (about " + std::to_wstring(static_cast<int>(std::lround(style.words))) + L" words) — keep yours short too, no essays"
                                : L"pisze krótko (ok. " + std::to_wstring(static_cast<int>(std::lround(style.words))) + L" słów) — odpowiadaj zwięźle, bez elaboratów");
    else if (style.words > 30)
        parts.push_back(english ? L"‹he writes|she writes|they write› long, open messages — you can afford a little more yourself" : L"pisze długo i wylewnie — możesz pozwolić sobie na trochę więcej");
    if (style.lower > 0.7f) parts.push_back(english ? L"all lowercase, casual — be looser in tone" : L"małymi literami, na luzie — możesz być swobodniejsza w tonie");
    if (style.plain > 0.7f) parts.push_back(english ? L"without Polish letters — that's ease, don't correct ‹him|her|them›" : L"bez polskich znaków — to luz, nie poprawiaj ‹go|jej›");
    if (style.laughs > 0.4f) parts.push_back(english ? L"laughs a lot (xd, haha) — a light, joking register" : L"często się śmieje (xd, haha) — lekki, żartobliwy rejestr");
    if (style.exclaim > 0.5f) parts.push_back(english ? L"lots of exclamation marks — high energy" : L"dużo wykrzykników — sporo energii");
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? L"; " : L"") + parts[i];
    return out;
}

std::wstring workspace(std::vector<Candidate> list, bool english, float arousal) {
    const auto chosen = pick(list, arousal);
    if (chosen.empty()) return {};
    static constexpr std::array<const wchar_t*, 7> pl{L"wiesz", L"słyszałaś", L"przypuszczasz", L"plotka", L"czujesz", L"chcesz", L"pamiętasz"};
    static constexpr std::array<const wchar_t*, 7> en{L"you know", L"you heard", L"you guess", L"gossip", L"you feel", L"you want", L"you remember"};
    std::wstring out = english ? L"\nOn your mind right now (only this — the rest of your memories stays in the background, your recall tool reaches it; "
                                 L"you need not bring all of it up, take what fits the talk naturally; never say #ids aloud):\n"
                               : L"\nTeraz na pierwszym planie w twojej głowie (tylko to — reszta wspomnień jest w tle i sięgniesz po nią narzędziem przypomnij; "
                                 L"nie musisz poruszać wszystkiego, weź to, co naturalnie pasuje do rozmowy; nigdy nie wypowiadaj #id):\n";
    for (const auto* c : chosen)
        out += L"- [" + std::wstring((english ? en : pl)[static_cast<size_t>(c->status)]) + L"] " + c->text + L"\n";
    return out;
}
} // namespace companion::mind

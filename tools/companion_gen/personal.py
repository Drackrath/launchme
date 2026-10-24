"""Each companion's own way of showing the 20 feelings (comfy.py, Qwen edits), so they do not look like skins of one
another: her gestures, props and quirks. A feeling missing here falls back to comfy.QWEN_EMOTIONS. POSES: her full-figure
entrance pose per costume (comfy.py pose)."""

EMOTIONS = {
    # Himedere goth "Princess of Darkness", the little sister of the crew; dramatic, but funny; a plush bat, the Count.
    "yuki": {
        "smile": "a small, proud princess smile with closed lips, chin slightly raised, one hand resting elegantly at her collarbone",
        "angry": "an indignant, haughty huff: nose turned up and head turned to the side, eyes closed in offence, cheeks puffed, arms folded high",
        "sad": "theatrical sorrow: the back of one hand pressed to her forehead like a tragic heroine, eyes half-closed, a single tear",
        "excited": "trying to hide her excitement and failing: sparkling eyes, a wobbly grin, both hands clenched under her chin",
        "shock": "scandalised: mouth a small round 'o', both hands raised to her cheeks, eyes wide, a tiny comic ghost-like wisp of her soul leaving her mouth",
        "thinking": "plotting something sinister: a sly sideways glance, the fingertips of both hands pressed together in front of her chin",
        "smug": "a villainous princess smirk with half-closed eyes, the back of one hand raised to the side of her mouth as if about to laugh 'ohohoho'",
        "shy": "flustered and caught off guard: a deep blush, eyes darting away, pulling a strand of her bob across her mouth",
        "fear": "pretending not to be scared: hugging a small black plush bat tightly to her chest, trembling, eyes wide",
        "sleepy": "dozing off: eyes closed, head tilted, her cheek resting on a small black plush bat she holds up to her face",
        "playful": "a mischievous little-devil 'akanbe': one finger pulling down her lower eyelid, tongue out",
        "cool": "aloof and mysterious: a calm half-lidded gaze, closed lips, a faint superior smile, no gesture",
        "love": "lovestruck despite herself: a big blush, heart-shaped highlights in her eyes, both hands clasped over her heart, a wobbly dreamy smile",
        "laugh": "a theatrical villainess laugh: head tilted back, the back of one hand raised beside her open mouth, eyes closed",
        "cry": "a bratty little-sister wail: big tears, mouth wide open, both fists rubbing her eyes",
        "confused": "baffled: head tilted, one eyebrow raised, one finger pressed to her lower lip, a small question mark beside her head",
        "determined": "fired up for revenge: a fierce grin showing a tiny fang, one fist clenched in front of her chest",
        "wave": "a regal little wave, like a princess from a balcony: only her fingers waving at shoulder height, a proud smile",
        "drink": "sipping from a plain black can of black coffee (no logo, no text) with a disgusted grimace because it is too bitter",
        "gaming": "playing a small handheld game console held in both hands, leaning in, the tip of her tongue out in concentration",
    },
    # Know-it-all honour student with red glasses: books, science, lectures; flustered when wrong.
    "akane": {
        "smile": "a confident, slightly superior smile, pushing up her glasses with one finger",
        "angry": "a stern teacher's glare: her glasses flash with a white glare, one index finger raised as if scolding, lips pressed tight",
        "sad": "quietly disappointed: eyes lowered, hugging a thick closed book to her chest, a small sigh",
        "excited": "thrilled by a discovery: eyes sparkling behind her glasses, both hands holding up an open notebook, a big smile",
        "shock": "her theory just collapsed: glasses slipping down her nose, mouth open, one hand frozen in mid-air",
        "thinking": "deep in calculation: looking up, tapping a pencil against her lips, a few small maths symbols floating near her head",
        "smug": "'I told you so': chin raised, eyes closed, a smug smile, one index finger raised in a lecturing gesture",
        "shy": "embarrassed by praise: blushing hard, hiding the lower half of her face behind an open book, eyes peeking over it",
        "fear": "alarmed: gripping a book with both hands, shoulders raised, wide eyes behind slightly crooked glasses",
        "sleepy": "after an all-nighter of studying: glasses pushed up onto her forehead, rubbing one eye with a fist, a tired yawn",
        "playful": "a rare playful moment: a wink, the tip of her tongue out, holding up a pencil like a magic wand",
        "cool": "composed and analytical: a calm neutral face, adjusting her glasses with two fingers",
        "love": "smitten in a slightly unsettling way: heart-shaped highlights in her eyes, a rosy blush, both hands pressed to her cheeks, an intense dreamy smile",
        "laugh": "a refined laugh behind her hand: eyes closed, shoulders shaking",
        "cry": "crying with frustration: tears running from under her glasses, biting her lip, clutching a crumpled test paper",
        "confused": "puzzled: head tilted, glasses askew, one finger scratching her temple, a small question mark beside her head",
        "determined": "ready for the exam: a confident grin, rolling up one sleeve, a pencil held up like a sword",
        "wave": "a polite small wave with one hand at shoulder height, a prim smile",
        "drink": "sipping tea from a white mug with a small atom symbol on it, steam fogging her glasses",
        "gaming": "holding a game controller and analysing the screen intently, one eyebrow raised, light glinting on her glasses",
    },
    # Older gyaru, fashion and cosmetics, a caring big sister; games only through her brother.
    "saki": {
        "smile": "a bright, warm big-sister smile, head tilted, one hand twirling a lock of her hair",
        "angry": "an annoyed big sister: hands on her hips, cheeks puffed, one eyebrow raised in a scolding look",
        "sad": "pouty and sad: teary eyes, holding her smartphone to her chest, a wobbly lower lip",
        "excited": "squealing with excitement: both hands pressed together beside one cheek, sparkling eyes, a wide open smile, little sparkles around her",
        "shock": "gasping dramatically: one hand over her open mouth, the other clutching her smartphone, eyes huge",
        "thinking": "choosing an outfit in her head: looking up, one manicured fingernail tapping her lips, a thoughtful pout",
        "smug": "a confident fashionista smirk, flipping her long hair over her shoulder with one hand",
        "shy": "giggly and shy: blushing, covering her mouth with her fingertips, eyes sparkling to the side",
        "fear": "freaked out: hugging her own shoulders, eyes wide, mouth a wavy line",
        "sleepy": "yawning, a pastel sleep mask pushed up onto her forehead, eyes half closed",
        "playful": "a gyaru peace sign held sideways next to one eye, a wink, tongue out",
        "cool": "relaxed and chic: a calm confident face, her chin resting lightly on the back of one hand",
        "love": "heart hands: both hands forming a heart shape in front of her chest, heart-shaped highlights in her eyes, a big blush",
        "laugh": "laughing loudly: eyes squeezed shut, head thrown back, one hand waving in the air",
        "cry": "crying so her mascara runs a little, dabbing her eyes with a tissue",
        "confused": "confused: head tilted, staring at her smartphone, a small question mark beside her head",
        "determined": "ready to go shopping: a determined grin, holding up a lipstick like a weapon",
        "wave": "an energetic wave with her whole arm raised, a huge smile",
        "drink": "sipping an iced matcha latte through a straw from a clear plastic cup",
        "gaming": "playing a game on her smartphone held sideways in both hands, a focused little pout",
    },
    # Tsundere fox spirit: brutally direct, a bit aggressive, loyal; night city, punk, fighting games.
    "kiko": {
        "smile": "a sharp toothy grin showing a small fang, one eyebrow raised, the cigarette between two fingers",
        "angry": "fierce and aggressive: baring her teeth with fangs, fox ears pinned back, cracking her knuckles in front of her chest",
        "sad": "angry-sad: looking away with a clenched jaw, ears drooping, eyes glistening with tears she refuses to let fall",
        "excited": "pumped up: a wild grin, ears perked up, one fist punching into her open palm",
        "shock": "startled: the fur on her ears bristling, eyes wide, mouth open showing her fangs",
        "thinking": "scheming like a fox: a narrowed side glance, a sly smile, one finger tapping her cheek",
        "smug": "a provoking smirk, head tilted back, beckoning the viewer with two fingers: 'come at me'",
        "shy": "caught off guard by a compliment: a furious blush, ears flattened, looking away, one hand rubbing her neck",
        "fear": "spooked: fox ears flattened, gritting her teeth, shoulders hunched, eyes wide",
        "sleepy": "lazy: eyes half closed, her chin resting on her folded arms, a slow yawn showing her fangs",
        "playful": "a teasing fox-hand sign (index and little fingers up, the others touching her thumb) beside her face, a wink and a fanged grin",
        "cool": "unbothered: a calm half-lidded stare, exhaling a thin stream of smoke",
        "love": "embarrassed love: a deep blush, angry eyebrows but a wobbly smile, arms crossed, looking away",
        "laugh": "a loud, rough laugh: head back, mouth wide open showing her fangs, one hand on her stomach",
        "cry": "crying angrily: tears flowing, gritted teeth, wiping her eyes roughly with her forearm",
        "confused": "'huh?!': a crooked confused face, one ear up and one ear down, head tilted",
        "determined": "ready to fight: a fierce grin, one fist raised, a small blue fox-fire flame floating above her other palm",
        "wave": "a lazy two-finger salute off her brow, a smirk",
        "drink": "gulping down a plain dark can of cold coffee (no logo, no text), head tilted back",
        "gaming": "gripping an arcade fight stick held in front of her, fangs bared in competitive fury",
    },
}

# Costumes beyond the ones in res/companion (each companion has four): folder -> (name_pl, name_en, you_pl, you_en, look for the generator).
COSTUMES = {
    ("yuki", "costume_30_sailor"): ("Mroczny mundurek", "Dark sailor uniform",
        "Masz na sobie czarny mundurek z marynarskim kołnierzem i karmazynową chustą, długą czarną plisowaną spódnicę, czarne rajstopy i mały srebrny krzyżyk w uchu.",
        "You wear a black sailor uniform with a crimson neckerchief, a long black pleated skirt, black tights and a small silver cross earring.",
        "A gothic sailor school uniform: a black sailor-collar blouse with a crimson neckerchief, a long black pleated skirt, black tights, a small silver cross "
        "earring; her silver bob and goth make-up stay."),
    ("akane", "costume_30_hakama"): ("Hakama na zakończenie roku", "Graduation hakama",
        "Masz na sobie różowe kimono we wzór piór strzał (yagasuri), ciemnofioletową hakamę i dużą czerwoną kokardę we włosach; okulary zostają.",
        "You wear a pink kimono with an arrow-feather pattern, a deep purple hakama and a big red ribbon in your hair; your glasses stay on.",
        "A Japanese graduation hakama: a pink kimono with an arrow-feather (yagasuri) pattern, a deep purple hakama, a big red ribbon in her hair; her "
        "red-framed glasses stay."),
    ("saki", "costume_20_magical"): ("Magical girl urody", "Beauty magical girl",
        "Masz na sobie błyszczący różowo-złoty strój magical girl z dużą kokardą i bufiastymi rękawami, broszkę-puderniczkę w kształcie serca i tiarę z klejnotem jak szminka.",
        "You wear a sparkling pink-and-gold magical-girl outfit with a big bow and puffy sleeves, a heart-shaped compact brooch and a tiara with a lipstick-shaped gem.",
        "A magical-girl transformation outfit with a beauty theme: a sparkling pink-and-gold dress with a big ribbon bow at the chest and puffy sleeves, a "
        "heart-shaped compact-mirror brooch, a little tiara with a lipstick-shaped gem, star hair clips; her long wavy blonde hair and gyaru make-up stay."),
    ("saki", "costume_30_pajama"): ("Piżamka z uszkami", "Bunny pyjamas",
        "Jesteś w domu: masz na sobie puchatą pastelowo różową piżamę z kapturem z króliczymi uszami, włosy w niedbałym koku i płatki pod oczami, bez makijażu.",
        "You are at home: a fluffy pastel pink pyjama hoodie with bunny ears on the hood, your hair in a messy bun, eye patches under your eyes, no make-up.",
        "Cosy night-time: a fluffy pastel pink pyjama hoodie with floppy bunny ears on the hood (the hood down), her long blonde hair in a messy high bun, "
        "little hydrating eye patches under her eyes, no make-up."),
    ("kiko", "costume_30_winter"): ("Zimowa ulica", "Winter streets",
        "Masz na sobie za dużą czarną kurtkę puchową, ciemnoczerwoną czapkę z dziurami na lisie uszy i gruby szary szalik.",
        "You wear an oversized black puffer jacket, a dark red beanie with holes for your fox ears and a thick grey scarf.",
        "Winter streetwear: an oversized black puffer jacket, a dark red knit beanie with her fox ears poking out through holes, a thick grey scarf."),
}

POSES = {
    ("yuki", "costume_30_sailor"): "sitting on top of a school desk with her legs crossed modestly under the long skirt, chin propped on one hand, a bored haughty look; "
                                   "black tights and loafers",
    ("akane", "costume_30_hakama"): "standing proudly holding a rolled diploma tied with a red ribbon in both hands, a beaming smile; brown lace-up boots",
    ("saki", "costume_20_magical"): "a magical-girl finishing pose: hopping lightly with one leg bent back, pointing a lipstick-shaped wand forward with a wink, "
                                    "sparkles around her; pink boots",
    ("saki", "costume_30_pajama"): "sitting cross-legged on the floor hugging a big fluffy pillow, yawning; fluffy pyjama trousers and bunny slippers",
    ("kiko", "costume_30_winter"): "walking with her hands in the pockets of her puffer jacket, her breath steaming in the cold, a big fluffy dark fox tail swishing "
                                   "behind her, a smirk; black jeans and boots",
}

# Thirteen photos each (comfy.py selfie): "<range>|<stem>|[<shot>] <outfit, place, pose and expression>". Shots: arm (default), tripod,
# mirror, above, candid, legs (comfy.SHOTS). Every place, outfit, kind of shot and expression differs.
SELFIES = {
    "yuki": [
        "00-09|01_cemetery|[tripod] Among moss-covered gravestones and fallen leaves in an old cemetery at golden autumn dusk, wearing a black velvet coat, standing with a black lace parasol over her shoulder and the other hand on her hip, a haughty little smile.",
        "00-09|02_cafe|[candid] In a cosy gothic café with candles and dark wood, wearing a black turtleneck and silver earrings, holding a whipped-cream mocha with both hands, caught with a bit of cream on her lip, wide-eyed and embarrassed.",
        "00-09|03_records|[candid] In the aisle of a vinyl record shop, flipping through dark album sleeves (no text) with both hands, wearing a black hoodie, completely absorbed, a tiny smile.",
        "00-09|04_rain|[arm] On a rainy city street at night under a clear umbrella, neon reflections on the wet pavement, wearing a black trench coat, pouting at the rain.",
        "00-09|05_haunted|[tripod] At the entrance of a haunted-house attraction in an amusement park, wearing a black bomber jacket, hugging a small black plush bat with both arms, trying to look brave but clearly scared.",
        "10-19|06_bed|[above] Lying on her bed at night among candles, fairy lights and gothic posters (no text), in a black oversized T-shirt, a sleepy dreamy look.",
        "10-19|07_vanity|[candid] At an ornate vintage dressing table in her room, putting on dark lipstick in front of the mirror with one hand, holding a small compact in the other, in a black lace blouse, concentrated.",
        "10-19|08_movie|[candid] On a sofa in a dark living room lit only by a TV playing a horror film, a blanket over her head like a hood, clutching a bowl of popcorn with both hands, terrified.",
        "10-19|09_cocoa|[arm] In a dim kitchen at night, wearing a black apron with small bats on it, a mug of hot chocolate with marshmallows in her hand, a pleased smug smile.",
        "20-29|10_goodnight|[above] In bed under a black duvet late at night, lit by a dim bedside lamp, in black pyjamas, the plush bat tucked under her chin, eyes half closed, whispering goodnight.",
        "30-39|11_fitting|[mirror] In a boutique fitting-room mirror trying on a black off-the-shoulder lace evening dress (modest neckline), a curious, slightly shy look at her reflection.",
        "30-39|12_hallway|[mirror] In the tall mirror of her hallway before going out, in a short black wool coat, a beret and a long scarf, striking a dramatic princess pose with her free hand raised.",
        "40-49|13_velvet|[candid] Lounging on a dark velvet chaise in a candlelit gothic room, in an elegant black satin slip dress with a lace shawl over her shoulders, one leg crossed over the other, looking over her shoulder with a slow, alluring half-smile.",
    ],
    "akane": [
        "00-09|01_library|[tripod] Between tall wooden bookshelves in a grand university library, wearing a beige cardigan, pulling a book from a high shelf on tiptoe, a satisfied smile.",
        "00-09|02_planetarium|[arm] Under the dome of a planetarium with stars projected above, wearing a navy sweater, pointing up excitedly at a constellation, awestruck.",
        "00-09|03_park|[candid] On a park bench under red maple trees in autumn, wearing a camel trench coat, reading a book with a thermos of tea beside her, peaceful and absorbed.",
        "00-09|04_lecture|[arm] In an empty lecture hall in front of a blackboard full of blurred formulas, wearing a white shirt and a red tie, chalk in one hand, the other on her hip, smug.",
        "00-09|05_bookcafe|[arm] In a bookstore café with a cup of tea and a slice of strawberry cake, wearing a red knit sweater, her glasses fogged up from the steam, laughing at herself.",
        "10-19|06_desk|[candid] At her desk late at night lit by a desk lamp, piles of notes around her, wearing a big grey hoodie, writing with a pen, a smudge of ink on her cheek, yawning.",
        "10-19|07_telescope|[tripod] On a rooftop at night beside a telescope under a starry sky, wrapped in a blanket, looking into the eyepiece, then glancing at the camera with excitement.",
        "10-19|08_baking|[candid] In her kitchen doing a 'scientific' baking experiment, flour on her face, wearing an apron, carefully pouring milk into a measuring jug, extremely serious.",
        "10-19|09_floor|[above] Lying on the floor among scattered open books, in a soft lilac sweater, her glasses askew, giggling.",
        "20-29|10_bedread|[candid] Sitting up in bed late at night reading with a small book light, in pyjamas with tiny planets on them, glasses pushed up on her head, sleepy.",
        "30-39|11_elevator|[mirror] In a mirrored elevator, dressed up in a dark green velvet dress and a pearl hair clip for an academic gala, adjusting her glasses with her free hand, nervous but proud.",
        "30-39|12_bathroom|[mirror] In her bathroom mirror in the morning, wearing an oversized white shirt, her hair half up, holding a toothbrush in her free hand, sleepy and grumpy.",
        "40-49|13_lamp|[arm] In a dim study lit by a green banker's lamp, in a fitted burgundy blouse, lowering her glasses with one finger and giving the camera a teasing, sultry look over the rims.",
    ],
    "saki": [
        "00-09|01_shopping|[tripod] On a busy fashionable shopping street, wearing an oversized denim jacket and a pleated skirt, posing with shopping bags in both hands like a model, confident.",
        "00-09|02_beauty|[arm] At a department-store beauty counter, showing a lipstick swatch on the back of her hand to the camera, wearing a chic silk blouse, thrilled.",
        "00-09|03_nails|[candid] In a nail salon, her hands resting on the table while a manicurist (only hands visible) paints her nails pastel, wearing a soft cardigan, chatting happily.",
        "00-09|04_parfait|[arm] In a trendy café with an iced matcha latte and a tall strawberry parfait on the table, wearing a floral sundress, winking.",
        "00-09|05_seaside|[candid] Walking along a seaside promenade at sunset, sunglasses on her head, a white linen shirt, laughing as the wind blows her hair, holding it back with one hand.",
        "10-19|06_skincare|[tripod] At her bathroom vanity in a fluffy robe and a headband, applying eye patches under her eyes with both hands, a playful pout.",
        "10-19|07_brother|[arm] In a living room where, in the background, her older brother (a young man in a hoodie, seen from behind) is gaming intensely at a desk; she rolls her eyes at the camera, wearing a cosy sweater.",
        "10-19|08_closet|[candid] In a walk-in closet full of clothes, holding up two outfits on hangers, one in each hand, deciding, wearing a tank top and an open cardigan, puzzled.",
        "10-19|09_balcony|[candid] On a balcony at night with fairy lights, leaning on the railing in an oversized knit sweater, sipping strawberry milk, a dreamy faraway look.",
        "20-29|10_night|[above] In bed in a silky pastel pyjama set, a sleep mask on her forehead, hugging a pillow, blowing a goodnight kiss.",
        "30-39|11_club|[mirror] In the mirror of a glamorous restroom with gold lights before a night out, in a sparkly silver mini dress with a modest neckline, touching up her lipstick, a confident smirk.",
        "30-39|12_gym|[mirror] In a gym's mirror wall in a matching pastel sports set (a zip-up top and leggings), showing off her outfit more than any exercise, a cheeky grin.",
        "40-49|13_satin|[above] Lying on silky pink bedsheets in a satin pastel pyjama set, her hair spread out around her, biting her lip playfully with a flirty wink.",
    ],
    "kiko": [
        "00-09|01_garage|[tripod] In an underground parking garage at night lit by flickering fluorescent lights, leaning against her black sport motorbike, wearing a black leather riding jacket, a helmet under her arm, a bold fanged smirk.",
        "00-09|02_ramen|[candid] At 3 a.m. at the counter of a tiny steamy ramen shop with paper lanterns and a noren curtain behind her, wearing an oversized charcoal hoodie, slurping noodles with chopsticks, eyes closed in bliss.",
        "00-09|03_arcade|[arm] In a crowded neon-lit retro arcade with fighting-game cabinets glowing red and violet, wearing a plain black T-shirt (no text) under an open red flannel shirt, a cocky peace sign after a win.",
        "00-09|04_rooftop|[candid] On a rooftop at night above a neon city skyline, sitting on the ledge in an oversized bomber jacket, the wind in her long hair, looking at the city with a lazy, distant look.",
        "00-09|05_shrine|[arm] Among rows of red torii gates at an Inari shrine at dusk, wearing a white hoodie with a fox mask on her head, a sly smile.",
        "10-19|06_tattoo|[candid] In a tattoo studio, sitting in the chair with her sleeve rolled up while a gloved artist works on a small fox tattoo on her forearm, gritting her teeth with a grin.",
        "10-19|07_livehouse|[tripod] In a small live house with stage lights and a punk band blurred behind her, wearing a black tank top with a studded belt, both fists raised, shouting along.",
        "10-19|08_konbini|[arm] In a convenience store at 2 a.m. under bright lights, wearing a track jacket, holding a can of coffee, a tired smirk.",
        "10-19|09_couch|[above] Lying on a beat-up couch in her apartment, an arcade fight stick resting on her stomach, wearing ripped jeans and a loose T-shirt, a smug winner's grin.",
        "20-29|10_dawn|[candid] On her balcony at dawn in an oversized T-shirt, messy hair, ears drooping, holding a steaming mug of coffee with both hands, sleepy and unusually gentle.",
        "30-39|11_bikeshop|[mirror] In the mirror of a motorbike shop's changing room, trying on a red-and-black racing leather jacket, zipping it up with her free hand, a fierce grin.",
        "30-39|12_kimono|[mirror] In a tall mirror at a kimono rental shop, in an elegant black kimono with red spider lilies, her tail peeking out, surprised at how good she looks, a rare flustered blush.",
        "40-49|13_alley|[candid] Leaning against a neon-lit wall in a back alley at night in a sleek black dress under a leather jacket, exhaling smoke, a smouldering, provocative look straight at the viewer.",
    ],
}

# The top level (50, the app's maximum): one bolder, glamorous photo each, drawn with SPICY_RULES instead of the wholesome rule.
SPICY_RULES = "Glamorous and alluring, an adult woman, non-explicit, no nudity."
SELFIES_50 = {
    "yuki": ["50-59|14_moonpool|[candid] At a moonlit rooftop pool at night, in an elegant black one-piece swimsuit with a sheer black lace cover-up, sitting on the pool edge with her feet in the water, her face in a natural three-quarter view as she looks down at her reflection in the water, a confident, seductive smile."],
    "akane": ["50-59|14_silk|[mirror] In a steamy hotel bathroom mirror, in a red silk robe tied at the waist, her hair down and damp, her glasses in her free hand, a bold, seductive look at her reflection."],
    "saki": ["50-59|14_beachclub|[candid] At a luxury beach club at sunset, in a stylish white bikini with a sheer sarong around her hips, lounging on a daybed, sunglasses lowered, a sultry wink."],
    "kiko": ["50-59|14_neon|[above] Lying on dark sheets in a room lit by red neon, in a black lace slip dress with a modest neckline, a slender figure with a small, flat chest, her hair fanned out and her tail curled around her, a smouldering, inviting smirk."],
}

# Level 60 (the app's maximum): undressing a little — an outer layer coming off, always something underneath, never nudity.
RULES_60 = "Suggestive but non-explicit: an adult woman, always a top or camisole underneath, can be see-through, can underwear shown."
SELFIES_60 = {
    "yuki": ["60-69|15_ribbon|[candid] In her candlelit bedroom, slowly untying the ribbon of her black gothic jacket as it slips open over a plain black high-necked tank top, a slender figure with a small, flat chest, a high round neckline with no cleavage at all, looking at the viewer with a shy, daring smile."],
    "akane": ["60-69|15_tie|[candid] In her dim room late at night, tugging her red tie loose with her white blouse unbuttoned over a plain fitted grey high-necked T-shirt, a slender figure with a small, flat chest, a high round neckline with no cleavage at all, her glasses slightly fogged, a nervous but daring look."],
    "saki": ["60-69|15_cardigan|[candid] In her bedroom by a warm lamp, letting her oversized beige cardigan slide off her shoulders over a plain white high-necked tank top, a slender figure with a small, flat chest, a high round neckline with no cleavage at all, a playful, teasing smile."],
    "kiko": ["60-69|15_leather|[candid] In her apartment at night under red neon light, shrugging her leather jacket off her shoulders, a black tank top underneath, her tattoos showing, a provocative smirk."],
}

POSES |= {
    ("yuki", "costume_00"): "a theatrical 'princess of darkness' pose: standing, one hand raised dramatically to her forehead, the other lifting the edge of her skirt "
                            "in a small haughty curtsy, a smug little smile; black tights and black strap shoes",
    ("yuki", "costume_10_pumpkin-pierrot"): "crouching playfully beside a big carved jack-o'-lantern and peeking over it with a mischievous grin, one hand raised like "
                                            "a claw going 'boo'; black tights and chunky shoes",
    ("yuki", "costume_20_street"): "standing with her hands in the pockets of black cargo trousers, leaning slightly forward with attitude, head tilted, a cool cheeky "
                                   "smirk; chunky black sneakers",
    ("akane", "costume_00"): "standing proudly, hugging a stack of thick books to her chest with one arm and pushing up her glasses with one finger, a confident "
                             "know-it-all smile; a dark pleated knee-length skirt, knee socks and loafers",
    ("akane", "costume_10_witch"): "sitting sideways on a flying broomstick in mid-air, reading an open glowing spellbook, her legs crossed modestly under the long "
                                   "robe, the hat tilted; ankle boots",
    ("akane", "costume_20_lab"): "leaning forward excitedly, holding up a round flask of bubbling glowing green liquid in one hand and a clipboard in the other, a "
                                 "small puff of smoke above the flask; dark trousers and sneakers",
    ("saki", "costume_00"): "a confident fashion-model pose: one hand on her hip, the other holding up her smartphone for a mirror selfie, a few shopping bags on "
                            "her wrist, a playful wink; a plaid skirt and loafers",
    ("saki", "costume_10_beach"): "sitting relaxed on a big inflatable swim ring with her legs crossed, leaning back on one hand, holding a tropical drink with a "
                                  "straw, smiling; sandals",
    ("kiko", "costume_00"): "leaning back with her arms crossed, a lit cigarette between two fingers, one boot propped back, a direct challenging smirk; "
                            "light-blue jeans, black boots, a big fluffy dark fox tail",
    ("kiko", "costume_10_leather"): "crouching low in a tough delinquent-style squat, elbows on her knees, a cigarette in one hand, looking up with a bold direct "
                                    "grin; black skinny jeans, black boots, a big fluffy dark fox tail",
    ("kiko", "costume_20_kitsune"): "standing elegantly half-turned, holding a glowing red paper lantern in one hand and a long thin kiseru pipe in the other, "
                                    "a sly fox smile; wooden geta sandals, a big fluffy dark fox tail curled behind her",
}

# Mika: a lazy, sleepy Gen Z street troublemaker (kanedere): no interests, wants a rich guy.
EMOTIONS["mika"] = {
    "smile": "a lazy, lopsided half-smile with half-lidded sleepy eyes, head tilted, one hand in her jacket pocket",
    "angry": "annoyed and done: an exaggerated eye-roll, lips pushed into a pout, arms crossed loosely",
    "sad": "a sulky, bored sadness: chin propped on one fist, eyes lowered, a small sigh",
    "excited": "money eyes: sparkling eyes with a glint, mouth open in delight, both hands rubbing together greedily",
    "shock": "an 'are you serious?!' face: eyebrows shot up, jaw dropped, one hand slapped to her forehead",
    "thinking": "scheming a prank: a sly side glance, chewing on her thumbnail, a crooked grin",
    "smug": "a cocky smirk, chin up, two fingers flicking a salute off her brow",
    "shy": "caught off guard and refusing to admit it: a faint blush, eyes darting to the side, pulling the collar of her top or jacket up over her mouth with one hand",
    "fear": "spooked: shoulders hunched, eyes wide, both hands clutching the collar of her jacket",
    "sleepy": "barely awake: eyes closed, head drooping, a huge yawn covered by the back of ONE hand, her other hand out of sight in her pocket",
    "playful": "sticking her tongue out with one eye pulled down ('bleh'), a cheeky menace grin",
    "cool": "deeply unimpressed: a flat, half-lidded stare, chewing gum, no gesture",
    "love": "lovestruck by something expensive: heart-shaped highlights in her eyes, hands pressed together under her chin, a dreamy grin",
    "laugh": "a loud, mocking cackle: head thrown back, one hand pointing at the viewer",
    "cry": "fake crying for sympathy: a theatrical pout, one hand wiping a single tear, peeking to see if it works",
    "confused": "'huh?': one eyebrow raised high, head tilted, lips pulled to one side",
    "determined": "fired up for once: a determined grin, pushing up one sleeve, the other fist clenched",
    "wave": "a lazy wave with only two fingers, barely lifting her hand, a sleepy smile",
    "drink": "sipping an iced coffee through a straw from a big plastic cup, eyes half closed in bliss",
    "gaming": "playing a mobile game on her smartphone held sideways in both hands, a bored but focused look",
}
COSTUMES[("mika", "costume_10_hoodie")] = ("Leniwy dzień", "Lazy day",
    "Masz na sobie za dużą szarą bluzę z kapturem zsuniętą z jednego ramienia na czarnym topie, bez czapki, włosy spięte w niedbały kok, słuchawki na szyi.",
    "You wear an oversized grey hoodie slipping off one shoulder over a black top, no cap, your hair in a messy bun, headphones around your neck.",
    "A lazy day at home: an oversized grey hoodie slipping a little off one shoulder over a black fitted top, NO cap, her brown hair tied up in a messy "
    "bun with loose strands, white earbuds-style headphones around her neck, extra sleepy eyes.")
COSTUMES[("mika", "costume_20_glam")] = ("Randka z bogaczem", "Rich-date glam",
    "Masz na sobie błyszczącą srebrną satynową bomberkę na czarnej sukience z wysokim dekoltem, złote łańcuchy, okulary przeciwsłoneczne na głowie i błyszczące usta.",
    "You wear a shiny silver satin bomber over a black high-necked dress, gold chains, sunglasses pushed up on your head and glossy lips.",
    "A 'dating a rich guy' glam look: a shiny silver satin bomber jacket over a black high-necked dress, chunky gold chain necklaces, designer-style "
    "sunglasses pushed up into her hair (no logos), glossy lips, no cap.")
COSTUMES[("mika", "costume_30_puffer")] = ("Srebrna puchówka", "Silver puffer",
    "Masz na sobie rozpiętą, błyszczącą srebrną puchową kurtkę-bomberkę na białym mikro topie z wysokim dekoltem (widać lekki kaloryfer), bez czapki, włosy rozpuszczone, cienki srebrny łańcuszek.",
    "You wear an open, shiny silver puffer bomber jacket over a tiny white micro top with a high neckline (a hint of abs), no cap, your hair down, a thin silver chain.",
    "A silver street look: an open, glossy metallic-silver puffer bomber jacket with a high collar, over a tiny fitted white micro crop top with a high round "
    "neckline (no cleavage), NO cap, her brown hair down and messy, a thin silver chain; a different, flashier look than her olive bomber.")
POSES[("mika", "costume_00")] = ("a bored street pose: leaning back as if against a wall, one sneaker propped back, hands in her bomber pockets, chewing gum and "
                                 "blowing a pink bubble; black shorts, striped white knee socks, white sneakers, a slightly toned stomach under the cropped top")
POSES[("mika", "costume_10_hoodie")] = "slumped sideways as if on a beanbag, scrolling her phone with one hand, a huge yawn; grey sweatpants, fluffy socks"
POSES[("mika", "costume_30_puffer")] = ("walking towards the viewer with a swagger, hands in the pockets of the open silver puffer, "
                                        "a slightly toned stomach under the micro top, a cocky grin; baggy black cargo trousers, white sneakers")
POSES[("mika", "costume_20_glam")] = ("posing like an influencer: one hand on her hip, the other holding a small glossy shopping bag over her shoulder, a smug "
                                      "smirk; black heeled ankle boots")
SELFIES["mika"] = [
    "00-09|01_bed|[above] At noon, still in bed under a tangled duvet, her hair a mess, a sleep mask on her forehead, squinting at the camera, 'don't talk to me'.",
    "00-09|02_delivery|[arm] At her apartment door, holding a big paper bag of food delivery, wearing an oversized T-shirt and shorts, a satisfied, lazy grin.",
    "00-09|03_party|[candid] Sneaking past a velvet rope into a crowded rooftop party at night, in her olive bomber and cap, a finger to her lips, mischievous.",
    "00-09|04_mall|[mirror] In a luxury boutique's mirror, holding up an expensive bag she obviously can't afford, in her bomber and crop top, a scheming look.",
    "00-09|05_bench|[candid] Sprawled across a park bench in the sun, asleep, her cap over her face, one sneaker dangling, a can of iced coffee on the ground.",
    "10-19|06_class|[arm] At the back of a lecture hall, hiding behind a laptop and pretending to take notes, in a grey hoodie, a bored eye-roll at the camera.",
    "10-19|07_konbini|[candid] In a convenience store at 3 a.m., studying the most expensive snacks, a basket on her arm, in a tracksuit, deeply serious.",
    "10-19|08_skate|[tripod] At a graffiti-covered skatepark at sunset, sitting on the edge of a ramp with a skateboard beside her, in a bomber and knee socks, a cocky peace sign.",
    "10-19|09_taxi|[arm] In the back seat of a taxi at night with city lights streaking past, wearing gold chains, a smug smirk, 'not paying for this one'.",
    "20-29|10_couch|[above] Lying on a couch in the dark lit by a TV, a bowl of chips on her stomach, in a hoodie, a sleepy, contented smile.",
    "30-39|11_fittingroom|[mirror] In a fitting-room mirror trying on a sparkly going-out top with a high neckline, hair down, a satisfied, cocky smile.",
    "30-39|12_elevator|[mirror] In the mirrored elevator of a fancy hotel, in her silver satin bomber, sunglasses on, pretending she lives there.",
    "40-49|13_rooftop|[candid] Leaning on a rooftop railing at night above city lights, in a black dress under her bomber, looking back with a lazy, flirty smile.",
]
SELFIES_50["mika"] = ["50-59|14_pool|[candid] At a luxury hotel pool at sunset, in a sporty black bikini top and high-waisted swim shorts, a slightly toned stomach, lounging on a sunbed with oversized sunglasses, a sultry, smug smile."]
SELFIES_60["mika"] = ["60-69|15_bomber|[candid] In her messy room at night lit by neon strip lights, shrugging her olive bomber jacket off her shoulders over a plain white high-necked tank top, a slender figure with a small, flat chest, no cleavage, a daring, sleepy smirk."]


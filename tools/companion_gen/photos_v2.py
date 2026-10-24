"""Her chat photos for the levels (companion_import/photos/<key>/<band>/<stem>.png + .txt, res/companion/README.md):
she in anime style, the place a real photo (unholy_anima_zdjecie_v2_real + her LoRA + "Anime in real"), 1536x960.

    python tools/companion_gen/photos_v2.py run [key] [--only 01,05]    draw them -> ComfyUI output/photos_v2/<key>/<band>/<stem>_*.png
    python tools/companion_gen/photos_v2.py install [key]               newest of each -> <COMPANION_IMPORT>/companion_photos/<key>/<band>/
                                                                       (the old band folders moved to companion_import/backups/)
Workflows to run by hand: ComfyUI workflows/launchme/<key>/zdjecia/<band>_<stem>.json (written by run)."""
import argparse, importlib.util, pathlib, shutil, sys, time

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("feed_v2", HERE / "feed_v2.py")
feed = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(feed)
wf, saki = feed.wf, feed.saki

SIZE = (1536, 960)        # What the app gets (16:10, like the photos before).
LATENT = (1216, 768)      # Drawn at this, hires x1.5 = 1824x1152, then fit to SIZE.
PHOTOS_DIR = feed.IMPORT / "companion_photos"
ANIME_IN_REAL = (feed.ANIME_IN_REAL[0], 0.6, feed.ANIME_IN_REAL[2])  # Lighter than in the details: her LoRA keeps her face.

# band: [(stem, outfit, shot + pose + mood, place, what it shows - her <stem>.txt)]
PHOTOS = {
    "saki": {
        "00-09": [
            ("01_shopping", "oversized denim jacket, white top, pleated skirt",
             "cowboy shot, posing like a model with shopping bags in both hands, confident smile, looking at viewer",
             "busy fashionable shopping street in Paris, Rue Saint-Honore, sunny afternoon",
             "A photo she took of herself with her phone on a tripod. On a fashionable Paris shopping street in an oversized denim jacket and a pleated skirt, posing like a model with shopping bags in both hands, confident."),
            ("02_beauty", "chic cream silk blouse",
             "selfie, upper body, showing a lipstick swatch on the back of her hand to the camera, excited smile",
             "department store beauty counter in Paris, bright lights, cosmetic displays",
             "A selfie she took herself. At a Paris department-store beauty counter in a cream silk blouse, showing a lipstick swatch on the back of her hand to the camera, thrilled."),
            ("03_nails", "soft pink cardigan",
             "upper body, showing freshly painted pastel nails to the camera with both hands raised, happy smile",
             "nail salon with a pastel interior, shelves of nail polish bottles",
             "A selfie at the nail salon: in a soft pink cardigan, showing off her freshly painted pastel nails with both hands, chatting happily."),
            ("04_parfait", "floral sundress",
             "selfie, upper body, sitting at a cafe table, winking, peace sign",
             "trendy Paris cafe, an iced matcha latte and a tall strawberry parfait on the table, daylight from the window",
             "A selfie she took herself. In a trendy Paris cafe with an iced matcha latte and a tall strawberry parfait on the table, wearing a floral sundress, winking."),
            ("05_seaside", "white linen shirt, denim shorts, sunglasses on head",
             "full body, candid, jumping on the seaside promenade with arms spread wide, laughing, linen shirt fluttering, wind in her hair, dynamic pose, motion",
             "seaside promenade in Nice at sunset, the sea behind her, golden light",
             "A photo someone took of her. On the seaside promenade in Nice at sunset, caught mid-jump with her arms spread wide, her linen shirt and hair flying in the wind, laughing."),
        ],
        "10-19": [
            ("06_skincare", "fluffy white bathrobe, hair headband",
             "upper body, applying under-eye patches with both hands, playful pout, looking at viewer",
             "bright bathroom vanity full of skincare bottles, mirror, morning light",
             "A photo she took of herself with her phone on a tripod. At her bathroom vanity in a fluffy robe and a headband, applying eye patches under her eyes with both hands, a playful pout."),
            ("07_brother", "cosy beige sweater",
             "selfie, upper body, rolling her eyes at the camera, thumb pointing over her shoulder",
             "living room, a gaming desk with glowing RGB lights and a big monitor behind her",
             "A selfie she took herself. In the living room in a cosy sweater, pointing over her shoulder at her brother's gaming corner glowing with RGB lights, rolling her eyes at the camera."),
            ("08_closet", "white tank top, open beige cardigan",
             "cowboy shot, holding up two outfits on hangers, one in each hand, puzzled look",
             "walk-in closet full of clothes and shoes, warm light",
             "A photo someone took of her. In a walk-in closet full of clothes, holding up two outfits on hangers, one in each hand, deciding, puzzled."),
            ("09_balcony", "oversized cream knit sweater",
             "upper body, leaning on a railing, sipping strawberry milk through a straw, dreamy faraway look",
             "Paris apartment balcony at night with fairy lights, rooftops and the lit Eiffel Tower in the distance",
             "A photo someone took of her. On her Paris balcony at night with fairy lights, leaning on the railing in an oversized knit sweater, sipping strawberry milk, a dreamy faraway look."),
        ],
        "20-29": [
            ("10_night", "silky pastel pink pyjama set, sleep mask on forehead",
             "selfie from above, lying in bed on her back, hugging a pillow, blowing a kiss, one eye closed",
             "bedroom at night, soft bedside lamp, white sheets",
             "A selfie from above, lying down. In bed in a silky pastel pyjama set, a sleep mask on her forehead, hugging a pillow, blowing a goodnight kiss."),
            ("11_towel", "white bath towel wrapped around her body, wet hair in a small towel turban",
             "upper body, asleep on top of the bed after a shower, lying on her side, eyes closed, peaceful sleeping face, one hand under her cheek",
             "bright bedroom in the afternoon, white sheets, soft daylight from the window",
             "A photo someone took of her. Fast asleep on top of the bed straight after a shower, wrapped in a white towel with her hair in a towel turban, one hand under her cheek."),
        ],
    },
    "kiko": {
        "00-09": [
            ("01_garage", "black leather riding jacket, black jeans",
             "cowboy shot, leaning against a black sport motorbike, helmet under her arm, fanged smirk, looking at viewer",
             "underground parking garage at night, flickering fluorescent lights, concrete pillars",
             "A photo she took of herself with her phone on a tripod. In an underground parking garage at night, leaning against her black motorbike in a leather riding jacket, helmet under her arm, a bold fanged smirk."),
            ("02_ramen", "oversized charcoal hoodie",
             "upper body, sitting at a counter, slurping ramen with chopsticks, eyes closed, blissful",
             "tiny steamy ramen shop at 3 a.m., paper lanterns, noren curtain, wooden counter",
             "A photo someone took of her. At 3 a.m. at the counter of a tiny steamy ramen shop, in an oversized charcoal hoodie, slurping noodles with her eyes closed in bliss."),
            ("03_arcade", "black t-shirt, open red flannel shirt",
             "selfie, upper body, peace sign, cocky grin, small fang",
             "crowded neon-lit retro arcade, fighting game cabinets glowing red and violet",
             "A selfie she took herself. In a neon-lit retro arcade among glowing fighting-game cabinets, black T-shirt under an open red flannel shirt, a cocky peace sign after a win."),
            ("04_rooftop", "oversized black bomber jacket",
             "cowboy shot, sitting on a rooftop ledge, wind in her long hair, looking at the city, lazy distant look",
             "rooftop at night above a neon city skyline, Tokyo",
             "A photo someone took of her. On a rooftop at night above a neon skyline, sitting on the ledge in an oversized bomber jacket, the wind in her long hair, a lazy, distant look."),
            ("05_shrine", "white hoodie, fox mask on side of head",
             "selfie, upper body, sly smile, one eye closed",
             "rows of red torii gates at Fushimi Inari shrine at dusk, lanterns",
             "A selfie she took herself. Among the red torii gates of an Inari shrine at dusk, in a white hoodie with a fox mask on her head, a sly smile."),
        ],
        "10-19": [
            ("06_tattoo", "black tank top",
             "upper body, sitting in a tattoo chair, showing a fresh small fox tattoo on her forearm under clear film, grinning with gritted teeth",
             "tattoo studio, flash sheets on the wall, warm lamps",
             "A photo someone took of her. In a tattoo studio in a black tank top, showing off a fresh little fox tattoo on her forearm, grinning through gritted teeth."),
            ("07_livehouse", "black tank top, studded belt",
             "upper body, both fists raised, shouting along, excited",
             "small live house with colourful stage lights, a punk band blurred on stage behind her",
             "A photo she took of herself with her phone on a tripod. In a small live house with a punk band blurred behind her, in a black tank top and a studded belt, both fists up, shouting along."),
            ("08_konbini", "black track jacket",
             "selfie, upper body, holding a can of coffee, tired smirk",
             "Japanese convenience store at 2 a.m., bright fluorescent lights, shelves of snacks",
             "A selfie she took herself. In a convenience store at 2 a.m. in a track jacket, holding a can of coffee, a tired smirk."),
            ("09_couch", "loose grey t-shirt, ripped jeans",
             "selfie from above, lying on a couch, an arcade fight stick resting on her stomach, smug winner's grin",
             "small messy apartment, beat-up couch, evening lamp light",
             "A selfie from above, lying down. On a beat-up couch in her apartment, an arcade fight stick on her stomach, a smug winner's grin."),
        ],
        "20-29": [
            ("10_dawn", "oversized white t-shirt",
             "upper body, messy hair, fox ears drooping, holding a steaming mug of coffee with both hands, sleepy gentle smile",
             "small apartment balcony at dawn, pink sky over city rooftops",
             "A photo someone took of her. On her balcony at dawn in an oversized T-shirt, messy hair and drooping ears, a steaming mug of coffee in both hands, sleepy and unusually gentle."),
            ("11_back", "bare back, black low-rise jeans, full back tattoo of foxes and peonies in black and orange covering her whole back",
             "cowboy shot, from behind, standing with her back to the camera, long hair swept over one shoulder, looking back over her shoulder, smirk, showing off her back tattoo",
             "dim apartment lit by warm evening light through the blinds",
             "A photo someone took of her. Standing with her bare back to the camera, her long hair swept over one shoulder to show the whole back tattoo of foxes and peonies, glancing back with a smirk."),
        ],
    },
    "akane": {
        "00-09": [
            ("01_library", "beige cardigan, white blouse, black pleated skirt",
             "cowboy shot, standing on tiptoe, pulling a book from a high shelf, satisfied smile",
             "grand old university library in Krakow, tall wooden bookshelves",
             "A photo she took of herself with her phone on a tripod. Between tall wooden bookshelves of an old Krakow university library, in a beige cardigan, pulling a book from a high shelf on tiptoe, satisfied."),
            ("02_planetarium", "navy sweater",
             "selfie, upper body, pointing up at the projected stars, awestruck, open mouth",
             "planetarium dome with stars and constellations projected above, dark",
             "A selfie she took herself. Under a planetarium dome full of projected stars, in a navy sweater, pointing up at a constellation, awestruck."),
            ("03_park", "camel trench coat, red scarf",
             "upper body, sitting on a park bench, reading a book, a thermos of tea beside her, peaceful, absorbed",
             "Planty park in Krakow in autumn, red maple leaves",
             "A photo someone took of her. On a bench in Krakow's Planty park under red maples, in a camel trench coat, reading with a thermos of tea beside her, peaceful and absorbed."),
            ("04_lecture", "white shirt, red tie",
             "cowboy shot, holding chalk in one hand, other hand on her hip, smug smile, looking at viewer",
             "empty lecture hall, blackboard full of physics formulas behind her",
             "A selfie she took herself. In an empty lecture hall in front of a blackboard full of formulas, white shirt and red tie, chalk in one hand, the other on her hip, smug."),
            ("05_bookcafe", "red knit sweater, red-framed glasses",
             "selfie, upper body, glasses fogged up from the tea steam, laughing at herself",
             "cosy bookstore cafe in Kazimierz, a cup of tea and a slice of strawberry cake on the table, shelves of books",
             "A selfie she took herself. In a bookstore cafe in Kazimierz with tea and strawberry cake, in a red knit sweater, her glasses fogged up from the steam, laughing at herself."),
        ],
        "10-19": [
            ("06_desk", "big grey hoodie, red-framed glasses, high ponytail",
             "upper body, writing with a pen at a desk, a smudge of ink on her cheek, yawning",
             "desk late at night lit by a desk lamp, piles of notes",
             "A photo someone took of her. At her desk late at night under a desk lamp, piles of notes around her, in a big grey hoodie, a smudge of ink on her cheek, yawning."),
            ("07_telescope", "warm blanket wrapped around her shoulders",
             "upper body, beside a telescope, glancing at the camera, excited smile",
             "rooftop at night under a starry sky over Krakow",
             "A photo she took of herself with her phone on a tripod. On a rooftop at night beside her telescope, wrapped in a blanket, glancing at the camera with excitement."),
            ("08_baking", "white apron over a red sweater",
             "upper body, carefully pouring milk into a measuring jug, flour on her face, extremely serious",
             "home kitchen, flour and bowls on the counter, daylight",
             "A photo someone took of her. In her kitchen doing a 'scientific' baking experiment, flour on her face, an apron on, pouring milk into a measuring jug, extremely serious."),
            ("09_floor", "soft lilac sweater, red-framed glasses",
             "selfie from above, lying on the floor among scattered open books, glasses askew, giggling",
             "wooden floor of a flat full of books",
             "A selfie from above, lying down. On the floor among scattered open books, in a soft lilac sweater, her glasses askew, giggling."),
        ],
        "20-29": [
            ("10_bedread", "pyjamas with tiny planets, red-framed glasses pushed up on her head",
             "upper body, sitting up in bed reading a book with a small book light, sleepy smile",
             "bedroom late at night, dark, a small warm book light",
             "A photo someone took of her. Sitting up in bed late at night reading by a small book light, in pyjamas with tiny planets, glasses pushed up on her head, sleepy."),
            ("11_ladder", "white blouse, loose black wide-leg trousers, red ribbon in her hair",
             "full body, from below, from behind, climbing a wooden library ladder, reaching for a book on a high shelf, looking back down at the camera over her shoulder, teasing smile",
             "her private home library, floor-to-ceiling wooden bookshelves full of old books, a rolling ladder, warm lamps",
             "A photo someone took of her from below. Climbing the rolling ladder of her private home library to reach a book on the top shelf, glancing back down over her shoulder with a teasing smile."),
        ],
    },
    "mika": {
        "00-09": [
            ("01_bed", "oversized white t-shirt, sleep mask on forehead",
             "selfie from above, lying in bed under a tangled duvet, messy hair, squinting at the camera, smug pout",
             "bedroom at noon, sunlight through the curtains, white sheets",
             "A selfie from above, lying down. At noon, still in bed under a tangled duvet, messy hair and a sleep mask on her forehead, squinting at the camera - 'don't talk to me'."),
            ("02_delivery", "oversized pink t-shirt, grey sweat shorts",
             "upper body, at her apartment door holding a big paper bag of food delivery, satisfied smug grin",
             "apartment doorway, hallway light",
             "A selfie she took herself. At her apartment door with a big paper bag of food delivery, in an oversized pink T-shirt, a satisfied, smug grin."),
            ("03_party", "pink varsity jacket, white crop top, black cap worn backwards",
             "cowboy shot, sneaking past a velvet rope, finger to her lips, mischievous smirk",
             "crowded rooftop party at night in Warsaw, string lights, city skyline",
             "A photo someone took of her. Sneaking past a velvet rope into a crowded Warsaw rooftop party at night, pink varsity jacket and cap, a finger to her lips, mischievous."),
            ("04_mall", "pink varsity jacket, white crop top",
             "mirror selfie, holding up an expensive designer bag, scheming smirk",
             "luxury boutique in Warsaw with a big mirror, designer bags on shelves",
             "A mirror selfie. In a Warsaw luxury boutique's mirror, holding up an expensive bag she obviously can't afford, a scheming smirk."),
            ("05_bench", "black cap worn backwards, pink hoodie, baggy jeans, chunky sneakers, black sunglasses pushed up on her cap",
             "full body, sitting on top of the backrest of a park bench with her feet on the seat, leaning forward on her knees, sipping an iced coffee through a straw, smug smirk, looking at viewer",
             "Lazienki park in Warsaw on a sunny autumn day, golden trees, dappled sunlight",
             "A photo someone took of her. Perched on top of a bench's backrest in Lazienki park with her sneakers on the seat, sipping an iced coffee through a straw, a smug smirk."),
        ],
        "10-19": [
            ("06_class", "oversized grey hoodie",
             "selfie, upper body, hiding behind a laptop, rolling her eyes, bored smirk",
             "back row of a university lecture hall in Warsaw",
             "A selfie she took herself. At the back of a lecture hall, hiding behind a laptop and pretending to take notes, in a grey hoodie, a bored eye-roll."),
            ("07_zabka", "black tracksuit",
             "upper body, holding a shopping basket, studying the most expensive snacks, deeply serious",
             "Polish convenience store at 3 a.m., bright lights, snack shelves",
             "A photo someone took of her. In a convenience store at 3 a.m. in a tracksuit, a basket on her arm, studying the most expensive snacks, deeply serious."),
            ("08_skate", "pink varsity jacket, denim shorts, white knee socks",
             "cowboy shot, sitting on the edge of a ramp, skateboard beside her, cocky peace sign, smirk",
             "graffiti-covered skatepark at sunset",
             "A photo she took of herself with her phone on a tripod. At a graffiti-covered skatepark at sunset, sitting on a ramp with a skateboard beside her, a cocky peace sign."),
            ("09_taxi", "black top, gold chains",
             "upper body, sitting in the back seat, head leaning against the car window, looking out of the window, pensive, thoughtful, city lights reflected on her face",
             "back seat of a taxi at night, Warsaw city lights streaking past the window",
             "A photo someone took of her. In the back seat of a taxi at night, her head against the window, watching Warsaw's lights streak past, unusually quiet and thoughtful."),
        ],
        "20-29": [
            ("10_couch", "oversized pink hoodie",
             "selfie from above, lying on a couch, a bowl of chips on her stomach, sleepy contented smirk",
             "dark living room lit by a TV",
             "A selfie from above, lying down. On the couch in the dark lit by the TV, a bowl of chips on her stomach, in a pink hoodie, a sleepy, contented smirk."),
            ("11_upside", "unzipped pink zip-up hoodie over a black sports bra, black shorts, black sunglasses",
             "full body, lying upside down on a couch, legs up over the backrest, head hanging over the edge of the seat, hair falling to the floor, black sunglasses, smug smirk, dynamic pose",
             "bright living room in Warsaw in the afternoon, sunlight from the window",
             "A photo someone took of her. Lying upside down on the couch with her legs over the backrest and her hair falling to the floor, in an unzipped pink hoodie, shorts and black sunglasses, smug."),
        ],
    },
    "yuki": {
        "00-09": [
            ("01_cemetery", "short white bob, black velvet coat, black lace parasol",
             "cowboy shot, black lace parasol over her shoulder, other hand on her hip, cool serious look",
             "Highgate Cemetery in London, moss-covered gravestones, fallen leaves, golden autumn dusk",
             "A photo she took of herself with her phone on a tripod. Among mossy gravestones in Highgate Cemetery at golden autumn dusk, in a black velvet coat, a lace parasol over her shoulder, playing it cool."),
            ("02_cafe", "short white bob, black turtleneck, silver earrings",
             "upper body, holding a whipped cream mocha with both hands, a bit of cream on her lip, wide-eyed, embarrassed, blush",
             "cosy gothic cafe in London with candles and dark wood",
             "A photo someone took of her. In a cosy gothic cafe with candles, in a black turtleneck, holding a whipped-cream mocha, caught with cream on her lip, wide-eyed and embarrassed."),
            ("03_records", "short white bob, black hoodie",
             "upper body, flipping through vinyl records with both hands, absorbed, tiny smile",
             "vinyl record shop aisle in Camden, London, crates of records",
             "A photo someone took of her. In a Camden record shop, flipping through dark album sleeves in a black hoodie, completely absorbed, a tiny smile."),
            ("04_rain", "short white bob, black trench coat",
             "selfie, upper body, holding a clear umbrella, pouting",
             "rainy London street at night, neon reflections on the wet pavement",
             "A selfie she took herself. On a rainy London street at night under a clear umbrella, neon on the wet pavement, in a black trench coat, pouting at the rain."),
            ("05_haunted", "short white bob, black bomber jacket",
             "upper body, hugging a small black plush bat with both arms, trying to look brave but scared",
             "entrance of a haunted house attraction at an amusement park at night, spooky lights",
             "A photo she took of herself with her phone on a tripod. At the entrance of a haunted house, hugging her little plush bat, trying to look brave but clearly scared."),
        ],
        "10-19": [
            ("06_bed", "her natural short black hair, black oversized t-shirt",
             "selfie from above, lying on her bed, sleepy dreamy look, shy smile",
             "bedroom at night with candles, fairy lights and gothic posters",
             "A selfie from above, lying down. On her bed at night among candles and fairy lights, without the wig - her own short black hair - in a black oversized T-shirt, a shy, sleepy smile."),
            ("07_vanity", "short white bob, black lace blouse",
             "upper body, putting on dark plum lipstick in front of a mirror, holding a small compact, concentrated",
             "ornate vintage dressing table in her room",
             "A photo someone took of her. At an ornate vintage dressing table, putting on dark lipstick in the mirror, a compact in her other hand, in a black lace blouse, concentrated."),
            ("08_movie", "her natural short black hair, a blanket over her head like a hood",
             "upper body, sitting on a sofa clutching a bowl of popcorn with both hands, terrified",
             "dark living room lit only by a TV playing a horror film",
             "A photo someone took of her. On the sofa in the dark lit only by a horror film, a blanket over her head like a hood, clutching a bowl of popcorn, terrified."),
            ("09_cocoa", "her natural short black hair, black apron with small bats",
             "selfie, upper body, holding a mug of hot chocolate with marshmallows, pleased smug smile",
             "dim kitchen at night",
             "A selfie she took herself. In a dim kitchen at night in a black apron with little bats, a mug of hot chocolate with marshmallows, a pleased, smug smile."),
        ],
        "20-29": [
            ("10_goodnight", "her natural short black hair, black pyjamas, a small black plush bat under her chin",
             "selfie from above, lying in bed, eyes half closed, shy sleepy smile",
             "bedroom late at night under a black duvet, dim bedside lamp",
             "A selfie from above, lying down. In bed under a black duvet late at night, in black pyjamas, the plush bat tucked under her chin, eyes half closed, whispering goodnight."),
            # Without the wig: her own short black hair (the white bob is her cosplay signature).
            ("11_tattoo", "her natural short black hair, black oversized t-shirt, black shorts, small black bat tattoo on her outer thigh",
             "cowboy shot, sitting on the edge of her bed, pointing at the small bat tattoo on her thigh, shy proud smile, blush",
             "her bedroom in daylight, black sheets, gothic posters",
             "A photo she took of herself with her phone on a tripod. Without the wig, her own short black hair, sitting on the edge of her bed and pointing at the little bat tattoo on her thigh - shy, but proud of it."),
            ("12_nomakeup", "her natural short black hair, no makeup, oversized grey hoodie",
             "selfie, upper body, bare face without makeup, holding a mug of tea with both hands, shy smile, sleepy eyes",
             "small kitchen in the morning, soft daylight from the window",
             "A selfie she took herself. In the morning with no make-up and her own short black hair, in an oversized grey hoodie with a mug of tea, a shy smile - rare proof she's a normal human being."),
            ("13_wigs", "her natural short black hair, black tank top",
             "upper body, sitting at a desk combing a long pink wig on a mannequin head, concentrated, tiny smile",
             "cosplayer's workroom at night, colourful wigs on mannequin heads on the shelves, a sewing machine",
             "A photo someone took of her. In her workroom at night with her own short black hair, combing a long pink wig on a mannequin head, the other wigs lined up on the shelves behind her."),
            ("14_window", "her natural short black hair, black knit cardigan",
             "upper body, sitting on a window seat with her knees up, looking out at the rain, calm soft expression",
             "window seat in a London flat on a rainy day, raindrops on the glass",
             "A photo someone took of her. On the window seat of her London flat on a rainy day, her own short black hair, knees up, watching the rain, calm and soft."),
        ],
    },
}


# From level 30: at home and closer every band - photos she sends only to him, not posts: silly, natural, unguarded.
CLOSER = {
    "saki": {
        "30-39": [
            ("20_facemask", "white sheet face mask on her face, fluffy headband, pastel pink pyjamas",
             "selfie, upper body, sheet face mask, pulling a silly monster face, both hands raised like claws",
             "bathroom at night, soft warm light, mirror",
             "A selfie at night: a sheet face mask on, pulling a silly monster face with her hands as claws - 'don't look, I'm becoming beautiful'."),
            ("21_crepe", "oversized white t-shirt, apron, hair in a messy bun",
             "upper body, holding up a slightly burnt crepe on a spatula, guilty laugh",
             "small Paris apartment kitchen in the morning, a pan on the stove, flour on the counter",
             "A photo she took with the phone propped on the counter: holding up a slightly burnt crepe on the spatula, laughing guiltily - breakfast for you, almost."),
        ],
        "40-49": [
            ("22_hoodie", "oversized men's grey hoodie, sleeves covering her hands, messy hair, no makeup",
             "selfie, upper body, sitting on the bed hugging her knees, sleeves over her hands, sleepy happy smile",
             "bedroom in the morning, rumpled white sheets, sunlight through the curtains",
             "A selfie on a lazy morning: swimming in an oversized grey hoodie that is clearly not hers, sleeves over her hands, sleepy and happy."),
            ("23_dance", "white tank top, pastel pink pyjama shorts, fluffy socks",
             "full body, candid, dancing alone in the living room, singing into a hairbrush like a microphone, eyes closed, dynamic pose",
             "cosy Paris living room in the evening, warm lamps, a record player",
             "A photo someone took of her. Dancing around the living room in pyjamas and fluffy socks, singing into a hairbrush with her eyes closed."),
        ],
        "50-59": [
            ("24_breakfast", "oversized white shirt, messy hair",
             "upper body, sitting cross-legged on the bed with a breakfast tray, holding a croissant out towards the camera, warm smile",
             "bedroom in the morning, a tray with two coffees and croissants, sunlight",
             "A photo she took for you: cross-legged on the bed with a breakfast tray for two, holding a croissant out to the camera - one is yours, come and get it."),
            # 26_bed, 27_drunk: added by hand (install keeps them).
            ("25_movie", "cosy oversized knit sweater, fluffy socks",
             "full body, curled up asleep on the sofa under a blanket, peaceful sleeping face",
             "living room at night, a paused movie on the TV, fairy lights, two mugs on the side table",
             "A photo someone took of her. Fallen asleep on the sofa under a blanket halfway through your movie night, the film still paused."),
        ],
    },
    "kiko": {
        "30-39": [
            ("20_guitar", "oversized black t-shirt, loose black shorts",
             "upper body, sitting cross-legged on the floor playing an acoustic guitar, singing softly, eyes closed, fluffy tail curled around her",
             "messy apartment at night, fairy lights, vinyl records on the floor",
             "A photo she took with the phone on the floor: playing an acoustic guitar for you, singing softly with her eyes closed, her tail curled around her."),
            ("21_bangs", "black tank top, a towel around her shoulders",
             "upper body, cutting her own bangs with scissors in front of a mirror, tongue out in concentration",
             "small bathroom, a mirror, hair clippings in the sink",
             "Cutting her own bangs in the bathroom mirror, tongue out in concentration - it's going great, don't worry."),
        ],
        "40-49": [
            ("22_ramen", "oversized grey hoodie",
             "upper body, sitting on the kitchen floor eating instant ramen straight from the pot with chopsticks, sleepy grin",
             "small kitchen at 4 a.m., lit by the open fridge",
             "Sitting on the kitchen floor at 4 a.m. eating instant ramen straight from the pot, a sleepy grin - she made enough for two."),
            ("23_tail", "white t-shirt, black shorts",
             "upper body, sitting on the bed brushing her own fluffy fox tail in her lap, annoyed pout",
             "bedroom in the morning, sunlight, rumpled sheets",
             "Brushing her fluffy tail on the bed with a pout - the most private photo she will ever send anyone."),
        ],
        "50-59": [
            ("24_rain", "oversized black hoodie, wet hair",
             "upper body, sitting on a balcony floor under the roof edge, holding out a can of beer towards the camera, the other can in her hand, soft tired smile",
             "small balcony at night in the rain, city lights",
             "On her balcony in the rain at night, holding the second can out to the camera - sit with me."),
            ("25_asleep", "oversized white t-shirt",
             "upper body, asleep in bed hugging her own fluffy fox tail like a pillow, fox ears relaxed, peaceful",
             "dark bedroom, moonlight through the window",
             "Asleep hugging her own tail like a pillow, ears relaxed - caught completely off guard."),
            ("26_neon", "red lace lingerie set, open oversized black flannel shirt slipping off her shoulders, small fox tattoo on her forearm",
             "cowboy shot, kneeling on the bed, fluffy fox tail curled behind her, one hand running through her long hair, confident fanged smirk, half-closed eyes, looking at viewer",
             "dim bedroom at night lit by red and violet neon light from the window",
             "A photo she took with the phone on a tripod. Kneeling on the bed in the red neon glow, in red lace and an open flannel shirt slipping off her shoulders, a hand in her hair and a fanged smirk - your turn to say something."),
            ("27_window", "black lace lingerie set, black garter belt, black thigh-high stockings, small fox tattoo on her forearm",
             "cowboy shot, leaning back against a big window, arms raised behind her head, fluffy fox tail, lazy seductive smirk, looking at viewer",
             "high-rise apartment at night, floor-to-ceiling window, neon city lights behind her",
             "A photo she took with the phone on a tripod. Leaning back against her window above the neon city in black lace and stockings, arms behind her head, a lazy smirk - the whole city behind her and she's looking at you."),
            ("28_grease", "black sports bra, grey mechanic coveralls unzipped and tied at the waist by the sleeves, smears of black motor oil and grease on her face, arms and stomach, small fox tattoo on her forearm, sharp black claw-like nails",
             "cowboy shot, sitting on the oily concrete floor leaning back against her motorbike, knees up, both hands raised in a claw pose showing her sharp black nails, tongue sticking out upwards, wild fanged grin, fox ears perked up, fluffy tail on the floor, looking at viewer",
             "dark cluttered workshop at night, lit only by one hanging work lamp, deep shadows, her black sport motorbike half taken apart, tools and parts on the floor",
             "A photo she took with the phone propped on the toolbox. Sitting on the floor of her dark workshop against the half-fixed bike, smeared in motor oil, claws up and tongue out - rawr, come and help her wash it off."),
        ],
    },
    "akane": {
        "30-39": [
            ("20_foam", "white t-shirt, apron, big safety goggles",
             "upper body, a kitchen science experiment erupting colourful foam from a bottle, shocked laugh",
             "home kitchen, colourful foam all over the counter",
             "A kitchen 'science experiment' gone gloriously wrong: colourful foam everywhere, safety goggles on, laughing in shock."),
            ("21_chess", "cosy cardigan, red-framed glasses",
             "upper body, sitting at a chessboard, moving a piece, smug teasing smile, looking at viewer",
             "living room by the window in the evening, a chessboard on a small table, a cup of tea",
             "A game she set up for the two of you: moving her piece with a teasing smile - your move."),
        ],
        "40-49": [
            ("22_asleep", "oversized grey hoodie, red-framed glasses askew",
             "upper body, fallen asleep at her desk, cheek on an open textbook, pen still in her hand, peaceful",
             "desk late at night, desk lamp, piles of notes",
             "A photo someone took of her. Fallen asleep at her desk on an open textbook, glasses askew, the pen still in her hand."),
            ("23_hairdryer", "white bathrobe, very long wet crimson hair",
             "upper body, sitting on the floor drying her very long hair with a hairdryer, hair blowing everywhere, laughing",
             "bathroom with steam on the mirror",
             "Drying her very long hair - it takes forever, she says, so you had better keep her company."),
        ],
        "50-59": [
            ("24_soup", "white blouse with rolled-up sleeves, apron",
             "upper body, holding out a spoon of soup towards the camera to taste, teasing smile",
             "warm home kitchen in the evening, a pot on the stove",
             "Holding a spoon of soup out to the camera with a teasing smile - say 'aah', it's for you."),
            # 26_shibari, 27_devil: added by hand (install keeps them).
            ("25_fort", "pyjamas with tiny planets",
             "full body, lying in a blanket fort made of books and duvets, reading with a flashlight, looking up at the camera, inviting smile",
             "living room at night, a blanket fort, fairy lights",
             "A blanket fort made of books and duvets, and her inside with a flashlight, looking up at you - there's room for two."),
        ],
    },
    "mika": {
        "30-39": [
            ("20_polish", "oversized pink t-shirt",
             "upper body, sitting on the bed painting her nails, nail polish spilled on the duvet, horrified face",
             "bed in the evening, nail polish bottles on the duvet",
             "Painting her nails on the bed and spilling the polish all over the duvet - a horrified face, a photo sent to you as evidence."),
            ("21_mask", "green clay face mask, hair in a towel turban",
             "selfie, upper body, green clay face mask, deadpan stare",
             "bathroom, bright light",
             "A deadpan selfie in a green clay face mask and a towel turban - you are the only one who gets to see this."),
        ],
        "40-49": [
            ("22_fridge", "oversized hoodie, sleep shorts, fluffy slides",
             "full body, standing in front of an open fridge, eating cheese straight from the pack, caught, guilty smirk",
             "dark kitchen at 3 a.m., lit only by the fridge light",
             "Caught at 3 a.m. in front of the open fridge, eating cheese straight from the pack, a guilty smirk."),
            ("23_burrito", "fluffy blanket wrapped around her like a burrito, only her face showing",
             "upper body, wrapped in a blanket burrito on the bed, smug, refusing to get up",
             "bedroom in the morning, sunlight",
             "Wrapped in a blanket burrito with only her face out, smugly refusing to get up - come and get her out yourself."),
        ],
        "50-59": [
            ("24_hoodie", "oversized black men's hoodie, sleeves covering her hands",
             "selfie, upper body, sitting on the bed in the oversized hoodie, sleeves over her hands, sticking her tongue out",
             "bedroom at night, fairy lights",
             "A selfie in your oversized hoodie, sleeves over her hands, tongue out - mine now."),
            ("25_asleep", "pink pyjamas, black sleep mask on her forehead",
             "upper body, asleep on the couch with her phone on her chest, messy hair, peaceful",
             "living room at night, TV glow",
             "A photo someone took of her. Fallen asleep on the couch mid-scroll, the phone on her chest, a sleep mask on her forehead."),
            ("26_pink", "pink lace lingerie set, unzipped oversized pink hoodie slipping off her shoulders",
             "cowboy shot, sitting on the bed with her knees up, finger on her lips, smug teasing smirk, looking at viewer",
             "bedroom at night, pink LED strip glow, fairy lights",
             "A photo she took with the phone on a tripod. On her bed in the pink LED glow, in pink lace with the hoodie sliding off her shoulders, a finger on her lips and a smug smirk - don't tell anyone."),
            ("27_satin", "black satin lingerie set, thin gold chains",
             "selfie from above, lying on her back on the bed, hair spread over the pillow, winking, tongue out, peace sign",
             "bed with white sheets at night, warm bedside lamp",
             "A selfie from above, lying on her back in black satin and gold chains, hair all over the pillow, winking with her tongue out - a goodnight you weren't ready for."),
            ("28_curtain", "wrapped in a sheer white curtain, holding black lace lingerie in her hand, earrings",
             "cowboy shot, standing behind a long sheer white curtain, her body a soft blurred silhouette behind the fabric, only her face and one bare shoulder peeking out from the side of the curtain, a black lace bra dangling from her fingers outside the curtain, smug teasing smirk, looking at viewer",
             "bright bedroom in the morning, sunlight streaming through the sheer curtains, white sheets",
             "A photo she took with the phone on a tripod. Hiding behind the sheer morning curtain, only her face and a shoulder peeking out, black lace dangling from her fingers - looking for this? Come and get it."),
            ("29_friend", "black lace lingerie set, unzipped oversized pink hoodie",
             "selfie, upper body, 2girls, different hairstyles, different faces. Mika (long black hair with pink streaks, purple eyes) lies on her stomach on the bed holding the phone, smirking smugly with a peace sign. "
             "Her best friend looks completely different: short silver-blue bob haircut, tanned skin, freckles, light blue eyes, a white lace lingerie set; "
             "she lies across Mika's back with both arms wrapped around Mika's neck and her chin on top of Mika's head, winking with her tongue out",
             "bedroom at night, pink LED glow, clothes thrown everywhere after a night out",
             "A selfie with her best friend after a night out, cheek to cheek on the bed in lace and an open hoodie, both making kissy faces - she says hi. Mika says she's taken, don't get ideas."),
        ],
    },
    "yuki": {
        "30-39": [
            ("20_armour", "her natural short black hair, black tank top, unfinished cosplay armour made of EVA foam on one arm",
             "upper body, flexing her arm in unfinished foam armour, a hot glue gun in the other hand, proud grin",
             "workroom at night, foam scraps and paper patterns everywhere",
             "Flexing her arm in half-finished foam armour with a glue gun in her other hand, a proud grin - her own hair, at home."),
            ("21_horror", "her natural short black hair, black hoodie",
             "upper body, peeking out from under a blanket at a horror game on the screen, a gamepad in her hands, scared",
             "dark room lit by the TV",
             "Peeking out from under a blanket at a horror game, gamepad in her hands, scared stiff - play the next part with her?"),
        ],
        "40-49": [
            ("22_cookies", "her natural short black hair, black apron with small bats",
             "upper body, holding a tray of bat-shaped cookies, one cookie bitten, shy proud smile",
             "kitchen, flour on the counter",
             "A tray of bat-shaped cookies she baked, one already bitten, a shy proud smile - some are for you."),
            ("23_rainbow", "her natural short black hair, wig cap, a ridiculous rainbow wig on her head",
             "upper body, trying on a ridiculous rainbow wig, laughing, peace sign",
             "her workroom, wigs on mannequin heads behind her",
             "Trying on the most ridiculous rainbow wig in her collection and laughing - not for any cosplay, only for you."),
        ],
        "50-59": [
            ("24_count", "her natural short black hair, no makeup, black oversized t-shirt",
             "selfie, upper body, lying in bed half asleep, holding a small black plush bat next to her face, soft smile",
             "bedroom at night, fairy lights",
             "Half asleep in bed with no make-up, holding the Count, her plush bat, next to her face - goodnight from both of us."),
            ("25_counter", "her natural short black hair, no makeup, oversized white shirt",
             "upper body, sitting on the kitchen counter, one hand holding a mug of coffee, her other hand resting flat on the counter beside her, laughing, morning light",
             "kitchen in the morning, sunlight",
             "Sitting on the kitchen counter in the morning with a coffee and an oversized shirt, laughing at something you said."),
            ("26_lace", "her natural short black hair, black lace lingerie set, sheer black robe slipping off one shoulder, small black bat tattoo on her outer thigh",
             "cowboy shot, sitting on the edge of her bed, legs crossed showing the small bat tattoo on her outer thigh, glancing at viewer over her shoulder, calm cool gaze, shy blush",
             "her bedroom at night, black sheets, candles, fairy lights, gothic posters",
             "A photo she took with the phone on a tripod. On the edge of her bed by candlelight in black lace, a sheer robe slipping off one shoulder, her own short black hair and the little bat on her thigh - a cool look and burning cheeks."),
            # Her own twist: caught in a giant spider web like a gothic fly.
            ("27_web", "short white bob, black spiderweb lace lingerie set, sheer black spiderweb pattern stockings, small black bat tattoo on her outer thigh",
             "cowboy shot, caught in a giant black spider web stretched across the corner of the room, one arm raised and tangled in the web threads, a small black plush spider on her shoulder, deadpan cool stare, slight blush",
             "dark gothic bedroom at night, a giant black rope spider web decoration across the corner, candles, dim violet light",
             "A photo she took with the phone on a tripod. Caught in a giant spider web in the corner of her room, in spiderweb lace and stockings, the white wig on, a plush spider on her shoulder and the bat on her thigh, staring deadpan - she's the prey tonight. Or is she."),
        ],
    },
}
for _key, _bands in CLOSER.items():
    PHOTOS[_key].update(_bands)


def duo(entry):
    return "2girls" in entry[2]


def photo_workflow(info, key, band, entry, seed):
    stem, outfit, doing, place, _ = entry
    c = feed.CHARACTERS[key]
    g = wf.photo_real_workflow(info)
    body, avoid = c["body"], c["avoid"] + feed.no_glasses(key, outfit, doing)
    if "no makeup" in outfit:  # Her bare face: her description's make-up goes, and is avoided.
        body = ", ".join(t for t in body.split(", ") if "eyeliner" not in t and "lipstick" not in t)
        avoid += ", makeup, eyeliner, lipstick, eyeshadow"
    if "natural short black hair" in outfit:  # Yuki without her white bob wig.
        avoid += ", white hair, silver hair, bob wig"
    if "lingerie" in outfit:  # Suggestive, never explicit, and plainly a grown woman.
        body = body.replace("1girl, solo", "1girl, solo, adult woman, mature body, 25 years old")
        avoid += ", child, loli, teen, childlike body, young-looking, topless, see-through nipples"
    if duo(entry):  # A photo with a friend: two girls allowed (the friend described in "doing").
        body = body.replace("1girl, solo", "2girls")
        feed.ui_set(g, "Negatyw (wspolny)", wf.NEGATIVE_PHOTO.replace("multiple girls, 2girls, clone, ", "") + ", 3girls, twins, identical faces")
    lora, strength, trigger = ANIME_IN_REAL
    feed.ui_set(g, "1. Postac", f"{trigger}, " + body)
    feed.ui_set(g, "1a. Stroj", outfit)
    feed.ui_set(g, "1b. Postac", avoid)
    feed.ui_set(g, "2. Co robi", doing)
    feed.ui_set(g, "3. Miejsce", place)
    feed.ui_set(g, "LoRA postaci", c["lora"], "lora_name")
    for field in ("strength_model", "strength_clip"):  # Lighter with a friend: at full strength both get her face.
        feed.ui_set(g, "LoRA postaci", min(c["strength"], 0.5) if duo(entry) else c["strength"], field)
    second = feed.ui_set(g, "LoRA 2", lora, "lora_name")
    second.widgets.update(strength_model=strength, strength_clip=strength)
    second.mode, second.title = 0, "LoRA 2: Anime in real (ona anime, swiat realny)"
    latent = feed.ui_set(g, "768x1280", LATENT[0], "width")
    latent.widgets["height"], latent.title = LATENT[1], f"{LATENT[0]}x{LATENT[1]} (poziom)"
    feed.ui_set(g, "1. przebieg", seed, "seed")
    feed.ui_set(g, "Folder zapisu", f"photos_v2/{key}/{band}")
    feed.ui_set(g, "Zapis: zdjecie", stem, "string_b")
    return g


def run(server, keys, only, bands=None, reroll=0):
    info = wf.http(server, "/object_info")
    for key in keys:
        for band, entries in PHOTOS[key].items():
            if bands and band not in bands:
                continue
            for i, entry in enumerate(entries):
                if only and entry[0][:2] not in only:
                    continue
                seed = feed.CHARACTERS[key]["seed"] + 2000 + int(entry[0][:2]) + 7919 * reroll
                g = photo_workflow(info, key, band, entry, seed)
                feed.write_workflow(g, f"{key}/zdjecia/{band}_{entry[0]}.json")
                saved = saki.check(server, g.prompt(), "1. przebieg", single=not duo(entry))
                print(f"{key} {band} {entry[0]}:", *(f for _, f in saved), flush=True)


def install(keys, bands=None):
    from PIL import Image, ImageOps
    stamp = time.strftime("%Y%m%d-%H%M")
    for key in keys:
        for band, entries in PHOTOS[key].items():
            if bands and band not in bands:
                continue
            target, backup = PHOTOS_DIR / key / band, None
            if target.exists():
                backup = feed.IMPORT / "backups" / f"companion_photos_{stamp}" / key / band
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(target), backup)
            target.mkdir(parents=True)
            stems = {entry[0] for entry in entries}
            if backup:  # Photos added by hand (not in PHOTOS) stay.
                for kept in backup.iterdir():
                    if kept.stem not in stems:
                        shutil.copy2(kept, target / kept.name)
            for stem, *_, caption in entries:
                picture = feed.newest(feed.COMFY / "output" / "photos_v2" / key / band, stem)
                if not picture:
                    print(f"{key}/{band}/{stem}: not drawn yet", flush=True)
                    continue
                ImageOps.fit(Image.open(picture).convert("RGB"), SIZE, Image.LANCZOS).save(target / f"{stem}.png", optimize=True)
                (target / f"{stem}.txt").write_text(caption + "\n", encoding="utf-8")
            print(f"{key}/{band}: {len(entries)} photos", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stage", choices=["run", "install"])
    parser.add_argument("key", nargs="?")
    parser.add_argument("--only", help="photo numbers, e.g. '01,05' (quoted in PowerShell)")
    parser.add_argument("--bands", help="level bands, e.g. '30-39,40-49' (default: all)")
    parser.add_argument("--reroll", type=int, default=0, help="another seed (1, 2, ...) for a picture that came out wrong")
    parser.add_argument("--server", default="http://127.0.0.1:8188")
    args = parser.parse_args()
    keys = [args.key] if args.key else list(PHOTOS)
    bands = set(args.bands.split(",")) if args.bands else None
    if args.stage == "run":
        run(args.server, keys, set(args.only.split(",")) if args.only else None, bands, args.reroll)
    else:
        install(keys, bands)


if __name__ == "__main__":
    main()

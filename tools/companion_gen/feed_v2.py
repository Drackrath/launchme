"""The v2 companions on the local ComfyUI (comfy_workflows.py: the same models and each one's own LoRA everywhere):
Launchfeed photos with realistic backgrounds, entrance poses, bust bases and 20 feelings per costume, and the sprite pack.

    python tools/companion_gen/feed_v2.py <key> [--only 1,5] [--seed N]   feed photos -> output/feed_v2/<key>/<NN>_*.png
    python tools/companion_gen/feed_v2.py install <key>                   newest photo of every scene -> the feed folder (+ .txt)
    python tools/companion_gen/feed_v2.py poses [key] [--only 3,4]        entrance poses -> output/poses_v2/<key>/<costume>/
    python tools/companion_gen/feed_v2.py bases [key] [--only 3,4]        neutral busts -> output/emotions_v2/<key>/<costume>/baza_*
    python tools/companion_gen/feed_v2.py emotions [key] [--only 3,4]     20 feelings from the newest base, same folder
    python tools/companion_gen/feed_v2.py pack [key]                      backgrounds cut out -> <COMPANION_IMPORT>/res/companion/<key>/

pack: the newest picture of each feeling and pose (any flat backdrop colour is keyed out), companion_sprites.py's canvas
(560x560, costume_00 sets the head size the others get), pose.png cropped to her standing on its bottom edge,
character.txt and costume.txt from the
repository. A picture with two faces (two of her) is drawn again with another seed (face detector of
tools/companion_faces.py; needs OpenCV 4 on PYTHONPATH, else the check is skipped)."""
import argparse, copy, importlib.util, json, os, pathlib, re, shutil, sys

HERE = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("comfy_workflows", HERE / "comfy_workflows.py")
wf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(wf)
_spec = importlib.util.spec_from_file_location("saki_v2", HERE / "saki_v2.py")
saki = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(saki)

COMFY = saki.COMFY
IMPORT = pathlib.Path(os.environ.get("COMPANION_IMPORT", pathlib.Path.home() / "Desktop" / "companion_import"))
FEED_DIR = pathlib.Path(os.environ.get("COMPANION_FEED", IMPORT / "companion_feed"))
REPO = HERE.parents[1]
QUALITY = "score_9, score_8, masterpiece, best quality, year 2025, newest, highres, absurdres, 1girl, solo"
NO_GLASSES = "glasses, eyewear, red-framed eyewear, sunglasses"
GLASSES_PRONE = ("akane", "mika")  # Their LoRAs (or habits) add eyewear unless the prompt asks for it.


def no_glasses(key, *texts):
    return "" if key not in GLASSES_PRONE or any("glasses" in t for t in texts) else ", " + NO_GLASSES

CHARACTERS = {
    "saki": dict(body=wf.CHARACTER, lora="Kitagawa_Marin_Anima.safetensors", strength=0.8, avoid="red eyes, pink hair", seed=31000),
    # Mika v2: Akane Ousaki's look (nochekaiser LoRA: black hair with pink, purple eyes, earrings), her own lazy Gen Z clothes.
    # Sassy and smug, never sad or sleepy-looking (the feed was drawn with her old bored face and stays).
    "mika": dict(body=QUALITY + ", akane ousaki, gyaru, beautiful face, long hair, black hair, pink hair, two-tone hair, multicolored hair, "
                 "purple eyes, glossy lips, stylish gyaru makeup, earrings, jewelry, long eyelashes. Clean anime linework, flat",
                 lora="akane-ousaki-s1-anima-lora-nochekaiser.safetensors", strength=0.8,
                 avoid="blonde hair, red eyes, school uniform, sad, gloomy, tired eyes", seed=34000),
    "kiko": dict(body=QUALITY + ", adult woman, tall, slender, small breasts, fox girl, black fox ears, white inner ears, fluffy fox tail, "
                 "black tail with white tip, very long straight navy black hair, blunt bangs, red eyes, half-closed eyes, small fang, sharp confident "
                 "gaze. Clean anime linework, flat", lora="Kitagawa_Marin_Anima.safetensors", strength=0.0,
                 avoid="blonde hair, golden eyes, human ears, multiple tails, large breasts, huge breasts, cleavage", seed=32100),
    "yuki": dict(body=QUALITY + ", young woman, petite, slender, small breasts, pale skin, grey-violet eyes, sharp black eyeliner, plum lipstick, "
                 "beautiful delicate face, famous cosplayer. Clean anime linework, flat", lora="Kitagawa_Marin_Anima.safetensors", strength=0.0,
                 avoid="large breasts, cleavage", seed=35000),
    "akane": dict(body=QUALITY + ", rias gremory, mature female, very long crimson hair, ahoge, elegant, graceful, confident, gentle teasing "
                  "smile. Clean anime linework, flat", lora="Sysdeep Rias - Anima v1.safetensors", strength=0.9,
                  avoid="blonde hair, animal ears, fox ears, cleavage", seed=33000),
}

# (outfit, shot + pose + mood, place, what the photo shows: feed/<key>/<n>.txt, her caption is written from it)
SCENES = {
    # Saki v2's Launchfeed photos come from saki_v2.py (its FEED): one list, so a caption always describes its picture.
    "saki": saki.FEED,
    "kiko": [
        ("black ribbed tank top, rust-orange cargo pants, black combat boots, brown leather jacket folded under her head",
         "full body, lying on park bench, arm behind head, holding cigarette, sleepy, looking at the sky", "empty Tokyo park at dawn, mist, pale sky",
         "Kiko lying on a park bench at dawn, her leather jacket under her head and a cigarette in her hand, watching the sky go pale. "
         "She has not gone home yet."),
        ("black tank top, orange cargo pants, black boots",
         "full body, leaning on black motorcycle, holding cigarette, smoke", "underground parking garage, fluorescent lights, night",
         "Kiko leaning on her black motorcycle in an underground car park at night, a cigarette between her fingers."),
        ("grey tank top, black work pants, tool belt",
         "cowboy shot, repairing motorcycle, holding wrench, grease on cheek, focused", "motorcycle workshop, tools on the wall, warm lights, evening",
         "Kiko in a motorcycle workshop in the evening, wrench in hand and grease on her cheek, working on her bike."),
        ("black strap top, mustard yellow work pants, black boots",
         "full body, sitting on stack of tires, smoking cigarette, tired smile", "garage workshop at night",
         "Kiko taking a smoke break on a stack of tyres in the garage at night, tired but satisfied."),
        ("oversized charcoal knit sweater, black beanie",
         "upper body, eating ramen, slurping noodles, chopsticks, satisfied", "late-night ramen stall in Koenji, steam, red lanterns",
         "Kiko slurping ramen at a late-night stall in Koenji, red lanterns and steam around her. Three a.m. is ramen time."),
        ("mustard yellow cropped bomber jacket, black ribbed tank top, black wide-leg trousers, black platform sneakers",
         "cowboy shot, pressing button on ramen ticket machine, looking back at viewer, smirk", "ramen shop entrance with a ticket vending machine, night",
         "Kiko at a ramen shop's ticket machine at night, looking back with a smirk - she already knows what she wants."),
        ("black haori jacket over orange tank top, black wide pants",
         "full body, walking through red torii gates, looking back over shoulder", "Fushimi Inari shrine torii path at dusk, stone lanterns",
         "Kiko walking through the red torii gates of an Inari shrine at dusk, looking back over her shoulder. Family visit, she says."),
        ("dark navy yukata with orange obi",
         "cowboy shot, holding fox mask and candy apple, smile", "shrine summer festival at night, paper lanterns, food stalls",
         "Kiko at a summer shrine festival at night in a navy yukata, holding a fox mask and a candy apple."),
        ("long grey wool coat, black turtleneck, orange knit scarf",
         "upper body, praying with hands together, closed eyes", "small Tokyo shrine at night, stone lanterns lit",
         "Kiko at a small shrine at night in an orange faux fur coat, hands together and eyes closed for a quiet moment."),
        ("rust-orange suede jacket, black camisole, grey wide-leg jeans, black ankle boots",
         "full body, sitting on park bench, crossed legs, smoking cigarette, looking at viewer", "Tokyo park at night, street lamps, autumn leaves",
         "Kiko on a park bench at night, legs crossed and a cigarette in her hand, autumn leaves under the street lamps."),
        ("cream and mustard striped knit cardigan, black tank top, black jeans, black loafers",
         "cowboy shot, standing, hands in pockets, smirk", "park with yellow ginkgo trees, late afternoon",
         "Kiko under yellow ginkgo trees in the late afternoon, hands in the pockets of an oversized grey coat."),
        ("black tank top, oversized orange faux fur coat slipping off shoulders, black flared pants, black heeled boots",
         "cowboy shot, leaning on rooftop railing, smoking cigarette, wind in hair", "Tokyo rooftop at night, city skyline",
         "Kiko on a rooftop at night, smoking against the railing with the Tokyo skyline behind her and the wind in her hair."),
        ("black track jacket with orange stripes, black track pants, black sneakers",
         "full body, squatting next to vending machines, holding canned coffee, cigarette, looking at viewer", "glowing vending machines on a quiet street at night",
         "Kiko squatting by glowing vending machines on a quiet street at night, canned coffee in one hand and a cigarette in the other."),
        ("black strap top, yellow plaid trousers, black creepers",
         "cowboy shot, playing arcade fighting game, hand on joystick, competitive grin", "Tokyo game center at night, colorful arcade machines",
         "Kiko at a game centre at night, one hand on the joystick of a fighting game cabinet and grinning - she is winning."),
        ("black band tank top, silver chains, ripped black jeans, black studded boots",
         "upper body, rock hand sign, shouting, excited", "small underground punk live house, stage lights",
         "Kiko at a small underground punk gig, shouting with a rock hand sign under the stage lights."),
        ("oversized grey sweatshirt, black biker shorts, black slides",
         "cowboy shot, holding beer can and onigiri, tired smile", "Tokyo convenience store at 3 a.m., bright lights",
         "Kiko at a convenience store at 3 a.m. with a beer and an onigiri, tired smile. Dinner, apparently."),
        ("long black coat, orange turtleneck, black boots",
         "full body, standing on train platform, hands in pockets, waiting, looking at viewer", "Chuo line train platform at night",
         "Kiko waiting on a Chuo line platform at night, hands in the pockets of a long black coat."),
        ("brown leather jacket",
         "upper body, lighting cigarette with lighter, flame glow on face, half-closed eyes", "riverside at night, city lights reflecting on the water",
         "Kiko by the river at night, lighting a cigarette, the flame glowing on her face."),
        ("black cropped tank top, grey joggers, black sneakers",
         "full body, hanging from pull-up bar, doing pull-ups, arms up, sweat, determined", "gym at night, fluorescent lights, weights",
         "Kiko doing pull-ups at the gym late at night, determined and sweating. Nobody spars with her, so she trains alone."),
        ("black strap top, grey cargo pants",
         "upper body, showing new tattoo on upper arm, smirk", "tattoo studio, neon light",
         "Kiko in a tattoo studio showing off a fresh tattoo on her upper arm."),
        ("black band t-shirt, ripped black jeans, black boots",
         "cowboy shot, playing electric guitar, guitar strap, intense expression, head tilted", "small rehearsal room, amplifiers, posters, dim red light",
         "Kiko in a small rehearsal room under red light, playing an electric guitar with an intense look - loud, fast and a little out of tune."),
    ],
    "akane": [
        ("red trench coat, black turtleneck, black pencil skirt",
         "cowboy shot, walking, hair flowing, confident smile, looking at viewer", "Wawel castle and the Vistula boulevards, Krakow, autumn afternoon",
         "Akane walking along the Vistula below Wawel on an autumn afternoon, in a red trench coat, her long crimson hair flowing."),
        ("white blouse with black ribbon",
         "upper body, sitting at cafe table, holding teacup, finger on lips, teasing smile", "old Kazimierz cafe, candles, antique furniture",
         "Akane in an old Kazimierz cafe with a cup of tea, a finger on her lips and a teasing smile."),
        ("cream cardigan, white shirt, red-framed glasses",
         "upper body, reading old book, glasses, focused", "Jagiellonian Library reading room, wooden shelves, green lamps",
         "Akane in the Jagiellonian Library reading room, glasses on, deep in an old book."),
        ("white lab coat over black dress, red-framed glasses",
         "upper body, holding test tube, glasses, smirk", "university physics lab, equipment",
         "Akane in the physics lab in a lab coat and glasses, holding up a test tube with a smirk."),
        ("black velvet dress, black choker",
         "upper body, sitting at table, candles, old grimoire, hand on own cheek, mysterious smile", "candlelit club room with occult symbols, old books",
         "Akane in her science club's room by candlelight, an old grimoire in front of her and a mysterious smile. "
         "Officially they debunk the paranormal."),
        ("emerald green satin blouse, black high-waisted skirt, gold earrings",
         "cowboy shot, pouring tea, low bun, elegant, gentle smile", "elegant club room, afternoon light, porcelain teacups",
         "Akane pouring tea for the club members with full ceremony, her hair in a low bun and a gentle smile."),
        ("burgundy turtleneck, red-framed glasses",
         "upper body, sitting at table with chessboard, chess pieces on the board, moving chess piece, glasses, confident smirk", "chess cafe, evening",
         "Akane in a chess cafe in the evening, glasses on, moving a piece on the board with a confident smirk. Checkmate in three."),
        ("long black coat, red scarf",
         "cowboy shot, leaning on railing, wind in hair, looking back at viewer, teasing smile", "Vistula river boulevards at night, Wawel castle lit up",
         "Akane on the Vistula boulevards at night, leaning on the railing with Wawel lit up behind her, looking back with a teasing smile."),
        ("beige coat, scarf, red-framed glasses",
         "cowboy shot, looking through telescope, glasses, night sky", "rooftop at night with a telescope, starry sky over Krakow",
         "Akane on a rooftop at night with her telescope, glasses on, looking for Saturn's rings over Krakow."),
        ("black blouse, red cardigan",
         "upper body, holding old grimoire, finger to lips, mysterious smile", "antique bookshop, dusty shelves, warm light",
         "Akane in an antique bookshop with a suspiciously convincing grimoire, a finger to her lips."),
        ("white off-shoulder blouse, high-waisted black wide trousers, black heels",
         "full body, standing, waving, hair bun, single hair bun, smile", "Krakow Main Market Square, Cloth Hall, sunny day",
         "Akane in Krakow's Main Market Square on a sunny day, hair up in a bun, waving, the Cloth Hall behind her."),
        ("oversized grey university sweatshirt, red-framed glasses",
         "upper body, sitting at desk, physics notes, glasses, high ponytail, pen in mouth, tired smile", "flat full of books at night, desk lamp",
         "Akane at her desk late at night with physics notes, glasses on, hair in a ponytail and a pen in her mouth."),
        ("black satin halter dress, silver earrings",
         "cowboy shot, dancing in a beam of spotlight, hair flowing, closed eyes, ecstatic smile, haze", "dark nightclub dance floor, single spotlight beam, haze",
         "Akane on a dark club dance floor, caught in a single beam of light, eyes closed and dancing - a side of her the library never sees."),
        ("camel coat, white turtleneck, plaid scarf",
         "cowboy shot, standing, pointing up, explaining, smug", "Torun old town, Copernicus monument",
         "Akane in Torun next to the Copernicus monument, pointing up and explaining something with a smug look. Her hometown."),
        ("red one-piece swimsuit under an open white linen shirt, wide straw sun hat",
         "full body, lying on beach towel, reading book, stack of books beside her, sunglasses on head, smile", "sunny Baltic beach in Sopot, sea, dunes",
         "Akane on a sunny Baltic beach in Sopot, lying on a towel with a stack of books beside her and reading, a straw hat against the sun."),
        ("white shirt, loosened red tie, black vest",
         "upper body, leaning forward, finger on lips, one eye closed, teasing smile", "empty lecture hall after class, sunset light",
         "Akane in an empty lecture hall after class, leaning towards the camera with a finger on her lips and a wink."),
        ("oversized cream knit sweater",
         "upper body, messy bun, holding teacup, steam, gentle smile", "kitchen with porcelain tea set, morning light",
         "Akane in her kitchen in the morning, hair in a messy bun, holding a steaming cup of tea from her grandmother's porcelain."),
        ("grey pinstripe blazer, white blouse, red-framed glasses",
         "cowboy shot, writing equations on chalkboard, glasses, ponytail, looking back over shoulder, smirk", "lecture hall with chalkboard full of equations",
         "Akane at a chalkboard full of equations, glasses on, hair in a ponytail, looking back over her shoulder with a smirk - tutoring time."),
        ("black wig, long straight black hair, gothic black lace dress, black choker",
         "cowboy shot, wearing a black wig, crimson hair peeking out from under the wig, holding candle, mysterious smile, Halloween costume",
         "old Krakow street at night, Halloween, jack-o-lanterns",
         "Akane on Halloween night in a black wig and a gothic lace dress, a candle in her hand - a few crimson strands give her away."),
        ("black turtleneck, long black skirt",
         "cowboy shot, standing in the rain, no umbrella, wet hair, wet clothes, hair sticking to face, looking up at the sky, smile",
         "Kazimierz street at night in heavy rain, warm windows",
         "Akane caught in the rain on a Kazimierz street at night with no umbrella, soaked hair stuck to her face, looking up and smiling anyway."),
    ],
    "yuki": [
        ("short white bob, black cropped turtleneck, black wide-leg trousers, silver chains, black platform boots",
         "full body, standing, hands in pockets, serious, looking at viewer", "studio:white",
         "Yuki in a white studio for a magazine shoot: her signature white bob, a black cropped turtleneck, wide trousers, chains and platform boots, "
         "staring into the lens without a hint of a smile."),
        ("short black hair, oversized grey hoodie, holding a white bob wig",
         "upper body, laughing, closed eyes, open mouth, holding wig", "backstage makeup room, mirror with light bulbs, cosplay props",
         "Yuki backstage with her natural short black hair, holding her white wig and laughing - the dark look is just a costume."),
        ("long green twin tails wig, android costume, white and black bodysuit with panel lines, headset, glowing circuit lines, mechanical arm",
         "cowboy shot, robotic pose, expressionless, blank stare", "studio:black|blue rim light",
         "Yuki as an android: long green twin tails, a white-and-black bodysuit with glowing lines and a mechanical arm, a perfectly blank stare."),
        ("long pink wig, succubus costume, small black horns, bat wings, demon tail, black bodysuit with high collar, long black gloves",
         "cowboy shot, holding whip, tongue out, licking lips, smirk", "studio:black|red light",
         "Yuki as a succubus in a long pink wig, horns, bat wings and a whip, licking her lips with a smirk. Only cosplay, she insists."),
        ("short white bob, military uniform, black tactical vest, cargo pants, combat boots, dog tags, fingerless gloves",
         "full body, holding prop rifle on shoulder, serious, menacing glare", "studio:white",
         "Yuki as a soldier in tactical gear with a prop rifle on her shoulder and a menacing glare."),
        ("long silver wig with a braid, sorceress costume, dark purple robe with gold embroidery, circlet",
         "cowboy shot, casting spell, hand raised, glowing magic circle, open mouth, shouting", "studio:black|purple magic glow",
         "Yuki as a sorceress in a long silver wig and purple robe, casting a spell with a glowing magic circle, mid-shout."),
        ("long purple wig, witch hat, black witch dress, striped stockings, holding broom",
         "full body, standing, holding broom, grin, tongue out", "studio:white",
         "Yuki as a witch: long purple wig, a big hat and striped stockings, holding a broom and sticking her tongue out."),
        ("short white bob, dark fantasy warrior, black leather armor, fur cape",
         "full body, holding large battle axe over shoulder, menacing grin", "studio:black bw",
         "A black-and-white shot of Yuki as a dark fantasy warrior in leather armour and a fur cape, a huge axe over her shoulder, grinning menacingly."),
        ("long red wig, rock idol costume, black leather jacket, plaid skirt, torn black tights",
         "cowboy shot, playing electric guitar, shouting, open mouth, energetic", "studio:black|stage spotlight",
         "Yuki as a rock idol in a long red wig, playing an electric guitar and shouting under a stage spotlight."),
        ("short white bob, police officer costume, black police uniform, police cap, aviator sunglasses",
         "cowboy shot, dangling handcuffs from finger, sunglasses lowered, smirk", "studio:white",
         "Yuki in a police costume, aviators pushed down her nose, dangling a pair of handcuffs from one finger with a smirk."),
        ("long black wig, vampire countess costume, red and black gothic gown, high collar cape",
         "upper body, fangs, holding wine glass, menacing smile", "studio:black bw",
         "A black-and-white portrait of Yuki as a vampire countess in a high-collared cape, fangs out, a glass in her hand."),
        ("long blonde wig, angel costume, white feather wings, halo, white dress",
         "upper body, praying, serene, closed eyes", "studio:white|soft high key light",
         "Yuki as an angel in a long blonde wig, white wings and a halo, eyes closed and serene - the exact opposite of her usual look."),
        ("short white bob, cyberpunk jacket with neon trim, LED visor pushed up, techwear",
         "cowboy shot, peace sign, one eye closed, tongue out", "studio:black|neon pink and blue lights",
         "Yuki in cyberpunk techwear under pink and blue neon, a peace sign, a wink and her tongue out."),
        ("short white bob, black blazer, white shirt, black tie, silver rings",
         "upper body, portrait, arms crossed, confident smirk, looking at viewer, magazine cover composition, empty space at the top", "cover:white",
         "Yuki on the cover of a cosplay magazine: white bob, black blazer and tie, arms crossed and a confident smirk."),
        ("long pink wig, black and pink gothic lolita dress, bonnet, holding parasol",
         "cowboy shot, holding parasol, laughing, open mouth, magazine cover composition, empty space at the top", "cover:black",
         "Yuki on another magazine cover in a pink-and-black gothic lolita dress and a long pink wig, laughing with a parasol."),
        ("long blue wig in a high ponytail, kunoichi costume, black ninja outfit, red scarf",
         "full body, dynamic pose, holding katana, serious", "studio:white bw",
         "A black-and-white action shot of Yuki as a kunoichi with a long ponytail wig, katana drawn, dead serious."),
        ("short white bob, witch cosplay, big witch hat, black cape",
         "cowboy shot, posing for photos, peace sign, laughing", "cosplay convention hall, colorful booths, camera flash light",
         "Yuki at a cosplay convention in her witch costume, laughing with a peace sign in the flash of a camera."),
        ("short black hair, oversized band t-shirt",
         "upper body, sitting on floor, sewing cosplay costume, needle and thread, tongue out, concentrating", "her room with mannequins and wigs on wig heads, desk lamp, night",
         "Yuki at home with her natural short black hair, sewing a new costume on the floor at night, tongue out in concentration."),
        ("short white bob, black and red magical girl outfit, staff with crescent moon, star hairpin",
         "cowboy shot, magical girl pose, one eye closed, smile", "studio:white",
         "Yuki as a dark magical girl in black and red, posing with a crescent-moon staff and a wink."),
        ("short white bob, long black coat, black lace top, black platform boots",
         "cowboy shot, standing among old gravestones, serious, eerie calm", "Highgate cemetery at night, fog, old gravestones, lantern",
         "Yuki among the old gravestones of Highgate Cemetery at night, fog around her, completely calm - her favourite place for a shoot."),
    ],
    "mika": [
        ("oversized black t-shirt",
         "upper body, lying in bed under blanket, holding phone above face, half-closed eyes, bored, expressionless", "messy bedroom at noon, closed curtains, light through the gap",
         "Mika still in bed at noon, under the blanket and scrolling on her phone held above her face, completely unbothered."),
        ("black crop top, oversized grey zip hoodie, baggy cargo pants, sunglasses on head",
         "cowboy shot, holding iced coffee with straw, deadpan, bored", "cafe terrace at Plac Zbawiciela in Warsaw, summer afternoon",
         "Mika at a cafe terrace on Plac Zbawiciela with an iced coffee, sunglasses pushed up, looking deeply bored."),
        ("oversized pink hoodie, black shorts",
         "upper body, sitting on floor, eating from food delivery boxes with chopsticks, cheeks full, expressionless", "small apartment, food delivery bags on the floor, evening",
         "Mika on her apartment floor surrounded by delivery boxes, cheeks full, eating noodles straight from the box. Third order today."),
        ("black puffer jacket, white crop top, mini skirt",
         "cowboy shot, standing in front of luxury boutique window, looking at handbag, longing, pout", "luxury boutique window on Nowy Swiat street in Warsaw, evening",
         "Mika outside a luxury boutique on Nowy Swiat in the evening, pouting at a handbag in the window she cannot afford."),
        ("white tank top, black leather mini skirt, chunky platform sneakers, sunglasses",
         "full body, posing like influencer, hand on hip, sunglasses, smirk", "Warsaw Old Town market square, sunny day",
         "Mika posing like an influencer in Warsaw's Old Town square, hand on hip, sunglasses and a smirk."),
        ("cropped black hoodie",
         "upper body, filming herself with phone, ring light, peace sign, tongue out, smug", "bedroom with ring light and fairy lights, night",
         "Mika filming a TikTok in her room at night under a ring light, tongue out and a smug peace sign."),
        ("silver sequin top, black jeans",
         "cowboy shot, holding red plastic cup, laughing hard, closed eyes, open mouth, head tilted back", "house party at night, colorful lights",
         "Mika at a house party she was not invited to, red cup in hand and laughing her head off."),
        ("oversized grey track jacket, black cap",
         "upper body, blowing bubble gum bubble, half-closed eyes, bored", "bus stop at night, Warsaw street lights",
         "Mika at a night bus stop in Warsaw, blowing a bubble with her gum, half asleep."),
        ("black hoodie, baggy jeans",
         "cowboy shot, holding expensive sneaker box, looking at price tag, shocked, wide eyes", "sneaker store, wall of shoes",
         "Mika in a sneaker store, staring at the price tag on a box in shock."),
        ("oversized white t-shirt, grey sweatpants",
         "full body, lying on sofa, eating potato chips, watching TV, lazy, expressionless", "dark living room at night, TV glow",
         "Mika sprawled on the sofa at night with a bag of chips, watching a reality show, lit only by the TV."),
        ("black puffer jacket",
         "upper body, sitting in taxi back seat, earphones, looking out window, bored", "taxi interior at night, city lights through the window",
         "Mika in the back of a taxi at night, earphones in, staring out at the city lights. Someone else is paying, obviously."),
        ("black oversized hoodie, plaid mini skirt",
         "upper body, laughing hard, closed eyes, open mouth, tears of laughter, holding fake rubber spider", "university hallway",
         "Mika in a university hallway, laughing so hard she is crying, holding the rubber spider she just used on someone."),
        ("oversized grey hoodie, pajama shorts",
         "cowboy shot, leaning on balcony railing, holding energy drink can, sleepy, yawning", "apartment balcony at sunrise, Warsaw skyline with the Palace of Culture",
         "Mika on her balcony at sunrise with an energy drink, yawning - she has not slept yet."),
        ("black bomber jacket, white t-shirt",
         "upper body, eating zapiekanka, cheese pull, smug", "street food stall at night, warm lights",
         "Mika at a late-night street food stall, biting into a zapiekanka with a long cheese pull and a smug look."),
        ("cropped white jacket, black mini skirt, sunglasses",
         "cowboy shot, holding many shopping bags, smirk, sunglasses", "Zlote Tarasy shopping mall in Warsaw",
         "Mika in the Zlote Tarasy mall with a pile of shopping bags and a smirk. Do not ask whose card it was."),
        ("black off-shoulder top",
         "upper body, showing long acrylic nails to viewer, smug, half-closed eyes", "nail salon, bright lights",
         "Mika at the nail salon showing off long new acrylic nails with a smug, half-lidded look."),
        ("black slip dress, oversized denim jacket",
         "full body, sitting on curb, holding high heels in hand, laughing hard, head thrown back, open mouth", "street outside a nightclub at dawn",
         "Mika sitting on the curb outside a club at dawn, heels in her hand, laughing her head off at something from last night."),
        ("oversized black t-shirt, black shorts",
         "upper body, holding food delivery bag at the door, small smirk", "apartment doorway, hallway",
         "Mika at her door taking a food delivery bag, with the smallest smirk - the best moment of her day."),
        ("black bikini top, oversized open white shirt, denim shorts, sunglasses",
         "cowboy shot, sitting on sand, holding can of drink, deadpan", "Vistula riverbank beach in Warsaw, summer evening",
         "Mika on the Vistula river beach on a summer evening with a can in her hand, sunglasses on, deadpan."),
        ("grey hoodie",
         "upper body, sleeping on desk, head on arms, closed eyes, drooling", "university lecture hall",
         "Mika fast asleep on her desk in a lecture hall, head on her arms. Attendance counts, apparently."),
    ],
}


STUDIO = {"white": "simple background, white background, professional cosplay photoshoot, fashion photography, studio lighting, softbox lighting, "
                    "magazine editorial, sharp focus",
          "black": "simple background, black background, professional cosplay photoshoot, dramatic studio lighting, rim light, magazine editorial, sharp focus"}
BW = "monochrome, greyscale, high contrast black and white photo"
STUDIO_KEEP = ("simple background", "plain background", "monochrome")


def studio(place):
    """("white"|"black", black-and-white?, extra light) for "studio:<colour>[ bw][|light]" and "cover:<colour>"; None for a real place."""
    kind, _, rest = place.partition(":")
    if kind not in ("studio", "cover"):
        return None
    colour, _, light = rest.partition("|")
    return colour.split()[0], "bw" in colour.split(), light


def prompt_for(info, key, number, seed):
    c = CHARACTERS[key]
    outfit, doing, place, _ = SCENES[key][number]
    prompt = copy.deepcopy(wf.photo_real_workflow(info).prompt())
    shot = studio(place)
    if shot:
        colour, bw, light = shot
        wf.set_value(prompt, "3b. Realistyczne tlo", STUDIO[colour] + (", " + BW if bw else ""))
        place = "photo studio" + (", " + light if light else "")
        negative = next(n for n in prompt.values() if n["_meta"]["title"].startswith("Negatyw (wspolny)"))
        negative["inputs"]["value"] = ", ".join(t for t in negative["inputs"]["value"].split(", ") if t not in STUDIO_KEEP)
    wf.set_value(prompt, "1. Postac", c["body"])
    wf.set_value(prompt, "1a. Stroj", outfit)
    avoid = c["avoid"] + no_glasses(key, outfit, doing)
    if c.get("unsmiling") and not any(w in doing for w in ("laugh", "smile")):
        avoid += ", " + c["unsmiling"]
    wf.set_value(prompt, "1b. Postac", avoid)
    wf.set_value(prompt, "2. Co robi", doing)
    wf.set_value(prompt, "3. Miejsce", place)
    wf.set_value(prompt, "LoRA postaci", c["lora"], "lora_name")
    wf.set_value(prompt, "LoRA postaci", c["strength"], "strength_model")
    wf.set_value(prompt, "LoRA postaci", c["strength"], "strength_clip")
    wf.set_value(prompt, "1. przebieg", seed, "seed")
    wf.set_value(prompt, "Folder zapisu", f"feed_v2/{key}")
    wf.set_value(prompt, "Zapis: zdjecie", f"{number + 1:02d}", "string_b")
    return prompt


# Entrance poses (pose.png) in her four costumes: full figure on flat green (unholy_anima_poza_v2), each pose different.
POSES = {
    "saki": {
        "costume_00": (saki.COSTUMES["costume_00"][0], "full body, walking, looking back over shoulder, holding shopping bags, waving, smile"),
        "costume_10_beach": (saki.COSTUMES["costume_10_beach"][0], "full body, standing, one leg raised behind, adjusting sunglasses on head, holding beach ball under arm, grin"),
        # Office, very classy (instead of the magical girl).
        "costume_20_office": ("cream silk blouse, black tailored blazer, black high-waisted pencil skirt, thin gold necklace, pearl earrings, "
                              "slim gold watch, sheer black pantyhose, black pointed pumps, elegant, classy, luxury fashion",
                              "full body, walking confidently, holding takeaway coffee cup, black leather document folder under arm, "
                              "confident smile, looking at viewer"),
        "costume_30_pajama": (saki.COSTUMES["costume_30_pajama"][0], "full body, sitting on floor, hugging big pillow, yawning, sleepy"),
    },
    "kiko": {
        "costume_00": ("navy tank top, light-blue jeans, dark belt, gold bracelets, black boots",
                       "full body, leaning back as if against a wall, arms crossed, holding cigarette between fingers, one boot propped back, smirk"),
        "costume_10_leather": ("brown leather jacket, black tank top, rust-orange leather pants, black biker boots",
                               "full body, squatting, elbows on knees, cigarette in mouth, looking up at viewer, grin"),
        "costume_20_kitsune": ("black kimono with orange fox fire pattern, red obi, wooden geta, fox mask on side of head",
                               "full body, standing half-turned, holding glowing paper lantern, long kiseru pipe in other hand, sly smile"),
        "costume_30_winter": ("oversized black puffer jacket, dark red beanie with holes for fox ears, thick grey scarf, black jeans, black boots",
                              "full body, walking, hands in pockets, breath vapor, smirk"),
    },
    "akane": {
        "costume_00": ("white blouse, black corset vest, red ribbon, black high-waisted pleated skirt, black pantyhose, black loafers",
                       "full body, standing elegantly, hugging stack of books to chest, head tilted, finger on lips, teasing smile"),
        "costume_10_witch": ("witch hat, black witch dress with red lining, red cape, black boots",
                             "full body, casting spell, open grimoire floating in front of her, hand raised, magic glow, confident smile"),
        "costume_20_lab": ("white lab coat over red turtleneck, black pencil skirt, black heels, red-framed glasses",
                           "full body, leaning forward, holding round flask with glowing liquid, glasses, smirk"),
        "costume_30_hakama": ("crimson kimono with white cherry blossom pattern, dark purple hakama, brown lace-up boots, red ribbon in hair",
                              "full body, standing proudly, holding rolled diploma, wind in hair, gentle smile"),
    },
    # Stylish Gen Z gyaru with pink accents (her hair), smug and teasing.
    "mika": {
        "costume_00": ("black baseball cap worn backwards, open cropped pink varsity jacket, fitted white baby tee crop top, low-rise light-wash "
                       "baggy jeans, pink belt, layered silver necklaces, silver hoop earrings, chunky black platform sneakers",
                       "full body, standing with weight on one hip, hand on hip, holding smartphone in other hand, smug smirk, looking at viewer"),
        "costume_10_hoodie": ("oversized pastel pink hoodie slipping off one shoulder, black camisole strap, grey sweat shorts, white knee socks, "
                              "fluffy pink slides, white headphones around neck, messy hair bun",
                              "full body, sitting on floor, leaning back on one hand, legs crossed, holding bubble tea, smug smirk, looking at viewer"),
        # All in black, expensive and effortless: an open oversized blazer, loose trousers.
        "costume_20_glam": ("all black outfit, black oversized blazer worn open, black lace bralette, high-waisted black wide-leg trousers, "
                            "gold chain belt, gold jewelry, black strappy heels, small black designer bag, luxury fashion, elegant, expensive",
                            "full body, from behind, back slightly turned to viewer, turning around, looking back over shoulder, "
                            "one hand in trouser pocket, hair flowing, smug smirk"),
        "costume_30_puffer": ("black beanie, cropped glossy silver puffer jacket worn open, black sports bra, bare midriff, low-rise black "
                              "baggy cargo pants, matching pair of chunky white sneakers",
                              "full body, walking with swagger, hands in pockets, open jacket, lollipop in mouth, round pink swirl lollipop "
                              "candy visible at her lips, white stick, smug smirk"),
    },
    "yuki": {
        "costume_00": ("short white bob, black lace hair bow, black gothic dress with high white lace collar, silver cross necklace, velvet choker, black platform boots",
                       "full body, standing, one hand on hip, peace sign over one eye, smirk"),
        "costume_10_pumpkin-pierrot": ("short white bob, Halloween pierrot costume, orange and black ruffled collar, pumpkin hairpin, striped tights, black boots",
                                       "full body, crouching playfully next to carved jack-o-lantern, tongue out, one eye closed"),
        "costume_20_street": ("short white bob, round glasses, black crop top, oversized black jacket, black cargo pants, silver chains, "
                              "chunky boots, sheer black full bodystocking under her clothes, sheer mesh sleeves",
                              "full body, standing, hands in pockets, leaning slightly, serious, cool"),
        "costume_30_sailor": ("short white bob, black sailor uniform, long black skirt, red ribbon, black tights, loafers",
                              "full body, sitting on school desk, legs crossed, chin on hand, mischievous smile"),
    },
}


STEPS = 20  # Poses, bases and feelings: 20 steps, no hires pass (fast()).


def prune(prompt):
    """Only what the saved pictures need stays in the API prompt."""
    keep, todo = set(), [i for i, n in prompt.items() if n["class_type"] == "SaveImage"]
    while todo:
        i = todo.pop()
        if i in keep:
            continue
        keep.add(i)
        todo += [v[0] for v in prompt[i]["inputs"].values() if isinstance(v, list) and len(v) == 2 and isinstance(v[0], str)]
    for i in [i for i in prompt if i not in keep]:
        del prompt[i]
    return prompt


def fast(prompt):
    """STEPS everywhere and no hires pass: the first pass's picture is what is saved."""
    for node in prompt.values():
        if node["class_type"] == "KSampler":
            node["inputs"]["steps"] = STEPS
    if any(n["_meta"]["title"].startswith("Hires fix") for n in prompt.values()):
        first = next(i for i, n in prompt.items() if n["_meta"]["title"].startswith("1. przebieg"))
        decoded = next(i for i, n in prompt.items() if n["class_type"] == "VAEDecode" and n["inputs"]["samples"][0] == first)
        for node in prompt.values():
            if node["class_type"] == "SaveImage":
                node["inputs"]["images"] = [decoded, 0]
    return prune(prompt)


def pose_prompt_for(info, key, costume, seed):
    c = CHARACTERS[key]
    outfit, pose = POSES[key][costume]
    prompt = copy.deepcopy(wf.pose_workflow(info).prompt())
    avoid = c["avoid"] + no_glasses(key, outfit, pose)
    if c.get("unsmiling") and not any(w in pose for w in ("laugh", "smile", "grin")):
        avoid += ", " + c["unsmiling"]
    wf.set_value(prompt, "1. Postac", c["body"])
    wf.set_value(prompt, "1a. Stroj", outfit)
    wf.set_value(prompt, "1b. Postac", avoid)
    wf.set_value(prompt, "2. Poza", pose)
    for field in ("strength_model", "strength_clip"):
        wf.set_value(prompt, "LoRA postaci", c["strength"], field)
    wf.set_value(prompt, "LoRA postaci", c["lora"], "lora_name")
    wf.set_value(prompt, "1. przebieg", seed, "seed")
    wf.set_value(prompt, "Folder zapisu", f"poses_v2/{key}/{costume}")
    return fast(prompt)


# A bust shows no footwear or legwear: in close-ups they make the model draw a second, whole figure.
BUST_DROP = re.compile(r"\b(boots?|shoes?|sneakers?|heels|loafers|geta|slides|socks|tights|pantyhose|leggings|sandals|slippers|"
                       r"skirt|pants|jeans|shorts|trousers|handbag|bag)\b")
BASE_MOOD = {"mika": "smug, smirk, confident teasing look", "yuki": "calm, closed mouth"}  # The base's face the feelings start from.


def bust_outfit(key, costume):
    return ", ".join(t for t in POSES[key][costume][0].split(", ") if not BUST_DROP.search(t))


def character_values(prompt, key, outfit, bust=False):
    """Her description, outfit, what to avoid and her LoRA - the same in poses, bases and feelings. A bust shows no
    tail (Kiko's would take half the picture): it is left out of her description and avoided."""
    c = CHARACTERS[key]
    body, avoid = c["body"], c["avoid"] + no_glasses(key, outfit)
    if bust and "tail" in body:
        body = ", ".join(t for t in body.split(", ") if "tail" not in t)
        avoid += ", tail, fox tail"
    wf.set_value(prompt, "1. Postac", body)
    wf.set_value(prompt, "1a. Stroj", outfit)
    wf.set_value(prompt, "1b. Postac", avoid)
    wf.set_value(prompt, "LoRA postaci", c["lora"], "lora_name")
    for field in ("strength_model", "strength_clip"):
        wf.set_value(prompt, "LoRA postaci", c["strength"], field)


def base_prompt_for(info, key, costume, seed):
    """Her upper body in the costume, arms down, on green, square (a can in her hand or a waving hand fits): the
    picture all 20 feelings are drawn from (the bust is cut from it just above her head, see bust_frame)."""
    prompt = copy.deepcopy(wf.pose_workflow(info).prompt())
    character_values(prompt, key, bust_outfit(key, costume), bust=True)
    wf.set_value(prompt, "2. Poza", f"standing, arms at sides, {BASE_MOOD.get(key, 'light smile')}, looking at viewer")
    wf.set_value(prompt, "3. Kadr i tlo", "upper body, from waist up, looking at viewer, centered, whole head in frame, empty space above head, "
                 "shoulders and arms fully in frame, " + wf.GREEN)
    wf.set_value(prompt, "832x1216", 1024, "width")
    wf.set_value(prompt, "832x1216", 1024, "height")
    wf.set_value(prompt, "1. przebieg", seed, "seed")
    wf.set_value(prompt, "Folder zapisu", f"emotions_v2/{key}/{costume}")
    wf.set_value(prompt, "Zapis: pose", "baza", "string_b")
    return fast(prompt)


BUST_ASPECT = 1.0  # The feelings' picture and the sprite canvas are square.
CANVAS = 560       # The shared sprite canvas, CANVAS x CANVAS (companion_ui::sprite_width/height).


def bust_frame(path):
    """Where the bust is cut from a base: (width, height, crop height, crop top, top of her head, sides touched).
    The crop spans the whole width and starts a little above her head; the base is fit for it when the head has
    room above it and nothing of her touches the left or right edge within the crop."""
    import numpy as np
    alpha = np.asarray(cutout(path).getchannel("A")) > 128
    h, w = alpha.shape
    crop = round(w / BUST_ASPECT)
    rows = np.nonzero(alpha.sum(axis=1) > 2)[0]
    top = int(rows[0]) if len(rows) else 0
    y = max(0, min(top - round(crop * 0.06), h - crop))
    band = alpha[y: y + crop]
    sides = int(band[:, :4].sum() + band[:, -4:].sum()) > 30
    return w, h, crop, y, top, sides


def framed(path):
    """Her head clear of the top edge (room is added above it later, prepare_base) and nothing cut at the sides."""
    _, _, _, _, top, sides = bust_frame(path)
    return top >= 4 and not sides


def prepare_base(base, target):
    """The base for the feelings, with backdrop added above her head when it is closer to the top edge than the
    bust's margin; returns its bust_frame."""
    import numpy as np
    from PIL import Image
    w, h, crop, _, top, _ = bust_frame(base)
    margin = round(crop * 0.06)
    image = Image.open(base).convert("RGB")
    if top < margin:
        backdrop = tuple(int(v) for v in np.median(np.asarray(image)[:4].reshape(-1, 3), axis=0))
        padded = Image.new("RGB", (w, h + margin - top), backdrop)
        padded.paste(image, (0, margin - top))
        image = padded
    image.save(target)
    return bust_frame(target)


def check_base(server, prompt, tries=4):
    """A base with one face (two of her: another seed). A head or sides cut by the frame are kept and only reported:
    room above her head is added later (prepare_base), the rest can be outpainted."""
    for attempt in range(tries):
        saved = wf.run(server, prompt)
        path = COMFY / "output" / saved[0][0] / saved[0][1]
        count = saki.faces(path)
        _, _, _, _, top, sides = bust_frame(path)
        if count is None or count <= 1 or attempt == tries - 1:
            if not framed(path):
                print(f"  {saved[0][1]}: head top {top}px, sides cut {sides} (kept)", flush=True)
            return saved
        print(f"  {saved[0][1]}: {count} faces - another seed", flush=True)
        path.unlink()
        node = next(n for n in prompt.values() if n["_meta"]["title"].startswith("1. przebieg"))
        node["inputs"]["seed"] += 7919
    return saved


# The feelings' tags (comfy_workflows.EMOTIONS) where the bust needs clearer gestures, and without drawn marks: the
# app draws its own (question mark, sweat drop, sparkles - companion_marks.cpp). Per character: her own drink etc.
EMOTION_TAGS = {
    "angry": "angry, pout, furrowed brow, glaring, crossed arms, arms folded across chest",
    "shock": "surprised, wide-eyed, open mouth, both hands on own cheeks",
    "fear": "scared, pale face, wide-eyed, trembling, both hands near mouth",
    "sleepy": "sleepy, yawning, closed eyes, hand over own mouth",
    "confused": "confused, head tilt, raised eyebrow, pursed lips, scratching head",
    "laugh": "laughing, closed eyes, open mouth, hand over own mouth",
    "cry": "crying, tears, wiping tears with hand, trembling lips",
    "gaming": "holding game controller in both hands, gamepad visible, playing video games, focused, slight smile",
}
CHARACTER_EMOTION_TAGS = {
    "saki": {"drink": "drinking through straw, holding green smoothie in clear plastic cup, healthy drink, half-closed eyes"},
}
NO_MARKS = "speech bubble, question mark, exclamation mark, sweatdrop, musical note, heart symbol, sparkle, text, symbols, motion lines"
GESTURE_DENOISE = 0.78  # From a base with arms at her sides; 0.65 hardly moves them (no crossed arms, no controller).


def emotions_prompt_for(info, key, costume, picture, frame, seed):
    """20 feelings from the base's bust: the whole width, from just above her head down (BUST_ASPECT)."""
    w, h, crop, y, _, _ = frame
    prompt = copy.deepcopy(wf.emotions_workflow(info).prompt())
    character_values(prompt, key, bust_outfit(key, costume), bust=True)
    avoid = next(n for n in prompt.values() if n["_meta"]["title"].startswith("1b. Postac"))
    avoid["inputs"]["value"] += ", " + NO_MARKS
    for emotion, tags in {**EMOTION_TAGS, **CHARACTER_EMOTION_TAGS.get(key, {})}.items():
        wf.set_value(prompt, f"{emotion}: tagi miny", tags, "string_b")
    wf.set_value(prompt, "Denoise: z gestem", GESTURE_DENOISE)
    wf.set_value(prompt, "Poza (stroj i twarz)", picture, "image")
    wf.set_value(prompt, "Do 1024x1536", w, "width")  # Kept as it is: the crop is measured on the base itself.
    wf.set_value(prompt, "Do 1024x1536", h, "height")
    wf.set_value(prompt, "Kadr popiersia", w, "width")
    wf.set_value(prompt, "Kadr popiersia", crop, "height")
    wf.set_value(prompt, "Kadr popiersia", 0, "x")
    wf.set_value(prompt, "Kadr popiersia", y, "y")
    wf.set_value(prompt, "Popiersie 1216x832", 1024, "width")
    wf.set_value(prompt, "Popiersie 1216x832", 1024, "height")
    wf.set_value(prompt, "Seed (wspolny)", seed)
    wf.set_value(prompt, "Folder zapisu", f"emotions_v2/{key}/{costume}")
    del prompt[next(i for i, n in prompt.items() if n["_meta"]["title"] == "Emotion sheet")]  # Only a preview.
    return fast(prompt)


def edit_prompt_for(info, key, costume, picture, size, stage, seed, denoise=0.6):
    """A picture of hers drawn again (img2img) with the costume's current outfit, the rest of it kept: a pose
    (full body, poses_v2) or a base (bust, emotions_v2). One branch of the feelings workflow, its tags = the pose."""
    w, h = size
    pose = stage == "pose"
    prompt = copy.deepcopy(wf.emotions_workflow(info).prompt())
    character_values(prompt, key, POSES[key][costume][0] if pose else bust_outfit(key, costume), bust=not pose)
    for title in ("Do 1024x1536", "Kadr popiersia", "Popiersie 1216x832"):
        wf.set_value(prompt, title, w, "width")
        wf.set_value(prompt, title, h, "height")
    wf.set_value(prompt, "Kadr popiersia", 0, "x")
    wf.set_value(prompt, "Kadr popiersia", 0, "y")
    wf.set_value(prompt, "Poza (stroj i twarz)", picture, "image")
    if pose:
        wf.set_value(prompt, "Kadr i tlo", "full body, centered, " + wf.GREEN)
    expression = POSES[key][costume][1] if pose else f"standing, arms at sides, {BASE_MOOD.get(key, 'light smile')}, looking at viewer"
    wf.set_value(prompt, "cool: tagi miny", expression, "string_b")
    sampler = next(n for n in prompt.values() if n["class_type"] == "KSampler" and n["_meta"]["title"] == "cool")
    sampler["inputs"]["denoise"] = denoise
    wf.set_value(prompt, "Seed (wspolny)", seed)
    wf.set_value(prompt, "Folder zapisu", f"{'poses_v2' if pose else 'emotions_v2'}/{key}/{costume}")
    wf.set_value(prompt, "Zapis: cool", "pose" if pose else "baza", "string_b")
    for i in [i for i, n in prompt.items() if n["class_type"] == "SaveImage" and n["inputs"]["filename_prefix"][0] !=
              next(j for j, m in prompt.items() if m["_meta"]["title"] == "Zapis: cool")]:
        del prompt[i]
    return fast(prompt)


# ---- the same as ComfyUI workflows (UI format) to run by hand: workflows [key] -----------------------------------------
WORKFLOWS = pathlib.Path(os.environ.get("COMFY_WORKFLOWS", wf.DEFAULT_OUT)) / "launchme"


def ui_set(graph, title, value, field="value"):
    hits = [n for n in graph.nodes if (n.title or "").startswith(title)]
    if len(hits) != 1:
        sys.exit(f"{len(hits)} workflow nodes titled {title!r}")
    hits[0].widgets[field] = value
    return hits[0]


def ui_character(graph, key, outfit, bust, extra_avoid=""):
    c = CHARACTERS[key]
    body, avoid = c["body"], c["avoid"] + no_glasses(key, outfit)
    if bust and "tail" in body:
        body = ", ".join(t for t in body.split(", ") if "tail" not in t)
        avoid += ", tail, fox tail"
    ui_set(graph, "1. Postac", body)
    ui_set(graph, "1a. Stroj", outfit)
    ui_set(graph, "1b. Postac", avoid + extra_avoid)
    ui_set(graph, "LoRA postaci", c["lora"], "lora_name")
    for field in ("strength_model", "strength_clip"):
        ui_set(graph, "LoRA postaci", c["strength"], field)


def ui_fast(graph):
    """STEPS everywhere; the hires pass muted, the first pass's picture saved."""
    for node in graph.nodes:
        if node.type == "KSampler":
            node.widgets["steps"] = STEPS
    first = next((n for n in graph.nodes if (n.title or "").startswith("1. przebieg")), None)
    if first is None:
        return
    first.title = f"1. przebieg ({STEPS} krokow, bez hires)"
    decoded = next(n for n in graph.nodes if n.type == "VAEDecode" and n.links.get("samples") == (first, 0))
    save = next(n for n in graph.nodes if n.type == "SaveImage")
    for node in graph.nodes:
        if (node.title or "").startswith("Hires") or (node.type in ("VAEEncode", "VAEDecode") and node is not decoded and any(
                s is not None and (s.title or "").startswith("Hires") for s, _ in node.links.values())):
            node.mode = 2  # Muted: Ctrl+M brings it back (then connect its picture to the save again).
    save.links["images"] = (decoded, 0)


def write_workflow(graph, name):
    """workflows/launchme/<name> (name may hold folders: <key>/<costume>/...)."""
    graph.prompt()  # Every required input connected.
    path = WORKFLOWS / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(graph.workflow(), ensure_ascii=False, indent=1), encoding="utf-8")
    return path


def feed_workflow(info, key):
    """Her Launchfeed photo workflow, filled in with her first scene shot on location (an example to change)."""
    c = CHARACTERS[key]
    number = next(n for n, scene in enumerate(SCENES[key]) if not studio(scene[2]))
    outfit, doing, place, _ = SCENES[key][number]
    g = wf.photo_real_workflow(info)
    avoid = c["avoid"] + no_glasses(key, outfit, doing)
    if c.get("unsmiling") and not any(w in doing for w in ("laugh", "smile")):
        avoid += ", " + c["unsmiling"]
    ui_set(g, "1. Postac", c["body"])
    ui_set(g, "1a. Stroj", outfit)
    ui_set(g, "1b. Postac", avoid)
    ui_set(g, "2. Co robi", doing)
    ui_set(g, "3. Miejsce", place)
    ui_set(g, "LoRA postaci", c["lora"], "lora_name")
    for field in ("strength_model", "strength_clip"):
        ui_set(g, "LoRA postaci", c["strength"], field)
    ui_set(g, "1. przebieg", c["seed"] + number, "seed")
    ui_set(g, "Folder zapisu", f"feed_v2/{key}")
    ui_set(g, "Zapis: zdjecie", f"{number + 1:02d}", "string_b")
    return write_workflow(g, f"{key}/{key}_feed.json")


def workflows(server, key):
    """workflows/launchme/<key>/: her feed example (<key>_feed) and, per costume (<key>/<costume>/), the poza, baza and
    emocje workflows filled in as the scripted runs fill them (each base copied into ComfyUI's input for the feelings)."""
    frames = {}
    for costume in POSES[key]:  # Bases into the input folder first: LoadImage only offers files that are there.
        base = newest(COMFY / "output" / "emotions_v2" / key / costume, "baza")
        if base:
            frames[costume] = prepare_base(base, COMFY / "input" / f"v2_{key}_{costume}_baza.png")
    info = wf.http(server, "/object_info")
    written = []
    for i, costume in enumerate(POSES[key]):
        c = CHARACTERS[key]
        outfit, pose = POSES[key][costume]
        g = wf.pose_workflow(info)
        ui_character(g, key, outfit, bust=False)
        ui_set(g, "2. Poza", pose)
        ui_set(g, "1. przebieg", c["seed"] + 500 + i, "seed")
        ui_set(g, "Folder zapisu", f"poses_v2/{key}/{costume}")
        ui_fast(g)
        written.append(write_workflow(g, f"{key}/{costume}/{key}_{costume}_1_poza.json"))

        g = wf.pose_workflow(info)
        ui_character(g, key, bust_outfit(key, costume), bust=True)
        ui_set(g, "2. Poza", f"standing, arms at sides, {BASE_MOOD.get(key, 'light smile')}, looking at viewer")
        ui_set(g, "3. Kadr i tlo", "upper body, from waist up, looking at viewer, centered, whole head in frame, empty space above head, "
               "shoulders and arms fully in frame, " + wf.GREEN)
        latent = ui_set(g, "832x1216", 1024, "width")
        latent.widgets["height"], latent.title = 1024, "1024x1024 (kwadrat)"
        ui_set(g, "1. przebieg", c["seed"] + 700 + i, "seed")
        ui_set(g, "Folder zapisu", f"emotions_v2/{key}/{costume}")
        ui_set(g, "Zapis: pose", "baza", "string_b")
        ui_fast(g)
        written.append(write_workflow(g, f"{key}/{costume}/{key}_{costume}_2_baza.json"))

        if costume in frames:
            w, h, crop, y, _, _ = frames[costume]
            g = wf.emotions_workflow(info)
            ui_character(g, key, bust_outfit(key, costume), bust=True, extra_avoid=", " + NO_MARKS)
            for emotion, tags in {**EMOTION_TAGS, **CHARACTER_EMOTION_TAGS.get(key, {})}.items():
                ui_set(g, f"{emotion}: tagi miny", tags, "string_b")
            ui_set(g, "Denoise: z gestem", GESTURE_DENOISE)
            ui_set(g, "Poza (stroj i twarz)", f"v2_{key}_{costume}_baza.png", "image")
            scale = ui_set(g, "Do 1024x1536", w, "width")
            scale.widgets["height"], scale.title = h, f"Baza {w}x{h} (bez zmian)"
            cut = ui_set(g, "Kadr popiersia", w, "width")
            cut.widgets.update(height=crop, x=0, y=y)
            bust = ui_set(g, "Popiersie 1216x832", 1024, "width")
            bust.widgets["height"], bust.title = 1024, "Popiersie 1024x1024"
            ui_set(g, "Seed (wspolny)", c["seed"] + 900 + i)
            ui_set(g, "Folder zapisu", f"emotions_v2/{key}/{costume}")
            ui_fast(g)
            written.append(write_workflow(g, f"{key}/{costume}/{key}_{costume}_3_emocje.json"))
    written.append(feed_workflow(info, key))
    for path in written:
        print(path)


# ---- detail photos for her feed: her things, not her (Z-Image Turbo, photorealistic): details [key] [--run] -----------
ZIMAGE = dict(unet="z_image_turbo_int8_convrot.safetensors", clip="qwen_3_4b_fp8_mixed.safetensors", vae="ae.safetensors")
DETAIL_SIZE = (960, 1600)  # Upright like a phone photo.
DETAIL_STYLE = ("Casual smartphone photo posted on social media, photorealistic, natural light, sharp focus, realistic textures, "
                "slightly imperfect amateur framing, no text, no watermark")
# (what the photo shows - Z-Image follows long plain sentences; what it shows in her feed: feed/<key>/<n>.txt)
DETAILS = {
    "yuki": [
        ("A styrofoam mannequin head on a cluttered desk wearing a short platinum-white bob wig with a small black lace bow clip, "
         "freshly styled. Wig brushes, hair pins and a can of hairspray beside it, a sewing machine and black lace fabric in the "
         "blurred background, warm desk lamp light, cosplayer's workroom at night. No people.",
         "Yuki's white bob wig on a mannequin head on her workroom desk, freshly styled, with brushes and pins beside it and the "
         "sewing machine in the background."),
        ("A black gothic dress with a high white lace collar hanging on a velvet clothes hanger hooked on a dark wardrobe door, "
         "a silver cross necklace and a velvet choker draped over the hanger, black platform boots standing below on the floor. "
         "Dim bedroom with warm fairy lights. No people.",
         "Tomorrow's outfit hanging on her wardrobe door: the black gothic dress with the white lace collar, the silver cross, the "
         "velvet choker and the platform boots."),
        ("Top-down flat lay on a bed with black satin sheets: a complete gothic cosplay outfit laid out neatly - black lace dress, "
         "fingerless lace gloves, silver chains, black eyeliner, a plum lipstick, a small plush bat toy and a printed convention "
         "badge on a lanyard. Soft evening light. No people.",
         "A whole convention outfit laid out on her black sheets, with the plush bat, the eyeliner and her convention badge."),
    ],
    "mika": [
        ("Point-of-view photo looking down at a girl's feet wearing brand-new chunky white and pink platform sneakers, standing on "
         "a sunny city sidewalk, the hem of baggy light-wash jeans visible, a pink shopping bag with tissue paper beside the shoes. "
         "Only legs and shoes, no face.",
         "Mika looking down at her brand-new chunky pink-and-white sneakers on the pavement, the shopping bag still beside her."),
        ("A hand with long glossy pink acrylic nails holding an iced latte in a clear plastic cup with a pink straw, raised against "
         "a busy Warsaw street with tram lines, golden hour, background blurred. Only the hand and the drink, no face.",
         "Her iced coffee held up against a Warsaw street at golden hour, long pink nails, the city blurred behind it."),
        ("A shopping haul on an unmade bed with white sheets: several designer shopping bags, a new small pink handbag, a perfume "
         "bottle, a receipt and a phone with a cracked pink case. Afternoon light from the window. No people.",
         "Today's shopping haul spread on her bed: designer bags, a new pink handbag, perfume and a suspiciously long receipt."),
    ],
    "saki": [
        ("Flat lay of cosmetics on a white marble vanity: skincare serums in glass bottles, several lipsticks, makeup brushes in a "
         "cup, an eyeshadow palette, a small round mirror and a vase of pale pink peonies, soft morning light from a window. "
         "No people.",
         "Her morning cosmetics on the marble vanity: serums, lipsticks, brushes, a palette and pink peonies."),
        ("A spotless bright Parisian bathroom full of neatly arranged cosmetics: glass shelves with skincare bottles and perfumes "
         "lined up by height, rolled white towels, a small green plant, a round mirror with soft lights, white tiles and brass "
         "taps. No people.",
         "Her freshly cleaned bathroom in Paris, every bottle and perfume lined up on the glass shelves, towels rolled, all shining."),
        ("Top-down photo of a woman's bare feet with freshly painted glossy coral toenails resting on a fluffy white rug, a bottle "
         "of coral nail polish and cotton pads beside them, soft daylight. Only the feet, no face.",
         "Her bare feet on the fluffy white rug with freshly painted coral toenails, the polish bottle still open beside them."),
    ],
    "kiko": [
        ("A black cafe racer motorcycle with orange accents parked alone on a wet city street at night, neon signs reflecting on "
         "the asphalt and on the chrome, a black helmet hanging on the mirror. No people.",
         "Kiko's black cafe racer with orange accents parked on a wet street at night, neon reflecting on the chrome."),
        ("A black electric guitar with an orange pickguard leaning against a small guitar amplifier in a dim rehearsal room, "
         "cables on the floor, a set list taped to the floor, an ashtray and a can of beer on the amp. No people.",
         "Her black guitar leaning on the amp in the rehearsal room, set list taped to the floor, an ashtray on the amp."),
        ("A worn brown leather jacket and a burnt-orange scarf hanging on a hook in a dark hallway, below them on a shelf a black "
         "motorcycle helmet with small fox-ear stickers and a pair of black biker boots. No people.",
         "Her leather jacket, orange scarf, the helmet with fox-ear stickers and her biker boots by the door."),
    ],
    "akane": [
        ("A stack of physics textbooks and Stanislaw Lem novels on an old wooden desk, red-framed glasses resting on top of the "
         "stack, a cup of tea in fine porcelain, handwritten equations in a notebook, a green desk lamp, Krakow rooftops in the "
         "evening window. No people.",
         "Akane's desk in the evening: physics textbooks, Lem, her red-framed glasses on top, tea in porcelain and a notebook of equations."),
        ("Top-down photo of a tea set on a wooden table: green gyokuro tea being poured from a small Japanese teapot into a fine "
         "porcelain cup, steam rising, beside it a chessboard with a game in progress. No people.",
         "Gyokuro poured with full ceremony next to an unfinished game of chess - checkmate in three, she claims."),
        ("A telescope on a small balcony at night pointing at the starry sky, a star chart and a notebook with handwritten "
         "calculations on a little table with a red flashlight, Krakow rooftops and church towers below. No people.",
         "Her telescope on the balcony at night, a star chart and notebook beside it, Krakow's roofs below."),
    ],
}


# Mixed worlds, like her feed: her hand or feet drawn in anime style (Anima, as in her photos), the things and the place a
# real photo. (part of her, what she wears on it, the shot, the place)
MIXED_DETAILS = {
    ("saki", 3): ("bare feet",
                  "glossy coral toenail polish on her toenails",
                  "close-up side view of her two bare feet resting on a fluffy white rug, toes with freshly painted toenails, ankles "
                  "visible, feet only, legs out of frame, head out of frame",
                  "bedroom floor, fluffy white rug, an open bottle of coral nail polish and cotton pads beside her feet, soft daylight"),
    ("mika", 2): ("hand",
                  "long glossy pink acrylic nails, silver rings, pink varsity jacket sleeve",
                  "pov, close-up of her hand only, holding an iced latte in a clear plastic cup with a pink straw raised in front of the "
                  "camera, hand and cup only, head out of frame, no face",
                  "busy Warsaw street with tram lines, golden hour, background blurred"),
}
# "Anime in real" for Anima (Civitai 1979448, v1.5): an anime figure in a realistic photo; its trigger opens the prompt.
ANIME_IN_REAL = ("Anima_in_real_v1.5.safetensors", 0.8, "@Ani3rel")
MIXED_AVOID = ("face, head, eyes, hair, upper body, portrait, thighs, knees, crotch, skirt, panties, sitting, from below, "
               "multiple girls, 2girls, extra legs, extra feet, multiple legs, photorealistic hand, realistic skin, 3d")


def mixed_detail_workflow(info, key, number, seed):
    """Her feed photo of a detail: the part of her in anime style, the rest a real photo (unholy_anima_zdjecie_v2_real)."""
    part, wearing, doing, place = MIXED_DETAILS[(key, number)]
    g = wf.photo_real_workflow(info)
    lora, strength, trigger = ANIME_IN_REAL
    second = ui_set(g, "LoRA 2", lora, "lora_name")
    second.widgets.update(strength_model=strength, strength_clip=strength)
    second.mode, second.title = 0, "LoRA 2: Anime in real (anime na realnym zdjeciu)"
    ui_set(g, "1. Postac", f"{trigger}, " + QUALITY + ", anime style, clean anime linework, flat colors")
    ui_set(g, "1a. Stroj", wearing)
    ui_set(g, "1b. Postac", MIXED_AVOID)
    ui_set(g, "2. Co robi", doing)
    ui_set(g, "3. Miejsce", place)
    ui_set(g, "3b. Realistyczne tlo", wf.REALISM.replace("only she is drawn in anime style",
                                                          f"only her {part} is drawn in anime style; every object is real"))
    ui_set(g, "LoRA postaci", CHARACTERS[key]["lora"], "lora_name")
    for field in ("strength_model", "strength_clip"):  # No face in the picture: her LoRA would only pull one in.
        ui_set(g, "LoRA postaci", 0.0, field)
    ui_set(g, "1. przebieg", seed, "seed")
    ui_set(g, "Folder zapisu", f"feed_v2_detale/{key}")
    ui_set(g, "Zapis: zdjecie", f"{number:02d}_mix", "string_b")
    return g


def mixed(server, run):
    info = wf.http(server, "/object_info")
    general = mixed_detail_workflow(info, "mika", 2, 61000)  # The general one: Mika's coffee as the example to change.
    ui_set(general, "Folder zapisu", "detale")
    ui_set(general, "Zapis: zdjecie", "mix", "string_b")
    print(write_workflow(general, "_ogolne/detal_mix_anime_in_real.json"))
    for (key, number), _ in MIXED_DETAILS.items():
        g = mixed_detail_workflow(info, key, number, 61000 + number)
        print(write_workflow(g, f"{key}/detale/{key}_detal_{number:02d}_mix.json"))
        if run:
            saved = wf.run(server, g.prompt())
            print(f"mix {key} {number:02d}:", *(f for _, f in saved), flush=True)


def detail_workflow(info, key=None, number=0, seed=20261022):
    """Z-Image Turbo: one photo from a plain description (9 steps, cfg 1), saved to feed_v2_detale/<key>/<NN>."""
    text = DETAILS[key][number][0] if key else "A cup of coffee on a cafe table by the window, morning light. No people."
    g = wf.Graph(info)
    g.note("Jak to dziala", (
        "Zdjecia detali do feedu (jej rzeczy, nie ona): Z-Image Turbo, fotorealizm.\n\n"
        "1. 'Co na zdjeciu': zwykle zdania po angielsku - przedmioty, miejsce, swiatlo. Czesc ciala bez twarzy: napisz to wprost\n"
        "   (np. 'Only the hand and the drink, no face').\n"
        "2. Styl zdjecia: zostaw (telefon, social media, naturalne swiatlo).\n"
        "3. 9 krokow, cfg 1, res_multistep/simple, shift 3 - tak jak chce Turbo. Negatyw nie dziala (cfg 1): czego nie chcesz,\n"
        "   opisz w pozytywie (np. 'No people').\n"
        "Zapis: output/<folder>/<nazwa>_00001_.png. Seed 'randomize': kazdy Run = nowe ujecie."), (0, 0), (520, 300))
    unet = g.node("UNETLoader", "Z-Image Turbo (int8)", (0, 340), (380, 82), widgets={"unet_name": ZIMAGE["unet"]})
    # The text encoder on the CPU: on 12 GB the GPU keeps room for the model (and whatever else is loaded).
    clip = g.node("CLIPLoader", "Enkoder Qwen3 4B (na CPU)", (0, 460), (380, 106), widgets={"clip_name": ZIMAGE["clip"], "type": "lumina2", "device": "cpu"})
    vae = g.node("VAELoader", "VAE", (0, 600), (380, 58), widgets={"vae_name": ZIMAGE["vae"]})
    model = g.node("ModelSamplingAuraFlow", "Shift 3", (0, 700), (380, 58), widgets={"shift": 3.0}, links={"model": unet[0]})
    g.group("Modele", [unet, clip, vae, model], "#3f789e")
    what = g.node("PrimitiveStringMultiline", "1. Co na zdjeciu", (560, 0), (560, 220), widgets={"value": text})
    style = g.node("PrimitiveStringMultiline", "2. Styl zdjecia (zostaw)", (560, 250), (560, 110), widgets={"value": DETAIL_STYLE})
    joined = g.node("StringConcatenate", "Opis + styl", (1160, 0), (300, 110), widgets={"delimiter": " "}, links={"string_a": what[0], "string_b": style[0]})
    positive = g.node("CLIPTextEncode", "Prompt", (1160, 140), (300, 90), links={"clip": clip[0], "text": joined[0]})
    negative = g.node("ConditioningZeroOut", "Negatyw (pusty przy cfg 1)", (1160, 260), (300, 30), links={"conditioning": positive[0]})
    g.group("Prompt", [what, style, joined, positive, negative], "#3f7a4e")
    latent = g.node("EmptySD3LatentImage", f"{DETAIL_SIZE[0]}x{DETAIL_SIZE[1]} (pion)", (1500, 0), (300, 106),
                    widgets={"width": DETAIL_SIZE[0], "height": DETAIL_SIZE[1], "batch_size": 1})
    sampler = g.node("KSampler", "Z-Image (9 krokow)", (1500, 140), (320, 262), controls={"seed": "randomize"},
                     widgets={"seed": seed, "steps": 9, "cfg": 1.0, "sampler_name": "res_multistep", "scheduler": "simple", "denoise": 1.0},
                     links={"model": model[0], "positive": positive[0], "negative": negative[0], "latent_image": latent[0]})
    decoded = g.node("VAEDecode", None, (1500, 440), (220, 46), links={"samples": sampler[0], "vae": vae[0]})
    folder = g.node("PrimitiveString", "Folder zapisu (w output)", (1860, 0), (360, 58),
                    widgets={"value": f"feed_v2_detale/{key}" if key else "detale"})
    prefix = wf.saved_as(g, folder, f"{number + 1:02d}" if key else "detal", (1860, 90))
    save = g.node("SaveImage", None, (1860, 240), (420, 640), links={"images": decoded[0], "filename_prefix": prefix[0]})
    g.group("Generowanie", [latent, sampler, decoded, folder, prefix, save], "#7a3f3f")
    return g


def details(server, keys, run, only=None):
    """workflows/launchme/_ogolne/detal_zimage.json and launchme/<key>/detale/<key>_detal_NN.json; with run, each drawn once
    (output/feed_v2_detale/<key>/NN_*.png, its feed caption beside it as NN.txt)."""
    info = wf.http(server, "/object_info")
    print(write_workflow(detail_workflow(info), "_ogolne/detal_zimage.json"))
    for key in keys:
        for number, (_, caption) in enumerate(DETAILS[key]):
            g = detail_workflow(info, key, number, 50000 + number)
            print(write_workflow(g, f"{key}/detale/{key}_detal_{number + 1:02d}.json"))
            if run and (not only or number + 1 in only):
                saved = wf.run(server, g.prompt())
                folder = COMFY / "output" / "feed_v2_detale" / key
                (folder / f"{number + 1:02d}.txt").write_text(caption + "\n", encoding="utf-8")
                print(f"detal {key} {number + 1:02d}:", *(f for _, f in saved), flush=True)


def newest(folder, stem):
    """The newest ComfyUI output <stem>_NNNNN_.png in a folder (None when there is none)."""
    found = sorted(p for p in folder.glob(f"{stem}_*.png") if re.fullmatch(rf"{re.escape(stem)}_\d+_", p.stem))
    return found[-1] if found else None


def cutout(path):
    """RGBA: the flat backdrop (whatever green or other colour it came out) keyed out. The backdrop is what the top
    edge and the upper side edges show; it goes where it is connected to the border (dark line art stops it, so a
    silver jacket reflecting the green keeps its colour), and in enclosed gaps (an arm on her hip) as close to it as
    the open backdrop. Edge pixels get their colour un-mixed from it and lose the green cast."""
    import numpy as np
    from PIL import Image
    from scipy import ndimage
    rgb = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    h, w, _ = rgb.shape
    edge = np.concatenate([rgb[:6].reshape(-1, 3), rgb[: h // 2, :6].reshape(-1, 3), rgb[: h // 2, -6:].reshape(-1, 3)])
    backdrop = np.median(edge, axis=0)
    backdrop = edge[np.linalg.norm(edge - backdrop, axis=1) < 30].mean(axis=0)
    distance = np.linalg.norm(rgb - backdrop, axis=2)
    near, far = 16.0, 58.0
    candidate = distance < far
    labels, count = ndimage.label(candidate)
    touching = set(np.unique(np.concatenate([labels[0], labels[:, 0], labels[:, -1]]))) - {0}
    background = np.isin(labels, list(touching))
    if count:
        sizes = ndimage.sum(np.ones_like(distance), labels, index=np.arange(1, count + 1))
        medians = ndimage.median(distance, labels, index=np.arange(1, count + 1))
        holes = [i + 1 for i in range(count) if i + 1 not in touching and sizes[i] > h * w * 0.0004 and medians[i] < near]
        background |= np.isin(labels, holes)
    alpha = np.where(background, np.clip((distance - near) / (far - near), 0.0, 1.0), 1.0)
    alpha = np.clip((alpha - 0.1) / 0.9, 0.0, 1.0)
    a = np.maximum(alpha, 1e-3)[..., None]
    colour = np.clip(np.where(alpha[..., None] < 1.0, (rgb - (1.0 - a) * backdrop) / a, rgb), 0.0, 255.0)
    r, g, b = colour[..., 0], colour[..., 1], colour[..., 2]
    rim = ndimage.binary_dilation(alpha < 0.99, iterations=2) & (alpha > 0)
    if backdrop[1] > max(backdrop[0], backdrop[2]):
        g = np.where(rim & (g > np.maximum(r, b)), np.minimum(g, (r + b) / 2.0 + 8.0), g)
    pixels = np.dstack([r, g, b, alpha * 255.0])
    pixels[alpha <= 0] = 0
    return Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8), "RGBA")


def pack(key):
    """Her sprite pack: <IMPORT>/res/companion/<key>/ with character.txt and each costume's feelings, pose.png and costume.txt."""
    _spec = importlib.util.spec_from_file_location("companion_sprites", REPO / "tools" / "companion_sprites.py")
    sprites = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(sprites)
    width = height = CANVAS
    target = IMPORT / "res" / "companion" / key
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    shutil.copy2(REPO / "res" / "companion" / key / "character.txt", target / "character.txt")
    for costume in sorted(POSES[key], key=lambda c: (c != "costume_00", c)):
        source = COMFY / "output" / "emotions_v2" / key / costume
        keyed = COMFY / "output" / "pack_v2" / key / costume
        if keyed.exists():
            shutil.rmtree(keyed)
        keyed.mkdir(parents=True)
        missing = []
        for emotion in sprites.EMOTIONS:
            picture = newest(source, emotion)
            if picture:
                cutout(picture).save(keyed / f"{emotion}.png")
            else:
                missing.append(emotion)
        if missing:
            print(f"{key}/{costume}: no {', '.join(missing)}", flush=True)
        folder = target / costume
        sprites.images(keyed, folder, width, height, match_heads=costume == "costume_00",
                       reference=None if costume == "costume_00" else target / "costume_00")
        pose = newest(COMFY / "output" / "poses_v2" / key / costume, "pose")
        if pose:
            image = cutout(pose)
            box = image.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
            if box:  # Cropped to her, her lowest point on the bottom edge (she stands on the column's floor).
                image = image.crop((max(0, box[0] - 8), max(0, box[1] - 8), min(image.width, box[2] + 8), box[3]))
            image.save(keyed / "pose.png")
            image.save(folder / "pose.png", optimize=True)
        text = REPO / "res" / "companion" / key / costume / "costume.txt"
        if text.exists():
            shutil.copy2(text, folder / "costume.txt")
        print(f"{key}/{costume}: packed ({pose.name if pose else 'no pose'})", flush=True)


def keep_newest(key, number):
    """Only the newest picture of a scene stays in feed_v2/<key>; the others move to feed_v2/<key>_odrzucone."""
    folder = COMFY / "output" / "feed_v2" / key
    pictures = sorted(folder.glob(f"{number + 1:02d}_*.png"))
    if len(pictures) > 1:
        rejected = folder.parent / f"{key}_odrzucone"
        rejected.mkdir(exist_ok=True)
        for picture in pictures[:-1]:
            shutil.move(str(picture), rejected / picture.name)


def install(key):
    """The newest picture of every scene into <feed>/<key>/, numbered after what is there, each with its .txt."""
    source = COMFY / "output" / "feed_v2" / key
    target = FEED_DIR / key
    target.mkdir(parents=True, exist_ok=True)
    taken = [int(p.stem) for p in target.glob("*.png") if p.stem.isdigit()]
    n = max(taken, default=0)
    for number, scene in enumerate(SCENES[key]):
        pictures = sorted(source.glob(f"{number + 1:02d}_*.png"))
        if not pictures:
            continue
        n += 1
        shutil.copy2(pictures[-1], target / f"{n}.png")
        (target / f"{n}.txt").write_text(scene[3] + "\n", encoding="utf-8")
        print(target / f"{n}.png", "<-", pictures[-1].name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("key", choices=[*CHARACTERS, "install", "poses", "bases", "emotions", "pack", "edit", "workflows", "details", "mixed"])
    parser.add_argument("--run", action="store_true", help="details: draw each detail photo once")
    parser.add_argument("target", nargs="?")
    parser.add_argument("--only", help="scene numbers, 1-20 (poses, bases, emotions: costume numbers, 1-4)")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--server", default="http://127.0.0.1:8188")
    args = parser.parse_args()
    if args.key == "install":
        install(args.target)
        return
    if args.key == "pack":
        for key in [args.target] if args.target else POSES:
            pack(key)
        return
    if args.key == "mixed":  # mixed [--run]: detail photos with her part in anime style in a real photo (MIXED_DETAILS).
        mixed(args.server, args.run)
        return
    if args.key == "details":  # details [key] [--run]: Z-Image detail photos (her things) - workflows, and drawn once with --run.
        details(args.server, [args.target] if args.target else list(DETAILS), args.run,
                {int(n) for n in args.only.split(",")} if args.only else None)
        return
    if args.key == "workflows":  # workflows [key]: ComfyUI workflows/launchme/<key>/<key>_feed.json, <key>/<costume>/..._1_poza|2_baza|3_emocje.json
        for key in [args.target] if args.target else POSES:
            workflows(args.server, key)
        return
    info = wf.http(args.server, "/object_info")
    only = {int(n) for n in args.only.split(",")} if args.only else None
    if args.key == "edit":  # edit <key> --only N: her newest pose and base drawn again with the costume's current outfit.
        from PIL import Image
        for i, costume in enumerate(POSES[args.target]):
            if only and i + 1 not in only:
                continue
            for stage, folder, stem in (("pose", "poses_v2", "pose"), ("base", "emotions_v2", "baza")):
                source = newest(COMFY / "output" / folder / args.target / costume, stem)
                if not source:
                    continue
                picture = f"v2_{args.target}_{costume}_{stem}_edit.png"
                shutil.copy2(source, COMFY / "input" / picture)
                with Image.open(source) as image:
                    size = image.size
                seed = args.seed if args.seed is not None else CHARACTERS[args.target]["seed"] + 1100 + i
                saved = wf.run(args.server, edit_prompt_for(info, args.target, costume, picture, size, stage, seed))
                print(f"edit {args.target}/{costume} {stage} from {source.name}:", *(f for _, f in saved), flush=True)
        return
    if args.key in ("poses", "bases", "emotions"):  # <stage> [key] [--only 3,4]: every (or the chosen) costume of one (or each).
        offset = {"poses": 500, "bases": 700, "emotions": 900}[args.key]
        for key in [args.target] if args.target else POSES:
            for i, costume in enumerate(POSES[key]):
                if only and i + 1 not in only:
                    continue
                seed = (args.seed if args.seed is not None else CHARACTERS[key]["seed"] + offset) + i
                if args.key == "poses":
                    saved = saki.check(args.server, pose_prompt_for(info, key, costume, seed), "1. przebieg", single=True)
                elif args.key == "bases":
                    saved = check_base(args.server, base_prompt_for(info, key, costume, seed))
                else:
                    base = newest(COMFY / "output" / "emotions_v2" / key / costume, "baza")
                    if not base:
                        print(f"emotions {key}/{costume}: no base yet (run: bases)", flush=True)
                        continue
                    picture = f"v2_{key}_{costume}_baza.png"
                    frame = prepare_base(base, COMFY / "input" / picture)
                    saved = wf.run(args.server, emotions_prompt_for(info, key, costume, picture, frame, seed))
                print(f"{args.key} {key}/{costume}:", len(saved), "pictures", *(f for _, f in saved[:1]), flush=True)
        return
    base = args.seed if args.seed is not None else CHARACTERS[args.key]["seed"]
    for number in range(len(SCENES[args.key])):
        if only and number + 1 not in only:
            continue
        saved = saki.check(args.server, prompt_for(info, args.key, number, base + number), "1. przebieg", single=True)
        keep_newest(args.key, number)
        print(f"{args.key} {number + 1:02d}:", *(f for _, f in saved), flush=True)


if __name__ == "__main__":
    main()

"""Saki v2 on the local ComfyUI (comfy_workflows.py: Unholy Desire Mix Anima 2B + the Kitagawa Marin LoRA): four
costumes - a bust base, an entrance pose and 20 feelings each - and 20 Launchfeed photos with realistic backgrounds.

    python tools/companion_gen/saki_v2.py test|bases|poses|emotions|feed [--only a,b] [--server http://127.0.0.1:8188]

ComfyUI output/saki_v2/<costume>/baza_*, pose_*, <emotion>_*;  output/saki_v2/feed/<NN>_*.
emotions needs bases (each base is copied into ComfyUI's input folder as saki_v2_<costume>_baza.png)."""
import argparse, copy, importlib.util, os, pathlib, shutil, sys

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("comfy_workflows", HERE / "comfy_workflows.py")
wf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wf)

COMFY = pathlib.Path(os.environ.get("COMFY_SHARED", str(pathlib.Path.home() / "ComfyUI")))
ROOT = "saki_v2"
SEED = 20261021

# Each costume: the whole outfit (pose), only what a bust shows (base, feelings), and the entrance pose.
COSTUMES = {
    "costume_00": ("white shirt, beige skirt, black tights, long camel coat, black ankle boots, belt",
                   "white shirt, long camel coat",
                   "full body, standing, hand on own hip, holding smartphone, shopping bag, one eye closed, smile, looking at viewer"),
    "costume_10_beach": ("light tan, sunglasses on head, hibiscus hair flower, open coral hoodie over white bikini, denim shorts, sandals",
                         "light tan, sunglasses on head, hibiscus hair flower, open coral hoodie over white bikini top",
                         "full body, sitting on inflatable swim ring, crossed legs, holding tropical drink with straw, smile, looking at viewer"),
    "costume_20_magical": ("magical girl, sparkling pink and gold magical girl outfit, big pink bow on chest, puffy sleeves, heart-shaped brooch, "
                           "tiara, frilled pink skirt, white boots",
                           "magical girl, sparkling pink and gold magical girl outfit, big pink bow on chest, puffy sleeves, heart-shaped brooch, tiara",
                           "full body, magical girl pose, standing on one leg, leg up, holding lipstick-shaped wand, pointing wand at viewer, "
                           "one eye closed, sparkle, smile"),
    "costume_30_pajama": ("fluffy pastel pink pajama hoodie with bunny ears on the hood, pastel pink pajama shorts, bunny slippers, hair bun, "
                          "messy hair, under-eye patches",
                          "fluffy pastel pink pajama hoodie with bunny ears on the hood, hair bun, messy hair, under-eye patches",
                          "full body, sitting, crossed legs, hugging pillow, yawning, sleepy, looking at viewer"),
}

# Launchfeed photos: outfit, shot and pose, place, what the picture shows (feed/saki/<n>.txt, for her caption).
FEED = [
    ("black turtleneck, long camel coat, scarf",
     "cowboy shot, leaning on bridge railing, looking back at viewer, gentle smile, wind in hair",
     "Pont des Arts bridge over the Seine at night, Paris city lights, reflections on the river",
     "Saki at night on a bridge over the Seine, leaning on the railing in a camel coat and scarf and looking back over her shoulder. "
     "The city lights of Paris shimmer on the river behind her. A quiet evening walk after a long day."),
    ("black fitted long-sleeve crop top, high-waisted straight blue jeans, white sneakers",
     ("full body, mirror selfie, reflection, face reflection in mirror, back view, turned away from camera, from behind, standing in "
      "front of full-length mirror, holding smartphone, her face is only visible in the mirror"),
     "apartment hallway with a full-length mirror, warm evening lamp, coat rack",
     "A mirror selfie in her hallway in the evening: Saki stands with her back to the camera and only her face shows in the "
     "full-length mirror, phone in hand. A black crop top and high-waisted jeans - checking the look before going out."),
    ("oversized cream knit sweater",
     "upper body, sitting at counter, eating ramen, holding chopsticks, noodles, blush, happy",
     "small ramen shop counter, steam from the bowl, warm lights, wooden interior, menu boards",
     "Saki at a tiny ramen shop counter, in an oversized cream sweater, lifting noodles with chopsticks from a steaming bowl. "
     "Warm lights and wooden walls behind her; she is clearly enjoying it a lot."),
    ("white blouse, light denim jacket",
     "cowboy shot, waving at viewer, holding paper bag of vegetables, big smile",
     "open-air vegetable market in Paris, wooden stalls with fresh vegetables and fruit, morning sunlight",
     "Saki at an open-air morning market in Paris, holding a paper bag full of vegetables and waving at the camera with a big smile. "
     "Wooden stalls with fresh produce in the sunlight behind her."),
    ("pastel pink cardigan, white top",
     "upper body, sitting at cafe table, holding iced matcha latte, smile, looking at viewer",
     "cozy cafe in Le Marais, window light, plants, marble table",
     "Saki in a cozy café in Le Marais, holding an iced matcha latte at a marble table by the window, in a pastel pink cardigan. "
     "Soft daylight and plants around her - her favourite kind of afternoon."),
    ("white t-shirt, beige trench coat",
     "cowboy shot, holding up a dress on a hanger, excited, open mouth, looking at viewer",
     "fashion boutique, clothing racks, warm spotlights",
     "Saki in a fashion boutique, holding up a dress on a hanger against herself and looking excited. Racks of clothes and warm "
     "spotlights behind her - she found something she really wants."),
    ("light blue silk pajama shirt",
     "upper body, sitting at vanity, applying lipstick, holding compact mirror, looking at viewer",
     "bedroom vanity table with makeup products, ring light, evening",
     "Saki at her vanity table in the evening, putting on lipstick with a small compact mirror in her hand. Makeup products and a ring "
     "light around her - getting ready to go out."),
    ("cream knit sweater",
     "upper body, showing nails to viewer, hands up, manicure, pink nails, smile",
     "nail salon, shelves of nail polish bottles, bright clean lights",
     "Saki at the nail salon, holding her hands up to show off a fresh pink manicure. Shelves of colourful polish bottles behind her."),
    ("white sundress, light denim jacket",
     "cowboy shot, leaning on railing, holding glass of lemonade, sunset glow on face, smile",
     "Paris rooftop terrace at sunset, Eiffel tower in the distance, golden sky",
     "Saki on a Paris rooftop terrace at sunset, a glass of lemonade in her hand and the Eiffel Tower in the distance. "
     "Golden light on her face and a calm, happy smile."),
    ("beige trench coat, white turtleneck",
     "cowboy shot, holding transparent umbrella, rain, looking at viewer, smile",
     "rainy Paris street at night, wet cobblestones, shop lights reflecting on the street",
     "Saki on a rainy Paris street at night under a transparent umbrella, in a beige trench coat. Wet cobblestones reflect the "
     "shop lights behind her."),
    ("white puffer jacket, red knit scarf, white beanie",
     "upper body, holding cup of mulled wine with both hands, breath vapor, smile, looking at viewer",
     "Christmas market at night, string lights, wooden stalls, light snow",
     "Saki at a Christmas market at night, holding a warm cup of mulled wine with both hands, in a white puffer jacket and red scarf. "
     "Fairy lights and wooden stalls behind her, a little snow falling."),
    ("pink hoodie, denim skirt",
     "cowboy shot, holding cotton candy, peace sign, one eye closed, smile",
     "amusement park, ferris wheel in the background, sunny day",
     "Saki at an amusement park with a big pink cotton candy, winking and showing a peace sign. A ferris wheel behind her on a sunny day."),
    ("light tan, sunglasses on head, hibiscus hair flower, open coral hoodie over white bikini, denim shorts",
     "full body, walking on beach, holding sandals in hand, wind in hair, smile, looking at viewer",
     "sandy beach at sunset, ocean waves, Nice coastline",
     "Saki walking barefoot on the beach in Nice at sunset, sandals in her hand, in a coral hoodie over a bikini and denim shorts. "
     "Waves and a golden sky behind her."),
    ("magical girl, sparkling pink and gold magical girl outfit, big pink bow on chest, puffy sleeves, tiara, frilled pink skirt",
     "cowboy shot, magical girl pose, peace sign, one eye closed, smile, cosplay",
     "anime convention hall, colorful booths and banners",
     "Saki in her pink-and-gold magical girl cosplay at an anime convention, winking with a peace sign. Colourful booths and banners "
     "behind her - her brother dragged her here and she ended up loving it."),
    ("striped breton shirt, black beret",
     "upper body, holding baguette and paper bag, smile, looking at viewer",
     "Parisian bakery counter with pastries, morning light",
     "Saki at a Parisian bakery in the morning, in a striped breton shirt and a black beret, holding a baguette and a paper bag of pastries."),
    ("floral sundress",
     "cowboy shot, holding bouquet of peonies, smelling flowers, closed eyes, smile",
     "flower shop entrance, buckets of flowers, sunny street",
     "Saki outside a flower shop on a sunny street, smelling a big bouquet of pink peonies with her eyes closed, in a floral sundress."),
    ("black leather jacket, white t-shirt",
     "cowboy shot, standing, holding handrail, earphones, looking at viewer",
     "Paris metro train interior, evening",
     "Saki on the Paris metro in the evening, holding the handrail with earphones in, in a black leather jacket and white t-shirt."),
    ("fluffy pastel pink pajama hoodie with bunny ears on the hood",
     "upper body, sheet face mask, holding smartphone, peace sign",
     "bathroom with skincare products, warm light, evening",
     "Saki at home in the evening in her bunny pyjama hoodie, wearing a sheet face mask and showing a peace sign. Skincare products "
     "on the shelf behind her - her self-care night."),
    ("light pink blouse, cream cardigan",
     "upper body, looking up, cherry blossoms, falling petals, smile",
     "park with cherry blossom trees in spring, sunny day",
     "Saki under cherry blossom trees in spring, looking up at the falling petals with a soft smile, in a light pink blouse and cream cardigan."),
    ("white blouse, black mini skirt, ankle boots, shoulder bag",
     "full body, walking, holding many shopping bags, sunglasses on head, smile, looking at viewer",
     "Champs-Elysees shopping street, sunny afternoon",
     "Saki walking down the Champs-Elysees on a sunny afternoon with lots of shopping bags, sunglasses pushed up on her head. "
     "A very successful shopping day."),
    ("cream knit sweater",
     "close-up, portrait, face resting on hands, chin on hands, long nails, manicure, pink nails, looking at viewer, gentle smile",
     "cafe table by the window, soft daylight",
     "A close-up of Saki resting her face on her hands at a cafe table, showing off her long pink manicured nails with a soft smile."),
]
TEST = [0, 1, 2, 3]  # The four scenes asked for first: bridge at night, mirror, ramen, market.
SEEDS = {1: 404}  # Feed photo index -> its seed, where the default one drew the scene wrong (the mirror's reflection).
MIRROR_KEEP = ("multiple views", "clone", "2girls", "multiple girls")  # A reflection is still only her.


CASCADE = HERE.parent / "lbpcascade_animeface.xml"


def faces(path):
    """Anime faces on a picture (tools/companion_faces.py's detector); None when OpenCV 4 / the cascade is missing."""
    try:
        import cv2, numpy as np
        from PIL import Image
    except ImportError:
        return None
    if not hasattr(cv2, "CascadeClassifier") or not CASCADE.exists():
        return None
    cascade = cv2.CascadeClassifier(str(CASCADE))
    grey = cv2.equalizeHist(cv2.cvtColor(np.asarray(Image.open(path).convert("RGB")), cv2.COLOR_RGB2GRAY))
    side = min(grey.shape)
    return len(cascade.detectMultiScale(grey, scaleFactor=1.05, minNeighbors=4, minSize=(side // 10, side // 10)))


def check(server, prompt, seed_title=None, single=False, tries=3):
    """Run it; with `single`, a picture with two faces (two of her) is drawn again with another seed."""
    for attempt in range(tries):
        saved = wf.run(server, prompt)
        found = [faces(COMFY / "output" / s / f) for s, f in saved] if single else []
        if not single or all(n is None or n <= 1 for n in found):
            return saved
        print(f"  {saved}: {found} faces - another seed", flush=True)
        node = next(n for n in prompt.values() if n["_meta"]["title"].startswith(seed_title))
        node["inputs"]["seed"] += 7919
    return saved


def base_prompt(info, costume):
    full, bust, _ = COSTUMES[costume]
    prompt = copy.deepcopy(wf.pose_workflow(info).prompt())
    wf.set_value(prompt, "1a. Stroj", bust)
    wf.set_value(prompt, "2. Poza", "standing, arms at sides, light smile, looking at viewer")
    wf.set_value(prompt, "3. Kadr i tlo", "upper body, portrait, looking at viewer, centered, head fully in frame, " + wf.GREEN)
    wf.set_value(prompt, "832x1216", 1216, "width")
    wf.set_value(prompt, "832x1216", 832, "height")
    wf.set_value(prompt, "1. przebieg", SEED, "seed")
    wf.set_value(prompt, "Folder zapisu", f"{ROOT}/{costume}")
    wf.set_value(prompt, "Zapis: pose", "baza", "string_b")
    return prompt


def pose_prompt(info, costume):
    full, _, pose = COSTUMES[costume]
    prompt = copy.deepcopy(wf.pose_workflow(info).prompt())
    wf.set_value(prompt, "1a. Stroj", full)
    wf.set_value(prompt, "2. Poza", pose)
    wf.set_value(prompt, "1. przebieg", SEED + 1, "seed")
    wf.set_value(prompt, "Folder zapisu", f"{ROOT}/{costume}")
    return prompt


def emotions_prompt(info, costume, picture):
    _, bust, _ = COSTUMES[costume]
    prompt = copy.deepcopy(wf.emotions_workflow(info).prompt())
    wf.set_value(prompt, "1a. Stroj", bust)
    wf.set_value(prompt, "Poza (stroj i twarz)", picture, "image")
    wf.set_value(prompt, "Do 1024x1536", 1216, "width")   # The base already is a bust:
    wf.set_value(prompt, "Do 1024x1536", 832, "height")   # keep all of it.
    wf.set_value(prompt, "Kadr popiersia", 1216, "width")
    wf.set_value(prompt, "Kadr popiersia", 832, "height")
    wf.set_value(prompt, "Kadr popiersia", 0, "y")
    wf.set_value(prompt, "Seed (wspolny)", SEED + 2)
    wf.set_value(prompt, "Folder zapisu", f"{ROOT}/{costume}")
    return prompt


def feed_prompt(info, number, seed):
    outfit, doing, place, _ = FEED[number]
    prompt = copy.deepcopy(wf.photo_real_workflow(info).prompt())
    wf.set_value(prompt, "1a. Stroj", outfit)
    wf.set_value(prompt, "2. Co robi", doing)
    wf.set_value(prompt, "3. Miejsce", place)
    wf.set_value(prompt, "1. przebieg", seed, "seed")
    wf.set_value(prompt, "Folder zapisu", f"{ROOT}/feed")
    wf.set_value(prompt, "Zapis: zdjecie", f"{number + 1:02d}", "string_b")
    if "mirror" in doing:
        negative = next(n for n in prompt.values() if n["_meta"]["title"].startswith("Negatyw (wspolny)"))
        negative["inputs"]["value"] = ", ".join(t for t in negative["inputs"]["value"].split(", ") if t not in MIRROR_KEEP)
    return prompt


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stage", choices=["test", "bases", "poses", "emotions", "feed"])
    parser.add_argument("--only", help="comma-separated costumes, or feed numbers (1-20)")
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--server", default="http://127.0.0.1:8188")
    args = parser.parse_args()
    info = wf.http(args.server, "/object_info")
    only = set(args.only.split(",")) if args.only else None
    if args.stage in ("test", "feed"):
        numbers = TEST if args.stage == "test" else range(len(FEED))
        for number in numbers:
            if only and str(number + 1) not in only:
                continue
            prompt = feed_prompt(info, number, SEEDS.get(number, args.seed + number))
            saved = check(args.server, prompt, "1. przebieg", single="mirror" not in FEED[number][1])
            print(f"feed {number + 1:02d}:", *(COMFY / "output" / s / f for s, f in saved), flush=True)
        return
    for costume in COSTUMES:
        if only and costume not in only:
            continue
        if args.stage == "bases":
            saved = check(args.server, base_prompt(info, costume))
        elif args.stage == "poses":
            saved = check(args.server, pose_prompt(info, costume))
        else:
            bases = sorted((COMFY / "output" / ROOT / costume).glob("baza_*.png"))
            if not bases:
                sys.exit(f"{costume}: no base yet (run: bases)")
            picture = f"saki_v2_{costume}_baza.png"
            shutil.copy2(bases[-1], COMFY / "input" / picture)
            saved = check(args.server, emotions_prompt(info, costume, picture))
        print(f"{costume} {args.stage}:", len(saved), "pictures", flush=True)


if __name__ == "__main__":
    main()

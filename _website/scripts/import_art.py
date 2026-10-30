"""Copies SteamGridDB artwork the launcher already cached (%LOCALAPPDATA%\\Launchme) into public/img/art.

Run locally only (npm run art); CI uses the committed WebP files. Reads image caches and the
public Steam store details the launcher cached in metadata/. It never opens key or token files.
"""
import json
import hashlib
import os
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CACHE = Path(os.path.expandvars(r"%LOCALAPPDATA%\Launchme"))
OUT = ROOT / "public" / "img" / "art"

# slug: Steam app id. Every one has a cover, poster, landscape, hero and logo cached.
GAMES = {
    "clair": "1903340",
    "silksong": "1030300",
    "liesofp": "1627720",
    "cyberpunk": "1091500",
    "bg3": "1086940",
    "wukong": "2358720",
    "ds2": "3280350",
    "silenthillf": "2947440",
    "onimusha": "2638890",
    "castlevania": "4231820",
    "ebb": "2057760",
    "kena": "1954200",
    "seaofstars": "1244090",
    "cuphead": "268910",
    "p3r": "2161700",
    "witcher3": "292030",
    "ff7rebirth": "2909400",
    "kcd2": "1771300",
    "indiana": "2677660",
    "doom": "3017860",
    "mina": "1875580",
    "ac6": "1888160",
    "palecoins": "2438330",
    "zeroparades": "2863680",
}

# Steam's cached store pages are in Polish here; English names and one-line blurbs for the site.
ENGLISH = {
    "clair": ("Clair Obscur: Expedition 33", "Lead Expedition 33 to destroy the Paintress before she paints death again."),
    "silksong": ("Hollow Knight: Silksong", "Discover a vast, haunted kingdom as Hornet, princess-protector of Hallownest."),
    "liesofp": ("Lies of P", "A souls-like that turns the story of Pinocchio on its head, set in the dark Belle Epoque."),
    "cyberpunk": ("Cyberpunk 2077", "An open-world RPG in Night City, a megalopolis obsessed with power and body modification."),
    "bg3": ("Baldur's Gate 3", "Gather your party and return to the Forgotten Realms in a tale of fellowship and betrayal."),
    "wukong": ("Black Myth: Wukong", "An action RPG rooted in Chinese mythology, on the trail of the Destined One."),
    "ds2": ("Death Stranding 2: On the Beach", "Sam and his companions set out on a new journey to save humanity from extinction."),
    "silenthillf": ("Silent Hill f", "Fog swallows Hinako's hometown in 1960s Japan, and she must fight to survive."),
    "onimusha": ("Onimusha: Way of the Sword", "Armed with the Oni Gauntlet, a samurai faces the Genma in Edo-period Kyoto."),
    "castlevania": ("Castlevania: Belmont's Curse", "A crimson curse: will you find despair or salvation at the end of the night?"),
    "ebb": ("Esoteric Ebb", "A disco-ball of a fantasy RPG where every skill check is a choice."),
    "kena": ("Kena: Bridge of Spirits", "A story-driven action adventure about a young spirit guide and her tiny Rot."),
    "seaofstars": ("Sea of Stars", "A turn-based RPG inspired by the classics, with Solstice Warriors and a lot of heart."),
    "cuphead": ("Cuphead", "A run-and-gun about one-on-one boss fights, drawn like a 1930s cartoon."),
    "p3r": ("Persona 3 Reload", "Step into the shoes of a transfer student thrown into the Dark Hour."),
    "witcher3": ("The Witcher 3: Wild Hunt", "Geralt of Rivia searches for the child of prophecy in a war-torn open world."),
    "ff7rebirth": ("Final Fantasy VII Rebirth", "Cloud and his friends leave Midgar behind and head into the wide world."),
    "kcd2": ("Kingdom Come: Deliverance II", "A story-driven open-world RPG in 15th-century Bohemia."),
    "indiana": ("Indiana Jones and the Great Circle", "Uncover one of history's greatest mysteries in a first-person adventure."),
    "doom": ("DOOM: The Dark Ages", "The prequel to DOOM (2016): a medieval war against Hell."),
    "mina": ("Mina the Hollower", "A gothic action adventure from the makers of Shovel Knight."),
    "ac6": ("Armored Core VI: Fires of Rubicon", "Build your mech and fight across Rubicon 3 for a substance called Coral."),
    "palecoins": ("Pale Coins", "A dark fantasy roguelike about a debt that can only be paid in coin."),
    "zeroparades": ("Zero Parades", "A spy RPG from the makers of Disco Elysium."),
}
GENRES = {"Akcja": "Action", "Przygodowe": "Adventure", "Niezależne": "Indie", "RPG": "RPG", "Strategie": "Strategy",
          "Symulacje": "Simulation", "Wyścigi": "Racing", "Sportowe": "Sports", "Rekreacyjne": "Casual"}

KINDS = {
    # kind: (cache folder, file pattern, output folder, target size, mode)
    "cover": ("covers", "{}.v2.img", "c", (320, 320), "fill"),
    "poster": ("posters", "{}.v2.img", "p", (300, 450), "fill"),
    "landscape": ("landscapes", "{}.v2.img", "l", (460, 215), "fill"),
    "hero": ("heroes", "{}.v2.hd.img", "h", (1280, None), "width"),
    "logo": ("logos", "{}.v2.img", "g", (640, 220), "fit"),
}


def fill(image: Image.Image, size):
    w, h = size
    scale = max(w / image.width, h / image.height)
    resized = image.resize((max(w, round(image.width * scale)), max(h, round(image.height * scale))), Image.LANCZOS)
    left = (resized.width - w) // 2
    top = (resized.height - h) // 2
    return resized.crop((left, top, left + w, top + h))


def accent(image: Image.Image) -> str:
    """A saturated, bright colour of the cover, like the launcher's focus outline."""
    small = image.convert("RGB").resize((40, 40))
    best, score = (200, 200, 210), -1.0
    for r, g, b in small.get_flattened_data():
        mx, mn = max(r, g, b), min(r, g, b)
        sat = 0 if mx == 0 else (mx - mn) / mx
        value = mx / 255
        s = sat * 1.4 + value * (0.6 if value > 0.35 else 0)
        if s > score:
            best, score = (r, g, b), s
    r, g, b = best
    # Lift dark picks so the outline reads on black.
    lift = max(0, 170 - max(best))
    return "#{:02x}{:02x}{:02x}".format(min(255, r + lift), min(255, g + lift), min(255, b + lift))


def details(app_id: str):
    path = CACHE / "metadata" / f"{app_id}.details.json"
    data = json.loads(path.read_text(encoding="utf-8")).get(app_id, {}).get("data", {})
    date = (data.get("release_date") or {}).get("date", "")
    year = next((int(part) for part in date.replace(",", " ").split() if part.isdigit() and len(part) == 4), None)
    return {
        "name": data.get("name", app_id).replace("\u2122", "").replace("\u00ae", "").strip(),
        "year": year,
        "genres": [g["description"] for g in data.get("genres", [])][:2],
        "developer": (data.get("developers") or [""])[0],
        "desc": data.get("short_description", ""),
    }


def main():
    games = []
    for slug, app_id in GAMES.items():
        info = details(app_id)
        name, blurb = ENGLISH[slug]
        entry = {
            "id": slug,
            "app": app_id,
            "name": name,
            "year": info["year"],
            "genres": {"en": [GENRES.get(g, g) for g in info["genres"]], "pl": info["genres"]},
            "developer": info["developer"],
            "desc": {"en": blurb, "pl": info["desc"].split(". ")[0].rstrip(".") + "."},
        }
        digest = int(hashlib.sha256(slug.encode()).hexdigest(), 16)
        minutes = 20 + digest % (60 * 90)
        entry["time"] = f"{minutes // 60} h" if minutes >= 90 else f"{minutes} min"
        total = 30 + digest % 50
        entry["ach"] = [digest // 7 % (total + 1), total]
        for kind, (folder, pattern, out_dir, size, mode) in KINDS.items():
            source = CACHE / folder / pattern.format(app_id)
            image = Image.open(source)
            target = OUT / out_dir / f"{slug}.webp"
            target.parent.mkdir(parents=True, exist_ok=True)
            if mode == "fill":
                result = fill(image.convert("RGB"), size)
                if kind == "cover":
                    entry["accent"] = accent(result)
                result.save(target, "WEBP", quality=78, method=6)
            elif mode == "width":
                w = size[0]
                rgb = image.convert("RGB")
                rgb = rgb.resize((w, round(rgb.height * w / rgb.width)), Image.LANCZOS) if rgb.width > w else rgb
                rgb.save(target, "WEBP", quality=68, method=6)
            else:
                rgba = image.convert("RGBA")
                bbox = rgba.getchannel("A").getbbox()
                if bbox:
                    rgba = rgba.crop(bbox)
                rgba.thumbnail(size, Image.LANCZOS)
                rgba.save(target, "WEBP", quality=86, method=6)
        games.append(entry)
        print(slug, entry["year"], entry["accent"])
    (ROOT / "src" / "data" / "games.json").write_text(json.dumps(games, ensure_ascii=False, indent=1), encoding="utf-8")
    total = sum(f.stat().st_size for f in OUT.rglob("*.webp"))
    print(f"{len(games)} games, {total / 1024 / 1024:.2f} MB")


# One sprite per companion (res/companion in the launchme repo), shown in the companion section.
COMPANIONS = {"yuki": "smug", "saki": "excited", "akane": "smile", "kiko": "angry", "mika": "cool"}


def companions():
    source = next((p for p in (ROOT.parent / "res" / "companion", ROOT.parent / "launchme" / "res" / "companion") if p.exists()), None)
    if source is None:
        print("res/companion not found, skipping companions")
        return
    out = ROOT / "public" / "img" / "companion"
    out.mkdir(parents=True, exist_ok=True)
    for name, face in COMPANIONS.items():
        image = Image.open(source / name / "costume_00" / f"{face}.png").convert("RGBA")
        bbox = image.getchannel("A").getbbox()
        if bbox:
            image = image.crop(bbox)
        image.thumbnail((420, 620), Image.LANCZOS)
        image.save(out / f"{name}.webp", "WEBP", quality=84, method=6)
        print("companion", name, image.size)


if __name__ == "__main__":
    main()
    companions()

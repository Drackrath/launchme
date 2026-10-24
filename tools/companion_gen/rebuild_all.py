"""Redraw every companion with the local ComfyUI (comfy.py, Qwen Image 2.1) into one folder the app can take in:

    python tools/companion_gen/rebuild_all.py [--only yuki,kiko] [--skip-selfies] [--export-only]

    <EXPORT>\\res\\companion\\<char>\\character.txt                 (copied from the repository)
    <EXPORT>\\res\\companion\\<char>\\<costume>\\<emotion>.png      (557x475, companion_sprites.py)
    <EXPORT>\\res\\companion\\<char>\\<costume>\\pose.png + costume.txt
    <EXPORT>\\companion_photos\\<char>\\<range>\\<stem>.png          (16:10 selfies)

EXPORT: %COMPANION_IMPORT% (default Desktop\\companion_import). Raw keyed pictures: %COMPANION_COMFY_OUT% (default
Desktop\\companion_comfy). Costumes: the repository's res/companion folders plus personal.COSTUMES (four each); her
feelings, poses and photos: personal.py. The identity of an existing costume is its original picture in
Desktop\\companion_gen\\<char>\\<costume>\\smile.png (else the installed one); a new costume is drawn first (outfit.png)
from her basic outfit. Existing pictures are kept: delete one (or a folder) to draw it again; --export-only rebuilds
the import folder from what is there. Nothing in the repository changes."""
import argparse, importlib.util, os, pathlib, shutil, subprocess, sys, time, zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GEN = pathlib.Path.home() / "Desktop" / "companion_gen"
WORK = pathlib.Path(os.environ.get("COMPANION_COMFY_OUT", pathlib.Path.home() / "Desktop" / "companion_comfy"))
EXPORT = pathlib.Path(os.environ.get("COMPANION_IMPORT", pathlib.Path.home() / "Desktop" / "companion_import"))
RES = ROOT / "res" / "companion"

_spec = importlib.util.spec_from_file_location("companion_personal", HERE / "personal.py")
personal = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(personal)


def say(text: str):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def comfy(*args: str):
    env = dict(os.environ, COMPANION_COMFY_OUT=str(WORK), COMPANION_PHOTOS_OUT=str(EXPORT / "companion_photos"))
    subprocess.call([sys.executable, str(HERE / "comfy.py"), *args], env=env)


def characters() -> list[str]:
    found = [p.parent.name for p in RES.glob("*/character.txt")]
    order = {}
    for key in found:
        for line in (RES / key / "character.txt").read_text(encoding="utf-8").splitlines():
            if line.startswith("order="):
                order[key] = int(line.split("=", 1)[1])
    return sorted(found, key=lambda key: (order.get(key, 999), key))


def costumes(char: str) -> list[str]:
    existing = sorted(p.name for p in (RES / char).glob("costume_*") if p.is_dir())
    new = sorted(c for (who, c) in personal.COSTUMES if who == char)
    return sorted(set(existing) | set(new) | {"costume_00"}, key=lambda c: (c != "costume_00", c))


def identity(char: str, costume: str) -> pathlib.Path:
    """Her picture in that costume: the original generator picture, else the installed one, else a new outfit drawn now."""
    for candidate in (GEN / char / costume / "smile.png", RES / char / costume / "smile.png"):
        if candidate.exists():
            return candidate
    outfit = WORK / char / costume / "outfit.png"
    if not outfit.exists():
        look = personal.COSTUMES[(char, costume)][4]
        comfy("outfit", char, costume, look, "--ref", str(identity(char, "costume_00")), "--seed", str(zlib.crc32(f"{char}/{costume}/outfit".encode()) % 10**9))
    return outfit


def draw(char: str, skip_selfies: bool):
    for costume in costumes(char):
        reference = identity(char, costume)
        say(f"{char}/{costume}: emotions from {reference}")
        comfy("emotions", char, costume, "--ref", str(reference), "--seed", str(1000 + zlib.crc32(f"{char}/{costume}".encode()) % 10**6))
        say(f"{char}/{costume}: pose")
        comfy("pose", char, costume, "--ref", str(reference), "--seed", str(77 + zlib.crc32(f"{costume}/{char}".encode()) % 10**6))
    if not skip_selfies and char in personal.SELFIES:
        say(f"{char}: selfies")
        comfy("selfie", char, "--ref", str(identity(char, "costume_00")), "--seed", "2026", *personal.SELFIES[char])


def export(char: str):
    """Her pictures at the app's size, poses cropped to her, character.txt and costume.txt beside them."""
    from PIL import Image
    target = EXPORT / "res" / "companion" / char
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(RES / char / "character.txt", target / "character.txt")
    for costume in costumes(char):
        source = WORK / char / costume
        if not (source / "smile.png").exists():
            say(f"{char}/{costume}: nothing drawn yet")
            continue
        folder = target / costume
        if folder.exists():
            shutil.rmtree(folder)
        command = [sys.executable, str(ROOT / "tools" / "companion_sprites.py"), "--images", str(source), "--look", char, "--costume", costume,
                   "--out", str(EXPORT / "res" / "companion")]
        command += ["--match-heads"] if costume == "costume_00" else ["--reference-set", str(target / "costume_00")]
        subprocess.call(command)
        if (RES / char / costume / "costume.txt").exists():
            shutil.copy2(RES / char / costume / "costume.txt", folder / "costume.txt")
        elif (char, costume) in personal.COSTUMES:
            name_pl, name_en, you_pl, you_en, _ = personal.COSTUMES[(char, costume)]
            level = int(costume.split("_")[1])
            (folder / "costume.txt").write_text(f"# Strój {char.capitalize()} (res/companion/README.md): od poziomu {level} przy włączonej grywalizacji.\n"
                                                f"name_pl={name_pl}\nname_en={name_en}\nyou_pl={you_pl}\nyou_en={you_en}\n", encoding="utf-8")
        pose = source / "pose.png"
        if pose.exists():
            image = Image.open(pose).convert("RGBA")
            box = image.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
            if box:
                pad = 8
                image = image.crop((max(0, box[0] - pad), max(0, box[1] - pad), min(image.width, box[2] + pad), min(image.height, box[3] + pad)))
            image.save(folder / "pose.png", optimize=True)


README = """Companion pictures redrawn with ComfyUI (tools/companion_gen/rebuild_all.py).

res\\companion\\<char>\\...   -> copy over the repository's res\\companion (same folder names replace the old pictures; new
                              costume folders are added). Old costume folders not listed here stay as they were.
                              Then build: CMake picks every character.txt, costume folder, emotion and pose.png up.
companion_photos\\<char>\\... -> copy into the photos folder set in Settings -> Towarzyszka AI -> Zdjęcia (by default
                              Desktop\\companion_photos). Range folders unlock at their first level ("10-19" from 10).
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", help="comma-separated characters")
    parser.add_argument("--skip-selfies", action="store_true")
    parser.add_argument("--export-only", action="store_true")
    parser.add_argument("--selfies-only", action="store_true", help="only the photos (and their descriptions); costumes untouched")
    args = parser.parse_args()
    chosen = [c for c in characters() if not args.only or c in args.only.split(",")]
    EXPORT.mkdir(parents=True, exist_ok=True)
    (EXPORT / "README.txt").write_text(README, encoding="utf-8")
    if args.selfies_only:
        for char in chosen:
            if char in personal.SELFIES:
                say(f"{char}: selfies")
                comfy("selfie", char, "--ref", str(identity(char, "costume_00")), "--seed", "2026", *personal.SELFIES[char])
        say("done")
        return
    for char in chosen:
        if not args.export_only:
            draw(char, args.skip_selfies)
        export(char)
        say(f"{char}: exported")
    say("done")


if __name__ == "__main__":
    main()

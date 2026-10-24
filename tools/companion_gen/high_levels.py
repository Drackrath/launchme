"""Her photos for high sympathy levels (60+), from a local ComfyUI (comfy.py selfie, Qwen Image 2.1).

    python tools/companion_gen/high_levels.py [scenes.txt] [--only yuki,kiko] [--out <photos folder>]

Scenes: high_levels_scenes.txt next to this script (UTF-8), one per line:
    <char>|<range>|<file stem>|<outfit, place, pose and mood>
<range> is a folder "60-69", "70-79", "80-89", "90-99": the app unlocks it once her level reaches its first number
(src/net/companion_photos.hpp: any "<from>-<to>" folder works). Lines starting with # are comments.

Edit RULES / NEGATIVE / CFG below to change the closing style rule of every prompt; the default keeps the same tasteful
rule as the other photos. With CFG above 1 the negative prompt counts (fewer extra hands), at about twice the time.
Existing pictures are kept: delete one to draw it again. Her look comes from res/companion/<char>/costume_00/smile.png
(or REFERENCES below)."""
import argparse, os, pathlib, subprocess, sys
from collections import defaultdict

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

RULES = "Tasteful and romantic, an adult woman, fully dressed or covered, non-explicit."
NEGATIVE = "extra hands, three hands, extra arms, extra fingers, deformed hands, two phones, phone screen, text, watermark, logo"
CFG = 1.0
SEED = 6060
# Her reference picture per character (default: her basic outfit's smile in res/companion).
REFERENCES: dict[str, str] = {}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenes", nargs="?", default=str(HERE / "high_levels_scenes.txt"))
    parser.add_argument("--only", help="comma-separated characters")
    parser.add_argument("--out", help="photos folder (default: %COMPANION_PHOTOS_OUT% or Desktop\\companion_photos)")
    args = parser.parse_args()
    wanted = set(args.only.split(",")) if args.only else None
    shots: dict[str, list[str]] = defaultdict(list)
    for number, line in enumerate(pathlib.Path(args.scenes).read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|", 3)
        if len(parts) != 4 or not parts[1].replace("-", "").isdigit():
            sys.exit(f"{args.scenes}:{number}: expected <char>|<from-to>|<stem>|<scene>")
        char, folder, stem, scene = (p.strip() for p in parts)
        if wanted is None or char in wanted:
            shots[char].append(f"{folder}|{stem}|{scene}")
    env = dict(os.environ)
    if args.out:
        env["COMPANION_PHOTOS_OUT"] = args.out
    for char, list_ in shots.items():
        command = [sys.executable, str(HERE / "comfy.py"), "selfie", char, "--seed", str(SEED), "--cfg", str(CFG), "--rules", RULES, "--negative", NEGATIVE]
        if char in REFERENCES:
            command += ["--ref", REFERENCES[char]]
        subprocess.call(command + list_, env=env)


if __name__ == "__main__":
    main()

"""Find her face on every companion picture and write faces.txt beside them (res/companion/README.md), so a round
avatar (desktop sidebar, Flix pill) can centre her face whatever the picture's framing:

    python tools/companion_faces.py [--only saki,akane] [--root res/companion] [--preview <png>]

    res/companion/<key>/<costume>/faces.txt:   face_<emotion>=<x> <y> <size>   (pixels of the 560x560 canvas)

Detection: nagadomi's lbpcascade_animeface (MIT; downloaded once next to this script, git-ignored) through OpenCV's
CascadeClassifier, which OpenCV 5 no longer ships: use opencv-python(-headless) 4.x, e.g.
    pip install --target %TEMP%\\cv4 "opencv-python-headless<5"   then set PYTHONPATH=%TEMP%\\cv4
Per costume, a picture whose face is missing or far from the costume's median face gets the median (a stray hand or a
wide shot). Rebuild the launcher afterwards."""
import argparse, pathlib, statistics, sys, urllib.request

import numpy as np
from PIL import Image, ImageDraw

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
CASCADE = HERE / "lbpcascade_animeface.xml"
CASCADE_URL = "https://raw.githubusercontent.com/nagadomi/lbpcascade_animeface/master/lbpcascade_animeface.xml"
EMOTIONS = ("smile angry sad excited shock thinking smug shy fear sleepy playful cool love laugh cry confused determined wave "
            "drink gaming").split()  # companion::Emotion order.


def detector():
    try:
        import cv2
    except ImportError:
        sys.exit("OpenCV is missing: pip install \"opencv-python-headless<5\"")
    if not hasattr(cv2, "CascadeClassifier"):
        sys.exit(f"OpenCV {cv2.__version__} has no CascadeClassifier (removed in 5.x): install opencv-python-headless<5 (see --help)")
    if not CASCADE.exists():
        print("downloading", CASCADE_URL, flush=True)
        urllib.request.urlretrieve(CASCADE_URL, CASCADE)
    cascade = cv2.CascadeClassifier(str(CASCADE))
    if cascade.empty():
        sys.exit(f"cannot load {CASCADE}")

    def faces(picture: Image.Image) -> list[tuple[float, float, float]]:
        flat = Image.new("RGBA", picture.size, (128, 128, 128, 255))
        flat.alpha_composite(picture.convert("RGBA"))
        grey = cv2.equalizeHist(cv2.cvtColor(np.asarray(flat.convert("RGB")), cv2.COLOR_RGB2GRAY))
        found = cascade.detectMultiScale(grey, scaleFactor=1.05, minNeighbors=3, minSize=(80, 80))
        return [(x + w / 2, y + h / 2, float(max(w, h))) for x, y, w, h in found]
    return faces


def costume_faces(folder: pathlib.Path, faces) -> dict[str, tuple[float, float, float]]:
    found = {e: faces(Image.open(folder / f"{e}.png")) for e in EMOTIONS if (folder / f"{e}.png").exists()}
    largest = [max(c, key=lambda f: f[2]) for c in found.values() if c]
    if not largest:
        return {}
    median = tuple(statistics.median(f[i] for f in largest) for i in range(3))
    out = {}
    for emotion, candidates in found.items():
        near = [f for f in candidates if abs(f[0] - median[0]) <= 0.35 * median[2] and abs(f[1] - median[1]) <= 0.45 * median[2]
                and 0.7 * median[2] <= f[2] <= 1.4 * median[2]]
        pick = max(near, key=lambda f: f[2]) if near else median
        out[emotion] = pick
        if not near:
            print(f"  {folder.parent.name}/{folder.name}/{emotion}: no face near the others, the median kept", flush=True)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", help="comma-separated character keys")
    parser.add_argument("--root", default=str(ROOT / "res" / "companion"))
    parser.add_argument("--preview", help="a contact sheet of every cool picture with its face box")
    args = parser.parse_args()
    faces = detector()
    root = pathlib.Path(args.root)
    wanted = set(args.only.split(",")) if args.only else None
    previews = []
    for character in sorted(p for p in root.iterdir() if (p / "character.txt").exists()):
        if wanted and character.name not in wanted:
            continue
        for folder in sorted(p for p in character.iterdir() if p.is_dir() and p.name.startswith("costume_")):
            result = costume_faces(folder, faces)
            if not result:
                print(f"{character.name}/{folder.name}: no face found, faces.txt not written", flush=True)
                continue
            lines = ["# Jej twarz na każdym obrazku (tools/companion_faces.py): face_<emocja>=<x> <y> <rozmiar>, w pikselach płótna 560×560."]
            lines += [f"face_{e}={x:.0f} {y:.0f} {s:.0f}" for e, (x, y, s) in result.items()]
            (folder / "faces.txt").write_bytes(("\r\n".join(lines) + "\r\n").encode("utf-8"))
            print(f"{character.name}/{folder.name}: {len(result)} faces", flush=True)
            if args.preview and "cool" in result:
                picture = Image.open(folder / "cool.png").convert("RGBA")
                x, y, s = result["cool"]
                ImageDraw.Draw(picture).rectangle((x - s / 2, y - s / 2, x + s / 2, y + s / 2), outline=(255, 0, 0, 255), width=3)
                previews.append(picture)
    if args.preview and previews:
        columns = 4
        cell_w, cell_h = previews[0].size  # The shared canvas.
        sheet = Image.new("RGBA", (cell_w * columns, cell_h * ((len(previews) + columns - 1) // columns)), (40, 40, 48, 255))
        for i, picture in enumerate(previews):
            sheet.alpha_composite(picture, ((i % columns) * cell_w, (i // columns) * cell_h))
        sheet.save(args.preview)


if __name__ == "__main__":
    main()

"""Prepare the companion's pictures (res/companion/README.md).

Every costume is a folder of cut PNGs on one shared canvas:

    res/companion/<key>/costume_<NN>[_<name>]/<emotion>[_talk|_blink].png   (with res/companion/<key>/character.txt)

costume_00 is her basic outfit; NN is the level that unlocks a costume with gamification on.

A sprite sheet is a 4x3 grid of busts on a chroma-key green background, a caption under each bust
(Smile, Angry, Sad, Excited / Shock, Thinking, Smug, Shy / Fear, Sleepy, Playful, Cool). The green
becomes alpha (soft edges, green spill removed), captions are dropped, and every bust is placed on
the shared canvas with its flat bottom edge centred, so swapping emotions never makes her jump.

Both basic outfits (sizes the canvas from both sheets):
    python tools/companion_sprites.py res/companion/source/yuki.jpg res/companion/source/akane.jpg --out res/companion

One costume (on the existing canvas, scaled down when a bust does not fit):
    python tools/companion_sprites.py --look yuki --costume costume_10_kimono sheet.png --out res/companion

Ready transparent PNGs, one per emotion (any names containing the emotion, e.g. 01_angry.png):
    python tools/companion_sprites.py --images <folder> --look akane --costume costume_10_halloween --out res/companion [--match-heads]

Templates (an empty green sheet with its grid and captions, and a guide of the canvas):
    python tools/companion_sprites.py --template res/companion/_template

Requires Pillow and NumPy (offline asset preparation; CMake embeds the results).
"""
import argparse
import pathlib
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# The twelve of a 4x3 sheet, then the later feelings and everyday moments (separate pictures only).
CLASSIC = ["smile", "angry", "sad", "excited", "shock", "thinking", "smug", "shy", "fear", "sleepy", "playful", "cool"]
EMOTIONS = CLASSIC + ["love", "laugh", "cry", "confused", "determined", "wave", "drink", "gaming"]
LOOKS = ["yuki", "akane"]
COSTUME = re.compile(r"^costume_[0-9]{2}(_[A-Za-z0-9_-]+)?$")
SHEET_SIZE = (2752, 1536)


def key(sheet: Image.Image) -> np.ndarray:
    """RGBA float array: green keyed out, edge colours un-mixed from it, spill removed."""
    rgb = np.asarray(sheet.convert("RGB")).astype(np.float32)
    spill = rgb[..., 1] - np.maximum(rgb[..., 0], rgb[..., 2])
    # The backdrop's own colour (median of clearly green pixels).
    backdrop = np.median(rgb[spill > 200], axis=0)
    # Opaque up to a slight green cast (JPEG noise on white hair), clear on the backdrop, a soft
    # ramp between them for anti-aliased edges and fine strands.
    alpha = 1.0 - np.clip((spill - 40.0) / (215.0 - 40.0), 0.0, 1.0)
    # Edge pixels are a blend of the figure and the backdrop: recover the figure's colour.
    a = np.maximum(alpha, 1e-3)[..., None]
    colour = np.where(alpha[..., None] < 1.0, (rgb - (1.0 - a) * backdrop) / a, rgb)
    colour = np.clip(colour, 0.0, 255.0)
    r, g, b = colour[..., 0], colour[..., 1], colour[..., 2]
    # Despill: green above the other channels is the backdrop's reflection; pulled down to the
    # mean of red and blue (neutral on white hair, no yellow rim on skin).
    g = np.where(g > np.maximum(r, b), np.minimum(g, (r + b) / 2.0 + 6.0), g)
    # JPEG colour bleeding tints the soft rim: it keeps its brightness but loses most of its hue,
    # and the rim is choked a little so no backdrop-coloured fringe remains.
    rim = alpha < 0.95
    luma = 0.3 * r + 0.59 * g + 0.11 * b
    r, g, b = (np.where(rim, c * 0.35 + luma * 0.65, c) for c in (r, g, b))
    alpha = np.clip((alpha - 0.12) / 0.88, 0.0, 1.0)
    alpha = np.where(alpha < 0.04, 0.0, alpha)
    return np.dstack([r, g, b, alpha])

def runs(profile: np.ndarray, gap: int, minimum: int):
    """Spans of nonzero values, joining gaps up to `gap`, at least `minimum` long."""
    spans, start, last = [], None, None
    for i, value in enumerate(profile):
        if value > 0:
            if start is None:
                start = i
            elif i - last > gap + 1:
                spans.append((start, last + 1))
                start = i
            last = i
    if start is not None:
        spans.append((start, last + 1))
    return [s for s in spans if s[1] - s[0] >= minimum]


def busts(rgba: np.ndarray):
    """The 12 busts in reading order: (crop RGBA, bottom-centre x)."""
    mask = rgba[..., 3] > 0.5
    result = []
    bands = runs(mask.sum(axis=1), gap=2, minimum=200)
    if len(bands) != 3:
        raise SystemExit(f"expected 3 rows of busts, found {len(bands)}")
    for top, bottom in bands:
        band = mask[top:bottom]
        columns = runs(band.sum(axis=0), gap=8, minimum=200)
        if len(columns) != 4:
            raise SystemExit(f"expected 4 busts in a row, found {len(columns)}")
        for left, right in columns:
            box = mask[top:bottom, left:right]
            counts = box.sum(axis=1)
            widest = counts.max()
            # Captions are narrow: walk up from the bottom to the flat cut of the bust.
            last = len(counts) - 1
            while last > 0 and counts[last] < 0.45 * widest:
                last -= 1
            rows = np.nonzero(counts[: last + 1])[0]
            first = rows[0]
            crop = rgba[top + first: top + last + 1, left:right].copy()
            # Anything below the cut (a caption touching the band) is gone; the cut row decides the centre.
            row = crop[-1, :, 3] > 0.5
            xs = np.nonzero(row)[0]
            centre = (xs[0] + xs[-1]) / 2.0 if len(xs) else crop.shape[1] / 2.0
            # Columns that only hold stray noise around the bust are trimmed.
            cols = np.nonzero(crop[..., 3].max(axis=0) > 0.5)[0]
            crop = crop[:, cols[0]: cols[-1] + 1]
            centre -= cols[0]
            result.append((crop, centre))
    return result


def place(sprite: np.ndarray, centre: float, width: int, height: int) -> Image.Image:
    """The bust on the shared canvas: bottom edge on the canvas's, bottom centre centred."""
    if sprite.shape[0] > height or max(centre, sprite.shape[1] - centre) * 2 > width:
        scale = min(height / sprite.shape[0], width / (2 * max(centre, sprite.shape[1] - centre)))
        image = Image.fromarray(np.clip(sprite * [1, 1, 1, 255], 0, 255).astype(np.uint8), "RGBA")
        image = image.resize((max(1, int(sprite.shape[1] * scale)), max(1, int(sprite.shape[0] * scale))), Image.LANCZOS)
        sprite = np.asarray(image).astype(np.float32) / [1, 1, 1, 255]
        centre *= scale
    canvas = np.zeros((height, width, 4), dtype=np.float32)
    x = int(round(width / 2 - centre))
    y = height - sprite.shape[0]
    canvas[y:, x: x + sprite.shape[1]] = sprite
    rgba = canvas.copy()
    rgba[..., 3] *= 255.0
    # Fully transparent pixels carry no colour (smaller files, no fringes when scaled).
    rgba[rgba[..., 3] < 1.0] = 0
    return Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8), "RGBA")


def save(folder: pathlib.Path, items, width: int, height: int):
    folder.mkdir(parents=True, exist_ok=True)
    images = []
    for name, (sprite, centre) in zip(CLASSIC, items):
        image = place(sprite, centre, width, height)
        image.save(folder / f"{name}.png", optimize=True)
        images.append(image)
    return images


def head_width(image: Image.Image, depth: int):
    """Opaque width `depth` pixels below the top of her hair, and that top (her head's size)."""
    alpha = np.asarray(image.getchannel("A")) > 128
    rows = np.nonzero(alpha.any(axis=1))[0]
    if not len(rows):
        return 0, 0
    top = rows[0]
    xs = np.nonzero(alpha[min(alpha.shape[0] - 1, top + depth)])[0]
    return (xs[-1] - xs[0] if len(xs) else 0), top


def images(folder: pathlib.Path, out: pathlib.Path, width: int, height: int, match_heads: bool = False, reference: pathlib.Path | None = None):
    """Ready transparent PNGs, one per emotion (its name anywhere in the file name, e.g.
    01_angry_transparent.png), on the shared canvas: one scale for all of them (the tallest fills
    the canvas, the widest bust fits), bottom edges on the canvas's, bottom centres centred.
    match_heads: pictures drawn from further away are enlarged until her head is as wide as in the
    others (its top level with theirs, the chest cut lower).
    reference: a costume folder already on the canvas (adding feelings to it): every new picture gets
    its head size and height."""
    found = {}
    for file in sorted(folder.glob("*.png")):
        stem = file.stem.lower()
        frame = "_talk" if "talk" in stem else "_blink" if "blink" in stem else ""
        emotion = next((e for e in EMOTIONS if re.search(rf"(^|[^a-z]){e}([^a-z]|$)", stem)), None)
        if emotion:
            found[emotion + frame] = Image.open(file).convert("RGBA")
    if not found:
        raise SystemExit(f"no <emotion>.png in {folder} (names: {', '.join(EMOTIONS)})")
    boxes = {name: image.getchannel("A").point(lambda a: 255 if a > 128 else 0).getbbox() for name, image in found.items()}
    scale = min(min(height / image.height for image in found.values()),
                min(width / max(1, box[2] - box[0]) for box in boxes.values() if box))
    out.mkdir(parents=True, exist_ok=True)
    factors = {name: 1.0 for name in found}
    head_top = None
    if reference is not None:
        depth = int(height * 0.25)
        existing = [Image.open(p).convert("RGBA") for p in sorted(reference.glob("*.png")) if p.stem in EMOTIONS]
        if not existing:
            raise SystemExit(f"no pictures in {reference}")
        marks = [head_width(image, depth) for image in existing]
        typical = float(np.median([w for w, _ in marks if w]))
        head_top = int(np.median([top for _, top in marks]))
        sized = {name: image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS) for name, image in found.items()}
        factors = {name: float(np.clip(typical / w, 0.7, 1.6)) if w else 1.0
                   for name, (w, _) in ((name, head_width(image, depth)) for name, image in sized.items())}
    elif match_heads:
        depth = int(height * 0.25)
        sized = {name: image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS) for name, image in found.items()}
        heads = {name: head_width(image, depth) for name, image in sized.items()}
        typical = float(np.median([w for w, _ in heads.values() if w]))
        factors = {name: float(np.clip(typical / w, 0.85, 1.35)) if w else 1.0 for name, (w, _) in heads.items()}
        head_top = int(np.median([top for name, (_, top) in heads.items() if abs(factors[name] - 1) < 0.05] or [0]))
    saved = []
    for name, image in found.items():
        factor = factors[name]
        image = image.resize((max(1, round(image.width * scale * factor)), max(1, round(image.height * scale * factor))), Image.LANCZOS)
        rgba = np.asarray(image).astype(np.float32)
        # The bust's flat cut: its lowest opaque row; its middle is the bottom centre.
        rows = np.nonzero((rgba[..., 3] > 128).any(axis=1))[0]
        bottom = rows[-1] if len(rows) else rgba.shape[0] - 1
        y = height - (bottom + 1)
        if (factor > 1.05 or reference is not None) and head_top is not None and len(rows):
            # Her head's top level with the others', the chest cut by the canvas's edge (never floating above it).
            y = head_top - rows[0]
            if y + bottom + 1 < height:
                y = height - (bottom + 1)
            bottom = min(bottom, height - 1 - y)
        xs = np.nonzero(rgba[bottom, :, 3] > 128)[0]
        centre = (xs[0] + xs[-1]) / 2.0 if len(xs) else rgba.shape[1] / 2.0
        x = int(round(width / 2 - centre))
        layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        layer.paste(image.crop((0, 0, image.width, bottom + 1)), (x, y))
        pixels = np.asarray(layer).copy()
        pixels[pixels[..., 3] == 0] = 0  # Fully transparent pixels carry no colour.
        result = Image.fromarray(pixels, "RGBA")
        result.save(out / f"{name}.png", optimize=True)
        saved.append(result)
    missing = [e for e in EMOTIONS if e not in found]
    print(f"{len(saved)} pictures at scale {scale:.3f} -> {out}" + (f"; missing (shown as cool): {', '.join(missing)}" if missing else ""))
    return saved


def font(size: int):
    for name in ("segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def template(out: pathlib.Path, width: int, height: int):
    """An empty sheet for the cutter, and a guide of the shared canvas for cut pictures."""
    out.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGB", SHEET_SIZE, (0, 255, 0))  # Pure green, like the original sheets.
    draw = ImageDraw.Draw(sheet)
    cell_w, cell_h = SHEET_SIZE[0] // 4, SHEET_SIZE[1] // 3
    caption = font(42)
    for i, emotion in enumerate(CLASSIC):
        x, y = (i % 4) * cell_w, (i // 4) * cell_h
        # Paint each bust inside its cell, above the caption, its flat cut on the dashed line. The
        # guides are a green the keyer removes completely (green over red and blue by >= 215).
        draw.rectangle([x + 30, y + 20, x + cell_w - 30, y + cell_h - 90], outline=(0, 225, 0), width=3)
        for dash in range(x + 30, x + cell_w - 30, 24):
            draw.line([dash, y + cell_h - 90, dash + 12, y + cell_h - 90], fill=(0, 220, 0), width=4)
        draw.text((x + cell_w / 2, y + cell_h - 45), emotion.capitalize(), fill=(255, 255, 255), font=caption, anchor="mm")
    sheet.save(out / "sheet_template.png", optimize=True)
    # The canvas every cut picture shares: transparent, her flat cut on its bottom edge, centred.
    guide = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(guide)
    draw.rectangle([0, 0, width - 1, height - 1], outline=(255, 64, 160, 255), width=2)
    draw.line([width / 2, 0, width / 2, height], fill=(255, 64, 160, 120), width=1)
    draw.line([0, height - 2, width, height - 2], fill=(255, 220, 0, 255), width=3)
    small = font(18)
    draw.text((width / 2, 16), f"{width}x{height} - transparent PNG", fill=(255, 64, 160, 255), font=small, anchor="mm")
    draw.text((width / 2, height - 22), "bust's flat cut on the bottom edge, centred", fill=(255, 220, 0, 255), font=small, anchor="mm")
    guide.save(out / "canvas_guide.png", optimize=True)
    print(f"{out / 'sheet_template.png'} ({SHEET_SIZE[0]}x{SHEET_SIZE[1]}), {out / 'canvas_guide.png'} ({width}x{height})")


def canvas_size(out: pathlib.Path):
    """The shared canvas: the size of her basic outfit's pictures."""
    existing = out / LOOKS[0] / "costume_00" / "smile.png"
    if existing.exists():
        with Image.open(existing) as image:
            return image.size
    return 560, 560  # companion_ui::sprite_width/height.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sheets", nargs="*")
    parser.add_argument("--out", default="res/companion")
    parser.add_argument("--look", help="the character's key: her folder under res/companion/ (yuki, akane, hana…)")
    parser.add_argument("--costume", help="costume_<NN>[_<name>], e.g. costume_10_kimono")
    parser.add_argument("--template", help="write the empty sheet and the canvas guide to this folder")
    parser.add_argument("--images", help="a folder of ready transparent PNGs, one per emotion (with --look and --costume)")
    parser.add_argument("--match-heads", action="store_true", help="--images: enlarge pictures drawn from further away to the others' head size")
    parser.add_argument("--reference-set", help="--images: an installed costume folder whose head size and height new pictures get")
    parser.add_argument("--preview")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    if args.template:
        width, height = canvas_size(out)
        template(pathlib.Path(args.template), width, height)
        return
    previews = []
    if args.images:
        if not (args.costume and args.look) or not COSTUME.match(args.costume) or not re.match(r"^[a-z0-9][a-z0-9_-]*$", args.look):
            raise SystemExit("ready pictures: --images <folder> --look <key> --costume costume_<NN>[_<name>]")
        width, height = canvas_size(out)
        images(pathlib.Path(args.images), out / args.look / args.costume, width, height, args.match_heads,
               pathlib.Path(args.reference_set) if args.reference_set else None)
        return
    if args.costume or args.look:
        if not (args.costume and args.look) or len(args.sheets) != 1:
            raise SystemExit("one costume: --look <key> --costume costume_<NN>[_<name>] sheet.png")
        if not re.match(r"^[a-z0-9][a-z0-9_-]*$", args.look):
            raise SystemExit("the key is her folder name: lowercase letters, digits, _ or -")
        if not (out / args.look / "character.txt").exists():
            print(f"note: {out / args.look / 'character.txt'} is missing; copy one from res/companion/yuki/ and fill it in")
        if not COSTUME.match(args.costume):
            raise SystemExit("costume folder must be costume_<NN>[_<name>]: NN = 00-99, name of letters, digits, _ or -")
        width, height = canvas_size(out)
        previews = save(out / args.look / args.costume, busts(key(Image.open(args.sheets[0]))), width, height)
    else:
        if len(args.sheets) != 2:
            raise SystemExit("both basic outfits: <yuki sheet> <akane sheet>")
        looks = {look: busts(key(Image.open(sheet))) for look, sheet in zip(LOOKS, args.sheets)}
        # One canvas for every sprite of both looks: bottom edges aligned, bottom centres centred.
        half = max(max(c, s.shape[1] - c) for items in looks.values() for s, c in items)
        width, height = int(np.ceil(half * 2)) + 2, max(s.shape[0] for items in looks.values() for s, _ in items)
        for look, items in looks.items():
            previews += save(out / look / "costume_00", items, width, height)
    print(f"{len(previews)} sprites, {width}x{height}")
    if args.preview:
        sheet = Image.new("RGBA", (width * 6, height * ((len(previews) + 5) // 6)), (34, 38, 48, 255))
        for i, image in enumerate(previews):
            sheet.alpha_composite(image, ((i % 6) * width, (i // 6) * height))
        pathlib.Path(args.preview).parent.mkdir(parents=True, exist_ok=True)
        sheet.convert("RGB").save(args.preview)


if __name__ == "__main__":
    main()

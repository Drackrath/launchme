"""Prepare the site's images from the launchme repo and the desktop screenshot.

Run from _www:  python scripts/prepare_assets.py
Writes public/img/** (WebP) and src/data/assets.json (sizes + ambient colours).
Nothing is written into the launchme repository.
"""
from __future__ import annotations

import colorsys
import json
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent / "launchme"
DESKTOP = Path.home() / "Desktop"
SHOTS = REPO / "docs" / "screenshots"
OUT = ROOT / "public" / "img"
DATA = ROOT / "src" / "data"

# Screenshots used by the site (name -> source path).
SCREENS = {
    "shelves": DESKTOP / "scrn.png",
    "psfive": SHOTS / "psfive.jpg",
    "xmb": SHOTS / "xmb.jpg",
    "metro": SHOTS / "metro.jpg",
    "desktop": SHOTS / "desktop.jpg",
    "desktop-settings": SHOTS / "desktop-settings.jpg",
    "layouts": SHOTS / "layouts.jpg",
    "list-plus": SHOTS / "list-plus.jpg",
    "shelf": SHOTS / "coverflow.jpg",
    "details": SHOTS / "game-details.jpg",
    "search": SHOTS / "search.jpg",
    "widgets": SHOTS / "widgets.jpg",
    "glance": SHOTS / "glance.jpg",
    "companion": SHOTS / "companion.jpg",
}




# Controller gallery drawn by launchme (Picture enum order, 5 per row, 320x180 cells).
GALLERY = REPO / "build" / "temp" / "root-032-captures" / "controllers-family-gallery.png"
PADS = {
    "dualsense": 0, "ultimate2": 1, "xbox": 2, "dualshock4": 3, "dualsense-edge": 4,
    "xbox360": 5, "xbox-elite": 6, "switch-pro": 8, "joycon-pair": 11, "8bitdo-pro": 12,
    "8bitdo-sn": 14, "steam-controller": 15, "steam-deck": 16, "logitech": 17, "generic": 18,
    "steam-controller2": 19, "zero2": 20,
}


def save_webp(image: Image.Image, path: Path, quality: int = 82) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "WEBP", quality=quality, method=6)


def ambient(image: Image.Image) -> str:
    """A vivid but dark colour that represents the picture (for the page glow)."""
    small = image.convert("RGB").resize((64, 36), Image.LANCZOS)
    best, best_score = (40, 60, 90), -1.0
    buckets: dict[tuple[int, int, int], list[float]] = {}
    for r, g, b in list(small.get_flattened_data() if hasattr(small, 'get_flattened_data') else small.getdata()):
        h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
        if l < 0.12 or l > 0.85:
            continue
        key = (int(h * 18), 0, 0)
        entry = buckets.setdefault(key, [0, 0, 0, 0, 0])
        weight = s * (0.4 + l)
        entry[0] += r * weight; entry[1] += g * weight; entry[2] += b * weight
        entry[3] += weight; entry[4] += 1
    for entry in buckets.values():
        if entry[3] <= 0:
            continue
        score = entry[3] * (entry[4] ** 0.3)
        if score > best_score:
            best_score = score
            best = (entry[0] / entry[3], entry[1] / entry[3], entry[2] / entry[3])
    h, l, s = colorsys.rgb_to_hls(*(c / 255 for c in best))
    r, g, b = colorsys.hls_to_rgb(h, min(max(l, 0.42), 0.55), min(1.0, max(s, 0.55)))
    return "#%02x%02x%02x" % (int(r * 255), int(g * 255), int(b * 255))


def rounded_square(image: Image.Image) -> Image.Image:
    return image


def main() -> None:
    meta: dict = {"screens": {}, "covers": {}, "pads": []}
    sources: dict[str, Image.Image] = {}

    for name, path in SCREENS.items():
        image = Image.open(path).convert("RGB")
        sources[name] = image
        w, h = image.size
        large = image.resize((1600, round(1600 * h / w)), Image.LANCZOS) if w > 1600 else image
        small = image.resize((800, round(800 * h / w)), Image.LANCZOS)
        save_webp(large, OUT / "shots" / f"{name}.webp", 84)
        save_webp(small, OUT / "shots" / f"{name}-sm.webp", 80)
        # A tiny blurred placeholder, inlined as a data URL later if needed.
        blur = image.resize((32, round(32 * h / w)), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1))
        save_webp(blur, OUT / "shots" / f"{name}-blur.webp", 50)
        meta["screens"][name] = {"w": large.size[0], "h": large.size[1], "color": ambient(image)}
        print("screen", name, large.size, meta["screens"][name]["color"])

    # A full-resolution hero for big screens.
    hero = sources["shelves"]
    save_webp(hero, OUT / "shots" / "shelves-xl.webp", 86)

    # Game covers come from the launcher's artwork cache instead: scripts/import_art.py.

    if GALLERY.exists():
        gallery = Image.open(GALLERY).convert("RGBA")
        bg = gallery.getpixel((2, 2))
        for name, index in PADS.items():
            cx, cy = (index % 5) * 320, (index // 5) * 180
            cell = gallery.crop((cx, cy, cx + 320, cy + 180))
            pixels = cell.load()
            for yy in range(cell.size[1]):
                for xx in range(cell.size[0]):
                    r, g, b, _ = pixels[xx, yy]
                    d = abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2])
                    if d < 10:
                        pixels[xx, yy] = (r, g, b, 0)
                    elif d < 40:
                        pixels[xx, yy] = (r, g, b, int(255 * (d - 10) / 30))
            box = cell.getbbox()
            if box:
                cell = cell.crop(box)
            save_webp(cell, OUT / "pads" / f"{name}.webp", 90)
            meta["pads"].append({"id": name, "w": cell.size[0], "h": cell.size[1]})
        print("pads", len(meta["pads"]))
    else:
        print("controller gallery capture not found:", GALLERY)

    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / "assets.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print("wrote", DATA / "assets.json")


if __name__ == "__main__":
    main()

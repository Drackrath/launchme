"""The application icon from the launchme symbol (res/launchme_symbol.svg): the portal in light ink on a dark rounded
tile, every size drawn on its own (not scaled down from the largest), written to res/launchme.ico (Pillow + NumPy).

    python tools/launchme_icon.py [--preview <png>]
    python tools/launchme_icon.py --hicolor packaging/linux/icons

--hicolor writes the Linux icons instead: hicolor/<n>x<n>/apps/<app id>.png for the freedesktop theme sizes
and hicolor/scalable/apps/<app id>.svg, the same tile as a vector (installed by cmake/Linux.cmake).

The symbol's path is read from the SVG (M L H V A Q C Z, absolute and relative); each figure is flattened and filled
even-odd, so a hole drawn as its own figure stays a hole. Rebuild the launcher afterwards (res/launchme.rc.in)."""
import argparse, math, pathlib, re, xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
SYMBOL = ROOT / "res" / "launchme_symbol.svg"
ICON = ROOT / "res" / "launchme.ico"
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
APP_ID = "io.github.uxandai.Launchme"  # packaging/linux: desktop entry, AppStream, Flatpak.
HICOLOR = [16, 24, 32, 48, 64, 128, 256, 512]
TILE = (0x11, 0x11, 0x11)  # The logo's own ink, as the tile.
INK = (0xF2, 0xF2, 0xF2)


def path_data(svg: pathlib.Path) -> tuple[str, tuple[float, float, float, float]]:
    root = ET.parse(svg).getroot()
    box = tuple(float(v) for v in root.get("viewBox").split())
    d = " ".join(e.get("d") for e in root.iter() if e.tag.endswith("path"))
    return d, box


def arc(p0, rx, ry, phi, large, sweep, p1, steps_per_turn=720):
    """SVG endpoint arc → points (SVG 1.1 F.6.5), without the start point."""
    (x1, y1), (x2, y2) = p0, p1
    if rx == 0 or ry == 0:
        return [p1]
    rx, ry, phi = abs(rx), abs(ry), math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    xp, yp = cp * dx + sp * dy, -sp * dx + cp * dy
    scale = xp * xp / (rx * rx) + yp * yp / (ry * ry)
    if scale > 1:
        rx, ry = rx * math.sqrt(scale), ry * math.sqrt(scale)
    num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp
    coef = math.sqrt(max(0.0, num / (rx * rx * yp * yp + ry * ry * xp * xp)))
    if large == sweep:
        coef = -coef
    cxp, cyp = coef * rx * yp / ry, -coef * ry * xp / rx
    cx, cy = cp * cxp - sp * cyp + (x1 + x2) / 2, sp * cxp + cp * cyp + (y1 + y2) / 2
    angle = lambda ux, uy, vx, vy: math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
    t1 = angle(1, 0, (xp - cxp) / rx, (yp - cyp) / ry)
    dt = angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    steps = max(8, int(abs(dt) / (2 * math.pi) * steps_per_turn))
    out = []
    for i in range(1, steps + 1):
        t = t1 + dt * i / steps
        x, y = rx * math.cos(t), ry * math.sin(t)
        out.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return out


def figures(d: str) -> list[list[tuple[float, float]]]:
    tokens = re.findall(r"[MmLlHhVvAaZzQqCc]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", d)
    out, figure, i, command = [], [], 0, None
    current = start = (0.0, 0.0)
    num = lambda: float(tokens[i])
    while i < len(tokens):
        if tokens[i].isalpha():
            command = tokens[i]; i += 1
            if command in "Zz":
                if figure: out.append(figure)
                figure, current = [], start
                continue
        rel = command.islower()
        base = current if rel else (0.0, 0.0)
        c = command.upper()
        if c == "M":
            if figure: out.append(figure)
            current = (base[0] + float(tokens[i]), base[1] + float(tokens[i + 1])); i += 2
            figure, start = [current], current
            command = "l" if rel else "L"
        elif c == "L":
            current = (base[0] + float(tokens[i]), base[1] + float(tokens[i + 1])); i += 2; figure.append(current)
        elif c == "H":
            current = ((current[0] if rel else 0.0) + num(), current[1]); i += 1; figure.append(current)
        elif c == "V":
            current = (current[0], (current[1] if rel else 0.0) + num()); i += 1; figure.append(current)
        elif c == "A":
            rx, ry, phi, large, sweep = (float(tokens[i + k]) for k in range(5))
            target = (base[0] + float(tokens[i + 5]), base[1] + float(tokens[i + 6])); i += 7
            figure += arc(current, rx, ry, phi, bool(large), bool(sweep), target); current = target
        elif c in "QC":
            n = 2 if c == "Q" else 3
            pts = [(base[0] + float(tokens[i + 2 * k]), base[1] + float(tokens[i + 2 * k + 1])) for k in range(n)]; i += 2 * n
            p = [current] + pts
            for s in range(1, 33):
                t = s / 32
                q = p[:]
                while len(q) > 1:
                    q = [((1 - t) * a[0] + t * b[0], (1 - t) * a[1] + t * b[1]) for a, b in zip(q, q[1:])]
                figure.append(q[0])
            current = pts[-1]
        else:
            raise ValueError(f"unsupported path command {command}")
    if figure: out.append(figure)
    return out


def draw(size: int, shape: list[list[tuple[float, float]]], bounds) -> Image.Image:
    k = max(8, 2048 // size)  # Supersampling: every size from its own sharp drawing.
    big = size * k
    tile = Image.new("L", (big, big), 0)
    ImageDraw.Draw(tile).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(big * 0.225), fill=255)
    rim = Image.new("L", (big, big), 0)
    inset = max(k, round(big / 128))
    ImageDraw.Draw(rim).rounded_rectangle((inset, inset, big - 1 - inset, big - 1 - inset), radius=round(big * 0.225) - inset, fill=255)
    # Small icons get a larger mark: fewer pixels need less margin to read.
    share = 0.70 if size <= 24 else 0.64 if size <= 40 else 0.58
    left, top, right, bottom = bounds
    span = max(right - left, bottom - top)
    scale = big * share / span
    ox, oy = (big - (right - left) * scale) / 2 - left * scale, (big - (bottom - top) * scale) / 2 - top * scale
    mask = np.zeros((big, big), bool)
    for figure in shape:
        layer = Image.new("1", (big, big), 0)
        ImageDraw.Draw(layer).polygon([(x * scale + ox, y * scale + oy) for x, y in figure], fill=1)
        mask ^= np.asarray(layer, bool)
    glyph = Image.fromarray(mask.astype(np.uint8) * 255, "L")
    shrink = lambda image: np.asarray(image.resize((size, size), Image.BOX), np.float32) / 255
    a_tile, a_inner, a_glyph = shrink(tile), shrink(rim), shrink(glyph)
    a_rim = np.clip(a_tile - a_inner, 0, 1) * (0.10 if size >= 32 else 0.0)  # A faint light edge on dark taskbars.
    rgb = np.empty((size, size, 3), np.float32)
    for c in range(3):
        base = TILE[c] / 255 * (1 - a_rim) + a_rim
        rgb[..., c] = base * (1 - a_glyph) + INK[c] / 255 * a_glyph
    alpha = np.maximum(a_tile, a_glyph)
    return Image.fromarray(np.dstack([rgb * 255, alpha * 255]).clip(0, 255).round().astype(np.uint8), "RGBA")


def scalable(d: str, bounds) -> str:
    """The large sizes' tile as SVG: share 0.58, the faint light rim, even-odd fill."""
    left, top, right, bottom = bounds
    scale = 256 * 0.58 / max(right - left, bottom - top)
    ox, oy = (256 - (right - left) * scale) / 2 - left * scale, (256 - (bottom - top) * scale) / 2 - top * scale
    tile, ink = "#%02x%02x%02x" % TILE, "#%02x%02x%02x" % INK
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <title>Launchme</title>
  <rect width="256" height="256" rx="57.6" fill="{tile}"/>
  <rect x="1" y="1" width="254" height="254" rx="56.6" fill="none" stroke="#ffffff" stroke-opacity="0.1" stroke-width="2"/>
  <path transform="translate({ox:.4f} {oy:.4f}) scale({scale:.6f})" fill="{ink}" fill-rule="evenodd" d="{d}"/>
</svg>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preview", help="also write every size side by side to this PNG")
    parser.add_argument("--hicolor", help="write the Linux hicolor icons under this folder instead of the ICO")
    args = parser.parse_args()
    d, _ = path_data(SYMBOL)
    shape = figures(d)
    xs = [x for f in shape for x, _ in f]; ys = [y for f in shape for _, y in f]
    bounds = (min(xs), min(ys), max(xs), max(ys))
    if args.hicolor:
        theme = pathlib.Path(args.hicolor) / "hicolor"
        for size in HICOLOR:
            folder = theme / f"{size}x{size}" / "apps"
            folder.mkdir(parents=True, exist_ok=True)
            draw(size, shape, bounds).save(folder / f"{APP_ID}.png", optimize=True)
        (theme / "scalable" / "apps").mkdir(parents=True, exist_ok=True)
        (theme / "scalable" / "apps" / f"{APP_ID}.svg").write_text(scalable(d, bounds), encoding="utf-8")
        print(theme, ", ".join(str(s) for s in HICOLOR), "+ scalable")
        return
    images = [draw(size, shape, bounds) for size in SIZES]
    images[-1].save(ICON, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(ICON, ", ".join(str(s) for s in SIZES))
    if args.preview:
        sheet = Image.new("RGBA", (sum(SIZES) + 12 * len(SIZES), 256 + 24), (40, 40, 40, 255))
        x = 12
        for image in images:
            sheet.alpha_composite(image, (x, 12)); x += image.width + 12
        sheet.save(args.preview)


if __name__ == "__main__":
    main()

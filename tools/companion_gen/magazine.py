"""Finish feed pictures: a magazine masthead and cover lines over a cover shot, or a true black-and-white print.

    python tools/companion_gen/magazine.py cover <picture> <out> --title "COSPLAY NOW" --line "YUKI" --line "..." [--dark]
    python tools/companion_gen/magazine.py bw <picture> <out>

The model draws text badly, so the cover's words are set here (Windows fonts: Impact, Georgia, Segoe UI)."""
import argparse, pathlib

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps

FONTS = pathlib.Path(r"C:\Windows\Fonts")


def font(name, size):
    return ImageFont.truetype(str(FONTS / name), size)


def fitted(draw, text, name, width, start):
    size = start
    while size > 10 and draw.textlength(text, font=font(name, size)) > width:
        size -= 2
    return font(name, size)


def cover(source, out, title, lines, issue, dark=False):
    image = Image.open(source).convert("RGB")
    w, h = image.size
    draw = ImageDraw.Draw(image, "RGBA")
    ink = (255, 255, 255) if dark else (12, 12, 14)
    accent = (255, 64, 129) if dark else (200, 20, 40)
    shade = (0, 0, 0, 0) if not dark else (0, 0, 0, 0)
    margin = int(w * 0.05)
    # The masthead across the top, as wide as the page allows.
    masthead = fitted(draw, title, "impact.ttf", w - 2 * margin, int(h * 0.16))
    box = draw.textbbox((0, 0), title, font=masthead)
    draw.text(((w - (box[2] - box[0])) / 2 - box[0], int(h * 0.025) - box[1]), title, font=masthead, fill=ink)
    top = int(h * 0.025) + (box[3] - box[1]) + int(h * 0.01)
    small = font("segoeuib.ttf", int(h * 0.018))
    luminance = image.convert("L")

    def legible(xy, text, face, colour=None):
        """Light text on a dark spot, dark text on a light one, always outlined: readable over her clothes too."""
        box = draw.textbbox(xy, text, font=face)
        patch = luminance.crop(tuple(int(v) for v in box)) if box[2] > box[0] and box[3] > box[1] else None
        dark_spot = patch is not None and sum(patch.getdata()) / max(1, patch.width * patch.height) < 128
        fill = colour or ((255, 255, 255) if dark_spot else (12, 12, 14))
        outline = (0, 0, 0) if dark_spot or colour else (255, 255, 255)
        draw.text(xy, text, font=face, fill=fill, stroke_width=max(2, face.size // 18), stroke_fill=outline)

    legible((margin, top), issue, small, accent)
    # Cover lines down the left: the first one big (her name), the rest smaller.
    y = int(h * 0.62)
    for i, line in enumerate(lines):
        face = font("impact.ttf", int(h * 0.075)) if i == 0 else font("georgiab.ttf", int(h * 0.026))
        legible((margin, y), line, face, accent if i == 0 else None)
        y += int(face.size * 1.25)
    # A barcode in the corner, as on a real issue.
    bx, by, bw_, bh = w - margin - int(w * 0.16), h - margin - int(h * 0.06), int(w * 0.16), int(h * 0.06)
    draw.rectangle((bx - 6, by - 6, bx + bw_ + 6, by + bh + 6), fill=(255, 255, 255))
    x = bx
    for i in range(48):
        thick = 1 + (i * 7 % 3)
        draw.rectangle((x, by, x + thick - 1, by + bh), fill=(0, 0, 0))
        x += thick + 1 + (i % 2)
        if x > bx + bw_:
            break
    image.save(out)


def black_and_white(source, out):
    image = ImageOps.grayscale(Image.open(source).convert("RGB"))
    image = ImageEnhance.Contrast(ImageOps.autocontrast(image, cutoff=1)).enhance(1.25)
    image.convert("RGB").save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["cover", "bw"])
    parser.add_argument("source")
    parser.add_argument("out")
    parser.add_argument("--title", default="COSPLAY NOW")
    parser.add_argument("--issue", default="No. 112  ·  LONDON EDITION")
    parser.add_argument("--line", action="append", default=[])
    parser.add_argument("--dark", action="store_true")
    args = parser.parse_args()
    if args.mode == "cover":
        cover(args.source, args.out, args.title, args.line, args.issue, args.dark)
    else:
        black_and_white(args.source, args.out)


if __name__ == "__main__":
    main()

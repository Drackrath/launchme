"""Contact sheet: sheet.py <out.png> <folder-or-files...>  (12 emotions in order when given a folder)."""
import pathlib, sys
from PIL import Image

EMOTIONS = ["smile", "angry", "sad", "excited", "shock", "thinking", "smug", "shy", "fear", "sleepy", "playful", "cool", "love", "laugh", "cry", "confused", "determined", "wave", "drink", "gaming"]
out = pathlib.Path(sys.argv[1])
files = []
for arg in sys.argv[2:]:
    path = pathlib.Path(arg)
    files += [path / f"{e}.png" for e in EMOTIONS] if path.is_dir() else [path]
W, H = 320, 250
cols = 5
sheet = Image.new("RGBA", (W * cols, H * ((len(files) + cols - 1) // cols)), (40, 44, 56, 255))
for i, f in enumerate(files):
    if not f.exists():
        continue
    image = Image.open(f).convert("RGBA")
    image.thumbnail((W, H))
    sheet.alpha_composite(image, ((i % cols) * W + (W - image.width) // 2, (i // cols) * H + (H - image.height)))
sheet.convert("RGB").save(out)
print(out, len(files))

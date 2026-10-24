"""Drop the faint glow some generated pictures have around the figure (a wide band of low alpha)."""
import os, shutil, sys
import numpy as np
from PIL import Image

root = os.environ.get("COMPANION_GEN_OUT", os.path.join(os.path.expanduser("~"), "Desktop", "companion_gen"))  # dehalo.py <char>\<costume>\<emotion> ...
for name in sys.argv[1:]:
    f = os.path.join(root, name + ".png")
    orig = f[:-4] + ".halo"
    if not os.path.exists(orig):
        shutil.copy(f, orig)
    im = np.asarray(Image.open(orig).convert("RGBA")).astype(np.float32)
    a = im[..., 3]
    na = np.clip((a - 110.0) / (235.0 - 110.0), 0, 1) * 255
    im[..., 3] = na
    im[na == 0] = 0
    Image.fromarray(im.astype(np.uint8), "RGBA").save(f)
    print(name, int(((na > 8) & (na < 200)).sum()))

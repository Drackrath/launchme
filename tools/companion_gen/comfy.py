"""Companion pictures from a local ComfyUI, the free twin of gen.py.

Engine (--engine auto): Qwen Image 2.1 edit when its diffusion model is installed (qwen_image_2.1_turbo_int8_convrot in
models/diffusion_models, text encoder qwen3vl_8b_int8_convrot, VAE qwen_image_2.1_vae_bf16 — the Comfy-Org template's
graph): each emotion is an edit of her reference, a pose a new head-to-toe canvas of the same character (~15 s each on
an RTX 4070). Otherwise SDXL Illustrious img2img / text to image (below).

    comfy.py emotions <char> <costume> [--tags "<outfit tags>"] [--ref <picture>] [--only smile,angry] [--seed N]
    comfy.py pose <char> <costume> --pose "<pose tags>" [--tags "<outfit tags>"] [--seed N]
    comfy.py selfie <char> "<range>|<file stem>|<outfit and scene>" ...   (photos, Desktop\\companion_photos\\<char>\\<range>\\)
    comfy.py outfit <char> <costume> "<clothes and styling>" [--ref <her picture>]   (a new costume's base picture: <costume>/outfit.png)
    comfy.py export <folder>             (the workflows to open in ComfyUI: emotion/pose with a switch, selfie, SDXL)

Her identity: the `comfy=` tags of res/companion/<char>/character.txt, then the costume's (--tags, else a `comfy=`
line in its costume.txt). With --ref (a transparent PNG or any picture of her) every emotion is an img2img of it;
without one, smile.png is drawn first (text to image) and becomes the reference of the other 19, so she stays the
same person. Face-only feelings change less than ones with a gesture (--face / --gesture denoise).

Every picture is drawn on a flat green backdrop and keyed to transparency with companion_sprites.key, so no
background-removal model is needed. Results go to %COMPANION_COMFY_OUT% (default Desktop\\companion_comfy)
\\<char>\\<costume>\\<emotion>.png; existing files are kept. Then, as with gen.py:
    python tools/companion_sprites.py --images <that folder> --look <char> --costume <costume> --out res/companion --match-heads
ComfyUI: %COMFY_URL% (default http://127.0.0.1:8188). Nothing to enable there: the HTTP API is always on."""
import argparse, importlib.util, io, json, os, pathlib, random, sys, time, urllib.parse, urllib.request, uuid

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = pathlib.Path(os.environ.get("COMPANION_COMFY_OUT", pathlib.Path.home() / "Desktop" / "companion_comfy"))
URL = os.environ.get("COMFY_URL", "http://127.0.0.1:8188").rstrip("/")
OPEN = urllib.request.build_opener(urllib.request.ProxyHandler({})).open  # Loopback: never through a proxy.
GREEN = (0, 255, 0)
BUST, FIGURE = (1216, 832), (832, 1216)  # SDXL sizes: a landscape bust (like the 1536x1024 sets), a portrait full figure.

def _load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sprites = _load("companion_sprites", ROOT / "tools" / "companion_sprites.py")
personal = _load("companion_personal", pathlib.Path(__file__).with_name("personal.py"))  # Each one's own gestures and props.

QUALITY = "masterpiece, best quality, amazing quality, very aesthetic, absurdres, anime coloring, cel shading, clean lineart"
NEGATIVE = ("lowres, worst quality, bad quality, bad anatomy, bad hands, extra fingers, missing fingers, extra arms, deformed, blurry, jpeg artifacts, "
            "text, watermark, signature, logo, username, multiple views, cropped head, out of frame, nsfw, cleavage, revealing clothes, "
            "white background, grey background, gradient background, detailed background, scenery, shadow on background, "
            "green hair, green eyes, green clothes, green skin")
BACKDROP = "(simple background:1.3), (green background:1.5), (flat chroma key green background:1.3)"
FRAMING = "solo, upper body, portrait, bust, looking at viewer, centered, head fully in frame"
# Each feeling: Danbooru tags, and whether it is the face alone (less change from the reference) or a gesture.
EMOTIONS = {
    "smile": ("smile, open mouth, happy, :d", False),
    "angry": ("angry, pout, puffed cheeks, furrowed brow, crossed arms", True),
    "sad": ("sad, frown, tears, teary eyes, hand on own chest", True),
    "excited": ("excited, sparkling eyes, open mouth, :d, clenched hands, raised fists", True),
    "shock": ("surprised, wide-eyed, open mouth, sweatdrop, hand on own cheek", True),
    "thinking": ("thinking, looking up, finger to chin, closed mouth", True),
    "smug": ("smug, smirk, half-closed eyes, chin up", False),
    "shy": ("blush, embarrassed, looking away, nervous smile, scratching cheek", True),
    "fear": ("scared, pale face, wide-eyed, sweatdrop, trembling, hand near mouth", True),
    "sleepy": ("sleepy, yawning, closed eyes, hand over own mouth, tears", True),
    "playful": ("one eye closed, wink, tongue out, v, peace sign, hand up", True),
    "cool": ("expressionless, calm, light smile, closed mouth", False),
    "love": ("heart-shaped pupils, blush, smile, own hands clasped, hands near face", True),
    "laugh": ("laughing, closed eyes, open mouth, hand over own mouth", True),
    "cry": ("crying, streaming tears, wiping tears, trembling lips", True),
    "confused": ("confused, head tilt, raised eyebrow, pursed lips, scratching head, question mark", True),
    "determined": ("determined, v-shaped eyebrows, grin, clenched hand, raised fist", True),
    "wave": ("waving, open hand, hand up, smile", True),
    "drink": ("drinking, holding can, soda can, red can, half-closed eyes", True),
    "gaming": ("holding game controller, playing video games, focused, slight smile", True),
}


def field(path: pathlib.Path, key: str) -> str:
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith(key + "="):
                return line.split("=", 1)[1].strip()
    return ""


def identity(char: str, costume: str, tags: str | None) -> str:
    base = field(ROOT / "res" / "companion" / char / "character.txt", "comfy")
    outfit = tags if tags is not None else field(ROOT / "res" / "companion" / char / costume / "costume.txt", "comfy")
    if not base:
        sys.exit(f"res/companion/{char}/character.txt has no comfy= line")
    if outfit and costume != "costume_00":  # The basic outfit's clothes in comfy= give way to the costume's.
        base = ", ".join(t for t in base.split(", ") if not any(w in t for w in ("dress", "blouse", "hoodie", "shirt", "jacket", "top", "vest", "blazer",
                                                                                "cardigan", "skirt", "jeans", "shorts", "outfit", "tie", "bow")))
    return ", ".join(t for t in (base, outfit) if t)


def http(path: str, data: bytes | None = None, headers: dict | None = None):
    request = urllib.request.Request(URL + path, data=data, headers=headers or {}, method="POST" if data is not None else "GET")
    body = OPEN(request, timeout=600).read()
    if path.startswith("/view"):
        return body
    text = body.decode("utf-8", "replace")
    # A local proxy may wrap the answer in an HTML page: the first JSON object is ComfyUI's.
    return json.JSONDecoder().raw_decode(text[text.index("{"):])[0]


def upload(picture: Image.Image, name: str) -> str:
    buffer = io.BytesIO()
    picture.save(buffer, "PNG")
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\nContent-Type: image/png\r\n\r\n").encode()
    body += buffer.getvalue() + f"\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"subfolder\"\r\n\r\ncompanion\r\n".encode()
    body += f"--{boundary}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{boundary}--\r\n".encode()
    answer = http("/upload/image", body, {"Content-Type": f"multipart/form-data; boundary={boundary}"})
    return f"{answer.get('subfolder', 'companion')}/{answer['name']}"


def on_green(path: pathlib.Path, size: tuple[int, int], headroom: float = 0.0) -> Image.Image:
    """The reference on the green backdrop, its bottom edge on the bottom, centred, at the target aspect. `headroom`: a share
    of the height left empty above her, so a head, hair or hat cut off at the top edge can be completed there (outpainting)."""
    picture = Image.open(path).convert("RGBA")
    width, height = size
    scale = min(width / picture.width, height * (1 - headroom) / picture.height)
    picture = picture.resize((round(picture.width * scale), round(picture.height * scale)), Image.LANCZOS)
    canvas = Image.new("RGBA", size, GREEN + (255,))
    canvas.alpha_composite(picture, ((width - picture.width) // 2, height - picture.height))
    return canvas.convert("RGB")


def graph(positive: str, negative: str, size: tuple[int, int], seed: int, ckpt: str, steps: int, cfg: float,
          reference: str | None = None, denoise: float = 1.0, prefix: str = "companion/picture") -> dict:
    nodes = {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": ckpt}},
        "2": {"class_type": "CLIPSetLastLayer", "inputs": {"clip": ["1", 1], "stop_at_clip_layer": -2}},
        "3": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": positive}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": negative}},
        "6": {"class_type": "KSampler", "inputs": {"model": ["1", 0], "positive": ["3", 0], "negative": ["4", 0], "latent_image": ["5", 0], "seed": seed,
                                                   "steps": steps, "cfg": cfg, "sampler_name": "euler_ancestral", "scheduler": "normal", "denoise": denoise}},
        "7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["1", 2]}},
        "8": {"class_type": "SaveImage", "inputs": {"images": ["7", 0], "filename_prefix": prefix}},
    }
    if reference:  # img2img: her picture, scaled to the size, encoded as the starting latent.
        nodes["10"] = {"class_type": "LoadImage", "inputs": {"image": reference}}
        nodes["11"] = {"class_type": "ImageScale", "inputs": {"image": ["10", 0], "upscale_method": "lanczos", "width": size[0], "height": size[1], "crop": "center"}}
        nodes["5"] = {"class_type": "VAEEncode", "inputs": {"pixels": ["11", 0], "vae": ["1", 2]}}
    else:
        nodes["5"] = {"class_type": "EmptyLatentImage", "inputs": {"width": size[0], "height": size[1], "batch_size": 1}}
    return nodes


QWEN_UNETS = ("qwen_image_2.1_turbo_int8_convrot.safetensors", "qwen_image_2.1_turbo_bf16.safetensors", "qwen_image_2.1_int8_convrot.safetensors",
              "qwen_image_2.1_bf16.safetensors")
TURBO_SIGMAS = "1.0, 0.978453, 0.95418, 0.926626, 0.89508, 0.845148, 0.704534, 0.414568, 0"  # The official turbo edit template's.


def qwen_unet() -> str | None:
    names = http("/object_info/UNETLoader")["UNETLoader"]["input"]["required"]["unet_name"][0]
    return next((name for name in QWEN_UNETS if name in names), None)


def qwen_graph(prompt: str, reference: str, seed: int, unet: str, size: tuple[int, int] | None = None, prefix: str = "companion/picture",
               steps: int = 25, resolution: int = 1024, negative: str = "", cfg: float = 1.0, extra: list[str] | None = None) -> dict:
    """cfg 1 (the templates') ignores `negative`; above 1 it counts, at about twice the time. `extra`: more reference pictures
    (image_2, image_3…), e.g. other characters for the art style."""
    """Qwen Image 2.1 edit (Comfy-Org template): her picture as image_1, an instruction as the prompt. `size`: a new canvas
    (a full figure from a bust); otherwise the output keeps the reference's size."""
    turbo = "turbo" in unet
    nodes = {
        "1": {"class_type": "UNETLoader", "inputs": {"unet_name": unet, "weight_dtype": "default"}},
        "2": {"class_type": "QwenImage21Cache", "inputs": {"model": ["1", 0], "device": "auto", "dtype": "default"}},
        "3": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen3vl_8b_int8_convrot.safetensors", "type": "qwen_image", "device": "default"}},
        "4": {"class_type": "VAELoader", "inputs": {"vae_name": "qwen_image_2.1_vae_bf16.safetensors"}},
        "5": {"class_type": "LoadImage", "inputs": {"image": reference}},
        "6": {"class_type": "TextEncodeQwenImage21", "inputs": {"clip": ["3", 0], "prompt": prompt, "negative_prompt": negative, "resolution": resolution,
                                                               "images.image_1": ["5", 0], "vae": ["4", 0]}},
        "12": {"class_type": "VAEDecode", "inputs": {"samples": ["11", 0], "vae": ["4", 0]}},
        "13": {"class_type": "SaveImage", "inputs": {"images": ["12", 0], "filename_prefix": prefix}},
    }
    for number, picture in enumerate(extra or [], 2):
        nodes[f"5{number}"] = {"class_type": "LoadImage", "inputs": {"image": picture}}
        nodes["6"]["inputs"][f"images.image_{number}"] = [f"5{number}", 0]
    latent = ["6", 2]
    if size:
        nodes["14"] = {"class_type": "EmptyLatentImage", "inputs": {"width": size[0], "height": size[1], "batch_size": 1}}
        latent = ["14", 0]
    if turbo:
        nodes.update({
            "7": {"class_type": "RandomNoise", "inputs": {"noise_seed": seed}},
            "8": {"class_type": "KSamplerSelect", "inputs": {"sampler_name": "euler"}},
            "9": {"class_type": "ManualSigmas", "inputs": {"sigmas": TURBO_SIGMAS}},
            "10": {"class_type": "CFGGuider", "inputs": {"model": ["2", 0], "positive": ["6", 0], "negative": ["6", 1], "cfg": cfg}},
            "11": {"class_type": "SamplerCustomAdvanced", "inputs": {"noise": ["7", 0], "guider": ["10", 0], "sampler": ["8", 0], "sigmas": ["9", 0],
                                                                     "latent_image": latent}},
        })
    else:
        nodes["11"] = {"class_type": "KSampler", "inputs": {"model": ["2", 0], "positive": ["6", 0], "negative": ["6", 1], "latent_image": latent, "seed": seed,
                                                            "steps": steps, "cfg": cfg, "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0}}
    return nodes


# Qwen edit instructions: what changes, and what must stay.
KEEP = ("Keep everything else exactly the same: the same character, face, eyes, hair, make-up, accessories, outfit, colours, anime art style, "
        "line weight, cel shading, framing and the plain flat green background. If the top of her head, her hair, a hair bun, a ponytail or a hat is "
        "cut off by the top edge of her picture, complete it naturally in the empty green space above, so her whole head and everything on it is "
        "fully inside the image with a little empty space above.")


def outfit_words(char: str, costume: str) -> str:
    """Her outfit in words (you_en of costume.txt, else of character.txt; personal.COSTUMES for new ones)."""
    base = ROOT / "res" / "companion" / char
    if (char, costume) in personal.COSTUMES:
        return personal.COSTUMES[(char, costume)][3]
    text = field(base / costume / "costume.txt", "you_en") if costume != "costume_00" else ""
    return text or field(base / "character.txt", "you_en")
QWEN_EMOTIONS = {
    "smile": "a gentle happy smile, eyes open, no special gesture",
    "angry": "angry and pouting: furrowed brows, puffed cheeks, arms crossed",
    "sad": "sad: teary eyes with a small tear, downturned mouth, one hand held to her chest",
    "excited": "excited: sparkling wide eyes, a big open-mouthed smile, both fists raised happily",
    "shock": "shocked: wide eyes with small pupils, mouth open in surprise, a sweat drop, one hand on her cheek",
    "thinking": "thinking: looking slightly up and to the side, one finger touching her chin, lips pressed in thought",
    "smug": "smug: half-closed confident eyes, a teasing one-sided smirk, chin slightly raised",
    "shy": "shy and flustered: a light blush, eyes glancing to the side, a nervous little smile, one hand scratching her cheek",
    "fear": "scared: a pale face, trembling wide eyes, mouth slightly open, a sweat drop, one hand raised near her mouth",
    "sleepy": "sleepy: eyes closed, yawning with one hand covering her mouth, a small tear of tiredness",
    "playful": "playful: winking with one eye, tongue slightly out, a peace V-sign with one hand next to her face",
    "cool": "calm and composed: a relaxed neutral face, a closed-mouth faint smile, eyes open, no gesture",
    "love": "in love: heart-shaped highlights in her eyes, a warm blush, a dreamy smile, both hands clasped near her cheek",
    "laugh": "laughing out loud: eyes squeezed shut with joy, a wide open laughing mouth, one hand near her mouth",
    "cry": "crying: tears streaming down her cheeks, trembling lips, wiping a tear with the back of one hand",
    "confused": "confused: head slightly tilted, one eyebrow raised, lips pursed, one finger scratching the side of her head",
    "determined": "determined: fierce confident eyes, a small grin, one fist raised in front of her chest",
    "wave": "waving hello: a bright friendly smile, one open hand raised and waving next to her face",
    "drink": "taking a break: sipping from a plain red soda can (no logo, no text) held in one hand, relaxed half-closed eyes",
    "gaming": "gaming: holding a black game controller in both hands in front of her chest, focused, a small excited smile",
}


def run(nodes: dict) -> Image.Image:
    client = uuid.uuid4().hex
    queued = http("/prompt", json.dumps({"prompt": nodes, "client_id": client}).encode(), {"Content-Type": "application/json"})
    if "prompt_id" not in queued:
        raise RuntimeError(json.dumps(queued)[:600])
    pid = queued["prompt_id"]
    while True:
        time.sleep(1.0)
        history = http(f"/history/{pid}")
        if pid in history:
            entry = history[pid]
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                raise RuntimeError(json.dumps(status.get("messages", []))[:600])
            for output in entry.get("outputs", {}).values():
                for image in output.get("images", []):
                    query = urllib.parse.urlencode({"filename": image["filename"], "subfolder": image.get("subfolder", ""), "type": image.get("type", "output")})
                    return Image.open(io.BytesIO(http(f"/view?{query}"))).convert("RGB")
            if status.get("completed"):
                raise RuntimeError("finished without an image")


def keyed(picture: Image.Image) -> Image.Image:
    rgb = np.asarray(picture.convert("RGB")).astype(np.float32)
    green = (rgb[..., 1] - np.maximum(rgb[..., 0], rgb[..., 2])) > 200
    if green.mean() > 0.05:  # The green backdrop: the sprite tool's soft key with despill.
        rgba = sprites.key(picture)
        rgba[..., 3] *= 255.0
        return Image.fromarray(np.nan_to_num(np.clip(rgba, 0, 255)).astype(np.uint8), "RGBA")
    # It drew another flat backdrop: key the colour of the border by distance (harder edges).
    border = np.concatenate([rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]])
    backdrop = np.median(border, axis=0)
    distance = np.sqrt(((rgb - backdrop) ** 2).sum(axis=2))
    alpha = np.clip((distance - 18.0) / 40.0, 0.0, 1.0) * 255.0
    print("  note: no green backdrop; keyed the border colour", tuple(int(c) for c in backdrop), flush=True)
    return Image.fromarray(np.dstack([rgb, alpha]).astype(np.uint8), "RGBA")



def checkpoint(wanted: str | None) -> str:
    info = http("/object_info/CheckpointLoaderSimple")
    names = info["CheckpointLoaderSimple"]["input"]["required"]["ckpt_name"][0]
    if wanted:
        if wanted not in names:
            sys.exit(f"no checkpoint {wanted}; installed: {names}")
        return wanted
    for preferred in ("waiIllustriousSDXL", "zukiAnimeILL", "Illustrious", "noob", "pony"):
        for name in names:
            if preferred.lower() in name.lower():
                return name
    if not names:
        sys.exit("ComfyUI has no checkpoints")
    return names[0]


def engine(args) -> str | None:
    """The Qwen Image 2.1 edit model when installed (and not --engine sdxl): edits keep her identity far better."""
    if args.engine == "sdxl":
        return None
    unet = qwen_unet()
    if args.engine == "qwen" and not unet:
        sys.exit(f"no Qwen Image 2.1 model in ComfyUI's diffusion_models (one of {QWEN_UNETS})")
    return unet


def emotions(args):
    ckpt = checkpoint(args.ckpt)
    who = identity(args.char, args.costume, args.tags)
    folder = OUT / args.char / args.costume
    folder.mkdir(parents=True, exist_ok=True)
    seed = args.seed if args.seed is not None else random.randrange(1 << 40)
    wanted = [e.strip() for e in args.only.split(",")] if args.only else list(EMOTIONS)
    reference = pathlib.Path(args.ref) if args.ref else None
    if reference is None:  # Her first picture: text to image; then it anchors the rest.
        smile = folder / "smile.png"
        if not smile.exists():
            prompt = f"{QUALITY}, {who}, {EMOTIONS['smile'][0]}, {FRAMING}, {BACKDROP}"
            keyed(run(graph(prompt, NEGATIVE, BUST, seed, ckpt, args.steps, args.cfg,
                            prefix=f"companion/{args.char}_{args.costume}_smile"))).save(smile)
            print(f"{args.char}/{args.costume}/smile: ok (text to image, seed {seed})", flush=True)
        reference = smile
    unet = engine(args)
    # Qwen keeps the reference's own size (at about `resolution`²): a large, sharp bust; SDXL works at its native size.
    uploaded = upload(on_green(reference, (1536, 1024) if unet else BUST, args.headroom if unet else 0.0), f"{args.char}_{args.costume}_reference.png")
    own = personal.EMOTIONS.get(args.char, {})
    for emotion in wanted:
        target = folder / f"{emotion}.png"
        if target.exists():
            print(f"{args.char}/{args.costume}/{emotion}: kept", flush=True)
            continue
        tags, gesture = EMOTIONS[emotion]
        prompt = f"{QUALITY}, {who}, {tags}, {FRAMING}, {BACKDROP}"
        number = seed + 1 + list(EMOTIONS).index(emotion)
        prefix = f"companion/{args.char}_{args.costume}_{emotion}"
        try:
            if unet:  # Qwen edits her picture: only the expression and gesture change.
                feeling = own.get(emotion, QWEN_EMOTIONS[emotion])
                picture = run(qwen_graph(f"Change only her facial expression, her arms and hands and any small held prop, to show: {feeling}. {KEEP}",
                                         uploaded, number, unet, prefix=prefix, resolution=args.resolution))
            else:
                picture = run(graph(prompt, NEGATIVE, BUST, number, ckpt, args.steps, args.cfg, uploaded, args.gesture if gesture else args.face, prefix))
            keyed(picture).save(target)
            print(f"{args.char}/{args.costume}/{emotion}: ok", flush=True)
        except Exception as error:  # noqa: BLE001 - one failure does not stop the set
            print(f"{args.char}/{args.costume}/{emotion}: {error}", flush=True)


def pose(args):
    ckpt = checkpoint(args.ckpt)
    who = identity(args.char, args.costume, args.tags)
    folder = OUT / args.char / args.costume
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / "pose.png"
    if target.exists():
        print(f"{args.char}/{args.costume}/pose: kept")
        return
    seed = args.seed if args.seed is not None else random.randrange(1 << 40)
    unet = engine(args)
    reference = pathlib.Path(args.ref) if args.ref else ROOT / "res" / "companion" / args.char / args.costume / "smile.png"
    if not args.pose:
        args.pose = personal.POSES.get((args.char, args.costume)) or sys.exit(f"no --pose and none in personal.POSES for {args.char}/{args.costume}")
    if unet and reference.exists():  # Qwen: the same character from her bust, now head to toe on a portrait canvas.
        uploaded = upload(on_green(reference, (1536, 1024), args.headroom), f"{args.char}_{args.costume}_pose_reference.png")
        instruction = (f"Draw this same character as a FULL FIGURE, the whole body from head to toe including her feet, in a new, dynamic pose: {args.pose}. "
                       "Keep her face, eyes, hair, make-up, accessories, colours and the anime art style exactly the same. She wears EXACTLY the same "
                       f"clothes as in the picture — the same garments, cut, colours and details on her upper body: {outfit_words(args.char, args.costume)} "
                       "Only add what the bust does not show (legs, footwear) in the same style. The whole figure, her hair and anything on her head "
                       "fully inside the image with a little margin, nothing cut off. Plain flat green background, no floor, no shadow, no scenery, no text.")
        picture = run(qwen_graph(instruction, uploaded, seed, unet, size=(1024, 1536), prefix=f"companion/{args.char}_{args.costume}_pose",
                                 resolution=args.resolution))
    else:
        prompt = f"{QUALITY}, {who}, solo, full body, {args.pose}, {BACKDROP}"
        picture = run(graph(prompt, NEGATIVE.replace("cropped head, ", "cropped head, cropped legs, cropped feet, "), FIGURE, seed, ckpt, args.steps, args.cfg,
                            prefix=f"companion/{args.char}_{args.costume}_pose"))
    keyed(picture).save(target)
    print(f"{args.char}/{args.costume}/pose: ok (seed {seed})")


def new(args):
    """A NEW character from words, in the set's art style (the first images of two other companions as style references):
    her basic outfit's base picture -> Desktop\\companion_gen\\<char>\\costume_00\\smile.png (the identity everything else is drawn from)."""
    unet = engine(args)
    if not unet:
        sys.exit("a new character needs the Qwen Image 2.1 model")
    target = pathlib.Path.home() / "Desktop" / "companion_gen" / args.char / "costume_00" / "smile.png"
    if target.exists():
        print(f"{args.char}/costume_00/smile: kept")
        return
    styles = []
    for key in ("yuki", "saki"):
        source = pathlib.Path.home() / "Desktop" / "companion_gen" / key / "costume_00" / "smile.png"
        if source.exists():
            styles.append(upload(on_green(source, (1536, 1024), 0.14), f"style_{key}.png"))
    instruction = ("Create a NEW anime character in exactly the same art style, line weight, cel shading and colouring as the reference images of other "
                   "characters from the same set — do not copy their faces, hair, eyes or clothes. "
                   f"Character: {args.character} Outfit: {args.outfit} A gentle, lazy smile, no special gesture. "
                   "Framing: a bust portrait from the chest up, facing the viewer, centred, the whole head, hair and anything on the head fully inside the "
                   "image with a little empty space above, the bust cut flat at the bottom edge. A plain flat green background, no text, no watermark, "
                   "tasteful and non-sexualised.")
    seed = args.seed if args.seed is not None else random.randrange(1 << 40)
    picture = run(qwen_graph(instruction, styles[0], seed, unet, size=(1536, 1024), prefix=f"companion/{args.char}_new", resolution=args.resolution,
                             extra=styles[1:]))
    target.parent.mkdir(parents=True, exist_ok=True)
    keyed(picture).save(target)
    print(f"{args.char}/costume_00/smile: ok (seed {seed})")


def outfit(args):
    """A new costume of a known character (Qwen): her costume_00 picture with other clothes, a calm smile, on green -> <costume>/outfit.png,
    the reference of that costume's emotions."""
    unet = engine(args)
    if not unet:
        sys.exit("outfits need the Qwen Image 2.1 model")
    target = OUT / args.char / args.costume / "outfit.png"
    if target.exists():
        print(f"{args.char}/{args.costume}/outfit: kept")
        return
    reference = pathlib.Path(args.ref) if args.ref else ROOT / "res" / "companion" / args.char / "costume_00" / "smile.png"
    uploaded = upload(on_green(reference, (1536, 1024)), f"{args.char}_{args.costume}_outfit_reference.png")
    instruction = (f"Change only her clothes and styling to: {args.look} Keep her face, eyes, age, hair colour and length, skin tone, the anime art style, "
                   "line weight, cel shading, the bust framing and the plain flat green background exactly the same. A gentle happy smile, no special gesture.")
    seed = args.seed if args.seed is not None else random.randrange(1 << 40)
    picture = run(qwen_graph(instruction, uploaded, seed, unet, prefix=f"companion/{args.char}_{args.costume}_outfit", resolution=args.resolution))
    target.parent.mkdir(parents=True, exist_ok=True)
    keyed(picture).save(target)
    print(f"{args.char}/{args.costume}/outfit: ok")


PHOTOS = pathlib.Path(os.environ.get("COMPANION_PHOTOS_OUT", pathlib.Path.home() / "Desktop" / "companion_photos"))
# How each photo was taken (a scene may start with "[tripod]" etc.; default arm).
SHOTS = {
    "arm": ("A casual selfie she just took herself with her phone's front camera: the hand holding the phone is outside the frame (only the start of "
            "that arm may show at the very edge), so at most ONE of her hands is visible."),
    "tripod": ("A photo she took of herself with her phone on a small tripod and the timer, from a few steps away: we see more of her (half or "
               "three-quarter figure), she is posing for it with both hands free and visible; nobody holds a camera and no arm reaches towards the viewer."),
    "mirror": ("A mirror selfie: the picture shows ONLY her reflection in a large mirror that fills the frame — her real body is not in the picture, there "
               "is only one of her. In the reflection she holds her phone in one hand in front of her chest, so we see the back of the phone, the other "
               "hand free."),
    "above": ("A selfie taken from straight above while she lies down, the phone held overhead outside the frame, so at most ONE of her hands is visible."),
    "candid": ("A candid photo a friend took of her from a few metres away: she holds no phone, she is caught naturally in the moment, both hands busy "
               "with what she is doing."),
    "legs": ("A photo of only her legs and feet, seen from her own point of view; her face and upper body are not in the picture."),
}
SELFIE_NEGATIVE = ("extra hands, three hands, extra arms, three arms, extra fingers, missing fingers, deformed hands, fused hands, two phones, "
                   "phone screen, text, watermark, logo, nsfw, cleavage")


SHOT_WORDS = {"arm": "A selfie she took herself", "tripod": "A photo she took of herself with her phone on a tripod", "mirror": "A mirror selfie",
              "above": "A selfie from above, lying down", "candid": "A photo someone took of her", "legs": "A photo of her legs"}


def photo_description(scene: str) -> str:
    """What the photo shows, for her chat (the app's <stem>.txt): "A mirror selfie. In a boutique fitting room …"."""
    kind = "arm"
    if scene.startswith("[") and "]" in scene:
        kind, scene = scene[1:scene.index("]")], scene[scene.index("]") + 1:].strip()
    return f"{SHOT_WORDS.get(kind, SHOT_WORDS['arm'])}. {scene}"


def selfie_instruction(scene: str, rules: str = "Tasteful, wholesome and non-sexualised.") -> str:
    """A photo's prompt: "[<shot>] <scene>" (SHOTS; default arm)."""
    kind = "arm"
    if scene.startswith("[") and "]" in scene:
        kind, scene = scene[1:scene.index("]")], scene[scene.index("]") + 1:].strip()
    return ("Make a new picture of this same character: keep her face, eyes, hair, ears, skin tone, make-up and the anime art style, line weight "
            "and cel shading exactly the same, but she may wear other clothes. "
            f"{SHOTS.get(kind, SHOTS['arm'])} She has exactly two arms and two hands, never a third. "
            f"Scene and outfit: {scene} The character is drawn in anime style, the background is a realistic, photographic, detailed real-world "
            "place (not green, not plain) with natural light and depth of field. A wide 16:10 landscape composition, no text, no watermark, no "
            f"phone screen interface. {rules}")


def selfie(args):
    """Her photos for the chat (opaque, 16:10, with a background): "<range>|<file stem>|<outfit and scene>" shots, as gen.py photos."""
    unet = engine(args)
    if not unet:
        sys.exit("selfies need the Qwen Image 2.1 model")
    reference = pathlib.Path(args.ref) if args.ref else ROOT / "res" / "companion" / args.char / "costume_00" / "smile.png"
    uploaded = upload(on_green(reference, (1536, 1024)), f"{args.char}_selfie_reference.png")
    base = args.seed if args.seed is not None else random.randrange(1 << 40)
    for index, shot in enumerate(args.shots):
        folder, stem, scene = shot.split("|", 2)
        target = PHOTOS / args.char / folder / f"{stem}.png"
        # Its description beside it (<stem>.txt, read by the app when she sends it): the scene without the shot tag.
        target.parent.mkdir(parents=True, exist_ok=True)
        target.with_suffix(".txt").write_text(photo_description(scene) + "\n", encoding="utf-8")
        if target.exists():
            print(f"{args.char}/{folder}/{stem}: kept", flush=True)
            continue
        instruction = selfie_instruction(scene, args.rules)
        try:
            picture = run(qwen_graph(instruction, uploaded, base + index, unet, size=(1536, 960), prefix=f"companion/{args.char}_selfie_{stem}",
                                     resolution=args.resolution, negative=(args.negative or SELFIE_NEGATIVE) if args.cfg > 1 else "", cfg=args.cfg))
            target.parent.mkdir(parents=True, exist_ok=True)
            picture.save(target)
            print(f"{args.char}/{folder}/{stem}: ok", flush=True)
        except Exception as error:  # noqa: BLE001
            print(f"{args.char}/{folder}/{stem}: {error}", flush=True)


def titled(nodes: dict, titles: dict) -> dict:
    for key, title in titles.items():
        nodes[key]["_meta"] = {"title": title}
    return nodes


def export(args):
    """Workflows to open in ComfyUI (Workflow -> Open, or drop the file on the canvas; API format, laid out automatically)."""
    folder = pathlib.Path(args.folder)
    folder.mkdir(parents=True, exist_ok=True)
    unet = QWEN_UNETS[0]
    # Emotions and poses: one graph; the "Full figure (pose)" switch draws on a new portrait canvas instead of her picture's.
    emotion = qwen_graph(f"Change only her facial expression, her arms and hands and any small held prop, to show: {QWEN_EMOTIONS['angry']}. {KEEP}",
                         "companion/reference.png", 1234, unet, prefix="companion/emotion", resolution=1280)
    emotion["14"] = {"class_type": "EmptyLatentImage", "inputs": {"width": 1024, "height": 1536, "batch_size": 1}}
    emotion["20"] = {"class_type": "PrimitiveBoolean", "inputs": {"value": False}}
    emotion["21"] = {"class_type": "ComfySwitchNode", "inputs": {"switch": ["20", 0], "on_true": ["14", 0], "on_false": ["6", 2]}}
    emotion["11"]["inputs"]["latent_image"] = ["21", 0]
    titled(emotion, {"1": "Qwen Image 2.1 (turbo)", "2": "KV cache", "3": "Text encoder (Qwen3-VL 8B)", "4": "VAE",
                     "5": "Her reference (a bust on a flat green background, some empty space above her head)",
                     "6": "Instruction: what changes (emotion) / the full-figure pose; keep the rest", "7": "Seed",
                     "9": "Turbo sigmas (8 steps)", "10": "CFG 1 (negative ignored)", "14": "Pose canvas 1024x1536",
                     "20": "Full figure (pose)? on = new portrait canvas, off = edit her picture", "21": "Canvas switch",
                     "13": "Save (green background: keyed by comfy.py)"})
    selfie_graph = qwen_graph(selfie_instruction("[tripod] On a rooftop at night above a neon city skyline, wearing an oversized bomber jacket, "
                                                 "sitting on the ledge with both hands on her knees, a lazy smirk."),
                              "companion/reference.png", 2026, unet, size=(1536, 960), prefix="companion/selfie", resolution=1280,
                              negative=SELFIE_NEGATIVE, cfg=1.0)
    titled(selfie_graph, {"1": "Qwen Image 2.1 (turbo)", "5": "Her reference picture", "6": "Selfie: shot type, scene, outfit (see comfy.SHOTS)",
                          "10": "CFG (above 1: the negative prompt counts, ~2x time)", "14": "Photo canvas 1536x960 (16:10)", "13": "Save"})
    sdxl = graph(f"{QUALITY}, 1girl, <her tags>, smile, open mouth, {FRAMING}, {BACKDROP}", NEGATIVE, BUST, 1, "waiIllustriousSDXL_v170.safetensors", 28, 5.5,
                 "companion/reference.png", 0.6)
    for name, nodes in (("companion_emotion_pose_qwen.json", emotion), ("companion_selfie_qwen.json", selfie_graph), ("companion_emotion_sdxl.json", sdxl)):
        (folder / name).write_text(json.dumps(nodes, indent=2, ensure_ascii=False), encoding="utf-8")
        print(folder / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    for name in ("emotions", "pose"):
        p = sub.add_parser(name)
        p.add_argument("char")
        p.add_argument("costume")
        p.add_argument("--tags", help="outfit tags (default: comfy= of the costume's costume.txt)")
        p.add_argument("--seed", type=int)
        p.add_argument("--ckpt", help="checkpoint (default: an installed Illustrious one)")
        p.add_argument("--steps", type=int, default=28)
        p.add_argument("--cfg", type=float, default=5.5)
        p.add_argument("--engine", choices=("auto", "qwen", "sdxl"), default="auto",
                       help="qwen: Qwen Image 2.1 edit (when installed; the default), sdxl: Illustrious img2img / text to image")
        p.add_argument("--ref", help="her picture to start from (emotions: default a generated smile.png; pose: her costume's smile.png)")
        if name == "emotions":
            p.add_argument("--only", help="comma-separated emotions")
            p.add_argument("--face", type=float, default=0.5, help="denoise of face-only feelings")
            p.add_argument("--gesture", type=float, default=0.66, help="denoise of feelings with a gesture")
        else:
            p.add_argument("--pose", help="the pose (default: personal.POSES), e.g. 'sitting, crossed legs, holding a book'")
        p.add_argument("--resolution", type=int, default=1280, help="Qwen: about this many pixels squared per reference (official 1024, up to 2048)")
        p.add_argument("--headroom", type=float, default=0.14, help="Qwen: share of the height left empty above her, to complete a cut-off head or hat")
    s = sub.add_parser("selfie", help='her photos: comfy.py selfie <char> "<range>|<stem>|<outfit and scene>" ...')
    s.add_argument("char")
    s.add_argument("shots", nargs="+")
    s.add_argument("--ref", help="her picture (default: res/companion/<char>/costume_00/smile.png)")
    s.add_argument("--seed", type=int)
    s.add_argument("--engine", choices=("auto", "qwen"), default="auto")
    s.add_argument("--resolution", type=int, default=1280)
    s.add_argument("--cfg", type=float, default=1.0, help="above 1 the negative prompt (extra hands…) counts, about twice the time")
    s.add_argument("--rules", default="Tasteful, wholesome and non-sexualised.", help="the closing style rule of every selfie prompt")
    s.add_argument("--negative", default=None, help="negative prompt (with --cfg above 1); default: extra hands, text…")
    o = sub.add_parser("outfit", help='a new costume: comfy.py outfit <char> <costume> "<clothes and styling>" [--ref <her picture>]')
    o.add_argument("char")
    o.add_argument("costume")
    o.add_argument("look")
    o.add_argument("--ref")
    o.add_argument("--seed", type=int)
    o.add_argument("--engine", choices=("auto", "qwen"), default="auto")
    o.add_argument("--resolution", type=int, default=1280)
    n = sub.add_parser("new", help='a new character: comfy.py new <char> "<who she is and looks like>" "<her basic outfit>"')
    n.add_argument("char")
    n.add_argument("character")
    n.add_argument("outfit")
    n.add_argument("--seed", type=int)
    n.add_argument("--engine", choices=("auto", "qwen"), default="auto")
    n.add_argument("--resolution", type=int, default=1280)
    e = sub.add_parser("export", help="write the ComfyUI workflows (API format) into a folder")
    e.add_argument("folder")
    args = parser.parse_args()
    {"emotions": emotions, "pose": pose, "selfie": selfie, "outfit": outfit, "new": new, "export": export}[args.mode](args)


if __name__ == "__main__":
    main()

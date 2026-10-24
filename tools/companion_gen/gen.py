"""Companion sprite generation with gpt-image-2.5-flare (images/edits).

    gen.py base <char> <costume> "<character description>" "<outfit description>"
    gen.py emotions <char> <costume> [emotion ...]
    gen.py outfit <char> <costume> "<outfit and styling>"   (another costume: her costume_00 smile as identity)

Raw results go to %COMPANION_GEN_OUT% (default Desktop\\companion_gen)\\<char>\\<costume>\\<emotion>.png; existing files are kept.
    gen.py photos <char> <identity.png> "<character description>" "<range>|<file stem>|<scene>" ...   (selfies, Desktop\\companion_photos)
    gen.py poses "<char>|<costume>|<character and outfit>|<pose>" ...   (the chat's entrance: <char>\\<costume>\\pose.png, 1024x1536)
`emotions` uses the set's smile.png as the identity reference and Yuki's picture of that emotion (when there is one)
for the expression only. Stops everything on a billing/quota error (prints QUOTA)."""
import base64, json, os, pathlib, random, re, sys, threading, time, urllib.error, urllib.request, uuid
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]  # The repository (its .openai_token holds the OpenAI key; git-ignored).
OUT = pathlib.Path(os.environ.get("COMPANION_GEN_OUT", pathlib.Path.home() / "Desktop" / "companion_gen"))
KEY = (ROOT / ".openai_token").read_text(encoding="utf-8").strip()
STOP = threading.Event()

EMOTIONS = {
    "smile": "a gentle happy smile, eyes open, no special gesture",
    "angry": "angry and pouting: furrowed brows, puffed cheeks, arms crossed",
    "sad": "sad: teary eyes with a small tear, downturned mouth, one hand held to her chest",
    "excited": "excited: sparkling wide eyes, big open-mouthed smile, both fists raised happily",
    "shock": "shocked: wide eyes with small pupils, mouth open in surprise, a sweat drop, one hand on her cheek",
    "thinking": "thinking: looking slightly up and to the side, one finger touching her chin, lips pressed in thought",
    "smug": "smug: half-closed confident eyes, a teasing one-sided smirk, chin slightly raised",
    "shy": "shy and flustered: light blush on her cheeks, eyes glancing to the side, a nervous little smile, one hand shyly scratching her cheek",
    "fear": "scared: pale face, trembling wide eyes, mouth slightly open, a sweat drop, one hand raised near her mouth",
    "sleepy": "sleepy: eyes closed, yawning with one hand covering her mouth, a small tear of tiredness",
    "playful": "playful: winking with one eye, tongue slightly out, a peace V-sign with one hand next to her face",
    "cool": "calm and composed: relaxed neutral face, closed-mouth faint smile, eyes open, no gesture",
    # Added: more feelings and everyday "filler" moments.
    "love": "in love: heart-shaped highlights in her eyes, a warm blush, a dreamy smile, both hands clasped together near her cheek",
    "laugh": "laughing out loud: eyes squeezed shut with joy, wide open laughing mouth, one hand near her mouth",
    "cry": "crying: tears streaming down her cheeks, trembling lips, wiping a tear with the back of one hand",
    "confused": "confused: head slightly tilted, one eyebrow raised, lips pursed, one finger scratching the side of her head",
    "determined": "determined: fierce confident eyes, a small grin, one fist raised in front of her chest",
    "wave": "waving hello: a bright friendly smile, one open hand raised and waving next to her face",
    "drink": "taking a break: sipping from a plain red soda can (no logo, no text) held in one hand, relaxed half-closed eyes",
    "gaming": "gaming: holding a black game controller in both hands in front of her chest, focused, a small excited smile",
}
FRAMING = ("Framing: bust portrait from the chest up, facing the viewer, centred, the whole head, hair and anything on the head fully "
           "inside the image with a little empty space above, the bust cut flat at the bottom edge. Transparent background, no text, "
           "no watermark, tasteful and non-sexualised.")


def call(prompt: str, refs: list[pathlib.Path], size: str = "1536x1024"):
    boundary = uuid.uuid4().hex
    body = bytearray()
    for name, value in {"model": "gpt-image-2.5-flare", "prompt": prompt, "size": size, "quality": "high",
                        "background": "transparent", "output_format": "png", "n": "1"}.items():
        body.extend(f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n{value}\r\n".encode())
    for ref in refs:
        body.extend(f"--{boundary}\r\nContent-Disposition: form-data; name=\"image[]\"; filename=\"{ref.name}\"\r\nContent-Type: image/png\r\n\r\n".encode())
        body.extend(ref.read_bytes())
        body.extend(b"\r\n")
    body.extend(f"--{boundary}--\r\n".encode())
    request = urllib.request.Request("https://api.openai.com/v1/images/edits", data=bytes(body), method="POST",
                                     headers={"Authorization": f"Bearer {KEY}", "Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(request, timeout=600) as response:
        return base64.b64decode(json.loads(response.read())["data"][0]["b64_json"])


def run(target: pathlib.Path, prompt: str, refs: list[pathlib.Path], generate=None) -> str:
    label = f"{target.parent.parent.name}/{target.parent.name}/{target.stem}"
    if target.exists():
        return f"{label}: kept"
    if STOP.is_set():
        return f"{label}: skipped (quota)"
    attempts = 12
    for attempt in range(attempts):
        try:
            data = (generate or call)(prompt, refs)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            return f"{label}: ok"
        except urllib.error.HTTPError as error:
            message = error.read()[:400].decode(errors="replace")
            if error.code == 429 and ("quota" in message or "billing" in message) or "billing_hard_limit" in message or "insufficient" in message:
                STOP.set()
                return f"{label}: QUOTA {message}"
            if error.code == 429 and attempt < attempts - 1:  # Rate limit (input images per minute): wait as told, then try again.
                wait = re.search(r"try again in ([0-9.]+)s", message)
                time.sleep((float(wait.group(1)) if wait else 10.0) + random.uniform(2, 8))
                continue
            if error.code not in (429, 500, 502, 503, 504) or attempt >= 2:
                return f"{label}: HTTP {error.code} {message.splitlines()[2] if len(message.splitlines()) > 2 else message}"
        except Exception as error:  # noqa: BLE001 - network hiccups: try again
            if attempt >= 2:
                return f"{label}: {error}"
    return f"{label}: failed"


def style_refs():
    return [ROOT / "res/companion/yuki/costume_00/smile.png", ROOT / "res/companion/akane/costume_00/smile.png"]


def base(char: str, costume: str, character: str, outfit: str, extra: list[str]):
    prompt = ("Create a NEW anime character in exactly the same art style, line weight, cel shading and colouring as the reference "
              "images of other characters from the same set (do not copy their faces, hair or clothes). "
              f"Character: {character} Outfit: {outfit} Emotion: {EMOTIONS['smile']}. {FRAMING}")
    refs = style_refs() + [pathlib.Path(p) for p in extra]
    print(run(OUT / char / costume / "smile.png", prompt, refs), flush=True)


def outfit(char: str, costume: str, look: str):
    """Another costume of an existing character: her identity from costume_00's smile, a new outfit."""
    identity = next((OUT / char / "costume_00" / f"{e}.png" for e in ("smile", "cool") if (OUT / char / "costume_00" / f"{e}.png").exists()),
                    ROOT / f"res/companion/{char}/costume_00/smile.png")
    prompt = ("The image shows the character: keep her exactly the same - face, eyes, age, hair colour and length, skin tone, art style, line weight, "
              f"cel shading and framing - but change her outfit and styling: {look} Emotion: {EMOTIONS['smile']}. {FRAMING}")
    print(run(OUT / char / costume / "smile.png", prompt, [identity]), flush=True)


def emotions(char: str, costume: str, wanted: list[str]):
    # Her identity: the set's smile, else its calm face (a set someone else drew may lack a smile).
    smile = next((OUT / char / costume / f"{e}.png" for e in ("smile", "cool") if (OUT / char / costume / f"{e}.png").exists()), OUT / char / costume / "smile.png")
    jobs = []
    for emotion in wanted or [e for e in EMOTIONS if e != "smile"]:
        pose = ROOT / f"res/companion/yuki/costume_00/{emotion}.png"
        refs = [smile] + ([pose] if pose.exists() else [])
        prompt = ("The FIRST image shows the character: keep her exactly the same - face, eyes, hair, make-up or face paint, accessories, "
                  "outfit, skin tone, colours, art style, line weight and framing. "
                  + ("The SECOND image is a different character: copy ONLY her expression and gesture, nothing else. " if pose.exists() else "")
                  + f"New emotion: {EMOTIONS[emotion]}. {FRAMING}")
        jobs.append((OUT / char / costume / f"{emotion}.png", prompt, refs))
    with ThreadPoolExecutor(6) as pool:
        for line in pool.map(lambda job: run(*job), jobs):
            print(line, flush=True)


def poses(shots: list[str]):
    """Full-figure poses for the chat's entrance (portrait, transparent): shots are "<char>|<costume>|<who she is and what she wears>|<pose>".
    Her identity comes from res/companion/<char>/<costume>/smile.png (and costume_00's smile for her face)."""
    jobs = []
    for shot in shots:
        char, costume, character, pose, *example = shot.split("|", 4)  # Optional 5th field: a picture whose pose to follow.
        refs = [ROOT / f"res/companion/{char}/{costume}/smile.png"]
        if costume != "costume_00":
            refs.append(ROOT / f"res/companion/{char}/costume_00/smile.png")
        if example:
            pose += (" The LAST image shows an older version of this pose: follow its pose, composition and props closely, "
                     "but draw her as she looks in the first image.")
            refs.append(pathlib.Path(example[0]))
        prompt = ("The FIRST image shows the character: keep her exactly the same - face, eyes, hair, make-up or face paint, accessories, "
                  "outfit, colours, art style, line weight and cel shading. "
                  + ("The SECOND image is the same character in her everyday outfit: use it only to keep her face and hair consistent; she wears the outfit of the FIRST image. "
                     if costume != "costume_00" else "")
                  + f"Character: {character} "
                  "Draw her FULL FIGURE (the whole body, head to toe, including shoes or feet), a dynamic, expressive pose that shows her personality: "
                  f"{pose} The whole figure, her hair, hat and any props fully inside the image with a little empty margin on every side, "
                  "nothing cut off. Transparent background, no floor, no scenery, no frame, no text, no watermark. "
                  "Tasteful, wholesome and non-sexualised; skirts and dresses stay modest whatever the pose.")
        jobs.append((OUT / char / costume / "pose.png", prompt, refs))
    with ThreadPoolExecutor(4) as pool:
        for line in pool.map(lambda job: run(*job, lambda p, r: call(p, r, "1024x1536")), jobs):
            print(line, flush=True)


PHOTOS = pathlib.Path(os.environ.get("COMPANION_PHOTOS_OUT", pathlib.Path.home() / "Desktop" / "companion_photos"))


def photos(char: str, identity: str, character: str, shots: list[str]):
    """Selfies (opaque, landscape): shots are "<range>|<file stem>|<scene>"."""
    jobs = []
    for shot in shots:
        folder, stem, scene = shot.split("|", 2)
        prompt = ("An anime illustration in exactly the same art style, line weight and colouring as the reference image, of the SAME "
                  f"character: {character} Keep her face, eyes, hair and colours exactly as in the reference. "
                  "It is a casual selfie she just took herself: the camera is her phone held at arm's length (her arm reaching towards "
                  "the viewer at the edge of the frame), a natural candid selfie angle, a playful or friendly look at the camera. "
                  f"Scene: {scene} Wide 16:9 landscape composition with a detailed background (not transparent), soft natural light, "
                  "no text, no watermark, no phone UI. Tasteful, wholesome and non-sexualised; fully dressed or covered where needed.")
        jobs.append((PHOTOS / char / folder / f"{stem}.png", prompt, [pathlib.Path(identity)]))
    with ThreadPoolExecutor(5) as pool:
        for line in pool.map(lambda job: run_photo(*job), jobs):
            print(line, flush=True)


def run_photo(target: pathlib.Path, prompt: str, refs: list[pathlib.Path]) -> str:
    def opaque(prompt_text, references):
        boundary = uuid.uuid4().hex
        body = bytearray()
        for name, value in {"model": "gpt-image-2.5-flare", "prompt": prompt_text, "size": "1536x1024", "quality": "high",
                            "background": "opaque", "output_format": "png", "n": "1"}.items():
            body.extend(f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n{value}\r\n".encode())
        for ref in references:
            body.extend(f"--{boundary}\r\nContent-Disposition: form-data; name=\"image[]\"; filename=\"{ref.name}\"\r\nContent-Type: image/png\r\n\r\n".encode())
            body.extend(ref.read_bytes())
            body.extend(b"\r\n")
        body.extend(f"--{boundary}--\r\n".encode())
        request = urllib.request.Request("https://api.openai.com/v1/images/edits", data=bytes(body), method="POST",
                                         headers={"Authorization": f"Bearer {KEY}", "Content-Type": f"multipart/form-data; boundary={boundary}"})
        with urllib.request.urlopen(request, timeout=600) as response:
            return base64.b64decode(json.loads(response.read())["data"][0]["b64_json"])
    return run(target, prompt, refs, opaque)


if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "photos":
        photos(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
    elif mode == "poses":
        poses(sys.argv[2:])
    elif mode == "outfit":
        outfit(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "base":
        base(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6:])
    else:
        emotions(sys.argv[2], sys.argv[3], sys.argv[4:])

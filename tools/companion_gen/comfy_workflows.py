"""ComfyUI workflows (UI format, open them from the Workflows panel) for one consistent look of a companion: the same
models as unholy_anima_zdjecie_v2 (Unholy Desire Mix Anima 2B, Qwen3 0.6B text encoder, Qwen Image VAE, er_sde/beta,
CFG 4.5) and the same character LoRA in every pass, so her photos, her pose and her 20 feelings match.

    python tools/companion_gen/comfy_workflows.py [--out <workflows folder>] [--server http://127.0.0.1:8188] [--smoke pose|real]

    unholy_anima_poza_v2.json     her full figure on flat green: from the prompt alone, or the pose of a reference
                                  picture (its silhouette, img2img); 832x1216 then hires x1.5 -> <folder>/pose_*.png
    unholy_anima_emocje_v2.json   all 20 feelings from that pose (bust crop, img2img, one shared seed)
                                  -> <folder>/<emotion>_*.png and <folder>/arkusz_emocji_*.png (5x4 sheet)
    unholy_anima_zdjecie_v2_real.json  her photo with a realistic background, pressed by the prompt (no compositing);
                                  768x1280 then hires x1.5 -> <folder>/zdjecie_*.png

The running ComfyUI is asked for its node definitions (/object_info): every node, input and model name is checked
before anything is written. --smoke queues one of them once (output/launchme_test/...) and waits.
Then: key the green away and fit the pictures with tools/companion_sprites.py --images, faces with
tools/companion_faces.py (res/companion/README.md)."""
import argparse, copy, json, os, pathlib, sys, time, urllib.request, uuid

DEFAULT_OUT = pathlib.Path(os.environ.get("COMFY_WORKFLOWS", str(pathlib.Path.home() / "ComfyUI" / "user" / "default" / "workflows")))

CHECKPOINT = "unholyDesireMixDarkSerenity_v30Anima2b.safetensors"
TEXT_ENCODER = ("qwen_3_06b_base.safetensors", "stable_diffusion")
VAE = "qwen_image_vae.safetensors"
LORA, LORA_STRENGTH = "Kitagawa_Marin_Anima.safetensors", 0.8
LORA2, LORA2_STRENGTH = "anima-base-1-masterpiece-v51.safetensors", 0.5
SAMPLER = dict(cfg=4.5, sampler_name="er_sde", scheduler="beta")

# The character's block: the same text as in unholy_anima_zdjecie_v2 ("1. Postac"), without the framing (each
# workflow adds its own: full body / upper body), so her photos, pose and feelings share one description.
CHARACTER = ("score_9, score_8, masterpiece, best quality, year 2025, newest, highres, absurdres, 1girl, solo, kitagawa marin, "
             "medium breasts, blonde, curly hair, beautiful, voluminous, wavy honey-blonde hair, side-swept bangs framing her face, "
             "warm golden eyes, defined black eyeliner, long eyelashes. Stylish makeup, small round stud earrings and a delicate gold "
             "necklace with a tiny pendant. Warm, approachable facial features, a confident fashionable appearance. Clean anime linework, flat")
# Her outfit on its own line: changing clothes touches nothing else, and a close-up can drop what is out of frame (shoes,
# tights) - naming them in a selfie is what draws a second, full-body her into the picture.
OUTFIT = "wearing beige skirt, black tights, coat, boots, belt, white shirt"
AVOID = "red eyes, pink hair"
GREEN = "simple background, green background, flat chroma key green background"
NEGATIVE = ("worst quality, low quality, bad quality, low score, bad score, lowres, sketch, blurry, jpeg artifacts, pixelated, monochrome, "
            "greyscale, bad anatomy, bad hands, bad proportions, extra fingers, missing finger, extra limbs, extra hands, extra arms, "
            "deformed, mutated, poorly drawn face, bad eyes, text, watermark, signature, gradient background, detailed background, "
            "scenery, shadow, green hair, green eyes, green clothes, out of frame, multiple views")
NEGATIVE_POSE = NEGATIVE + ", floor, chair, cropped head, cropped legs, cropped feet, from above, from below, dutch angle"
NEGATIVE_BUST = NEGATIVE + ", cropped head"
# unholy_anima_zdjecie_v2's negative (its character-specific end moved to "1b").
NEGATIVE_PHOTO = ("worst quality, low quality, bad quality, low score, bad score, lowres, sketch, jpeg artifacts, pixelated, bad anatomy, "
                  "bad hands, bad proportions, extra fingers, missing finger, extra limbs, deformed, mutated, poorly drawn face, bad eyes, text, "
                  "watermark, signature, simple background, green background, plain background, extra hands, extra arms, two phones, "
                  "phone screen, from above, multiple girls, 2girls, clone, multiple views, anime background, illustrated background, painted "
                  "background, cartoon background, flat background, nsfw, explicit, nude, nipples, pantyshot")
# Pressed hard: Anima draws its own (anime) scenery unless told plainly, in tags and in words, that the place is a photo.
REALISM = ("photo background, photorealistic background, real world location, realistic lighting, depth of field. The background is a "
           "real photograph taken with a phone camera: real textures, real light and real depth, not drawn; only she is drawn in anime style")
FOLDER = "saki/costume_00"
PHOTO_FOLDER = "saki/zdjecia"

# Tags of each feeling (companion::Emotion order); face-only feelings keep more of the pose (lower denoise).
EMOTIONS = [
    ("smile", "smile, open mouth, happy, :d", True),
    ("angry", "angry, pout, puffed cheeks, furrowed brow, crossed arms", False),
    ("sad", "sad, frown, tears, teary eyes, hand on own chest", False),
    ("excited", "excited, sparkling eyes, open mouth, :d, clenched hands, raised fists", False),
    ("shock", "surprised, wide-eyed, open mouth, sweatdrop, hand on own cheek", False),
    ("thinking", "thinking, looking up, finger to chin, closed mouth", False),
    ("smug", "smug, smirk, half-closed eyes, chin up", True),
    ("shy", "blush, embarrassed, looking away, nervous smile, scratching cheek", False),
    ("fear", "scared, pale face, wide-eyed, sweatdrop, trembling, hand near mouth", False),
    ("sleepy", "sleepy, yawning, closed eyes, hand over own mouth, tears", False),
    ("playful", "one eye closed, wink, tongue out, v, peace sign, hand up", False),
    ("cool", "expressionless, calm, light smile, closed mouth", True),
    ("love", "heart-shaped pupils, blush, smile, own hands clasped, hands near face", False),
    ("laugh", "laughing, closed eyes, open mouth, hand over own mouth", False),
    ("cry", "crying, streaming tears, wiping tears, trembling lips", False),
    ("confused", "confused, head tilt, raised eyebrow, pursed lips, scratching head, question mark", False),
    ("determined", "determined, v-shaped eyebrows, grin, clenched hand, raised fist", False),
    ("wave", "waving, open hand, hand up, smile", False),
    ("drink", "drinking, holding can, soda can, red can, half-closed eyes", False),
    ("gaming", "holding game controller, playing video games, focused, slight smile", False),
]

WIDGET_TYPES = {"INT", "FLOAT", "STRING", "BOOLEAN", "COMBO"}


def default(kind, options):
    """A widget's value when the workflow does not set it: ComfyUI's default, else the first choice or an empty value."""
    if "default" in options:
        return options["default"]
    if kind == "COMBO":
        return (options.get("options") or [""])[0]
    return {"STRING": "", "INT": 0, "FLOAT": 0.0, "BOOLEAN": False}.get(kind)


class Node:
    def __init__(self, graph, nid, type_, title, pos, size, mode, widgets, controls, links, out_types, autogrow):
        self.graph, self.id, self.type, self.title = graph, nid, type_, title
        self.pos, self.size, self.mode = list(pos), list(size), mode
        self.widgets, self.controls, self.links, self.out_types, self.autogrow = widgets, controls, links, out_types, autogrow

    def __getitem__(self, slot):  # node[0]: its first output, to connect elsewhere.
        return (self, slot)


class Graph:
    def __init__(self, info):
        self.info, self.nodes, self.groups = info, [], []

    def node(self, type_, title=None, pos=(0, 0), size=(320, 120), mode=0, widgets=None, controls=None, links=None, out_types=None, autogrow=0):
        if type_ != "Note" and type_ not in self.info:
            sys.exit(f"ComfyUI has no node {type_}")
        node = Node(self, len(self.nodes) + 1, type_, title, pos, size, mode, widgets or {}, controls or {}, links or {}, out_types or {}, autogrow)
        self.nodes.append(node)
        return node

    def note(self, title, text, pos, size=(420, 300)):
        return self.node("Note", title, pos, size, widgets={"text": text})

    def group(self, title, members, color="#3f789e", pad=40):
        left = min(n.pos[0] for n in members) - pad / 2; top = min(n.pos[1] for n in members) - pad - 20
        right = max(n.pos[0] + n.size[0] for n in members) + pad / 2; bottom = max(n.pos[1] + n.size[1] for n in members) + pad / 2
        self.groups.append({"id": len(self.groups) + 1, "title": title, "bounding": [left, top, right - left, bottom - top],
                            "color": color, "font_size": 24, "flags": {}})

    # ---- node definitions -------------------------------------------------------------------------------------------
    def spec(self, node):
        definition = self.info[node.type]
        order = definition.get("input_order") or {k: list(v) for k, v in definition["input"].items() if k in ("required", "optional")}
        out = []
        for category in ("required", "optional"):
            for name in order.get(category, []):
                value = definition["input"][category][name]
                kind, options = value[0], (value[1] if len(value) > 1 and isinstance(value[1], dict) else {})
                if kind == "COMFY_AUTOGROW_V3":
                    prefix = options["template"]["prefix"]
                    for i in range(node.autogrow + 1):
                        out.append((f"{name}.{prefix}{i}", "IMAGE", {}, i > 0 or category == "optional", False))
                    continue
                if isinstance(kind, list):
                    out.append((name, "COMBO", {"options": kind, **options}, category == "optional", True))
                elif kind == "COMBO":
                    out.append((name, "COMBO", options, category == "optional", True))
                else:
                    out.append((name, kind, options, category == "optional", kind in WIDGET_TYPES and not options.get("forceInput")))
        return out

    def outputs(self, node):
        if node.type == "Note":
            return []
        definition = self.info[node.type]
        return [(name, node.out_types.get(i, kind)) for i, (name, kind) in enumerate(zip(definition["output_name"], definition["output"]))]

    # ---- UI workflow ------------------------------------------------------------------------------------------------
    def workflow(self):
        links, link_of, outgoing = [], {}, {}
        for node in self.nodes:
            specs = {s[0]: s for s in self.spec(node)} if node.type != "Note" else {}
            for name, (source, slot) in node.links.items():
                if name not in specs:
                    sys.exit(f"{node.type} ({node.title}) has no input {name}")
                kind = self.outputs(source)[slot][1]
                lid = len(links) + 1
                links.append([lid, source.id, slot, node.id, None, kind])
                link_of[(node.id, name)] = lid
                outgoing.setdefault((source.id, slot), []).append(lid)
        nodes = []
        for order, node in enumerate(self.nodes):
            if node.type == "Note":
                nodes.append({"id": node.id, "type": "Note", "pos": node.pos, "size": node.size, "flags": {}, "order": order, "mode": 0,
                              "inputs": [], "outputs": [], "title": node.title, "properties": {}, "widgets_values": [node.widgets["text"]],
                              "color": "#432", "bgcolor": "#653"})
                continue
            sockets, widgets, values = [], [], []  # The frontend lists sockets first, then the widgets' inputs.
            for name, kind, options, optional, widget in self.spec(node):
                lid = link_of.get((node.id, name))
                if not widget and kind == "COMFY_MATCHTYPE_V3":
                    kind = node.out_types.get(0, "*")
                entry = {"localized_name": name, "name": name, "type": kind, "link": lid}
                if optional and not widget:
                    entry["shape"] = 7
                if widget:
                    entry["widget"] = {"name": name}
                    value = node.widgets.get(name, default(kind, options))
                    if kind == "COMBO" and options.get("options") and value not in options["options"]:
                        sys.exit(f"{node.type} ({node.title}): {name}={value!r} is not installed; ComfyUI offers {options['options']}")
                    values.append(value)
                    if options.get("control_after_generate"):
                        values.append(node.controls.get(name, "fixed"))
                    if options.get("image_upload"):
                        values.append("image")
                    widgets.append(entry)
                elif lid is not None or not optional or "." in name:
                    sockets.append(entry)
            inputs = sockets + widgets
            for slot, entry in enumerate(inputs):
                if entry["link"] is not None:
                    links[entry["link"] - 1][4] = slot
            outputs = [{"localized_name": name, "name": name, "type": kind, "links": outgoing.get((node.id, i), [])}
                       for i, (name, kind) in enumerate(self.outputs(node))]
            nodes.append({"id": node.id, "type": node.type, "pos": node.pos, "size": node.size, "flags": {}, "order": order, "mode": node.mode,
                          "inputs": inputs, "outputs": outputs, "title": node.title or node.type,
                          "properties": {"Node name for S&R": node.type, "cnr_id": "comfy-core"}, "widgets_values": values})
        return {"id": str(uuid.uuid4()), "revision": 0, "last_node_id": len(self.nodes), "last_link_id": len(links), "nodes": nodes,
                "links": links, "groups": self.groups, "config": {}, "extra": {"ds": {"scale": 0.55, "offset": [80, 120]}}, "version": 0.4}

    # ---- API prompt (validation, --smoke) ------------------------------------------------------------------------------
    def prompt(self):
        def resolve(source, slot):
            while source.mode == 4:  # Bypassed: its input of the same type stands in for the output.
                kind = self.outputs(source)[slot][1]
                spec = {s[0]: s for s in self.spec(source)}
                found = next(((s, k) for name, (s, k) in source.links.items() if spec[name][1] == kind), None)
                if not found:
                    sys.exit(f"bypassed {source.title} has no {kind} input")
                source, slot = found
            return [str(source.id), slot]
        out = {}
        for node in self.nodes:
            if node.type == "Note" or node.mode == 4:
                continue
            inputs = {}
            for name, kind, options, optional, widget in self.spec(node):
                if name in node.links:
                    inputs[name] = resolve(*node.links[name])
                elif widget:
                    inputs[name] = node.widgets.get(name, default(kind, options))
                elif not optional:
                    sys.exit(f"{node.type} ({node.title}): required input {name} is not connected")
            out[str(node.id)] = {"class_type": node.type, "inputs": inputs, "_meta": {"title": node.title or node.type}}
        return out


def http(server, path, data=None):
    request = urllib.request.Request(server + path, data=json.dumps(data).encode() if data is not None else None,
                                     headers={"Content-Type": "application/json"} if data is not None else {})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read())


def set_value(prompt, title, value, field="value"):
    """One input of the API prompt's node whose title starts with `title` (exactly one such node)."""
    hits = [n for n in prompt.values() if n["_meta"]["title"].startswith(title)]
    if len(hits) != 1:
        sys.exit(f"{len(hits)} nodes titled {title!r}")
    hits[0]["inputs"][field] = value


def run(server, prompt, poll=2.0):
    """Queue an API prompt, wait for it; returns its saved pictures as (subfolder, filename)."""
    queued = http(server, "/prompt", {"prompt": prompt, "client_id": uuid.uuid4().hex})
    if queued.get("node_errors"):
        sys.exit(json.dumps(queued["node_errors"], ensure_ascii=False)[:2000])
    pid = queued["prompt_id"]
    while True:
        time.sleep(poll)
        history = http(server, f"/history/{pid}")
        if pid not in history:
            continue
        entry = history[pid]
        status = entry.get("status", {})
        if status.get("status_str") == "error":
            sys.exit(json.dumps(status.get("messages", []), ensure_ascii=False)[:2000])
        if status.get("completed"):
            return [(i["subfolder"], i["filename"]) for o in entry.get("outputs", {}).values() for i in o.get("images", []) if i.get("type") == "output"]


# ---- the shared part ---------------------------------------------------------------------------------------------------
def common(g, x=0, y=0, negative=NEGATIVE, folder_name=FOLDER):
    """Models, the character LoRA (+ an optional second one, bypassed), her description, what to avoid and the folder."""
    ckpt = g.node("CheckpointLoaderSimple", "Unholy Desire Mix (Anima 2B)", (x, y), (380, 98), widgets={"ckpt_name": CHECKPOINT})
    clip = g.node("CLIPLoader", "Text encoder Anima (Qwen3 0.6B)", (x, y + 130), (380, 106),
                  widgets={"clip_name": TEXT_ENCODER[0], "type": TEXT_ENCODER[1], "device": "default"})
    vae = g.node("VAELoader", "VAE (Qwen Image)", (x, y + 270), (380, 58), widgets={"vae_name": VAE})
    # LoraLoader, not ...ModelOnly: some Anima LoRAs (Sysdeep Rias) also train the text encoder; without one it passes through.
    lora = g.node("LoraLoader", "LoRA postaci (ta sama co w zdjeciach)", (x, y + 370), (380, 106),
                  widgets={"lora_name": LORA, "strength_model": LORA_STRENGTH, "strength_clip": LORA_STRENGTH}, links={"model": ckpt[0], "clip": clip[0]})
    lora2 = g.node("LoraLoader", "LoRA 2 - opcjonalna (styl / stroj): Ctrl+B wlacza", (x, y + 510), (380, 106), mode=4,
                   widgets={"lora_name": LORA2, "strength_model": LORA2_STRENGTH, "strength_clip": LORA2_STRENGTH},
                   links={"model": lora[0], "clip": lora[1]})
    body = g.node("PrimitiveStringMultiline", "1. Postac (twarz, wlosy, sylwetka) - ten sam tekst wszedzie", (x + 420, y - 330), (440, 200),
                  widgets={"value": CHARACTER})
    outfit = g.node("PrimitiveStringMultiline", "1a. Stroj (bez butow w zblizeniach)", (x + 420, y - 100), (440, 90), widgets={"value": OUTFIT})
    who = g.node("StringConcatenate", "Postac + stroj", (x + 900, y - 330), (300, 110), widgets={"delimiter": ", "},
                 links={"string_a": body[0], "string_b": outfit[0]})
    avoid = g.node("PrimitiveStringMultiline", "1b. Postac - czego unikac (do negatywu)", (x + 420, y + 260), (440, 90), widgets={"value": AVOID})
    base_negative = g.node("PrimitiveStringMultiline", "Negatyw (wspolny)", (x + 420, y + 380), (440, 160), widgets={"value": negative})
    folder = g.node("PrimitiveString", "Folder zapisu (w output)", (x + 420, y + 570), (440, 58), widgets={"value": folder_name})
    negative_text = g.node("StringConcatenate", "Negatyw + czego unikac", (x + 900, y + 380), (300, 120),
                           widgets={"delimiter": ", "}, links={"string_a": base_negative[0], "string_b": avoid[0]})
    negative_cond = g.node("CLIPTextEncode", "Negative", (x + 900, y + 530), (300, 90), links={"clip": lora2[1], "text": negative_text[0]})
    g.group("Wspolne: modele, LoRA, postac", [ckpt, clip, vae, lora, lora2, body, outfit, who, avoid, base_negative, folder, negative_text, negative_cond])
    # c["clip"][0] in the workflows: the text encoder after the LoRAs.
    return dict(ckpt=ckpt, clip={0: lora2[1]}, vae=vae, model=lora2, who=who, folder=folder, negative=negative_cond)


def saved_as(g, folder, name, pos):
    return g.node("StringConcatenate", f"Zapis: {name}", pos, (300, 110), widgets={"string_b": name, "delimiter": "/"}, links={"string_a": folder[0]})


# ---- pose ----------------------------------------------------------------------------------------------------------------
def pose_workflow(info):
    g = Graph(info)
    c = common(g, 0, 760, NEGATIVE_POSE)
    g.note("Jak to dziala", (
        "Jej cala postac na plaskim zielonym tle: pose.png dla aplikacji i obrazek startowy dla unholy_anima_emocje_v2.\n\n"
        "Modele, sampler i LoRA jak w unholy_anima_zdjecie_v2. LoRA postaci dziala w obu przebiegach (tozsamosc nie odplywa w hires).\n"
        "Druga LoRA (styl/stroj) jest wylaczona: zaznacz ja i Ctrl+B.\n\n"
        "1. Postac: wklej ten sam tekst co w zdjeciach (bez full body / upper body - kadr dokleja kazdy flow sam).\n"
        "2. Poza: co robi, gest, mina. 1b: czego unikac (np. kolor oczu innej postaci).\n\n"
        "Tryb 'Poza z obrazka referencyjnego':\n- false: poza tylko z promptu (txt2img, denoise 1.0).\n"
        "- true: sylwetka z referencji na zielonym tle -> img2img (denoise 0.75; 0.65-0.7 trzyma poze 1:1, 0.8+ luzniej).\n\n"
        "Seed 1. przebiegu 'randomize': kazdy Run = nowe ujecie. Podoba sie -> 'fixed'.\n"
        "Zapis: output/<folder>/pose_00001_.png (832x1216 -> 1248x1824)."), (0, 0), (860, 340))
    pose = g.node("PrimitiveStringMultiline", "2. Poza (co robi, gest, mina)", (900, 0), (440, 120),
                  widgets={"value": "standing, hand on own hip, waving, smile, looking at viewer"})
    frame = g.node("PrimitiveStringMultiline", "3. Kadr i tlo (zostaw)", (900, 150), (440, 90), widgets={"value": "full body, centered, " + GREEN})
    text1 = g.node("StringConcatenate", "Postac + poza", (1380, 0), (300, 110), widgets={"delimiter": ", "}, links={"string_a": c["who"][0], "string_b": pose[0]})
    text2 = g.node("StringConcatenate", "+ kadr i tlo", (1380, 140), (300, 110), widgets={"delimiter": ", "}, links={"string_a": text1[0], "string_b": frame[0]})
    shown = g.node("PreviewAny", "Polaczony prompt", (1380, 280), (300, 160), links={"source": text2[0]})
    positive = g.node("CLIPTextEncode", None, (1380, 470), (300, 90), links={"clip": c["clip"][0], "text": text2[0]})
    g.group("Prompt", [pose, frame, text1, text2, shown, positive], "#3f7a4e")

    # The reference's pose as a faded grey silhouette on green (unholy_anima_pose_img2img).
    sx, sy = 0, 1200
    reference = g.node("LoadImage", "Referencja pozy (zielone tlo)", (sx, sy), (320, 330), widgets={"image": "04_kiko_sukienka.png"})
    scaled = g.node("ImageScale", "Do 832x1216", (sx + 360, sy), (300, 130), widgets={"upscale_method": "lanczos", "width": 832, "height": 1216, "crop": "center"},
                    links={"image": reference[0]})
    green = g.node("ImageToMask", "Kanal G", (sx + 360, sy + 160), (300, 58), widgets={"channel": "green"}, links={"image": scaled[0]})
    red = g.node("ImageToMask", "Kanal R", (sx + 360, sy + 250), (300, 58), widgets={"channel": "red"}, links={"image": scaled[0]})
    blue = g.node("ImageToMask", "Kanal B", (sx + 360, sy + 340), (300, 58), widgets={"channel": "blue"}, links={"image": scaled[0]})
    g_r = g.node("MaskComposite", "G - R", (sx + 700, sy), (300, 126), widgets={"x": 0, "y": 0, "operation": "subtract"},
                 links={"destination": green[0], "source": red[0]})
    backdrop = g.node("MaskComposite", "G - R - B = zielone tlo", (sx + 700, sy + 160), (300, 126), widgets={"x": 0, "y": 0, "operation": "subtract"},
                      links={"destination": g_r[0], "source": blue[0]})
    cut = g.node("ThresholdMask", "Tlo (prog)", (sx + 700, sy + 320), (300, 58), widgets={"value": 0.35}, links={"mask": backdrop[0]})
    figure = g.node("InvertMask", "Postac", (sx + 700, sy + 410), (300, 26), links={"mask": cut[0]})
    grown = g.node("GrowMask", "Postac + 4 px", (sx + 1040, sy), (300, 82), widgets={"expand": 4, "tapered_corners": True}, links={"mask": figure[0]})
    grey_mask = g.node("SolidMask", "Szary 0.55", (sx + 1040, sy + 120), (300, 106), widgets={"value": 0.55, "width": 832, "height": 1216})
    grey = g.node("MaskToImage", None, (sx + 1040, sy + 260), (300, 26), links={"mask": grey_mask[0]})
    faded = g.node("ImageBlend", "Wyblakla postac (blend = ile szarosci)", (sx + 1040, sy + 320), (300, 102),
                   widgets={"blend_factor": 0.65, "blend_mode": "normal"}, links={"image1": scaled[0], "image2": grey[0]})
    silhouette = g.node("ImageCompositeMasked", "Sylwetka na zielonym tle", (sx + 1380, sy), (300, 146),
                        widgets={"x": 0, "y": 0, "resize_source": False}, links={"destination": scaled[0], "source": faded[0], "mask": grown[0]})
    preview_s = g.node("PreviewImage", "Podglad sylwetki", (sx + 1380, sy + 180), (300, 330), links={"images": silhouette[0]})
    encoded = g.node("VAEEncode", None, (sx + 1720, sy), (220, 46), links={"pixels": silhouette[0], "vae": c["vae"][0]})
    g.group("Poza z obrazka -> sylwetka (tylko gdy tryb = true)", [reference, scaled, green, red, blue, g_r, backdrop, cut, figure, grown,
                                                                     grey_mask, grey, faded, silhouette, preview_s, encoded], "#7a6a3f")

    # The mode: the start latent and the first pass's denoise.
    mx, my = 1760, 0
    mode = g.node("PrimitiveBoolean", "Poza z obrazka referencyjnego? (false = z promptu)", (mx, my), (360, 58), widgets={"value": False})
    empty = g.node("EmptyLatentImage", "832x1216 (pion)", (mx, my + 100), (360, 106), widgets={"width": 832, "height": 1216, "batch_size": 1})
    latent = g.node("ComfySwitchNode", "Start: pusty / sylwetka", (mx, my + 240), (360, 78), out_types={0: "LATENT"},
                    links={"switch": mode[0], "on_false": empty[0], "on_true": encoded[0]})
    from_prompt = g.node("PrimitiveFloat", "Denoise: z promptu", (mx, my + 360), (360, 58), widgets={"value": 1.0})
    from_picture = g.node("PrimitiveFloat", "Denoise: z obrazka (0.7-0.8)", (mx, my + 450), (360, 58), widgets={"value": 0.75})
    denoise = g.node("ComfySwitchNode", "Denoise 1. przebiegu", (mx, my + 540), (360, 78), out_types={0: "FLOAT"},
                     links={"switch": mode[0], "on_false": from_prompt[0], "on_true": from_picture[0]})
    g.group("Tryb", [mode, empty, latent, from_prompt, from_picture, denoise], "#6a3f7a")

    kx, ky = 2180, 0
    first = g.node("KSampler", "1. przebieg (30 krokow)", (kx, ky), (320, 262), controls={"seed": "randomize"},
                   widgets={"seed": 990941020750616, "steps": 30, **SAMPLER, "denoise": 1.0},
                   links={"model": c["model"][0], "positive": positive[0], "negative": c["negative"][0], "latent_image": latent[0], "denoise": denoise[0]})
    decoded = g.node("VAEDecode", None, (kx, ky + 300), (220, 46), links={"samples": first[0], "vae": c["vae"][0]})
    preview = g.node("PreviewImage", "Podglad 1. przebiegu", (kx, ky + 380), (320, 420), links={"images": decoded[0]})
    bigger = g.node("ImageScaleBy", "Hires: x1.5", (kx + 360, ky), (300, 82), widgets={"upscale_method": "lanczos", "scale_by": 1.5}, links={"image": decoded[0]})
    re_encoded = g.node("VAEEncode", None, (kx + 360, ky + 120), (220, 46), links={"pixels": bigger[0], "vae": c["vae"][0]})
    hires = g.node("KSampler", "Hires fix (denoise 0.4)", (kx + 360, ky + 200), (320, 262), widgets={"seed": 20261018, "steps": 20, **SAMPLER, "denoise": 0.4},
                   links={"model": c["model"][0], "positive": positive[0], "negative": c["negative"][0], "latent_image": re_encoded[0]})
    final = g.node("VAEDecode", None, (kx + 360, ky + 500), (220, 46), links={"samples": hires[0], "vae": c["vae"][0]})
    prefix = saved_as(g, c["folder"], "pose", (kx + 720, ky))
    save = g.node("SaveImage", None, (kx + 720, ky + 140), (420, 640), links={"images": final[0], "filename_prefix": prefix[0]})
    g.group("Generowanie", [first, decoded, preview, bigger, re_encoded, hires, final, prefix, save], "#7a3f3f")
    return g


# ---- feelings ------------------------------------------------------------------------------------------------------------
def emotions_workflow(info):
    g = Graph(info)
    c = common(g, 0, 760, NEGATIVE_BUST)
    g.note("Jak to dziala", (
        "Wszystkie 20 emocji z obrazka pozy (wynik unholy_anima_poza_v2): stroj, fryzura i twarz z niego, mina i gest z promptu.\n"
        "Modele, sampler i ta sama LoRA postaci co w zdjeciach i pozie -> spojny zestaw.\n\n"
        "1. 'Poza (stroj i twarz)': obrazek na plaskim zielonym tle.\n"
        "2. 'Kadr popiersia': y/height tak, zeby w podgladzie byla glowa (z uszami/kapeluszem) do pasa.\n"
        "3. Postac: ten sam tekst co w zdjeciach i pozie; kadr dokleja sie sam. Kazda emocja dokleja swoje tagi.\n"
        "4. Denoise: 'sama twarz' (smile, smug, cool) i 'z gestem' (reszta). Za malo zmian -> +0.05; twarz/stroj odplywa -> -0.05.\n"
        "5. Wspolny seed = podobny szum wszedzie. Inny zestaw: zmien seed.\n\n"
        "Pojedyncza emocja od nowa: zmien jej tagi albo denoise; reszta zostaje z cache. Wylaczenie emocji: jej grupa + Ctrl+B "
        "(wtedy wylacz tez arkusz).\nZapis: output/<folder>/<emocja>_00001_.png i arkusz_emocji_00001_.png (5x4)."), (0, 0), (860, 340))
    picture = g.node("LoadImage", "Poza (stroj i twarz)", (900, 0), (320, 330), widgets={"image": "04_kiko_sukienka.png"})
    tall = g.node("ImageScale", "Do 1024x1536", (1260, 0), (300, 130), widgets={"upscale_method": "lanczos", "width": 1024, "height": 1536, "crop": "center"},
                  links={"image": picture[0]})
    crop = g.node("ImageCrop", "Kadr popiersia (glowa do pasa)", (1260, 160), (300, 130), widgets={"width": 1024, "height": 683, "x": 0, "y": 40},
                  links={"image": tall[0]})
    bust = g.node("ImageScale", "Popiersie 1216x832", (1260, 320), (300, 130), widgets={"upscale_method": "lanczos", "width": 1216, "height": 832, "crop": "center"},
                  links={"image": crop[0]})
    preview = g.node("PreviewImage", "Podglad kadru", (900, 360), (320, 260), links={"images": bust[0]})
    latent = g.node("VAEEncode", None, (1260, 480), (220, 46), links={"pixels": bust[0], "vae": c["vae"][0]})
    framing = g.node("PrimitiveStringMultiline", "Kadr i tlo (zostaw)", (900, 650), (440, 100),
                     widgets={"value": "upper body, portrait, bust, looking at viewer, centered, head fully in frame, " + GREEN})
    base = g.node("StringConcatenate", "Postac + kadr (bez miny)", (1380, 650), (300, 110), widgets={"delimiter": ", "},
                  links={"string_a": c["who"][0], "string_b": framing[0]})
    seed = g.node("PrimitiveInt", "Seed (wspolny)", (900, 790), (300, 82), widgets={"value": 20261018}, controls={"value": "fixed"})
    face = g.node("PrimitiveFloat", "Denoise: sama twarz", (1240, 790), (300, 58), widgets={"value": 0.5})
    gesture = g.node("PrimitiveFloat", "Denoise: z gestem", (1240, 880), (300, 58), widgets={"value": 0.65})
    g.group("Poza -> popiersie, kadr, seed, denoise", [picture, tall, crop, bust, preview, latent, framing, base, seed, face, gesture], "#3f7a4e")

    decoded = []
    for i, (emotion, tags, face_only) in enumerate(EMOTIONS):
        x, y = 1800 + (i % 5) * 400, (i // 5) * 1150
        tagged = g.node("StringConcatenate", f"{emotion}: tagi miny", (x, y), (340, 130), widgets={"string_b": tags, "delimiter": ", "},
                        links={"string_a": base[0]})
        encoded = g.node("CLIPTextEncode", None, (x, y + 160), (340, 90), links={"clip": c["clip"][0], "text": tagged[0]})
        sample = g.node("KSampler", emotion, (x, y + 280), (340, 262), widgets={"seed": 0, "steps": 30, **SAMPLER, "denoise": 0.5 if face_only else 0.65},
                        links={"model": c["model"][0], "positive": encoded[0], "negative": c["negative"][0], "latent_image": latent[0],
                               "seed": seed[0], "denoise": (face if face_only else gesture)[0]})
        image = g.node("VAEDecode", None, (x, y + 580), (220, 46), links={"samples": sample[0], "vae": c["vae"][0]})
        prefix = saved_as(g, c["folder"], emotion, (x, y + 660))
        save = g.node("SaveImage", None, (x, y + 800), (340, 290), links={"images": image[0], "filename_prefix": prefix[0]})
        g.group(emotion, [tagged, encoded, sample, image, prefix, save], "#7a3f3f" if face_only else "#3f789e")
        decoded.append(image)

    sx, sy = 1800, 4 * 1150
    batch = g.node("BatchImagesNode", "Wszystkie emocje", (sx, sy), (300, 60 + 22 * len(decoded)), autogrow=len(decoded),
                   links={f"images.image{i}": image[0] for i, image in enumerate(decoded)})
    grid = g.node("ImageGrid", "Arkusz 5x4 (kolejnosc jak w aplikacji)", (sx + 340, sy), (300, 160),
                  widgets={"columns": 5, "cell_width": 304, "cell_height": 208, "padding": 4}, links={"images": batch[0]})
    prefix = saved_as(g, c["folder"], "arkusz_emocji", (sx + 340, sy + 200))
    save = g.node("SaveImage", "Emotion sheet", (sx + 680, sy), (820, 520), links={"images": grid[0], "filename_prefix": prefix[0]})
    g.group("Emotion sheet (podglad calego zestawu)", [batch, grid, prefix, save], "#6a3f7a")
    return g


# ---- photos with a realistic background ----------------------------------------------------------------------------------
def photo_real_workflow(info):
    g = Graph(info)
    c = common(g, 0, 760, NEGATIVE_PHOTO, PHOTO_FOLDER)
    g.note("Jak to dziala", (
        "unholy_anima_zdjecie_v2 z realistycznym tlem: ona narysowana jak zawsze (ta sama LoRA postaci w obu przebiegach),\n"
        "tlo docisniete promptem do zdjecia (blok 3b, zostaw) i negatywem (rysowane/plaskie tlo, druga ona).\n\n"
        "Prompt = Postac + Stroj + Co robi + Miejsce + Realistyczne tlo.\n"
        "- Co robi: najpierw UJECIE, potem poza i mina. Jedno ujecie na zdjecie:\n"
        "  selfie / upper body / cowboy shot (do ud) / full body / from behind, mirror selfie (w lustrze).\n"
        "  Selfie i zblizenia: w 1a. Stroj bez butow, rajstop i spodnicy - inaczej model dorysowuje druga, cala ja.\n"
        "- Miejsce: konkretne prawdziwe miejsce, pora, swiatlo, pogoda (\"Seine bridge at night, city lights\").\n\n"
        "Pion 768x1280 (jak dotychczasowy feed) -> hires x1.5 = 1152x1920. Poziom zwieksza ryzyko dwoch postaci.\n"
        "Seed 'randomize': kazdy Run = nowe ujecie. Podoba sie -> 'fixed'.\n"
        "Zapis: output/<folder>/zdjecie_00001_.png."), (0, 0), (860, 380))
    doing = g.node("PrimitiveStringMultiline", "2. Co robi (UJECIE, poza, mina)", (900, 0), (440, 110),
                   widgets={"value": "cowboy shot, walking, holding coffee cup, looking at viewer, smile"})
    place = g.node("PrimitiveStringMultiline", "3. Miejsce (gdzie, pora, swiatlo)", (900, 140), (440, 90),
                   widgets={"value": "paris street, autumn, golden afternoon light"})
    real = g.node("PrimitiveStringMultiline", "3b. Realistyczne tlo (zostaw)", (900, 260), (440, 130), widgets={"value": REALISM})
    text1 = g.node("StringConcatenate", "Postac + co robi", (1380, 0), (300, 110), widgets={"delimiter": ", "}, links={"string_a": c["who"][0], "string_b": doing[0]})
    text2 = g.node("StringConcatenate", "+ miejsce", (1380, 140), (300, 110), widgets={"delimiter": ", "}, links={"string_a": text1[0], "string_b": place[0]})
    text3 = g.node("StringConcatenate", "+ realistyczne tlo", (1380, 280), (300, 110), widgets={"delimiter": ", "}, links={"string_a": text2[0], "string_b": real[0]})
    shown = g.node("PreviewAny", "Polaczony prompt", (1380, 420), (300, 160), links={"source": text3[0]})
    positive = g.node("CLIPTextEncode", None, (1380, 610), (300, 90), links={"clip": c["clip"][0], "text": text3[0]})
    g.group("Prompt", [doing, place, real, text1, text2, text3, shown, positive], "#3f7a4e")

    kx, ky = 1760, 0
    empty = g.node("EmptyLatentImage", "768x1280 (pion)", (kx, ky), (320, 106), widgets={"width": 768, "height": 1280, "batch_size": 1})
    first = g.node("KSampler", "1. przebieg (30 krokow)", (kx, ky + 140), (320, 262), controls={"seed": "randomize"},
                   widgets={"seed": 990941020750616, "steps": 30, **SAMPLER, "denoise": 1.0},
                   links={"model": c["model"][0], "positive": positive[0], "negative": c["negative"][0], "latent_image": empty[0]})
    decoded = g.node("VAEDecode", None, (kx, ky + 440), (220, 46), links={"samples": first[0], "vae": c["vae"][0]})
    preview = g.node("PreviewImage", "Podglad 1. przebiegu", (kx, ky + 520), (320, 480), links={"images": decoded[0]})
    bigger = g.node("ImageScaleBy", "Hires: x1.5", (kx + 360, ky), (300, 82), widgets={"upscale_method": "lanczos", "scale_by": 1.5}, links={"image": decoded[0]})
    re_encoded = g.node("VAEEncode", None, (kx + 360, ky + 120), (220, 46), links={"pixels": bigger[0], "vae": c["vae"][0]})
    hires = g.node("KSampler", "Hires fix (denoise 0.4)", (kx + 360, ky + 200), (320, 262), widgets={"seed": 20261018, "steps": 20, **SAMPLER, "denoise": 0.4},
                   links={"model": c["model"][0], "positive": positive[0], "negative": c["negative"][0], "latent_image": re_encoded[0]})
    final = g.node("VAEDecode", None, (kx + 360, ky + 500), (220, 46), links={"samples": hires[0], "vae": c["vae"][0]})
    prefix = saved_as(g, c["folder"], "zdjecie", (kx + 720, ky))
    save = g.node("SaveImage", None, (kx + 720, ky + 140), (420, 660), links={"images": final[0], "filename_prefix": prefix[0]})
    g.group("Generowanie", [empty, first, decoded, preview, bigger, re_encoded, hires, final, prefix, save], "#7a3f3f")
    return g


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="ComfyUI's user workflows folder")
    parser.add_argument("--server", default="http://127.0.0.1:8188")
    parser.add_argument("--smoke", choices=["pose", "real"], help="queue that workflow once (from the prompt) and wait for its picture")
    args = parser.parse_args()
    try:
        info = http(args.server, "/object_info")
    except OSError as error:
        sys.exit(f"ComfyUI is not running at {args.server} ({error}): it checks every node and model name")
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    built = {"unholy_anima_poza_v2.json": pose_workflow(info), "unholy_anima_emocje_v2.json": emotions_workflow(info),
             "unholy_anima_zdjecie_v2_real.json": photo_real_workflow(info)}
    for name, graph in built.items():
        graph.prompt()  # Every required input connected, bypasses resolvable.
        (out / name).write_text(json.dumps(graph.workflow(), ensure_ascii=False, indent=1), encoding="utf-8")
        print(out / name, f"({len(graph.nodes)} nodes)")
    if args.smoke:
        name = {"pose": "unholy_anima_poza_v2.json", "real": "unholy_anima_zdjecie_v2_real.json"}[args.smoke]
        prompt = copy.deepcopy(built[name].prompt())
        set_value(prompt, "Folder zapisu", "launchme_test")
        for subfolder, filename in run(args.server, prompt):
            print("saved", subfolder, filename)


if __name__ == "__main__":
    main()

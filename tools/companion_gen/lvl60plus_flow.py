"""Write lvl60plus_flow: a ComfyUI workflow, each companion's reference picture and a short guide, so the photos for
levels 60 and up (60-69, 70-79, 80-89) can be made by hand in ComfyUI.

    python tools/companion_gen/lvl60plus_flow.py [<folder>]     (default: Desktop\\companion_import\\lvl60plus_flow)"""
import importlib.util, json, os, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
spec = importlib.util.spec_from_file_location("comfy", HERE / "comfy.py")
comfy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comfy)

GUIDE = """lvl60plus_flow — zdjęcia na poziomy 60+ (60-69, 70-79, 80-89) w ComfyUI
===============================================================================

Modele (folder models\\ ComfyUI, %COMFY_SHARED%):
  diffusion_models\\qwen_image_2.1_turbo_int8_convrot.safetensors     (Qwen Image 2.1 Turbo, 8 kroków)
  text_encoders\\qwen3vl_8b_int8_convrot.safetensors                  (typ: qwen_image)
  vae\\qwen_image_2.1_vae_bf16.safetensors

1. Otwórz ComfyUI (http://127.0.0.1:8188) → Workflow → Open → lvl60plus_flow.json
   (albo przeciągnij plik na płótno).
2. Węzeł „Her reference picture": wybierz z listy companion/ref_<postać>.png (np. ref_kiko.png; te same pliki są w refs\\).
   To jej twarz i styl; ubranie i miejsce zmienisz w prompcie. Inny strój jako baza: dowolne jej
   popiersie z res\\companion\\<postać>\\<strój>\\smile.png (najlepiej na zielonym tle, z miejscem nad głową).
3. Węzeł „Selfie prompt": podmień TYLKO część po „Scene and outfit:" (do „The character is drawn…").
   Zostaw początek (pilnuje tej samej postaci i dwóch rąk) i koniec (styl, tło, 16:10).
   Rodzaj ujęcia: zamień zdanie po „but she may wear other clothes." na jedno z:
     arm    — selfie z ręki (ręka z telefonem poza kadrem; najmniej dodatkowych rąk przy jednej widocznej dłoni)
     mirror — w lustrze (w kadrze TYLKO odbicie, telefon tyłem)
     above  — z góry, na leżąco
     candid — ktoś zrobił jej zdjęcie (bez telefonu w kadrze; najpewniejsze dla anatomii)
     tripod — ze statywu (widać więcej postaci, obie ręce wolne)
   (pełne zdania: SHOTS w tools\\companion_gen\\comfy.py)
   Ostatnie zdanie to reguła stylu — tu ją ustawiasz (np. łagodniej/odważniej). Dorosła postać.
4. Seed: zmieniaj, aż wyjdzie dobrze. CFG 1 = szybko, negatyw ignorowany; CFG 2–3 = negatyw działa
   (mniej dodatkowych rąk), ~2x dłużej.
5. Queue. Wynik: output\\companion\\selfie_*.png w folderze ComfyUI (1536x960).
6. Skopiuj do <folder zdjęć z Ustawień>\\photos\\<postać>\\<zakres>\\<nazwa>.png
   zakres: 60-69, 70-79 albo 80-89 (otwiera się od pierwszej liczby; maksymalny poziom w aplikacji: 80).
   Obok połóż <nazwa>.txt — jedno zdanie, co jest na zdjęciu (np. „Zdjęcie, które ktoś jej zrobił. Na nocnym
   festiwalu…"). Ona o nim opowie w czacie.
7. W aplikacji: Ustawienia → System → Towarzyszka AI → „Tryb 18+" (domyślnie wyłączony) — bez niego
   zdjęcia z katalogów od 60 w górę nigdy nie są wysyłane.

Hurtem bez ComfyUI w przeglądarce: tools\\companion_gen\\high_levels.py + high_levels_scenes.txt
(jedna linia = jedno zdjęcie; RULES/NEGATIVE/CFG na górze skryptu).
"""


def main():
    folder = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path.home() / "Desktop" / "companion_import" / "lvl60plus_flow"
    (folder / "refs").mkdir(parents=True, exist_ok=True)
    scene = ("[candid] At a night festival, in an elegant dark yukata, holding a paper lantern, an alluring, confident smile.")
    nodes = comfy.qwen_graph(comfy.selfie_instruction(scene, "Suggestive but non-explicit: an adult woman, no nudity."), "companion/ref_kiko.png", 7070,
                             comfy.QWEN_UNETS[0], size=(1536, 960), prefix="companion/selfie", resolution=1280, negative=comfy.SELFIE_NEGATIVE, cfg=1.0)
    comfy.titled(nodes, {"1": "Qwen Image 2.1 Turbo (diffusion_models)", "2": "KV cache", "3": "Text encoder: qwen3vl_8b (type qwen_image)",
                         "4": "VAE: qwen_image_2.1_vae", "5": "Her reference picture (refs\\<name>.png)", "6": "Selfie prompt (edit the scene part)",
                         "7": "Seed", "9": "Turbo sigmas (8 steps)", "10": "CFG (1: fast; 2-3: the negative counts)", "14": "Photo canvas 1536x960 (16:10)",
                         "13": "Save (output\\companion\\selfie_*.png)"})
    (folder / "lvl60plus_flow.json").write_text(json.dumps(nodes, indent=2, ensure_ascii=False), encoding="utf-8")
    gen = pathlib.Path.home() / "Desktop" / "companion_gen"
    for character in sorted(p.parent.name for p in (ROOT / "res" / "companion").glob("*/character.txt")):
        source = gen / character / "costume_00" / "smile.png"
        if not source.exists():
            source = ROOT / "res" / "companion" / character / "costume_00" / "smile.png"
        if source.exists():
            picture = comfy.on_green(source, (1536, 1024), 0.14)
            picture.save(folder / "refs" / f"{character}.png")
            inputs = pathlib.Path(os.environ.get("COMFY_SHARED", str(pathlib.Path.home() / "ComfyUI"))) / "input" / "companion"  # ComfyUI's input folder: the node's list.
            if inputs.parent.exists():
                inputs.mkdir(exist_ok=True)
                picture.save(inputs / f"ref_{character}.png")
    (folder / "INSTRUKCJA.txt").write_text(GUIDE, encoding="utf-8")
    print(folder)


if __name__ == "__main__":
    main()

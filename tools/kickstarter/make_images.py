"""Kickstarter / tutorial images from the HPDG_DemoReel screenshots (build/kickstarter/raw).

usage: python tools/kickstarter/make_images.py            -> docs/kickstarter/images/*.png

cover_*.png       campaign cover (1920x1080 and Kickstarter's 1024x576 project image)
shot_*.png        clean framed screenshots for the story
tut_*.png         tutorial screenshots with numbered markers (numbers match the tutorial text)
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "demo_reel"))
import effects  # noqa: E402  (logo + fonts)

RAW = ROOT / "build" / "kickstarter" / "raw"
OUT = ROOT / "docs" / "kickstarter" / "images"
BG = (12, 13, 15)
ACCENT = effects.ACCENT
INK = effects.INK
INK_SOFT = effects.INK_SOFT


def load(name):
    img = Image.open(RAW / f"{name}.png").convert("RGBA")
    rects = json.loads((RAW / f"{name}.json").read_text(encoding="utf-8"))
    return img, rects


def find(rects, kind=None, text=None, lane="", index=0):
    hits = [r for r in rects if (kind is None or r["kind"] == kind) and (text is None or r["text"] == text)
            and (lane is None or r["lane"] == lane)]
    return hits[index]["rect"]


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, img.width - 1, img.height - 1], radius=radius, fill=255)
    out = img.copy()
    out.putalpha(mask)
    return out


def framed(img, width, pad=40):
    """Screenshot on the dark ground with rounded corners, a soft shadow and a thin accent edge."""
    scale = (width - 2 * pad) / img.width
    shot = img.resize((width - 2 * pad, round(img.height * scale)), Image.Resampling.LANCZOS)
    shot = rounded(shot, 14)
    canvas = Image.new("RGBA", (width, shot.height + 2 * pad), BG + (255,))
    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([pad + 6, pad + 14, pad + shot.width + 6, pad + shot.height + 14],
                                             radius=16, fill=(0, 0, 0, 170))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(14)))
    canvas.alpha_composite(shot, (pad, pad))
    ImageDraw.Draw(canvas).rounded_rectangle([pad, pad, pad + shot.width - 1, pad + shot.height - 1], radius=14,
                                             outline=(70, 32, 34), width=2)
    return canvas


def save(img, name):
    OUT.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").save(OUT / name, optimize=True)
    print(OUT / name)


# ---- cover --------------------------------------------------------------------------------------
def cover(shot):
    W, H = 1920, 1080
    c = Image.new("RGBA", (W, H), BG + (255,))
    effects.paste_center(c, effects.logo(1100, (255, 255, 255)), W * 0.86, H * 0.52, 0.03)
    # the plugin, large, bleeding off the right edge
    s = shot.resize((1380, 690), Image.Resampling.LANCZOS)
    s = rounded(s, 16)
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([700, 330, 700 + 1380, 330 + 690], radius=18, fill=(0, 0, 0, 200))
    c.alpha_composite(sh.filter(ImageFilter.GaussianBlur(24)))
    c.alpha_composite(s, (690, 300))
    d = ImageDraw.Draw(c)
    # title block
    mark = effects.logo(120, INK)
    glow = Image.new("RGBA", (mark.width + 100, mark.height + 100), ACCENT + (0,))
    glow.alpha_composite(effects.logo(120, ACCENT), (50, 50))
    c.alpha_composite(glow.filter(ImageFilter.GaussianBlur(14)), (90 - 50, 90 - 50))
    c.alpha_composite(mark, (90, 90))
    d.text((90, 390), "HPDG", font=effects.font(150, bold=True), fill=INK, anchor="ls")
    d.rectangle([94, 414, 230, 421], fill=ACCENT)
    d.text((92, 500), "Drum & Bass", font=effects.font(58, bold=True), fill=INK, anchor="ls")
    d.text((92, 566), "Generator VST3", font=effects.font(58, bold=True), fill=INK, anchor="ls")
    lines = ["Boom Bap · Trap · DnB · Techno", "Builds beats from your samples", "Drag MIDI / WAV into any DAW"]
    for i, t in enumerate(lines):
        y = 670 + i * 54
        d.ellipse([94, y - 20, 108, y - 6], fill=ACCENT)
        d.text((126, y), t, font=effects.font(34), fill=INK_SOFT, anchor="ls")
    d.text((92, 1010), "by HamloProd", font=effects.font(30, bold=True), fill=INK_SOFT, anchor="ls")
    save(c, "cover_1920x1080.png")
    save(c.resize((1024, 576), Image.Resampling.LANCZOS), "cover_1024x576.png")


# ---- tutorial markers ---------------------------------------------------------------------------
def marker(d, n, x, y):
    r = 19
    d.ellipse([x - r - 3, y - r - 3, x + r + 3, y + r + 3], fill=(12, 13, 15, 255))
    d.ellipse([x - r, y - r, x + r, y + r], fill=ACCENT + (255,))
    d.text((x, y + 1), str(n), font=effects.font(24, bold=True), fill=(255, 255, 255), anchor="mm")


def annotate(img, crop, marks, pad_top=0, pad_bottom=0, out_width=1600):
    """crop: (x, y, w, h) in screenshot pixels; marks: [(n, rect, side)] side = up|down|left|right|on."""
    x0, y0, w, h = crop
    part = img.crop((x0, y0, x0 + w, y0 + h))
    canvas = Image.new("RGBA", (w, h + pad_top + pad_bottom), BG + (255,))
    canvas.alpha_composite(part, (0, pad_top))
    d = ImageDraw.Draw(canvas)
    for n, r, side in marks:
        cx = r[0] - x0 + r[2] / 2
        cy = r[1] - y0 + r[3] / 2 + pad_top
        # outline the control
        d.rounded_rectangle([r[0] - x0 - 3, r[1] - y0 + pad_top - 3, r[0] - x0 + r[2] + 3, r[1] - y0 + pad_top + r[3] + 3],
                            radius=6, outline=ACCENT + (255,), width=3)
        if side == "up":
            mx, my = cx, r[1] - y0 + pad_top - 34
        elif side == "down":
            mx, my = cx, r[1] - y0 + pad_top + r[3] + 34
        elif side == "left":
            mx, my = r[0] - x0 - 32, cy
        elif side == "right":
            mx, my = r[0] - x0 + r[2] + 32, cy
        else:
            mx, my = cx, cy
        if side != "on":
            d.line([cx, cy, mx, my], fill=ACCENT + (255,), width=3)
        marker(d, n, mx, my)
    if canvas.width > out_width:
        canvas = canvas.resize((out_width, round(canvas.height * out_width / canvas.width)), Image.Resampling.LANCZOS)
    return canvas


def tutorials(img, rects):
    sliders = [r for r in rects if r["kind"] == "slider" and r["lane"] == ""]
    by_pos = lambda x, y: min(sliders, key=lambda r: abs(r["rect"][0] - x) + abs(r["rect"][1] - y))["rect"]
    header = find(rects, "header")
    # 1 header
    marks = [
        (1, find(rects, "button", "Generate Pattern"), "up"),
        (2, find(rects, "button", "Stop"), "up"),
        (3, find(rects, "combo", "Boom Bap"), "down"),
        (4, find(rects, "combo", "Classic"), "down"),
        (5, find(rects, "combo", "4"), "down"),
        (6, by_pos(660, 120), "down"),
        (7, by_pos(774, 120), "down"),
        (8, find(rects, "button", "Mutate"), "up"),
        (9, by_pos(675, 51), "up"),
        (10, find(rects, "button", "Export Loop WAV"), "up"),
        (11, find(rects, "button", "Drag Full"), "up"),
        (12, find(rects, "button", "Presets"), "up"),
        (13, by_pos(1167, 117), "down"),
        (14, find(rects, "button", "SKIN: DARK"), "down"),
    ]
    save(annotate(img, (0, 0, 2400, header[1] + header[3] + 10), marks, pad_top=60, pad_bottom=60), "tut_1_header.png")

    # 2 lane controls (Kick + Sub808 rows)
    kick = find(rects, "row", lane="Kick")
    sub = find(rects, "row", lane="Sub808")
    marks = [
        (1, find(rects, "button", "RG", "Kick"), "up"),
        (2, find(rects, "button", "S", "Kick"), "up"),
        (3, find(rects, "button", "M", "Kick"), "up"),
        (4, find(rects, "button", "C", "Kick"), "up"),
        (5, find(rects, "button", "L", "Kick"), "up"),
        (6, find(rects, "button", "E", "Kick"), "up"),
        (7, find(rects, "button", "<", "Kick"), "up"),
        (8, find(rects, "button", ">", "Kick"), "up"),
        (9, find(rects, "slider", lane="Kick"), "up"),
        (10, find(rects, "button", "Drag", "Kick"), "up"),
        (11, find(rects, "button", "WAV", "Kick"), "up"),
        (12, find(rects, "bass", lane="Sub808"), "down"),
        (13, find(rects, "button", "-", "Sub808"), "down"),
    ]
    y_top = kick[1] - 4
    crop = (0, y_top, 1160, sub[1] + sub[3] - y_top + 4)
    save(annotate(img, crop, marks, pad_top=60, pad_bottom=60), "tut_2_lanes.png")


def sample_tutorial(img, rects, name, mode_text, analyze_text):
    strip = find(rects, "strip")
    sliders = [r for r in rects if r["kind"] == "slider" and r["lane"] == ""]
    quant = min(sliders, key=lambda r: abs(r["rect"][0] - 1587) + abs(r["rect"][1] - 210))["rect"]
    info = [strip[0] + 8, strip[1] + 40, 330, 92]
    marks = [
        (1, find(rects, "button", "Open..."), "up"),
        (2, find(rects, "button", "Trim..."), "up"),
        (3, find(rects, "combo", mode_text), "up"),
        (4, quant, "up"),
        (5, find(rects, "button", analyze_text), "up"),
        (6, find(rects, "button", "Play w/ HPDG"), "up"),
        (7, info, "right"),
    ]
    crop = (0, strip[1] - 8, 2400, strip[3] + 16)
    save(annotate(img, crop, marks, pad_top=60, pad_bottom=20), name)


def overview(img, rects):
    zones = [("A", find(rects, "header"), "Generator"), ("B", find(rects, "strip"), "Sample Analysis"),
             ("C", find(rects, "rack"), "Instrument Rack"), ("D", find(rects, "grid"), "Pattern")]
    c = img.copy()
    d = ImageDraw.Draw(c)
    for letter, r, label in zones:
        d.rounded_rectangle([r[0] - 2, r[1] - 2, r[0] + r[2] + 2, r[1] + r[3] + 2], radius=10, outline=ACCENT + (255,), width=4)
        f = effects.font(30, bold=True)
        tw = d.textlength(label, font=f)
        bx, by = r[0] + r[2] - tw - 90, r[1] + 10
        d.rounded_rectangle([bx, by, bx + tw + 74, by + 50], radius=25, fill=(12, 13, 15, 235), outline=ACCENT + (255,), width=2)
        d.ellipse([bx + 8, by + 7, bx + 44, by + 43], fill=ACCENT + (255,))
        d.text((bx + 26, by + 25), letter, font=f, fill=(255, 255, 255), anchor="mm")
        d.text((bx + 56, by + 25), label, font=f, fill=INK, anchor="lm")
    save(framed(c, 1920), "tut_0_overview.png")


def main():
    boombap, boombap_r = load("boombap")
    guide, guide_r = load("guide")
    copybreak, copybreak_r = load("copybreak")
    cover(boombap)
    for name in ("boombap", "trap", "guide", "copybreak", "skin_dark", "skin_steel", "skin_sketch"):
        img, _ = load(name)
        save(framed(img, 1920), f"shot_{name}.png")
    # three skins side by side
    skins = [load(n)[0] for n in ("skin_dark", "skin_steel", "skin_sketch")]
    w = 1920
    tile_w = (w - 4 * 30) // 3
    tiles = [rounded(s.resize((tile_w, round(s.height * tile_w / s.width)), Image.Resampling.LANCZOS), 10) for s in skins]
    row = Image.new("RGBA", (w, tiles[0].height + 60), BG + (255,))
    for i, t in enumerate(tiles):
        row.alpha_composite(t, (30 + i * (tile_w + 30), 30))
    save(row, "shot_skins.png")
    overview(guide, guide_r)
    tutorials(guide, guide_r)
    sample_tutorial(guide, guide_r, "tut_3_guide.png", "Guide generation", "Analyze")
    sample_tutorial(copybreak, copybreak_r, "tut_4_copybreak.png", "Copy break (K/S/H)", "Analyze + Copy")


if __name__ == "__main__":
    main()

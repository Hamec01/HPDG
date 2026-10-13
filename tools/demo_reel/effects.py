"""Motion graphics for compose.py: the logo, title / chapter cards, callouts next to clicks, the
camera punch-in on clicks and the DAW window the patterns are dragged into."""
import math
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONT_DIR = Path("C:/Windows/Fonts")
ACCENT = (226, 62, 62)
INK = (236, 238, 242)
INK_SOFT = (150, 156, 166)


def font(size, bold=False):
    f = ImageFont.truetype(str(FONT_DIR / "bahnschrift.ttf"), size)
    try:
        f.set_variation_by_name("SemiBold" if bold else "Regular")
    except Exception:
        pass
    return f


F_NOTE = font(30, bold=True)
F_SPLASH_NUM = font(150, bold=True)
F_SPLASH = font(96, bold=True)
F_TITLE = font(130, bold=True)
F_TITLE_SUB = font(44)
F_TITLE_SMALL = font(30)
F_DAW = font(20)
F_DAW_BOLD = font(22, bold=True)
F_CHIP = font(24, bold=True)


def ease(k):
    k = max(0.0, min(1.0, k))
    return k * k * (3 - 2 * k)


def ease_out(k):
    k = max(0.0, min(1.0, k))
    return 1 - (1 - k) ** 3


def ease_out_back(k):
    k = max(0.0, min(1.0, k))
    c = 1.70158
    return 1 + (c + 1) * (k - 1) ** 3 + c * (k - 1) ** 2


# ---- logo --------------------------------------------------------------------------------------
@lru_cache(maxsize=32)
def logo(height, color=INK, alpha=255):
    """The HamloProd mark (Assets/Images/Logo.png, black on transparent) tinted, at any height."""
    src = Image.open(ROOT / "Assets" / "Images" / "Logo.png").convert("RGBA")
    a = src.getchannel("A")
    w = round(src.width * height / src.height)
    a = a.resize((w * 2, height * 2), Image.Resampling.LANCZOS).filter(ImageFilter.GaussianBlur(1.2))
    a = a.point(lambda v: max(0, min(255, (v - 110) * 4)))  # crisp edges after the upscale
    a = a.resize((w, height), Image.Resampling.LANCZOS)
    if alpha < 255:
        a = a.point(lambda v: v * alpha // 255)
    out = Image.new("RGBA", (w, height), color + (0,))
    out.putalpha(a)
    return out


def paste_center(canvas, img, cx, cy, opacity=1.0):
    if opacity < 0.999:
        img = img.copy()
        img.putalpha(img.getchannel("A").point(lambda v: int(v * opacity)))
    canvas.alpha_composite(img, (int(cx - img.width / 2), int(cy - img.height / 2)))


# ---- cards ---------------------------------------------------------------------------------------
def title_card(size, text, alpha, local, intro):
    W, H = size
    card = Image.new("RGBA", (W, H), (12, 13, 15, 255))
    d = ImageDraw.Draw(card)
    # faint big mark in the background, slowly drifting
    paste_center(card, logo(900, (255, 255, 255)), W * 0.78 + local * 0.25, H * 0.55, 0.035)
    # step grid with a running playhead (the beat underneath is playing)
    for i in range(32):
        x = 160 + i * 50
        on = (i * 7 + 3) % 5 in (0, 2) or i % 8 == 0
        shade = 34 if not on else 58
        d.rounded_rectangle([x, 900, x + 40, 940], radius=5, fill=(shade, shade + 2, shade + 6))
    head = (local // 4) % 32
    d.rounded_rectangle([160 + head * 50, 900, 200 + head * 50, 940], radius=5, fill=ACCENT)

    k_logo = ease_out_back(local / 22.0) if intro else 1.0
    lh = int(230 * (0.7 + 0.3 * k_logo))
    mark = logo(lh, ACCENT)
    glow = Image.new("RGBA", (mark.width + 120, mark.height + 120), ACCENT + (0,))
    glow.alpha_composite(mark, (60, 60))
    glow = glow.filter(ImageFilter.GaussianBlur(18))  # padded so the blur is not cut square
    paste_center(card, glow, W / 2, 270, 0.55 * min(1.0, local / 20.0) if intro else 0.45)
    paste_center(card, logo(lh, INK), W / 2, 270, min(1.0, local / 10.0) if intro else 1.0)

    k_text = ease_out((local - 12) / 16.0) if intro else 1.0
    shift = (1 - k_text) * 40
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ta = int(255 * k_text)
    ld.text((W / 2, 520 + shift), text[0], font=F_TITLE, fill=INK + (ta,), anchor="mm")
    tw = ld.textlength(text[0], font=F_TITLE)
    ld.rectangle([W / 2 - tw / 2, 598 + shift, W / 2 - tw / 2 + 130 * k_text, 604 + shift], fill=ACCENT + (ta,))
    if len(text) > 1:
        ld.text((W / 2, 670 + shift * 1.3), text[1], font=F_TITLE_SUB, fill=INK + (ta,), anchor="mm")
    if len(text) > 2:
        ld.text((W / 2, 740 + shift * 1.6), text[2], font=F_TITLE_SMALL, fill=INK_SOFT + (ta,), anchor="mm")
    card.alpha_composite(layer)
    card.putalpha(card.getchannel("A").point(lambda v: int(v * alpha)))
    return card


SPLASH_FRAMES = 40


def chapter_splash(canvas, text, local, editor_h):
    """A band across the picture: chapter number + name slide in, the mark behind."""
    if local < 0 or local >= SPLASH_FRAMES:
        return
    W = canvas.width
    k_in = ease_out(local / 9.0)
    k_out = ease((local - (SPLASH_FRAMES - 9)) / 9.0)
    band_h = int(300 * k_in * (1 - k_out))
    if band_h < 2:
        return
    cy = editor_h // 2
    band = Image.new("RGBA", (W, band_h), (12, 13, 15, 238))
    d = ImageDraw.Draw(band)
    d.rectangle([0, 0, W, 3], fill=ACCENT)
    d.rectangle([0, band_h - 4, W, band_h], fill=ACCENT)
    paste_center(band, logo(260, (255, 255, 255)), W - 260, band_h / 2, 0.06)
    parts = text.split("·")
    num = parts[0].strip()
    name = parts[-1].strip().upper()
    slide = (1 - k_in) * -300 + k_out * 200
    d.text((200 + slide, band_h / 2), num, font=F_SPLASH_NUM, fill=ACCENT, anchor="lm")
    nw = d.textlength(num, font=F_SPLASH_NUM)
    d.text((240 + nw + slide * 0.6, band_h / 2 + 6), name, font=F_SPLASH, fill=INK, anchor="lm")
    canvas.alpha_composite(band, (0, cy - band_h // 2))


# ---- callout next to a click ---------------------------------------------------------------------
NOTE_FRAMES = 60


def draw_note(canvas, text, sx, sy, local):
    if local < 0 or local >= NOTE_FRAMES:
        return
    W = canvas.width
    k = ease_out_back(local / 8.0) if local < 8 else 1.0
    fade = 1 - ease((local - (NOTE_FRAMES - 10)) / 10.0)
    d0 = ImageDraw.Draw(canvas)
    tw = d0.textlength(text, font=F_NOTE)
    bw, bh = int(tw + 52), 58
    x = sx + 34
    y = sy - 92
    if x + bw > W - 20:
        x = sx - 34 - bw
    if y < 16:
        y = sy + 40
    bubble = Image.new("RGBA", (bw + 12, bh + 14), (0, 0, 0, 0))
    d = ImageDraw.Draw(bubble)
    d.rounded_rectangle([5, 7, bw + 5, bh + 7], radius=12, fill=(0, 0, 0, 120))
    d.rounded_rectangle([0, 0, bw, bh], radius=12, fill=(20, 21, 25, 246), outline=ACCENT, width=3)
    d.rectangle([0 + 10, 12, 16, bh - 12], fill=ACCENT)
    d.text((32, bh / 2), text, font=F_NOTE, fill=INK, anchor="lm")
    if k < 0.999:
        bubble = bubble.resize((max(1, int(bubble.width * k)), max(1, int(bubble.height * k))), Image.Resampling.BICUBIC)
    if fade < 0.999:
        bubble.putalpha(bubble.getchannel("A").point(lambda v: int(v * fade)))
    # thin pointer line from the click to the bubble
    lx = x if x > sx else x + bw
    d0.line([sx, sy, lx, y + bh / 2], fill=ACCENT + (int(200 * fade),), width=3)
    canvas.alpha_composite(bubble, (int(x), int(y)))


# ---- camera --------------------------------------------------------------------------------------
class Camera:
    """Punch-in toward each click (zoom 1.0 -> PUNCH -> 1.0), smoothed so successive clicks glide."""
    PUNCH = 0.22

    def __init__(self, events, total, editor_w, editor_h):
        import numpy as np
        weight = np.zeros(total)
        cx = np.full(total, editor_w / 2.0)
        cy = np.full(total, editor_h / 2.0)
        blocked = np.zeros(total, dtype=bool)
        for e in events:
            if e["type"] in ("dawdrag",):
                blocked[e["frame"]: e["frame"] + e["frames"] + 20] = True
        for e in events:
            if e["type"] == "click":
                f, r = e["frame"], e["rect"]
                p = (r[0] + r[2] / 2, r[1] + r[3] / 2)
            elif e["type"] == "drag":
                f, p = e["frame"], tuple(e["from"])
            else:
                continue
            for i in range(max(0, f - 16), min(total, f + 50)):
                k = i - f
                if k < 0:
                    w = ease((k + 16) / 16.0)
                elif k < 20:
                    w = 1.0
                else:
                    w = 1 - ease((k - 20) / 30.0)
                if w > weight[i]:
                    weight[i] = w
                    cx[i], cy[i] = p
        # frames without a click keep the nearest click's centre (zoom is 1.0 there anyway), so the
        # camera never drifts from the screen centre into a click; then glide between clicks
        has = weight > 0
        idx = np.where(has, np.arange(total), -1)
        idx = np.maximum.accumulate(idx)
        first = np.argmax(has) if has.any() else 0
        idx[idx < 0] = first
        cx, cy = cx[idx].copy(), cy[idx].copy()
        for arr in (cx, cy):
            a = 0.3
            for i in range(1, total):
                arr[i] = arr[i - 1] + (arr[i] - arr[i - 1]) * a
            for i in range(total - 2, -1, -1):
                arr[i] = arr[i + 1] + (arr[i] - arr[i + 1]) * a
        weight[blocked] = 0
        self.factor = 1.0 + self.PUNCH * weight
        self.cx, self.cy = cx, cy

    def at(self, frame):
        return float(self.cx[frame]), float(self.cy[frame]), float(self.factor[frame])


# ---- the DAW the patterns are dragged into -------------------------------------------------------
DAW_TRACKS = 4
DROP_AT = 0.5   # fraction of the event when the file lands


def daw_geometry(W, editor_h):
    x0, x1 = 80, W - 80
    h = 430
    return x0, x1, h, editor_h - h - 30


def daw_drop_point(index, W, editor_h):
    x0, x1, h, y_final = daw_geometry(W, editor_h)
    row = 1 + index % (DAW_TRACKS - 1)
    lane_top = y_final + 44 + 34 + row * 74
    return x0 + 210 + 60, lane_top + 37


def _clip(d, x, y, w, h, name, kind, grow=1.0):
    w = max(6, int(w * grow))
    color = (70, 120, 200) if kind == "midi" else (196, 120, 60)
    d.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=color + (235,), outline=(255, 255, 255, 90))
    d.rectangle([x, y, x + w, y + 20], fill=tuple(c // 2 for c in color) + (255,))
    d.text((x + 8, y + 10), name, font=F_DAW, fill=(255, 255, 255), anchor="lm")
    if kind == "midi":
        for i in range(int(w // 14)):
            nx = x + 6 + i * 14
            ny = y + 26 + ((i * 5 + 3) % 7) * 5
            d.rectangle([nx, ny, nx + 9, ny + 4], fill=(230, 240, 255))
    else:
        mid = y + 20 + (h - 20) / 2
        for i in range(int(w // 3)):
            amp = (h - 26) / 2 * (0.25 + 0.75 * abs(math.sin(i * 0.37) * math.cos(i * 0.11)))
            d.line([x + 3 + i * 3, mid - amp, x + 3 + i * 3, mid + amp], fill=(255, 235, 210), width=2)


def draw_daw(canvas, e, local, earlier, cursor, editor_h):
    """canvas: the picture; e: the dawdrag event; earlier: names dropped before (stay in the DAW)."""
    W = canvas.width
    frames = e["frames"]
    k_in = ease_out(local / 16.0)
    k_out = ease((local - (frames - 16)) / 16.0)
    x0, x1, h, y_final = daw_geometry(W, editor_h)
    y = int(y_final + (1 - k_in + k_out) * (h + 60))
    win = Image.new("RGBA", (x1 - x0, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(win)
    d.rounded_rectangle([0, 0, x1 - x0 - 1, h - 1], radius=10, fill=(30, 33, 38, 250), outline=(90, 96, 106), width=2)
    d.rounded_rectangle([0, 0, x1 - x0 - 1, 44], radius=10, fill=(44, 48, 56))
    d.rectangle([0, 30, x1 - x0, 44], fill=(44, 48, 56))
    for i, c in enumerate([(236, 95, 90), (240, 190, 70), (98, 196, 90)]):
        d.ellipse([16 + i * 24, 15, 30 + i * 24, 29], fill=c)
    d.text((100, 22), "Your DAW  —  Playlist", font=F_DAW_BOLD, fill=INK, anchor="lm")
    # ruler
    ruler_y = 44
    d.rectangle([0, ruler_y, x1 - x0, ruler_y + 34], fill=(38, 41, 47))
    for bar in range(9):
        bx = 210 + bar * 180
        d.line([bx, ruler_y, bx, h], fill=(52, 56, 64), width=1)
        d.text((bx + 6, ruler_y + 17), str(bar + 1), font=F_DAW, fill=INK_SOFT, anchor="lm")
    names = ["Sample", "Drums", "Drums 2", "Bass"]
    for t in range(DAW_TRACKS):
        ty = ruler_y + 34 + t * 74
        d.rectangle([0, ty, 200, ty + 72], fill=(40, 44, 50))
        d.line([0, ty + 73, x1 - x0, ty + 73], fill=(48, 52, 60))
        d.text((16, ty + 36), f"Track {t + 1}", font=F_DAW, fill=INK, anchor="lm")
    # the user's own sample already in the project
    _clip(d, 214, ruler_y + 34 + 6, 4 * 180 - 8, 60, "sample.wav", "wav")
    def kind_of(n):
        return "midi" if n.lower().endswith(".mid") else "wav"
    for i, n in enumerate(earlier):
        row = 1 + i % (DAW_TRACKS - 1)
        _clip(d, 214 + (i // (DAW_TRACKS - 1)) * 4 * 180, ruler_y + 34 + row * 74 + 6, 4 * 180 - 8, 60, n, kind_of(n))
    drop_k = (local - frames * DROP_AT) / 8.0
    if drop_k >= 0:
        index = len(earlier)
        row = 1 + index % (DAW_TRACKS - 1)
        _clip(d, 214 + (index // (DAW_TRACKS - 1)) * 4 * 180, ruler_y + 34 + row * 74 + 6, 4 * 180 - 8, 60,
              e["name"], kind_of(e["name"]), grow=ease_out(drop_k))
        # drop flash
        if drop_k < 1.5:
            fy = ruler_y + 34 + row * 74
            d.rectangle([200, fy, x1 - x0, fy + 72], outline=ACCENT + (int(255 * (1 - drop_k / 1.5)),), width=3)
    canvas.alpha_composite(win, (x0, y))

    # the file being dragged, under the cursor
    if local < frames * DROP_AT and local > 4:
        cxs, cys = cursor
        label = e["name"]
        dd = ImageDraw.Draw(canvas)
        tw = dd.textlength(label, font=F_CHIP)
        box = [cxs + 22, cys + 24, cxs + 22 + 64 + tw + 22, cys + 24 + 56]
        dd.rounded_rectangle([box[0] + 4, box[1] + 5, box[2] + 4, box[3] + 5], radius=9, fill=(0, 0, 0, 120))
        midi = kind_of(label) == "midi"
        dd.rounded_rectangle(box, radius=9, fill=(36, 38, 44, 248), outline=(70, 120, 200) if midi else (196, 120, 60), width=3)
        dd.text((box[0] + 18, box[1] + 28), "MIDI" if midi else "WAV", font=F_DAW_BOLD,
                fill=(130, 175, 255) if midi else (240, 170, 110), anchor="lm")
        dd.text((box[0] + 76, box[1] + 28), label, font=F_CHIP, fill=INK, anchor="lm")

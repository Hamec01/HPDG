"""Builds the demo video from a HPDG_DemoReel recording.

usage: python compose.py <recording folder> <out.mp4> [--preview N]   (--preview: every Nth frame, quick check)

The recording holds frames/*.jpg + frames.txt (one file name per video frame), audio.wav and
timeline.jsonl (script events with screen positions). On top of the UI frames this draws the
mouse cursor, clicks, opened combo menus, the dragged sample file, highlights and zooms, and puts
the captions in a bar under the plugin; title cards cover the whole frame.
"""
import json
import math
import subprocess
import sys
from pathlib import Path

import imageio_ffmpeg
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import effects

W, H = 1920, 1080
BAR_H = 120
FONT_DIR = Path("C:/Windows/Fonts")

ACCENT = (226, 62, 62)
INK = (236, 238, 242)
INK_SOFT = (150, 156, 166)
BAR_BG = (16, 17, 20)


def font(size, bold=False):
    for name in (["bahnschrift.ttf"] if not bold else ["bahnschrift.ttf"]):
        try:
            f = ImageFont.truetype(str(FONT_DIR / name), size)
            try:
                f.set_variation_by_name("SemiBold" if bold else "Regular")
            except Exception:
                pass
            return f
        except OSError:
            continue
    return ImageFont.truetype(str(FONT_DIR / "segoeui.ttf"), size)


F_CAPTION = font(40, bold=True)
F_CAPTION_SMALL = font(30)
F_CHAPTER = font(24, bold=True)
F_TITLE = font(118, bold=True)
F_TITLE_SUB = font(42)
F_TITLE_SMALL = font(30)
F_MENU = font(22)
F_FILE = font(22, bold=True)
F_BADGE = font(26, bold=True)


def ease(k):
    k = max(0.0, min(1.0, k))
    return k * k * (3 - 2 * k)


def ease_out(k):
    k = max(0.0, min(1.0, k))
    return 1 - (1 - k) ** 3


class Timeline:
    def __init__(self, events, total_frames):
        self.events = events
        self.total = total_frames
        self.editor_w = 1920
        self.editor_h = 960
        for e in events:
            if e["type"] == "editor":
                self.editor_w, self.editor_h = e["width"], e["height"]
        self._build_cursor()
        self._build_notes()

    def _build_notes(self):
        # a note shows next to the first action that follows it
        self.notes = []
        anchors = [e for e in self.events if e["type"] in ("click", "drag", "drop", "dawdrag")]
        for e in self.events:
            if e["type"] != "note":
                continue
            a = next((x for x in anchors if x["frame"] >= e["frame"]), None)
            if a is None:
                continue
            if a["type"] == "drag":
                p = a["from"]
            elif a["type"] == "drop":
                r = a["rect"]
                p = (r[0] + r[2] * 0.3, r[1] + r[3] * 0.45)
            else:
                r = a["rect"]
                p = (r[0] + r[2] / 2, r[1] + r[3] / 2)
            start = a["frame"] + (int(a["frames"] * 0.75) if a["type"] == "drop" else 0)
            self.notes.append((start, e["text"][0], p))

    def _build_cursor(self):
        # cursor keyframes: (frame, x, y). A move starts at its frame and lasts `frames`.
        self.cursor_keys = []
        x, y = self.editor_w * 0.55, self.editor_h * 0.6
        self.cursor_keys.append((0, x, y))
        for e in self.events:
            if e["type"] == "move":
                self.cursor_keys.append((e["frame"], x, y))
                x, y = e["x"], e["y"]
                self.cursor_keys.append((e["frame"] + e["frames"], x, y))
            elif e["type"] == "drop":
                r = e["rect"]
                tx, ty = r[0] + r[2] * 0.3, r[1] + r[3] * 0.45
                self.cursor_keys.append((e["frame"], self.editor_w * 0.92, self.editor_h * 0.95))
                self.cursor_keys.append((e["frame"] + int(e["frames"] * 0.8), tx, ty))
                x, y = tx, ty
            elif e["type"] == "menu":
                r = e["rect"]
                row = r[3] + 4
                ix, iy = r[0] + r[2] * 0.5, r[1] + r[3] + 6 + row * (e["chosen"] + 0.5)
                self.cursor_keys.append((e["frame"] + int(e["frames"] * 0.25), x, y))
                self.cursor_keys.append((e["frame"] + int(e["frames"] * 0.7), ix, iy))
                self.cursor_keys.append((e["frame"] + e["frames"] + 6, ix, iy))
                self.cursor_keys.append((e["frame"] + e["frames"] + 16, x, y))
            elif e["type"] == "dawdrag":
                r = e["rect"]
                bx, by = r[0] + r[2] / 2, r[1] + r[3] / 2
                index = sum(1 for o in self.events if o["type"] == "dawdrag" and o["frame"] < e["frame"])
                tx, ty = effects.daw_drop_point(index, self.editor_w, self.editor_h)
                self.cursor_keys.append((e["frame"] + 5, bx, by))
                self.cursor_keys.append((e["frame"] + int(e["frames"] * effects.DROP_AT), tx, ty))
                x, y = tx, ty
            elif e["type"] == "drag":
                (fx, fy), (tx, ty) = e["from"], e["to"]
                self.cursor_keys.append((e["frame"], fx, fy))
                self.cursor_keys.append((e["frame"] + e["frames"], tx, ty))
                x, y = tx, ty
        self.cursor_keys.sort(key=lambda k: k[0])

    def cursor(self, frame):
        keys = self.cursor_keys
        prev = keys[0]
        for k in keys:
            if k[0] > frame:
                if k[0] == prev[0]:
                    return k[1], k[2]
                t = ease((frame - prev[0]) / (k[0] - prev[0]))
                return prev[1] + (k[1] - prev[1]) * t, prev[2] + (k[2] - prev[2]) * t
            prev = k
        return prev[1], prev[2]

    def state_at(self, frame):
        """Persistent overlay state (caption, chapter, title, highlight, zoom, badge) at a frame."""
        s = {"caption": None, "caption_frame": 0, "chapter": None, "title": None, "title_frame": 0,
             "title_off_frame": None, "highlight": None, "highlight_frame": 0, "zoom": None, "zoom_frame": 0,
             "zoom_prev": None, "badge": None, "cursor_visible": True, "chapter_frame": -999}
        for e in self.events:
            if e["frame"] > frame:
                break
            t = e["type"]
            if t == "caption":
                s["caption"], s["caption_frame"] = e["text"], e["frame"]
            elif t == "caption-clear":
                s["caption"] = None
            elif t == "chapter":
                s["chapter"], s["chapter_frame"] = e["text"], e["frame"]
            elif t == "title":
                s["title"], s["title_frame"], s["title_off_frame"] = e["text"], e["frame"], None
            elif t == "title-clear":
                s["title_off_frame"] = e["frame"]
            elif t == "highlight":
                s["highlight"], s["highlight_frame"] = e["rect"], e["frame"]
            elif t == "highlight-off":
                s["highlight"] = None
            elif t == "zoom":
                s["zoom_prev"] = s["zoom"]
                s["zoom"], s["zoom_frame"] = (e["rect"], e["amount"], e.get("anchor", "center")), e["frame"]
            elif t == "zoom-off":
                s["zoom_prev"] = s["zoom"]
                s["zoom"], s["zoom_frame"] = None, e["frame"]
            elif t == "badge":
                s["badge"] = e["text"]
            elif t == "badge-clear":
                s["badge"] = None
            elif t == "cursor-hide":
                s["cursor_visible"] = False
            elif t == "move":
                s["cursor_visible"] = True
        return s

    def active(self, frame, kind, length_key="frames", default_len=12):
        out = []
        for e in self.events:
            if e["frame"] > frame:
                break
            if e["type"] == kind:
                length = e.get(length_key, default_len)
                if frame < e["frame"] + length:
                    out.append(e)
        return out


# ---- drawing ---------------------------------------------------------------------------------
def draw_cursor(img, x, y, pressed=False):
    d = ImageDraw.Draw(img)
    s = 1.55
    pts = [(0, 0), (0, 17), (4, 13), (7, 20), (10, 19), (7, 12.5), (12.5, 12.5)]
    poly = [(x + px * s, y + py * s) for px, py in pts]
    shadow = [(px + 2, py + 3) for px, py in poly]
    d.polygon(shadow, fill=(0, 0, 0, 90))
    d.polygon(poly, fill=(255, 255, 255, 255) if not pressed else (255, 225, 225, 255), outline=(10, 10, 10, 255))
    d.line(poly + [poly[0]], fill=(10, 10, 10, 255), width=2)


def draw_click(overlay, e, frame):
    k = (frame - e["frame"]) / 12.0
    r = e["rect"]
    cx, cy = r[0] + r[2] / 2, r[1] + r[3] / 2
    d = ImageDraw.Draw(overlay)
    rad = 10 + 34 * ease_out(k)
    alpha = int(200 * (1 - k))
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=ACCENT + (alpha,), width=4)
    # button flash
    d.rounded_rectangle([r[0] - 3, r[1] - 3, r[0] + r[2] + 3, r[1] + r[3] + 3], radius=6,
                        outline=(255, 255, 255, int(160 * (1 - k))), width=3)


def draw_menu(overlay, e, frame):
    r = e["rect"]
    k = (frame - e["frame"]) / e["frames"]
    items = e["items"]
    row = r[3] + 4
    width = max(r[2], 220, max(F_MENU.getlength(t) for t in items) + 32)
    x0, y0 = r[0], r[1] + r[3] + 6
    h = row * len(items) + 8
    appear = ease_out(min(1.0, k / 0.15))
    d = ImageDraw.Draw(overlay)
    d.rounded_rectangle([x0 + 4, y0 + 6, x0 + width + 4, y0 + h * appear + 6], radius=6, fill=(0, 0, 0, 110))
    d.rounded_rectangle([x0, y0, x0 + width, y0 + h * appear], radius=6, fill=(28, 30, 35, 250),
                        outline=(90, 94, 102, 255), width=1)
    if appear < 0.95:
        return
    hover = e["current"] if k < 0.55 else e["chosen"]
    if k >= 0.45 and k < 0.55:
        hover = -1
    for i, text in enumerate(items):
        iy = y0 + 4 + i * row
        if i == hover:
            fill = ACCENT + (255,) if k >= 0.75 and i == e["chosen"] else (70, 74, 82, 255)
            d.rounded_rectangle([x0 + 4, iy, x0 + width - 4, iy + row - 2], radius=4, fill=fill)
        d.text((x0 + 14, iy + row / 2 - 1), text, font=F_MENU, fill=INK + (255,), anchor="lm")


def draw_drop(overlay, e, frame, cursor_xy):
    k = (frame - e["frame"]) / e["frames"]
    r = e["rect"]
    d = ImageDraw.Draw(overlay)
    # drop zone glow
    glow = int(200 * ease(min(1.0, k / 0.5)))
    d.rounded_rectangle([r[0] + 2, r[1] + 2, r[0] + r[2] - 2, r[1] + r[3] - 2], radius=8,
                        outline=ACCENT + (glow,), width=4)
    if k > 0.85:
        return
    x, y = cursor_xy
    label = e["name"]
    tw = d.textlength(label, font=F_FILE)
    box = [x + 18, y + 22, x + 18 + 58 + tw + 20, y + 22 + 54]
    d.rounded_rectangle([box[0] + 4, box[1] + 5, box[2] + 4, box[3] + 5], radius=8, fill=(0, 0, 0, 120))
    d.rounded_rectangle(box, radius=8, fill=(36, 38, 44, 245), outline=(120, 124, 132, 255), width=1)
    # waveform icon
    ix = box[0] + 14
    for j, hgt in enumerate([10, 22, 34, 18, 28, 12, 24]):
        cx = ix + j * 5
        d.line([cx, box[1] + 27 - hgt / 2, cx, box[1] + 27 + hgt / 2], fill=ACCENT + (255,), width=3)
    d.text((box[0] + 58, box[1] + 27), label, font=F_FILE, fill=INK + (255,), anchor="lm")


def draw_highlight(overlay, rect, frame, since):
    k = (frame - since) / 30.0
    pulse = 0.65 + 0.35 * math.sin(k * math.pi * 1.2)
    alpha = int(230 * ease(min(1.0, (frame - since) / 8.0)) * pulse)
    d = ImageDraw.Draw(overlay)
    pad = 6
    d.rounded_rectangle([rect[0] - pad, rect[1] - pad, rect[0] + rect[2] + pad, rect[1] + rect[3] + pad],
                        radius=10, outline=(255, 214, 90, alpha), width=5)


def draw_bar(canvas, state, frame):
    d = ImageDraw.Draw(canvas)
    y0 = H - BAR_H
    d.rectangle([0, y0, W, H], fill=BAR_BG)
    d.rectangle([0, y0, W, y0 + 2], fill=(44, 46, 52))
    mark = effects.logo(52, INK)
    canvas.alpha_composite(mark, (W - 48 - mark.width, y0 + (BAR_H - mark.height) // 2))
    right = W - 48 - mark.width - 28
    if state["chapter"]:
        text = state["chapter"][0].upper()
        tw = d.textlength(text, font=F_CHAPTER)
        d.rounded_rectangle([right - tw - 32, y0 + 38, right, y0 + 82], radius=8, outline=ACCENT, width=2)
        d.text((right - tw / 2 - 16, y0 + 60), text, font=F_CHAPTER, fill=ACCENT, anchor="mm")
    cap = state["caption"]
    if cap:
        k = ease_out((frame - state["caption_frame"]) / 9.0)
        alpha = int(255 * k)
        shift = (1 - k) * 14
        d.rectangle([48, y0 + 30, 54, y0 + BAR_H - 30], fill=ACCENT)
        layer = Image.new("RGBA", (W - 520, BAR_H), (0, 0, 0, 0))
        ld = ImageDraw.Draw(layer)
        if len(cap) > 1:
            ld.text((0, 36 + shift), cap[0], font=F_CAPTION, fill=INK + (alpha,), anchor="lm")
            ld.text((0, 84 + shift), cap[1], font=F_CAPTION_SMALL, fill=INK_SOFT + (alpha,), anchor="lm")
        else:
            ld.text((0, BAR_H / 2 + shift), cap[0], font=F_CAPTION, fill=INK + (alpha,), anchor="lm")
        canvas.paste(layer, (76, y0), layer)


def draw_badge(canvas, text):
    d = ImageDraw.Draw(canvas)
    label = text[0]
    tw = d.textlength(label, font=F_BADGE)
    x1 = W - 40
    x0 = x1 - tw - 64
    y0, y1 = 30, 78
    d.rounded_rectangle([x0, y0, x1, y1], radius=24, fill=(16, 17, 20, 235), outline=ACCENT, width=2)
    d.ellipse([x0 + 18, (y0 + y1) / 2 - 7, x0 + 32, (y0 + y1) / 2 + 7], fill=ACCENT)
    d.text((x0 + 44, (y0 + y1) / 2), label, font=F_BADGE, fill=INK, anchor="lm")


_logo_cache = {}


def title_card(text, alpha, frame_local):
    card = Image.new("RGBA", (W, H), (12, 13, 15, 255))
    d = ImageDraw.Draw(card)
    # subtle grid of steps, like the pattern editor
    for i in range(32):
        x = 160 + i * 50
        on = (i * 7 + 3) % 5 in (0, 2) or i % 8 == 0
        shade = 34 if not on else 58
        d.rounded_rectangle([x, 820, x + 40, 860], radius=5, fill=(shade, shade + 2, shade + 6))
    head = (frame_local // 4) % 32
    d.rounded_rectangle([160 + head * 50, 820, 200 + head * 50, 860], radius=5, fill=ACCENT)
    main = text[0]
    d.text((W / 2, 420), main, font=F_TITLE, fill=INK, anchor="mm")
    tw = d.textlength(main, font=F_TITLE)
    d.rectangle([W / 2 - tw / 2, 492, W / 2 - tw / 2 + 120, 498], fill=ACCENT)
    if len(text) > 1:
        d.text((W / 2, 570), text[1], font=F_TITLE_SUB, fill=INK, anchor="mm")
    if len(text) > 2:
        d.text((W / 2, 640), text[2], font=F_TITLE_SMALL, fill=INK_SOFT, anchor="mm")
    card.putalpha(int(255 * alpha))
    return card


# ---- audio -----------------------------------------------------------------------------------
def read_wav(path):
    """WAV reader for 16/24-bit PCM and 32-bit float (the recorder writes float)."""
    import numpy as np
    data = Path(path).read_bytes()
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 8], "little")
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = (int.from_bytes(body[0:2], "little"), int.from_bytes(body[2:4], "little"),
                   int.from_bytes(body[4:8], "little"), int.from_bytes(body[14:16], "little"))
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, bits = fmt
    if tag == 0xFFFE:  # extensible: the sub-format decides
        tag = 3 if bits == 32 else 1
    if tag == 3:
        x = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(raw, dtype="<i2") / 32768.0
    else:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(v >= 2 ** 23, v - 2 ** 24, v) / 2 ** 23
    return x.reshape(-1, channels), rate


def master_audio(src, dst):
    """Evens out the level between sections (Boom Bap and Trap differ by ~12 dB), then a peak
    limiter, so the video plays at one comfortable loudness. Writes 16-bit PCM for ffmpeg."""
    import numpy as np
    x, rate = read_wav(src)
    hop = rate // 10
    n = len(x) // hop
    frames = x[: n * hop].reshape(n, hop, -1)
    rms = np.sqrt((frames ** 2).mean(axis=(1, 2)) + 1e-12)
    target = 10 ** (-17 / 20)
    window = 30  # 3 s
    gain = np.ones(n)
    last = 1.0
    for i in range(n):
        seg = rms[max(0, i - window // 2): i + window // 2 + 1]
        loud = seg[seg > 0.004]
        if len(loud) >= 5:
            last = np.clip(target / np.sqrt((loud ** 2).mean()), 10 ** (-12 / 20), 10 ** (12 / 20))
        gain[i] = last
    # smooth the gain curve (~1.5 s) so changes glide
    k = np.hanning(31)
    k /= k.sum()
    gain = np.convolve(np.pad(gain, 15, mode="edge"), k, mode="valid")
    g = np.interp(np.arange(len(x)), np.arange(n) * hop + hop / 2, gain)
    y = x * g[:, None]
    # look-ahead peak limiter at -1 dBFS
    ceiling = 10 ** (-1 / 20)
    peak = np.abs(y).max(axis=1)
    look = int(0.005 * rate)
    need = np.minimum(1.0, ceiling / np.maximum(peak, 1e-9))
    # running minimum over the look-ahead window, then release smoothing
    from numpy.lib.stride_tricks import sliding_window_view
    padded = np.pad(need, (0, look), constant_values=1.0)
    env = sliding_window_view(padded, look + 1).min(axis=1)
    rel = np.exp(-1.0 / (0.08 * rate))
    out = np.empty_like(env)
    cur = 1.0
    for i, v in enumerate(env):
        cur = v if v < cur else v + (cur - v) * rel
        out[i] = cur
    y = y * out[:, None]
    pcm = (np.clip(y, -1, 1) * 32767).astype("<i2")
    import wave
    with wave.open(str(dst), "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


# ---- main ------------------------------------------------------------------------------------
def zoom_params(state, frame, tl):
    """(cx, cy, factor) for the editor layer."""
    def target(z):
        if z is None:
            return tl.editor_w / 2, tl.editor_h / 2, 1.0
        r, amount, anchor = z
        cx = r[0] + r[2] / 2
        if anchor == "left":
            cx = r[0] + tl.editor_w / amount / 2 - 12
        return cx, r[1] + r[3] / 2, amount

    k = ease((frame - state["zoom_frame"]) / 24.0)
    a = target(state["zoom_prev"])
    b = target(state["zoom"])
    return tuple(a[i] + (b[i] - a[i]) * k for i in range(3))


def apply_zoom(layer, cx, cy, factor, tl):
    """Returns the zoomed layer and a function mapping editor points to screen points."""
    if factor <= 1.001:
        return layer, (lambda x, y: (x, y))
    w, h = tl.editor_w / factor, tl.editor_h / factor
    x0 = min(max(0, cx - w / 2), tl.editor_w - w)
    y0 = min(max(0, cy - h / 2), tl.editor_h - h)
    zoomed = layer.transform((tl.editor_w, tl.editor_h), Image.Transform.EXTENT, (x0, y0, x0 + w, y0 + h),
                             resample=Image.Resampling.BICUBIC)
    return zoomed, (lambda x, y: ((x - x0) * factor, (y - y0) * factor))


def main():
    rec = Path(sys.argv[1])
    out = Path(sys.argv[2])
    step = 1
    stills = None
    if "--stills" in sys.argv:
        stills = [int(v) for v in sys.argv[sys.argv.index("--stills") + 1].split(",")]
    if "--preview" in sys.argv:
        step = int(sys.argv[sys.argv.index("--preview") + 1])
    frames = (rec / "frames.txt").read_text().split()
    events = [json.loads(l) for l in (rec / "timeline.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
    tl = Timeline(events, len(frames))
    fps = 30
    total = len(frames)
    camera = effects.Camera(events, total, tl.editor_w, tl.editor_h)
    first_title = next((e["frame"] for e in events if e["type"] == "title"), -1)

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [ffmpeg, "-y", "-loglevel", "error",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(fps / step), "-i", "-"]
    if step == 1 and stills is None:
        dur = total / fps
        mastered = rec / "audio_master.wav"
        print("mastering audio...", flush=True)
        master_audio(rec / "audio.wav", mastered)
        cmd += ["-i", str(mastered),
                "-af", f"afade=t=in:d=0.4,afade=t=out:st={dur - 1.5:.2f}:d=1.5",
                "-c:a", "aac", "-b:a", "320k"]
    cmd += ["-c:v", "libx264", "-preset", "medium", "-crf", "17", "-pix_fmt", "yuv420p",
            "-movflags", "+faststart", "-shortest" if step == 1 else "-an", str(out)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE) if stills is None else None

    cache_name, cache_img = None, None
    for frame in (stills if stills is not None else range(0, total, step)):
        name = frames[frame]
        if name != cache_name:
            img = Image.open(rec / "frames" / name).convert("RGB")
            if img.size != (tl.editor_w, tl.editor_h):
                img = img.resize((tl.editor_w, tl.editor_h), Image.Resampling.LANCZOS)
            cache_name, cache_img = name, img
        state = tl.state_at(frame)

        layer = cache_img.convert("RGBA")
        overlay = Image.new("RGBA", layer.size, (0, 0, 0, 0))
        if state["highlight"]:
            draw_highlight(overlay, state["highlight"], frame, state["highlight_frame"])
        for e in tl.active(frame, "menu"):
            draw_menu(overlay, e, frame)
        for e in tl.active(frame, "click", default_len=12):
            draw_click(overlay, e, frame)
        cxy = tl.cursor(frame)
        drops = tl.active(frame, "drop")
        for e in drops:
            draw_drop(overlay, e, frame, cxy)
        dawdrags = tl.active(frame, "dawdrag")
        if state["cursor_visible"] and not dawdrags:
            pressed = bool(tl.active(frame, "click", default_len=5)) or bool(tl.active(frame, "drag"))
            draw_cursor(overlay, cxy[0], cxy[1], pressed)
        layer.alpha_composite(overlay)
        cx, cy, factor = zoom_params(state, frame, tl)
        explicit = state["zoom"] is not None or (frame - state["zoom_frame"]) < 26
        if not explicit:
            cx, cy, factor = camera.at(frame)
        layer, to_screen = apply_zoom(layer, cx, cy, factor, tl)

        canvas = Image.new("RGBA", (W, H), BAR_BG + (255,))
        ox = (W - tl.editor_w) // 2
        canvas.paste(layer, (ox, 0))
        for start, text, p in tl.notes:
            if start <= frame < start + effects.NOTE_FRAMES:
                sx, sy = to_screen(*p)
                effects.draw_note(canvas, text, sx + ox, sy, frame - start)
        for e in dawdrags:
            earlier = [o["name"] for o in events if o["type"] == "dawdrag" and o["frame"] < e["frame"]]
            effects.draw_daw(canvas, e, frame - e["frame"], earlier, (cxy[0] + ox, cxy[1]), tl.editor_h)
            local = frame - e["frame"]
            draw_cursor(canvas, cxy[0] + ox, cxy[1], 3 < local < e["frames"] * effects.DROP_AT)
        draw_bar(canvas, state, frame)
        if state["badge"]:
            draw_badge(canvas, state["badge"])
        if state["chapter"]:
            effects.chapter_splash(canvas, state["chapter"][0], frame - state["chapter_frame"], tl.editor_h)

        if state["title"]:
            local = frame - state["title_frame"]
            alpha = ease(local / 10.0)
            if state["title_off_frame"] is not None:
                alpha *= 1 - ease((frame - state["title_off_frame"]) / 12.0)
            if alpha > 0.002:
                card = effects.title_card((W, H), state["title"], alpha, local, state["title_frame"] == first_title)
                canvas.alpha_composite(card)

        if stills is not None:
            canvas.convert("RGB").save(out.parent / f"still_{frame:06d}.png")
            continue
        proc.stdin.write(canvas.convert("RGB").tobytes())
        if frame % (300 * step) == 0:
            print(f"{frame}/{total}", flush=True)
    if proc is not None:
        proc.stdin.close()
        proc.wait()
    print("done", out)


if __name__ == "__main__":
    main()

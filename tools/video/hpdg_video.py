"""HPDG promo-video pipeline: screen recording of FL Studio -> short YouTube video.

One JSON job file describes a video; subcommands do one step each:

  inspect  <rec.mp4> <dir>       contact sheets (every N s) + loudness per 5 s
  build    <job.json>            horizontal 1920x1080 cut: intro, segments, outro
  pauses   <voice.wav>           silence gaps of a voice-over (to cut it into lines)
  voice    <job.json>            place voice-over lines on the cut, duck the music
  vertical <job.json>            9:16 Shorts version (crops stacked, big captions)
  thumb    <job.json>            1280x720 thumbnail (style "bold" or "plain")
  frame    <rec.mp4> <sec> <out.png>   grab one full frame (for crops / thumbnails)

Paths in the job are absolute or relative to the job file. See
.claude/skills/hpdg-video/SKILL.md for the workflow and the job format.
"""
import json, math, os, subprocess, sys, wave

import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

try:
    import imageio_ffmpeg
    FF = imageio_ffmpeg.get_ffmpeg_exe()
except ImportError:
    FF = "ffmpeg"

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
KIT = os.path.join(ROOT, "Releases", "HPDG_presentation_kit")
LOGO = os.path.join(KIT, "logo", "logo_red_1024.png")
FONT_B = r"C:/Windows/Fonts/segoeuib.ttf"
FONT_L = r"C:/Windows/Fonts/segoeuil.ttf"
IMPACT = r"C:/Windows/Fonts/impact.ttf"
RED = (226, 62, 62)
RED2 = (240, 48, 40)
YEL = (255, 214, 0)
BG = (14, 14, 17)
FPS = 30
SR = 48000


def run(args, quiet=True):
    r = subprocess.run([FF, "-hide_banner", "-v", "error", "-y"] + args)
    if r.returncode:
        sys.exit("ffmpeg failed: " + " ".join(map(str, args)))


def load_job(path):
    with open(path, encoding="utf-8") as f:
        job = json.load(f)
    base = os.path.dirname(os.path.abspath(path))
    job["_base"] = base
    job["_work"] = os.path.join(os.environ.get("TEMP", base), "hpdg_video",
                                os.path.splitext(os.path.basename(path))[0])
    os.makedirs(job["_work"], exist_ok=True)
    return job


def P(job, p):
    return p if os.path.isabs(p) else os.path.normpath(os.path.join(job["_base"], p))


def ease(t):
    t = max(0.0, min(1.0, t))
    return 1 - (1 - t) ** 3


# ---------------------------------------------------------------- inspect
def cmd_inspect(rec, out_dir, step="4"):
    os.makedirs(out_dir, exist_ok=True)
    step = float(step)
    run(["-i", rec, "-vf", f"fps=1/{step},crop=1080:690:200:0,scale=384:-1",
         "-q:v", "4", os.path.join(out_dir, "f_%03d.jpg")])
    frames = sorted(f for f in os.listdir(out_dir) if f.startswith("f_"))
    for k in range(0, len(frames), 20):
        ims = [Image.open(os.path.join(out_dir, f)) for f in frames[k:k + 20]]
        sheet = Image.new("RGB", (384 * 5, ims[0].height * 4))
        for i, im in enumerate(ims):
            d = ImageDraw.Draw(im)
            d.rectangle((0, 0, 90, 34), fill="black")
            d.text((4, 2), f"{int((k + i) * step)}s", fill="yellow",
                   font=ImageFont.truetype(FONT_B, 26))
            sheet.paste(im, ((i % 5) * 384, (i // 5) * im.height))
        sheet.save(os.path.join(out_dir, f"sheet_{k // 20:02d}.jpg"))
    r = subprocess.run([FF, "-hide_banner", "-i", rec, "-vn", "-af",
                        "asetnsamples=240000,astats=metadata=1:reset=1,"
                        "ametadata=print:key=lavfi.astats.Overall.RMS_level", "-f", "null", "-"],
                       capture_output=True, text=True)
    vals = [l.split("=")[-1] for l in r.stderr.splitlines() if "RMS_level=" in l]
    print("loudness dB per 5 s (-inf = silence):")
    print(" ".join(f"{i * 5}:{v.split('.')[0]}" for i, v in enumerate(vals)))
    print("sheets:", out_dir)


def cmd_frame(rec, sec, out):
    run(["-ss", str(sec), "-i", rec, "-frames:v", "1", out])
    print(out)


# ---------------------------------------------------------------- horizontal build
class Cut:
    W, H = 1920, 1080

    def __init__(self, job):
        self.job = job
        self.src = P(job, job["src"])
        self.work = job["_work"]
        cx, cy, cw, ch = job.get("crop", [200, 0, 1080, 690])
        self.crop = (cx, cy, cw, ch)
        self.VW = round(cw * self.H / ch / 2) * 2
        self.VX = (self.W - self.VW) // 2
        self.bed = job.get("bed")
        self.bed_pos = 0.0

    def background(self):
        W, H, VX, VW = self.W, self.H, self.VX, self.VW
        img = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(img)
        for x in range(VX):
            t = x / VX
            c = tuple(int(BG[i] + (28 - BG[i]) * t * 0.6) for i in range(3))
            d.line([(x, 0), (x, H)], fill=c)
            d.line([(W - 1 - x, 0), (W - 1 - x, H)], fill=c)
        d.line([(VX - 2, 0), (VX - 2, H)], fill=(90, 28, 28), width=2)
        d.line([(VX + VW + 1, 0), (VX + VW + 1, H)], fill=(90, 28, 28), width=2)
        p = os.path.join(self.work, "bg.png")
        img.save(p)
        return p

    def label(self, i, text, badge):
        W, H, VX, VW = self.W, self.H, self.VX, self.VW
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        f = ImageFont.truetype(FONT_B, 34)
        tw = d.textlength(text, font=f)
        x0, y1, h = VX + 28, H - 26, 62
        d.rounded_rectangle((x0, y1 - h, x0 + tw + 58, y1), 12, fill=(12, 12, 15, 225))
        d.rectangle((x0, y1 - h + 10, x0 + 6, y1 - 10), fill=RED)
        d.text((x0 + 30, y1 - h / 2), text, font=f, fill=(245, 245, 245), anchor="lm")
        if badge:
            fb = ImageFont.truetype(FONT_B, 32)
            bw = d.textlength(badge, font=fb)
            bx1 = VX + VW - 28
            d.rounded_rectangle((bx1 - bw - 96, y1 - h, bx1, y1), 12, fill=RED + (235,))
            ax, cy = bx1 - bw - 74, y1 - h / 2
            for k in range(2):
                x = ax + k * 20
                d.polygon([(x, cy - 12), (x, cy + 12), (x + 20, cy)], fill="white")
            d.text((bx1 - 18 - bw, cy), badge, font=fb, fill="white", anchor="lm")
        p = os.path.join(self.work, f"label_{i:02d}.png")
        img.save(p)
        return p

    def segment(self, i, s, e, speed, text, bg):
        badge = f"x{round(speed)}" if speed > 1 else None
        lab = self.label(i, text, badge)
        out = os.path.join(self.work, f"seg_{i:02d}.mp4")
        dur = (e - s) / speed
        cx, cy, cw, ch = self.crop
        vf = (f"[1:v]crop={cw}:{ch}:{cx}:{cy},setpts=(PTS-STARTPTS)/{speed},fps={FPS},"
              f"scale={self.VW}:{self.H}:flags=lanczos,setsar=1[v];"
              f"[0:v][v]overlay={self.VX}:0:shortest=1[b];"
              f"[2:v]format=rgba,fade=in:st=0.15:d=0.35:alpha=1[l];"
              f"[b][l]overlay=0:0:shortest=1,format=yuv420p,trim=duration={dur:.3f}[vo]")
        if speed == 1:
            af = (f"[1:a]aresample={SR},afade=in:st=0:d=0.12,"
                  f"afade=out:st={dur - 0.15:.3f}:d=0.15,atrim=duration={dur:.3f}[ao]")
            fc, extra = vf + ";" + af, []
        elif self.bed is not None:
            # sped-up part: the finished beat (from `bed` seconds in the recording) plays under it
            af = (f"[3:a]aresample={SR},afade=in:st=0:d=0.08,"
                  f"afade=out:st={dur - 0.1:.3f}:d=0.1,atrim=duration={dur:.3f}[ao]")
            fc = vf + ";" + af
            extra = ["-ss", f"{self.bed + self.bed_pos:.3f}", "-t", f"{dur + 0.5:.3f}", "-i", self.src]
            self.bed_pos += dur
        else:
            fc, extra = None, ["-f", "lavfi", "-t", f"{dur:.3f}", "-i", f"anullsrc=r={SR}:cl=stereo"]
        amap = (["-filter_complex", fc, "-map", "[vo]", "-map", "[ao]"] if fc else
                ["-filter_complex", vf, "-map", "[vo]", "-map", "3:a"])
        run(["-loop", "1", "-framerate", str(FPS), "-i", bg,
             "-ss", f"{s}", "-t", f"{e - s}", "-i", self.src,
             "-loop", "1", "-framerate", str(FPS), "-i", lab] + extra + amap +
            ["-t", f"{dur:.3f}", "-r", str(FPS), "-c:v", "libx264", "-preset", "slow", "-crf", "17",
             "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-ar", str(SR), "-ac", "2", out])
        return out, dur

    def title_card(self, name, seconds, title, sub):
        W, H = self.W, self.H
        logo = Image.open(LOGO).convert("RGBA")
        lh = 420
        logo = logo.resize((int(logo.width * lh / logo.height), lh), Image.LANCZOS)
        glow = Image.new("RGBA", (logo.width + 200, logo.height + 200), (0, 0, 0, 0))
        glow.paste(logo, (100, 100), logo)
        glow = glow.filter(ImageFilter.GaussianBlur(40))
        fdir = os.path.join(self.work, name)
        os.makedirs(fdir, exist_ok=True)
        lines = [(title, ImageFont.truetype(FONT_B, 120), (245, 245, 245), 680),
                 (sub, ImageFont.truetype(FONT_L, 44), (200, 200, 205), 840)]
        n = int(seconds * FPS)
        for k in range(n):
            t = k / FPS
            img = Image.new("RGBA", (W, H), BG + (255,))
            d = ImageDraw.Draw(img)
            a_logo = ease(t / 0.9)
            sc = 0.82 + 0.18 * ease(t / 1.1)
            lg = logo.resize((int(logo.width * sc), int(logo.height * sc)), Image.LANCZOS)
            gl = glow.resize((int(glow.width * sc), int(glow.height * sc)), Image.LANCZOS)
            pulse = 0.55 + 0.25 * math.sin(t * 2.2)
            gl.putalpha(gl.getchannel("A").point(lambda v: int(v * a_logo * pulse)))
            img.alpha_composite(gl, (W // 2 - gl.width // 2, 360 - gl.height // 2))
            lg.putalpha(lg.getchannel("A").point(lambda v: int(v * a_logo)))
            img.alpha_composite(lg, (W // 2 - lg.width // 2, 360 - lg.height // 2))
            for j, (txt, font, col, y) in enumerate(lines):
                a = ease((t - 0.7 - j * 0.25) / 0.6)
                if a <= 0:
                    continue
                layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
                ImageDraw.Draw(layer).text((W // 2, y + 30 * (1 - a)), txt, font=font,
                                           fill=col + (int(255 * a),), anchor="mm")
                img.alpha_composite(layer)
            la2 = ease((t - 0.9) / 0.8)
            if la2 > 0:
                half = int(260 * la2)
                d.rectangle((W // 2 - half, 778, W // 2 + half, 781), fill=RED + (255,))
            if t > seconds - 0.5:
                img = Image.blend(img, Image.new("RGBA", (W, H), (0, 0, 0, 255)),
                                  (t - (seconds - 0.5)) / 0.5)
            img.convert("RGB").save(os.path.join(fdir, f"f_{k:04d}.png"))
        out = os.path.join(self.work, f"{name}.mp4")
        run(["-framerate", str(FPS), "-i", os.path.join(fdir, "f_%04d.png"),
             "-f", "lavfi", "-t", f"{seconds}", "-i", f"anullsrc=r={SR}:cl=stereo",
             "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-pix_fmt", "yuv420p",
             "-c:a", "aac", "-b:a", "192k", "-shortest", out])
        return out, n / FPS


def cmd_build(job_path):
    job = load_job(job_path)
    c = Cut(job)
    bg = c.background()
    parts = [c.title_card("intro", job.get("intro", 4.0), job.get("intro_title", "HPDG"),
                          job.get("intro_sub", "HamloProd Drum Generator"))]
    names = ["intro"]
    for i, (s, e, speed, text) in enumerate(job["segments"]):
        parts.append(c.segment(i, s, e, speed, text, bg))
        names.append(text)
        print("segment", i, "done", flush=True)
    parts.append(c.title_card("outro", job.get("outro", 4.5), job.get("outro_title", "HPDG"),
                              job.get("outro_sub", "hamloprod.org")))
    names.append("outro")
    lst = os.path.join(c.work, "list.txt")
    with open(lst, "w", encoding="utf-8") as f:
        for p, _ in parts:
            f.write(f"file '{p.replace(os.sep, '/')}'\n")
    out = P(job, job["out"])
    run(["-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", "-movflags", "+faststart", out])
    t = 0.0
    print("timeline of the cut (use it to place voice lines and chapters):")
    for (_, d), n in zip(parts, names):
        print(f"  {int(t // 60)}:{t % 60:05.2f}  ({d:5.2f}s)  {n}")
        t += d
    print(f"total {t:.2f}s -> {out}")


# ---------------------------------------------------------------- voice-over
def cmd_pauses(wav_path, noise="-38", dur="0.3"):
    r = subprocess.run([FF, "-hide_banner", "-i", wav_path, "-af",
                        f"silencedetect=noise={noise}dB:d={dur}", "-f", "null", "-"],
                       capture_output=True, text=True)
    starts = [float(l.split("silence_start: ")[1]) for l in r.stderr.splitlines() if "silence_start" in l]
    ends = [float(l.split("silence_end: ")[1].split()[0]) for l in r.stderr.splitlines() if "silence_end" in l]
    total = [l for l in r.stderr.splitlines() if "Duration" in l]
    print(total[0].strip() if total else "")
    print("pauses (start-end, length):")
    for s, e in zip(starts, ends):
        print(f"  {s:6.2f}-{e:6.2f} ({e - s:.2f})")
    print("speech between pauses = one line; long pauses (>0.6 s) are usually paragraph breaks")


def cmd_voice(job_path):
    job = load_job(job_path)
    v = job["voice"]
    video = P(job, v.get("video", job["out"]))
    out = P(job, v["out"])
    work = job["_work"]
    v48 = os.path.join(work, "voice48.wav")
    run(["-i", P(job, v["file"]), "-ar", str(SR), "-ac", "1", "-c:a", "pcm_s16le", v48])
    with wave.open(v48) as w:
        src = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768
    r = subprocess.run([FF, "-hide_banner", "-i", video], capture_output=True, text=True)
    hms = [l for l in r.stderr.splitlines() if "Duration" in l][0].split("Duration: ")[1].split(",")[0]
    h, m, s = hms.split(":")
    total = int(h) * 3600 + int(m) * 60 + float(s)
    track = np.zeros(int(total * SR) + SR, np.float32)
    fade = int(0.03 * SR)
    for vs, ve, at, name in v["clips"]:
        a, b = int(max(0, vs - 0.05) * SR), int(min(len(src) / SR, ve + 0.12) * SR)
        seg = src[a:b].copy()
        seg[:fade] *= np.linspace(0, 1, fade)
        seg[-fade:] *= np.linspace(1, 0, fade)
        o = int(at * SR)
        if o + len(seg) > int(total * SR):
            print(f"  WARNING: '{name}' runs past the end of the video")
        track[o:o + len(seg)] += seg[:len(track) - o]
        end = at + ve - vs
        print(f"  {int(at // 60)}:{at % 60:05.2f} - {int(end // 60)}:{end % 60:05.2f}  {name}")
    track *= 0.89 / max(1e-6, np.abs(track).max())
    vt = os.path.join(work, "voice_track.wav")
    with wave.open(vt, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((track * 32767).astype(np.int16).tobytes())
    fc = ("[1:a]aformat=channel_layouts=stereo,asplit=2[vs][vm];[0:a]volume=0.85[m];"
          "[m][vs]sidechaincompress=threshold=0.02:ratio=10:attack=15:release=350:makeup=1[duck];"
          "[duck][vm]amix=inputs=2:duration=first:dropout_transition=0,volume=2,alimiter=limit=0.95[a]")
    run(["-i", video, "-i", vt, "-filter_complex", fc, "-map", "0:v", "-map", "[a]",
         "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out])
    print("->", out)


# ---------------------------------------------------------------- vertical (Shorts)
def cmd_vertical(job_path):
    job = load_job(job_path)
    vj = job["vertical"]
    src = P(job, job["src"])
    work = os.path.join(job["_work"], "vertical")
    os.makedirs(work, exist_ok=True)
    W, H = 1080, 1920

    bgimg = Image.new("RGB", (W, H), (12, 12, 15))
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    ImageDraw.Draw(glow).ellipse((-200, 500, 1280, 1500), fill=(140, 22, 22))
    bgimg = Image.blend(bgimg, glow.filter(ImageFilter.GaussianBlur(220)), 0.5)
    d = ImageDraw.Draw(bgimg)
    logo = Image.open(LOGO).convert("RGBA")
    lh = 90
    sl = logo.resize((int(logo.width * lh / logo.height), lh), Image.LANCZOS)
    bgimg.paste(sl, (60, 70), sl)
    d.text((60 + sl.width + 20, 70 + lh / 2), "HPDG", font=ImageFont.truetype(FONT_B, 46),
           fill=(240, 240, 240), anchor="lm")
    d.text((W - 60, 70 + lh / 2), "hamloprod.org", font=ImageFont.truetype(FONT_B, 34),
           fill=(190, 190, 195), anchor="rm")
    bg = os.path.join(work, "bg.png")
    bgimg.save(bg)

    def card(name, lines, dur):
        img = bgimg.copy()
        dd = ImageDraw.Draw(img)
        lg = logo.resize((int(logo.width * 520 / logo.height), 520), Image.LANCZOS)
        img.paste(lg, ((W - lg.width) // 2, 520), lg)
        y = 1160
        for k, t in enumerate(lines):
            size = 150 if len(t) <= 10 else 100
            dd.text((W // 2, y), t, font=ImageFont.truetype(IMPACT, size),
                    fill=RED if k else (250, 250, 250), anchor="mt", stroke_width=4, stroke_fill=(0, 0, 0))
            y += int(size * 1.15)
        png = os.path.join(work, f"{name}.png")
        img.save(png)
        out = os.path.join(work, f"{name}.mp4")
        run(["-loop", "1", "-framerate", str(FPS), "-t", f"{dur}", "-i", png,
             "-f", "lavfi", "-t", f"{dur}", "-i", f"anullsrc=r={SR}:cl=stereo",
             "-vf", f"fade=in:st=0:d=0.3,fade=out:st={dur - 0.3}:d=0.3,format=yuv420p",
             "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-c:a", "aac", "-b:a", "192k",
             "-shortest", out])
        return out

    parts = [card("intro", vj.get("intro", ["HPDG", "DRUM GENERATOR"]), vj.get("intro_len", 2.5))]
    for i, (s, e, lines, boxes) in enumerate(vj["segments"]):
        cap = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        cd = ImageDraw.Draw(cap)
        y = 1490
        for k, t in enumerate(lines):
            cd.text((W // 2, y), t, font=ImageFont.truetype(IMPACT, 104), fill=RED if k else (250, 250, 250),
                    anchor="mt", stroke_width=5, stroke_fill=(0, 0, 0))
            y += 118
        capp = os.path.join(work, f"cap_{i}.png")
        cap.save(capp)
        dur = e - s
        n = len(boxes)
        heights = [(b[3] - b[1]) * 1000 / (b[2] - b[0]) for b in boxes]
        gap = 30
        k_s = min(1.0, (1220 - gap * (n - 1)) / sum(heights))
        y = 230 + (1220 - (sum(heights) * k_s + gap * (n - 1))) / 2
        chain = [f"[1:v]split={n}" + "".join(f"[c{k}]" for k in range(n))] if n > 1 else []
        last = "[0:v]"
        for k, b in enumerate(boxes):
            w = int(1000 * k_s) // 2 * 2
            h = int(heights[k] * k_s) // 2 * 2
            inp = f"[c{k}]" if n > 1 else "[1:v]"
            chain.append(f"{inp}crop={b[2]-b[0]}:{b[3]-b[1]}:{b[0]}:{b[1]},scale={w}:{h}:flags=lanczos,"
                         f"pad={w+8}:{h+8}:4:4:color=0xE83A34[p{k}]")
            chain.append(f"{last}[p{k}]overlay={(W - w - 8) // 2}:{int(y)}:shortest=1[o{k}]")
            last = f"[o{k}]"
            y += h + 8 + gap
        chain.append("[2:v]format=rgba,fade=in:st=0:d=0.25:alpha=1[cap]")
        chain.append(f"{last}[cap]overlay=0:0:shortest=1,fps={FPS},format=yuv420p,trim=duration={dur}[vo]")
        chain.append(f"[1:a]aresample={SR},afade=in:st=0:d=0.1,afade=out:st={dur - 0.12}:d=0.12[ao]")
        out = os.path.join(work, f"seg_{i}.mp4")
        run(["-loop", "1", "-framerate", str(FPS), "-i", bg, "-ss", f"{s}", "-t", f"{dur}", "-i", src,
             "-loop", "1", "-framerate", str(FPS), "-i", capp,
             "-filter_complex", ";".join(chain), "-map", "[vo]", "-map", "[ao]",
             "-t", f"{dur}", "-c:v", "libx264", "-preset", "slow", "-crf", "17",
             "-c:a", "aac", "-b:a", "192k", "-ar", str(SR), "-ac", "2", out])
        parts.append(out)
    parts.append(card("outro", vj.get("outro", ["HPDG", "HAMLOPROD.ORG"]), vj.get("outro_len", 2.5)))
    lst = os.path.join(work, "list.txt")
    with open(lst, "w") as f:
        for p in parts:
            f.write(f"file '{p.replace(os.sep, '/')}'\n")
    out = P(job, vj["out"])
    run(["-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", "-movflags", "+faststart", out])
    print("->", out)


# ---------------------------------------------------------------- thumbnails
def thumb_plain(frame, box, lines, tag, out):
    W, H = 1280, 720
    plug = Image.open(frame).convert("RGB").crop(box)
    plug = ImageEnhance.Sharpness(ImageEnhance.Contrast(plug).enhance(1.15)).enhance(1.6)
    ph = 500
    plug = plug.resize((int(plug.width * ph / plug.height), ph), Image.LANCZOS)
    img = Image.new("RGB", (W, H), (12, 12, 15))
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    ImageDraw.Draw(glow).ellipse((560, 60, 1400, 760), fill=(150, 25, 25))
    img = Image.blend(img, glow.filter(ImageFilter.GaussianBlur(160)), 0.55)
    px, py = W - plug.width + 30, (H - ph) // 2 + 40
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rectangle((px - 10, py + 10, px + plug.width + 20, py + ph + 30), fill=(0, 0, 0, 200))
    img.paste(sh.filter(ImageFilter.GaussianBlur(25)), (0, 0), sh.filter(ImageFilter.GaussianBlur(25)))
    img.paste(plug, (px, py))
    ImageDraw.Draw(img).rectangle((px - 3, py - 3, px + plug.width + 3, py + ph + 3), outline=RED, width=3)
    fade = Image.new("L", (W, H), 0)
    fd = ImageDraw.Draw(fade)
    for x in range(760):
        fd.line([(x, 0), (x, H)], fill=235 if x < 380 else int(235 * (1 - (x - 380) / 380) ** 1.4))
    img = Image.composite(Image.new("RGB", (W, H), (10, 10, 13)), img, fade)
    d = ImageDraw.Draw(img)
    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.resize((int(logo.width * 74 / logo.height), 74), Image.LANCZOS)
    img.paste(logo, (48, 40), logo)
    d.text((48 + logo.width + 16, 77), "HPDG", font=ImageFont.truetype(FONT_B, 34), fill=(240, 240, 240), anchor="lm")
    ft = ImageFont.truetype(FONT_B, 26)
    tw = d.textlength(tag, font=ft)
    d.rounded_rectangle((48, H - 92, 48 + tw + 36, H - 46), 10, fill=RED)
    d.text((66, H - 69), tag, font=ft, fill="white", anchor="lm")
    for size in range(170, 40, -2):
        f = ImageFont.truetype(IMPACT, size)
        if max(d.textlength(t, font=f) for t, _ in lines) <= 640 and size * 1.02 * len(lines) <= 430:
            break
    y = (H - size * len(lines)) // 2 + 6
    for t, c in lines:
        d.text((48, y), t, font=f, fill=RED if c == "r" else (250, 250, 250), stroke_width=4, stroke_fill=(0, 0, 0))
        y += size
    img.save(out)


def thumb_bold(frame, grid_box, top, bottom, sticker, out, wave_src=None, wave_at=0.0):
    W, H = 1280, 720
    img = Image.new("RGB", (W, H), (8, 6, 8))
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((520, -120, 1500, 860), fill=(190, 20, 18))
    gd.ellipse((-300, 300, 500, 1000), fill=(110, 10, 10))
    img = Image.blend(img, glow.filter(ImageFilter.GaussianBlur(170)), 0.75)
    grid = Image.open(frame).convert("RGB").crop(grid_box)
    grid = ImageEnhance.Sharpness(ImageEnhance.Brightness(ImageEnhance.Contrast(grid).enhance(1.5)).enhance(1.35)).enhance(2.0)
    grid = grid.resize((820, int(grid.height * 820 / grid.width)), Image.LANCZOS)
    framed = Image.new("RGBA", (grid.width + 16, grid.height + 16), RED2 + (255,))
    framed.paste(grid, (8, 8))
    rot = framed.rotate(-6, resample=Image.BICUBIC, expand=True)
    gx, gy = 500, 250
    for col, blur, off in (((0, 0, 0, 230), 28, (18, 26)), (RED2 + (255,), 30, (0, 0))):
        layer = Image.new("RGBA", rot.size, (0, 0, 0, 0))
        layer.paste(col, mask=rot.getchannel("A"))
        layer = layer.filter(ImageFilter.GaussianBlur(blur))
        img.paste(layer, (gx + off[0], gy + off[1]), layer)
    img.paste(rot, (gx, gy), rot)
    d = ImageDraw.Draw(img)
    if wave_src:
        wav = os.path.join(os.environ.get("TEMP", "."), "hpdg_thumb_wave.wav")
        run(["-ss", str(wave_at), "-t", "5", "-i", wave_src, "-ac", "1", "-ar", "8000", wav])
        with wave.open(wav) as w:
            a = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32)
        a /= np.abs(a).max() + 1
        cx0, cy0, cx1, cy1 = 40, 470, 470, 650
        card = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        cd = ImageDraw.Draw(card)
        cd.rounded_rectangle((cx0, cy0, cx1, cy1), 18, fill=(18, 18, 22, 240), outline=(255, 255, 255, 255), width=4)
        mid, n = (cy0 + cy1) / 2 + 12, cx1 - cx0 - 40
        step = max(1, len(a) // n)
        for i in range(n):
            v = float(np.abs(a[i * step:(i + 1) * step]).max()) if len(a[i * step:(i + 1) * step]) else 0
            cd.line([(cx0 + 20 + i, mid - 4 - v * 58), (cx0 + 20 + i, mid + 4 + v * 58)], fill=(255, 255, 255, 255))
        cd.text((cx0 + 22, cy0 + 12), "YOUR SAMPLE", font=ImageFont.truetype(FONT_B, 26), fill=YEL + (255,))
        img.paste(card, (0, 0), card)
        pts = [(470, 545), (560, 545), (560, 510), (640, 575), (560, 640), (560, 605), (470, 605)]
        d.polygon(pts, fill=YEL)
        d.line(pts + [pts[0]], fill=(0, 0, 0), width=5)
    f1 = ImageFont.truetype(IMPACT, 178)
    while max(d.textlength(top, font=f1), d.textlength(bottom, font=f1)) > 820:
        f1 = ImageFont.truetype(IMPACT, f1.size - 4)
    d.text((40, 40), top, font=f1, fill=(255, 255, 255), stroke_width=9, stroke_fill=(0, 0, 0))
    d.text((40, 40 + int(f1.size * 1.06)), bottom, font=f1, fill=YEL, stroke_width=9, stroke_fill=(0, 0, 0))
    if sticker:
        st = Image.new("RGBA", (360, 120), (0, 0, 0, 0))
        sd = ImageDraw.Draw(st)
        sd.rounded_rectangle((6, 6, 354, 114), 22, fill=RED2 + (255,), outline=(255, 255, 255, 255), width=5)
        fs = ImageFont.truetype(IMPACT, 58)
        while sd.textlength(sticker, font=fs) > 320:
            fs = ImageFont.truetype(IMPACT, fs.size - 2)
        sd.text((180, 60), sticker, font=fs, fill="white", anchor="mm")
        st = st.rotate(8, resample=Image.BICUBIC, expand=True)
        img.paste(st, (880, 40), st)
    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.resize((int(logo.width * 70 / logo.height), 70), Image.LANCZOS)
    img.paste(logo, (1160, 625), logo)
    img.save(out)


def cmd_thumb(job_path):
    job = load_job(job_path)
    for t in job["thumbs"]:
        out = P(job, t["out"])
        frame = os.path.join(job["_work"], f"thumb_frame_{t['at']}.png")
        run(["-ss", str(t["at"]), "-i", P(job, job["src"]), "-frames:v", "1", frame])
        if t.get("style", "bold") == "bold":
            thumb_bold(frame, t["box"], t["top"], t["bottom"], t.get("sticker"), out,
                       P(job, job["src"]) if t.get("wave_at") is not None else None, t.get("wave_at", 0))
        else:
            thumb_plain(frame, t["box"], [tuple(x) for x in t["lines"]], t.get("tag", "FL STUDIO"), out)
        print("->", out)


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    cmds = {"inspect": cmd_inspect, "frame": cmd_frame, "build": cmd_build, "pauses": cmd_pauses,
            "voice": cmd_voice, "vertical": cmd_vertical, "thumb": cmd_thumb}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        sys.exit(__doc__)
    cmds[sys.argv[1]](*sys.argv[2:])

"""Local, reproducible DarkTrap reference audit. Media/cache remain in ignored build/."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import math
import re
import statistics
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "build/dark-reference-deps"))
OUT = ROOT / "build/darktrap-reference"
TRACKS = [
    "[FREE] METRO BOOMIN x 21 SAVAGE TYPE BEAT - NOIR.mp3",
    "[FREE] METRO BOOMIN x 21 SAVAGE TYPE BEAT - RASSVET.mp3",
    "[FREE] DRAKE x 21 SAVAGE TYPE BEAT - MORAL.mp3",
    "(FREE) 21 SAVAGE x METRO BOOMIN TYPE BEAT - OMEN.mp3",
    "[FREE] METRO BOOMIN x 21 SAVAGE TYPE BEAT - TRUST-1.mp3",
    "(FREE) 21 SAVAGE x METRO BOOMIN TYPE BEAT - LEGEND.mp3",
    "[FREE] Metro Boomin x 21 Savage Type Beat - Exit.mp3",
    "[FREE] Metro Boomin x 21 Savage Type Beat - Barbed.mp3",
    "[FREE] METRO BOOMIN x 21 SAVAGE TYPE BEAT - OUR LORD.mp3",
    "21 Savage Type Beat - Nothing Personal _ Metro Boomin Type Beat.mp3",
    "(FREE) 21 SAVAGE x METRO BOOMIN TYPE BEAT - NOIR.mp3",
]
VIDEOS = ["n8Ei80pWuM8", "QP6tylS-dgM", "aSx6RcIYW8U", "Gl_zx1Tbv5c",
          "r2_gi6GSpVo", "H5OT4kNzr7I", "8O3I2oDwCf8"]


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def inventory():
    rows = []
    for i, name in enumerate(TRACKS):
        path = Path("D:/Downloads/mp3") / name
        info = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-show_format", "-of", "json", str(path)]))
        row = dict(id=f"track{i+1:02}", path=str(path),
                   sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                   duration=float(info["format"]["duration"]), tags=info["format"].get("tags", {}),
                   split="validation" if i in (1, 5, 8) else "calibration")
        rows.append(row)
        print(row["id"], round(row["duration"], 1), row["sha256"][:12], name, flush=True)
    write_json(OUT / "tracks.json", rows)


def fetch_one(video_id):
    sys.path.insert(0, str(ROOT / "build/dark-reference-deps"))
    import yt_dlp
    folder = OUT / "videos" / video_id
    folder.mkdir(parents=True, exist_ok=True)
    base = dict(quiet=True, no_warnings=True, noplaylist=True, socket_timeout=20,
                retries=1, fragment_retries=1, outtmpl=str(folder / "video.%(ext)s"))
    try:
        with yt_dlp.YoutubeDL(base) as ydl:
            info = ydl.extract_info(f"https://www.youtube.com/watch?v={video_id}", download=False)
        row = {k: info.get(k) for k in ("id", "title", "duration", "description", "chapters", "uploader", "webpage_url")}
        row["caption_languages"] = sorted(set(info.get("subtitles", {})) | set(info.get("automatic_captions", {})))
        write_json(folder / "metadata.json", row)
        # Captions are optional; a caption failure must not cancel video inspection.
        try:
            with yt_dlp.YoutubeDL(dict(base, skip_download=True, writesubtitles=True,
                                      writeautomaticsub=True, subtitleslangs=["en-orig", "en"],
                                      subtitlesformat="vtt")) as ydl:
                ydl.process_ie_result(info.copy(), download=True)
            row["caption_status"] = "available" if list(folder.glob("*.vtt")) else "none"
        except Exception as exc:
            row["caption_status"] = str(exc)[:220]
        try:
            with yt_dlp.YoutubeDL(dict(base, format="bv[height<=480]+ba/b[height<=480]/worst",
                                      merge_output_format="mkv")) as ydl:
                ydl.process_ie_result(info.copy(), download=True)
            row["video_status"] = "downloaded"
        except Exception as exc:
            row["video_status"] = str(exc)[:220]
        write_json(folder / "metadata.json", row)
        print(json.dumps({k: row[k] for k in ("id", "title", "duration", "caption_status", "video_status")}, ensure_ascii=False), flush=True)
        return row
    except Exception as exc:
        row = dict(id=video_id, error=str(exc)[:400])
        write_json(folder / "metadata.json", row)
        print(json.dumps(row), flush=True)
        return row


def contacts():
    from PIL import Image, ImageDraw
    for video_id in VIDEOS:
        folder = OUT / "videos" / video_id
        info = json.loads((folder / "metadata.json").read_text(encoding="utf-8"))
        files = [p for p in folder.glob("video.*") if p.suffix in (".mkv", ".mp4", ".webm")]
        if not files:
            continue
        duration = info["duration"]
        count = max(5, min(24, int(duration / 25)))
        sheet = Image.new("RGB", (4 * 360, ((count + 3) // 4) * 225), "#151515")
        draw = ImageDraw.Draw(sheet)
        for n in range(count):
            time = (n + .5) * duration / count
            frame = folder / f"frame_{n:02}.jpg"
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(time), "-i", str(files[0]),
                            "-frames:v", "1", "-vf", "scale=360:202:force_original_aspect_ratio=decrease", str(frame)], check=True)
            im = Image.open(frame)
            x, y = n % 4 * 360, n // 4 * 225
            sheet.paste(im, (x, y + 20))
            draw.text((x + 5, y + 3), f"{int(time)//60:02}:{int(time)%60:02}", fill="white")
        sheet.save(folder / "contact.jpg")
        print(video_id, info["title"], folder / "contact.jpg", flush=True)


def separate():
    import numpy as np
    import soundfile as sf
    import torch
    from demucs.pretrained import get_model
    from demucs.apply import apply_model
    torch.set_num_threads(4)
    torch.manual_seed(0)
    model = get_model("htdemucs").cpu().eval()
    rows = json.loads((OUT / "tracks.json").read_text(encoding="utf-8"))
    for row in rows:
        for window, fraction in enumerate((.28, .60)):
            folder = OUT / "stems" / f"{row['id']}_w{window}"
            folder.mkdir(parents=True, exist_ok=True)
            start = row["duration"] * fraction
            write_json(folder / "window.json", dict(track=row["id"], start=start, seconds=24, split=row["split"]))
            if (folder / "bass.flac").exists() and (folder / "drums.flac").exists():
                continue
            mix = folder / "mix.wav"
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(start), "-t", "24", "-i", row["path"],
                            "-ar", "44100", "-ac", "2", str(mix)], check=True)
            audio, sr = sf.read(mix, dtype="float32", always_2d=True)
            wav = torch.from_numpy(audio.T.copy())
            ref = wav.mean(0)
            mean, std = ref.mean(), ref.std()
            with torch.inference_mode():
                result = apply_model(model, ((wav - mean) / std)[None], device="cpu", shifts=0,
                                     split=True, overlap=.25, progress=False, num_workers=0)[0] * std + mean
            for stem in ("bass", "drums"):
                samples = result[model.sources.index(stem)].numpy().T
                sf.write(folder / f"{stem}.flac", np.clip(samples, -.99999, .99999), sr, subtype="PCM_24")
            print(folder.name, "separated", flush=True)


def transcripts():
    import html
    for video_id in VIDEOS:
        folder = OUT / "videos" / video_id
        files = list(folder.glob("*.vtt"))
        if not files:
            continue
        lines, seen, timestamp = [], set(), ""
        for line in files[0].read_text(encoding="utf-8-sig").splitlines():
            if " --> " in line:
                timestamp = line.split(" --> ")[0]
                continue
            if not timestamp or not line or line.startswith(("NOTE", "Kind:", "Language:")):
                continue
            words = html.unescape(re.sub(r"<[^>]+>", "", line)).strip()
            if words and words not in seen:
                lines.append(f"{timestamp} {words}")
                seen.add(words)
        (folder / "transcript.txt").write_text("\n".join(lines), encoding="utf-8")
        print(video_id, len(lines), flush=True)


def transcribe_missing():
    import torch
    import whisper
    torch.set_num_threads(2)
    model = whisper.load_model("tiny.en", device="cpu")
    for video_id in ("n8Ei80pWuM8", "H5OT4kNzr7I"):
        folder = OUT / "videos" / video_id
        path = next(p for p in folder.glob("video.*") if p.suffix in (".mkv", ".mp4", ".webm"))
        result = model.transcribe(str(path), language="en", fp16=False, verbose=False,
                                  condition_on_previous_text=False)
        write_json(folder / "whisper.json", result)
        lines = [f"{s['start']:.2f} {s['text']}" for s in result["segments"]]
        (folder / "transcript-whisper.txt").write_text("\n".join(lines), encoding="utf-8")
        print(video_id, "transcribed locally (tiny.en; verify against frames)", flush=True)


def measure():
    """Use existing HPDG analyzers; retain windows separately and keep short notes."""
    import collections
    import librosa
    import numpy as np
    import soundfile as sf
    from scipy.signal import butter, sosfiltfilt
    from atmos_passport import parse_bass, parse_hits
    from atl_hat_onsets import classify
    lab = ROOT / "build/Release/HPDG_BreakLab.exe"
    tracks = json.loads((OUT / "tracks.json").read_text(encoding="utf-8"))
    rows = []
    for track in tracks:
        windows = []
        for w in (0, 1):
            folder = OUT / "stems" / f"{track['id']}_w{w}"
            if not (folder / "bass.flac").exists():
                continue
            cache = folder / "analysis.json"
            if cache.exists():
                windows.append(json.loads(cache.read_text(encoding="utf-8")))
                continue
            def run_lab(name, *args):
                path = folder / name
                if not path.exists():
                    result = subprocess.run([str(lab), *map(str, args)], capture_output=True,
                                            text=True, encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL, check=True)
                    path.write_text(result.stdout, encoding="utf-8")
                return path.read_text(encoding="utf-8")
            drum_text = run_lab("drums.txt", "analyze", folder / "drums.flac", "--cut", "--hits")
            head, hits = parse_hits(drum_text)
            if not head:
                raise ValueError(f"No tempo in {folder}")
            bpm = min((head["bpm"] * k for k in (.5, 1, 2)), key=lambda x: abs(math.log2(x / 155)))
            bass_text = run_lab("bass.txt", "lines", folder / "bass.flac", "--print")
            notes, key = parse_bass(bass_text)
            notes.sort(key=lambda n: n["start"])
            drums, sr = librosa.load(folder / "drums.flac", sr=44100, mono=True)
            high = sosfiltfilt(butter(4, 7000, "high", fs=sr, output="sos"), drums)
            env = librosa.onset.onset_strength(y=high, sr=sr, hop_length=128)
            hat_times = librosa.onset.onset_detect(onset_envelope=env, sr=sr, hop_length=128,
                                                   units="time", delta=.07, wait=2)
            labels = [classify((b - a) / (60 / bpm / 4)) for a, b in zip(hat_times, hat_times[1:])]
            counts = collections.Counter(labels)
            rolls, run = 0, 0
            for label in labels + ["end"]:
                if label in ("1/32", "1/64", "1/16T"):
                    run += 1
                else:
                    rolls += int(run >= 3)
                    run = 0
            kicks = [h["t"] for h in hits if h["lane"] == "Kick" and h["conf"] >= .5]
            snares = [h["t"] for h in hits if h["lane"] == "Snare" and h["conf"] >= .5]
            bass, sr = sf.read(folder / "bass.flac", always_2d=True)
            bass = bass.mean(axis=1)
            # Pitch transcription merges same-pitch reattacks. Audit attacks independently;
            # no pitch inference is required to count a new 808 articulation.
            attack_result = bass_attacks(bass, sr, folder, bpm)
            bands = {}
            for name, lo, hi in (("sub", 25, 80), ("lowmid", 80, 250), ("mid", 250, 1000), ("high", 1000, 8000)):
                filtered = sosfiltfilt(butter(4, [lo, hi], "bandpass", fs=sr, output="sos"), bass)
                bands[name] = float(np.mean(filtered ** 2))
            bands = {name: 10 * math.log10(max(value, 1e-16) / max(bands["sub"], 1e-16)) for name, value in bands.items()}
            intervals = [b["start"] - a["start"] for a, b in zip(notes, notes[1:])]
            same_pitch = [a["midi"] == b["midi"] for a, b in zip(notes, notes[1:])]
            kick_distances = [min((abs(n["start"] - k) for k in kicks), default=99) for n in notes]
            bars = len(bass) / sr * bpm / 240
            result = dict(window=w, seconds=len(bass) / sr, bpm=bpm, analyzed_bpm=head["bpm"],
                          tempo_confidence=head["conf"], key=key, bars=bars, bass_notes=notes,
                          kick_times=kicks, snare_times=snares, hat_times=hat_times.tolist(),
                          hat_gaps=dict(counts), rolls=rolls, band_db_relative_sub=bands,
                          bass_per_bar=len(notes)/bars, kick_per_bar=len(kicks)/bars,
                          hats_per_bar=len(hat_times)/bars,
                          bass_median_beats=statistics.median([n["dur"] * bpm / 60 for n in notes]) if notes else None,
                          bass_short_share=sum(n["dur"] * bpm / 60 <= .5 for n in notes)/max(1,len(notes)),
                          bass_repeat_share=sum(same_pitch)/max(1,len(same_pitch)),
                          bass_kick_alignment=sum(d <= .06 for d in kick_distances)/max(1,len(notes)),
                          bass_attacks=attack_result,
                          bass_gap_fill=statistics.median([min(1,a["dur"]/dt) for a,dt in zip(notes,intervals) if dt>0]) if intervals else None)
            write_json(cache, result)
            windows.append(result)
            print(folder.name, "measured", flush=True)
        rows.append(dict(id=track["id"], name=Path(track["path"]).name, split=track["split"], windows=windows))
    write_json(OUT / "analysis.json", rows)
    for row in rows:
        ws = row["windows"]
        if ws:
            print(row["id"], row["split"], "bpm", [w["bpm"] for w in ws],
                  "bass/bar", round(statistics.mean(w["bass_per_bar"] for w in ws),2),
                  "bass beats", [round(w["bass_median_beats"],2) if w["bass_median_beats"] else None for w in ws],
                  "hats/bar", round(statistics.mean(w["hats_per_bar"] for w in ws),2), flush=True)


def bass_attacks(bass, sr, folder, bpm):
    import numpy as np
    import librosa
    from scipy.signal import butter, sosfiltfilt, find_peaks
    from scipy.ndimage import uniform_filter1d
    low = sosfiltfilt(butter(4, [25, 250], "bandpass", fs=sr, output="sos"), bass)
    hop = round(sr * .005)
    env = np.sqrt(np.maximum(1e-14, uniform_filter1d(low ** 2, round(sr * .050))))[::hop]
    db = 20 * np.log10(env + 1e-9)
    delta = db - np.minimum.accumulate(np.stack([np.roll(db, k) for k in range(2, 9)]), axis=0)[-1]
    delta[:10] = 0
    delta[db < max(db) - 26] = 0
    times = {}
    for threshold in (3, 6, 9):
        peaks, _ = find_peaks(delta, height=threshold, prominence=1.5, distance=16)
        times[str(threshold)] = (peaks * hop / sr).tolist()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(15, 3))
    ax.plot(np.arange(len(db)) * hop / sr, db, lw=.6)
    for t in times["6"]:
        ax.axvline(t, color="tab:red", lw=.6, alpha=.6)
    ax.set(title=f"{folder.name}: 50ms RMS, red = >=6dB attack / 30ms (estimate)",
           xlabel="seconds", ylabel="bass RMS dBFS", ylim=(max(db)-42,max(db)+2))
    fig.tight_layout()
    fig.savefig(folder / "bass-envelope.png", dpi=110)
    plt.close(fig)
    return dict(times=times, notes_per_bar={k:len(v)/(len(bass)/sr*bpm/240) for k,v in times.items()})


def attack_audit():
    import soundfile as sf
    for folder in sorted((OUT / "stems").glob("*")):
        cache = folder / "analysis.json"
        if not cache.exists():
            continue
        row = json.loads(cache.read_text(encoding="utf-8"))
        bass, sr = sf.read(folder / "bass.flac", always_2d=True)
        row["bass_attacks"] = bass_attacks(bass.mean(axis=1), sr, folder, row["bpm"])
        write_json(cache, row)
        print(folder.name, row["bass_attacks"]["notes_per_bar"], flush=True)


def timbre():
    from atl_808_reference import analyse, med
    rows = []
    for folder in sorted((OUT / "stems").glob("*")):
        if not (folder / "bass.flac").exists():
            continue
        notes = analyse(str(folder / "bass.flac"))
        row = dict(window=folder.name, analyzed_sustained_notes=len(notes),
                   f0_hz=med(n["f0"] for n in notes), h2_db=med(n["h2"] for n in notes),
                   h3_db=med(n["h3"] for n in notes),
                   pitch_attack_semitones_estimate=med(n["drop"] for n in notes),
                   t_minus_12_seconds_censored=med(n["t12"] for n in notes),
                   sustained_share=sum(n["sustained"] for n in notes)/max(1,len(notes)))
        rows.append(row)
        print(json.dumps(row), flush=True)
    write_json(OUT / "timbre.json", rows)


def timbre_yin():
    """Steady harmonic estimates; strongest FFT peak can be H2, not the fundamental."""
    import librosa
    import numpy as np
    import soundfile as sf
    from atl_808_reference import f0_of, amp_at
    rows = []
    for folder in sorted((OUT / "stems").glob("*")):
        cache = folder / "analysis.json"
        if not cache.exists():
            continue
        info = json.loads(cache.read_text(encoding="utf-8"))
        audio, sr = sf.read(folder / "bass.flac", always_2d=True)
        audio = audio.mean(axis=1)
        small = librosa.resample(audio, orig_sr=sr, target_sr=8000)
        pitches = librosa.yin(small, fmin=25, fmax=110, sr=8000, frame_length=2048, hop_length=80)
        notes = []
        for note in info["bass_notes"]:
            if note["dur"] < .35:
                continue
            start = note["start"] + .10
            end = min(note["start"] + .35, note["start"] + note["dur"] - .03)
            values = pitches[int(start/.01):int(end/.01)]
            if len(values) < 8:
                continue
            f0 = float(np.median(values))
            if np.std(12*np.log2(values/f0)) > .6:
                continue
            seg = audio[int(start*sr):int(end*sr)]
            spec = np.abs(np.fft.rfft(seg*np.hanning(len(seg)), 131072))
            freqs = np.fft.rfftfreq(131072,1/sr)
            band = (freqs >= f0*.96) & (freqs <= f0*1.04)
            f0 = float(freqs[band][np.argmax(spec[band])])
            a1 = amp_at(spec, freqs, f0)
            notes.append(dict(f0_hz=f0, h2_db=float(20*np.log10(amp_at(spec,freqs,2*f0)/a1)),
                              h3_db=float(20*np.log10(amp_at(spec,freqs,3*f0)/a1))))
        row = dict(window=folder.name, steady_notes=len(notes), method="YIN + local spectral peak; Demucs stem",
                   medians={k:statistics.median(n[k] for n in notes) for k in ("f0_hz","h2_db","h3_db")} if notes else {})
        rows.append(row)
        print(json.dumps(row), flush=True)
    write_json(OUT / "timbre-yin.json", rows)


def compare_original():
    import numpy as np
    import soundfile as sf
    from scipy.signal import butter, sosfiltfilt
    from scipy.ndimage import uniform_filter1d
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    for name in ("track01_w0", "track04_w0", "track05_w0", "track10_w0"):
        folder = OUT / "stems" / name
        fig, ax = plt.subplots(figsize=(15, 3))
        for stem, offset, color in (("mix.wav", 0, "black"), ("bass.flac", -12, "tab:blue")):
            y, sr = sf.read(folder / stem, always_2d=True)
            y = sosfiltfilt(butter(4, [25, 250], "bandpass", fs=sr, output="sos"), y.mean(axis=1))
            hop = round(sr * .005)
            rms = np.sqrt(np.maximum(1e-14, uniform_filter1d(y*y, round(sr*.050))))[::hop]
            db = 20*np.log10(rms+1e-9)
            ax.plot(np.arange(len(db))*hop/sr, db + offset, lw=.6, color=color, label=f"{stem}; offset {offset} dB")
        ax.set(title=f"{name}: cross-check original mix / extracted bass (50ms RMS 25-250Hz)",
               xlabel="seconds", ylabel="dBFS + display offset")
        ax.legend()
        fig.tight_layout()
        fig.savefig(folder / "mix-bass-crosscheck.png", dpi=110)
        plt.close(fig)
        print(name, "original cross-check plotted", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["inventory", "fetch", "contacts", "separate", "transcripts", "transcribe", "measure", "attacks", "timbre", "timbre-yin", "original"])
    args = parser.parse_args()
    if args.action == "inventory":
        inventory()
    elif args.action == "contacts":
        contacts()
    elif args.action == "separate":
        separate()
    elif args.action == "transcripts":
        transcripts()
    elif args.action == "transcribe":
        transcribe_missing()
    elif args.action == "measure":
        measure()
    elif args.action == "attacks":
        attack_audit()
    elif args.action == "timbre":
        timbre()
    elif args.action == "timbre-yin":
        timbre_yin()
    elif args.action == "original":
        compare_original()
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(fetch_one, VIDEOS))

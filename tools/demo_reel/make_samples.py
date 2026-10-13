"""Synthesizes the two demo samples for the video (no third-party audio, nothing for Content ID):

  demo_keys_Am_88.wav   4 bars of electric-piano comping + sub bass, A minor, 88 BPM (Guide generation)
  demo_break_96.wav     4 bars of a swung funk break, synthesized drums, 96 BPM (Copy break)

usage: python make_samples.py <out folder>
"""
import sys
import wave
from pathlib import Path

import numpy as np

SR = 44100
rng = np.random.default_rng(7)


def write_wav(path, x):
    x = x / max(1e-9, np.abs(x).max()) * 0.89
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def place(buf, sound, at, gain=1.0):
    i = int(at * SR)
    n = min(len(sound), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sound[:n] * gain


def one_pole_lp(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.empty_like(x)
    s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s
        y[i] = s
    return y


def midi_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


# ---- keys loop -----------------------------------------------------------------------------
def epiano(freq, dur, vel):
    t = np.arange(int(dur * SR)) / SR
    mod_env = np.exp(-t * 9.0)
    mod = np.sin(2 * np.pi * freq * 1.0 * t) * (1.8 * mod_env + 0.15)
    tone = np.sin(2 * np.pi * freq * t + mod)
    tine = np.sin(2 * np.pi * freq * 4.0 * t) * np.exp(-t * 18) * 0.25
    env = np.exp(-t * 1.6) * np.minimum(1, t * 400)
    release = np.minimum(1, (dur - t) * 30)
    return (tone + tine) * env * release * vel


def sub(freq, dur, vel):
    t = np.arange(int(dur * SR)) / SR
    env = np.minimum(1, t * 200) * np.exp(-t * 1.2) * np.minimum(1, (dur - t) * 40)
    return (np.sin(2 * np.pi * freq * t) + 0.18 * np.sin(4 * np.pi * freq * t)) * env * vel


def keys_loop():
    bpm = 88.0
    beat = 60.0 / bpm
    bars = 4
    out = np.zeros(int(bars * 4 * beat * SR) + SR)
    # Am9 | Fmaj7 | Dm9 | E7(b9)  (voicings around middle C)
    chords = [
        (45, [57, 60, 64, 67, 71]),
        (41, [57, 60, 64, 65, 69]),
        (38, [57, 60, 62, 65, 69]),
        (40, [56, 59, 62, 64, 65]),
    ]
    # comping rhythm in beats (swung 8ths): hit, length, velocity
    comp = [(0.0, 1.4, 1.0), (1.66, 0.6, 0.7), (2.5, 1.0, 0.85), (3.66, 0.3, 0.55)]
    bass_rhythm = [(0.0, 1.5, 1.0), (1.66, 0.8, 0.8), (3.0, 0.9, 0.85)]
    for bar, (root, notes) in enumerate(chords):
        bar_start = bar * 4 * beat
        for pos, length, vel in comp:
            for k, n in enumerate(notes):
                strum = k * 0.008
                place(out, epiano(midi_hz(n), length * beat, vel * 0.22), bar_start + pos * beat + strum)
        for pos, length, vel in bass_rhythm:
            place(out, sub(midi_hz(root), length * beat, vel * 0.55), bar_start + pos * beat)
    out = out[: int(bars * 4 * beat * SR)]
    # soft tape hiss + gentle warmth
    out += one_pole_lp(rng.normal(0, 0.004, len(out)), 5000)
    out = np.tanh(out * 1.6)
    left = out
    right = np.concatenate([np.zeros(int(0.006 * SR)), out[: -int(0.006 * SR)]])
    return np.stack([left * 0.95 + right * 0.05, right * 0.95 + left * 0.05], axis=1)


# ---- drum break ----------------------------------------------------------------------------
def kick(vel):
    t = np.arange(int(0.45 * SR)) / SR
    f = 48 + 110 * np.exp(-t * 38)
    phase = 2 * np.pi * np.cumsum(f) / SR
    body = np.sin(phase) * np.exp(-t * 7.5)
    click = rng.normal(0, 1, len(t)) * np.exp(-t * 300) * 0.25
    return np.tanh((body + click) * 1.8) * vel


def snare(vel):
    t = np.arange(int(0.32 * SR)) / SR
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 28) * 0.7
    noise = rng.normal(0, 1, len(t))
    noise = noise - one_pole_lp(noise, 900)  # high-passed rattle
    noise *= np.exp(-t * 13)
    return np.tanh((tone + noise * 0.9) * 1.5) * vel


def hat(vel, open_hat=False):
    dur = 0.38 if open_hat else 0.07
    t = np.arange(int(dur * SR)) / SR
    noise = rng.normal(0, 1, len(t))
    noise = noise - one_pole_lp(noise, 6500)
    env = np.exp(-t * (7 if open_hat else 60))
    return noise * env * vel * 0.45


def break_loop():
    bpm = 96.0
    step = 60.0 / bpm / 4
    swing = 0.58  # off-16ths pushed late
    bars = 4

    def at(i):
        late = (swing - 0.5) * 2 * step if i % 2 == 1 else 0.0
        return i * step + late

    # 2-bar break (32 steps): K kick, S snare, s ghost snare, h hat, o open hat
    bar_a = {"K": [0, 3, 10], "S": [4, 12], "s": [7, 9, 15], "h": [0, 2, 4, 6, 8, 10, 12, 14], "o": [14]}
    bar_b = {"K": [0, 2, 10, 11], "S": [4, 12], "s": [6, 9, 13], "h": [0, 2, 4, 6, 8, 10, 12], "o": [14]}
    out = np.zeros(int(bars * 16 * step * SR) + SR)
    for bar in range(bars):
        pattern = bar_a if bar % 2 == 0 else bar_b
        base = bar * 16
        for i in pattern["K"]:
            place(out, kick(1.0 if i in (0, 10) else 0.8), at(base + i) - 0 * step + (bar * 0))
        for i in pattern["S"]:
            place(out, snare(1.0), at(base + i))
        for i in pattern["s"]:
            place(out, snare(0.28), at(base + i))
        for i in pattern["h"]:
            place(out, hat(0.9 if i % 4 == 0 else 0.6), at(base + i))
        for i in pattern["o"]:
            place(out, hat(0.7, open_hat=True), at(base + i))
    out = out[: int(bars * 16 * step * SR)]
    # small room + a bit of grit, like a sampled break
    room = np.zeros_like(out)
    for delay, g in ((0.011, 0.35), (0.019, 0.25), (0.029, 0.18), (0.041, 0.12)):
        d = int(delay * SR)
        room[d:] += out[:-d] * g
    out = np.tanh((out + room * 0.6) * 1.3)
    out = one_pole_lp(out, 9000) * 0.6 + out * 0.4
    return out


def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    folder.mkdir(parents=True, exist_ok=True)
    write_wav(folder / "demo_keys_Am_88.wav", keys_loop())
    write_wav(folder / "demo_break_96.wav", break_loop())
    print("written to", folder)


if __name__ == "__main__":
    main()

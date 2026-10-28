"""Where an 808 one-shot's energy sits, played at a few pitches (the player slows / speeds the sample).

usage: python tools/synth_808_bands.py <wav> [<wav> ...]

Per pitch (C1, F1, C2 for a sample tuned to C2): energy of the first 1.5 s in each band, in dB
against the sub band
  sub      below 1.6 x the played fundamental (the fundamental itself, 0 dB)
  low-mid  1.6 x f0 - 400 Hz (2nd-5th harmonics: what makes an 808 audible on small speakers)
  mid      400 Hz - 1 kHz
  high     above 1 kHz (buzz, click: "toy" / "pluck" / "fart" when there is much of it)
and the seconds until the note falls 6 dB (how long it holds).
"""
import os
import sys

import numpy as np
import soundfile as sf

PITCHES = (('C1', 32.70), ('F1', 43.65), ('C2', 65.41))
C2 = 65.406


def bands(y, sr, f0):
    seg = y[: int(1.5 * sr)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    total = spec.sum() + 1e-12
    edges = (0, 1.6 * f0, 400, 1000, sr / 2)
    return [spec[(f >= a) & (f < b)].sum() / total for a, b in zip(edges, edges[1:])]


def hold_seconds(y, sr):
    w = int(0.04 * sr)
    power = np.convolve(y ** 2, np.ones(w) / w, mode='valid')
    db = 10 * np.log10(power + 1e-12)
    peak = int(np.argmax(db[: int(0.2 * sr)]))
    below = np.where(db[peak:] < db[peak] - 6)[0]
    return (below[0] if len(below) else len(db) - peak) / sr


def main():
    print('| sample | pitch | low-mid dB | mid dB | high dB | holds -6 dB s |')
    print('|---|---|---|---|---|---|---|')
    for path in sys.argv[1:]:
        y, sr = sf.read(path, always_2d=True)
        y = y.mean(axis=1)
        for name, hz in PITCHES:
            n = int(len(y) * C2 / hz)  # played at hz: the sample stretched by C2 / hz
            played = np.interp(np.linspace(0, len(y) - 1, n), np.arange(len(y)), y)
            s, lm, m, h = (10 * np.log10(b + 1e-15) for b in bands(played, sr, hz))
            print('| %s | %s | %.1f | %.1f | %.1f | %.2f |' % (os.path.basename(path), name, lm - s, m - s, h - s, hold_seconds(played, sr)))


if __name__ == '__main__':
    main()

"""Builds HPDG's synthesized sub basses: Samples/Synth/Sub808/*.wav (2026-10-15).

Four one-shots tuned exactly to C2 (65.406 Hz, 0 cents): the Sub808 lane plays them like any 808
sample (pitch by playback rate, glides, transpose, drag / render), so they are always in key with
the sample analysis. 48 kHz, 32-bit PCM, 8 s (the player cuts each note at its length).

  Clean  pure sine, 4 ms attack: fits every style
  Warm   sine + 2nd / 3rd harmonics, soft saturation: audible on phones / laptops
  Round  sine + a little band-limited triangle: a fuller body
  Punch  sine with a short click and an 808-like decay, the pitch does not move

Usage: python tools/make_synth_sub.py [<Samples folder>]
"""
import json
import os
import sys

import numpy as np
from scipy.io import wavfile

SR = 48000
F0 = 440.0 * 2.0 ** ((36 - 69) / 12.0)  # C2
LENGTH = 8.0
PEAK = 0.80


def envelope(n, attack_s=0.004, release_s=0.30):
    env = np.ones(n)
    a = int(attack_s * SR)
    r = int(release_s * SR)
    env[:a] = 0.5 - 0.5 * np.cos(np.linspace(0.0, np.pi, a))
    env[-r:] *= 0.5 + 0.5 * np.cos(np.linspace(0.0, np.pi, r))
    return env


def normalize(x):
    return x * (PEAK / np.max(np.abs(x)))


def make():
    n = int(LENGTH * SR)
    t = np.arange(n) / SR
    phase = 2.0 * np.pi * F0 * t  # starts at 0: no click
    sine = np.sin(phase)

    clean = sine * envelope(n)

    warm = sine + 0.22 * np.sin(2 * phase) + 0.10 * np.sin(3 * phase)
    warm = np.tanh(1.6 * warm) / np.tanh(1.6)
    warm = warm * envelope(n)

    tri = sum(((-1) ** k) / (2 * k + 1) ** 2 * np.sin((2 * k + 1) * phase) for k in range(6)) * (8 / np.pi ** 2)
    round_ = (0.85 * sine + 0.30 * tri) * envelope(n)

    click_len = int(0.012 * SR)
    click = np.zeros(n)
    click[:click_len] = np.sin(2 * np.pi * 1800.0 * t[:click_len]) * np.exp(-t[:click_len] / 0.0025) * 0.35
    decay = 0.55 + 0.45 * np.exp(-t / 0.9)
    punch = (np.tanh(1.3 * sine) / np.tanh(1.3) * decay + click) * envelope(n, attack_s=0.0015)

    return {
        'SubClean': (clean, 'HPDG Sub Clean - C'),
        'SubWarm': (warm, 'HPDG Sub Warm - C'),
        'SubRound': (round_, 'HPDG Sub Round - C'),
        'SubPunch': (punch, 'HPDG Sub Punch - C'),
    }


def main():
    samples = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'Samples')
    out = os.path.join(samples, 'Synth', 'Sub808')
    os.makedirs(out, exist_ok=True)
    names = {}
    for file_name, (signal, original) in make().items():
        data = (normalize(signal) * (2 ** 31 - 1)).astype(np.int32)
        wavfile.write(os.path.join(out, file_name + '.wav'), SR, data)
        names[file_name + '.wav'] = original
    with open(os.path.join(out, 'sample-names.json'), 'w', encoding='utf-8') as f:
        json.dump(names, f, indent=2)
        f.write('\n')
    print('wrote', out, sorted(names))


if __name__ == '__main__':
    main()

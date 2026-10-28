"""Builds HPDG's synthesized sub basses: Samples/Synth/Sub808/*.wav (2026-10-15).

Four one-shots tuned exactly to C2 (65.406 Hz, 0 cents): the Sub808 lane plays them like any 808
sample (pitch by playback rate, glides, transpose, drag / render), so they are always in key with
the sample analysis. 48 kHz, 32-bit PCM, 8 s (the player cuts each note at its length).

  Clean  pure sine, 4 ms attack: fits every style
  Warm   sine + 2nd / 3rd harmonics, soft saturation: audible on phones / laptops
  Round  sine + a little band-limited triangle: a fuller body
  Punch  sine with a short click and an 808-like decay, the pitch does not move
  Sub808 ATL 1-3 / Dark 1-3  style 808s (2026-10-28, rebuilt after listening): the sine carries
         the weight, soft saturation adds the low-mid (2nd-4th harmonics, -15..-20 dB), a low-pass
         removes the rest, the note holds 1.6-4 s and starts with a short pitch punch. ATL ones are
         a little brighter, Dark ones darker and punchier (tools/synth_808_bands.py). Their display
         names sort after the others, so saved Sub808 sample indices stay valid; Trap ATLClassic /
         DarkTrap start on ATL 1 / Dark 1.

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


def style_808(t, glide, drive, bias, blend, cutoff, hold):
    """A style 808 (2026-10-28, rebuilt after listening): the sine carries the weight (sub), a soft
    asymmetric saturation adds the 2nd-4th harmonics (low-mid: the 808 is heard on small speakers),
    a low-pass at `cutoff` (Hz at C2) removes everything brighter (buzz, click - the first version
    sounded like a toy / a pluck / a fart), and the note holds: -6 dB only after `hold` s at C2.
    `glide` semitones at the attack fall back within ~30 ms: the 808's punch."""
    from scipy.signal import butter, sosfiltfilt
    freq = F0 * 2.0 ** (glide * np.exp(-t / 0.012) / 12.0)
    phase = 2.0 * np.pi * np.cumsum(freq) / SR
    tau = hold / np.log(2.0)  # -6 dB after `hold` s
    x = np.exp(-t / tau) * np.sin(phase)
    y = (np.tanh(drive * (x + bias)) - np.tanh(drive * bias)) / np.tanh(drive)
    out = (1.0 - blend) * x + blend * y
    out = sosfiltfilt(butter(4, cutoff, 'low', fs=SR, output='sos'), out)
    return out * envelope(len(t), attack_s=0.003)


# (glide st, drive, bias, saturation blend, low-pass Hz at C2, hold s at C2); measured with
# tools/synth_808_bands.py at C1 / F1 / C2 (docs/audit/DARK_TRAP_PASSPORT.md "808 sound, rebuilt").
STYLE_808 = {
    'ATL 1': (3.0, 2.8, 0.12, 0.60, 420, 1.00),   # at F1: low-mid -17.3 dB, mid / high < -90 dB, holds 2.1 s
    'ATL 2': (4.0, 3.2, 0.14, 0.60, 520, 0.80),   # grittier: low-mid -16.2 dB, mid -76 dB, holds 1.6 s
    'ATL 3': (2.0, 2.2, 0.10, 0.55, 380, 1.70),   # long and round: low-mid -19.7 dB, holds 3.2 s
    'Dark 1': (5.0, 3.0, 0.12, 0.60, 300, 1.30),  # darker, more punch: low-mid -16.9 dB, mid -111 dB, holds 2.7 s
    'Dark 2': (4.0, 4.0, 0.16, 0.55, 360, 1.10),  # driven but dark: low-mid -15.3 dB, mid -91 dB, holds 2.3 s
    'Dark 3': (6.0, 2.4, 0.10, 0.55, 260, 2.20),  # deep sub, long: low-mid -19.5 dB, holds 4.3 s
}


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

    styles = {'Sub808' + name.replace(' ', ''): (style_808(t, *params), 'HPDG Sub808 ' + name + ' - C')
              for name, params in STYLE_808.items()}
    return {
        **styles,
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

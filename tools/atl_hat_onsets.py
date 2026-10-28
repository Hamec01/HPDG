"""Hat rhythm of reference trap tracks from the drum stem itself (rolls and triplets included).

usage: python tools/atl_hat_onsets.py <stems root> <passport out folder>

HPDG_BreakLab transcribes on the 16th grid of the tempo it reads (75 BPM for a 150 BPM trap beat),
so 1/32 rolls and triplets do not show in its hats. Here: drums stem high-passed at 7 kHz, onsets
by librosa, gaps measured in 16ths of the trap bar (tempo from the passport, octave nearest 155).
Per track: hats per trap bar, share of gaps 1/8, 1/16, 1/16T (triplet), 1/32, 1/64, and rolls
(>= 3 gaps in a row at 1/32 / 1/64 / 1/16T) per 4 bars. Middle 60 % of the song (past the intro).
"""
import json
import math
import os
import statistics as st
import sys

import librosa
import numpy as np
from scipy.signal import butter, sosfiltfilt

SR = 44100
GAPS = (('1/64', 0.25), ('1/32', 0.5), ('1/16T', 2 / 3), ('1/16', 1.0), ('1/8T', 4 / 3), ('1/8', 2.0))


def style_bpm(bpm):
    return min((bpm * k for k in (0.5, 1, 2)), key=lambda b: abs(math.log2(b / 155)))


def classify(r):
    for label, val in GAPS:
        if abs(r - val) <= 0.12 * val:
            return label
    return 'other'


def main():
    stems, passport = sys.argv[1], sys.argv[2]
    print('| track | trap bpm | hats/bar | 1/8 | 1/16 | 1/16T | 1/32 | 1/64 | other | rolls per 4 bars | roll kinds |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    total = {k: 0 for k, _ in GAPS}
    total['other'] = 0
    for f in sorted(os.listdir(passport)):
        if not f.endswith('.json'):
            continue
        meta = json.load(open(os.path.join(passport, f), encoding='utf-8'))
        path = os.path.join(stems, meta['name'], 'drums.flac')
        if not os.path.exists(path):
            continue
        y, _ = librosa.load(path, sr=SR, mono=True)
        a, b = int(len(y) * 0.2), int(len(y) * 0.8)
        y = sosfiltfilt(butter(4, 7000, 'high', fs=SR, output='sos'), y[a:b])
        env = librosa.onset.onset_strength(y=y, sr=SR, hop_length=128)
        times = librosa.onset.onset_detect(onset_envelope=env, sr=SR, hop_length=128, units='time',
                                           delta=0.07, wait=2)
        bpm = style_bpm(meta['bpm'])
        s16 = 60 / bpm / 4
        bars = (len(y) / SR) / (16 * s16)
        labels = [classify((t2 - t1) / s16) for t1, t2 in zip(times, times[1:])]
        counts = {k: labels.count(k) for k in list(total)}
        for k in counts:
            total[k] += counts[k]
        rolls, kinds, run, kind = 0, {}, 0, None
        for lab in labels + ['end']:
            if lab in ('1/32', '1/64', '1/16T') and (kind is None or lab == kind):
                run += 1
                kind = lab
                continue
            if run >= 3:
                rolls += 1
                kinds[kind] = kinds.get(kind, 0) + 1
            run, kind = (1, lab) if lab in ('1/32', '1/64', '1/16T') else (0, None)
        n = max(1, len(labels))
        print('| %s | %.0f | %.1f | %s | %.1f | %s |' % (
            meta['name'][:44], bpm, len(times) / bars,
            ' | '.join('%.2f' % (counts[k] / n) for k in ('1/8', '1/16', '1/16T', '1/32', '1/64', 'other')),
            rolls / bars * 4, ', '.join(f'{k} {v}' for k, v in sorted(kinds.items()))))
    n = sum(total.values())
    print('\nall gaps: ' + ', '.join(f'{k} {v / n:.2f}' for k, v in total.items()))


if __name__ == '__main__':
    main()

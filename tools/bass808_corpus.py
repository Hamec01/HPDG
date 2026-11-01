"""What pro 808 one-shots are made of (2026-11-01): the measurements behind HPDG 808's presets.

usage: python tools/bass808_corpus.py <out.csv> <folder> [<folder> ...]
       python tools/bass808_corpus.py --summary <out.csv>

Per one-shot (duplicates by content skipped, at most 60 per folder):
  f0         settled pitch (Hz), from 0.15-0.6 s
  punch_st   pitch at the attack against f0, semitones (8-14 ms in)
  punch_ms   time until the pitch is within 0.5 semitone of f0
  h2..h6     harmonics against the fundamental in the body (0.15-0.6 s), dB
  click_db   energy above 1 kHz in the first 25 ms against the body's fundamental band, dB
  crest      peak / RMS in the body (sine 1.41, square 1.0, clipped in between)
  t6/t12/t24 seconds from the peak until the RMS (20 ms) falls 6 / 12 / 24 dB
  length     seconds until -50 dB (or the file's end)
  sustain    level 0.5 s after the peak against the peak, dB (a plateau or a decay)
"""
import csv
import hashlib
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt

FIELDS = ['file', 'f0', 'punch_st', 'punch_ms', 'h2', 'h3', 'h4', 'h5', 'h6', 'click_db', 'crest',
          't6', 't12', 't24', 'length', 'sustain']


def settled_f0(y, sr):
    seg = y[int(0.15 * sr): int(0.6 * sr)]
    if len(seg) < int(0.2 * sr):
        seg = y[int(0.05 * sr):]
    n = 1 << 18
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    f = np.fft.rfftfreq(n, 1 / sr)
    band = (f > 25) & (f < 200)
    k = np.argmax(spec * band)
    # a strong 2nd harmonic can win: prefer half when it is a real peak
    half = np.argmin(np.abs(f - f[k] / 2))
    if f[k] / 2 > 25 and spec[half] > 0.25 * spec[k]:
        k = half
    return float(f[k])


def pitch_track(y, sr, f0):
    lp = sosfiltfilt(butter(4, min(2.6 * f0, sr / 2 - 1), 'low', fs=sr, output='sos'), y)
    sign = np.signbit(lp)
    up = np.where(sign[:-1] & ~sign[1:])[0]
    if len(up) < 4:
        return None
    # sub-sample crossing
    frac = lp[up] / (lp[up] - lp[up + 1])
    t = (up + frac) / sr
    period = np.diff(t)
    return (t[:-1] + period / 2), 1.0 / period


def harmonics(y, sr, f0):
    seg = y[int(0.15 * sr): int(0.6 * sr)]
    if len(seg) < int(0.1 * sr):
        return [np.nan] * 5
    w = np.hanning(len(seg))
    tt = np.arange(len(seg)) / sr
    amp = []
    for h in range(1, 7):
        if h * f0 > sr / 2:
            amp.append(1e-9)
            continue
        c = np.sum(seg * w * np.exp(-2j * np.pi * h * f0 * tt))
        amp.append(abs(c) + 1e-12)
    return [20 * np.log10(a / amp[0]) for a in amp[1:]]


def envelope_db(y, sr, win=0.02):
    w = max(1, int(win * sr))
    p = np.convolve(y ** 2, np.ones(w) / w, mode='same')
    return 10 * np.log10(p + 1e-14)


def analyse(path):
    y, sr = sf.read(path, always_2d=True)
    y = y.mean(axis=1)
    if len(y) < int(0.25 * sr) or np.max(np.abs(y)) < 1e-4:
        return None
    y = y / np.max(np.abs(y))
    start = int(np.argmax(np.abs(y) > 0.02))
    y = y[start:]
    f0 = settled_f0(y, sr)
    row = {'file': path, 'f0': round(f0, 2)}

    tr = pitch_track(y[: int(0.4 * sr)], sr, f0)
    if tr is not None:
        t, f = tr
        st = 12 * np.log2(np.clip(f, 5, 5000) / f0)
        early = (t > 0.008) & (t < 0.014)
        row['punch_st'] = round(float(np.median(st[early])) if early.any() else float(st[0]), 2)
        out = np.where(np.abs(st) > 0.5)[0]
        settled = t[out[-1]] if len(out) and t[out[-1]] < 0.35 else (0.0 if not len(out) else 0.35)
        row['punch_ms'] = round(1000 * float(settled), 1)
    h = harmonics(y, sr, f0)
    for i, v in enumerate(h):
        row['h%d' % (i + 2)] = round(float(v), 1)

    att = y[: int(0.025 * sr)]
    hp = sosfiltfilt(butter(4, 1000, 'high', fs=sr, output='sos'), y)[: int(0.025 * sr)]
    body = sosfiltfilt(butter(4, 1.6 * f0, 'low', fs=sr, output='sos'), y)[int(0.15 * sr): int(0.6 * sr)]
    row['click_db'] = round(10 * np.log10((np.mean(hp ** 2) + 1e-14) / (np.mean(body ** 2) + 1e-14)), 1)
    seg = y[int(0.15 * sr): int(0.6 * sr)]
    if len(seg):
        row['crest'] = round(float(np.max(np.abs(seg)) / (np.sqrt(np.mean(seg ** 2)) + 1e-12)), 2)

    db = envelope_db(y, sr)
    peak = int(np.argmax(db[: int(0.15 * sr)]))
    for drop, key in ((6, 't6'), (12, 't12'), (24, 't24')):
        below = np.where(db[peak:] < db[peak] - drop)[0]
        row[key] = round((below[0] if len(below) else len(db) - peak) / sr, 3)
    below = np.where(db[peak:] < db[peak] - 50)[0]
    row['length'] = round((below[0] if len(below) else len(db) - peak) / sr, 3)
    at = peak + int(0.5 * sr)
    row['sustain'] = round(float(db[at] - db[peak]) if at < len(db) else -60.0, 1)
    del att
    return row


def collect(out_csv, folders):
    seen = set()
    rows = []
    for folder in folders:
        count = 0
        for root, _, files in os.walk(folder):
            for name in sorted(files):
                if not name.lower().endswith('.wav') or count >= 60:
                    continue
                path = os.path.join(root, name)
                digest = hashlib.md5(open(path, 'rb').read()).hexdigest()
                if digest in seen:
                    continue
                seen.add(digest)
                try:
                    row = analyse(path)
                except Exception as error:  # noqa: BLE001 - a broken file is skipped, not fatal
                    print('skip', path, error, file=sys.stderr)
                    continue
                if row:
                    rows.append(row)
                    count += 1
        print(folder, count, file=sys.stderr)
    with open(out_csv, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, FIELDS)
        w.writeheader()
        w.writerows(rows)


def summary(path):
    with open(path, encoding='utf-8') as f:
        rows = list(csv.DictReader(f))
    print('files', len(rows))
    print('%-9s %8s %8s %8s %8s %8s' % ('', 'p10', 'p25', 'p50', 'p75', 'p90'))
    for key in FIELDS[1:]:
        v = np.array([float(r[key]) for r in rows if r.get(key) not in (None, '', 'nan')])
        v = v[np.isfinite(v)]
        if len(v):
            print('%-9s' % key + ''.join('%9.2f' % x for x in np.percentile(v, [10, 25, 50, 75, 90])))


if __name__ == '__main__':
    if sys.argv[1] == '--summary':
        summary(sys.argv[2])
    else:
        collect(sys.argv[1], sys.argv[2:])

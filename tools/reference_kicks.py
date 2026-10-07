"""Reference kick distribution vs HPDG (roadmap Phase 2, RULES 18 / 19).

usage: python tools/reference_kicks.py <reports folder> [--lane kick|kickall|hat] [--generated <lab patterns.csv> ...]

<reports folder>: `HPDG_BreakLab analyze <loop> --bpm <label> --hits` reports (one .txt per loop).
Kicks with transcription confidence >= 0.5 are read per bar on the 16th grid. Loops are split
70 / 30 into calibration / validation by a stable hash of the file name (tune on calibration,
check on validation).

Printed per set:
  - kick probability per 16th position (per bar), kicks per bar, distinct bar patterns;
  - for each --generated CSV (GenerationQualityLab, column kickBars): the same profile, the L1
    distance of the position profile to the reference, and the share of reference bars whose
    exact kick pattern HPDG ever generates.
"""
import collections
import csv
import hashlib
import os
import re
import sys

args = sys.argv[1:]
folder = args[0]
generated = [args[i + 1] for i, a in enumerate(args) if a == '--generated']
lane = args[args.index('--lane') + 1] if '--lane' in args else 'kick'
LANE_NAME = {'kick': 'Kick', 'kickall': 'Kick', 'hat': 'HiHat'}[lane]
COLUMN = {'kick': 'kickBars', 'kickall': 'kickAllBars', 'hat': 'hatBars'}[lane]


def split_of(name):
    return 'validation' if int(hashlib.md5(name.encode('utf-8')).hexdigest(), 16) % 10 < 3 else 'calibration'


def profile(bars):
    counts = [0] * 16
    for bar in bars:
        for s in bar:
            counts[s] += 1
    n = max(1, len(bars))
    return [c / n for c in counts], sum(len(b) for b in bars) / n


ref = {'calibration': [], 'validation': []}
for name in sorted(os.listdir(folder)):
    if not name.endswith('.txt'):
        continue
    text = open(os.path.join(folder, name), encoding='utf-8', errors='replace').read()
    m = re.search(r'\| bars (\d+) \|', text)
    nbars = int(m.group(1)) if m else 0
    per_bar = collections.defaultdict(set)
    for hit in re.finditer(r'^' + LANE_NAME + r'\s+t=\S+ bar (\d+) 16th \S+ grid (\d+) off \S+ vel \d+ conf ([0-9.]+)', text, re.M):
        if float(hit.group(3)) >= 0.5:
            per_bar[int(hit.group(1))].add((int(hit.group(2)) % 3840) // 240)
    ref[split_of(name)].extend(frozenset(per_bar.get(b, set())) for b in range(1, nbars + 1))


def show(title, bars):
    prof, kpb = profile(bars)
    print(f'{title}: {len(bars)} bars | {lane}s/bar {kpb:.2f} | distinct patterns {len(set(bars))}')
    print('   16th: ' + ' '.join(f'{s:4d}' for s in range(16)))
    print('   p   : ' + ' '.join(f'{p:4.2f}' for p in prof))
    return prof


refprof = {s: show(f'REFERENCE {s}', ref[s]) for s in ('calibration', 'validation')}
for path in generated:
    rows = list(csv.DictReader(open(path, encoding='utf-8')))
    by_config = collections.defaultdict(list)
    for r in rows:
        bars = [frozenset(int(x) for x in b.split()) for b in r[COLUMN].split('|') if b != '' or True][:int(r['bars'])]
        by_config[(r['genre'], r['substyle'], r['density'])].extend(bars)
    for key, bars in by_config.items():
        prof = show(f'HPDG {key[0]} {key[1]} density {float(key[2]):.2f}', bars)
        patterns = set(bars)
        for s in ('calibration', 'validation'):
            l1 = sum(abs(a - b) for a, b in zip(prof, refprof[s]))
            covered = sum(1 for b in ref[s] if b in patterns) / max(1, len(ref[s]))
            print(f'   vs {s}: L1 position distance {l1:.2f} | reference bars HPDG ever plays: {100 * covered:.1f} %')

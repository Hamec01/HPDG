"""Density monotonicity and bar-length behaviour from a matrix *_summary.json (roadmap section 14 / 20).

usage: python tools/quality_matrix.py docs/audit/baseline/baseline_matrix_300_summary.json
For every genre / substyle / bar count: eventsPerBar, kicksPerBar, hatsPerBar and duplicate
rates at density low / default / high, plus whether events/bar rise monotonically.
"""
import json
import sys
from collections import defaultdict

data = json.load(open(sys.argv[1], encoding='utf-8'))
groups = defaultdict(list)
for c in data['configurations']:
    groups[(c['genre'], c['substyle'], c['bars'])].append(c)

monotone = total = 0
flat = []
print(f"{'genre':8s} {'substyle':20s} bars | events/bar  low  def  high | kicks/bar low def high | exact dup % low def high")
for (genre, sub, bars), configs in groups.items():
    configs.sort(key=lambda c: c['density'])
    ev = [c['metrics']['eventsPerBar']['mean'] for c in configs]
    kp = [c['metrics']['kicksPerBar']['mean'] for c in configs]
    dup = [100 * c['exactDuplicateRate'] for c in configs]
    total += 1
    ok = all(b >= a - 0.05 for a, b in zip(ev, ev[1:]))
    monotone += ok
    spread = ev[-1] - ev[0]
    if spread < 1.0:
        flat.append((genre, sub, bars, spread))
    print(f"{genre:8s} {sub:20s} {bars:4d} | {'':10s} {ev[0]:5.1f} {ev[1]:4.1f} {ev[2]:5.1f}{'' if ok else ' !'} | "
          f"{kp[0]:5.2f} {kp[1]:4.2f} {kp[2]:4.2f} | {dup[0]:5.1f} {dup[1]:4.1f} {dup[2]:5.1f}")
print(f"\nmonotone events/bar: {monotone}/{total}")
print(f"density barely changes the pattern (events/bar spread < 1.0 from 0.2 to 0.8): {len(flat)}/{total}")
for genre, sub, bars, spread in flat:
    print(f"  {genre} {sub} {bars} bars: +{spread:.2f} events/bar")

"""Lane reference from a bars file vs HPDG (roadmap Phase 2, RULES 18 / 19).

usage: python tools/reference_bars.py <bars.tsv> [--column kickBars] [--generated <lab patterns.csv> ...]

<bars.tsv>: one loop per line, `name<TAB>positions|positions|...` (16th positions per bar, `#` comments),
e.g. docs/audit/reference/trap_kick_bars.tsv. Loops are split 70 / 30 into calibration / validation by
an MD5 of their bars text (identical loops land in the same split). Printed per set: probability per
16th, notes per bar, the notes-per-bar distribution, distinct bar patterns; for each generated CSV
(GenerationQualityLab) the same per genre / substyle / density, the L1 position distance to each
split and the share of reference bars HPDG ever plays.
"""
import collections
import csv
import hashlib
import sys

args = sys.argv[1:]
column = args[args.index('--column') + 1] if '--column' in args else 'kickBars'
generated = [args[i + 1] for i, a in enumerate(args) if a == '--generated']

ref = {'calibration': [], 'validation': []}
for line in open(args[0], encoding='utf-8'):
    if line.startswith('#') or '\t' not in line:
        continue
    name, text = line.rstrip('\n').split('\t')[:2]
    split = 'validation' if int(hashlib.md5(text.encode()).hexdigest(), 16) % 10 < 3 else 'calibration'
    ref[split].extend(frozenset(int(x) for x in b.split()) for b in text.split('|'))


def show(title, bars):
    n = max(1, len(bars))
    prof = [sum(s in b for b in bars) / n for s in range(16)]
    dist = collections.Counter(min(len(b), 6) for b in bars)
    print(f'{title}: {len(bars)} bars | per bar {sum(map(len, bars)) / n:.2f} | distinct {len(set(bars))} | '
          + ' '.join(f'{k}:{100 * dist[k] / n:.0f}%' for k in range(7)))
    print('   p   : ' + ' '.join(f'{p:4.2f}' for p in prof))
    return prof


print('   16th: ' + ' '.join(f'{s:4d}' for s in range(16)))
refprof = {s: show(f'REFERENCE {s}', ref[s]) for s in ref}
for path in generated:
    by = collections.defaultdict(list)
    for r in csv.DictReader(open(path, encoding='utf-8')):
        by[(r['genre'], r['substyle'], r['density'], r['bars'])].extend(
            frozenset(int(x) for x in b.split()) for b in r[column].split('|')[:int(r['bars'])])
    for key, bars in by.items():
        prof = show('HPDG ' + ' '.join(key), bars)
        for s in ref:
            l1 = sum(abs(a - b) for a, b in zip(prof, refprof[s]))
            covered = sum(b in set(bars) for b in ref[s]) / max(1, len(ref[s]))
            print(f'   vs {s}: L1 {l1:.2f} | reference bars HPDG plays {100 * covered:.1f} %')

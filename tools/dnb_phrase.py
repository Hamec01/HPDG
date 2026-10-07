"""DnB phrase-level kick variation: reference loops vs HPDG (DnB stage, step 2).

usage:
  python tools/dnb_phrase.py tsv <reports folder> <out.tsv> <name prefix>
      BreakLab `analyze <loop> --bpm <label> --hits` reports of trimmed drum loops -> a bars file in
      the docs/audit/reference/dnb_kick_bars.tsv format (kick bars <TAB> snare bars), hits with
      confidence >= 0.5, positions on the report's own grid (beat 1 = first hit of a trimmed loop).
  python tools/dnb_phrase.py phrase <bars.tsv> [...] [--generated <lab patterns.csv> ...]
      Per loop / pattern of 4 bars: kick change rate between neighbouring bars, distinct kick bars,
      and the phrase shape (AAAA, AAAB, ABAB, ...) of the first four bars. Loops are split 70 / 30
      into calibration / validation by an MD5 of their bars text (as tools/reference_bars.py).
"""
import collections
import csv
import hashlib
import os
import re
import sys


def write_tsv(folder, out, prefix):
    rows = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith('.txt'):
            continue
        text = open(os.path.join(folder, name), encoding='utf-8', errors='replace').read()
        m = re.search(r'\| bars (\d+) \|', text)
        bars = int(m.group(1)) if m else 0
        lanes = {'Kick': collections.defaultdict(set), 'Snare': collections.defaultdict(set)}
        for h in re.finditer(r'^(Kick|Snare)\s+t=\S+ bar (\d+) 16th \S+ grid (\d+) off \S+ vel \d+ conf ([0-9.]+)', text, re.M):
            if float(h.group(4)) >= 0.5:
                lanes[h.group(1)][int(h.group(2))].add((int(h.group(3)) % 3840) // 240)
        if bars == 0:
            continue
        fmt = lambda lane: '|'.join(' '.join(str(s) for s in sorted(lanes[lane][b])) for b in range(1, bars + 1))
        rows.append(f"{prefix}{name[:-4]}\t{fmt('Kick')}\t{fmt('Snare')}")
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'# DnB kick / snare reference ({prefix.rstrip("_")}): 16th positions per bar, "|" between bars;\n')
        f.write('# third column = snare. Built with tools/dnb_phrase.py tsv from HPDG_BreakLab analyze --hits reports.\n')
        f.write('\n'.join(rows) + '\n')
    print(f'{out}: {len(rows)} loops')


def shape(bars):
    letters, seen = [], {}
    for b in bars[:4]:
        if b not in seen:
            seen[b] = chr(ord('A') + len(seen))
        letters.append(seen[b])
    return ''.join(letters)


def stats(title, loops):
    change, distinct, shapes = [], [], collections.Counter()
    for bars in loops:
        if len(bars) < 2:
            continue
        pairs = list(zip(bars, bars[1:]))
        change.append(sum(a != b for a, b in pairs) / len(pairs))
        distinct.append(len(set(bars[:4])))
        if len(bars) >= 4:
            shapes[shape(bars)] += 1
    n = max(1, len(change))
    total = max(1, sum(shapes.values()))
    print(f'{title}: {len(change)} loops | kick change between bars {sum(change) / n:.2f} | '
          f'distinct kick bars in 4 {sum(distinct) / n:.2f} | bars all equal {100 * sum(c == 0 for c in change) / n:.0f} %')
    print('   shapes: ' + ' '.join(f'{k} {100 * v / total:.0f}%' for k, v in shapes.most_common(8)))


if sys.argv[1] == 'tsv':
    write_tsv(sys.argv[2], sys.argv[3], sys.argv[4])
    sys.exit(0)

args = sys.argv[2:]
generated = [args[i + 1] for i, a in enumerate(args) if a == '--generated']
tsvs = [a for i, a in enumerate(args) if a != '--generated' and (i == 0 or args[i - 1] != '--generated')]
ref = {'calibration': [], 'validation': []}
for path in tsvs:
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or '\t' not in line:
            continue
        name, text = line.rstrip('\n').split('\t')[:2]
        split = 'validation' if int(hashlib.md5(text.encode()).hexdigest(), 16) % 10 < 3 else 'calibration'
        ref[split].append([frozenset(int(x) for x in b.split()) for b in text.split('|')])
for s in ('calibration', 'validation'):
    stats(f'REFERENCE {s}', ref[s])
for path in generated:
    groups = collections.defaultdict(list)
    for r in csv.DictReader(open(path, encoding='utf-8')):
        bars = [frozenset(int(x) for x in b.split()) for b in r['kickBars'].split('|')][:int(r['bars'])]
        groups[(r['genre'], r['substyle'], r['density'])].append(bars)
    for key, loops in groups.items():
        stats(f'HPDG {key[1]} density {float(key[2]):.2f}', loops)

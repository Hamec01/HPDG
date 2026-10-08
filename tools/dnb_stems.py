"""DnB hat / snare stems -> bars files (DnB stage, hats and ghost snares).

usage:
  python tools/dnb_stems.py hats <reports folder> <out.tsv> <name filter>
      Hat stems: every onset of an HPDG_BreakLab `analyze <stem> --bpm <label> --hits` report (a hat
      stem's lane labels are unreliable), step = round(t / 16th) from the file start (pack loops start
      on the bar; the report's own grid can be phase-shifted on a stem that opens off the beat).
      Row: name <TAB> 16th positions per bar "|" <TAB> hat notes per bar.
  python tools/dnb_stems.py snares <reports folder> <out.tsv> <name filter>
      Kick & snare stems: Snare onsets, same step rule. A snare is a ghost when its velocity is below
      0.7 x the loudest snare of its bar. Row: name <TAB> main snare bars <TAB> ghost snare bars.
"""
import collections
import os
import re
import sys

HIT = re.compile(r'^(\w+)\s+t=([0-9.]+) bar \d+ 16th \S+ grid \d+ off \S+ vel (\d+) conf ([0-9.]+)', re.M)


def load(folder, name_filter):
    for name in sorted(os.listdir(folder)):
        if not name.endswith('.txt') or name_filter not in name:
            continue
        text = open(os.path.join(folder, name), encoding='utf-8', errors='replace').read()
        bpm = float(re.search(r'bpm ([0-9.]+)', text).group(1))
        length = float(re.search(r'length ([0-9.]+) s', text).group(1))
        sixteenth = 60.0 / bpm / 4.0
        bars = max(1, round(length / (16 * sixteenth)))
        hits = [(h.group(1), round(float(h.group(2)) / sixteenth), int(h.group(3))) for h in HIT.finditer(text)]
        yield name[:-4], bars, [(lane, step, vel) for lane, step, vel in hits if step < bars * 16]


def fmt(per_bar, bars):
    return '|'.join(' '.join(str(s) for s in sorted(per_bar[b])) for b in range(bars))


mode, folder, out, name_filter = sys.argv[1:5]
rows = []
for name, bars, hits in load(folder, name_filter):
    if mode == 'hats':
        per_bar, notes = collections.defaultdict(set), 0
        for _, step, _ in hits:
            per_bar[step // 16].add(step % 16)
            notes += 1
        rows.append(f'{name}\t{fmt(per_bar, bars)}\t{notes / bars:.2f}')
    else:
        snares = [(step, vel) for lane, step, vel in hits if lane == 'Snare']
        loudest = collections.defaultdict(int)
        for step, vel in snares:
            loudest[step // 16] = max(loudest[step // 16], vel)
        main, ghost = collections.defaultdict(set), collections.defaultdict(set)
        for step, vel in snares:
            (ghost if vel < 0.7 * loudest[step // 16] else main)[step // 16].add(step % 16)
        rows.append(f'{name}\t{fmt(main, bars)}\t{fmt(ghost, bars)}')
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write(f'# DnB {mode} reference: 16th positions per bar, "|" between bars. Built with tools/dnb_stems.py {mode}\n')
    f.write('# from HPDG_BreakLab analyze --bpm <label> --hits reports; step = round(t / 16th) from the file start.\n')
    f.write('\n'.join(rows) + '\n')
print(f'{out}: {len(rows)} loops')

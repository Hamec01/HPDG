"""Bars of 16th positions from trimmed drum / hat loops, for reference_bars.py (RULES 18 / 19).

usage: python tools/loop_bars.py <out.tsv> <id>=<folder>[@<bpm>] ... [--lanes HiHat,Snare,Kick] [--breaklab <exe>]

Every .wav in each folder is transcribed with `HPDG_BreakLab analyze <file> --bpm <tempo> --hits`; the
tempo comes from the file name (`128bpm`, `_135BPM`, `DASHA_RUSH_131_`) or from `@<bpm>` for packs that
state it only in the folder name. The loops start on beat 1, so a hit's step is round(t / 16th) from
the file start (as docs/audit/reference/techno_ghosthack_drum_bars.tsv). Hits of the chosen lanes
(default: all) are kept; a loop's bars are joined by "|". Output line: `<id>_<file stem>\\t<bars>`.
"""
import os
import re
import subprocess
import sys

args = sys.argv[1:]
exe = args[args.index('--breaklab') + 1] if '--breaklab' in args else os.path.join(os.path.dirname(__file__), '..', 'build', 'Release', 'HPDG_BreakLab.exe')
lanes = set(args[args.index('--lanes') + 1].split(',')) if '--lanes' in args else None
skip = {args.index(k) + 1 for k in ('--breaklab', '--lanes') if k in args}
sources = [a for i, a in enumerate(args[1:], 1) if not a.startswith('--') and i not in skip]
HIT = re.compile(r'^(Kick|Snare|HiHat)\s+t=([0-9.]+)')
BPM = re.compile(r'(?:^|[^0-9])(1[0-9]{2})(?:\s*_?bpm|_)', re.I)

with open(args[0], 'w', encoding='utf-8') as out:
    out.write('# 16th positions per bar ("|" between bars) from the file start; ' + ' '.join(sources) + '\n')
    for source in sources:
        cid, folder = source.split('=', 1)
        fixed = None
        if '@' in folder:
            folder, fixed = folder.rsplit('@', 1)
            fixed = float(fixed)
        for name in sorted(os.listdir(folder)):
            if not name.lower().endswith('.wav'):
                continue
            m = BPM.search(name)
            bpm = fixed if fixed else (float(m.group(1)) if m else None)
            if not bpm:
                continue
            text = subprocess.run([exe, 'analyze', os.path.join(folder, name), '--bpm', f'{bpm:g}', '--hits'],
                                  capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
            sixteenth = 15.0 / bpm
            length = re.search(r'length ([0-9.]+) s', text)
            count = max(1, round(float(length.group(1)) / (16 * sixteenth))) if length else None
            bars = {}
            for line in text.splitlines():
                h = HIT.match(line)
                if not h or (lanes and h.group(1) not in lanes):
                    continue
                step = round(float(h.group(2)) / sixteenth)
                if count:
                    step %= 16 * count  # a hit rounding onto the loop end is the loop start
                bars.setdefault(step // 16, set()).add(step % 16)
            if not bars:
                continue
            count = count or max(bars) + 1
            line = '|'.join(' '.join(str(s) for s in sorted(bars.get(b, ()))) for b in range(count))
            out.write(f'{cid}_{os.path.splitext(name)[0]}\t{line}\n')
            print(cid, name, bpm, count, 'bars', flush=True)

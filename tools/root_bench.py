"""Root-note benchmark for bass / 808 one-shots (stage 6: the Sub808 lane must play its notes in tune).

usage:
  python tools/root_bench.py run <corpus.tsv> <out folder> [--breaklab <exe>] [--only <id>]
      Runs `HPDG_BreakLab rootnote` on every file of every corpus and caches the output per corpus.
  python tools/root_bench.py report <corpus.tsv> <out folder> [--list]
      Pitch-class accuracy against the note in the file name (and the octave where the name gives
      one, e.g. "C0"), per detector printed by the lab (yin, harmony, final), semitone-off share,
      fifth / fourth errors, cents spread.

corpus.tsv: id <TAB> folder <TAB> include regex (file name). The note is read from the file name with
the same rule as SampleLabelReader / tempo_bench.key_from_name.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import tempo_bench as tb

DEFAULT_EXE = os.path.join(os.path.dirname(__file__), '..', 'build', 'Release', 'HPDG_BreakLab.exe')
NOTES = 'C C# D D# E F F# G G# A A# B'.split()
LINE = re.compile(r'^(?P<name>.+?\.(?:wav|aif|aiff))\s+(?P<rest>.*)$', re.I)
DET = re.compile(r'(yin|harmony|final|root)\s*[^|]*?->\s*([A-G]#?)(-?\d)|(harmony|final|root) ([A-G]#?)(-?\d)')
OCTAVE_IN_NAME = re.compile(r'(?:^|[^A-Za-z])[A-G](?:#|b)?(-?\d)(?=[^A-Za-z0-9]|$)')


def load(path):
    rows = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        cid, folder, include = line.rstrip('\n').split('\t')
        rows.append(dict(id=cid, folder=folder, include=re.compile(include, re.I)))
    return rows


def files(corpus):
    if not os.path.isdir(corpus['folder']):
        return []
    out = []
    for name in sorted(os.listdir(corpus['folder'])):
        if not corpus['include'].search(name) or name.startswith('['):
            continue
        key = tb.key_from_name(name)
        if key is None:
            continue
        m = OCTAVE_IN_NAME.findall(os.path.splitext(name)[0])
        out.append((name, key[0], int(m[-1]) if m else None))
    return out


def run(corpus_path, out, exe, only):
    os.makedirs(out, exist_ok=True)
    for corpus in load(corpus_path):
        if only and corpus['id'] != only:
            continue
        target = os.path.join(out, corpus['id'] + '.txt')
        if os.path.exists(target):
            continue
        items = files(corpus)
        text = ''
        for i in range(0, len(items), 40):  # the lab takes many files per call
            chunk = [os.path.join(corpus['folder'], n) for n, _, _ in items[i:i + 40]]
            r = subprocess.run([exe, 'rootnote'] + chunk, capture_output=True, text=True, encoding='utf-8', errors='replace')
            text += r.stdout
        open(target, 'w', encoding='utf-8').write(text)
        print(corpus['id'], len(items), flush=True)


def parse_notes(rest):
    """{detector: (pitch class, octave)} from one lab output line."""
    found = {}
    for m in re.finditer(r'(yin|harmony|final)\b[^|]*?(?:->\s*)?\b([A-G]#?)(-?\d)\b', rest):
        found[m.group(1)] = (NOTES.index(m.group(2)), int(m.group(3)))
    c = re.search(r'\((-?\d+) c\)', rest)
    return found, (int(c.group(1)) if c else None)


def report(corpus_path, out, list_wrong):
    total = {}
    for corpus in load(corpus_path):
        path = os.path.join(out, corpus['id'] + '.txt')
        if not os.path.exists(path):
            continue
        truth = {n: (pc, octv) for n, pc, octv in files(corpus)}
        stats = {}
        wrong = []
        for line in open(path, encoding='utf-8', errors='replace'):
            m = LINE.match(line.strip())
            if not m or m.group('name') not in truth:
                continue
            pc, octv = truth[m.group('name')]
            found, cents = parse_notes(m.group('rest'))
            for det, (fpc, foct) in found.items():
                s = stats.setdefault(det, dict(n=0, ok=0, semi=0, fifth=0, oct_n=0, oct_ok=0))
                s['n'] += 1
                d = (fpc - pc) % 12
                s['ok'] += d == 0
                s['semi'] += d in (1, 11)
                s['fifth'] += d in (5, 7)
                if octv is not None and d == 0:
                    # packs number octaves differently (Kontakt / FL: C0 = our C1), so the check is
                    # whether the found octave keeps one constant offset from the named one
                    s['oct_n'] += 1
                    s.setdefault('oct_offsets', {}).setdefault(foct - octv, 0)
                    s['oct_offsets'][foct - octv] += 1
                    s['oct_ok'] = max(s['oct_offsets'].values())
                if det == 'final' and d != 0:
                    wrong.append(f"      truth {NOTES[pc]} found {NOTES[fpc]}{foct}  {m.group('name')}")
            for det in found:
                pass
        line = f"{corpus['id']:24s}"
        for det in ('final', 'yin', 'harmony'):
            if det in stats:
                s = stats[det]
                line += f" | {det} {s['ok'] / s['n']:.2f} (n {s['n']}, semitone {s['semi'] / s['n']:.2f}, fifth {s['fifth'] / s['n']:.2f}"
                if s['oct_n']:
                    line += f", octave consistent {s['oct_ok'] / s['oct_n']:.2f}"
                line += ')'
                t = total.setdefault(det, [0, 0])
                t[0] += s['ok']; t[1] += s['n']
        print(line)
        if list_wrong:
            print('\n'.join(wrong[:40]))
    print('ALL ' + ' | '.join(f'{det} {v[0] / v[1]:.3f} (n {v[1]})' for det, v in total.items()))


def main():
    args = sys.argv[1:]
    exe = args[args.index('--breaklab') + 1] if '--breaklab' in args else DEFAULT_EXE
    only = args[args.index('--only') + 1] if '--only' in args else None
    if args[0] == 'run':
        run(args[1], args[2], exe, only)
    elif args[0] == 'report':
        report(args[1], args[2], '--list' in args)


if __name__ == '__main__':
    main()

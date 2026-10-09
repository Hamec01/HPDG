"""Kick / snare / hat transcription benchmark on drum loops that ship with per-instrument stems
(stage 6: K/S/H accuracy for Copy Break and Guide).

Ground truth: onsets found on each clean stem (one instrument, no masking). The analyzer
(`HPDG_BreakLab analyze <full loop> --hits`) transcribes the full loop; hits are matched to the
stem onsets of the same lane within +-30 ms.

usage:
  python tools/kshh_bench.py run <corpus.tsv> <out folder> [--breaklab <exe>]
  python tools/kshh_bench.py report <corpus.tsv> <out folder> [--list]

corpus.tsv: id <TAB> folder <TAB> full-loop regex <TAB> kick suffix <TAB> snare suffix <TAB> hat suffix
  The stems are the full loop's file name with the full-loop suffix replaced; "-" = no such stem;
  a "kick+snare" stem may be given for both kick and snare (scored as one lane, K or S).
"""
import os
import re
import subprocess
import sys
import warnings

import numpy as np
from scipy.io import wavfile

warnings.filterwarnings('ignore')
DEFAULT_EXE = os.path.join(os.path.dirname(__file__), '..', 'build', 'Release', 'HPDG_BreakLab.exe')
TOL = 0.030
HIT = re.compile(r'^(Kick|Snare|HiHat)\s+t=([0-9.]+)', re.M)
LANES = ('Kick', 'Snare', 'HiHat')


def load_corpus(path):
    rows = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        cid, folder, full, k, s, h = line.rstrip('\n').split('\t')
        rows.append(dict(id=cid, folder=folder, full=re.compile(full), suffix=full.replace('\\', '').rstrip('$'),
                         stems={'Kick': k, 'Snare': s, 'HiHat': h}))
    return rows


def loops(corpus):
    if not os.path.isdir(corpus['folder']):
        return []
    out = []
    for name in sorted(os.listdir(corpus['folder'])):
        m = corpus['full'].search(name)
        if not m:
            continue
        stems = {}
        for lane, suffix in corpus['stems'].items():
            if suffix == '-':
                continue
            stem = name[:m.start()] + suffix
            if os.path.exists(os.path.join(corpus['folder'], stem)):
                stems[lane] = stem
        out.append((name, stems))
    return out


def stem_onsets(path):
    """Onsets of a single-instrument stem (5 ms frames): the peak of the next 30 ms stands at least
    4 dB above the loudest of the previous 60 ms (a new attack, not a swell inside a tail), reaches
    within 30 dB of the stem's loudest hit, and is the strongest such rise within +-20 ms; the file
    start counts as a rise from silence. Onsets at least 45 ms apart."""
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(1)
    if not np.any(x):
        return []
    hop = int(0.005 * sr)
    n = len(x) // hop
    env = np.sqrt(np.array([np.mean(x[i * hop:(i + 1) * hop] ** 2) for i in range(n)]) + 1e-12)
    db = 20 * np.log10(env / env.max())
    gain = np.full(n, -99.0)
    for i in range(n):
        peak = db[i:i + 6].max()
        before = db[max(0, i - 12):i].max() if i > 0 else -120.0
        if peak >= -30.0:
            gain[i] = peak - before
    onsets = []
    last = -1.0
    for i in range(n):
        if gain[i] < 4.0:
            continue
        if gain[i] < gain[max(0, i - 4):i + 5].max():
            continue
        t = i * hop / sr
        if last >= 0 and t - last < 0.045:
            continue
        onsets.append((t, float(db[i:i + 6].max())))
        last = t
    return onsets


def run(corpus_path, out, exe):
    os.makedirs(out, exist_ok=True)
    for corpus in load_corpus(corpus_path):
        for name, stems in loops(corpus):
            target = os.path.join(out, corpus['id'] + '__' + name + '.txt')
            if os.path.exists(target):
                continue
            r = subprocess.run([exe, 'analyze', os.path.join(corpus['folder'], name), '--hits'],
                               capture_output=True, text=True, encoding='utf-8', errors='replace')
            truth = []
            for lane, stem in stems.items():
                for t, level in stem_onsets(os.path.join(corpus['folder'], stem)):
                    truth.append(f'TRUTH {lane} {t:.4f} {level:.1f}')
            open(target, 'w', encoding='utf-8').write(r.stdout + '\n' + '\n'.join(truth) + '\n')
        print(corpus['id'], flush=True)


def match(found, truth):
    """Greedy one-to-one matching within TOL. Returns matched count."""
    used = set()
    hits = 0
    for t in found:
        best, bi = TOL, -1
        for j, u in enumerate(truth):
            if j not in used and abs(u - t) <= best:
                best, bi = abs(u - t), j
        if bi >= 0:
            used.add(bi)
            hits += 1
    return hits


def report(corpus_path, out, list_files):
    total = {}
    confusion = {}
    for corpus in load_corpus(corpus_path):
        stats = {}
        for name, stems in loops(corpus):
            path = os.path.join(out, corpus['id'] + '__' + name + '.txt')
            if not os.path.exists(path):
                continue
            text = open(path, encoding='utf-8', errors='replace').read()
            found = {lane: [] for lane in LANES}
            for lane, t in HIT.findall(text):
                found[lane].append(float(t))
            truth = {lane: [] for lane in LANES}
            for line in text.splitlines():
                if line.startswith('TRUTH '):
                    _, lane, t, level = line.split()
                    truth[lane].append(float(t))
            combined = corpus['stems']['Kick'] == corpus['stems']['Snare'] and corpus['stems']['Kick'] != '-'
            lanes = (('K+S', ('Kick', 'Snare')), ('HiHat', ('HiHat',))) if combined else tuple((l, (l,)) for l in LANES)
            for label, members in lanes:
                if not all(m in stems for m in members):
                    continue
                f = sorted(t for m in members for t in found[m])
                u = sorted(set(t for m in members for t in truth[m]))
                if combined:
                    u = sorted(truth['Kick'])  # the kick+snare stem was read once (as Kick)
                tp = match(f, u)
                s = stats.setdefault(label, [0, 0, 0])
                s[0] += tp; s[1] += len(f); s[2] += len(u)
                if list_files:
                    print(f'      {label:6s} found {len(f):3d} truth {len(u):3d} tp {tp:3d}  {name}')
            if not combined:
                for tl in LANES:
                    for fl in LANES:
                        if tl in stems:
                            c = confusion.setdefault((tl, fl), [0])
                            c[0] += match(found[fl], truth[tl])
        line = f"{corpus['id']:18s}"
        for label, (tp, nf, nt) in stats.items():
            p = tp / max(1, nf); r = tp / max(1, nt)
            line += f" | {label} P {p:.2f} R {r:.2f} F {2 * p * r / max(1e-9, p + r):.2f} (truth {nt})"
            t = total.setdefault(label, [0, 0, 0])
            t[0] += tp; t[1] += nf; t[2] += nt
        print(line)
    print('ALL ' + ' | '.join(f"{l} P {v[0] / max(1, v[1]):.2f} R {v[0] / max(1, v[2]):.2f} F {2 * v[0] / max(1, v[1] + v[2]):.2f}" for l, v in total.items()))
    if confusion:
        print('confusion (true lane -> found lane, matched within 30 ms), separated-stem loops:')
        for tl in LANES:
            row = '   ' + f'{tl:6s} -> ' + ' '.join(f'{fl} {confusion.get((tl, fl), [0])[0]:4d}' for fl in LANES)
            print(row)


def main():
    args = sys.argv[1:]
    exe = args[args.index('--breaklab') + 1] if '--breaklab' in args else DEFAULT_EXE
    if args[0] == 'run':
        run(args[1], args[2], exe)
    elif args[0] == 'report':
        report(args[1], args[2], '--list' in args)


if __name__ == '__main__':
    main()

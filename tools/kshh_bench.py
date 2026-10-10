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


def local_path(path):
    """Corpus roots of this PC (tempo_bench.py: docs/audit/reference/corpus_roots.local.tsv)."""
    sys.path.insert(0, os.path.dirname(__file__))
    import tempo_bench
    return tempo_bench.local_path(path)


def load_corpus(path):
    rows = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        cid, folder, full, k, s, h = line.rstrip('\n').split('\t')
        rows.append(dict(id=cid, folder=local_path(folder), full=re.compile(full), suffix=full.replace('\\', '').rstrip('$'),
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
    # The rise test fires up to ~25 ms before the attack (the next 30 ms against the previous 60 ms):
    # each onset moves to its attack, the first sample above 20 % of the hit's peak within 60 ms
    # (step 17: the stem truth had been 8 / 23 / 9 ms early for kick / snare / hat).
    ax = np.abs(x)
    refined = []
    for t, level in onsets:
        a = max(0, int((t - 0.005) * sr))
        seg = ax[a:a + int(0.060 * sr)]
        if len(seg) and seg.max() > 0:
            t = (a + int(np.argmax(seg > 0.2 * seg.max()))) / sr
        refined.append((t, level))
    return refined


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


def match(found, truth, pairs=None):
    """Greedy one-to-one matching within TOL. Returns matched count; (found index, truth index)
    pairs are appended to `pairs` when given."""
    used = set()
    hits = 0
    for i, t in enumerate(found):
        best, bi = TOL, -1
        for j, u in enumerate(truth):
            if j not in used and abs(u - t) <= best:
                best, bi = abs(u - t), j
        if bi >= 0:
            used.add(bi)
            hits += 1
            if pairs is not None:
                pairs.append((i, bi))
    return hits


# Copy Break plays a hit at origin + (grid + off) ticks (960 per quarter): the time it is placed at
HIT_FULL = re.compile(r'^(Kick|Snare|HiHat)\s+t=([0-9.]+) bar \d+ 16th [0-9.]+ grid (-?\d+) off (-?\d+) vel (\d+)', re.M)
BREAK = re.compile(r'Drum break: bpm ([0-9.]+) .*?origin ([-0-9.]+) ms')


def spearman(a, b):
    if len(a) < 3:
        return float('nan')
    ra = np.argsort(np.argsort(a)); rb = np.argsort(np.argsort(b))
    return float(np.corrcoef(ra, rb)[0, 1])


def report(corpus_path, out, list_files):
    total = {}
    confusion = {}
    timing = {}
    for corpus in load_corpus(corpus_path):
        stats = {}
        for name, stems in loops(corpus):
            path = os.path.join(out, corpus['id'] + '__' + name + '.txt')
            if not os.path.exists(path):
                continue
            text = open(path, encoding='utf-8', errors='replace').read()
            found = {lane: [] for lane in LANES}
            placed = {lane: [] for lane in LANES}  # (time Copy Break plays it at, velocity)
            b = BREAK.search(text)
            for lane, t, grid, off, vel in HIT_FULL.findall(text):
                found[lane].append(float(t))
                tick = 60.0 / float(b.group(1)) / 960.0 if b else 0.0
                placed[lane].append(((float(b.group(2)) / 1000.0 + (int(grid) + int(off)) * tick) if b else float(t), int(vel)))
            truth = {lane: [] for lane in LANES}
            levels = {lane: [] for lane in LANES}
            for line in text.splitlines():
                if line.startswith('TRUTH '):
                    _, lane, t, level = line.split()
                    truth[lane].append(float(t))
                    levels[lane].append(float(level))
            if not (corpus['stems']['Kick'] == corpus['stems']['Snare'] and corpus['stems']['Kick'] != '-'):
                for lane in LANES:
                    if lane not in stems:
                        continue
                    pairs = []
                    match(found[lane], truth[lane], pairs)
                    g = timing.setdefault(lane, [[], [], []])
                    for i, j in pairs:
                        g[0].append(abs(placed[lane][i][0] - truth[lane][j]) * 1000.0)
                        g[1].append(placed[lane][i][1]); g[2].append(levels[lane][j])
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
    for lane, (err, vel, lev) in timing.items():
        err = np.array(err)
        print(f'copy {lane:6s} matched {len(err):4d} | placed vs stem onset: mean {err.mean():.1f} ms, median {np.median(err):.1f}, '
              f'p90 {np.percentile(err, 90):.1f}, > 10 ms {np.mean(err > 10):.2f} | velocity vs stem level Spearman {spearman(vel, lev):.2f}')
    if confusion:
        print('confusion (true lane -> found lane, matched within 30 ms), separated-stem loops:')
        for tl in LANES:
            row = '   ' + f'{tl:6s} -> ' + ' '.join(f'{fl} {confusion.get((tl, fl), [0])[0]:4d}' for fl in LANES)
            print(row)


def load_sets(path):
    """kshh_sets.tsv (tools/find_drum_stems.py): folder TAB kick TAB snare TAB hat."""
    rows = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        folder, k, s, h = line.rstrip('\n').split('\t')
        rows.append(dict(folder=folder, stems={'Kick': k, 'Snare': s, 'HiHat': h}))
    return rows


def pack_of(folder):
    parts = folder.replace('\\', '/').split('/')
    return parts[4] if len(parts) > 4 and parts[3] == 'Downloads' else parts[-1]


def run_sets(sets_path, out, exe):
    """Stem sets without a full loop: the stems are summed into one (same rate, lengths within
    2 %, cut to the shortest, peak -1 dBFS), analysed, and scored against the stems."""
    mixdir = os.path.join(out, 'mix')
    os.makedirs(mixdir, exist_ok=True)
    for i, row in enumerate(load_sets(sets_path)):
        target = os.path.join(out, f'set__{i:03d}.txt')
        if os.path.exists(target):
            continue
        audio = {}
        rate = None
        try:
            for lane, name in row['stems'].items():
                sr, x = wavfile.read(os.path.join(row['folder'], name))
                x = x.astype(np.float64)
                if x.dtype.kind == 'i' or np.abs(x).max() > 2:
                    x = x / 32768.0 if np.abs(x).max() < 40000 else x / 2147483648.0
                audio[lane] = x.mean(1) if x.ndim > 1 else x
                if rate is not None and sr != rate:
                    raise ValueError('rates differ')
                rate = sr
        except Exception as e:
            open(target, 'w', encoding='utf-8').write(f'SKIP {e}\n')
            continue
        lengths = [len(v) for v in audio.values()]
        if max(lengths) > min(lengths) * 1.02:
            open(target, 'w', encoding='utf-8').write('SKIP lengths differ\n')
            continue
        n = min(lengths)
        mix = sum(v[:n] for v in audio.values())
        mix = mix / (np.abs(mix).max() + 1e-12) * 0.89
        mixfile = os.path.join(mixdir, f'set_{i:03d}.wav')
        wavfile.write(mixfile, rate, (mix * 32767).astype(np.int16))
        r = subprocess.run([exe, 'analyze', mixfile, '--hits'], capture_output=True, text=True, encoding='utf-8', errors='replace')
        truth = []
        for lane, name in row['stems'].items():
            for t, level in stem_onsets(os.path.join(row['folder'], name)):
                truth.append(f'TRUTH {lane} {t:.4f} {level:.1f}')
        open(target, 'w', encoding='utf-8').write(f"SET {row['folder']}\n" + r.stdout + '\n' + '\n'.join(truth) + '\n')
    print('sets done', flush=True)


def report_sets(sets_path, out):
    per_pack = {}
    for i, row in enumerate(load_sets(sets_path)):
        path = os.path.join(out, f'set__{i:03d}.txt')
        if not os.path.exists(path):
            continue
        text = open(path, encoding='utf-8', errors='replace').read()
        if text.startswith('SKIP'):
            continue
        found = {lane: [float(t) for l, t in HIT.findall(text) if l == lane] for lane in LANES}
        truth = {lane: [] for lane in LANES}
        for line in text.splitlines():
            if line.startswith('TRUTH '):
                _, lane, t, _ = line.split()
                truth[lane].append(float(t))
        stats = per_pack.setdefault(pack_of(row['folder']), {l: [0, 0, 0] for l in LANES})
        for lane in LANES:
            s = stats[lane]
            s[0] += match(found[lane], truth[lane]); s[1] += len(found[lane]); s[2] += len(truth[lane])
    total = {l: [0, 0, 0] for l in LANES}
    for pack, stats in per_pack.items():
        line = f'{pack[:40]:40s}'
        for lane, (tp, nf, nt) in stats.items():
            p, r = tp / max(1, nf), tp / max(1, nt)
            line += f' | {lane} P {p:.2f} R {r:.2f} F {2 * p * r / max(1e-9, p + r):.2f} ({nt})'
            for k in range(3):
                total[lane][k] += (tp, nf, nt)[k]
        print(line)
    print('ALL sets ' + ' | '.join(f"{l} P {v[0] / max(1, v[1]):.2f} R {v[0] / max(1, v[2]):.2f} F {2 * v[0] / max(1, v[1] + v[2]):.2f}" for l, v in total.items()))


def main():
    args = sys.argv[1:]
    exe = args[args.index('--breaklab') + 1] if '--breaklab' in args else DEFAULT_EXE
    if args[0] == 'run':
        run(args[1], args[2], exe)
    elif args[0] == 'report':
        report(args[1], args[2], '--list' in args)
    elif args[0] == 'run-sets':
        run_sets(args[1], args[2], exe)
    elif args[0] == 'report-sets':
        report_sets(args[1], args[2])


if __name__ == '__main__':
    main()

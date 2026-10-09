"""Tempo / phase benchmark for Sample Analysis (roadmap §24-29, RULES 24-27).

usage:
  python tools/tempo_bench.py run <corpus.tsv> <out folder> [--breaklab <exe>] [--only <corpus id>]
      Runs HPDG_BreakLab on every corpus file twice and caches the reports:
        <out>/auto/<id>/<file>.txt   analyze <file> --hits                (automatic tempo)
        <out>/typed/<id>/<file>.txt  analyze <file> --bpm <label> --hits  (typed tempo: phase only)
      Existing reports are kept (delete the folder to re-run after a change).
  python tools/tempo_bench.py report <corpus.tsv> <out folder> [--json <file>] [--list]
      Per corpus and per category: tempo accuracy (|err| <= 0.5 BPM, <= 2 %), half / double / other
      metrical errors, median error, confidence calibration (accuracy per confidence bin), drum-loop
      confidence (median, share >= 0.75 = "clearly a drum loop" in the plugin); phase (trimmed loops only, origin truth 0 ms): |origin| automatic (when the tempo is
      right) and typed, in ms and in 16ths. --list prints every wrong file.

  python tools/tempo_bench.py run-key <corpus.tsv> <out folder> [--breaklab <exe>] [--only <corpus id>]
      Tonal corpora only: analyze <file> --harmony -> <out>/harmony/<id>/<file>.txt (the key is
      estimated from the whole sample's chroma, independent of the tempo).
  python tools/tempo_bench.py key-report <corpus.tsv> <out folder> [--json <file>] [--list]
      Key accuracy against the key in the file name: MIREX weighted score (exact 1, fifth 0.5,
      relative 0.3, parallel 0.2), exact, relative, fifth, parallel, other; files whose name gives
      only a root ("A#") are scored on the root. Confidence -> exact accuracy.

  Both run commands take --labels name|wrong: the file's own tempo / key labels (as the plugin
  reads them) or deliberately wrong ones (tempo x4/3, key a tritone off). Default: audio only.

Tempo and phase are reported separately (roadmap §29); the host / typed tempo is a hint, the label in
the file name is the ground truth.
"""
import json
import os
import re
import subprocess
import sys

DEFAULT_EXE = os.path.join(os.path.dirname(__file__), '..', 'build', 'Release', 'HPDG_BreakLab.exe')


def load_corpus(path):
    rows = []
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        cid, category, genre, trimmed, folder, include, bpm_re = line.rstrip('\n').split('\t')
        rows.append(dict(id=cid, category=category, genre=genre, trimmed=trimmed == 'yes', folder=folder,
                         include=re.compile(include, re.I), bpm=re.compile(bpm_re, re.I)))
    return rows


def acid_tempo(path):
    """Tempo in a WAV 'acid' chunk (FL Studio renders, acidized packs), or None."""
    import struct
    try:
        with open(path, 'rb') as f:
            if f.read(12)[8:12] != b'WAVE':
                return None
            while True:
                h = f.read(8)
                if len(h) < 8:
                    return None
                cid, size = h[:4], struct.unpack('<I', h[4:])[0]
                if cid == b'acid' and size >= 24:
                    return struct.unpack('<f', f.read(size)[20:24])[0]
                f.seek(size + (size % 2), 1)
    except OSError:
        return None


PATHS = {}  # (corpus id, name) -> full path, for list-based corpora


def path_of(corpus, name):
    return PATHS.get((corpus['id'], name), os.path.join(corpus['folder'], name))


def files(corpus):
    """(name, true bpm) per file. 'folder' may be a .txt list of full paths; the bpm regex
    'acid' takes the tempo from the WAV acid chunk instead of the name."""
    if corpus['folder'].endswith('.txt'):
        if not os.path.isfile(corpus['folder']):
            return []
        paths = [l.strip() for l in open(corpus['folder'], encoding='utf-8') if l.strip() and not l.startswith('#')]
    elif os.path.isdir(corpus['folder']):
        paths = [os.path.join(corpus['folder'], n) for n in sorted(os.listdir(corpus['folder']))]
    else:
        return []
    out = []
    for path in paths:
        name = os.path.basename(path)
        if not corpus['include'].search(name):
            continue
        if corpus['bpm'].pattern == 'acid':
            bpm = acid_tempo(path)
            if bpm:
                out.append((name, round(bpm, 2)))
                PATHS[(corpus['id'], name)] = path
            continue
        m = corpus['bpm'].search(name)
        if m:
            out.append((name, float(m.group(1))))
            PATHS[(corpus['id'], name)] = path
    return out


def label_args(labels, name, bpm):
    """Extra lab arguments: no labels (audio only, default), the file's own labels (name / acid
    chunk, as the plugin reads them), or deliberately wrong ones (tempo x4/3, key a tritone off)
    to measure how often the audio rejects a wrong label."""
    if labels == 'name':
        return ['--labels']
    if labels == 'wrong':
        extra = ['--label-bpm', f'{bpm * 4 / 3:.2f}']
        key = key_from_name(name)
        if key is not None:
            extra += ['--label-key', NOTE_NAMES[(key[0] + 6) % 12] + ('' if key[1] is None else 'm' if key[1] == 0 else ' maj')]
        return extra
    return []


def run(corpus_path, out, exe, only, labels=None):
    for corpus in load_corpus(corpus_path):
        if only and corpus['id'] != only:
            continue
        items = files(corpus)
        print(f"{corpus['id']}: {len(items)} files", flush=True)
        for mode in ('auto', 'typed'):
            folder = os.path.join(out, mode, corpus['id'])
            os.makedirs(folder, exist_ok=True)
            for name, bpm in items:
                target = os.path.join(folder, name + '.txt')
                if os.path.exists(target):
                    continue
                args = [exe, 'analyze', path_of(corpus, name), '--hits']
                if mode == 'typed':
                    args[3:3] = ['--bpm', f'{bpm:g}']
                args += label_args(labels, name, bpm)
                result = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', errors='replace')
                with open(target, 'w', encoding='utf-8') as f:
                    f.write(result.stdout)


def run_key(corpus_path, out, exe, only, labels=None):
    for corpus in load_corpus(corpus_path):
        if corpus['category'] != 'tonal' or (only and corpus['id'] != only):
            continue
        items = files(corpus)
        print(f"{corpus['id']}: {len(items)} files", flush=True)
        folder = os.path.join(out, 'harmony', corpus['id'])
        os.makedirs(folder, exist_ok=True)
        for name, bpm in items:
            target = os.path.join(folder, name + '.txt')
            if os.path.exists(target):
                continue
            result = subprocess.run([exe, 'analyze', path_of(corpus, name), '--harmony'] + label_args(labels, name, bpm),
                                    capture_output=True, text=True, encoding='utf-8', errors='replace')
            with open(target, 'w', encoding='utf-8') as f:
                f.write(result.stdout)


NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
NOTE_NAMES = 'C C# D D# E F F# G G# A A# B'.split()
# same rule as SampleLabelReader: the letter must not continue a word ("Drum"); digits may touch it
# ("95Em"); accidentals "#", "b", "sharp", "shrp"
KEY_IN_NAME = re.compile(r'(?:^|[^A-Za-z])([A-G])(#|b|sharp|shrp)? ?(minor|major|min|maj|m)?(?=[^A-Za-z]|$)', re.I)
KEY_FOUND = re.compile(r'Sample harmony: key ([A-G]#?) (major|minor) \(([0-9.]+)\)')


def key_from_name(name):
    """(root 0-11, mode 0 minor / 1 major / None) from the last key token of a file name."""
    # some packs type the note with a Cyrillic look-alike ("Сm")
    stem = os.path.splitext(name)[0].translate(str.maketrans('САВЕ', 'CABE'))
    matches = [m for m in KEY_IN_NAME.finditer(stem) if m.group(1).isupper()]
    if not matches:
        return None
    m = matches[-1]
    acc = (m.group(2) or '').lower()
    root = (NOTE[m.group(1)] + (1 if acc == '#' or acc.startswith('s') else -1 if acc == 'b' else 0)) % 12
    suffix = (m.group(3) or '').lower()
    mode = 0 if suffix in ('m', 'min', 'minor') else 1 if suffix in ('maj', 'major') else None
    return root, mode


def key_kind(found, truth):
    root, mode = found
    troot, tmode = truth
    if tmode is None:
        return 'exact' if root == troot else 'other'
    if root == troot and mode == tmode:
        return 'exact'
    if mode == tmode and (root - troot) % 12 in (5, 7):
        return 'fifth'
    if mode != tmode and root == (troot + (3 if tmode == 0 else 9)) % 12:
        return 'relative'
    if root == troot:
        return 'parallel'
    return 'other'


MIREX = {'exact': 1.0, 'fifth': 0.5, 'relative': 0.3, 'parallel': 0.2, 'other': 0.0}


def key_report(corpus_path, out, json_path, list_wrong):
    result = {}
    everything = []
    for corpus in load_corpus(corpus_path):
        if corpus['category'] != 'tonal':
            continue
        rows = []
        for name, _ in files(corpus):
            truth = key_from_name(name)
            path = os.path.join(out, 'harmony', corpus['id'], name + '.txt')
            if truth is None or not os.path.exists(path):
                continue
            text = open(path, encoding='utf-8', errors='replace').read()
            if 'cannot read' in text:
                continue  # the lab could not open the file (non-ANSI name on Windows)
            m = KEY_FOUND.search(text)
            if not m:
                rows.append(dict(name=name, kind='other', conf=0.0, found='-', truth=truth))
                continue
            root = (NOTE[m.group(1)[0]] + (1 if m.group(1).endswith('#') else 0)) % 12
            found = (root, 1 if m.group(2) == 'major' else 0)
            rows.append(dict(name=name, kind=key_kind(found, truth), conf=float(m.group(3)),
                             found=f"{m.group(1)} {m.group(2)}", truth=truth))
        if rows:
            result[corpus['id']] = key_summary(corpus['id'], rows, list_wrong)
            everything.extend(rows)
    if everything:
        result['ALL tonal'] = key_summary('ALL tonal', everything, False)
    if json_path:
        json.dump(result, open(json_path, 'w', encoding='utf-8'), indent=1)


def key_summary(cid, rows, list_wrong):
    n = len(rows)
    share = lambda k: sum(1 for r in rows if r['kind'] == k) / n
    s = dict(n=n, mirex=sum(MIREX[r['kind']] for r in rows) / n, exact=share('exact'), fifth=share('fifth'),
             relative=share('relative'), parallel=share('parallel'), other=share('other'),
             modeKnown=sum(1 for r in rows if r['truth'][1] is not None) / n)
    bins = {}
    for r in rows:
        b = '<0.4' if r['conf'] < 0.4 else '0.4-0.6' if r['conf'] < 0.6 else '>=0.6'
        t, ok = bins.get(b, (0, 0))
        bins[b] = (t + 1, ok + (r['kind'] == 'exact'))
    s['calibration'] = {b: dict(n=t, exact=ok / t) for b, (t, ok) in bins.items()}
    cal = ' '.join(f"{b}:{v['exact']:.2f}(n{v['n']})" for b, v in sorted(s['calibration'].items()))
    print(f"{cid:20s} n {n:3d} | MIREX {s['mirex']:.2f} | exact {s['exact']:.2f} fifth {s['fifth']:.2f} "
          f"relative {s['relative']:.2f} parallel {s['parallel']:.2f} other {s['other']:.2f} "
          f"| mode in name {s['modeKnown']:.2f} | conf -> exact {cal}")
    if list_wrong:
        for r in rows:
            if r['kind'] != 'exact':
                t = NOTE_NAMES[r['truth'][0]] + ('' if r['truth'][1] is None else ' major' if r['truth'][1] else ' minor')
                print(f"{'':22s}{r['kind']:8s} truth {t:9s} found {r['found']:9s} conf {r['conf']:.2f}  {r['name']}")
    return s


SUMMARY = re.compile(r'Drum break: bpm ([0-9.]+) \| bars (\d+) \| tempo conf ([0-9.]+).*?origin ([-0-9.]+) ms', re.S)
LOOPCONF = re.compile(r'Drum loop confidence ([0-9.]+)')


def parse(path):
    if not os.path.exists(path):
        return None
    text = open(path, encoding='utf-8', errors='replace').read()
    m = SUMMARY.search(text)
    if not m:
        return None
    lc = LOOPCONF.search(text)
    return dict(bpm=float(m.group(1)), bars=int(m.group(2)), conf=float(m.group(3)),
                origin=float(m.group(4)), loop=float(lc.group(1)) if lc else None)


def kind(found, truth):
    if abs(found - truth) <= 0.5:
        return 'exact'
    if abs(found - truth) / truth <= 0.02:
        return 'near'
    for name, ratio in (('half', 0.5), ('double', 2.0), ('2/3', 2 / 3), ('3/2', 1.5), ('3/4', 0.75), ('4/3', 4 / 3)):
        if abs(found / truth - ratio) <= 0.03:
            return name
    return 'other'


def median(values):
    values = sorted(values)
    return values[len(values) // 2] if values else float('nan')


def report(corpus_path, out, json_path, list_wrong):
    corpora = load_corpus(corpus_path)
    result = {}
    groups = {}
    for corpus in corpora:
        rows = []
        for name, truth in files(corpus):
            a = parse(os.path.join(out, 'auto', corpus['id'], name + '.txt'))
            t = parse(os.path.join(out, 'typed', corpus['id'], name + '.txt'))
            if a is None:
                continue
            rows.append(dict(name=name, truth=truth, auto=a, typed=t, kind=kind(a['bpm'], truth)))
        if not rows:
            continue
        groups.setdefault(corpus['category'], []).extend((corpus, r) for r in rows)
        result[corpus['id']] = summarize(corpus, rows, list_wrong)
    for category, pairs in groups.items():
        rows = [r for _, r in pairs]
        trimmed = all(c['trimmed'] for c, _ in pairs)
        result['ALL ' + category] = summarize(dict(id='ALL ' + category, category=category, trimmed=trimmed), rows, False)
    if json_path:
        json.dump(result, open(json_path, 'w', encoding='utf-8'), indent=1)


def summarize(corpus, rows, list_wrong):
    n = len(rows)
    kinds = {}
    for r in rows:
        kinds[r['kind']] = kinds.get(r['kind'], 0) + 1
    right = [r for r in rows if r['kind'] in ('exact', 'near')]
    errs = [abs(r['auto']['bpm'] - r['truth']) for r in rows]
    s = dict(n=n, exact=kinds.get('exact', 0) / n, within2pct=len(right) / n,
             half=kinds.get('half', 0) / n, double=kinds.get('double', 0) / n,
             otherMetrical=sum(kinds.get(k, 0) for k in ('2/3', '3/2', '3/4', '4/3')) / n,
             other=kinds.get('other', 0) / n, medianErr=median(errs))
    # The genre engines fold a sample tempo into their own range (TempoInterpretation.h), so an
    # octave reading (x2 / x1/2) still gives the right pattern; x3/2, x3/4 and others do not.
    s['octaveOk'] = (len(right) + kinds.get('half', 0) + kinds.get('double', 0)) / n
    # confidence calibration: accuracy (within 2 %) per confidence bin
    bins = {}
    for r in rows:
        c = r['auto']['conf']
        b = '<0.4' if c < 0.4 else '0.4-0.6' if c < 0.6 else '0.6-0.8' if c < 0.8 else '>=0.8'
        hit = r['kind'] in ('exact', 'near')
        tot, ok = bins.get(b, (0, 0))
        bins[b] = (tot + 1, ok + hit)
    s['calibration'] = {b: dict(n=t, accuracy=ok / t) for b, (t, ok) in bins.items()}
    loops = [r['auto']['loop'] for r in rows if r['auto']['loop'] is not None]
    s['drumLoopConfMedian'] = median(loops)
    # 0.75 = the plugin's "clearly a drum loop" threshold (SampleAnalyzer / PluginProcessor)
    s['drumLoopConfAbove075'] = sum(1 for v in loops if v >= 0.75) / max(1, len(loops))
    if corpus['trimmed']:
        auto_origin = [abs(r['auto']['origin']) for r in right]
        typed_origin = [abs(r['typed']['origin']) for r in rows if r['typed'] is not None]
        sixteenth = lambda r: 60000.0 / r['truth'] / 4
        typed16 = [abs(r['typed']['origin']) / sixteenth(r) for r in rows if r['typed'] is not None]
        s['originAutoMedianMs'] = median(auto_origin)
        s['originAutoWithin20ms'] = sum(1 for v in auto_origin if v <= 20) / max(1, len(auto_origin))
        s['originTypedMedianMs'] = median(typed_origin)
        s['originTypedWithin20ms'] = sum(1 for v in typed_origin if v <= 20) / max(1, len(typed_origin))
        s['originTypedOffByAQuarterOrMore'] = sum(1 for v in typed16 if v >= 3.5) / max(1, len(typed16))
    cal = ' '.join(f"{b}:{v['accuracy']:.2f}(n{v['n']})" for b, v in sorted(s['calibration'].items()))
    line = (f"{corpus['id']:20s} n {n:3d} | exact {s['exact']:.2f} <=2% {s['within2pct']:.2f} up-to-octave {s['octaveOk']:.2f} | half {s['half']:.2f} "
            f"double {s['double']:.2f} 2/3-type {s['otherMetrical']:.2f} other {s['other']:.2f} | med err {s['medianErr']:.2f} "
            f"| loop conf med {s['drumLoopConfMedian']:.2f} (>=0.75: {s['drumLoopConfAbove075']:.2f})")
    if corpus['trimmed']:
        line += (f"\n{'':20s} phase: auto |origin| med {s['originAutoMedianMs']:.0f} ms (<=20 ms {s['originAutoWithin20ms']:.2f})"
                 f" | typed med {s['originTypedMedianMs']:.0f} ms (<=20 ms {s['originTypedWithin20ms']:.2f},"
                 f" >= a quarter off {s['originTypedOffByAQuarterOrMore']:.2f})")
    line += f"\n{'':20s} confidence -> accuracy: {cal}"
    print(line)
    if list_wrong:
        for r in rows:
            if r['kind'] not in ('exact', 'near'):
                print(f"{'':22s}{r['kind']:7s} truth {r['truth']:6.1f} found {r['auto']['bpm']:6.1f} conf {r['auto']['conf']:.2f}  {r['name']}")
    return s


def main():
    args = sys.argv[1:]
    exe = args[args.index('--breaklab') + 1] if '--breaklab' in args else DEFAULT_EXE
    only = args[args.index('--only') + 1] if '--only' in args else None
    json_path = args[args.index('--json') + 1] if '--json' in args else None
    labels = args[args.index('--labels') + 1] if '--labels' in args else None
    if args[0] == 'run':
        run(args[1], args[2], exe, only, labels)
    elif args[0] == 'run-key':
        run_key(args[1], args[2], exe, only, labels)
    elif args[0] == 'key-report':
        key_report(args[1], args[2], json_path, '--list' in args)
    elif args[0] == 'report':
        report(args[1], args[2], json_path, '--list' in args)


if __name__ == '__main__':
    main()

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


def files(corpus):
    if not os.path.isdir(corpus['folder']):
        return []
    out = []
    for name in sorted(os.listdir(corpus['folder'])):
        if not corpus['include'].search(name):
            continue
        m = corpus['bpm'].search(name)
        if m:
            out.append((name, float(m.group(1))))
    return out


def run(corpus_path, out, exe, only):
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
                args = [exe, 'analyze', os.path.join(corpus['folder'], name), '--hits']
                if mode == 'typed':
                    args[3:3] = ['--bpm', f'{bpm:g}']
                result = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', errors='replace')
                with open(target, 'w', encoding='utf-8') as f:
                    f.write(result.stdout)


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
    line = (f"{corpus['id']:20s} n {n:3d} | exact {s['exact']:.2f} <=2% {s['within2pct']:.2f} | half {s['half']:.2f} "
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
    if args[0] == 'run':
        run(args[1], args[2], exe, only)
    elif args[0] == 'report':
        report(args[1], args[2], json_path, '--list' in args)


if __name__ == '__main__':
    main()

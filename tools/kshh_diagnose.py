"""Why the K/S/H transcription errs (kshh_bench output): for every false hit, which stems have a
true onset within 30 ms; for every missed onset, what the analyzer saw there (its onset with the
K / S / H template levels, or no onset at all).
  python tools/kshh_diagnose.py <corpus.tsv> <bench out folder>"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import kshh_bench as kb

HIT = re.compile(r'^(Kick|Snare|HiHat)\s+t=([0-9.]+)', re.M)
ONSET = re.compile(r'^\s+onset ([0-9.]+) K ([0-9.]+) S ([0-9.]+) H ([0-9.]+)', re.M)


def main(corpus_path, out):
    false_why = {l: collections.Counter() for l in kb.LANES}
    miss_why = {l: collections.Counter() for l in kb.LANES}
    for corpus in kb.load_corpus(corpus_path):
        for name, stems in kb.loops(corpus):
            if len(stems) < 3:
                continue
            text = open(os.path.join(out, corpus['id'] + '__' + name + '.txt'), encoding='utf-8', errors='replace').read()
            found = {l: [] for l in kb.LANES}
            for lane, t in HIT.findall(text):
                found[lane].append(float(t))
            truth = {l: [] for l in kb.LANES}
            for lane, t, _ in re.findall(r'^TRUTH (\w+) ([0-9.]+) (-?[0-9.]+)', text, re.M):
                truth[lane].append(float(t))
            onsets = [(float(a), float(k), float(s), float(h)) for a, k, s, h in ONSET.findall(text)]
            near = lambda ts, t: any(abs(u - t) <= kb.TOL for u in ts)
            for lane in kb.LANES:
                for t in found[lane]:
                    if near(truth[lane], t):
                        continue
                    others = [l for l in kb.LANES if l != lane and near(truth[l], t)]
                    false_why[lane]['at a true ' + '+'.join(others) if others else 'no true onset of any lane'] += 1
                for t in truth[lane]:
                    if near(found[lane], t):
                        continue
                    o = [x for x in onsets if abs(x[0] - t) <= kb.TOL]
                    if not o:
                        miss_why[lane]['no analyzer onset'] += 1
                        continue
                    k, s, h = o[0][1:]
                    level = {'Kick': k, 'Snare': s, 'HiHat': h}[lane]
                    band = '< 0.1' if level < 0.1 else '0.1-0.35' if level < 0.35 else '>= 0.35'
                    found_other = [l for l in kb.LANES if l != lane and near(found[l], t)]
                    miss_why[lane][f'onset, own level {band}' + (', read as ' + '+'.join(found_other) if found_other else '')] += 1
    for lane in kb.LANES:
        print(f'{lane}: false hits {sum(false_why[lane].values())}: ' + '; '.join(f'{k} {v}' for k, v in false_why[lane].most_common(5)))
        print(f'{lane}: misses {sum(miss_why[lane].values())}: ' + '; '.join(f'{k} {v}' for k, v in miss_why[lane].most_common(6)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

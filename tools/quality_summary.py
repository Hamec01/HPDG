"""Print a compact table from a HPDG_GenerationQualityLab *_summary.json.

usage: python tools/quality_summary.py docs/audit/baseline/baseline_default_4bars_summary.json [other_summary.json]
       python tools/quality_summary.py --markdown <summary.json>
With two files the second column set is the comparison (before -> after).
"""
import json
import sys

KEYS = ['kicksPerBar', 'backbeatCoverage', 'hatsPerBar', 'barSimilarity', 'eventsPerBar', 'velocityStd']


def load(path):
    data = json.load(open(path, encoding='utf-8'))
    return {(c['genre'], c['substyle'], c['bars'], round(c['density'], 2)): c for c in data['configurations']}


def row(c):
    m = c['metrics']
    return (f"fail {100 * c['hardFailureRate']:5.1f}% | exact dup {100 * c['exactDuplicateRate']:5.1f}% | "
            f"near dup {100 * c['nearDuplicateRate']:5.1f}% | kick dup {100 * c['kickSkeletonDuplicateRate']:5.1f}% | "
            + ' | '.join(f"{k} {m[k]['mean']:.2f}" for k in KEYS)
            + f" | p95 {c['timeMs']['p95']:.1f} ms"
            + (f" | {c['failureKinds']}" if c['failureKinds'] else ''))


if len(sys.argv) > 2 and sys.argv[1] == '--markdown':
    names = {'boombap': 'Boom Bap', 'trap': 'Trap', 'dnb': 'DnB', 'techno': 'Techno'}
    print('| Genre / substyle | fail | exact dup | near dup | kick dup | kicks/bar | backbeat | hats/bar | bar sim. | events/bar | p95 ms |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    for key, c in load(sys.argv[2]).items():
        m = c['metrics']
        print(f"| {names.get(key[0], key[0])} {key[1]} | {100 * c['hardFailureRate']:.1f} % | {100 * c['exactDuplicateRate']:.1f} % | "
              f"{100 * c['nearDuplicateRate']:.1f} % | {100 * c['kickSkeletonDuplicateRate']:.1f} % | {m['kicksPerBar']['mean']:.2f} | "
              f"{m['backbeatCoverage']['mean']:.2f} | {m['hatsPerBar']['mean']:.1f} | {m['barSimilarity']['mean']:.2f} | "
              f"{m['eventsPerBar']['mean']:.1f} | {c['timeMs']['p95']:.1f} |")
    sys.exit(0)

a = load(sys.argv[1])
b = load(sys.argv[2]) if len(sys.argv) > 2 else None
for key in a:
    print(f'{key[0]:8s} {key[1]:20s} ' + row(a[key]))
    if b and key in b:
        print(f'{"":8s} {"  -> after":20s} ' + row(b[key]))

import os, re, sys
base = os.path.dirname(os.path.abspath(__file__))

def read(folder):
    out = {}
    for name in os.listdir(os.path.join(base, folder)):
        if not name.endswith('.txt'):
            continue
        text = open(os.path.join(base, folder, name), encoding='utf-8', errors='replace').read()
        m = re.search(r'Drum break: bpm ([0-9.]+)', text)
        o = re.search(r'origin ([-0-9.]+) ms', text)
        if m:
            out[name[:-4]] = (float(m.group(1)), float(o.group(1)) if o else 0.0)
    return out

def truth(name):
    m = re.search(r'([0-9]+(?:\.[0-9]+)?)\s*B?pm', name, re.I) or re.search(r'^([0-9]+(?:\.[0-9]+)?) BPM', name)
    return float(m.group(1)) if m else None

for corpus in ('1', '2'):
    old, new = read('old' + corpus), read('new' + corpus)
    rows = []
    for name in sorted(old):
        t = truth(name)
        if t is None or name not in new:
            continue
        rows.append((name, t, old[name], new[name]))
    def stats(idx):
        exact = within05 = within2 = octave = 0
        errs = []
        for name, t, o, n in rows:
            v = (o, n)[idx][0]
            rel = abs(v - t) / t
            errs.append(abs(v - t))
            exact += abs(v - t) <= 0.05
            within05 += abs(v - t) <= 0.5
            within2 += rel <= 0.02
            octave += min(abs(v / t - 2), abs(v / t - 0.5)) < 0.03
        errs.sort()
        return f'n {len(rows)} | |err|<=0.05 {exact} | <=0.5 bpm {within05} | <=2% {within2} | octave {octave} | median err {errs[len(errs)//2]:.3f} | mean err {sum(errs)/len(errs):.3f}'
    print(f'corpus {corpus} OLD: ' + stats(0))
    print(f'corpus {corpus} NEW: ' + stats(1))
    changed = [(n, t, o[0], nw[0], o[1], nw[1]) for n, t, o, nw in rows if abs(o[0] - nw[0]) > 1e-6 or abs(o[1] - nw[1]) > 1.0]
    print(f'  changed {len(changed)}:')
    for n, t, ob, nb, oo, no in changed[:40]:
        print(f'    truth {t:7.2f} | old {ob:7.2f} origin {oo:8.1f} | new {nb:7.2f} origin {no:8.1f} | {n[:50]}')

"""Trap reference from the maintainer's MIDI packs: kick, snare / clap and 808 lines (roadmap Phase 2).

usage: python tools/trap_pack_reference.py scan <root> [<root> ...] --out <json>
       python tools/trap_pack_reference.py report <json> [--hpdg <lab notes.csv>] [--md <file>]

scan: every folder under a root whose path says "trap" (not "drill") is read; its MIDI files are
sorted by name into kick / snare (snare, clap) / 808 (808, bass, sub); fills, hats, melodies and
chords are skipped. A folder whose files are byte-identical to an earlier folder (the same pack in
several formats / places) counts once. Notes are exact (no transcription); a bar is 4/4 from the
file start.

Grid. HPDG writes trap with the snare on the 16th 8 (half-time). A kit whose snare sits on 4 / 12 is
in double-time notation: its 16ths are HPDG 32nds. Kits are put on the HPDG grid by their snare;
kits with no clear snare grid are left out of the drum statistics (808 pitch statistics need no grid).
The main snare is the snare / clap file with the largest share on 8; the other snare / clap files
are "extra" snares (HPDG: the ClapGhostSnare lane). Shorter lanes repeat to the kit's longest lane.

HPDG patterns come from `HPDG_GenerationQualityLab --genre trap --notes` (lanes kick, ghostkick,
snare, clapghost, bass) and are measured by the same code.
"""
import collections
import hashlib
import json
import os
import re
import statistics as st
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from midi_drums import read_notes

ROLE_PATTERNS = [
    ('skip', r'fill|hat|hh|open|perc|chord|melod|piano|lead|pad|vox|vocal|fx|crash|ride|cymbal|tom|bell|pluck|keys|synth|arp|impact|riser|shaker|tops|top\b|reverb|rll|roll'),
    ('808', r'808|bass|sub'),
    ('kick', r'kick|kik|\bbd\b'),
    ('snare', r'snare|clap|snr|\bsd\b|rim'),
]


def role_of(name):
    n = name.lower()
    for role, pattern in ROLE_PATTERNS:
        if re.search(pattern, n):
            return role
    return None


def scan(roots):
    kits = collections.OrderedDict()
    seen = set()
    for root in roots:
        for folder, _, files in os.walk(root):
            path = folder.replace('\\', '/').lower()
            if 'trap' not in path or 'drill' in path:
                continue
            kit, digests = {}, []
            for f in sorted(files):
                if not f.lower().endswith('.mid'):
                    continue
                role = role_of(f)
                if role in (None, 'skip'):
                    continue
                p = os.path.join(folder, f)
                data = open(p, 'rb').read()
                try:
                    ppq, notes = read_notes(p)
                except Exception:
                    continue
                if not notes:
                    continue
                digests.append(hashlib.md5(data).hexdigest())
                kit.setdefault(role, []).append(dict(file=f, ppq=ppq, notes=[list(n) for n in notes]))
            if not kit:
                continue
            key = hashlib.md5(''.join(sorted(digests)).encode()).hexdigest()
            if key in seen:
                continue
            seen.add(key)
            kits[folder.replace('\\', '/')] = kit
    return kits


# --------------------------------------------------------------------------------------------- patterns
def entry_notes(entry, scale):
    """[(start 16th, length 16ths, pitch)] on the HPDG grid."""
    q = entry['ppq'] / 4
    return [(on / q * scale, (off - on) / q * scale, p) for on, off, p in entry['notes']]


def grid_scale(snare_entries):
    pos = collections.Counter()
    for e in snare_entries:
        for s, _, _ in entry_notes(e, 1.0):
            pos[int(round(s)) % 16] += 1
    total = sum(pos.values())
    if total < 2:
        return None
    on8, on412 = pos[8] / total, (pos[4] + pos[12]) / total
    if on8 >= 0.4 and on8 > on412:
        return 1.0
    if on412 >= 0.4 and on412 > on8:
        return 0.5
    return None


def share_on_8(entry, scale):
    notes = entry_notes(entry, scale)
    return sum(1 for s, _, _ in notes if int(round(s)) % 16 == 8) / max(1, len(notes))


def kit_pattern(kit):
    """{lane: notes} on the HPDG grid + bars, or None."""
    if 'kick' not in kit or 'snare' not in kit:
        return None
    scale = grid_scale(kit['snare'])
    if scale is None:
        return None
    snares = sorted(kit['snare'], key=lambda e: -share_on_8(e, scale))
    lanes = {'kick': entry_notes(kit['kick'][0], scale), 'snare': entry_notes(snares[0], scale),
             'extra': [n for e in snares[1:] for n in entry_notes(e, scale)]}
    if '808' in kit:
        bass = [e for e in kit['808'] if '808' in e['file'].lower()] or kit['808']
        lanes['bass'] = entry_notes(bass[0], scale)
    lengths = {k: max((s + l for s, l, _ in v), default=0) for k, v in lanes.items()}
    bars = {k: max(1, int(-(-(v - 0.01) // 16))) for k, v in lengths.items()}
    total = max(bars.values())
    for k in list(lanes):
        if bars[k] < total and total % bars[k] == 0 and lanes[k]:
            lanes[k] = [(s + 16 * bars[k] * r, l, p) for r in range(total // bars[k]) for s, l, p in lanes[k]]
    return dict(lanes=lanes, bars=total)


def hpdg_patterns(notes_csv):
    import csv
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    bars = {}
    for r in csv.DictReader(open(notes_csv)):
        key = (r['substyle'], r['seed'])
        lane = {'kick': 'kick', 'snare': 'snare', 'clapghost': 'extra', 'ghostkick': 'ghostkick', 'bass': 'bass'}[r['lane']]
        length = int(r['length']) + (240 if r['glide'] == '1' else 0)
        by[key][lane].append((int(r['gridTick']) / 240, length / 240, int(r['pitch'])))
        bars[key] = int(r['bars'])
    return [dict(lanes=v, bars=bars[k], substyle=k[0]) for k, v in by.items()]


# --------------------------------------------------------------------------------------------- metrics
def bar_sets(notes, bars):
    out = [set() for _ in range(bars)]
    for s, _, _ in notes:
        r = int(round(s))
        if 0 <= r < bars * 16:
            out[r // 16].add(r % 16)
    return out


def drum_metrics(pats):
    m = collections.defaultdict(list)
    prof = {k: [0] * 16 for k in ('kick', 'snare', 'extra', 'bass')}
    nbars = 0
    for p in pats:
        B = p['bars']
        nbars += B
        k = bar_sets(p['lanes'].get('kick', []), B)
        s = bar_sets(p['lanes'].get('snare', []), B)
        x = bar_sets(p['lanes'].get('extra', []), B)
        b = bar_sets(p['lanes'].get('bass', []), B)
        for lane, sets in (('kick', k), ('snare', s), ('extra', x), ('bass', b)):
            for bar in sets:
                for i in bar:
                    prof[lane][i] += 1
        m['kicks/bar'].append(sum(map(len, k)) / B)
        m['kick bar = previous'].append(sum(1 for i in range(1, B) if k[i] == k[i - 1]) / max(1, B - 1))
        m['kick bar = 2 before'].append(sum(1 for i in range(2, B) if k[i] == k[i - 2]) / max(1, B - 2) if B > 2 else 0)
        m['distinct kick bars'].append(len(set(map(frozenset, k))) / B)
        m['kick on the snare'].append(sum(len(a & c) for a, c in zip(k, s)) / B)
        m['kicks in the 2nd half (9-15)'].append(sum(1 for bar in k for i in bar if i >= 9) / max(1, sum(map(len, k))))
        m['kick pairs (16ths in a row)'].append(sum(1 for bar in k for i in bar if i + 1 in bar) / B)
        m['kick off the 8ths'].append(sum(1 for bar in k for i in bar if i % 2) / max(1, sum(map(len, k))))
        m['snares/bar (main)'].append(sum(map(len, s)) / B)
        m['snare on 8'].append(sum(1 for bar in s if 8 in bar) / B)
        m['extra snares/claps/bar'].append(sum(map(len, x)) / B)
        m['extra on the 8 (layer)'].append(sum(1 for bar in x if 8 in bar) / B)
        m['32nd notes (kick+snare+extra)/bar'].append(sum(1 for lane in ('kick', 'snare', 'extra') for st_, _, _ in p['lanes'].get(lane, [])
                                                         if abs(st_ * 2 - round(st_ * 2)) < 0.05 and int(round(st_ * 2)) % 2) / B)
        if 'bass' in p['lanes'] and p['lanes']['bass']:
            on = sum(len(a & c) for a, c in zip(b, k))
            m['808 starts/bar'].append(sum(map(len, b)) / B)
            m['808 on a kick'].append(on / max(1, sum(map(len, b))))
            m['kicks with an 808'].append(on / max(1, sum(map(len, k))))
    for lane in prof:
        prof[lane] = [v / max(1, nbars) for v in prof[lane]]
    return m, prof


def line_metrics(pats):
    m = collections.defaultdict(list)
    iv = collections.Counter()
    deg = collections.Counter()
    for p in pats:
        notes = sorted(p['lanes'].get('bass', []))
        if len(notes) < 2:
            continue
        B = p['bars']
        dur = collections.Counter()
        for s, l, pitch in notes:
            dur[pitch % 12] += l
        tonic = dur.most_common(1)[0][0]
        steps = [b[2] - a[2] for a, b in zip(notes, notes[1:])]
        for x in steps:
            a = abs(x)
            iv['repeat' if a == 0 else 'step 1-2' if a <= 2 else 'third 3-4' if a <= 4 else 'fourth 5' if a == 5 else
               'tritone 6' if a == 6 else 'fifth 7' if a == 7 else 'sixth 8-9' if a <= 9 else 'seventh 10-11' if a <= 11 else
               'octave 12' if a == 12 else 'over an octave'] += 1
        for s, l, pitch in notes:
            deg[(pitch % 12 - tonic) % 12] += 1
        m['808 notes/bar'].append(len(notes) / B)
        m['pitch changes/bar'].append(sum(1 for x in steps if x) / B)
        m['distinct pitches'].append(len(set(n[2] for n in notes)))
        m['span (semitones)'].append(max(n[2] for n in notes) - min(n[2] for n in notes))
        m['time on the tonic'].append(dur[tonic] / max(1e-9, sum(dur.values())))
        m['glides / note'].append(sum(1 for a, b in zip(notes, notes[1:]) if a[0] + a[1] > b[0] + 0.1 and a[2] != b[2]) / (len(notes) - 1))
        m['note length (16ths)'].append(st.median(n[1] for n in notes))
        m['gap fill'].append(st.median(min(1.0, a[1] / max(0.25, b[0] - a[0])) for a, b in zip(notes, notes[1:]) if b[0] > a[0]))
        first_bar = [n[2] for n in notes if n[0] < 16]
        m['line starts on the tonic'].append(1.0 if notes[0][2] % 12 == tonic else 0.0)
        m['last note = tonic'].append(1.0 if notes[-1][2] % 12 == tonic else 0.0)
        pcs_bar = [len(set(n[2] for n in notes if bar * 16 <= n[0] < bar * 16 + 16)) for bar in range(B)]
        m['pitches per bar'].append(st.mean(pcs_bar))
    return m, iv, deg


def fmt_metrics(title_a, a, title_b=None, b=None):
    rows = [f'| measure | {title_a} |' + (f' {title_b} |' if b is not None else ''), '|---|---|' + ('---|' if b is not None else '')]
    for k in a:
        va = a[k]
        cell = lambda v: f'{st.median(v):.2f} (mean {st.mean(v):.2f})' if v else '-'
        rows.append(f'| {k} | {cell(va)} |' + (f' {cell(b.get(k, []))} |' if b is not None else ''))
    return rows


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    if args[0] == 'scan':
        out = args[args.index('--out') + 1]
        roots = [a for i, a in enumerate(args[1:], 1) if not a.startswith('--') and args[i - 1] != '--out']
        kits = scan(roots)
        json.dump(kits, open(out, 'w', encoding='utf-8'))
        roles = collections.Counter(r for k in kits.values() for r in k)
        print(f'folders {len(kits)} | folders with role {dict(roles)} -> {out}')
        return 0
    if args[0] != 'report':
        print(__doc__)
        return 1
    kits = json.load(open(args[1], encoding='utf-8'))
    hp = hpdg_patterns(args[args.index('--hpdg') + 1]) if '--hpdg' in args else None
    out = []
    p = out.append
    ref_pats = [x for x in (kit_pattern(k) for k in kits.values()) if x]
    ref_lines = []
    for k in kits.values():
        for e in k.get('808', []):
            ref_lines.append(dict(lanes={'bass': entry_notes(e, 1.0)},
                                  bars=max(1, int(-(-max(s + l for s, l, _ in entry_notes(e, 1.0)) // 16)))))
    p(f'Kits on the HPDG grid (kick + snare): {len(ref_pats)} of {sum(1 for k in kits.values() if "kick" in k and "snare" in k)}; '
      f'808 / bass lines: {len(ref_lines)}' + (f'; HPDG patterns: {len(hp)}' if hp else ''))
    p('')
    p('### Drums')
    ra, rprof = drum_metrics(ref_pats)
    if hp:
        ha, hprof = drum_metrics(hp)
        out.extend(fmt_metrics('packs', ra, 'HPDG', ha))
    else:
        out.extend(fmt_metrics('packs', ra))
    p('')
    p('| lane | ' + ' | '.join(str(i) for i in range(16)) + ' |')
    p('|---|' + '---|' * 16)
    for lane in ('kick', 'snare', 'extra', 'bass'):
        p(f'| packs {lane} | ' + ' | '.join(f'{x:.2f}' for x in rprof[lane]) + ' |')
        if hp:
            p(f'| HPDG {lane} | ' + ' | '.join(f'{x:.2f}' for x in hprof[lane]) + ' |')
    p('')
    p('### 808 lines')
    rl, riv, rdeg = line_metrics(ref_lines)
    if hp:
        hl, hiv, hdeg = line_metrics(hp)
        out.extend(fmt_metrics('packs', rl, 'HPDG', hl))
    else:
        out.extend(fmt_metrics('packs', rl))
    p('')
    for title, iv, deg in [('packs', riv, rdeg)] + ([('HPDG', hiv, hdeg)] if hp else []):
        n, dn = sum(iv.values()), sum(deg.values())
        p(f'- {title} intervals: ' + ', '.join(f'{k} {v / n:.2f}' for k, v in iv.most_common()))
        p(f'- {title} scale degrees over the tonic: ' + ', '.join(f'{k} {v / dn:.2f}' for k, v in sorted(deg.items(), key=lambda x: -x[1])))
    print('\n'.join(out))
    if '--md' in args:
        open(args[args.index('--md') + 1], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())

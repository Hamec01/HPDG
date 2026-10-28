"""ATL trap passport: what makes "Atlanta / Young Thug type beats" (drums + 808), in Trap's own bar.

usage: python tools/atl_passport.py <passport out folder> [--md out.md]

Input: the raw reports of `tools/atmos_passport.py run` (HPDG_BreakLab on Demucs drums / bass
stems, two 40 s windows per track). Trap is written in a half-time bar around 150 BPM (snare on
beat 3); a drum stem read at 70-85 BPM has two of those bars in each of its bars. Every hit is
placed on that "trap bar" by its time (not by the analyzer's 16th), so rolls and triplets show.

Kept: tracks whose tempo reading puts the snare on the trap backbeat (step 9 of the trap bar in
>= 50 % of the bars that have a snare); the others are listed as unreliable and left out.

Hits with confidence >= 0.5 (kick / snare) and >= 0.35 (hats: masked under the snare).
"""
import collections
import json
import os
import re
import statistics as st
import sys

HIT = re.compile(r'^(Kick|Snare|HiHat|OpenHat|Perc|Clap)\s+t=([0-9.]+) .* vel (\d+) conf ([0-9.]+)', re.M)
BASS = re.compile(r'bass line \(\d+\): (.*)')
NOTE = re.compile(r'([A-G]#?)(-?\d)@([0-9.]+)\+([0-9.]+)')
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def style_bpm(bpm):
    """The octave nearest to 155 (HPDG's Trap reference, TrapTempoContext)."""
    best = bpm
    for k in (0.5, 1, 2):
        if abs(__import__('math').log2(bpm * k / 155)) < abs(__import__('math').log2(best / 155)):
            best = bpm * k
    return best


def windows(folder, name):
    for w in (0, 1):
        d = os.path.join(folder, f'{name}_{w}_drums.txt')
        b = os.path.join(folder, f'{name}_{w}_bass.txt')
        if os.path.exists(d):
            yield open(d, encoding='utf-8').read(), open(b, encoding='utf-8').read() if os.path.exists(b) else ''


def origin_ms(text):
    m = re.search(r'origin (-?[0-9.]+) ms', text)
    return float(m.group(1)) / 1000 if m else 0.0


def analyse(folder, meta):
    bpm = style_bpm(meta['bpm'])
    sixteenth = 60 / bpm / 4
    bar_s = 16 * sixteenth
    out = collections.defaultdict(list)
    hats_bars, kick_pos, snare_pos = [], collections.Counter(), collections.Counter()
    ioi = collections.Counter()
    hat_vel = []
    bass = []
    bars_total = 0
    snare_bars = backbeat_bars = 0
    for drums, basstext in windows(folder, meta['name']):
        org = origin_ms(drums)
        hits = [(m.group(1), float(m.group(2)) - org, int(m.group(3)), float(m.group(4))) for m in HIT.finditer(drums)]
        if not hits:
            continue
        end = max(h[1] for h in hits)
        nbars = int(end // bar_s)
        bars_total += nbars
        per_bar = collections.defaultdict(lambda: collections.defaultdict(list))
        for lane, t, vel, conf in hits:
            if t < 0 or t >= nbars * bar_s:
                continue
            if lane in ('Kick', 'Snare') and conf < 0.5:
                continue
            if lane == 'HiHat' and conf < 0.35:
                continue
            bar = int(t // bar_s)
            per_bar[bar][lane].append((t - bar * bar_s, vel))
        hat_times = sorted(t for lane, t, v, c in hits if lane == 'HiHat' and c >= 0.35 and t >= 0)
        for a, b in zip(hat_times, hat_times[1:]):
            r = (b - a) / sixteenth  # in 16ths of the trap bar
            for label, val in (('1/32', 0.5), ('1/16T', 2 / 3), ('1/16', 1.0), ('1/8T', 4 / 3), ('1/8', 2.0), ('1/64', 0.25)):
                if abs(r - val) <= 0.08 * val + 0.03:
                    ioi[label] += 1
                    break
            else:
                ioi['other'] += 1
        for bar in range(nbars):
            lanes = per_bar[bar]
            snares = {round(p / sixteenth) % 16 for p, _ in lanes['Snare']}
            if snares:
                snare_bars += 1
                backbeat_bars += 8 in snares
            for p, _ in lanes['Kick']:
                kick_pos[round(p / sixteenth) % 16] += 1
            for p, _ in lanes['Snare']:
                snare_pos[round(p / sixteenth) % 16] += 1
            hats_bars.append(len(lanes['HiHat']))
            hat_vel += [v for _, v in lanes['HiHat']]
        m = BASS.search(basstext)
        if m:
            for nm in NOTE.finditer(m.group(1)):
                pitch = NAMES.index(nm.group(1)) + 12 * (int(nm.group(2)) + 1)
                bass.append((float(nm.group(3)), float(nm.group(4)), pitch))
    return dict(bpm=meta['bpm'], style=bpm, bars=bars_total, backbeat=backbeat_bars / snare_bars if snare_bars else 0,
                kick_pos=kick_pos, snare_pos=snare_pos, hats_bars=hats_bars, ioi=ioi, hat_vel=hat_vel,
                bass=bass, sixteenth=sixteenth)


def bass_stats(r):
    notes = sorted(r['bass'])
    if len(notes) < 4:
        return None
    s16 = r['sixteenth']
    lengths = [d / (4 * s16) for _, d, _ in notes]  # in beats of the trap bar
    steps = [b[2] - a[2] for a, b in zip(notes, notes[1:])]
    octave = sum(abs(x) == 12 for x in steps) / len(steps)
    big = sum(abs(x) > 12 for x in steps) / len(steps)
    repeat = sum(x == 0 for x in steps) / len(steps)
    fifth = sum(abs(x) in (5, 7) for x in steps) / len(steps)
    short_hops = sum(1 for a, b in zip(notes, notes[1:]) if b[0] - a[0] <= 2 * s16 and b[2] != a[2]) / len(steps)
    span = (notes[-1][0] - notes[0][0]) / (16 * s16)
    return dict(per_bar=len(notes) / max(1, span), length=st.median(lengths), long=sum(l >= 2 for l in lengths) / len(lengths),
                repeat=repeat, octave=octave, big=big, fifth=fifth, hops=short_hops,
                register=st.median(p for _, _, p in notes))


def main():
    folder = sys.argv[1]
    md = sys.argv[sys.argv.index('--md') + 1] if '--md' in sys.argv else None
    lines = []
    p = lines.append
    kept, dropped = [], []
    for f in sorted(os.listdir(folder)):
        if not f.endswith('.json'):
            continue
        meta = json.load(open(os.path.join(folder, f), encoding='utf-8'))
        r = analyse(folder, meta)
        r['name'] = meta['name']
        (kept if r['backbeat'] >= 0.5 else dropped).append(r)
    p(f'tracks {len(kept) + len(dropped)}: kept {len(kept)} (snare on trap beat 3), unreliable tempo {len(dropped)}')
    for r in dropped:
        p(f'- unreliable: {r["name"][:60]} (read {r["bpm"]:.0f} bpm, snare on 3 in {r["backbeat"]:.0%} of bars)')
    p('')
    p('| track | read bpm | trap bpm | bars | snare on 3 | kicks/bar | hats/bar | hat vel sd | 808/bar | 808 len beats | 808 >= 2 beats | repeat | octave | > octave | 4th/5th | quick moves | 808 median |')
    p('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for r in kept:
        b = bass_stats(r) or {}
        p('| %s | %.0f | %.0f | %d | %.2f | %.2f | %.1f | %.1f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %s |' % (
            r['name'][:44], r['bpm'], r['style'], r['bars'], r['backbeat'], sum(r['kick_pos'].values()) / max(1, r['bars']),
            st.mean(r['hats_bars']) if r['hats_bars'] else 0, st.pstdev(r['hat_vel']) if len(r['hat_vel']) > 1 else 0,
            b.get('per_bar', 0), b.get('length', 0), b.get('long', 0), b.get('repeat', 0), b.get('octave', 0), b.get('big', 0),
            b.get('fifth', 0), b.get('hops', 0), NAMES[int(b['register']) % 12] + str(int(b['register']) // 12 - 1) if b else '-'))
    bars = sum(r['bars'] for r in kept)
    p('')
    p(f'kept bars {bars}. Position share per trap bar (16ths 1-16):')
    p('')
    p('| lane | ' + ' | '.join(str(i) for i in range(1, 17)) + ' | per bar |')
    p('|---|' + '---|' * 17)
    for lane in ('kick_pos', 'snare_pos'):
        c = collections.Counter()
        for r in kept:
            c.update(r[lane])
        p(f'| {lane[:-4]} | ' + ' | '.join('%.2f' % (c[i] / bars) for i in range(16)) + ' | %.2f |' % (sum(c.values()) / bars))
    ioi = collections.Counter()
    for r in kept + dropped:
        ioi.update(r['ioi'])
    total = sum(ioi.values())
    p('')
    p('hat gaps (all tracks, by time): ' + ', '.join(f'{k} {ioi[k] / total:.2f}' for k in ('1/64', '1/32', '1/16T', '1/16', '1/8T', '1/8', 'other')))
    text = '\n'.join(lines)
    print(text)
    if md:
        open(md, 'w', encoding='utf-8').write(text + '\n')


if __name__ == '__main__':
    main()

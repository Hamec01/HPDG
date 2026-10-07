"""Kick / 808 reference for Trap (roadmap Phase 2): where the 808 starts relative to the kick.

usage: python tools/trap_808_reference.py <GH Trap Kit root> <Cr2 Trippy Trap root> <Cr2 kick&snare reports> [out.tsv]
       python tools/trap_808_reference.py --stats [tsv]

Pairs (no audio in git, derived positions only):
- Ghosthack UPB2020 GH Trap Kit NN: "Drums Kick.mid" + "*808*.mid" (exact notes);
- Sample Tools by Cr2 Trippy Trap Drum Loops: "MIDI/TT_Drum_Loop_NN_808_BPM*.mid" + the kick of
  "Kick & Snare/TT_Drum_Loop_NN_KicknSnare_BPM*.wav" transcribed with
  `HPDG_BreakLab analyze <wav> --bpm <BPM> --hits` (reports named cr2_NN_BPM.txt), kick 16th =
  round(t / 16th) from the file start; the 808 MIDI is aligned to the audio by whole bars (it is
  exported from an arrangement position).
Line: name<TAB>kick positions per bar<TAB>808 positions per bar (16ths, "|" between bars).
--stats: 808 starts on a kick, 808 starts per bar, own-808 positions and 16ths since the previous kick.
"""
import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from midi_drums import read_onsets

OUT = 'docs/audit/reference/trap_808_bars.tsv'


def bars_text(steps, n):
    bars = [set() for _ in range(n // 16)]
    for s in steps:
        bars[(s % n) // 16].add(s % 16)
    return '|'.join(' '.join(map(str, sorted(b))) for b in bars)


def build(gh_root, cr2_root, cr2_reports, out):
    rows = []
    for d in sorted(glob.glob(os.path.join(gh_root, 'GH Trap Kit *'))):
        kick = glob.glob(os.path.join(d, '02 MIDI', '*Drums Kick.mid'))
        bass = glob.glob(os.path.join(d, '02 MIDI', '*808*.mid'))
        if not kick or not bass:
            continue
        pk, nk = read_onsets(kick[0])
        pb, nb = read_onsets(bass[0])
        ks = {round(t / (pk / 4)) for t, _, _ in nk}
        bs = {round(t / (pb / 4)) for t, _, _ in nb}
        n = -(-(max(ks | bs) + 1) // 16) * 16
        kit = re.search(r'GH Trap Kit (\d+)', d).group(1)
        rows.append((f'ghkit_{kit}_midi', bars_text(ks, n), bars_text(bs, n)))
    for path in sorted(glob.glob(os.path.join(cr2_root, 'Audio_MIDI', 'MIDI', '*.mid'))):
        num, bpm = re.search(r'Loop_(\d+)_808_(\d+)', path).groups()
        report = open(os.path.join(cr2_reports, f'cr2_{num}_{bpm}.txt'), encoding='utf-8-sig', errors='replace').read()
        sixteenth = 60 / float(bpm) / 4
        n = round(float(re.search(r'length ([0-9.]+) s', report).group(1)) / sixteenth)
        ks = {int(round(float(m.group(1)) / sixteenth)) % n for m in re.finditer(r'^Kick\s+t=([0-9.]+)', report, re.M)}
        ppq, notes = read_onsets(path)
        raw = {round(t / (ppq / 4)) for t, _, _ in notes}
        shift = max(range(-16, 17, 16), key=lambda s: len(ks & {(b + s) % n for b in raw}))
        rows.append((f'cr2_{num}_{bpm}', bars_text(ks, n), bars_text({(b + shift) % n for b in raw}, n)))
    with open(out, 'w', encoding='utf-8') as f:
        f.write('# Trap kick / 808 reference: name, kick 16ths per bar, 808-start 16ths per bar ("|" between bars).\n'
                '# Rebuild and statistics: tools/trap_808_reference.py. See docs/audit/TRAP_STAGE.md step 4.\n')
        for row in rows:
            f.write('\t'.join(row) + '\n')
    print(len(rows), 'pairs ->', out)


def stats(path):
    on = own = bars = 0
    pos, gap = collections.Counter(), collections.Counter()
    for line in open(path, encoding='utf-8'):
        if line.startswith('#'):
            continue
        _, kick, bass = line.rstrip('\n').split('\t')
        kb, bb = kick.split('|'), bass.split('|')
        n = 16 * len(kb)
        ks = {16 * i + int(x) for i, b in enumerate(kb) for x in b.split()}
        bs = {16 * i + int(x) for i, b in enumerate(bb) for x in b.split()}
        bars += len(kb)
        for b in bs:
            if b in ks:
                on += 1
                continue
            own += 1
            pos[b % 16] += 1
            if ks:
                gap[min((b - k) % n for k in ks)] += 1
    total = on + own
    print(f'808 starts {total}: on a kick {on / total:.2f}, own {own / total:.2f} ({own / bars:.2f} a bar), {total / bars:.2f} a bar')
    print('own-808 16th:', ' '.join(f'{s}:{pos[s]}' for s in range(16)))
    print('16ths since the previous kick (<= 8):', ' '.join(f'{g}:{gap[g]}' for g in range(1, 9)))


if __name__ == '__main__':
    if sys.argv[1] == '--stats':
        stats(sys.argv[2] if len(sys.argv) > 2 else OUT)
    else:
        build(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else OUT)

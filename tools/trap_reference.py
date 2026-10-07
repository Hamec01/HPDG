"""Rebuild docs/audit/reference/trap_kick_bars.tsv (Trap kick reference, no audio in git).

usage: python tools/trap_reference.py <GH Trap Kit root> <stem reports folder> [out.tsv]

<GH Trap Kit root>: folder holding "GH Trap Kit NN - ..." (Ghosthack UPB2020 Hip Hop Kits); each kit's
"02 MIDI/*Drums Kick.mid" is read exactly (tools/midi_drums.py).
<stem reports folder>: `HPDG_BreakLab analyze <kick stem> --bpm <label> --hits` reports named
<set>_<nn>_<bpm>.txt (kick stems of Ghosthack Urban / Hybrid Trap Essentials and Sonic Mechanics UTT2
drum loops). Every onset of the stem is a kick; its 16th is round(t / 16th) from the file start
(the loops are trimmed to the bar - the report's own bar / origin puts beat 1 on the first hit).
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from midi_drums import bars_of


def stem_bars(path):
    text = open(path, encoding='utf-8-sig', errors='replace').read()
    bpm = float(re.search(r'bpm +([0-9.]+)', text).group(1))
    length = float(re.search(r'length ([0-9.]+) s', text).group(1))
    sixteenth = 60 / bpm / 4
    nbars = max(1, int(round(length / (sixteenth * 16))))
    bars = [set() for _ in range(nbars)]
    for hit in re.finditer(r'^(Kick|Snare|HiHat)\s+t=([0-9.]+)', text, re.M):
        step = int(round(float(hit.group(2)) / sixteenth)) % (nbars * 16)
        bars[step // 16].add(step % 16)
    return bars


def main():
    kits, stems = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else 'docs/audit/reference/trap_kick_bars.tsv'
    rows = []
    for path in sorted(glob.glob(os.path.join(kits, 'GH Trap Kit *', '02 MIDI', '*Drums Kick.mid'))):
        kit = re.search(r'GH Trap Kit (\d+)', path).group(1)
        rows.append((f'ghkit_{kit}_midi', bars_of(path)))
    for name in sorted(os.listdir(stems)):
        if name.endswith('.txt'):
            rows.append((name[:-4] + '_stem', stem_bars(os.path.join(stems, name))))
    with open(out, 'w', encoding='utf-8') as f:
        f.write('# Trap kick reference: 16th positions per bar (0-15, snare on 8), "|" between bars.\n'
                '# *_midi: Ghosthack UPB2020 GH Trap Kit NN "Drums Kick.mid" (exact notes); *_stem: kick stems of\n'
                '# Ghosthack Urban / Hybrid Trap Essentials and Sonic Mechanics UTT2 drum loops, HPDG_BreakLab analyze\n'
                '# --bpm <label from the file name> --hits, every onset of the kick stem, step = round(t / 16th) from the\n'
                '# file start (the loops are trimmed to the bar). Rebuild: tools/trap_reference.py. See docs/audit/TRAP_STAGE.md.\n')
        for name, bars in rows:
            f.write(name + '\t' + '|'.join(' '.join(map(str, sorted(b))) for b in bars) + '\n')
    print(len(rows), 'loops ->', out)


if __name__ == '__main__':
    main()

# Trap packs vs HPDG Trap (step 7, 2026-10-14)

`python tools/trap_pack_reference.py scan D:/Drums --out refs.json` then `report refs.json --hpdg <lab notes.csv>`; HPDG = 300 seeds x 6 substyles after step 7. Packs: 39 trap folders (HamloProd, GH Trap Kit 01-10, Dirty Trap Vocal Kits, Progressive Trap 3, Origin Sound Unorthodox Trap 2 / Warfare, Sonic Mechanics UTT2). Medians, means in brackets.

Kits on the HPDG grid (kick + snare): 12 of 14; 808 / bass lines: 70; HPDG patterns: 1800

### Drums
| measure | packs | HPDG |
|---|---|---|
| kicks/bar | 2.75 (mean 2.98) | 2.50 (mean 2.43) |
| kick bar = previous | 0.00 (mean 0.11) | 0.00 (mean 0.01) |
| kick bar = 2 before | 0.30 (mean 0.30) | 0.50 (mean 0.33) |
| distinct kick bars | 0.25 (mean 0.28) | 0.75 (mean 0.83) |
| kick on the snare | 0.00 (mean 0.18) | 0.00 (mean 0.00) |
| kicks in the 2nd half (9-15) | 0.47 (mean 0.48) | 0.40 (mean 0.39) |
| kick pairs (16ths in a row) | 0.00 (mean 0.18) | 0.00 (mean 0.00) |
| kick off the 8ths | 0.00 (mean 0.03) | 0.00 (mean 0.01) |
| snares/bar (main) | 1.00 (mean 1.16) | 1.25 (mean 1.40) |
| snare on 8 | 1.00 (mean 0.82) | 1.00 (mean 1.00) |
| extra snares/claps/bar | 1.00 (mean 1.21) | 0.00 (mean 0.00) |
| extra on the 8 (layer) | 0.75 (mean 0.54) | 0.00 (mean 0.00) |
| 32nd notes (kick+snare+extra)/bar | 0.00 (mean 0.33) | 0.00 (mean 0.00) |
| 808 starts/bar | 2.12 (mean 2.24) | 2.25 (mean 2.16) |
| 808 on a kick | 0.68 (mean 0.71) | 0.82 (mean 0.82) |
| kicks with an 808 | 0.52 (mean 0.52) | 0.75 (mean 0.72) |

| lane | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| packs kick | 0.45 | 0.00 | 0.09 | 0.00 | 0.33 | 0.03 | 0.30 | 0.00 | 0.03 | 0.00 | 0.37 | 0.00 | 0.40 | 0.03 | 0.31 | 0.07 |
| HPDG kick | 0.53 | 0.00 | 0.14 | 0.00 | 0.37 | 0.00 | 0.42 | 0.00 | 0.00 | 0.03 | 0.36 | 0.00 | 0.28 | 0.00 | 0.30 | 0.00 |
| packs snare | 0.01 | 0.00 | 0.03 | 0.00 | 0.00 | 0.00 | 0.03 | 0.00 | 0.89 | 0.00 | 0.03 | 0.00 | 0.00 | 0.00 | 0.03 | 0.00 |
| HPDG snare | 0.00 | 0.00 | 0.15 | 0.00 | 0.02 | 0.00 | 0.08 | 0.00 | 1.00 | 0.00 | 0.05 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 |
| packs extra | 0.01 | 0.00 | 0.20 | 0.00 | 0.01 | 0.00 | 0.11 | 0.00 | 0.21 | 0.00 | 0.06 | 0.03 | 0.01 | 0.01 | 0.16 | 0.00 |
| HPDG extra | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| packs bass | 0.42 | 0.01 | 0.00 | 0.00 | 0.22 | 0.01 | 0.11 | 0.00 | 0.04 | 0.00 | 0.25 | 0.01 | 0.25 | 0.03 | 0.05 | 0.00 |
| HPDG bass | 0.49 | 0.00 | 0.17 | 0.00 | 0.30 | 0.00 | 0.34 | 0.00 | 0.00 | 0.02 | 0.28 | 0.00 | 0.26 | 0.00 | 0.28 | 0.00 |

### 808 lines
| measure | packs | HPDG |
|---|---|---|
| 808 notes/bar | 2.29 (mean 2.36) | 2.25 (mean 2.16) |
| pitch changes/bar | 1.00 (mean 1.25) | 1.00 (mean 1.03) |
| distinct pitches | 4.00 (mean 4.90) | 4.00 (mean 3.98) |
| span (semitones) | 14.00 (mean 19.61) | 15.00 (mean 14.37) |
| time on the tonic | 0.61 (mean 0.63) | 0.59 (mean 0.63) |
| glides / note | 0.00 (mean 0.12) | 0.11 (mean 0.12) |
| note length (16ths) | 6.00 (mean 7.88) | 5.50 (mean 5.97) |
| gap fill | 1.00 (mean 0.93) | 1.00 (mean 0.95) |
| line starts on the tonic | 1.00 (mean 0.75) | 1.00 (mean 0.80) |
| last note = tonic | 0.00 (mean 0.46) | 0.00 (mean 0.40) |
| pitches per bar | 1.50 (mean 1.59) | 1.50 (mean 1.53) |

- packs intervals: repeat 0.44, third 3-4 0.13, octave 12 0.10, step 1-2 0.09, fourth 5 0.08, over an octave 0.08, fifth 7 0.06, sixth 8-9 0.01, seventh 10-11 0.01, tritone 6 0.01
- packs scale degrees over the tonic: 0 0.54, 5 0.10, 7 0.08, 8 0.06, 10 0.04, 11 0.04, 4 0.04, 1 0.03, 9 0.03, 3 0.02, 2 0.02, 6 0.01
- HPDG intervals: repeat 0.46, octave 12 0.11, fourth 5 0.11, third 3-4 0.09, step 1-2 0.06, fifth 7 0.04, sixth 8-9 0.04, seventh 10-11 0.03, over an octave 0.03, tritone 6 0.00
- HPDG scale degrees over the tonic: 0 0.56, 5 0.10, 7 0.08, 10 0.07, 3 0.06, 8 0.05, 2 0.04, 9 0.02, 4 0.01, 11 0.00, 1 0.00, 6 0.00

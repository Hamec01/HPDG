# Boom Bap stage (roadmap §51, stage 2)

Each step: one hypothesis → the smallest change → the same benchmarks before / after
(RULES 2, 4, 39, 41). Results in `docs/audit/boombap/`.

## Diagnosis

Phase 0: 25–56 % exact duplicate patterns at 2 bars and low density (0 % at 4 / 8 bars).
Per-layer hashes (`GenerationQualityLab` CSV) showed why:

- the **groove core (bars 1–2) is poor at every length**: 60–120 distinct kick patterns in
  bars 1–2 per 300 seeds at 2, 4 and 8 bars alike; at 4 / 8 bars the variation of bars 3+
  hides it, at 2 bars nothing does;
- the snare layer has exactly 1 pattern at 2 bars (backbeat only, by design);
- at density 0.2 the hats collapse too (Classic: 39 distinct hat layers per 300 seeds).

Mechanism: bar 1's kick is one of 13 seeded motifs (`kickMotifs`), candidates only articulate it
and the selection rewards motif fidelity.

## Reference corpus

159 trimmed boom bap loops with labeled tempo (`D:/Drums/Boombap/2 GB OF FREE SAMPLES/
02_CUSTOM_DRUM_LOOPS`), transcribed with `HPDG_BreakLab analyze --bpm <label> --hits`
(kicks with confidence ≥ 0.5), split 70 / 30 by an MD5 hash of the file name into calibration
(114 loops, 424 bars) and validation (45 loops, 171 bars). Tool: `tools/reference_kicks.py`.

Kick probability per 16th (per bar):

| 16th | 0 | 3 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 13 | 15 | kicks/bar |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| reference, calibration | 0.88 | 0.35 | 0.21 | 0.36 | 0.16 | 0.36 | 0.06 | 0.48 | 0.36 | 0.31 | 0.26 | 4.00 |
| reference, validation | 0.92 | 0.36 | 0.15 | 0.24 | 0.40 | 0.60 | 0.16 | 0.33 | 0.53 | 0.23 | 0.35 | 4.57 |
| HPDG Classic, before | 0.99 | 0.11 | 0.07 | 0.16 | 0.30 | 0.17 | 0.29 | 0.40 | 0.10 | 0.13 | 0.35 | 3.40 |
| HPDG Classic, after step 1 | 0.99 | 0.20 | 0.13 | 0.22 | 0.25 | 0.22 | 0.20 | 0.38 | 0.16 | 0.25 | 0.20 | 3.42 |

Both splits agree: the played loops use the "a" after beat 1 (3), beat 3 (8), the "a" before the
snare (11) and 13 far more, and 9 far less, than HPDG.

## Step 1 — reference kick motifs (accepted)

**Hypothesis.** The kick-motif vocabulary under-represents 3 / 8 / 11 / 13; adding the most
frequent bar patterns of the calibration split moves the kick profile towards played loops
(measured on the validation split), widens the groove core and does not hurt validity / scorer
quality / runtime.

**Change.** 12 motifs appended to `kickMotifs` (13 → 25), the 12 most frequent calibration bar
patterns that were not already in the table; the original 13 stay (RULE 45).

| Measure (same seeds / settings) | Before | After |
|---|---|---|
| kick position L1 distance to **validation** | 1.79–1.88 | **1.54** (Classic) |
| validation bars HPDG can play | 30–35 % | **39 %** (Classic) |
| kick position L1 distance to calibration (tuned on it) | 1.96–2.11 | 1.32 (Classic) |
| `BoomBapBatchAudit 1000`: avg quality / hard trap / missing backbeats / ghost violations / determinism | 9.8215 / 0 / 0 / 0 / 0 | 9.8221 / 0 / 0 / 0 / 0 |
| `BoomBapBatchAudit 1000`: p50 / p95 ms | 6.67 / 7.79 | 6.59 / 7.73 |
| product lab 1000 seeds, 4 bars: hard failures / exact duplicates | 0 % / 0 % | 0 % / 0 % |
| product lab: kick-skeleton reuse, 4 bars (6 substyles) | 1.6–2.8 % | 1.5–4.5 % |
| 2 bars, exact duplicates at density 0.2 (Classic / Dusty / Jazzy / Gold / Russian / Lofi) | 55.7 / 26.0 / 26.3 / 47.3 / 24.7 / 29.0 % | 53.0 / 21.0 / 21.7 / 43.3 / 15.3 / 23.3 % |
| 2 bars, exact duplicates at default density | 12.3 / 5.0 / 5.0 / 8.0 / 19.7 / 29.0 % | 9.0 / 3.7 / 2.3 / 7.0 / 16.0 / 20.3 % |
| lane / core tests | pass | pass |

Notes: the kick-skeleton reuse at 4 bars rose by 1.4–1.6 points in three substyles (within the
1000-seed noise, sparser new motifs such as `{0, 40}` repeat more); kicks/bar did not move
(3.42) — the reference plays 4.0–4.6, the density / kick-count side is a separate hypothesis.
Low-density 2-bar duplicates remain high for Classic / Gold → step 2 looks at the hat layer.

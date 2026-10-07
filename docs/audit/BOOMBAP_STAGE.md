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

## Measurement fix (lab step reading) — before step 2

Boom Bap notes carry their micro-timing inside `gridTick` (`BoomBapEngine.cpp`, `stepIndexOf`:
up to a 1/64 early belongs to the next 16th). `Tests/QualityMetrics.h` floored `gridTick`, so every
early hat / kick was read a 16th early (an early off-beat hat on 2 → 1, an early downbeat of the
next bar → 15). The lab now reads Boom Bap steps the way the engine does (other genres unchanged,
their notes stay on the grid). Exact-duplicate / skeleton hashes are unaffected (53.0 % Classic,
2 bars, density 0.2, before and after the fix).

Effect on earlier Boom Bap numbers (same code, 1000 seeds, default density, 4 bars):

| Measure | floored reading | engine reading |
|---|---|---|
| hat odd-16th probability per position | 0.14–0.29 | **0.05–0.13** (reference 0.00–0.15) |
| distinct hat bar patterns, Classic | 1479 / 4000 bars | 698 / 4000 bars |
| kick-skeleton reuse, 4 bars (6 substyles) | 1.5–4.5 % | **8.3–11.6 %** |
| kick L1 distance to validation, Classic | 1.54 | 1.71 |

Step 1 re-checked under the engine reading (motif table temporarily reverted, same seeds):

| Measure (6 substyles) | before step 1 | after step 1 |
|---|---|---|
| kick L1 distance to validation | 1.80–1.94 | **1.65–1.76** |
| validation bars HPDG can play | 35.1–40.4 % | 37.4–44.4 % (Classic 39.2 → 38.0) |

Step 1 stays accepted (the profile moved towards the held-out loops in every substyle); the
earlier "34 → 39 % playable" claim was a measurement artifact and is replaced by the row above.
New reference summaries: `docs/audit/boombap/measure_v2_*_summary.json`.

## Step 2 — hats (measured, no change)

Hypothesis from the floored reading: HPDG plays too many odd 16ths. **Rejected** — it was the
reading. With the engine reading, compared only where the bar has no kick / snare (masked hats
fall under confidence 0.5 in the transcription; `tools/reference_hats.py`):

| hat probability, unmasked | 2 | 6 | 10 | 14 | odd 16ths | all free off-beats |
|---|---|---|---|---|---|---|
| reference calibration | 0.80 | 0.69 | 0.84 | 0.67 | 0.00–0.10 | 0.52 |
| reference validation | 0.86 | 0.70 | 0.77 | 0.64 | 0.00–0.15 | 0.47 |
| HPDG Classic | 0.90 | 0.89 | 0.88 | 0.92 | 0.08–0.10 | 0.76 |
| HPDG Gold | 0.89 | 0.89 | 0.85 | 0.91 | 0.09–0.11 | 0.74 |
| HPDG Dusty / Jazzy / Lofi / Russian | 0.82–0.85 | 0.80–0.84 | 0.79–0.81 | 0.83–0.86 | 0.04–0.13 | 0.55–0.64 |

Remaining gap: Classic / Gold play every free off-beat 8th more often than played loops (0.75 vs
≈ 0.5). Not changed: the transcription's recall of soft off-beat hats is not measured (RULE 30),
so part of the gap may be missed hats, and the gap is within ≈ 0.1–0.25 per position. Needs a
hat-recall check on labeled loops (stage 7) before it can justify a generator change (RULE 21).

2-bar, density-0.2 duplicates (Classic 53 %, Gold 43 %): Classic has 98 distinct kick and 37
distinct hat layers per 300 seeds (Classic `hatEighthDropout` = 0, bar 2 cloned from bar 1);
straight 8ths are genre-true, so the lever is not the odd-16th rate.


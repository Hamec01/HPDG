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

## Step 3 — kick count: motif cap + band-shaped kick target (accepted, pending listening)

**Finding.** Played loops spread over 3–5 kicks a bar (calibration: 3 = 19 %, 4 = 38 %, 5 = 26 %,
6+ = 7 %); HPDG almost never played 5 (1–5 % of bars) and averaged 3.2–3.7 vs 4.0–4.6. Ghost kicks
do not close the gap (+0.05–0.14 / bar; lab column `kickAllBars`, `reference_kicks.py --lane kickall`).
Two causes: a bar could not hold more main kicks than its motif (`maxMainKicks = max(3, motifSize)`),
and the scorer's kick term `close(kicks, 3 + 1.5·density)` peaks on one count, so the 64 candidates
converge on it — below the motif size it drops a random motif kick (variety that under-plays kicks),
at the motif size every seed plays its bare motif (25 outcomes).

**Change.** `maxMainKicks = max(3, motifSize + 1)` (one articulation kick may join a full motif);
kick term `band(kicks / bar, 3.0 + 0.8·d, 4.0 + 1.0·d, 0.75)` instead of the peaked target.

Variants measured and rejected (same seeds / benchmarks):

| Variant | 2-bar exact dup, sum of 18 cells | Classic 2-bar default dup | kicks/bar (6 substyles) | note |
|---|---|---|---|---|
| baseline | 294 | 9.0 % | 3.23–3.65 | |
| peaked target 3.5 + 1.2·d | 227 | 30.0 % | 3.61–3.84 | converges on the bare motif; density ≈ no effect above default |
| motif + 1 cap only | 291 | 9.0 % | 3.23–3.67 | scorer pulls the extra kick back out |
| peaked target + cap | 214 | 29.0 % | 3.65–3.87 | as above |
| band around old centre 2.5 + 1.5·d … 3.5 + 1.5·d, + cap | 207 | 17.3 % | 3.34–3.66 | kick profile barely moves |
| **band 3.0 + 0.8·d … 4.0 + 1.0·d, + cap** | **182** | 15.3 % | 3.57–3.73 | accepted |

Accepted variant, before → after (1000 seeds, defaults, 4 bars unless noted; validation split
not used for tuning):

| Measure | Before | After |
|---|---|---|
| kicks/bar (Classic / Dusty / Jazzy / Gold / Russian / Lofi) | 3.44 / 3.45 / 3.64 / 3.65 / 3.25 / 3.23 | 3.65 / 3.61 / 3.73 / 3.70 / 3.58 / 3.57 |
| bars with 5 kicks | 0.8–4.8 % | 6.4–13.1 % (reference 25–26 %) |
| kick L1 distance to **validation** | 1.71 / 1.69 / 1.65 / 1.66 / 1.76 / 1.76 | 1.63 / 1.62 / 1.63 / 1.64 / 1.64 / 1.63 |
| validation bars HPDG can play | 38.0 / 40.4 / 39.8 / 44.4 / 37.4 / 37.4 % | 45.6 / 37.4 / 46.2 / 48.0 / 42.7 / 41.5 % |
| kick-skeleton reuse, 4 bars | 8.3–11.6 % | 6.1–7.5 % |
| near duplicates, 4 bars | 0.8–2.5 % | 0.3–1.9 % |
| 2 bars, exact dup low / default / high density: Classic | 53.0 / 9.0 / 14.7 % | 39.7 / 15.3 / 6.3 % |
| Dusty | 21.0 / 3.7 / 7.7 % | 10.7 / 4.7 / 2.7 % |
| Jazzy | 21.7 / 2.3 / 7.7 % | 11.3 / 6.0 / 5.0 % |
| Gold | 43.3 / 7.0 / 15.3 % | 31.3 / 8.3 / 4.3 % |
| Russian | 15.3 / 16.0 / 2.3 % | 6.0 / 5.0 / 1.7 % |
| Lofi | 23.3 / 20.3 / 10.3 % | 10.0 / 9.0 / 4.7 % |
| hard failures / monotone events per bar (density) | 0 % / 18 of 18 | 0 % / 18 of 18 |
| `BoomBapBatchAudit 1000`: hard trap / missing backbeats / ghost violations / determinism | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| `BoomBapBatchAudit 1000`: p50 / p95 ms | 5.43 / 6.92 | 5.34 / 6.23 |
| lane / core tests | pass | pass |

Costs (RULE 38 / 45): 2-bar duplicates at default density rose for Classic (+6.3 points), Jazzy
(+3.7), Gold (+1.3), Dusty (+1.0); the kick-count spread between substyles narrowed (0.42 → 0.16
kicks/bar) and density moves kicks less (Classic 2 bars: 2.99–3.91 → 3.44–3.95). The scorer
average (9.82 → 9.87) changed with the scorer and is not comparable. Needs a listening check
(RULE 42) — Lofi / Russian at default density are the substyles to listen to first.

### Russian Underground check of step 3 (full tracks, weak evidence)

Question from the maintainer: does Russian Underground need more kicks at all? 36 full tracks
(no stems, vocals included) of early Тбили Тёплый, Рем Дигга, Триагрутрика, Восточный Округ,
Полумягкие, Кто ТАМ were transcribed with `HPDG_BreakLab analyze --hits`; 15 tracks / 200 bars
pass the reliability filter of `tools/track_kicks.py` (tempo confidence >= 0.6, 70-100 BPM, kick
on beat 1 in >= 60 % of bars). Audio and reports are not in git.

| Measure | Tracks | HPDG Russian before step 3 | after step 3 |
|---|---|---|---|
| kicks/bar | 3.64 (track median 3.67, spread 1.9-5.8) | 3.25 | 3.58 |
| kick on beat 3 (16th 8) | 0.65 (>= 0.5 in 12 of 15 tracks) | 0.23 | — |
| kick after the snare (16ths 5 / 13) | 0.03 / 0.00 | 0.06 / 0.18 | — |

Step 3 does not move Russian away from the tracks on kick count. The real substyle difference is
the position: a straight "1 and 3" kick frame (`0 8`, `0 3 8 15`, `0 7 8 10`, `0 8 10 14`) that
HPDG Russian plays in a quarter of its bars. The zero after the snare may be masking by the
snare + vocal in a full mix. Next Russian step: substyle kick motifs with beat 3, measured on
instrumentals / drum stems with a held-out track split (15 tracks is too few to validate).

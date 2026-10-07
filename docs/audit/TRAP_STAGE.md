# Trap stage (roadmap §51, stage 3)

Each step: one hypothesis → the smallest change (improve, not rework) → the same benchmarks before /
after (RULES 1, 2, 4, 39, 41); style claims are checked against written sources as well as the
reference corpus. Results in `docs/audit/trap/`.

## Diagnosis (Phase 0 / 1)

99.1 % of seeds reuse an earlier kick skeleton, 1.67 kicks/bar in every substyle, density without
effect; the scorer cannot fix it (every decile ≈ 1.7 kicks/bar). In `TrapAlgebraEngine::generateBarAdditions`:

- 9 four-bar phrases, chosen by `(candidateIndex + seed) % 9`;
- main kicks only on 16ths 0 / 4 / 6 / 9 / 10 / 14 (`isLegalMainKickLocalTick`), at most 2-3 a bar;
- the kick on 1 is skipped in bars 2-4 with 74 %;
- no kick articulation: the phrase fixes the kicks, density never reaches them.

## Reference

44 loops, `docs/audit/reference/trap_kick_bars.tsv` (derived, no audio; rebuild with
`tools/trap_reference.py`):

- 9 Ghosthack GH Trap Kit "Drums Kick.mid" files — exact notes (`tools/midi_drums.py`);
- 35 kick stems of trap drum loops (Ghosthack Urban / Hybrid Trap Essentials, Sonic Mechanics UTT2),
  `HPDG_BreakLab analyze --bpm <label> --hits`, every onset of the stem, 16th from the file start.

Split 70 / 30 by an MD5 of the bars text (identical loops share a split): calibration 136 bars,
validation 76 bars. Comparison: `tools/reference_bars.py`.

Written sources (programming tutorials) agree with the corpus on the frame: snare / clap on beat 3
(half-time), the kick on beat 1, a few syncopated kicks around it, the kick mostly with the 808, hats
on 8ths with 32nd rolls, and bar-pair variation:

- Native Instruments, *How to make a trap beat* — a two-bar kick on the 1st, 8th and 10th 8th notes
  (bar 1 on the downbeat, bar 2 answering), clap on beat 3, 8th hats with 32nd rolls, straight (no swing).
- EDMProd, *How to make trap music* — kick on beat 1, snare on beat 3, extra kicks "here and there";
  not every 808 needs a kick, but most should have one.
- eMastered, *Trap drum patterns* — a kick on the first beat, kicks between beats (before beat 3,
  off-beats), variations every 4 / 8 bars.

## Step 1 — kick vocabulary, articulation, bar-pair downbeat (accepted, pending listening)

**Findings (calibration / validation vs HPDG, all substyles identical):** kicks/bar 2.27 / 2.49 vs
1.67; distinct kick bars 44 per 136 bars vs 13 per 4000; kick on 1 in 0.62 / 0.61 of bars vs 0.25 —
bars 1 and 3 of a phrase 0.85-1.00, bars 2 and 4 0.16-0.38 (the A / B pairing of the tutorials), HPDG
bar 3 0.19; 16ths 2 and 12 at 0.15-0.33 / 0.18-0.24 vs 0 (not legal); 16th 9 at 0.00 vs 0.14.

**Changes (all additive; the original phrases, gates and repairs stay):**

- 1A: the 29 distinct 4-bar kick phrases of the calibration split appended to the 9 phrases (9 → 38);
  16ths 2 and 12 (ticks 8 / 48) legal for a main kick (48 not in double time, where it is the snare).
- 1C: one articulation kick per bar with probability `0.10 + 0.45·density + 0.20·kickIrregularity`, its
  position weighted by the calibration profile (16ths 2 / 4 / 6 / 10 / 12 / 14), legal, ≥ an 8th from
  other kicks; placed before the 808 coupling, so it gets its 808.
- 1D: the 74 % skip of the downbeat only in B bars (2, 4); in A bars (3) without an early kick, a kick
  on 1 with 80 %.
- Rejected: 1B, the phrase picked from the seed (as Boom Bap): no measurable change (kick-skeleton
  reuse 90 % with or without) — reverted (a mechanism change without a gain).

Before → after (1000 seeds, defaults, 4 bars; validation split not used for tuning):

| Measure (6 substyles) | Before | 1A | 1A + 1C | **1A + 1C + 1D** |
|---|---|---|---|---|
| kick-skeleton reuse | 99.1 % | 90 % | 4.5-15.8 % | **5.6-18.0 %** |
| kicks/bar (reference 2.27 / 2.49) | 1.67-1.73 | 1.61-1.77 | 1.99-2.25 | **2.09-2.37** |
| distinct kick bars / 4000 | 13-15 | 29-41 | 76-92 | 72-94 |
| kick on 1, bar 3 (reference 0.85-1.00) | 0.19 | — | 0.19 | **0.93** |
| kick L1 distance to **validation** | 1.69-1.70 | 1.01-1.09 | 0.99-1.15 | **0.79-0.88** |
| validation bars HPDG plays | 27.6 % | 48.7-57.9 % | 67.1-75.0 % | **67.1-77.6 %** |
| hard failures / exact / near duplicates | 0 / 0 / 0-0.1 % | | | 0 / 0 / 0-0.2 % |
| determinism failures | 0 | | | 0 |
| p50 / p95 ms | 21.7-24.1 / 23.9-26.4 | | | 21.4-24.6 / 23.0-26.9 |
| matrix 300 seeds: exact duplicates (54 cells) | 0-0.7 % | | | 0 % |
| lane / core tests | pass | | | pass |

Not solved: **density still barely moves Trap** (kicks/bar density 0.2 → 0.8: +0.03-0.10; events
+0.2-0.45). `HPDG_ScorerAudit scorers` (100 seeds): the Trap scorer prefers more kicks (rho +0.24-0.29,
top decile 2.24-2.58 vs bottom 1.84-2.15 kicks/bar), so the selection picks articulated candidates at
any density. Next Trap step: density control (scorer density awareness; tutorials put Trap density
mostly in hats / rolls / 808, not kicks). Substyles still share one kick vocabulary (RULE 9) — later step.

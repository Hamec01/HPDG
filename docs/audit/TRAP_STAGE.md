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

## Step 1 — kick vocabulary, articulation, bar-pair downbeat (accepted; maintainer listening 2026-10-07: OK)

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

## Step 2 — the density slider reaches Trap (accepted)

**Written sources.** Trap density lives mostly in the hats: an 8th-note base, 16ths / triplets /
32nd-64th rolls for energy, rolls with restraint (transitions, not every bar); darker trap uses
slower, sparser hats, aggressive trap fast, complex ones; kick and 808 stay sparse (2-4 808 notes a
bar) — eMastered, *Trap drum patterns*; Violet Recording, *The best drum patterns for trap*;
Native Instruments, *How to make a trap beat*. Reference hat stems (35 loops of the step-1 corpus,
every onset): 2-23 hats a bar, median 13; GH Trap Kit hat MIDI 4-8 a bar — a wide range.

**Finding.** HPDG moved by +0.2-0.45 events a bar from density 0.2 to 0.8 (18 / 18 configurations
"barely changes"). Measured step by step:

- 2A (rejected, reverted): the scorer's hat target made density-aware — no change to the hundredth.
- A temporary probe (first candidate instead of the selection; debug print of the generator's
  density): the generator itself barely moved, and it received **0.515 / 0.61 / 0.665** for slider
  values 0.2 / 0.58 / 0.8 (ATL). `applyTrapMusicalHints` (`StyleInfluence.cpp`) blended the slider
  75 % towards the style's fixed `groove.density`, keeping a quarter of the slider. Boom Bap skips
  these hints for the same reason (its Algebra generator owns density).

**Change (2B).** `applyTrapMusicalHints` no longer blends `densityAmount` (swing / timing / humanize
and lane weights unchanged; Rap and Drill have the same blend — not touched, other engines).

| Measure (300 seeds, density 0.2 / default / 0.8, 4 bars) | Before | After |
|---|---|---|
| events/bar spread 0.2 → 0.8 (18 configurations) | +0.17-0.45 | **+1.0-1.6** |
| "density barely changes the pattern" | 18 / 18 | **0 / 18** |
| monotone events/bar | 18 / 18 | 18 / 18 |
| hats/bar, ATL | 10.40 / 10.50 / 10.53 | 10.08 / 10.43 / 10.76 |
| roll notes/bar, ATL | 3.16 / 3.27 / 3.34 | 2.78 / 3.23 / 3.59 |
| kicks/bar, ATL | 2.12 / 2.17 / 2.19 | 2.01 / 2.16 / 2.27 |
| exact duplicates / failures (54 cells) | 0 / 0 | 0 / 0 |

At default density (1000 seeds): kicks/bar 2.06-2.37, validation kick L1 0.79-0.89 (step 1:
0.79-0.88), kick-skeleton reuse 6.3-20.3 % (step 1: 5.6-18.0 % — the substyles with a low default
density now play at it), failures / duplicates / determinism 0, p95 21.6-25.4 ms; lane / core / track
semantics tests pass.

Next: the hat range is still narrow (≈ 0.7 hats a bar from 0.2 to 0.8, reference 2-23) — the hat
generator's density response (8th base vs 16ths / rolls), with sources on sparse vs busy trap hats.

## Step 3 — gaps in the 8th hat carrier (accepted; maintainer listening 2026-10-07: hats sound right)

**Measurement.** The lab's `hatsPerBar` counts only the HiHat lane; Trap puts closed-hat accents and
roll notes in HatFX, while a hat stem holds both. New lab columns `hatAllBars` / `hatAllPerBar`
(HiHat + HatFX). Reference: the 35 hat stems of the step-1 loops, `docs/audit/reference/trap_hat_bars.tsv`
(16th positions per bar + hat notes per bar), `tools/reference_bars.py --column hatAllBars`.

**Written sources.** 8ths are the steady base, most producers write 16ths, triplets add drive, 32nd
rolls mark transitions (end of a 4-bar phrase, before the snare) with a volume build; dark /
atmospheric trap uses sparse hats; "space" works when the hits form rhythmic shapes — eMastered,
*5 best trap hi-hat patterns* and *Trap drum patterns*; MusicRadar, *How to thin out a busy drum beat*.

**Finding.** Hat notes per bar are inside the reference range (HPDG 10.6-13.5 at 0.2-0.8 density;
stems median 13.25, quartiles 9.75-16.25), but HPDG played **every 8th in every bar** (presence 1.00
on all even 16ths) where the stems hold an 8th in 53-88 % of bars — least under the snare (beat 3,
0.53-0.56) and on the 16ths 2 / 14, most on 4 / 12 (0.82-0.88). Hat-position L1 to validation 3.4-4.1.

**Change (3A).** In the skeleton, each 8th of the hat carrier (normal time) is skipped with
`(1 - calibration presence of that 8th) × (1.4 - 0.8·density)` — the calibration gaps at density 0.5,
more gaps when sparse, fewer when dense. Double time unchanged.

| Measure (300 seeds, 4 bars, density 0.2 / default / 0.8) | Before (step 2) | After |
|---|---|---|
| hat-position L1 to validation (ATL / Dark / Rage) | 3.6-4.1 / 3.7-4.1 / 3.4-3.9 | **1.9-2.2 / 1.8-2.1 / 1.8-2.1** |
| distinct closed-hat bars / 1200 | 148-223 | 922-1042 |
| hat notes/bar, ATL (reference median 13.1 / 13.3) | 11.5 / 12.0 / 12.5 | 8.6 / 10.0 / 10.8 |
| events/bar spread 0.2 → 0.8 | +1.0-1.6 | +2.1-2.9 |
| exact duplicates / failures (54 cells), monotone | 0 / 0, 18 / 18 | 0 / 0, 18 / 18 |
| 1000 seeds defaults: kicks/bar, validation kick L1 | 2.06-2.37, 0.79-0.89 | 2.06-2.38, 0.78-0.88 |
| kick-skeleton reuse | 6.3-20.3 % | 8.1-21.7 % |
| determinism, p95 ms | 0, 21.6-25.4 | 0, 23.5-27.0 (one 34.3 outlier run) |

Costs: hat notes at default density moved to the lower half of the reference (ATL 12.0 → 10.0);
the odd 16ths are still thinner than the stems except 7 / 15 → 3B candidate (16th fill, sources say
most trap hats are written in 16ths).

**Tests.** Two single-seed checks of `testTrapAlgebraEngineSmoke` turned into rates over seeds
(RULE 3): the phrase answering the snare in >= 3 of 4 bars (measured 79-86 % of seeds, reference
52 %; now >= 60 % of 50 seeds) and kick-808 coupling >= 0.60 per substyle (now >= 70 % of 50 seeds).
Both checks only held for the old fixed-phrase engine; at step 2 the coupling check passed by luck
(per substyle 3-17 % of seeds below 0.60, probability ≈ 0.59 that six single seeds pass).

**Open (core, RULE 11).** Kick-808 coupling below 0.60 in 3-17 % of seeds per substyle at step 2,
1-21 % after step 3 (CloudTrap 9 → 17 %, LuxuryTrap 17 → 21 %, MemphisTrap 5 → 1 %): the selection
lets secondary terms outweigh a weak low-end core. Next Trap step.

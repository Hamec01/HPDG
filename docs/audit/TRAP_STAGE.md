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

## Kick-808 coupling check (measured, no change)

The step-3 "open" item assumed that a kick-808 coupling score below 0.60 (1-21 % of seeds) is a
core failure. Measured against the reference instead (RULE 12 — the score is not the music):

| | HPDG, all substyles (step 3, 1000 seeds) | GH Trap Kit MIDI (9 kits with Kick + 808 files) |
|---|---|---|
| 808 starts on a kick | 0.93-0.94 | 0.40 (per kit 0.10-0.78) |
| kicks with an 808 start | scorer band 0.50-0.90 | 0.43 (0.10-0.73) |
| 808 starts per bar | 1.5-2.1 | ≈ 2.5 (1.2-4.0) |

Sources: the kick gives the punch, the 808 the sustain, the pair defines trap (eMastered, *How to make
808s hit hard*; MusicRadar, *808 kick guide*); the 808 line follows the melody's roots with glides
(Native Instruments); "not every 808 hit has to align with a kick, but it helps if most of them do"
(EDMProd). HPDG couples more tightly than the reference, so the low-coupling seeds are within played
practice — not tightened (RULE 10). Open, weak evidence (9 kits, one vendor; sources disagree): an 808
with more of its own rhythm between kicks. Needs more 808 references before any change.

## Step 4 — the 808 gets more of its own rhythm (accepted; listening: better, 808 sometimes cut off -> step 5)

**New references (maintainer-provided; good quality, not ground truth, never copied).**

- Sample Tools by Cr2 *Trippy Trap Drum Loops*: 15 "Kick & Snare" loops (snare on the 16th 8 — same
  half-time grid as HPDG) and 9 matching 808 MIDI files. The kick frame repeats the A / B pairing of
  step 1 (bar A on 1, bar B often on beat 2: `0 | 4`, `0 12 | 4`). The 808 MIDI is exported from an
  arrangement position — aligned to the audio by whole bars.
- Hex Loops *Trap MIDI Loops Vol. 3*: kick / snare / clap / 808 MIDI files are separate loops, and
  their grids differ (snares on 8 in some files, on 4 / 12 in others), so a kick file's grid is
  ambiguous — **not used** for the kick reference.

`docs/audit/reference/trap_808_bars.tsv` (18 kick / 808 pairs: 9 GH Trap Kit + 9 Cr2; rebuild /
statistics `tools/trap_808_reference.py`):

| | GH Trap Kit | Cr2 | pooled | HPDG step 3 |
|---|---|---|---|---|
| 808 starts on a kick | 0.40 | ≈ 0.57 | 0.46 | 0.92-0.95 |
| 808 starts per bar | ≈ 2.5 | ≈ 2.2 | 2.27 | 1.4-2.1 |
| own 808 (not on a kick) per bar | | | 1.22 | ≈ 0.1 |

Own 808 notes sit on even 16ths (12 most, then 10, 0, 4, 6, 8, 2) — an 8th (44), a dotted quarter
(33), a quarter (26) or a half (16) after the previous kick, almost never a dotted 8th (5). Sources:
the 808 is a bass line following the melody's roots with glides (Native Instruments); most 808 hits
with a kick, not all (EDMProd).

**Change.** The existing "independent 808 answer" (≤ 1 a bar, chance ≈ 0.2, an 8th or a dotted 8th
after a kick, never on the 16ths 4 / 8 / 12) became up to two answers a bar with chance
`0.22 + 0.30·density + 0.08·variation + 0.10·kickIrregularity` each, an 8th / dotted quarter / quarter
after a kick (0.43 / 0.25 / 0.32), never on the snare of the current grid (the old exclusion of 16 /
48 belongs to double time — in normal time 12 is the most frequent own-808 position).

| Measure | Step 3 | Step 4 |
|---|---|---|
| 808 starts on a kick (300 seeds, density 0.2 / default / 0.8) | 0.92-0.95 | **0.75-0.82** |
| 808 starts per bar | 1.4-2.1 | **1.7-2.6** (reference 2.27) |
| kicks/bar, validation kick L1 (1000 seeds, defaults) | 2.06-2.38, 0.78-0.88 | 2.06-2.40, 0.79-0.90 |
| hat-position L1 to validation | 1.8-2.2 | 1.8-2.1 |
| failures / duplicates / determinism | 0 / 0 / 0 | 0 / 0 / 0 |
| monotone density (18 cells) | 18 / 18 | 18 / 18 |
| p50 / p95 ms (re-run) | 20.5-23.8 / 23.5-27.0 | 19.6-22.7 / 20.6-26.6 |
| lane / core / track semantics tests | pass | pass |

Still tighter than played trap (808 on a kick 0.77 vs 0.46): every main kick still gets its 808 (kicks
with an 808: reference 0.43-0.45). Loosening that means moving the scorer's coupling band (0.50-0.90)
— a separate step, only with more references.

**Test.** The smoke check "kick-808 coupling ratio >= 0.70" on one seed became >= 60 % of 50 seeds
(RULE 3): the ratio's distribution did not move (>= 0.70 in 74 / 100 seeds before step 4, 75 / 100
after; mean 0.767 / 0.763) — seed 9090 went from 0.714 to 0.667.

## Step 5 — the 808 sustains (accepted; maintainer listening 2026-10-07: sounds good)

**Listening (maintainer, after step 4):** better, but the 808 sometimes cuts off abruptly — "no bounce".

**Measurement.** Lab metrics `bassMeanLength` (16ths), `bassGapFill` (share of the gap to the next
bass start the note fills; legato = 1), `bassShortRate` (notes shorter than an 8th); reference note
lengths from the 808 MIDI (`tools/midi_drums.py read_notes`):

| | GH Trap Kit + Cr2 + Hex 808 MIDI | HPDG step 3 | HPDG step 4 |
|---|---|---|---|
| 808 gap fill | 0.81 (GH 0.87-1.00, Cr2 0.53-1.00) | 0.41-0.55 | 0.43-0.56 |
| mean length (16ths) | 6.7 (median 4) | 3.0-3.1 | 2.5-2.6 |
| shorter than an 8th | 6 % | 3-6 % | 8-12 % |
| share of the time the 808 sounds | GH median 0.91, Cr2 0.67 | ≈ 0.3 | ≈ 0.3 |

The 808 was choppy before step 4 (a note filled ≈ half the gap, then silence); the step-4 answers an
8th after a kick made the cut audible. Two causes: note lengths capped at 8-10 ticks (an 8th) at
creation and 0.42-0.76 of the gap in `shape808Durations`; and the scorer's 808 occupancy band
(0.22-0.55, "mud" above) — longer 808s would have been selected away.

**Change (numbers only, mechanisms kept).** Occupancy bands (engine repair + scorer) raised towards
the reference with the substyle order kept (Cloud 0.38-0.72 … Rage 0.50-0.85; ATL 0.45-0.80); the
style-score 808 target 0.22 + 0.18·legato → 0.50 + 0.18·legato; gap fill 0.76 / anchor 0.90 / answer
0.62 / pickup 0.42 → 0.90 / 0.96 / 0.86 / 0.66; creation cap 8 / 10 → 22 / 26 ticks (the next kick
and the breath before the snare now limit the note); answer length 4-8 → 8-16 ticks (trimmed to the gap).
The breath before the snare (HPDG's choice) stays — the reason the fill stops below the reference.

| Measure (1000 seeds, defaults) | Step 4 | Step 5 | Reference |
|---|---|---|---|
| 808 gap fill | 0.43-0.56 | **0.60-0.70** | 0.81 |
| 808 mean length (16ths) | 2.5-2.6 | **3.6-3.9** | 6.7 (median 4) |
| 808 shorter than an 8th | 8-12 % | **1-2 %** | 6 % |
| 808 starts per bar | 1.8-2.5 | 1.8-2.5 | 2.27 |
| kicks/bar, validation kick L1 | 2.06-2.40, 0.79-0.90 | 2.06-2.40, 0.79-0.89 | |
| hat-position L1 | 1.8-2.1 | 1.8-2.1 | |
| failures / duplicates / determinism; monotone density | 0 / 0 / 0; 18 / 18 | 0 / 0 / 0; 18 / 18 | |
| lane / core / track semantics tests | pass | pass | |

Runtime: back-to-back runs with FL Studio open differ more between two runs of the same build (ATL
p50 24.4 / 27.1 ms) than between steps 4 and 5 — no measurable change.

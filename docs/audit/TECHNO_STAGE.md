# Techno stage (roadmap §51, stage 5)

Same method as Boom Bap / Trap / DnB: one hypothesis → smallest additive change → same benchmarks
before / after; written sources + several packs; packs are guidance, never copied.
Design reference of the engine: `docs/techno-engine.md` (Butler 2006, Zeiner-Henriksen 2010,
Toussaint 2005, Witek et al. 2014).

## Baseline (1000 seeds, defaults; matrix 300 seeds × density 0.2 / 0.55 / 0.8 × 2 / 4 / 8 bars)

| Substyle | exact dup (1000 seeds) | near dup | events/bar |
|---|---|---|---|
| Peak Time | 57.3 % | 95.1 % | 20.1 |
| Hypnotic | 20.4 % | 75.3 % | 26.5 |
| Minimal | 68.2 % | 95.1 % | 14.2 |
| Detroit | 43.7 % | 94.9 % | 23.6 |
| Dub | 60.8 % | 93.7 % | 17.1 |
| Acid | 52.8 % | 93.6 % | 19.1 |
| Hard | 31.8 % | 87.5 % | 25.4 |

(kicks always 4 a bar — four-on-the-floor, genre-true.)

Matrix: "density barely changes the pattern" 18 / 21 cells, monotone only 13 / 21 — Minimal and Dub
**inverted** (Minimal 4 bars: 14.7 / 14.3 / 13.8 events a bar at 0.2 / 0.55 / 0.8).

## Step 1 — the user's density moves the scorer's density target (accepted, listened 2026-10-08)

**Cause.** `TechnoScorer::score` fits the pattern density (weighted onsets per step) to a fixed
`style.densityTarget`; the selection is sharp (near-best tolerance 0.03, softmax temperature 0.012),
so whatever the grammar did with the density slider (it leans hats / perc), the selection pulled
every pattern back to the style's fixed density — and in Minimal / Dub (low targets) it chose the
sparser candidates at high density (inverted).

**Change.** `score(pattern, style, userDensity)`: target = `densityTarget × (1 + 1.2 × (userDensity -
densityDefault))`, clamped 0.4-1.6; the search passes `params.density`; other callers keep the fixed
target. Same idea as `StyleTargetModel::withPerformanceIntent` (Boom Bap / Trap). Neutral at the
default density: the 1000-seed lab CSV is identical to the baseline in all 7 substyles.

| Measure (300 seeds) | Baseline | Step 1 |
|---|---|---|
| "density barely changes" | 18 / 21 | **0 / 21** |
| monotone events/bar | 13 / 21 | 20 / 21 (Minimal 2 bars 14.3 / 14.0 / 16.5) |
| events/bar 0.2 → 0.8, 4 bars: Peak Time / Hypnotic / Minimal / Detroit / Dub / Acid / Hard | +0.8 / +0.6 / −0.9 / +0.5 / −0.5 / +1.3 / +0.3 | +7.2 / +17.3 / +2.7 / +10.2 / +7.1 / +9.1 / +13.8 |
| defaults (density 0.55) | — | identical |
| failures / determinism | 0 / 0 | 0 / 0 |
| lane / core / track semantics tests | pass | pass |

Cost: at the density ends sparse / busy patterns repeat more across seeds — exact duplicates at
density 0.2, 4 bars: Dub 42.3 → 79.0 %, Minimal 45.7 → 69.7 %, Hypnotic 3.7 → 16.0 %; at 0.8 some
rise (Detroit 33.3 → 44.7 %, Peak Time 47.7 → 55.7 %), some fall (Minimal 55.0 → 41.3 %, Dub
45.7 → 39.3 %). The defaults are unchanged. Hypnotic at 0.8 reaches 34 events a bar (26 at default)
— to be listened to.

## Duplicates — measured (step 2, no change)

Probe (temporary, removed): `TechnoEngine::search` directly, 300 sequential seeds, 4 bars, default
density. Distinct patterns among the selected: 202-296 of 300; among one *random candidate* per seed:
255-298 — the generator's own pattern space is the bound (four-on-the-floor kick 5-10 variants, clap
5-8, hats 21-79, "other" lanes 2-52 per 300 seeds). The high lab rates (20-68 % at 1000 seeds) are
that small space seen over many seeds (a pattern counts as a duplicate once any earlier seed played it).
Widening the near-best pool (tolerance 0.03 → 0.06, temperature 0.012 → 0.03) changed little
(distinct selected Detroit 235 → 251, Dub 207 → 202; lab exact dup at defaults −3 to −8 points) —
rejected. Percussion: 47-84 % of candidates carry it, the selection keeps it at default density
(41-98 %) but drops it at 0.2 (1-75 %).

## Step 2 — hat accents on chosen odd 16ths (accepted, listened 2026-10-08)

**Reference.** Ghosthack Ultimate Techno Essentials `GUT_Drum_Loops` (50 loops, 118-127 BPM): the
Strpd + Top_a + Top_b layers merged per loop → `docs/audit/reference/techno_ghosthack_drum_bars.tsv`;
lab column `drumBars` (every drum lane but the crash and the rumble — rumble is the kick's tail, not
a transient a loop transcription sees). The pack is deep / minimal tempo: Minimal and Dub compare
directly; Peak Time / Hard / Hypnotic (faster, busier) are not judged on its position averages.

**Finding.** Odd 16ths per bar: played loops 0: 28 %, 1: 22 %, **2-3: 24 %**, 4: 14 %, 6-8: 9 %, in **42
different odd-16th sets per 200 bars** (`2 7 11`, `3 10`, `1 15` ...). HPDG: 2-3 odd 16ths in **0 %** of
bars, 16-18 sets per 1200 bars — the hat modes give all-or-nothing (every "a", every 16th, none).

**Change (additive).** An 8th carrier (or offbeat without open hats) gets, with 0.65 per pattern, 1-3
odd 16ths chosen once for the loop (count leans up with density); 16th carriers unchanged.

| Measure (1000 seeds, defaults) | Step 1 | Step 2 |
|---|---|---|
| exact dup Minimal / Dub / Acid / Detroit / Peak Time | 68.2 / 60.8 / 52.8 / 43.7 / 57.3 % | **35.0 / 42.7 / 36.8 / 32.1 / 46.8 %** |
| exact dup Hypnotic / Hard (16th carriers) | 20.4 / 31.8 % | 20.5 / 31.0 % |
| odd-16th sets per 1200 bars | 16-18 | 59-116 |
| Minimal odd 16ths per bar 0 / 1 / 2 / 3 (reference 28 / 22 / 14 / 10 %) | 62 / 14 / 0 / 0 % | 27 / 20 / 18 / 6 % |
| position L1 calibration / validation, Minimal | 2.02 / 0.83 | 1.36 / 1.56 |
| position L1 calibration / validation, Dub / Detroit / Acid | 1.74 / 2.17, 2.85 / 3.73, 2.82 / 3.71 | 2.17 / 2.87, 3.36 / 4.24, 3.12 / 4.01 |

Matrix (300 seeds): monotone **21 / 21** (step 1: 20), "density barely changes" 0 / 21, failures none;
exact dup at the default density, 4 bars: Minimal 51.0 → 18.0, Dub 44.7 → 27.0, Acid 34.3 → 23.0,
Detroit 26.7 → 15.3, Peak Time 35.3 → 28.3 %. Lane / core / track semantics tests pass.

Cost: the position averages move away from the pack for Dub / Detroit / Acid (their odd 16ths were
already above it through the 16th / "a" carriers), and the validation split is small (19 loops,
18 distinct bars). Accepted on the duplicate and bar-shape evidence; listening decides.

## Step 3 — faster techno tops measured (2026-10-10, no change)

**Why.** Step 2 left the 16th carriers (Hypnotic / Hard, also Peak Time) unchecked: the GUT pack is
118-127 BPM deep / minimal. The second PC has faster packs.

**References** (`tools/loop_bars.py`, typed tempo, 16th steps from the file start):
- `docs/audit/reference/techno_fast_tops.tsv`: PML x Weska Peak Time Tops (30 loops, 128) and PML
  Overdrive Top Loops (10, 135-138), HiHat lane — 142 bars;
- `techno_fast_hats.tsv`: PML Overdrive Hat / Ride Loops (132-140) and Audentity Dark Techno 2
  Hi-Hat loops (125);
- `techno_dasha_full.tsv`: Dasha Rush "Techno Toys" full drum loops (7 loops, 131-140, TR-626).

**Lab.** New column `topBars`: HiHat, OpenHat, Ride, HatFX and Perc — what a top loop holds.
`hatAllBars` leaves out the open hat, so against a top loop it showed the offbeat at 0.03 (a false
"missing offbeat").

**Finding** (1000 seeds, defaults, 4 bars):

| | per bar | offbeat 2/6/10/14 | "a" 3/7/11/15 | on-beat 0/4/8/12 |
|---|---|---|---|---|
| Weska + Overdrive tops (142 bars) | 8.3-8.6 | 0.88-1.00 | 0.20-0.38 | 0.30-0.60 |
| Dasha Rush full loops (27 bars, drumBars) | 9.3-13.5 | 0.50-1.00 | 0.00-0.96 | 0.96-1.00 |
| HPDG Peak Time topBars | 12.8 | 1.00 | 0.81-0.86 | 0.79 |
| HPDG Hypnotic / Hard topBars | 12.5 / 13.7 | 1.00 | 0.93-0.97 | 0.45 / 0.76 |
| HPDG Minimal / Dub topBars | 9.8 / 11.2 | 1.00 | 0.34-0.43 / 0.65-0.71 | 0.80 / 0.75 |

**Decision: no change.** The two references disagree: the commercial peak-time tops are sparser than
HPDG (8.3 vs 12.5-13.7 hits a bar, the "a" 16th 0.2-0.4 vs 0.8-0.97), while Dasha Rush's raw
hypnotic loops are as dense as HPDG's Peak Time / Hypnotic (drumBars L1 1.1-1.4). The packs also keep
percussion in separate loops, which `topBars` counts. Thinning the 16th carriers would be tuning on
one pack against another (RULES 18 / 47); it needs written sources on hypnotic / hard techno hats and
a listening check first.

## Open (next steps)

1. (step 2: hat accents.) Further widening: 16th carriers (Hypnotic / Hard) - a second, faster techno reference is needed.
   Original note: widen the grammar's pattern space in a genre-true way (references: Ghosthack Ultimate Techno
   Essentials drum loops Full / Top / Kick, construction-kit stems; Afterhours Tech House "We Want
   Techno"; African Tech House Drums MIDI = tech house, not techno) — which hat / perc / rumble
   variations do played techno loops use?
2. Determinism (RULE 13): `TechnoEngine::generate` salts the seed with `generationCounter * 131`.

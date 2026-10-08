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

## Open (next steps)

1. Widen the grammar's pattern space in a genre-true way (references: Ghosthack Ultimate Techno
   Essentials drum loops Full / Top / Kick, construction-kit stems; Afterhours Tech House "We Want
   Techno"; African Tech House Drums MIDI = tech house, not techno) — which hat / perc / rumble
   variations do played techno loops use?
2. Determinism (RULE 13): `TechnoEngine::generate` salts the seed with `generationCounter * 131`.

# Phase 1 — measurement: scorer validation and candidate counts

Roadmap §8 (validate the scorers) and §11 (candidate count calibration). No generation change:
the engines only gained an optional audit output (`candidatesOut`) that returns every scored
candidate of a call; the returned pattern is identical (1000 seeds × 25 configurations
re-run against the Phase 0 baseline: **0 differing values**).

Tool: `HPDG_ScorerAudit` (`Tests/ScorerAudit.cpp`), shared metrics `Tests/QualityMetrics.h`.
Engine parameters mirror each engine's production mapping of the style defaults, 4 bars.
Results: `docs/audit/phase1/scorer_validation.{txt,json}`, `candidate_count.{txt,json}`.

## 1. Scorer validation (200 seeds per substyle, every candidate of every pool)

Candidates per substyle: Boom Bap 12 800, Trap 6 400, DnB 9 600, Techno 9 600.
Within each seed's pool candidates are ranked by the engine's own quality; "rho" is the mean
within-seed Spearman correlation of quality with a metric.

**Safety.** No candidate in any top 10 % (any engine) has a structural failure or is hard-invalid.
Hard-invalid candidates exist (Trap ≤ 10 / 6 400, DnB 22–325 / 9 600) and are never selected.

**What each scorer rewards** (strongest within-seed correlations):

| Engine | Quality goes with | Comment |
|---|---|---|
| Boom Bap | fewer events (rho −0.18 … −0.29 in 4 / 6 substyles), fewer snare ghosts, sparser hats | anti-clutter bias — consistent with RULE 6 / 18 (simple can be excellent); Jazzy / Gold prefer more kicks (+0.22 / +0.27) |
| Trap | more events (+0.10 … +0.42), less bar similarity (−0.10 … −0.22), slightly more kicks | rewards busier, more varied bars; kicks stay at ≈ 1.7/bar in every decile (kick collapse is upstream of the scorer) |
| DnB | hat continuity (+0.11 … +0.36), more events | bottom decile has broken hat carriers (eighth coverage 0.42–0.77) — the scorer correctly rejects them |
| Techno | bar similarity (+0.10 … +0.45 in Minimal / Dub / Acid) | the repetition target pushes towards near-identical bars; likely contributes to the seed collapse (§3) |

Overall correlations are weak (|rho| ≤ 0.45): with no structural failures in the pools, the scorers
mostly shape style. Whether higher score means *better music* cannot be settled by structure
metrics alone — this needs the human listening audit (roadmap §47–48, RULE 12 / 43).

## 2. Candidate counts (200 seeds per substyle; counts 16 / 32 / 48 / 64 / 96 / 128)

| Engine (production) | Selected quality 16 → 128 | Time p50 at production | Plateau | Reading |
|---|---|---|---|---|
| Boom Bap (64) | 9.73 → 9.84 (+0.02 per doubling above 64) | 5.9 ms | none, slow gains | 64 is a reasonable cost/quality point; 128 doubles time for +0.02 |
| Trap (32) | 0.863 → 0.867 (flat) | 24 ms | **at 16** | 16 candidates give the same selected quality at half the time (12 ms); selection is near-random inside a wide pool (tolerance 0.08, temperature ≥ 0.40) |
| DnB (48) | 0.961 → 0.969 | 6.1 ms | 32–48 | 48 is on the plateau |
| Techno (48) | 0.977 → 0.987 | 1.7 ms | 64 | cheap; quality still rises to 64 |

No count produced a structural failure. Duplicate rate of the selected pattern (engine level,
200 seeds) is ≤ 1 % for Boom Bap / Trap / DnB and 0.5–23 % for Techno (Dub highest).

## 3. Notes for the genre stages

- **Techno**: engine-level exact duplicates (Dub 21 %, Minimal 11.5 % at 48) are much lower than
  the product-level rate in Phase 0 (Dub 60.8 %, Minimal 68.2 %). The product path salts the
  engine seed with `generationCounter * 131`; the seed spacing this produces probably makes the
  candidate pools of different seeds overlap. Investigate first in the Techno stage, together
  with the repetition-target bias (§1).
- **Trap**: the scorer cannot fix the kick collapse (every decile ≈ 1.7 kicks/bar); the cause is in
  candidate generation. 16 candidates would halve runtime with no measured loss — but RULE 17
  asks for the smallest count on the plateau *after* quality work, so this waits for the Trap stage.
- **Boom Bap**: healthy pools; the measurable weakness is the 2-bar low-density collapse (Phase 0
  §5.1), not the scorer.

# HPDG — ROADMAP UPDATE
## Generation Quality + Sample Analysis Quality Program

**Project:** HPDG — HamloProd Drum Generator  
**Repository:** `Hamec01/HPDG`  
**Purpose:** improve the percentage of musically usable results without replacing the existing architecture.

---

# 0. PROGRESS LOG (keep updated — read this first when resuming)

Status as of 2026-10-07 (branch `main`). Rules: `docs/RULES.md`. Agent pointer: `CLAUDE.md`.

## Done

| Stage | What | Commit | Report |
|---|---|---|---|
| — | Copy Break: whole-BPM DAW renders (79.39 played in an 80 BPM file → 80, slots read on the played tempo, drift kept as timing) and beat 1 on the first hit for a typed tempo (half-bar silence bug). 243 labeled loops, typed tempo: beat 1 > 0.5 s off 22 → 1; automatic tempo unchanged | `43e0dc9` | commit message |
| Phase 0 | Generation Quality Lab (`HPDG_GenerationQualityLab`), pipeline map, quality-mechanism audit, baseline 1000 seeds × 25 substyles + density × bars matrix | `4f80f81` | `docs/audit/PHASE0_AUDIT.md`, `docs/audit/baseline/` |
| Phase 1 (measurement) | Scorer validation (top / middle / bottom decile, within-seed Spearman) and candidate-count benchmark 16–128 (`HPDG_ScorerAudit`); engines expose `candidatesOut` (no behaviour change, verified 0 differing values) | `6e03ea4` | `docs/audit/PHASE1_MEASUREMENT.md`, `docs/audit/phase1/` |
| Stage 2 Boom Bap, step 1 | 12 kick motifs from the calibration split of 159 transcribed boom bap loops (13 → 25). Validation kick-profile distance 1.80–1.94 → 1.65–1.76 (re-measured after the lab fix below), 2-bar low-density duplicates down (Classic 55.7 → 53.0 %, Russian 24.7 → 15.3 %), 0 failures, quality / runtime unchanged | `c95bb41` | `docs/audit/BOOMBAP_STAGE.md`, `docs/audit/boombap/` |

## Key baseline findings still open

1. **Techno** — 20–68 % exact duplicate patterns at defaults; density inverted in Minimal / Dub;
   product-level duplicates ≈ 3× engine-level (seed salted with `generationCounter * 131`);
   scorer rewards bar similarity (rho up to 0.45).
2. **Trap** — 99.1 % kick-skeleton reuse (≈ 9 kick patterns per 1000 seeds, 1.67 kicks/bar);
   density control almost without effect; quality flat from 16 to 128 candidates (time × 8).
3. **Boom Bap** — groove core (bars 1–2) diversity limited; 2-bar low-density duplicates still
   high for Classic / Gold (53 / 43 %); 4-bar kick-skeleton reuse 8–12 %; kicks/bar 3.4 vs 4.0–4.6 in played loops.
4. **Confidence → generation (RULE 20)** — GenerationHints / lane evidence are built but not
   consumed by any production engine; used confidences become binary thresholds.
5. **Determinism (RULE 13)** — Trap / DnB / Techno mix `generationCounter` into the seed.

## Boom Bap steps 2–3 (hats measured, kick count changed)

- **Lab measurement fix** (`Tests/QualityMetrics.h`): Boom Bap keeps micro-timing in `gridTick`;
  the lab floored it, reading every early hit a 16th early. Now read like the engine (Boom Bap only).
  Corrected: hat odd 16ths 0.05–0.13 (reference 0.00–0.15) — the "too many odd 16ths" finding was
  this artifact; 4-bar kick-skeleton reuse is really 8.3–11.6 % (not 1.5–4.5 %); step 1 re-checked
  and kept (validation kick L1 1.80–1.94 → 1.65–1.76; its "34 → 39 % playable" claim was an
  artifact, corrected 35–40 → 37–44 %). Details: `docs/audit/BOOMBAP_STAGE.md`.
- Hats vs played loops at unmasked positions (`tools/reference_hats.py`): Classic / Gold play all
  free off-beat 8ths in 0.75 of bars vs ≈ 0.5 in the reference; not changed until the
  transcriber's soft-hat recall is measured (RULES 21 / 30).
- **Step 3 (kick count)**: one articulation kick may join a full motif, and the scorer's kick term
  is a band (3.0 + 0.8·d … 4.0 + 1.0·d per bar) instead of a peaked target. Kicks/bar 3.23–3.65 →
  3.57–3.73, validation kick L1 better in 6 / 6 substyles, 4-bar kick-skeleton reuse 8–12 → 6–8 %,
  2-bar duplicates (18 cells) 294 → 182 (Classic low density 53 → 40 %); cost: Classic 2-bar
  default-density duplicates 9 → 15 %, substyle kick-count spread narrower. Pending listening.
- Open Boom Bap items: 2-bar duplicates (Classic 40 / 15 %, Gold 31 / 8 % low / default density),
  5-kick bars 6–13 % vs 26 % in played loops, listening check of step 3. Russian Underground substyle kick
  motifs (beat 3, from 36 full tracks: `tools/track_kicks.py`) wait for instrumentals / drum stems.


## Trap step 1 (kick vocabulary, articulation, bar-pair downbeat) — `docs/audit/TRAP_STAGE.md`

- Reference: 44 trap loops (9 GH Trap Kit kick MIDI + 35 kick stems), `docs/audit/reference/trap_kick_bars.tsv`,
  `tools/reference_bars.py` / `tools/trap_reference.py` / `tools/midi_drums.py`; written sources (NI, EDMProd, eMastered).
- Additive changes: 29 calibration phrases appended (9 → 38), 16ths 2 / 12 legal, one density-driven
  articulation kick, downbeat skip only in B bars + kick on 1 in bar 3. Kick-skeleton reuse 99 % →
  6–18 %, kicks/bar 1.67 → 2.09–2.37 (reference 2.27–2.49), validation kick L1 1.70 → 0.79–0.88,
  0 failures, runtime unchanged. Seed-picked phrase (Boom Bap style) measured, no gain, reverted.
- Step 2: the density slider reaches Trap — `applyTrapMusicalHints` blended it 75 % towards a fixed
  style density (slider 0.2–0.8 → 0.52–0.67). Removed: events/bar spread +0.2–0.45 → +1.0–1.6,
  "density barely changes" 18/18 → 0/18. Rap and Drill have the same blend (not touched).
- Step 3: gaps in the 8th hat carrier from hat stems (8ths present 53–88 %, HPDG 100 %), scaled by
  density: hat-position L1 3.4–4.1 → 1.8–2.2, events spread +1.0–1.6 → +2.1–2.9. Two single-seed smoke checks
  became rates over seeds. Lab: `hatAllBars` / `hatAllPerBar` (HiHat + HatFX).
- Kick-808 coupling measured against GH Trap Kit MIDI: HPDG couples tighter (808 on a kick 0.93
  vs 0.40), so the low-coupling seeds are not failures — no change.
- Step 4: 808's own rhythm from 18 kick / 808 pairs (GH Trap Kit + Cr2 Trippy Trap; Hex Loops MIDI
  not used — mixed grids): up to two 808 answers a bar, offsets from the reference, density-driven.
  808 on a kick 0.93 → 0.75–0.82 (reference 0.46), 808/bar 1.4–2.1 → 1.7–2.6 (reference 2.27).
- Step 5: the 808 sustains — gap fill 0.43–0.56 → 0.60–0.70 (reference 0.81), notes shorter than an
  8th 8–12 % → 1–2 % (reference 6 %); the scorer's 808 occupancy band raised from 0.22–0.55 towards the
  reference (GH 0.91, Cr2 0.67). Lab: `bassMeanLength` / `bassGapFill` / `bassShortRate`.
- Substyles checked against written sources: where sources describe the drums (Rage, Cloud, Dark,
  Memphis) HPDG already differs the right way; no source gives substyle kick positions — no change.
  Hat 16th fill (3B) inside the reference — no change.
- Open Trap (design decision): every kick still carries an 808 (kicks with an 808: HPDG ≈ 0.76,
  GH / Cr2 0.43–0.45); the scorer's coupling bands and a smoke test encode the tighter design.

## DnB step 1 — `docs/audit/DNB_STAGE.md`

- References: Ghosthack Upfront kick & snare loops (`docs/audit/reference/dnb_kick_bars.tsv`), Freaky
  Loops neurofunk drum loops, tutorials. Ghosthack is busier than the rest, so the kick count was not raised.
- Change: the kick budget's rounded-away fraction is a chance of one more kick (additive). Kick-skeleton
  reuse Modern 79 → 59 %, Liquid 86 → 77 %, Jump-Up 94 → 86 %; Roller / Neuro / Breakbeat unchanged.

## NOTE FOR THE NEXT AGENT (2026-10-07, end of session)

- Read `docs/RULES.md`, this log, `docs/audit/BOOMBAP_STAGE.md`, `TRAP_STAGE.md`, `DNB_STAGE.md`.
- Maintainer's working rules (also in agent memory): back every genre change with written sources
  (articles) and several reference packs; packs are guidance, not truth, never copy patterns; improve,
  do not rework (additive changes; revert mechanism changes without a measured gain); report in Russian
  with numbers; commit after the maintainer listens / approves; FL Studio must be closed before
  `build_vst.bat` (it locks HPDG.vst3).
- Boom Bap: done (steps 1-3). Russian Underground kick motifs (beat 3) wait for instrumentals / stems.
- Trap: done (steps 1-5, listened: good). Open design question: every kick carries an 808 (HPDG 0.76 vs
  packs 0.43-0.45); the maintainer has not decided, default is keep.
- DnB: step 1 committed, NOT yet listened to. Next: phrase-level repetition (Roller / Jump-Up reuse
  86 %), hats / ghosts vs Ghosthack "DnB_Top" and Freaky stems, the "4 + 10" snare variant.
- Then Techno (Phase 0: 20-68 % duplicates, density inverted in Minimal / Dub).
- Corpus: `E:/HPDG_corpus` (being copied from the other PC; verify with `tools/corpus_manifest.py verify`).
  More packs: `E:/DRUMS/...` (Ghosthack bundle, Sonic Mechanics, Freaky Loops), `C:/Users/Ham_h/Downloads`
  (Cr2 Trippy Trap, Hex Loops Trap MIDI with mixed grids; Controversial Loops / Jungle Loops torrents
  were incomplete).
- Rap and Drill still blend `densityAmount` towards a style value in `StyleInfluence.cpp` (Trap step 2
  removed it for Trap); check before tuning those engines.

## How to reproduce / continue (another PC)

- Build: `cmake --build build --config Release --target HPDG_GenerationQualityLab HPDG_ScorerAudit HPDG_BreakLab HPDG_BoomBapBatchAudit HPDG_LaneBoundaryTests HPDG_CoreTests`
- Baseline compare: `HPDG_GenerationQualityLab --seeds 1000 [--genre boombap] --out <dir> --tag <name>`,
  then `python tools/quality_summary.py docs/audit/baseline/baseline_default_4bars_summary.json <dir>/<name>_summary.json`
- Matrix: `--seeds 300 --density matrix --bars matrix`, then `python tools/quality_matrix.py <summary.json>`
- Scorers / candidate counts: `HPDG_ScorerAudit scorers|candidates --seeds 200 --out <dir>`
- Reference corpus (local, not in git): `D:/Drums/Boombap/2 GB OF FREE SAMPLES/02_CUSTOM_DRUM_LOOPS_(150+Custom_Drum_Loopz)`
  (tempo in the file name); transcribe each loop with `HPDG_BreakLab analyze <loop> --bpm <label> --hits > <reports>/<name>.txt`,
  then `python tools/reference_kicks.py <reports> [--lane hat] --generated <lab>_patterns.csv`.
  Classic-break corpus for tempo: `.../04_CLASSIC_DRUM_BREAKS_(80+Drm_Loopz)`. Multitrack stems
  for bass-line accuracy: `HPDG_BreakLab lines <stems folder>`.
- Hats at unmasked positions: `python tools/reference_hats.py docs/audit/reference/boombap_loops_transcribed <lab>_patterns.csv`.
- Per-pattern CSVs (`*_patterns.csv`) are not in git (regenerable); summaries are.
- Reference data without audio (transcription reports, corpus manifests with SHA-256, tempo-bench
  results): `docs/audit/reference/` (see its README). The kick / hat reference comparison runs from
  `docs/audit/reference/boombap_loops_transcribed` directly; audio corpora are not in git (public
  repository, third-party material) — verify a local copy with `tools/corpus_manifest.py verify`.

---

# 1. STRATEGIC GOAL

The next HPDG development stage is **not feature expansion**.

The priority is:

> **Increase the probability that HPDG produces a musically usable result on every generation and that Sample Analysis provides reliable information to the existing genre engines.**

HPDG already contains the necessary foundations:

- genre-specific generators;
- candidate generation;
- pattern scoring;
- hard validation;
- deterministic seeds;
- near-best selection;
- novelty control;
- Style Targets;
- Sample Analysis;
- onset detection;
- STFT;
- HPSS / percussive-harmonic separation;
- drum-break transcription;
- multi-label K/S/H detection;
- harmony analysis;
- bass-line analysis;
- Generation Hints;
- Sample-Aware Generation Context;
- Support vs Contrast;
- Reactivity;
- Copy Break;
- Guide Generation.

The roadmap therefore focuses on making these systems **work together more accurately**, rather than replacing them.

---

# 2. CORE PRODUCT TARGET

The desired user experience is:

## Generate without sample

User selects:

`Genre → Substyle → Generate`

HPDG should normally return a pattern that is already musically valid and usable.

The user should not need to press Generate dozens of times to find one acceptable result.

Target:

> From 20 random generations on normal/default settings, at least 15 should be musically usable starting points.

“Usable” does not mean perfect or finished.

It means:

- recognizable genre;
- correct backbone;
- sensible density;
- no obvious structural mistakes;
- no pathological repetition;
- no random clutter;
- enough musical identity to continue working.

---

# 3. DEVELOPMENT PRINCIPLE

Every improvement must follow:

`BASELINE → MEASURE → CHANGE → MEASURE AGAIN → COMPARE`

Never:

`CHANGE → LISTEN TO THREE SEEDS → CALL IT BETTER`

HPDG should move toward a scientific development loop.

---

# 4. PHASE 0 — FULL AUDIT AND BASELINE

Before changing production behavior, inspect the current system.

## 4.1 Generation pipeline map

Document the complete path for:

- Boom Bap;
- Trap;
- DnB;
- Techno.

For every genre identify:

1. input parameters;
2. style/substyle profile;
3. phrase planning;
4. lane generation;
5. candidate creation;
6. hard validation;
7. scoring;
8. pruning;
9. near-best pool;
10. novelty handling;
11. final candidate selection;
12. post-processing;
13. conversion to runtime pattern.

Pay special attention to:

- `BoomBapClassicAlgebraGenerator`
- `BoomBapClassicPatternScorer`
- `TrapAlgebraEngine`
- `TrapQualityScorer`
- `DnBGrammar`
- `DnBScorer`
- `TechnoGrammar`
- Techno scoring path
- `CandidateSelectionEngine`
- `StyleTargetModel`
- `PatternFeatureVector`

## 4.2 Sample Analysis pipeline map

Document:

`audio`
→ preprocessing
→ tempo candidates
→ beat/downbeat
→ spectral analysis
→ onset detection
→ percussive/harmonic separation
→ lane evidence
→ drum transcription
→ bass analysis
→ harmony
→ Generation Hints
→ Sample-Aware Generation Context
→ Guide Generation / Copy Break.

Inspect especially:

- `SampleAnalyzer`
- `DrumBreakTranscriber`
- `FeatureExtractor`
- `STFTAnalyzer`
- `OnsetDetector`
- `PercussiveHarmonicSeparator`
- `LaneEventInferer`
- `SampleTranscriber`
- `SampleHarmonyAnalyzer`
- `SampleLineTranscriber`
- `BasslineInferer`
- `GenerationHintsBuilder`
- `SampleAwareGenerationContext`
- `SampleApplyWeights`
- `SampleAnalysisBundle`

## 4.3 Existing quality mechanisms

List everything already implemented that attempts to improve quality:

- hard rules;
- gates;
- target distributions;
- candidate count;
- quality floor;
- near-best tolerance;
- novelty weighting;
- pruning;
- tempo confidence;
- per-hit confidence;
- drum loop confidence;
- key confidence;
- sample reactivity;
- support/contrast;
- locks;
- genre invariants.

For each mechanism answer:

- Is it actually used?
- Where is it used?
- Does its result survive to the final stage?
- Is confidence lost or converted to a binary value?
- Is the parameter calibrated?
- Is it tested?

## 4.4 Baseline snapshot

Before tuning anything, run current tests and store baseline metrics.

Minimum:

- deterministic behavior;
- generation performance;
- structural failures;
- average score;
- best score;
- near-best pool;
- duplicate rate;
- density distributions;
- groove statistics;
- Sample Analysis accuracy on the available reference corpus.

The baseline must be stored in a machine-readable format where practical.

---

# 5. PHASE 1 — QUALITY MEASUREMENT INFRASTRUCTURE

This phase comes before major generator tuning.

The purpose is to build the “measurement machine” that prevents subjective coefficient tweaking.

---

# 6. GENERATION QUALITY LAB

Create or extend automated generation audit tooling.

Suggested direction:

`Tests/GenerationQualityLab.cpp`

or genre-specific audit executables.

Use the existing `BoomBapBatchAudit` concept as the starting point.

## 6.1 Batch size

Normal development benchmark:

- minimum: `1,000` seeds per important configuration.

Deep validation before accepting significant generation changes:

- `5,000–10,000` seeds where runtime permits.

Do not rely on 5–20 hand-picked seeds.

## 6.2 Representative parameter matrix

Test each main genre using representative values rather than every possible combination.

Example:

### Tempo
- low;
- nominal;
- high.

### Density
- low;
- default;
- high.

### Length
- 2 bars;
- 4 bars;
- 8 bars where phrase behavior matters.

### Substyles
Every production substyle.

---

# 7. GENERATION METRICS

Collect structured metrics for every pattern.

## 7.1 Kick

Measure:

- hits per bar;
- downbeat rate;
- strong-beat ratio;
- weak-beat ratio;
- syncopation;
- consecutive hits;
- kick/snare collisions;
- kick/bass relationship;
- repeated skeleton frequency;
- phrase-position behavior.

## 7.2 Snare / clap

Measure:

- expected backbeat coverage;
- displaced-snare rate;
- ghost rate;
- extra-snare density;
- anchor/ghost velocity hierarchy;
- missing anchors;
- genre-invalid placements.

## 7.3 Hats

Measure:

- carrier continuity;
- carrier density;
- gaps;
- rolls;
- roll lengths;
- machine-gun sequences;
- open-hat placement;
- open/closed interaction;
- accents;
- repeated motif rate.

## 7.4 Phrase

Measure:

- bar similarity;
- A/A' behavior;
- fill rate;
- fill length;
- fill density;
- turnaround rate;
- last-bar difference;
- first/last-bar relationship;
- phrase-event frequency.

## 7.5 Dynamics

Measure:

- velocity range;
- velocity standard deviation;
- repeated identical velocity rate;
- ghost velocity relative to anchor;
- accents;
- excessive randomization.

## 7.6 Timing

Measure:

- swing;
- microtiming distribution;
- anchor timing stability;
- secondary-note timing spread;
- timing outliers;
- humanize range.

## 7.7 Overall

Measure:

- total events/bar;
- polyphonic density;
- syncopation;
- repetition;
- novelty;
- phrase coherence;
- style-target distance;
- hard-rule violations;
- scorer quality;
- generation time.

---

# 8. VALIDATE THE SCORERS

A high internal score must correspond to a better musical pattern.

For each engine:

1. generate a large raw candidate set;
2. keep candidate scores;
3. divide candidates into:
   - top 10%;
   - middle;
   - bottom 10%;
4. compare their musical feature distributions;
5. manually audition representative candidates.

Questions:

- Do top-scoring candidates preserve the genre backbone better?
- Are they less cluttered?
- Do they have better phrase structure?
- Are hats more coherent?
- Are fills more musical?
- Are there hidden pathological patterns with high scores?

If high score does not correlate with better patterns, fix the scorer rather than hiding the problem with more candidates.

---

# 9. CRITICAL GATES

Prevent score compensation.

A catastrophic error must not be compensated by unrelated good metrics.

Example failure:

- terrible kick;
- good hats;
- good repetition;
- good density;
- final quality still high.

Define a small set of truly critical failures per genre.

Do not over-gate.

Hard gates exist only for patterns that are clearly musically invalid.

Examples:

## Boom Bap
- broken main backbeat;
- pathological kick spam;
- ghost louder than anchor;
- severe density failure;
- strong Trap leakage.

## Trap
- kick occupying protected snare positions repeatedly;
- broken 808/kick relationship;
- missing rhythmic carrier;
- extreme hat pathology.

## DnB
- broken two-step / core backbone;
- invalid snare axis;
- catastrophic kick density;
- invalid phrase resolution.

## Techno
- broken kick axis where required;
- destructive kick/percussion conflict;
- open-hat/carrier breakdown;
- invalid density for substyle.

---

# 10. QUALITY FLOOR BEFORE NOVELTY

Use the existing candidate-selection system correctly.

Principle:

> **QUALITY > NOVELTY**

Novelty is allowed to choose between already good candidates.

Novelty must not rescue a bad candidate.

Recommended selection logic:

## Stage 1 — Validity

Reject candidates that fail critical rules.

## Stage 2 — Quality floor

Reject candidates below the minimum usable threshold.

## Stage 3 — Near-best pool

Build a pool near the best candidate.

## Stage 4 — Controlled variation

Use novelty and seeded temperature only inside the good-candidate pool.

---

# 11. CANDIDATE COUNT CALIBRATION

More candidates do not automatically mean a better product.

Benchmark candidate counts:

- 16;
- 32;
- 48;
- 64;
- 96;
- 128.

For each measure:

- average selected quality;
- best candidate quality;
- human-audited quality;
- p50 generation time;
- p95 generation time;
- worst generation time.

Find the point of diminishing returns.

Do not increase candidate counts permanently unless quality gain justifies runtime cost.

---

# 12. DIVERSITY WITHOUT CHAOS

Measure diversity across sequential seeds.

For 100–1000 seeds calculate:

- exact duplicate rate;
- near-duplicate rate;
- identical kick skeleton rate;
- identical snare skeleton rate;
- identical hat skeleton rate;
- identical phrase-plan rate.

The goal is NOT:

> every seed must be completely different.

Good repetition is allowed.

A strong two-bar groove may legitimately repeat.

The problem to eliminate is:

> large numbers of seeds collapsing into only a tiny number of effective outputs.

---

# 13. SUBSTYLE SEPARATION

Prove that substyles are behaviorally different.

For every substyle collect aggregate feature vectors.

Compare:

- density;
- kick architecture;
- snare behavior;
- ghost frequency;
- hat carrier;
- swing;
- fill behavior;
- phrase structure;
- bass archetype;
- repetition;
- syncopation.

If two substyles produce nearly identical statistical distributions, improve the existing profile values or profile-specific logic.

Do not create unnecessary new engines.

---

# 14. DENSITY AS MUSICAL COMPLEXITY

Density must not simply mean:

> add more random notes.

Target behavior:

## Low density
- backbone;
- essential accents;
- minimal support.

## Medium density
- secondary notes;
- controlled ghosting;
- rhythmic support.

## High density
- additional movement;
- fills;
- accents;
- genre-valid ornamentation.

Anchors should remain stable while secondary complexity increases.

Test monotonicity:

`density 0.2 → 0.5 → 0.8`

The progression should be musically understandable.

---

# 15. MUTATE QUALITY

Mutate must preserve pattern identity.

A single Mutate should feel like:

> a variation of this groove.

Not:

> completely new generation.

Measure similarity before/after mutation:

- anchor preservation;
- kick skeleton similarity;
- snare skeleton similarity;
- hat changes;
- fill changes;
- phrase identity.

Multiple sequential mutations may gradually diverge.

---

# 16. LANE RG QUALITY

Lane regeneration must stay context-aware.

Examples:

- regenerated Kick must respect Snare;
- regenerated Hats must understand Kick/Snare;
- regenerated Bass must use Kick + harmony;
- dependent lanes must honor locks;
- lane RG must not overwrite unrelated locked material.

Do not generate isolated lanes without musical context unless the current architecture explicitly requires it.

---

# 17. MUSICAL REDUNDANCY PRUNING

The existing DnB idea is strong:

> If removing a secondary note does not reduce pattern quality, that note may not deserve to exist.

Study whether equivalent logic can improve other genres where appropriate.

Potential targets:

- ghost notes;
- secondary hats;
- percussion;
- fills;
- secondary kicks.

Do not blindly port DnB code.

Reuse the principle, not necessarily the implementation.

---

# 18. SIMPLE PATTERNS ARE VALID

Do not reward complexity for its own sake.

A musically strong pattern may be very simple.

Scoring must never assume:

`more notes = higher quality`

Anti-complexity bias should protect:

- space;
- groove readability;
- strong anchors;
- intentional repetition.

---

# 19. BAR-TO-BAR LOGIC

Do not force artificial variation.

Valid structures include:

- A A;
- A A A A;
- A A A B;
- A A' A B;
- A B;
- A A' B B'.

A bar may repeat exactly if repetition is musically justified.

The goal is to eliminate accidental cloning caused by a weak generator, not intentional repetition.

---

# 20. PHRASE INTELLIGENCE

Improve the existing phrase logic without replacing it.

Test 4/8/16-bar generation.

Phrase events should consider:

- substyle;
- density;
- current complexity;
- previous bars;
- phrase position;
- whether a fill is actually necessary.

Complex grooves may need subtle endings.

Simple grooves may support a stronger turnaround.

---

# 21. HUMANIZATION QUALITY

Humanization must have hierarchy.

Strong anchors should generally be more timing-stable than secondary notes.

Examples:

- main Snare: stable;
- Kick anchors: fairly stable;
- hats: moderate movement;
- ghost notes: more flexible.

Check interaction between:

- swing;
- timing;
- humanize;
- velocity.

Avoid uncontrolled random jitter.

---

# 22. PHASE 2 — REFERENCE-BASED GENERATION CALIBRATION

Use real musical reference material to estimate distributions.

Do not copy reference patterns.

Extract statistics.

Examples:

- kicks/bar;
- backbeat rate;
- ghost rate;
- hat density;
- swing;
- bar similarity;
- syncopation;
- fill rate;
- phrase-event rate.

Compare:

`reference corpus distribution`

vs

`HPDG generated distribution`.

The generated distribution does not need to be identical.

It should occupy a musically similar region.

---

# 23. CALIBRATION / VALIDATION SPLIT

Avoid overfitting.

Reference material should be divided into:

## Calibration set
Used for tuning.

## Validation set
Not used while adjusting parameters.

Suggested starting split:

- 70% calibration;
- 30% validation.

Major tuning is accepted only when validation also improves.

---

# 24. PHASE 3 — SAMPLE ANALYSIS BENCHMARK

Build a proper benchmark for Sample Analysis.

Use and extend:

`Tests/BreakTranscriptionLab.cpp`

Corpus categories:

## A. Pure drum loops
Known:
- BPM;
- beat 1;
- Kick;
- Snare;
- Hat;
- swing where possible.

## B. Full music samples
Drums mixed with:
- bass;
- melody;
- vocals;
- instruments.

## C. Tonal samples with no drums
Used to test false-positive resistance.

## D. Bass-heavy material
Used specifically to test false Kick detection.

## E. Bright/sibilant material
Used specifically to test false Hat detection.

A small high-quality annotated corpus is more valuable than a large unverified one.

Start with roughly 30–100 carefully annotated references.

---

# 25. SAMPLE ANALYSIS METRICS

Measure separately:

## Tempo
- absolute BPM error;
- BPM within tolerance;
- half-time error rate;
- double-time error rate;
- confidence calibration.

## Phase
- beat-origin error;
- downbeat error.

## Swing
- swing-percent error.

## Drums
Per lane:
- Precision;
- Recall;
- F1;
- false positives;
- false negatives.

## Timing
- hit timing MAE;
- microtiming preservation.

## Dynamics
- velocity correlation.

## Harmony
- root accuracy;
- major/minor accuracy;
- confidence.

## Bass
- pitch accuracy;
- octave errors;
- onset error;
- duration error;
- false notes.

---

# 26. MULTI-HYPOTHESIS TEMPO

Use the already existing `tempoCandidates`.

Do not collapse ambiguous tempo detection too early.

Always consider plausible metrical alternatives where appropriate:

- detected BPM;
- half-time;
- double-time.

Score candidates using existing musical evidence:

- grid fit;
- loop-length fit;
- backbeat fit;
- onset regularity;
- host tempo hint;
- repetition;
- phrase/downbeat consistency.

Host BPM remains a hint, not ground truth.

---

# 27. TEMPO CONFIDENCE CALIBRATION

Confidence must correspond to real reliability.

Important concept:

> Confidence is not only the absolute score of the best hypothesis. It also depends on how clearly it beats the alternatives.

Example:

`92 BPM = 0.81`
`184 BPM = 0.80`

Result:

low confidence.

Example:

`92 BPM = 0.91`
`184 BPM = 0.44`

Result:

high confidence.

Use best-vs-second-best margin where appropriate.

---

# 28. HALF/DOUBLE-TIME ERROR PROGRAM

Build explicit regression cases for:

- 70 / 140;
- 75 / 150;
- 85 / 170;
- 90 / 180.

Do not mix this with genre tempo interpretation.

First determine the sample's actual metrical hypothesis.

Then allow the genre engine to interpret tempo musically.

---

# 29. DOWNBEAT / ORIGIN QUALITY

Correct BPM with wrong beat 1 still breaks Copy Break.

Measure origin separately.

Use:

`abs(predictedOrigin - groundTruthOrigin)`

in seconds and/or ticks.

Tempo accuracy and phase accuracy must not be combined into one vague metric.

---

# 30. SWING DETECTION PROGRAM

Create synthetic/reference loops with known swing:

- 50%;
- 54%;
- 58%;
- 62%;
- 66%.

Ensure:

- straight loops remain straight;
- swung loops are recognized;
- genre does not bias the analyzer into false swing.

---

# 31. ONSET AND DRUM CLASSIFICATION

Optimize Precision and Recall together.

Do not optimize only for Recall.

Example bad result:

- Recall improves from 0.76 to 0.94;
- Precision collapses from 0.88 to 0.51.

This is not an improvement.

Use F1 and false-positive analysis.

---

# 32. MULTI-LABEL ONSETS

Preserve the current ability for one onset to represent more than one lane.

Examples:

- Kick + Hat;
- Snare + Hat.

Do not force mutually exclusive classification.

Improve confidence, not architecture.

---

# 33. ADAPTIVE THRESHOLDS

Thresholds should adapt to material.

Consider:

- local energy;
- lane-energy distribution;
- median activation;
- noise floor;
- sample dynamics;
- transient density.

A dusty break and a modern compressed loop should not be judged by one rigid absolute threshold.

---

# 34. FALSE KICK FROM BASS

This deserves a dedicated benchmark.

Use existing features:

- transientSharpness;
- onsetLow;
- sustain;
- harmonicStrength;
- percussiveStrength;
- HPSS.

Goal:

low-frequency harmonic/sustained events should not become Kick merely because they contain bass energy.

---

# 35. FALSE SNARE / FALSE HAT

Create tests for:

## False Snare
- vocal consonants;
- piano attack;
- guitar attack;
- bright transient instruments.

## False Hat
- vocal sibilance;
- cymbal wash;
- broadband noise;
- bright sustained textures.

High-frequency energy alone must not equal HiHat.

---

# 36. PHASE 4 — CONFIDENCE-AWARE GENERATION

This is one of the highest-priority improvements in the roadmap.

HPDG already calculates multiple confidence-like values.

The next step is to make sure uncertainty reaches the generator.

Target concept:

`analysis confidence`
→ `transcription confidence`
→ `GenerationHints strength`
→ `SampleAwareGenerationContext`
→ `genre engine influence`.

A weak guess must not have the same influence as a highly reliable detection.

Example:

- Kick probability 0.93 → strong influence;
- Kick probability 0.55 → moderate suggestion;
- Kick probability 0.22 → almost ignored.

---

# 37. DO NOT TURN CONFIDENCE INTO BINARY TRUTH TOO EARLY

Avoid behavior like:

`0.59 = false`
`0.60 = absolute truth`

Use continuous influence where possible.

Confidence should affect:

- generation hint strength;
- sample reactivity;
- support/contrast;
- lane influence;
- Copy-vs-Guide decisions;
- fallback to genre defaults.

---

# 38. GENERATION HINTS CALIBRATION

The current `GenerationHintsBuilder` already combines:

- current-step probability;
- neighboring probabilities;
- transcription confidence;
- metric boosts.

Do not replace this system.

Benchmark and calibrate it.

Investigate whether fixed weighting should become partially confidence-aware.

Example principle:

## Low-confidence transcription
Feature/lane evidence dominates.

## High-confidence transcription
Direct transcription may receive stronger weight.

Do not implement a specific formula without measuring it.

---

# 39. PREVENT TRANSIENT SMEARING

Neighbor smoothing can accidentally turn one real hit into three plausible positions.

Example:

real Kick = step 4.

Smoothing may raise:

- step 3;
- step 4;
- step 5.

This may encourage the generator to place support notes beside the actual accent.

Benchmark this effect.

Preserve the distinction between:

- primary evidence;
- neighbor context.

---

# 40. SAMPLE TYPE CONFIDENCE

Use existing evidence to estimate a simple material type:

- clear drum/percussion loop;
- mixed sample;
- strongly tonal sample;
- uncertain.

Do not build a large machine-learning classifier.

Use existing:

- sustain ratio;
- template fit;
- percussive strength;
- harmonic strength;
- transient density;
- drum-loop confidence.

Then adjust trust:

## Clear drum loop
Trust drum-break transcription strongly.

## Tonal sample
Trust harmony/bass more than K/S/H.

## Mixed sample
Blend cautiously.

## Uncertain
Fall back toward genre defaults.

---

# 41. COPY BREAK QUALITY

Copy Break is accuracy-oriented.

Its goal is not creativity.

Measure:

- Kick Precision/Recall/F1;
- Snare Precision/Recall/F1;
- Hat Precision/Recall/F1;
- timing MAE;
- velocity correlation;
- microtiming preservation;
- BPM error;
- origin error.

Copy Break should reproduce the original groove as faithfully as practical.

---

# 42. GUIDE GENERATION QUALITY

Guide Generation is creative.

It should use:

- accents;
- energy;
- groove;
- swing;
- phrase information;
- harmony;
- bass;
- sample mood.

But it should still create a new genre-valid pattern.

Guide Generation must not silently turn into Copy Break.

---

# 43. SUPPORT VS CONTRAST

Strengthen the existing concept.

## Support
Drums reinforce important sample accents.

## Contrast
Drums create complementary space and responses around sample accents.

Do not implement it as:

`more support = blindly copy more hits`.

It is a musical relationship, not merely a probability multiplier.

---

# 44. REACTIVITY

Reactivity controls how strongly sample evidence influences the genre engine.

Suggested conceptual behavior:

## Low
Genre grammar dominates.

## Medium
Genre + sample groove interact.

## High
Sample strongly shapes groove, while hard genre invariants remain.

Even maximum reactivity should not make Boom Bap stop behaving like Boom Bap.

---

# 45. GENRE GRAMMAR IS THE SAFETY NET

Except explicit Copy Break mode, sample hints remain guidance.

If analysis is uncertain or wrong:

genre grammar must protect the result.

Low-confidence sample analysis must not destroy:

- backbeat;
- kick architecture;
- DnB backbone;
- Techno axis;
- phrase structure.

---

# 46. HARMONY AND BASS QUALITY

Keep the existing analyzers.

Benchmark and improve reliability.

## Harmony
Test:
- key root;
- major/minor;
- confidence;
- tuning compensation.

Include detuned examples:

- -10 cents;
- -25 cents;
- +20 cents.

## Bass line
Test:
- pitch accuracy;
- octave mistakes;
- onset timing;
- note duration;
- false notes.

Especially test:

- Kick mistakenly interpreted as bass;
- harmonic overtone octave flips.

---

# 47. PHASE 5 — HUMAN LISTENING AUDIT

Mathematical scoring cannot fully define musical quality.

Add a developer audit export.

Example:

`HPDG_GenerationAudit`

Export for each pattern:

- seed;
- genre;
- substyle;
- BPM;
- internal quality;
- feature summary;
- MIDI;
- optional WAV.

Human reviewer assigns:

- 0 = unusable;
- 1 = major repair needed;
- 2 = usable with edits;
- 3 = good;
- 4 = would actually use.

---

# 48. SCORER / HUMAN CORRELATION

Compare human rating with internal quality.

If:

high scorer quality repeatedly receives low human ratings,

the scorer is optimizing the wrong things.

This check is more important than maximizing the numeric score itself.

---

# 49. PHASE 6 — FINAL A/B VALIDATION

For every accepted major change compare:

## OLD BASELINE
vs
## NEW VERSION

Generation comparison must include:

- structural failures;
- quality distribution;
- duplicate rate;
- style-target distance;
- substyle separation;
- density behavior;
- performance;
- human ratings where available.

Sample Analysis comparison must include:

- BPM accuracy;
- octave errors;
- downbeat/origin error;
- swing accuracy;
- K/S/H Precision/Recall/F1;
- timing MAE;
- false positives;
- Copy Break similarity;
- confidence calibration.

---

# 50. PERFORMANCE SAFETY

Track:

## Generation
- p50;
- p95;
- worst-case generation time.

## Sample Analysis
- total analysis time;
- memory;
- large-file behavior.

Never move expensive analysis/scoring work into the real-time audio callback.

Avoid regression of past DAW hang problems.

---

# 51. DEVELOPMENT ORDER

Use this order.

## Stage 1
Measurement infrastructure.

## Stage 2
Boom Bap quality.

Reason:
existing `BoomBapBatchAudit` provides a strong starting point.

## Stage 3
Trap quality.

## Stage 4
DnB quality.

## Stage 5
Techno quality.

## Stage 6
Tempo and downbeat analysis.

## Stage 7
K/S/H transcription.

## Stage 8
Confidence calibration.

## Stage 9
Copy Break.

## Stage 10
Harmony and bass.

## Stage 11
Confidence-aware Guide Generation.

## Stage 12
Human audit + final A/B validation.

Do not tune all genres simultaneously.

---

# 52. REQUIRED REPORT FORMAT

Every meaningful tuning stage must report concrete numbers.

Bad report:

> Improved Boom Bap generation.

Good report:

### Before
- 5,000 seeds
- hard failures: 31
- near duplicates: 17.4%
- average kick density: 5.6/bar
- validation reference: 3.2/bar
- p95 generation: 8.1 ms

### After
- 5,000 seeds
- hard failures: 0
- near duplicates: 9.7%
- average kick density: 3.6/bar
- validation reference: 3.2/bar
- p95 generation: 8.8 ms
- determinism regressions: 0

---

# 53. ROADMAP COMPLETION CONDITION

This roadmap is successful when HPDG is measurably better at:

1. rejecting bad generated patterns;
2. selecting good candidates;
3. maintaining genre identity;
4. producing useful variation;
5. distinguishing substyles;
6. understanding tempo and beat phase;
7. transcribing K/S/H reliably;
8. avoiding false drum detections;
9. preserving Copy Break groove;
10. using uncertainty intelligently;
11. feeding reliable sample evidence into the generator;
12. falling back safely to genre grammar when analysis is uncertain.

The desired result is:

> **HPDG does not become more random. HPDG becomes more selective, more confident, and more musically reliable.**

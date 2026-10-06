# HPDG — RULES
## Mandatory Rules for Generation and Sample Analysis Development

These rules apply to every agent or developer working on HPDG generation quality, Sample Analysis, Copy Break, Guide Generation, scoring, Style Targets, candidate selection, or related musical logic.

They exist to protect the current architecture from unnecessary rewrites and subjective tuning.

---

# RULE 1 — IMPROVE, DO NOT REPLACE

The task is to improve the current HPDG.

Do not rewrite working systems simply because a different architecture appears cleaner.

Existing systems include:

- genre-specific engines;
- scoring;
- candidate selection;
- hard validation;
- Sample Analyzer;
- DrumBreakTranscriber;
- GenerationHints;
- SampleAwareGenerationContext;
- harmony analysis;
- bass-line analysis.

Before proposing replacement, prove that the existing system cannot be corrected or calibrated.

---

# RULE 2 — MEASUREMENT BEFORE TUNING

Never start by changing generator coefficients.

First create or run the measurement system.

Required order:

1. inspect;
2. baseline;
3. identify failure;
4. formulate hypothesis;
5. make the smallest reasonable change;
6. rerun the same benchmark;
7. compare;
8. accept or revert.

---

# RULE 3 — NEVER JUDGE A GENERATOR BY A FEW SEEDS

A handful of good examples proves nothing.

Normal quality work must use:

- at least 1,000 seeds for important configurations.

Major generation changes should preferably be checked over:

- 5,000–10,000 seeds where runtime allows.

Never report success because three hand-picked patterns sound good.

---

# RULE 4 — SAME TEST BEFORE AND AFTER

A change is meaningful only if it is compared against the same baseline.

Use:

same genre;

same substyle;

same BPM;

same density;

same bar count;

same test corpus;

same metrics.

Do not change the test conditions after tuning.

---

# RULE 5 — QUALITY > NOVELTY

Do not select a bad pattern merely because it is different.

Novelty is useful only among already musically acceptable candidates.

Priority:

1. hard validity;
2. musical quality;
3. style fit;
4. controlled novelty.

---

# RULE 6 — SIMPLE CAN BE EXCELLENT

Never equate complexity with quality.

A sparse groove can be better than a dense groove.

Do not reward:

- extra Kick;
- extra Hat;
- extra Ghost;
- extra Perc;
- extra Fill

just because they increase event count.

Every secondary event should have a musical reason to exist.

---

# RULE 7 — REPETITION IS NOT AUTOMATICALLY A BUG

Do not force every bar to differ.

Allowed structures include:

- A A;
- A A A A;
- A A A B;
- A A' A B;
- A B.

A strong pattern may intentionally repeat.

Fix accidental repetition caused by weak generation.

Do not destroy intentional musical repetition.

---

# RULE 8 — DO NOT CHANGE A GENRE INTO A GENERIC GENERATOR

Preserve separate genre intelligence.

Boom Bap must remain Boom Bap.

Trap must remain Trap.

DnB must remain DnB.

Techno must remain Techno.

Do not merge them into one generic probability engine.

Shared infrastructure is acceptable.

Shared musical grammar is not automatically acceptable.

---

# RULE 9 — SUBSTYLE DIFFERENCES MUST BE REAL

A substyle name alone is not enough.

If two substyles statistically produce almost the same patterns, improve the existing profile differences.

Do not create fake diversity through random noise.

---

# RULE 10 — HARD GATES ONLY FOR TRUE FAILURES

Hard validation is powerful and dangerous.

Use hard gates only for patterns that are clearly invalid.

Do not turn stylistic preferences into rigid absolute rules unless the genre truly requires them.

---

# RULE 11 — DO NOT LET SCORE COMPENSATION HIDE A CATASTROPHIC ERROR

A broken backbone cannot be compensated by nice hats.

Examples:

- missing critical backbeat;
- pathological Kick density;
- broken DnB core;
- broken Techno kick axis;
- invalid anchor/ghost hierarchy.

Critical failures must reduce or reject the candidate before secondary beauty metrics are considered.

---

# RULE 12 — SCORE IS NOT THE MUSIC

Internal quality score is a proxy.

It is not ground truth.

Always validate that higher-scoring candidates are actually better through:

- structural metrics;
- reference distributions;
- listening tests.

If humans consistently prefer lower-scoring patterns, fix the scorer.

---

# RULE 13 — PRESERVE DETERMINISM

Same:

- seed;
- genre;
- substyle;
- BPM;
- density;
- settings

must produce the same output in the same software version.

An intentional algorithm update may change output relative to an old build.

But repeated runs of the new build must remain deterministic.

---

# RULE 14 — PRESERVE LOCK SEMANTICS

Locked lanes are protected.

No generation, mutate, lane RG, validation, repair, or post-processing step may silently rewrite a locked lane.

If coupled lanes exist, respect their documented lock semantics.

---

# RULE 15 — LANE RG MUST UNDERSTAND CONTEXT

Regenerating one lane does not mean ignoring the rest of the pattern.

Examples:

- Kick understands Snare;
- Hat understands Kick + Snare;
- Bass understands Kick + harmony;
- fill notes understand phrase location.

---

# RULE 16 — MUTATE IS NOT GENERATE NEW

A single Mutate should preserve recognizable pattern identity.

It should normally preserve important anchors and modify secondary details.

Large divergence should occur progressively through repeated mutation, not instantly.

---

# RULE 17 — DO NOT INCREASE CANDIDATE COUNT BLINDLY

More candidates increase CPU time.

Benchmark candidate counts.

Use the smallest count that reaches the useful quality plateau.

Always track:

- average quality;
- best quality;
- p50 runtime;
- p95 runtime;
- worst runtime.

---

# RULE 18 — REFERENCE MATERIAL IS FOR CALIBRATION, NOT COPYING

Do not reproduce reference patterns.

Use references to estimate distributions:

- hit density;
- syncopation;
- repetition;
- swing;
- fills;
- phrase structure.

HPDG should learn the musical space, not clone a song.

---

# RULE 19 — KEEP A VALIDATION SET

Never tune on the entire reference corpus.

Keep material outside the tuning set.

If calibration improves but validation gets worse, assume overfitting until proven otherwise.

---

# RULE 20 — CONFIDENCE MUST SURVIVE THE PIPELINE

This is a core HPDG rule.

Analysis uncertainty must propagate.

Target:

`Audio evidence`
→ `confidence`
→ `transcription`
→ `GenerationHints`
→ `SampleAwareGenerationContext`
→ `generation influence`.

Do not convert uncertain information into absolute truth prematurely.

---

# RULE 21 — WEAK EVIDENCE MUST HAVE WEAK INFLUENCE

Example:

- confidence 0.95 = strong guidance;
- confidence 0.55 = moderate guidance;
- confidence 0.20 = very weak guidance.

Exact formulas may vary.

The principle does not.

---

# RULE 22 — GUIDE GENERATION AND COPY BREAK ARE DIFFERENT PRODUCTS

## Copy Break

Goal:

accuracy.

It should reproduce:

- Kick;
- Snare;
- Hat;
- timing;
- groove;
- velocity as practical.

## Guide Generation

Goal:

creative adaptation.

It should generate a new genre-valid pattern influenced by the sample.

Never merge these two behaviors into one ambiguous mode.

---

# RULE 23 — GENRE GRAMMAR PROTECTS GUIDE GENERATION

In Guide mode, sample analysis is guidance.

It must not destroy genre identity.

If analysis confidence is weak:

fall back toward genre defaults.

Explicit Copy Break is the exception because its goal is transcription.

---

# RULE 24 — DO NOT USE THE HOST BPM AS GROUND TRUTH

Host BPM is a hint.

The sample may have a different BPM.

Tempo analysis must consider the audio evidence first.

---

# RULE 25 — HANDLE HALF/DOUBLE-TIME EXPLICITLY

Tempo ambiguity such as:

- 70 / 140;
- 85 / 170;
- 90 / 180

must be treated as alternative hypotheses.

Do not hide octave mistakes with arbitrary genre correction.

First estimate the sample's tempo hypothesis.

Then let the genre engine interpret musical tempo separately.

---

# RULE 26 — CONFIDENCE DEPENDS ON COMPETING HYPOTHESES

A best candidate is not automatically high confidence.

Example:

best = 0.81  
second = 0.80

Confidence should be low.

Example:

best = 0.92  
second = 0.43

Confidence can be high.

---

# RULE 27 — PRECISION AND RECALL MUST BE BALANCED

Never improve detection by flooding the result with false positives.

Track:

- Precision;
- Recall;
- F1.

A large Recall increase with a Precision collapse is not an improvement.

---

# RULE 28 — KEEP MULTI-LABEL DRUM EVENTS

One onset may legitimately contain:

- Kick + Hat;
- Snare + Hat.

Do not force mutually exclusive K/S/H classification.

---

# RULE 29 — LOW-FREQUENCY ENERGY IS NOT AUTOMATICALLY KICK

Bass instruments can contain strong low frequencies.

Kick detection must use:

- transient information;
- spectral onset;
- percussive evidence;
- sustain;
- harmonic structure.

---

# RULE 30 — HIGH-FREQUENCY ENERGY IS NOT AUTOMATICALLY HIHAT

HiHat detection must not trigger simply because audio is bright.

Watch for:

- sibilance;
- noise;
- cymbal wash;
- sustained high-frequency content.

Require appropriate onset/percussive evidence.

---

# RULE 31 — COPY BREAK MUST PRESERVE MICROTIMING

Do not destroy:

- tick;
- gridTick;
- timingOffsetTicks.

When Quantize = 0, preserve original pocket as closely as possible.

---

# RULE 32 — QUANTIZE MUST BE MATHEMATICALLY PREDICTABLE

Expected behavior:

## 0%
Original timing.

## 50%
Halfway toward quantized grid.

## 100%
Hard grid.

Protect this with regression tests.

---

# RULE 33 — DO NOT SMEAR ONE STRONG HIT INTO THREE EQUAL HINTS

Neighbor smoothing is contextual support.

It must not erase the distinction between:

- primary hit;
- adjacent possible support.

Check `GenerationHintsBuilder` behavior carefully.

---

# RULE 34 — DO NOT TRUST EVERY SAMPLE TYPE THE SAME WAY

A clear drum loop and a tonal pad require different trust.

Use existing evidence:

- drumLoopConfidence;
- sustainRatio;
- templateFit;
- harmonic strength;
- percussive strength;
- transient density.

Adjust which analysis branch is trusted.

---

# RULE 35 — DO NOT REPLACE WORKING ANALYSIS WITH CLOUD AI

HPDG must remain locally functional.

Do not add mandatory:

- OpenAI API;
- Anthropic API;
- cloud inference;
- subscription service;
- remote model dependency

for core generation or Sample Analysis.

---

# RULE 36 — DO NOT REDESIGN THE UI DURING QUALITY WORK

This program is about:

- generation quality;
- analysis quality;
- confidence;
- benchmarks.

UI changes should be minimal and only when needed to expose useful confidence or warning information.

Do not combine visual redesign with algorithmic tuning.

---

# RULE 37 — PROTECT REAL-TIME AUDIO

Do not run expensive:

- candidate benchmark;
- sample analysis;
- file export;
- large allocations;
- heavy scoring batches

inside the audio callback.

Avoid introducing DAW hangs or playback instability.

---

# RULE 38 — DO NOT ACCEPT A CHANGE THAT ONLY IMPROVES ONE NUMBER

A tuning change can improve one metric while damaging the product.

Example:

- Kick Recall ↑;
- Kick Precision ↓ drastically.

Or:

- internal score ↑;
- duplicate rate ↑ drastically.

Or:

- candidate quality ↑ slightly;
- generation time ×10.

Evaluate the full effect.

---

# RULE 39 — MAKE SMALL CHANGES

Prefer:

one hypothesis → one small change → benchmark.

Avoid changing ten coefficients and three algorithms at once.

Otherwise the cause of improvement/regression becomes unknowable.

---

# RULE 40 — KEEP COMMITS LOGICAL

Good examples:

- `Add generation quality benchmark infrastructure`
- `Calibrate Boom Bap candidate scoring`
- `Improve tempo ambiguity confidence`
- `Reduce bass-to-kick false positives`
- `Propagate sample confidence into generation hints`

Bad example:

- `Improve everything`

---

# RULE 41 — REPORT NUMBERS, NOT IMPRESSIONS

Never finish a task with:

> Generation feels better.

Report:

- number of seeds;
- test configuration;
- failure counts;
- quality distributions;
- duplicate rate;
- reference distance;
- performance;
- determinism;
- before/after.

---

# RULE 42 — HUMAN LISTENING REMAINS REQUIRED

Automation is necessary but not sufficient.

For significant generation changes, export random samples for blind/manual listening.

Do not only listen to hand-picked best seeds.

---

# RULE 43 — HUMAN RATINGS SHOULD TEST THE SCORER

Use a simple rating scale:

- 0 = unusable;
- 1 = major repair;
- 2 = usable;
- 3 = good;
- 4 = would use.

Compare ratings to internal scorer values.

If correlation is poor, fix scoring.

---

# RULE 44 — THE TARGET IS FEWER BAD RESULTS

The purpose of HPDG quality work is not primarily:

> produce more possible patterns.

It is:

> reduce the probability that the user receives a bad pattern.

This distinction must guide every change.

---

# RULE 45 — DO NOT DESTROY GOOD BEHAVIOR TO FIX RARE BAD BEHAVIOR

Before fixing an edge case, verify that the change does not damage the common case.

Use regression tests and large-seed comparison.

---

# RULE 46 — BASELINE MUST EXIST BEFORE MAJOR TUNING

For every genre being tuned, capture a baseline before changing behavior.

Recommended stable seed subset:

- 1;
- 2;
- 3;
- 10;
- 25;
- 42;
- 100;
- 256;
- 512;
- 999.

Use this for deterministic and structural checks.

Use larger seed sets for statistical quality.

---

# RULE 47 — DO NOT OVERFIT TO EXACT BASELINE NOTES

The purpose of baseline is not to freeze all old notes forever.

A quality improvement may intentionally change generated patterns.

Protect:

- deterministic behavior;
- invariants;
- interfaces;
- musical safety;
- regression expectations.

Do not block legitimate algorithmic improvement merely because notes changed.

---

# RULE 48 — CONFIDENCE-AWARE GENERATION IS A HIGH-PRIORITY PRINCIPLE

The system already has most required pieces.

Do not create a new subsystem unnecessarily.

Make existing confidence information actually affect generation strength.

The generator should know the difference between:

> “I am almost certain this is a Kick.”

and:

> “This might be a Kick.”

---

# RULE 49 — MEASUREMENT INFRASTRUCTURE COMES BEFORE BOOM BAP TUNING

Do not immediately start “improving Boom Bap”.

First make sure HPDG can answer:

- Was the old version better or worse?
- By how much?
- Which features changed?
- Did failure rate change?
- Did duplicates change?
- Did runtime change?
- Did scorer/human agreement improve?

Without this, coefficient tuning is guesswork.

---

# RULE 50 — REQUIRED DEVELOPMENT LOOP

Every generation-quality task should follow this exact mental model:

### 1. Generate 1,000–10,000 patterns.

### 2. Measure statistics.

### 3. Identify a concrete weakness.

### 4. Form a specific hypothesis.

### 5. Change the minimum necessary code.

### 6. Generate the same statistical sample again.

### 7. Compare before/after.

### 8. Run regression tests.

### 9. Audit random listening examples.

### 10. Accept or revert.

---

# RULE 51 — DO NOT “TUNE BY VIBE”

Never continuously change coefficients until a few outputs sound nicer.

That process is not reliable.

HPDG development should behave more like engineering/research:

- hypothesis;
- experiment;
- measurement;
- validation.

---

# RULE 52 — THE FINAL PRINCIPLE

HPDG should not become:

> a more complicated randomizer.

HPDG should become:

> **a more selective musical system that understands when its evidence is strong, when it is weak, and when the safest choice is to trust the genre grammar instead.**

The desired product behavior is:

> **Generate less garbage, keep the good groove, understand the sample, and never pretend uncertain analysis is certain.**

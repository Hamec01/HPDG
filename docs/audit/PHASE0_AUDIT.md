# Phase 0 — audit and baseline

Roadmap: `docs/ROADMAP_UPDATE.md` §4. Rules: `docs/RULES.md`.
Nothing in generation or analysis was tuned in this phase; the only production
change made at the same time is the Copy Break fix in commit `43e0dc9`
(measured separately, see §6).

## 1. Generation pipeline (product path)

All genres enter through `BoomBapGeneratorAudioProcessor::generatePattern()`:

1. **Parameters** — `buildParamsFromState()` (APVTS): genre, substyle, bars, density,
   swing, humanize, seed, key/scale. Substyle change → `applySelectedStylePreset()`
   writes the style's swing / velocity / timing / humanize / density defaults and
   lane enable / volume defaults (`StyleDefaults.cpp`).
2. **Tempo** — `resolveGenerationBpm()`: DAW sync > BPM lock > analysed sample tempo
   (`generationBpmForSample`, octave rules for DnB / Techno) > deterministic style range
   (hash of seed + substyle).
3. **Engine pass** — `runGenerationPass()` copies the project, runs the genre engine on the
   copy without the lock, swaps it in.
4. **Post-processing** — `applySampleAwarePostProcessLocked()` (only with an analysed
   sample; see §2), lane-sample rotation, sample-name carry-over, debug report,
   `rebuildMidiCache()`.

| Stage | Boom Bap | Trap | DnB | Techno |
|---|---|---|---|---|
| Entry | `BoomBapEngine::generateWithAlgebra` | `TrapEngine` → `TrapAlgebraEngine::generate` | `DnBEngine::search` | `TechnoEngine::search` |
| Style profile | `BoomBapStyleProfile` | `TrapStyleProfile` / `weightsForSubstyle` | `DnBStyleProfile` | `TechnoStyleProfile` |
| Phrase planning | `BoomBapGenerationContext` (archetype, rare phrase event, fill type), phrase roles statement / confirmation / development / turnaround | per-candidate in `generateCandidate` | topologies AAAA / AABA / … + bar roles | A / A' / F bar roles |
| Lane generation | `generateBar` per bar, kick motif, ghosts owned by anchors | matrix (`TrapAlgebraLanes`), 808 durations | relational grammar (snare → kick → ghosts → hats → fills) | axis → rumble → clap → hats → ride → perc → rolls |
| Candidates | 64 (clamped 24–64) | 32 (generate) / 24 (lane) | 48 | 48 |
| Hard validation | `validateAndRepair` (repair), hard gate only `hardTrapLeak` | none structural: `acceptedByThreshold` = quality ≥ qMin **or** Boltzmann > 0.2 | gates (`passedGates`) | gates: axis ≥ 0.95, clap not on 1/3 |
| Scoring | weighted **sum** of ~20 terms (scale ≈ 9–10) | arithmetic mean core 68 % + style 32 % − penalties | gates + geometric-mean core + secondary | geometric product of 6 fits |
| Pruning | — | — | A/B pruning of secondary notes | — |
| Near-best pool | `CandidateSelectionEngine` floor 9.0, tolerance 0.12 | `CandidateSelectionEngine` floor qMin, tolerance 0.08 | own: tolerance, softmax | own: tolerance 0.03, softmax 0.012 |
| Novelty | yes (0.72 internal + 0.28 surface, weight 0.07) | surface novelty, weight 0.10 | — | — |
| Style targets | `StyleTargetModel` (+0.16 × fit) | `StyleTargetModel` (+0.06 × fit) | inverted-U targets in scorer | inverted-U targets in scorer |
| Bass | `BoomBapBassGenerator` (opt-in lane, styles × amount); [2] + sample → `SampleBassLineComposer` | engine 808; follower re-pitch; [2] + sample → composer | `DnBBassGenerator` search + sample lens | `TechnoBassGenerator` search + sample lens |
| Seed dependence | seed + candidate index (bass: + generationCounter) | **seed + generationCounter** | **seed + generationCounter** | **seed + generationCounter** |

**Determinism (RULE 13).** Trap, DnB and Techno engines mix `project.generationCounter` into the
seed, so the same seed gives a different pattern after a different number of previous
generations in the session. Fresh-session replay is deterministic (lab: 0 failures), but
"same seed + same settings → same output" holds only from the same session state.

## 2. Sample Analysis pipeline

`SampleAnalyzer::analyzeBufferExtended`:

1. downmix to mono;
2. `DrumBreakTranscriber::analyze` — onsets, spectral K/S/H activations (multi-label),
   tempo candidates (grid fit, loop length, backbeat, prior, host hint), bar phase,
   swing, per-hit confidence, drum-loop confidence (sustain, template fit, grid fit),
   tick assignment with Quantize;
3. harmony — tuning estimate → `SampleHarmonyAnalyzer` (key, per-beat bass) →
   `SampleLineTranscriber` (bass line / melody, HPSS + salience + Viterbi) → bass refine;
4. origin shift, level normalisation;
5. `analyzePreparedMono` — STFT, `OnsetDetector`, `PercussiveHarmonicSeparator`,
   `LaneEventInferer` (lane evidence), `SampleTranscriber` (step transcription),
   `BasslineInferer`, `GenerationHintsBuilder`;
6. `SampleAnalysisBundle` → `updateSampleAwareContextLocked` → `SampleAwareGenerationContext`.

Consumers:
- **Guide (GenerateFromSample)**, musical sample (drum-loop confidence < 0.75):
  `SampleGuideAccents` (kick-likeness ≥ 0.35, reactivity ≥ 0.6 → 2 accents/bar else 1),
  `SampleBassLineComposer` (bass [2]), `SampleBassFollower` (segment confidence ≥ 0.35);
  DnB / Techno read the harmony through their own sample lens; Techno skips accents.
- **Guide with a drum loop** (≥ 0.75) and **Copy Break** → `ExtractPatternBuilder`
  (`transcription.drumEvents`, every event copied) → `PatternBlendEngine` (apply weights).

## 3. Quality mechanisms — used? survives? calibrated? tested?

| Mechanism | Used | Survives to output | Binary? | Calibrated | Tested |
|---|---|---|---|---|---|
| Boom Bap hard gate (`hardTrapLeak`) | yes | yes | yes | — | batch audit |
| Boom Bap repairs (`validateAndRepair`) | yes | yes | — | — | batch audit counts |
| Trap "hard" validity | yes, but soft (quality ≥ qMin **or** Boltzmann) | yes | — | no | — |
| DnB / Techno gates | yes | yes | yes | Techno targets on own grammar | core tests |
| Quality floor | Boom Bap 9.0 (scale ≈ 9–10), Trap qMin | yes | — | no | — |
| Near-best tolerance / temperature | all four | yes | — | no | — |
| Novelty weighting | Boom Bap, Trap | yes | — | no | — |
| Candidate count | 64 / 32 / 48 / 48 | — | — | **not benchmarked** | — |
| Secondary-note pruning | DnB only | yes | — | — | DnB tests |
| Style targets (`StyleTargetModel`) | Boom Bap, Trap | yes | — | no | — |
| Tempo confidence | analysis, UI | partially (trusted / not trusted) | **yes**: drives "trusted tempo" | no | synth bench |
| Tempo best-vs-second margin | inside confidence (`min(0.3, best − runnerUp)`) | yes | — | no | — |
| Per-hit confidence | transcriber | **lost in Copy** (all events copied) | — | no | — |
| Drum-loop confidence | branch Guide vs Extract | yes | **yes** (0.75) | no | — |
| Key confidence | key apply | yes | **yes** (0.4) | synth | harmony synth |
| Bass segment confidence | follower | yes | **yes** (0.35) | stems | lane tests |
| Reactivity | `SampleGuideAccents` only | yes | **yes** (≥ 0.6) | no | — |
| Support vs Contrast | **only RapEngine (hidden)** | no | — | no | — |
| GenerationHints (step weights) | **built, not consumed by any production engine** | **no** | — | no | — |
| Lane evidence | **not consumed by production engines** | **no** | — | no | — |
| Locks | all engines | yes | — | — | lane tests |
| Genre invariants | DnB / Techno gates, Boom Bap repair | yes | — | — | core tests |

Most important gaps for the roadmap:
1. **Confidence does not reach generation** (RULE 20): hints / lane evidence / support-vs-contrast
   are computed and dropped; the confidence that is used becomes binary thresholds (RULE 37).
2. **Score compensation** (RULE 11): Boom Bap (weighted sum) and Trap (arithmetic mean) can
   offset a bad core term with good secondary terms; only DnB / Techno have hard gates.
3. **Candidate counts are not benchmarked** (RULE 17).
4. **Determinism depends on session history** for Trap / DnB / Techno (RULE 13).

## 4. Generation baseline

Tool: `HPDG_GenerationQualityLab` (`Tests/GenerationQualityLab.cpp`) — real product path,
fresh processor per configuration, seed lock, the same metrics for every genre.
Files: `docs/audit/baseline/baseline_default_4bars_summary.json` (per configuration) and
`_patterns.csv` (per pattern); see §5 for the numbers.

## 5. Baseline numbers

Configuration: every production substyle, style-default density, 4 bars, seeds 1–1000,
fresh processor per configuration (`HPDG_GenerationQualityLab --seeds 1000`), build `235810b`.
"fail" = the lab's per-genre critical checks (backbone / kick spam / ghost hierarchy /
protected snare / Techno axis / perc on kick); "exact dup" = identical drum skeleton
(32nd grid, no velocity / offset) to an earlier seed; "near dup" = Jaccard ≥ 0.9 to one of
the previous 200 seeds; "kick dup" = identical kick skeleton to an earlier seed.
Determinism replay (seeds 1, 2, 3, 10, 25, 42, 100, 256, 512, 999, two fresh processors):
**0 failures in all 25 configurations.**

| Genre / substyle | fail | exact dup | near dup | kick dup | kicks/bar | backbeat | hats/bar | bar sim. | events/bar | p95 ms |
|---|---|---|---|---|---|---|---|---|---|---|
| Boom Bap Classic | 0.0 % | 0.0 % | 4.2 % | 2.1 % | 3.42 | 1.00 | 7.9 | 0.78 | 13.5 | 10.5 |
| Boom Bap Dusty | 0.0 % | 0.0 % | 0.9 % | 2.2 % | 3.41 | 1.00 | 7.4 | 0.77 | 13.1 | 13.8 |
| Boom Bap Jazzy | 0.0 % | 0.0 % | 1.0 % | 1.6 % | 3.61 | 1.00 | 7.7 | 0.76 | 13.6 | 11.9 |
| Boom Bap BoomBapGold | 0.0 % | 0.0 % | 2.7 % | 1.9 % | 3.62 | 1.00 | 7.9 | 0.78 | 13.7 | 9.9 |
| Boom Bap RussianUnderground | 0.0 % | 0.0 % | 2.2 % | 2.8 % | 3.23 | 1.00 | 7.2 | 0.78 | 12.5 | 9.3 |
| Boom Bap LofiRap | 0.0 % | 0.0 % | 3.5 % | 2.3 % | 3.19 | 1.00 | 7.2 | 0.78 | 12.5 | 9.6 |
| Trap ATLClassic | 0.0 % | 0.0 % | 0.0 % | 99.1 % | 1.67 | 1.00 | 10.5 | 0.53 | 15.1 | 35.5 |
| Trap DarkTrap | 0.0 % | 0.0 % | 0.0 % | 99.1 % | 1.67 | 1.00 | 10.2 | 0.54 | 14.7 | 32.5 |
| Trap CloudTrap | 0.0 % | 0.0 % | 1.1 % | 99.1 % | 1.67 | 1.00 | 9.7 | 0.55 | 14.1 | 34.6 |
| Trap RageTrap | 0.0 % | 0.0 % | 0.0 % | 99.1 % | 1.73 | 1.00 | 11.4 | 0.51 | 16.4 | 42.0 |
| Trap MemphisTrap | 0.0 % | 0.0 % | 0.0 % | 99.1 % | 1.67 | 1.00 | 10.6 | 0.53 | 15.3 | 40.3 |
| Trap LuxuryTrap | 0.0 % | 0.0 % | 0.1 % | 99.1 % | 1.68 | 1.00 | 9.9 | 0.54 | 14.4 | 35.3 |
| DnB Modern | 0.0 % | 0.2 % | 2.9 % | 79.4 % | 1.87 | 1.00 | 9.2 | 0.84 | 15.5 | 12.1 |
| DnB Roller | 0.0 % | 0.7 % | 12.0 % | 85.7 % | 2.87 | 1.00 | 10.8 | 0.92 | 18.2 | 13.4 |
| DnB Liquid | 0.0 % | 0.6 % | 11.3 % | 85.7 % | 1.86 | 1.00 | 8.2 | 0.86 | 15.5 | 10.2 |
| DnB Neurofunk | 0.0 % | 0.1 % | 10.8 % | 69.3 % | 2.89 | 1.00 | 10.7 | 0.84 | 17.5 | 12.6 |
| DnB Jump-Up | 0.0 % | 3.3 % | 28.5 % | 93.8 % | 1.97 | 1.00 | 8.8 | 0.90 | 14.2 | 12.0 |
| DnB Breakbeat | 0.0 % | 0.0 % | 0.4 % | 35.1 % | 3.89 | 1.00 | 10.5 | 0.77 | 20.0 | 24.4 |
| Techno Peak Time | 0.0 % | 57.3 % | 95.1 % | 99.0 % | 3.95 | 0.94 | 8.5 | 0.91 | 20.1 | 4.0 |
| Techno Hypnotic | 0.0 % | 20.4 % | 75.3 % | 99.0 % | 3.95 | 0.51 | 8.6 | 0.96 | 26.5 | 8.2 |
| Techno Minimal | 0.0 % | 68.2 % | 95.1 % | 99.1 % | 4.02 | 0.41 | 6.3 | 0.96 | 14.2 | 4.0 |
| Techno Detroit | 0.0 % | 43.7 % | 94.9 % | 99.0 % | 3.99 | 0.85 | 9.0 | 0.93 | 23.6 | 4.8 |
| Techno Dub | 0.0 % | 60.8 % | 93.7 % | 99.2 % | 4.00 | 0.55 | 6.5 | 0.98 | 17.1 | 5.2 |
| Techno Acid | 0.0 % | 52.8 % | 93.6 % | 99.0 % | 3.99 | 0.86 | 8.1 | 0.96 | 19.1 | 3.8 |
| Techno Hard | 0.0 % | 31.8 % | 87.5 % | 99.0 % | 3.94 | 0.95 | 9.7 | 0.92 | 25.4 | 4.8 |

Reading (what is a finding, what is by design):
- **Techno: seed collapse** — 20–68 % of seeds reproduce an earlier drum pattern exactly.
  The kick-skeleton duplicate rate (99 %) is by design (four-on-the-floor axis); the exact
  duplicates are not: a near-argmax selection (tolerance 0.03, softmax 0.012) over a small
  grammar space returns the same optimum for many seeds. Highest-priority finding.
- **Trap: kick collapse** — 99.1 % of seeds reuse an earlier kick skeleton (≈ 9 distinct kick
  patterns per 1000 seeds, 1.67 kicks/bar); hats / 808 still make every pattern unique.
  The density control barely changes Trap (see §5.1).
- **DnB** — 0 failures. (A first run reported 0.3–11.6 % "snare on 1 / 3": a lab metric bug —
  rounding moved the last 32nd of a fill roll onto the next bar's downbeat. Fixed in the lab:
  a note's step is floor(tick / 240) and "on 1 / 3" needs an exact grid hit. Lesson for
  RULE 12: a failure metric is a proxy too and must be validated on examples.)
  Jump-Up is the most repetitive DnB style (3.3 % exact, 28.5 % near duplicates).
- **Boom Bap** — 0 failures, 0 exact duplicates, kick reuse 1.6–2.8 %; healthy diversity at 4 bars
  (but see the 2-bar collapse in §5.1).
- Techno backbeat coverage 0.41–0.95 is intended (clap probability per style), not a failure.
- Runtime: p95 ≤ 35 ms everywhere (Trap slowest, Techno fastest).

### 5.1 Density × length matrix

`--seeds 300 --density matrix --bars matrix` (densities 0.2 / style default / 0.8, bars 2 / 4 / 8):
`baseline_matrix_300_summary.json`, per-row table in `baseline_matrix_300_density.txt`
(`python tools/quality_matrix.py ...`).

- Events/bar rise monotonically with density in **66 / 75** genre × substyle × length cells.
- In **45 / 75** cells density changes events/bar by less than 1 (0.2 → 0.8): density is weak
  as a musical-complexity control (roadmap §14).
- **Trap**: density has almost no effect anywhere (+0.2 events/bar, kicks/bar flat at ≈ 1.67).
- **Techno Minimal / Dub**: density works backwards (0.8 gives fewer events than 0.2), and the
  duplicate rate grows with density (Minimal 2 bars: 70 → 80 % exact duplicates).
- **Boom Bap at 2 bars**: 25–56 % exact duplicates at density 0.2 (0 % at 4 and 8 bars) — the
  2-bar space collapses at low density.
- Boom Bap and DnB respond to density as intended (Boom Bap +1.5–2 events/bar, kicks 3.0 → 3.9;
  DnB kicks 1.9 → 2.8–4.6).

## 7. Priorities for Phase 1 (from the numbers, not impressions)

1. Techno seed collapse (20–68 % exact duplicates at defaults) and inverted density in Minimal / Dub.
2. Trap kick collapse (99.1 % kick-skeleton reuse) and an ineffective density control.
3. Boom Bap 2-bar low-density collapse (25–56 % exact duplicates).
4. Scorer validation (roadmap §8) per engine: top 10 % vs bottom 10 % candidates.
5. Candidate-count benchmark (16–128) for every engine (RULE 17).
6. Confidence → generation (RULE 20): GenerationHints / lane evidence are not consumed.


## 6. Sample Analysis baseline

| Benchmark | Material | Result |
|---|---|---|
| Synthetic breaks (`HPDG_BreakLab synth Samples/BoomBap --trials 200 --seed 7`) | 200 rendered breaks with ground truth | tempo 200/200, octave errors 0; F1 K 0.972 / S 0.976 / H 0.956; timing error 2.9 / 3.1 / 1.8 ms |
| Labeled tempo corpus, automatic | 159 custom boom bap loops + 84 classic breaks (tempo in filename) | exact (±0.05) 135/159 + 72/84; ≤ 2 % 153/159 + 82/84; octave errors 4 + 1 |
| Labeled tempo corpus, typed tempo | same 243 loops, tempo typed = label | beat 1 > 0.5 s off: 22 → 1 after `43e0dc9` |
| Bass line on stems (`HPDG_BreakLab lines`) | 8 multitrack stem sets (mix vs bass stem) | half-second windows correct 74–100 % (mean ≈ 93 %), all windows claimed |
| Harmony synth (`harmony-synth --trials 40`) | synthetic progressions | key exact 36/40 (+1 relative), bass pitch class 640/640 |

Missing for the roadmap's Phase 3: an annotated corpus with beat-1 / per-hit ground truth
(categories A–E), swing ground truth, false-kick / false-hat sets.

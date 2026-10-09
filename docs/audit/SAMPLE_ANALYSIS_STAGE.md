# Sample Analysis stage (roadmap §24-29, stage 6)

Same method as the genre stages: one hypothesis, the smallest change, the same benchmark before and
after. Tempo and phase are measured separately (§29). The tempo in the file name is the ground truth;
the host / typed tempo is only a hint.

## Benchmark — `tools/tempo_bench.py` + `docs/audit/reference/tempo_corpus.tsv`

- 11 corpora, 506 files. Audio stays outside git; the paths are local to the development PC.
  - **drums** (pure loops, trimmed to beat 1): boombap_custom 159, dnb_ghosthack 53, dnb_freaky 26,
    techno_ghosthack 50, trap_urban 15, trap_hybrid 10, trap_utt2 14, trap_cr2 20.
  - **breaks** (classic breaks, untrimmed: tempo only): classic_breaks 84.
  - **tonal** (music / bass loops, no drums): tonal_techno_music 38, tonal_techno_bass 37.
- `run` caches two `HPDG_BreakLab analyze --hits` reports per file: automatic tempo and typed tempo
  (`--bpm <label>`, phase only). `report` prints:
  - tempo: exact (≤ 0.5 BPM), ≤ 2 %, half, double, 2/3-type and other errors;
  - confidence → accuracy bins;
  - drum-loop confidence;
  - phase on trimmed corpora: |origin| (truth 0 ms), auto (only when the tempo is right) and typed,
    with the share that is a quarter or more off.
- Re-run after a change: delete the out folder and run `run`, then `report`.

## Baseline (before step 1)

| Corpus | tempo ≤ 2 % | half | auto origin ≤ 20 ms | typed origin ≤ 20 ms | typed ≥ ¼ off |
|---|---|---|---|---|---|
| Boom Bap custom | 96 % | 0 % | 90 % | 91 % | 0 % |
| Classic breaks | 98 % | 0 % | — | — | — |
| DnB Ghosthack | 75 % | 25 % | 95 % | 98 % | 2 % |
| DnB Freaky | 54 % | 46 % | 100 % | 92 % | 8 % |
| Techno Ghosthack | 92 % | 2 % | 76 % | 84 % | 16 % |
| Trap Urban | 27 % | 73 % | 25 % | 33 % | 67 % |
| Trap Hybrid | 30 % | 70 % | 67 % | 50 % | 50 % |
| Trap UTT2 | 43 % | 57 % | 50 % | 43 % | 57 % |
| Trap Cr2 | 45 % | 55 % | 78 % | 55 % | 45 % |
| Tonal (music + bass) | 69 % | 8 % | 44 % | 59 % | 30 % |

Other findings:
- **Over-confidence.** Drums: 330 of 347 files have tempo confidence ≥ 0.8, but only 80 % of them
  have the right tempo. Every wrong Trap tempo is a confident half-time reading.
- **Tonal loops look like drum loops.** 68 % of loops without drums get drum-loop confidence ≥ 0.5.

## Step 1 — beat 1 of half-time loops (snare on beat 3)

**Cause.** The downbeat / origin search scored each candidate beat 1 with `backbeatScore`, which
assumes the snare sits on beats 2 and 4. In half-time Trap the snare (clap) sits on beat 3. The best
backbeat then appears a quarter off, so even with the right typed tempo, beat 1 was a quarter off
in 45-67 % of Trap loops.

**Change** (`Source/Analysis/DrumBreakTranscriber.cpp`):
- `phaseBackbeatScore` = max(backbeat on 2 & 4, half-time score). The half-time score is
  0.75 × (share of snares on beat 3) + 0.25 × (kick on beat 1).
- It decides only beat 1, in both the automatic and the typed-tempo path.
- The tempo choice keeps the old score, so tempo selection is unchanged by construction.

**Result** (same 506 files):

| Corpus | auto origin ≤ 20 ms | typed origin ≤ 20 ms | typed ≥ ¼ off |
|---|---|---|---|
| Trap Urban | 25 → **100 %** | 33 → **100 %** | 67 → **0 %** |
| Trap Hybrid | 67 → **100 %** | 50 → **100 %** | 50 → **0 %** |
| Trap UTT2 | 50 → **100 %** | 43 → **100 %** | 57 → **0 %** |
| Trap Cr2 | 78 → **100 %** | 55 → **100 %** | 45 → **0 %** |
| DnB Freaky | 100 % | 92 → **100 %** | 8 → **0 %** |
| Techno Ghosthack | 76 → 78 % | 84 → 86 % | 16 → 14 % |
| Tonal | 44 → 50 % | 59 → 64 % | 30 → 23 % |
| **All drums** | 86 → **90 %** | 83 → **93 %** | 13 → **2 %** |

- Tempo accuracy, half/double errors, confidence and drum-loop confidence are identical on every
  corpus.
- Boom Bap typed and classic breaks are unchanged. 4 Boom Bap auto reports changed; all are
  double-tempo readings whose origin moved closer to 0 (e.g. −1068 → 3 ms).
- Techno loops GUT 01 / 04 / 22 moved by one quarter (e.g. −1143 → −1651 ms). They were already
  wrong in the baseline: the four-on-the-floor kick falls on 16ths 2 / 6 / 10 / 14, so the grid is
  a 16th off as well as quarters off. That is a separate 16th-phase issue, not caused by this step.
  GUT 14 and GUT 15 were fixed (−488 → 0 ms and −976 → 0 ms).
- Tests: CoreTests 77/77 PASS, SampleTrimTests, LaneBoundaryTests PASS.

## Step 2 — half / double time from the onset rate (§28)

**Cause.** A half-time Trap loop at 150 BPM, with its snare on beat 3, looks exactly like a 75 BPM
loop with the snare on 2 & 4. The 2 & 4 backbeat term (0.6) favoured the slow reading. The grid fit
of the fast reading was better, but the grid gap does not separate the two cases: swung Boom Bap
also gains +0.2 to +0.5 grid at double tempo.

**Evidence** (onsets per sixteenth of the slow reading; strokes on several lanes within 15 ms count
once; p10 / median / p90):

| Corpus | p10 | median | p90 |
|---|---|---|---|
| Boom Bap | 0.56 | 0.67 | 0.76 |
| Classic breaks | 0.55 | 0.69 | 0.97 |
| Trap (4 packs) | 0.96-1.03 | 1.13-1.31 | 1.35-1.69 |
| DnB | 0.73-0.86 | 1.00-1.16 | 1.36-1.44 |

At the slow reading, Trap hat rolls (1/32 at 140-160 BPM) and DnB ghost notes become 64ths. Real
loops at their written tempo do not need more than about one onset per sixteenth.

Sources:
- Octave errors are the main tempo-estimation failure, and candidate support / onset density are
  known correction signals: Schreiber 2020 (PhD thesis, AudioLabs Erlangen); arXiv 2401.00209
  (review of tempo estimation).
- Trap is written at 120-160 BPM with a half-time feel and hat rolls on a 1/32 grid: eMastered
  "Trap Drum Patterns".

**Change.** The new `subdivisionOverload(hits, bpm)` is 0 up to 0.85 onsets per sixteenth and
reaches 1 at 1.15. The candidate score subtracts 0.5 × overload. It is evidence from the sample
itself, with no genre hint (§28).

**Parameter choice.** The parameters were simulated on the cached candidate scores before coding.
The neighbouring settings (k 0.3-0.7, threshold 0.75-0.9, width 0.3-0.4) give the same picture, so
the result is not a knife-edge fit.

**Result** (tempo within 2 %, same 506 files):

| Corpus | before | after |
|---|---|---|
| Boom Bap | 96 % | 96 % |
| Classic breaks | 98 % | 96 % (1 file: Brian Auger "Compared To What", a busy 79 BPM funk break, now 158) |
| DnB Ghosthack | 75 % | **94 %** |
| DnB Freaky | 54 % | **77 %** |
| Techno | 92 % | 92 % |
| Trap Urban | 27 % | **87 %** |
| Trap Hybrid | 30 % | **90 %** |
| Trap UTT2 | 43 % | **79 %** |
| Trap Cr2 | 45 % | **100 %** |
| **All drums** | 79 % | **93 %** |
| Tonal | 69 % | 71 % (some 3/4 errors became 3/2) |

- Phase is unchanged: all-drums auto origin ≤ 20 ms is 91 %, typed 93 %.
- Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

## Step 3 — honest tempo confidence (§27)

**Cause.** A whole-bar loop raised its confidence to 0.45 + 0.5 × grid, whatever the alternatives
scored. After step 2, 23 of the 25 wrong drum-loop tempos still reported confidence ≥ 0.8.

**Evidence** (drum loops; margin = best score − the best candidate more than 2 % away):

| margin | n | tempo right |
|---|---|---|
| < 0.1 | 23 | 48 % |
| 0.1-0.2 | 30 | 87 % |
| 0.2-0.3 | 48 | 88 % |
| 0.3-0.5 | 93 | 97 % |
| ≥ 0.5 | 153 | 100 % |

Breaks and tonal loops show the same picture: below 0.1 the tempo is right in 37-75 % of files, from
0.3 up in 100 %. This is the roadmap §27 rule: confidence depends on how clearly the best tempo beats
the alternative.

**Change.** The confidence is capped at 0.5 + 2 × margin, within [0.5, 1].
- The floor 0.5 keeps every drum loop's tempo trusted in `SampleAnalyzer` exactly as before
  (drum-loop confidence ≥ 0.75 and tempo confidence ≥ 0.5).
- A tonal sample needs tempo confidence ≥ 0.8 to be trusted, so an ambiguous tempo there now falls
  back to the session tempo.

**Result.**
- Tempo choices are identical on every file.

| | before | after |
|---|---|---|
| drum loops: wrong tempo with confidence ≥ 0.8 | 23 / 25 | **11 / 25** |
| drum loops: right tempo with confidence ≥ 0.8 | 311 / 322 | 296 / 322 |
| drum loops: accuracy in the ≥ 0.8 bin | 93 % | **96 %** |
| tonal: wrong with confidence ≥ 0.8 | 2 / 22 | 1 / 22 |
| tonal: right with confidence ≥ 0.8 | 33 / 53 | 31 / 53 |

- Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

## Step 4 — melodic / bass loops are not drum loops

**Why it matters.** In the plugin, drum-loop confidence ≥ 0.75 means "clearly a drum loop". In Guide
mode such a sample gets its K/S/H transcription applied, so a melodic loop mistaken for drums has
kicks and an 808 line extracted from its bass notes and chords. The flag also makes the plugin trust
a weaker sample tempo (≥ 0.5 instead of ≥ 0.8) and keeps the measured bar phase.

**Tonal corpus widened.** 7 packs added to `tempo_corpus.tsv`, all with tempo and key in the file
names, 377 tonal loops in total:
- Cymatics Diamonds II, Cobra and Lofi Toolkit melody loops;
- Raw Hip-Hop melodic and bass loops;
- Freaky Loops DnB music and bass loops.

The Ghosthack techno loops are the calibration set and the 6 new packs the validation set (RULES:
same test before / after, no tuning on the test set).

**Cause.** Percussive bass and pluck loops have no sustain (0.00-0.10), and the K/S/H templates
"explain" them (template fit 0.8-0.95). Neither term separates them from drums.

**Evidence.** The share of power above 4 kHz, where hat and snare noise sits:

| | p10 | median | p90 |
|---|---|---|---|
| drum corpora (8) | 0.6-12 % | 1.5-29 % | 3.8-39 % |
| tonal packs (9) | 0 % | 0-0.4 % | 0-2 % |

The darkest drum loops are lo-fi Boom Bap (minimum 0.19 %).

**Change.**
- `computeHighBandShare` reads the share from the transcriber's band spectrogram.
- Drum-loop confidence is multiplied by 0.6 + 0.4 × f, where f fades from 1 at 0.4 % to 0 at 0.1 %
  (log scale).
- The value is printed in the report as `high band`.
- Simulation before coding: the neighbouring settings (fade 0.05-0.15 % → 0.2-0.6 %, floor 0.6-0.7)
  gave the same counts.

**Result** ("clearly a drum loop", confidence ≥ 0.75):

| | before | after |
|---|---|---|
| drum loops (8 corpora) | 320 / 347 | 320 / 347 (identical per corpus) |
| classic breaks | 42 / 84 | 42 / 84 |
| tonal, calibration (techno) | 26 / 75 | 7 / 75 |
| **tonal, validation (6 packs)** | 14 / 302 | **1 / 302** |

- Tonal loops whose tempo the plugin trusts: 82 → 66, of which wrong 14 → 12.
- Tempo and phase are unchanged.
- `tempo_bench.py` now reports the share ≥ 0.75 (the plugin's threshold) instead of ≥ 0.5.
- Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

## Step 5 — key of the loop (`SampleHarmonyAnalyzer`)

**Benchmark.** `tempo_bench.py run-key` / `key-report` run `HPDG_BreakLab analyze --harmony` on
the 377 tonal loops and score the key against the file name:
- MIREX weighting: exact 1, fifth 0.5, relative 0.3, parallel 0.2;
- 97 files name only a root (Freaky DnB bass, some Raw Hip-Hop bass) and are scored on the root;
- 3 Ghosthack files whose names use a Cyrillic "С" cannot be opened by the lab and are skipped.

The lab now passes the sample's tuning exactly as `SampleAnalyzer` does, and prints the key
decision's inputs (chroma, bass share, opening bass note).

**Baseline.** MIREX 0.50, exact 41 %. The errors:
- fifth 11 %;
- parallel 11 % (bass loops rarely play the third);
- other 33 %, often a minor key read as its VI or VII major (i-VI-VII progressions).

Confidence was no guide: 44 % exact at confidence ≥ 0.6.

**Experiments, offline before coding.**
- A Python replica of the decision reproduces the plugin on 377 / 377 files.
- Chroma variants were computed from the audio: summed spectrum (current), spectral peaks only,
  stability-weighted, harmonic salience, top-3 notes per frame.
- These were crossed with three profiles (Krumhansl-Kessler, Temperley, Albrecht & Shanahan 2013),
  bass weight, opening-note weight and a minor prior.
- Selection by leave-one-pack-out over the 9 packs: choose on 8, score the 9th.
- The same configuration won in 7 of 9 folds and every held-out pack improved (mean per-pack MIREX
  0.49 → 0.61): peak chroma, no compression, Albrecht-Shanahan profiles, bass 0.2, opening note
  0.35, minor prior.

**Final weights.** Opening note 0.5 and minor prior 0.1.
- The labelled loops are 255 minor / 28 major, which matches the genres here (EDM key work:
  Faraldo et al. 2016-17).
- With opening 0.35 the lane test "guide bass follows sample bass and key" broke. Its sample is
  Am-F-C-G (i-VI-III-VII) with loud bass roots, so the chroma correlates with F major 0.84 vs
  A minor 0.44, and A minor won or lost on details of the beat segmentation.
- Excluding the bass register from the chroma (≥ 110-220 Hz) does not help: real-pack MIREX
  0.64 → 0.60-0.63.
- Opening 0.5 gives that case a clear margin and helps the majors, for −0.01 per-pack MIREX
  (simulation).

**Result** (real lab, same 377 files):

| | before | after |
|---|---|---|
| MIREX | 0.50 | **0.65** |
| exact | 41 % | **56 %** |
| major loops right | 7 / 28 | 9 / 28 |
| synthetic harmony test (40 progressions) | 36 / 40 | 36 / 40 |

Per pack (MIREX):
- Techno bass 0.35 → 0.73;
- Raw Hip-Hop bass 0.48 → 0.76;
- Techno music 0.57 → 0.78;
- Raw Hip-Hop melodic 0.46 → 0.69;
- Freaky DnB music 0.48 → 0.64;
- Cymatics Diamonds 0.62 → 0.72;
- Cymatics Lofi 0.43 → 0.49;
- Freaky DnB bass 0.42 → 0.47;
- Cymatics Cobra 0.75 → 0.71 (more fifth errors).

Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

Sources:
- Albrecht & Shanahan (2013), "The Use of Large Corpora to Train a New Type of Key-Finding
  Algorithm", Music Perception 31(1);
- Krumhansl (1990);
- Temperley (2001);
- Faraldo, Gómez, Jordà, Herrera (2016), "Key Estimation in Electronic Dance Music", ECIR;
- Essentia `Key` profile notes.

## Step 6 — what the sample states about itself (file name, WAV acid chunk)

**Cause.** The plugin ignored the tempo and key that nearly every commercial sample carries in its
name ("Melody Loop 1 - 70 BPM G# Min", "rhh_bass_loop_90_Am"). FL Studio renders and "acidized" packs
also carry the tempo in the WAV `acid` chunk: 180 of the 883 corpus files, every one equal to the
label, and 1690 of the maintainer's 3134 FL renders.

**Change.**
- `SampleLabelReader` (new) reads the tempo from the `acid` chunk (via JUCE metadata), else from the
  name (explicit "BPM", else a single bare number 60-200 that is not a counter), and the key from the
  name ("G# Min", "_Am", "95Em", "Fmin", "Gsharp", "Dshrp", Cyrillic look-alikes).
- `SampleAnalyzer::analyzeAudioFileExtended` passes these to both analyzers through the request.
- Tempo: the stated tempo is evaluated exactly (no local refinement) and gets +0.6 in the candidate
  score. If it wins, the tempo is kept exactly (no length snap or regression) and the confidence is
  0.95.
- Key: the stated key is taken unless its scale clashes with the notes heard (chroma correlation
  with its profile < -0.1). Measured: true labels kept 94.7 %, a tritone-off label 15 %, a
  semitone-off label 24 %. A root-only label takes the mode the audio prefers. A rejected label
  caps the key confidence at 0.5.

**Lab and bench.**
- The lab gains `--labels` (read like the plugin), `--label-bpm` and `--label-key` (inject).
- The lab now reads its command line as UTF-16 on Windows, so Cyrillic paths open.
- `tempo_bench.py run|run-key --labels name|wrong` runs with the files' own labels or deliberately
  wrong ones (tempo x4/3, key a tritone off).
- A corpus folder may be a `.txt` list of paths, and the bpm regex `acid` takes the truth from the
  chunk.
- New corpora:
  - `user_renders`: 120 of the maintainer's FL renders, 4-30 s, spread over 70-180 BPM;
  - `user_hamlo_pack`: 12 loops with tempo and key in free-form names;
  - `user_piano`;
  - `user_song_stems`: 13 stems of one track, 79 Am.

**Result:**

| | audio only | with the files' labels | with wrong labels |
|---|---|---|---|
| tempo, drum loops | 93 % | **99 %** | 93 % |
| tempo, classic breaks | 96 % | **100 %** | 96 % |
| tempo, tonal loops (13 packs) | 48 % | **94 %** | 41 % |
| tempo, maintainer's FL renders (120) | 42 % | **96 %** | 34 % |
| key, tonal loops (MIREX / exact) | 0.64 / 56 % | **0.95 / 95 %** | 0.61 / 53 % |

- Wrong drum and break tempo labels were all rejected (tempo unchanged from audio only).
- On tonal loops some wrong labels pass because the audio itself is unsure there.
- Remaining errors with labels:
  - a few mislabelled packs: "DILLA ... 88Bpm" is exactly 4 bars at 91.9, and the analyzer keeps
    the audio tempo;
  - tonal loops whose audio rejects the true tempo label.
- Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

**Audio-only finding on the maintainer's material.** These are the most realistic test.
- Tempo is right on 42 % of FL renders, 42 % of HamloProd pack 2 and 8 % of the song stems.
- The errors are systematic:
  - x2 (85 -> 170, 79 -> 158);
  - x3/2 (70 -> 105: triplets or swing).
- The renders sit at 70-95 BPM. On melodic parts the analyzer counts every note as a beat and
  drifts to 140-180. This is the next step.

## Step 7 — loops are 4 / 8 bars; genres fold the sample tempo into their range

**Maintainer's point (2026-10-09).** An octave reading of a sample (80 vs 160) does not matter
musically: Boom Bap plays around 80 and Trap around 160 over either. Each engine has to fold the
sample tempo into its own range. The x3/2 and x3/4 readings are the errors that break the grid.
The bench now also reports **up-to-octave** accuracy (within 2 % of the truth, x2 or x1/2).

**Engines.** `generationBpmForSample` (`Source/Engine/TempoInterpretation.h`) folded only DnB (up to
≥ 120) and Techno (up to ≥ 100). Now Trap / Drill double a sample at ≤ 90 BPM and Boom Bap halves
one at ≥ 120 BPM, the same rule `interpretedBpmForGenre` uses for the host tempo. Sample time already
maps onto pattern ticks with the octave relation, so the sample stays in sync. Tests: CoreTests
checks 160 → 80 (Boom Bap), 92 → 92, 80 → 160 (Trap), 150 → 150, 70 → 140 (Drill).

**Cause of the melodic-loop errors.** For the errors on tonal loops and renders, the grid, length
and backbeat terms barely separate the winner from the truth. The x2 / x3/2 / x3/4 readings are the
other whole-bar counts of the same file length: a 12 s loop is 4 bars at 80, 8 at 160, 6 at 120,
3 at 60.

**Evidence: bar counts of the true tempo.**

| | 4 bars | 8 bars | 2 bars | others |
|---|---|---|---|---|
| drums | 267 | 14 | 15 | 1, 5, 9, 16 bars: 10 |
| tonal | 159 | 65 | 42 | 3, 5, 9-24 bars: 25 |
| renders | 63 | 19 | 0 | 6 bars: 1 |

**Tried offline.**
- Gating the step-2 density penalty by high-band noise gave little: renders +8 points.
- A tempogram prototype (spectral-flux ACF with loop-length candidates): renders 67-72 % alone.
  Added to the analyzer's scores it gave no gain over the bar prior, so it was not ported.
- A tempo prior around 100 BPM hurt DnB music (174).

**Change.** A whole-bar candidate (length fit ≥ 0.5) gets 0.3 × (4 or 8 bars: 1, 2 bars: 0.7,
16 bars: 0.5, 1 bar: 0.3, else 0).

**Result** (same files, audio only):

| | exact (≤ 2 %) before → after | up to octave, before → after |
|---|---|---|
| drum loops | 93 → **96 %** | 99 → **100 %** |
| classic breaks | 96 → 96 % | 99 → 99 % |
| tonal loops (13 packs) | 48 → **56 %** | 66 → **78 %** |
| maintainer's FL renders (120) | 42 → **57 %** | 60 → **79 %** |
| HamloProd pack 2 (12) | 42 → 42 % | 67 → **83 %** |

Per pack, up to octave:
- Techno music 0.82 → 0.92;
- Techno bass 0.72 → 0.95;
- Cymatics Diamonds 0.71 → 0.95;
- Lofi 0.58 → 0.81;
- Raw Hip-Hop melodic 0.70 → 0.90;
- Raw Hip-Hop bass 0.85 → 0.97.

Worse: **Freaky DnB music 0.48 → 0.36**. Its files carry reverb tails (13.79 s = 10 bars at 174,
12.41 s = 9 bars), so the length is not a whole number of bars at the true tempo and the prior
picks a false whole-bar reading (77.3 BPM = 4 bars of 13.79 s). Next: detect the tail and measure
the length without it.

Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

## Step 8 — weak onsets: the loop length decides

**Cause.** 26 of the maintainer's FL renders were still wrong after step 7 (up to octave).
- 18 of them were exactly 4 or 8 bars at the acid tempo, and the truth was among the candidates.
  The winner was a x3/2 reading (85 -> 127.5, 77 -> 115.5) with 6 or 12 bars. Swing (64-68 %)
  makes a triplet grid fit the faster tempo.
- On these melodic renders no candidate fits the onset grid well (grid 0.04-0.28), so the onsets
  cannot decide.
- 8 are not a whole number of bars at the acid tempo (6.46, 5.85, 4.80 bars) or the truth is not
  a candidate. The audio in those renders is probably not at the project tempo (an unsynced sample
  inside the pattern) or has a tail. This is a reference limit, not an analyzer error.

**Change.** With weak onsets (`weakOnsets` = 1 - best grid fit / 0.5, clamped to 0..1), a
whole-bar candidate gets a further 0.4 x weakOnsets x the bar-count prior. A 3 / 6 / 12-bar reading
gets -0.4 x weakOnsets instead. Drum loops have a strong grid, so weakOnsets is 0 and they are
unaffected.

**Simulation.** Among the gating options (by high-band noise, by grid, either) and weights, this was
the best choice with no corpus worse.

**Result** (audio only, up to octave / exact):

| | before | after |
|---|---|---|
| maintainer's FL renders (118) | 79 % / 57 % | **86 % / 63 %** |
| tonal loops (13 packs) | 78 % / 56 % | **80 %** / 56 % |
| drum loops | 100 % / 96 % | 100 % / 96 % |
| classic breaks | 99 % / 96 % | 99 % / 96 % |

Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass.

**Key experiments after step 5** (no change made; both are negative results):
- Confidence: the margin of the best key over the second hardly predicts correctness (46-70 %
  exact in every margin bin), so the audio-only key errors are confident ones. Calibration alone
  cannot fix them.
- Learned rotation-invariant profiles (softmax over 24 keys on chroma, bass share and opening
  note), leave-one-pack-out: per-pack MIREX 0.64 vs 0.66 for the current rule.
- Key from transcribed notes (`SampleLineTranscriber` without key bias; the lab now prints the
  bass / melody pitch-class durations and the first / last bass note): leave-one-pack-out 0.65 vs
  0.66. In-sample exact rose 55 -> 59 %, which did not carry to held-out packs.
- Opening chord (tonic-triad match of the first 0.4 / 0.8 / 1.5 s, or the last 1 s):
  leave-one-pack-out 0.645 vs 0.661.
- NNLS chroma (Mauch & Dixon 2010: semitone spectrum explained by note templates with decaying
  harmonics, so the 3rd harmonic is not counted as the fifth; with log whitening): the current
  peak chroma won all 12 folds (best NNLS variant 0.638).
- Error structure (current rule): exact 55 %, relative 3 %, fifth 14 %, parallel 4 %, other 24 %.
  In the same or a near scale (exact, relative or fifth): 72 %. Fifth errors concentrate in Cobra
  (34 %) and Raw Hip-Hop melodic (30 %). Several are genuinely ambiguous: in
  rhh_melodic_loop_abe_90_Dm the bass sits on A for the whole loop.
- Conclusion: audio-only key on short loops sits around MIREX 0.65 with these features. With the
  file's own key label it is 95 %.

**Tempo experiment after step 8: loop repetition** (not adopted). Self-similarity of chroma and
log-band frames at the bar and 2-bar lags of each candidate gave renders 85.7 -> 86.6 % and tonal
82.2 -> 82.7 % up to octave in simulation. Too small for its cost (a lag scan per file).

**Whole songs (maintainer's question, 2026-10-09).** The plugin analyses at most the first 64 s of
a file (`SampleAnalyzer` kMaxAnalysisSeconds); the lab now reads the same 64 s.
- `user_tracks` (`docs/audit/reference/user_tracks.txt`): 105 of the maintainer's FL renders of 45 s
  or longer, with the acid tempo. Up to octave: 35 %.
- The corpus turned out to be mostly consolidated parts, not mixed songs:
  - 36 vocal "Insert 23" takes and acapellas, 8 % right: sibilants read as hats;
  - 35 parts without drums: 34 %;
  - 16 s excerpts of the same files are no better (8-12 %).
- A whole-song mode was tried and reverted: tempo on the busiest 16 s window, no length evidence,
  then a regression over the whole file. It gave no gain (35 -> 34 %) and left the loops identical.
- Only one real master exists in the maintainer's renders ("Потому-что тебя не люблю 79Am_Master":
  158 BPM, right up to octave).
- Next: a corpus of real mixed songs with known tempo / key is needed (the maintainer's finished
  beats or masters). Ideas then: choose the analysed 64 s inside the song (the middle, with drums)
  rather than the start; and a vocal detector, so a vocal-only file is not read as drums.

**Serato Sample comparison** (maintainer's question). Serato's detectors are trained on a very large
DJ library; its UI also offers x2 / ÷2 for the octave ambiguity, and short melodic loops without
drums are a known weak case there too. A fair comparison needs the same files: load 10-20 renders
from `docs/audit/reference/user_renders_120.txt` into Serato Sample and note its tempo / key.

## Step 9 — the note of a bass / 808 one-shot from its sound

**Cause.** The Sub808 lane took a sample's root only from its name (`rootPitchClassFromName`): no
note in the name meant C, and there was no fine tuning. In the 40 bundled samples:
- Techno TSB3 sounds C#2 but was played as C;
- DnB DSB2 / DSB10 are labelled E but sound F;
- Drill DRSB6 (no note in the name) sounds C# −41 cents but was treated as C;
- several 808s are 25-55 cents off their note (DRSB2 −47, BSB2 +24, DSB7 +55, DSB9 +23).

**Benchmark.** `tools/root_bench.py` + `docs/audit/reference/root_corpus.tsv`.
- 16 packs, 1026 one-shots with the note in the file name: 808s (Cymatics Savage, Bangin, Boomin
  and Diamonds; Ghosthack trap and 808 kicks; Southside; PeeJay; UTT2 multisamples) and basses
  (Raw Hip-Hop, Cymatics Lofi, Origin Dusty, Oliver, Ghosthack DnB, Cymatics synth bass).
- Ultimate Boom Bap Bass was dropped: its "A#_07_..." prefix is the kit's key, not the note.
- The bench scores the pitch class against the name. Octave consistency is reported with a
  constant offset, because packs number octaves differently (Kontakt / FL: C0 = our C1).

**Detector** (`SampleRootDetector`, new):
- works at ~8 kHz (box-filtered decimation; bass fundamentals stay under 500 Hz);
- YIN frame by frame (80 ms window, 10 ms hop, 25-500 Hz);
- a deeper dip at 2x / 4x the first dip's lag is taken, since a weak fundamental shows its octave
  first. 3x was tried and rejected because it turns errors into fifths;
- the settled pitch is the energy x periodicity weighted median of the largest group of frames
  within ±35 cents, after the attack peak. This skips the pitch glide an 808 starts with;
- a softer YIN threshold (0.35) is used when the strict one (0.15) finds nothing (synth basses).

**Result** (pitch class right):

| | old lab YIN (40-440 ms window) | new detector |
|---|---|---|
| all | 77 % (no answer for 15 % of files) | **89 %** |
| 808 packs | 25-86 % | **90-100 %** (PeeJay 25 -> 100 %) |
| Raw Hip-Hop / Origin / Lofi / Oliver basses | 90-100 % | 92-100 % |
| Ghosthack DnB basses | 83 % | 87 % |
| Cymatics synth bass one-shots | (6 answers) | 48 % |

UTT2's lowest multisamples ("C0" = 16 Hz) are below the detector's 25 Hz floor, so it reports
their 2nd or higher harmonic. The pitch class is still right.

**Plugin.**
- `LaneSampleBank::prepareLibrary` detects the root of every Sub808 sample when the bank loads (off
  the audio / project lock).
- `resolveRoot` joins the name and the sound:
  - the name's pitch class is kept when the sound is within 75 cents of it, with the sound's cents;
  - a clear sound (confidence ≥ 0.5) that says otherwise wins (mislabelled samples);
  - without a note in the name, the sound decides.
- `playbackRateForTrackPitch` applies the cents, so the sample sits exactly on the written note.
- The register is unchanged on purpose: the sample's octave is still taken nearest the lane's
  default note. The detected octave is kept (`getSelectedRootMidi`) for a later decision with the
  maintainer: "sound as written" would raise the bundled trap 808s (C1) by an octave.

Tests: CoreTests 77/77, SampleTrimTests and LaneBoundaryTests pass, including the new "Sample root
from name and sound" (rule cases, plus a C#2 sine named C read as C#2 by the bank).

## Step 10 — Copy Break started a bar late; tempo range like FL Studio

**Bug (maintainer, 2026-10-09).** "Pattern 2_536.wav" (90 BPM, 4 bars, a loose swung groove, a hit
at 0.006 s) was copied with an empty first bar.
- The grid phase came out 35-45 ms off every hit, and the fit read the offset as 61 % swing.
- So the "a trimmed loop starts on its first hit" bonus (35 ms window) never applied, and beat 1
  went 2.5 s before the file.

**Change.** In the automatic path, a trimmed loop also tries a grid anchored at the first hit
(±6 ms, its own best swing). It is used for beat 1 and swing when its fit is within 0.08 of the
free fit. The tempo score keeps the free fit, so tempo choices barely move. The typed-tempo path is
unchanged: anchoring there made tonal loops a quarter off more often (Cobra 24 -> 34 %).
- On the bug file: origin −2533 -> 4 ms, swing 61 -> 51 %, backbeat 0.23 -> 0.60, exact 4-bar loop.

**Result** (auto origin within 20 ms of beat 1, trimmed corpora):

| corpus | before | after |
|---|---|---|
| Trap Urban / Hybrid / UTT2 | 93 / 80 / 85 % | **100 / 100 / 100 %** |
| Boom Bap | 89 % | 91 % |
| Techno | 80 % | 82 % |
| Lofi / Raw Hip-Hop melodic | 44 / 45 % | **59 / 64 %** |
| other tonal packs | 32-52 % | 41-56 % |

- Typed-tempo origin is unchanged everywhere.
- Tempo up to octave is unchanged: drums 100 %, tonal 80 %, renders 86 %.

**Tempo range (maintainer's request: like FL Studio's "Detect tempo").** The Sample BPM dialog has
a range list: Auto, 50-100, 75-150, 100-200, 150-300.
- It is stored with the sample (`tempo_range_min` / `tempo_range_max`), and the analyzer only
  considers tempos inside it.
- A stated tempo (name / acid) outside the range is folded into it by octaves.
- The lab takes `--range <min> <max>`.
- On the 46 renders the automatic reading missed (exact), the matching range fixes 25: renders exact
  ~61 % -> ~82 % when the user picks a range. These are mostly octave readings; x3/2 readings are
  not always fixed.

**Still open from the same file: a bass note read as a kick.** The maintainer: there is no kick at
the start, only bass and a snare.
- The first hit has 5 % of its energy below 100 Hz and 92 % at 100-250 Hz; this loop's kicks have
  65 % below 100 Hz.
- The kick template "explained" the bass note, and the snare under it was suppressed by the
  strong-kick rule.
- Next: compare doubtful hits with this loop's own kick and snare spectra (kits without sub must not
  suffer), or harmonic / percussive separation so sustained bass notes do not reach the drum
  templates.

## Open (next steps)

1. **Tempo of tonal loops: loop tails (NEXT).** After step 7: 78 % up to octave on tonal packs, 79 % on
   the maintainer's renders. Loops with a reverb tail (Freaky DnB music) get false whole-bar
   readings: measure the length without the tail. Stems of a full track (user_song_stems, 46 %) are
   the hardest case.
   Earlier note: **Tempo of tonal loops from the audio.** Audio only: 48 % on 13 tonal packs, 42 % on the
   maintainer's FL renders (x2 and x3/2 errors, see step 6). Before step 6: across the 9 tonal packs
   the tempo was right in only 49 % of files
   (Freaky DnB bass 30 %, Cymatics 42-48 %). The analyzer tracks the onsets of notes, not a beat.
   The plugin only trusts these at confidence ≥ 0.8 (81 % right there). Look at this together with
   the key work.
2. **16th phase.** On some Techno loops the grid is a 16th off (GUT 01 / 04 / 22).
3. **Key confidence and the remaining key errors.**
   - Confidence ≥ 0.6 is right 59 % of the time and < 0.4 is right 42 %; it should follow the
     margin over the second key, as tempo does (step 3).
   - Major loops: 9 / 28 right. Needs more major material (soul samples).
   - Fifth errors: 10-30 % per pack. Freaky DnB bass (reese, root only): 47 %.
4. **Root of the bass / 808 sample from its sound** — done in step 9 (register decision open). Today the root comes only from the file name
   (`LaneSampleBank::rootPitchClassFromName`): no note in the name means C, and the octave is
   ignored. A YIN f0 check of the 40 bundled Sub808 samples (2026-10-08) found:
   - mixed octaves: Boom Bap 2 × C1 (33 Hz) and 3 × C2 (65 Hz), Techno mixed too, so the same bass
     line sounds an octave apart depending on the sample;
   - one real key error: Techno TSB3 `techno-bass_120bpm` sounds C#2 but is read as C, a semitone
     off the key;
   - wrong labels: DnB DSB2 / DSB9 are named "E" but sound F1; DSB5 / DSB7 are named "F" but are
     near F#1;
   - detune: Drill DRSB2 is 47 cents flat of C1.

   Plan:
   - detect pitch class, octave and cents when a sample is loaded, keeping the name as a hint
     (when the two disagree and the detector is confident, trust the sound);
   - play each written note at its true pitch, with the detune corrected;
   - keep the bass register the same whatever octave the sample was recorded in.

   Measure: share of the bundled samples and the maintainer's packs that sound off the written note,
   before and after.
5. **Maintainer's own material** (in the bench since step 6).
   - `E:/FL/.../Audio/Rendered`: 1680 of 3134 renders carry FL's project tempo in the WAV `acid`
     chunk. That is a tempo reference from real beats; the bench needs a bpm-from-chunk mode and a
     length cap for loops.
   - "HamloProd sample pack 2": about 17 loops with tempo and key in free-form names (`Piano 85Dm`,
     `Guitar Gsharp 82`) for the key benchmark.
6. Later: swing, K/S/H transcription accuracy, Copy Break, confidence-aware Guide generation.

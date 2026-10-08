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

## Open (next steps)

1. **Tonal false positives.** 68 % of tonal loops get drum-loop confidence ≥ 0.5.
2. **16th phase.** On some Techno loops the grid is a 16th off (GUT 01 / 04 / 22).
3. **Key of the loop: benchmark.** About 75 tonal loops carry their key in the file name
   (`GUT_Music_Loop_13_123_BPM_Am`). Measure how often `SampleHarmonyAnalyzer` gets the root and the
   mode right. A bass "in key" of a wrong key is still wrong, so this comes before item 4.
4. **Root of the bass / 808 sample from its sound.** Today the root comes only from the file name
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
5. Later: swing, K/S/H transcription accuracy, Copy Break, confidence-aware Guide generation.

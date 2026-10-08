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

## Open (next steps)

1. **Honest confidence** (§27). 334 of 347 drum loops still report confidence ≥ 0.8. The whole-bar
   loop branch raises the confidence to 0.45 + 0.5 × grid whatever the octave alternative scores.
   The margin over the octave partner should lower it.
2. **Tonal false positives.** 68 % of tonal loops get drum-loop confidence ≥ 0.5.
3. **16th phase.** On some Techno loops the grid is a 16th off (GUT 01 / 04 / 22).
4. Later: swing, K/S/H transcription accuracy, Copy Break, harmony / bass (808 tone),
   confidence-aware Guide generation.

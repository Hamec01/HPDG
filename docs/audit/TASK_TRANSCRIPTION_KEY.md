# Task — key and bass from note transcription (MuScriptor as an offline reference)

Stage: Sample Analysis (stage 6), next after step 19. Type: **measurement first, no plugin change
until the decision gate** (RULES 2, 39, 49). Report to the maintainer in Russian, with numbers.

## Why

The key is the weakest part of the sample analysis (step 5 / 16, 377 tonal loops of
`tempo_corpus.tsv`): **MIREX 0.65, exact 56 %**, fifth errors 10-30 % per pack, majors 9 / 28,
Freaky DnB bass 0.47. Bass lines are already good (step 18: pitch class 0.94, 0.5 s windows 0.95).
Step 16 found that "agreement with the transcribed notes" predicts a right key better than the
current confidence (AUC 0.69 vs 0.57), but it was not tried as a key estimate.

MuScriptor (Kyutai / Mirelo, 2026) transcribes audio to multi-instrument MIDI. The question is:
**would knowing the notes give a better key than the chroma, and by how much?**

## Constraints (read before starting)

- **Licence: weights CC BY-NC 4.0, code MIT.** Lab use only. Not in the plugin, not in the
  installer, not a dependency of the build or of any test that CI / `HPDG_CoreTests` runs.
  The plugin stays self-contained (RULE 35 in spirit: no mandatory model).
- No `Source/` changes in phases 0-3. `git diff Source Tests` stays empty until phase 4.
- Third-party audio and MIDI made from it stay out of git: caches in
  `C:/Users/ham/Documents/bench/muscriptor/`, only scripts and aggregated numbers in the repo.
- The model download is untrusted data: its own folder, a venv outside the repo, run Python with `-I`.
- Do not build HPDG targets while the maintainer runs `build_vst.bat`; copy `HPDG_BreakLab.exe`
  for long runs (`--breaklab <copy>`).
- Same files, same truth, same scoring as the existing key bench (RULE 4): the file set and
  `key_from_name` come from `tools/tempo_bench.py`, the score from `key-report`.

## Phase 0 — setup and cost (this PC: RTX 3060 12 GB)

1. Venv in `C:/Users/ham/Documents/bench/muscriptor/venv`, install the MuScriptor CLI; record the
   package version, model checkpoint hashes and torch / CUDA versions in the report.
2. Cost on 30 tonal loops (10 per pack, fixed list in the report), for small / medium / large,
   GPU and CPU:
   - model load time;
   - wall time per 10 s of audio, p50 / p95 / worst;
   - peak RAM and VRAM.
3. Determinism: transcribe the same 20 files twice. MIDI must match note for note; if the CLI
   samples, find the greedy / fixed-seed setting. Non-deterministic output → stop and report.

## Phase 1 — key from MuScriptor notes (arm M)

New `tools/transcription_key.py` (style of `tempo_bench.py` / `root_bench.py`: docstring usage,
`run` / `report`, cache per corpus):

- `run <corpus.tsv> <out> --model medium|large` → `<out>/midi/<id>/<file>.mid` (existing kept).
- Key from notes: pitch-class histogram weighted by duration × velocity, drum channel / drum
  programs excluded, then the **same** Albrecht-Shanahan profiles and minor prior 0.1 as
  `SampleHarmonyAnalyzer` (no new profiles in this phase). Variants, each reported separately:
  - M0 all notes;
  - M1 + bass weight (lowest sounding note per onset, weight 0.2 as in the plugin);
  - M2 + opening-note weight 0.5 (first bass note, as in the plugin).
- Confidence: correlation margin best vs second key (RULE 26).

## Phase 2 — the same idea with HPDG's own transcriber (arm H)

`HPDG_BreakLab analyze --harmony` already prints `SampleLineTranscriber` bass / melody lines.
Parse them (add a `--notes` dump to the lab only if the printed lines are not enough — lab code,
not plugin code) and run the same key-from-notes as arm M. This tells whether a gain comes from
"notes instead of chroma" (reachable in the plugin) or only from MuScriptor's quality.

Also: arm C (current plugin key from the existing `harmony/` reports) and fusion C+H, C+M
(chroma correlation + note correlation, one weight chosen leave-one-pack-out).

## Phase 3 — bass check

On the 20 stem sets of step 18 (`D:/Downloads/mp3`, first 40 s, mix without vocals vs Bass stem):
MuScriptor bass notes (bass programs, else lowest voice) vs `HPDG_BreakLab lines`, same metrics:
frame recall / precision, exact note, pitch class, 0.5 s windows. Project stems
(Hamilio160, i see dead people, Замарай мои следы) reported separately, as in step 18.

## Report table (the deliverable of phases 0-3)

| arm | MIREX | exact | fifth | relative | parallel | other | majors | conf ≥ 0.6 exact | AUC | time / 10 s |
|---|---|---|---|---|---|---|---|---|---|---|
| C current | 0.65 | 56 % | | | | | 9 / 28 | 69 % (203) | | |
| H HPDG notes | | | | | | | | | | |
| M0 / M1 / M2 medium | | | | | | | | | | |
| M large (best variant) | | | | | | | | | | |
| C+H, C+M fusion (LOPO) | | | | | | | | | | |

Plus per pack MIREX for every arm (9 packs), and the bass table of phase 3.

## Tests and checks

Python (`tools/test_transcription_key.py`, `python -I -m unittest`):
- C major scale → C major; A natural minor melody → A minor;
- Am-F-C-G with loud bass roots → A minor with M2 (the case that broke the lane test in step 5);
- transposing any test MIDI by 0-11 semitones moves the key by the same amount (12 / 12);
- drum-only MIDI and empty MIDI → no key, confidence 0;
- the parser of the lab's printed lines reads a saved sample report.

Bench sanity, before any comparison:
- arm C recomputed from the cached `harmony/` reports reproduces MIREX 0.65 / exact 56 % on the
  same 377 files (the 3 Cyrillic-"С" files skipped as before). Mismatch → fix the bench first;
- the file list of every arm is identical (count printed in the report);
- any chosen weight is picked leave-one-pack-out over the 9 packs and the held-out score is the
  one reported (RULE 19); never tune on all 377.

Repository:
- `git status` shows no audio / MIDI / model files; `git diff Source Tests` empty (phases 0-3);
- if lab code changes (`--notes`), `HPDG_CoreTests`, `HPDG_LaneBoundaryTests`,
  `HPDG_SampleTrimTests` all pass and `analyze --harmony` output for 10 files is unchanged
  except the new lines.

## Decision gate (after phase 3, the maintainer decides)

- **H or C+H beats C by ≥ 0.05 MIREX held-out, in ≥ 6 / 9 packs, with no pack worse by > 0.03,
  and confidence ≥ 0.6 no less exact** → phase 4: bring the note-based key into
  `SampleHarmonyAnalyzer` (additive: a second correlation fused with the chroma one).
- **Only M wins** (H does not) → MuScriptor stays a lab tool: an upper bound and a second opinion
  for labelling loops without a key in the name (never ground truth by itself). Next work is
  `SampleLineTranscriber` quality, measured against M.
- **Gain < 0.03** → write it up as a measured negative result and close.
- Bass: change nothing unless an arm beats step 18 on the project stems (precision 0.91-0.94).

## Phase 4 (only after the gate) — plugin change

- One change: fuse the note-based key correlation into `SampleHarmonyAnalyzer`, weight from the
  LOPO choice; confidence from the fused margin.
- Before / after on the same 377 files (`run-key` / `key-report`), plus the synthetic harmony test
  (36 / 40 now, must not drop) and the root bench (`root_bench.py`) unchanged.
- Keep `SampleBassFollower::kMinKeyConfidence` (0.4) behaviour: report how many loops cross 0.4 in
  either direction (step 16 showed this breaks tests and drops the key for many loops).
- New CoreTests: the fusion keeps Am-F-C-G as A minor; a sample with no detected notes gives the
  same key and confidence as today (chroma only); determinism (same file twice → same key).
- All existing tests pass; generation of all genres unchanged for a sample-free run (1000 seeds
  per genre via the quality lab, identical output), because the key only enters through sample
  analysis.
- Analysis time per sample: p50 / p95 before and after; no work in the audio callback (RULE 37).
- Commit only after the maintainer listens to the guide 808 on 10 random tonal loops (RULE 42).

## Deliverables

- `tools/transcription_key.py`, `tools/test_transcription_key.py`;
- step 20 in `docs/audit/SAMPLE_ANALYSIS_STAGE.md` (setup, cost, tables, decision), one line in
  the progress log of `docs/ROADMAP_UPDATE.md`;
- report in Russian to the maintainer: cost, key table, bass table, recommendation per the gate.

Sources:
- MuScriptor model cards (small / medium / large), Hugging Face `MuScriptor/*`, licence CC BY-NC 4.0;
- Albrecht & Shanahan (2013), Music Perception 31(1);
- Faraldo et al. (2016), "Key Estimation in Electronic Dance Music", ECIR;
- earlier steps: `SAMPLE_ANALYSIS_STAGE.md` steps 5, 16, 18.

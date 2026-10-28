# ATL passport — what makes an "Atlanta / Young Thug type beat" (2026-10-28)

Maintainer's request: analyse the Atlanta / Young Thug type beats in `D:/Downloads/mp3`, build what
makes them "ATL" into Trap ATLClassic, and add an ATL 808 voice. Audio stays out of git; this file
keeps numbers only.

## Corpus and method

- 12 tracks whose title names Atlanta or Young Thug (8 "Atlanta Type Beat", 3 "Young Thug Type Beat",
  RAIKAHO "By Atlanta"). FRIENDLY THUG 52 is not Young Thug and was left out.
- Stems: Demucs htdemucs (drums / bass / other), on the GPU (PyTorch cu128 venv).
- Drums + bass: `tools/atmos_passport.py run` (HPDG_BreakLab on two 40 s windows per track), then
  `tools/atl_passport.py`: every hit placed on Trap's half-time bar (~150 BPM, snare on beat 3) by its
  time. Tracks whose tempo reading does not put the snare on beat 3 in >= 50 % of the bars are left
  out (3 of 12: Prime Era, SAVIOR, SPINZ); 9 tracks, 401 trap bars.
- Hats: `tools/atl_hat_onsets.py` — onsets of the drum stem above 7 kHz (BreakLab's hats sit on the
  16th grid of the tempo it reads, 75 BPM, so 1/32 rolls and triplets cannot show there).
- 808 sound: `tools/atl_808_reference.py` on the bass stems (1468 notes, 12 tracks). The envelope is
  a 40 ms RMS: 5 ms frames ripple +-3 dB with a 45 Hz wave and split one note into many.

## Findings (references vs HPDG ATLClassic before, 150 BPM, 1000 seeds)

| | ATL references | ATLClassic before | ATLClassic after |
|---|---|---|---|
| kicks / bar | 0.24-1.19 (stem; a lower bound — kicks under the 808 are masked) | 2.47 | 2.01 |
| 808 notes / bar | 1.5-4.9 (median 3.6) | 2.28 | 2.57 |
| 808 notes per kick | > 1 in every track | 0.92 | 1.28 |
| 808 length, beats | 1.0-2.8 (median 1.45) | 1.68 | 1.46 |
| 808 moves: repeat / octave / > octave / 4th-5th | 0.45 / 0.01 / 0.04 / 0.10 (means) | 0.46 / 0.10 / 0.03 / 0.16 | 0.52 / 0.02 / 0.03 / 0.19 |
| 808 register | median F1-F#1 (45 Hz) | G#1 | G1 |
| snare on trap beat 3 | 0.74 a bar (+ 0.15 on 16th 3) | 1.00 | 1.00 |
| hats / bar | 4.9-9.1 | 8.2 | 6.7 |
| hat gaps 1/8 / 1/16 | 0.59-0.95 / 0.00-0.19 (median 0.75 / 0.03) | 0.39 / 0.36 | 0.57 / 0.13 |
| hat rolls / 4 bars | 0 in 11 tracks, 1.2 (all 1/16 triplets) in one | 0.14 | 0.12 |

So an ATL type beat is: half-time snare on 3, a sparse kick with the 808 carrying the low end (more
808 notes than kicks, the 808 repeating its root, no octave jumps), and straight 8th hats with few
16ths and rare, triplet rolls.

ATL 808 sound (median of track medians, at the note's own pitch): fundamental 45 Hz, 2nd harmonic
-20 dB, 3rd -15 dB (asymmetric saturation), -12 dB after 0.9 s, then rings on.

## Changes (ATLClassic only; DarkTrap / LuxuryTrap measured identical, 1000 seeds)

- `TrapAlgebraEngine::generateBarAdditions`: one articulation kick at most, at 0.45 of its chance;
  up to three 808 answers between kicks (+0.25 chance); the off-8th 16th hats at 0.25 of their chance.
- `weightsForSubstyle(ATLClassic)`: rollRate 0.25 -> 0.18, tripletBias 0.15 -> 0.45.
- `assignTrap808Line`: the in-bar octave move becomes a repeat; a repeat of the degree answers a
  fifth instead of an octave.
- ATL 808 voices (`tools/tune_synth_808.py`, `tools/make_synth_sub.py`), measured at F1: ATL 1 (median)
  h2 -20.5 h3 -15.1 t-12 0.93 s; ATL 2 (gritty, SPINZ) -21.4 / -12.8 / 0.65 s (target h3 -9); ATL 3
  (long, ATL JACOBS) -19.0 / -10.2 / 1.71 s (target h3 -18: the model cannot hold a long note that
  clean). They sort after the other synthesized subs (saved sample indices unchanged); ATLClassic
  starts on ATL 1 (maintainer 2026-10-28), every other style keeps Sub Clean.
- Test: `testTrapAtlClassicFollowsAtlPassport` (fails on the old engine on all three checks).

## Open

- Listening by the maintainer (RULES: commit after listening).
- Kicks: the stem count is a lower bound; 2.01 a bar may still be dense — decide by ear.
- The 3 tracks with an unreliable tempo reading (121-130 BPM) were left out of the drum numbers.

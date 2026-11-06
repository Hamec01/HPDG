# LuxuryTrap reconstruction — 2026-11-06

Historical first-pass report. The owner rejected the subsequent listening result.
The current timing correction and its limits are documented in
[the rhythm review](LUXURY_TRAP_RHYTHM_REVIEW_2026_11_06.md); the old matrix and listening
claims below must not be presented as validation of the current generator.

## Evidence and measurement contract

- 16 supplied drum/bass pairs; two aligned 24 s windows at 30% and 60% of each stem (64 individual stem windows).
- 69 supplied one-shots: 16 bass, 15 closed hats, 10 clap/rim, 13 snare, 6 kick, 4 open hat, 5 percussion.
- inventory.json stores original paths, byte counts and SHA256. Decoded cuts/raw engine reports are in build/luxury-reference.
- HPDG_BreakLab supplies tempo/hit/pitch hypotheses. Independent 6 kHz spectral-flux detections use thresholds 0.06/0.10/0.16, 2.90 ms hop, minimum 11.61 ms between peaks.
- HF transients are **hat candidates**, including snare/open-hat leakage; these are not annotated ground truth or a recovered MIDI score.
- Half/double tempo chosen nearest 145 BPM is a display hypothesis, not a tempo label. Inconsistent window tempi and confidence <0.5 are flagged below.
- GODS COUNTRY and GODS COUNTRY-1 have identical duration and 0.997 waveform correlation in the first sampled drum window. Do not count them as independent evidence in pooled calibration.
- Bass pitch transcription merges same-pitch attacks: ELEGANT includes a 19.3 s segment (about 54 beats at 167), so its note count/duration cannot calibrate articulation.
- This is descriptive analysis of the supplied corpus, not a held-out genre-recognition experiment. No universal genre accuracy or identical-sound claim is made.

## Per-reference observations

Numbers are per-window estimates. Rolls mean >=3 consecutive fast gaps (at least four detected hits).

| Reference | BPM hypotheses | HF candidates/bar (threshold .10) | Fast runs/4 bars | Transcribed bass notes/bar | Flags |
|---|---|---|---|---|---|
| 10_[FREE] FUTURE TYPE BEAT - ELEGANT _ FREESTYLE BEAT _ TRAP TYPE BEAT _ FREE TRAP INSTRUMENTAL 2025 | 166.98 / 166.96 | 11.44 / 12.10 | 0.24 / 0.24 | 0.12 / 0.24 | bass attack merging |
| 11_TRAP TYPE BEAT - SNAKE _ FREESTYLE BEAT _ HARD TYPE BEAT _ FREE RAP INSTRUMENTAL 2026 _ FREE BEAT | 152.00 / 152.00 | 12.11 / 7.83 | 0.00 / 0.26 | 2.24 / 0.99 |  |
| 12_[FREE] Lil Baby x Gunna Type Beat - Soho _ Rap Trap Instrumental 2026 | 131.00 / 196.42 | 13.36 / 6.11 | 0.61 / 0.20 | 1.91 / 1.17 | tempo/section disagreement, low tempo confidence |
| 13_[FREE] Playboi Carti Type Beat - HBA | 137.00 / 137.00 | 20.00 / 17.37 | 0.29 / 0.29 | 1.53 / 1.02 |  |
| 14_[FREE] Playboi Carti Type Beat - yves | 139.00 / 139.00 | 18.71 / 19.86 | 4.60 / 3.74 | 1.37 / 1.65 |  |
| 15_[FREE] Playboi Carti Type Beat - deadeye | 135.00 / 135.00 | 17.33 / 12.81 | 0.00 / 0.30 | 1.70 / 1.63 |  |
| 16_[FREE] Playboi Carti Type Beat - GANGSTER | 142.00 / 142.00 | 43.10 / 40.63 | 1.41 / 0.56 | 3.80 / 3.38 |  |
| 1_KANYE WEST TYPE BEAT - ASCENSION | 192.00 / 192.00 | 10.78 / 13.33 | 1.88 / 2.71 | 0.99 / 0.78 |  |
| 2_[BEAT SWITCH] TRAVIS SCOTT x DRAKE TYPE BEAT - GODS COUNTRY-1 | 138.00 / 106.41 | 13.77 / 9.40 | 4.93 / 0.75 | 2.17 / 1.03 | tempo/section disagreement, low tempo confidence |
| 3_[BEAT SWITCH] TRAVIS SCOTT x THE WEEKND TYPE BEAT - THE FALL (INTRO) | 186.00 / 154.98 | 1.08 / 12.07 | 0.00 / 3.10 | 0.75 / 1.23 | tempo/section disagreement |
| 4_[BEAT SWITCH] TRAVIS SCOTT x UTOPIA TYPE BEAT - INFERNO | 150.00 / 150.00 | 2.60 / 14.13 | 0.53 / 2.93 | 2.20 / 1.73 |  |
| 5_TRAVIS SCOTT x DRAKE TYPE BEAT - KINGS | 130.00 / 130.00 | 12.62 / 11.62 | 0.31 / 0.00 | 1.62 / 1.46 |  |
| 6_[BEAT SWITCH] TRAVIS SCOTT x DRAKE TYPE BEAT - GODS COUNTRY | 138.00 / 175.06 | 13.77 / 4.63 | 4.93 / 0.69 | 2.17 / 0.63 | tempo/section disagreement, low tempo confidence, near-duplicate rendition |
| 7_Future Type Beat - Trap God _ Type Beat _ Rap_Trap Instrumental 2026 | 141.00 / 141.00 | 12.41 / 14.82 | 0.00 / 0.00 | 2.48 / 2.62 |  |
| 8_[FREE] FUTURE TYPE BEAT - LUXURY _ FREESTYLE BEAT _ TRAP TYPE BEAT _ FREE TRAP INSTRUMENTAL 2025 | 134.00 / 134.00 | 6.94 / 5.00 | 2.09 / 1.49 | 0.97 / 0.67 |  |
| 9_[FREE] Lil Baby x Gunna Type Beat - LUXURY TIMEPIECE _ Rap Trap Instrumental 2026 | 131.00 / 131.00 | 12.21 / 11.76 | 0.92 / 1.22 | 1.60 / 1.60 |  |

## Implemented musical grammar

- Dedicated Luxury generation replaces the previous shared kick/bass/hat additions, retaining the Trap backbeat and legal kick positions.
- Four seed-stable phrase families: pedal, melodic, rattling, driving. Candidate scoring cannot exchange one family for another. Families and exact probabilities are engineering choices informed by the heterogeneous references, not fitted ground-truth frequencies.
- A/B/A/B′ kick motifs; sparse reinforcement instead of one kick per bass attack. Bass anchors plus independent responses/pickups; final gates are monophonic and ring across the backbeat.
- 8th carrier with softer offbeats; denser families add 16ths. Phrase-local rolls use 32nds/64ths and triplets, minimum four notes, rising/falling/wave/strong-weak contours. Rolls replace their slice of the carrier, with clearance around triplet endpoints.
- Rolls Off really disables rolls. Fills forces phrase-ending ornaments. Half-tempo host mapping uses the existing resolved clock exactly once.
- Half of seeds add a controlled 0/-2/-5/0 semitone roll contour. This is a musical design choice; separated HF material does not establish the source hat pitches.
- Generic repair can no longer layer a closed-hat accent onto an existing Luxury roll. Fine note gates preserve 1/64 articulation instead of turning it into overlapping 16th notes.
- Sample-guided generation retains Luxury closed hats/HatFX and bass onsets. SampleBassFollower changes bass pitches using analysis; styleWindow808Pitch preserves the low register. Exact Copy remains a literal-copy mode, outside this guarantee.
- 63 deduplicated drum lane assets imported from the supplied kit (13 hats also exposed in HatFX), plus 16 C-tuned 808s in the Sub808 sample bank. SHA256 provenance is in kit_manifest.json. Append-only aliases preserve drum indices; saved 808 choices resolve by name when the synth tail moves. Other substyles retain their existing formulas.
- Existing HiHat/HatFX/OpenHat/Perc lanes cover these roles; no new lane/UI or serialization type was necessary.

## Bass tuning and synthesis

The neutral sampler trigger MIDI 60 (the owner’s FL C5 naming) is independent of the waveform’s physical fundamental. Physical C1 = MIDI 24 = 32.7032 Hz; C2 = MIDI 36 = 65.4064 Hz. A synth follows physical MIDI pitch; it must not globally transpose by the DAW label difference.

16 new copies are tuned to the nearest C octave, preserving the original character and originals. Rate tuning changes duration as in a sampler. MP3 stems are never used as factory one-shots.

| Source | Original measured Hz | Copy physical root | Verified cents |
|---|---:|---|---:|
| 808 (southside).wav | 32.929 | MIDI 24 / 32.7032 Hz | -0.81 |
| 808 [ Spinz ].wav | 32.929 | MIDI 24 / 32.7032 Hz | +0.17 |
| 808 [CANT STOP].wav | 32.586 | MIDI 24 / 32.7032 Hz | -2.12 |
| 808 luger.wav | 32.976 | MIDI 24 / 32.7032 Hz | +0.32 |
| 808 PAC (turn loop points on).wav | 32.710 | MIDI 24 / 32.7032 Hz | -0.09 |
| 808 Spinz.wav | 32.929 | MIDI 24 / 32.7032 Hz | -0.81 |
| 808Zay Spinz.wav | 32.944 | MIDI 24 / 32.7032 Hz | +0.10 |
| [ACD] - Lex 808 (2).wav | 32.730 | MIDI 24 / 32.7032 Hz | +0.11 |
| bass - seam.wav | 32.493 | MIDI 24 / 32.7032 Hz | -2.30 |
| BWB WAVE 7 808 SHORT (24).wav | 30.672 | MIDI 24 / 32.7032 Hz | -4.47 |
| BWB WAVE 7 BASS (2).wav | 41.187 | MIDI 24 / 32.7032 Hz | -0.47 |
| Lex 808.wav | 52.340 | MIDI 36 / 65.4064 Hz | -0.99 |
| Pony 808.wav | 32.944 | MIDI 24 / 32.7032 Hz | +0.10 |
| REX 808 1.wav | 32.585 | MIDI 24 / 32.7032 Hz | +1.74 |
| Trvpyyy 808 3.wav | 33.039 | MIDI 24 / 32.7032 Hz | -0.85 |
| WUNNA 808.wav | 34.282 | MIDI 24 / 32.7032 Hz | +0.61 |

Pure synthesis fits use the production BassEngine (not a separate Python sound model). A static harmonic fit seeds nonlinear least-squares refinement of drive, bias, blend, key-tracked tone cutoff, hold and decay. Amplitude is compared after equal-level alignment; no phase/waveform identity is claimed. The attack punch (1.5 st, 12 ms) is a conservative design prior, not a measured recovery of the original transient.

| Fit | Harmonics h2–h8 RMSE dB | h2–h6 clipped at −45 dB RMSE | Envelope RMSE dB | Outcome |
|---|---:|---:|---:|---|
| Luxury Spinz | 2.22 | 0.94 | 0.30 | default pure synth |
| Luxury Silk | 6.15 | 5.79 | 2.13 | rejected: insufficient match |
| Luxury Lex | 6.19 | 3.46 | 0.64 | alternative pure synth |

Luxury Spinz/Luxury Lex are appended to the embedded HPDG 808 factory presets. Selecting LuxuryTrap initializes Luxury Spinz; unlocked full/bass generation cycles the two synths and sixteen Luxury bank 808s. Lock preserves the sound. No source sample is required by the synth patches. The Lex fit is approximate; the Silk fit was not shipped as a factory preset.

Audibility correction: the original Snare 10 and Kick 05 were AIFF containers named .wav. Drum imports now decode to real stereo 48 kHz PCM24 WAV, with gain reduction only for peaks over 0.98. Original files remain unchanged; kit_manifest.json records separate source/output hashes.

## Production generation verification

Before: 40 seeds. After: 40 seeds. Default 4-bar settings; defaults changed deliberately.

| Metric | Before | After |
|---|---:|---:|
| hatsPerBar | 7.556 | 12.537 |
| trueRollNotesPerBar | 0.388 | 2.244 |
| patternsWithRoll | 0.325 | 0.950 |
| bassNotesPerBar | 2.019 | 2.275 |
| bassGapFill | 0.927 | 0.898 |
| kicksPerBar | 2.438 | 2.156 |
| backbeatCoverage | 1.000 | 1.000 |

After hard failure rate: 0.0; determinism failures: 0. Additional density/bar/tempo and sample-harmony regression results are recorded in the handoff and validation.json.

## Remaining perceptual validation

Generated WAVs and fitted reference/synth comparisons are supplied for audition. Automated spectral, rhythmic and finite-output checks do not establish that a person hears an identical source patch or that every generated beat is stylistically convincing. No live FL Studio audition was performed by the agent.

# Atmospheric engine — stage log

Spec: `ATMOS_ENGINE_SPEC.md`. Numbers: `ATMOS_PASSPORT.md` / `izzamuzzic_families.txt`.

## 2026-10-14 — steps 1-5 (first version): skeleton, Pulse / Two-Step / Broken drums, bass

What exists:
- `GenreType::Atmospheric` appended after Techno (genre choice index 6, parameter
  `atmospheric_substyle`, serialized as `performance_base_atmospheric_substyle`). Substyles Pulse,
  Two-Step, Broken (`Source/Engine/Atmospheric/AtmosphericStyleProfile.cpp`, every number from the
  passport).
- `AtmosphericEngine` (generate / RG / mutate, same dispatch as Techno): 48 candidates from the
  substyle grammar (`AtmosphericGrammar.cpp`), trait-fit scorer + hard gates, near-best softmax pool.
  - Pulse: kick bar types four / half / half -> four (never off the beat), clap 2 / 4 by sections
    (all / second half / alternate blocks / last bar only), pickups on 15 (more at block ends), flams on
    4 / 12, off-beat 8th hats by sections (+ tails or full 8ths), open hats none / all off-beats / only 15.
  - Two-Step: kick motif 1 + 11 (+ one extra), snare 5 + 13 every bar + motif pickups, hat motif on the
    8ths from the passport profile; per-bar kick variants, fills at block ends.
  - Broken: a motif bar (2-3 kicks, 2 / 4 anchor, 1-2 extra claps, hats from the profile) re-composed
    bar by bar: part of the motif kept, the rest re-placed from the passport profile; guards: 2 / 4
    anchor in every bar, kick on a clap at most once per bar, motif share >= 0.20, a kick at the top.
- Bass (`AtmosphericBass.cpp`): written against the written kicks; root plan of 4 bars per substyle,
  onsets by the passport's kick relation (on / after / before / away), pitches by its interval mix,
  lengths around its median; 32 candidates scored on the passport traits; gates: register, no overlap,
  not more than half the notes on a kick. Glides off. Key from the project (Guide mode sets it from the
  sample as for the other engines).
- Kit: `Samples/Atmospheric/{Kick, Snare, HiHat, OpenHat, Sub808}` (maintainer's FAKE - TOOLBOX pack,
  chosen by measurement: 14 kicks, 8 claps + 7 snares, 13 hats, 7 open hats / rides, 11 basses with a
  stable pitch — the pack's "Drop" subs and Reese loops sweep in pitch and were left out). A lane
  without its own sounds borrows Techno (claps from its ClapGhost), then DnB, then Boom Bap.

Measurements (`HPDG_CoreTests` calibration, 200 seeds, 4 bars, selected patterns; passport in brackets):

| | kicks / bar | snares / bar | hats / bar | kick off-beat | snare off 2 / 4 | kick bar = previous |
|---|---|---|---|---|---|---|
| Pulse | 2.96 (2.6) | 1.11 (1.1) | 3.69 (3.6) | 0.00 (0.00) | 0.39 (0.50) | 0.59 (0.54) |
| Two-Step | 2.15 (1.8) | 2.30 (2.1) | 3.51 (3.5) | 0.51 (0.54) | **0.12 (0.35)** | 0.51 (0.50) |
| Broken | 2.55 (2.5) | 3.56 (3.7) | 4.00 (4.3) | 0.68 (0.68) | 0.59 (0.70) | **0.00 (0.11)** |

| bass | notes / bar | changes / bar | median beats | >= 2 beats |
|---|---|---|---|---|
| Pulse | 2.40 (2.4) | 1.62 (1.6) | 0.93 (0.96) | 0.16 (0.26) |
| Two-Step | 1.64 (1.5) | 0.98 (1.0) | 0.79 (0.79) | 0.07 (0.11) |
| Broken | 2.32 (2.3) | 1.96 (2.1) | 1.22 (1.27) | 0.24 (0.35) |

Interval mix and kick relation are within 0.06 of the passport per category (Pulse octave 0.03 vs
0.08, Pulse "on the kick" 0.29 vs 0.23).

Outside +-25 %: Two-Step snares off 2 / 4 (the 5 + 13 snare is kept in every bar for a clear
backbeat; the passport's 0.72 per position includes sections without it), Broken kick repeat (the
grammar re-rolls a repeated kick bar), long bass notes (lengths are cut at the next note).

Lab (`HPDG_GenerationQualityLab --genre atmospheric --seeds 1000`, 4 bars): failures 0 in every
substyle; exact duplicates Pulse 349 / 1000 (Techno at 1000 seeds: 199-470), Two-Step 2, Broken 0.
Other genres: `--genre all --seeds 200` before / after — all 5000 pattern rows identical.

Not done yet (next steps of the spec):
- Sidechain pump on the Sub808 lane for Pulse (playback option, ~16 dB / 220 ms) — step 3.
- Sample lens per bar (the sample's roots, as `TechnoSampleLens`) — step 6.
- `tools/atmos_compare.py` position-profile L1 vs the nearest engine — step 6.
- Listening by the maintainer (blind against Techno / DnB) — step 7.

## 2026-10-14 — bass v2 (maintainer: "the bass is terrible")

Cause (notes dumped with `--notes`, renders checked: no clipping / clicks): the first composer matched
the passport statistics note by note — random onsets per bar (16ths 11, 16, 4 ...), a random walk of
scale steps away from the bar's root, and long 808 notes ringing through the four-on-the-floor kick.
The numbers fitted, the line had no groove and no harmony.

Now: a one-bar motif per substyle, repeated in every bar, each note a harmonic role (root, octave,
fifth, third, approach into the next root) over a 4-bar minor progression (i-VI-III-VII, i-iv-VI-v,
i-VII-VI-VII, ...). Pulse: off-beat 8ths between the kicks, every note ends before the next kick;
Two-Step: a held root with the kick and a late answer; Broken: long notes moving in thirds / fifths.
Amount [1] = the core groove (Pulse 3, Two-Step 2, Broken 2-3 notes / bar), [2] / [3] add notes.
The passport fit only leans the choice between motifs (wide pool). Core tests pass.

## 2026-10-14 — Pulse and Broken drums v2 (maintainer's listening)

- Pulse "sounds like an intro that shows the real pattern only at the end": the clap / hat sections
  and the half -> four kick (passport: whole songs) made the loop a build-up. Now the loop is the main
  section: kick four (or half) for the whole loop, clap 5 + 13 and hats in every bar, variation at the
  phrase end (pickups, a dropped last kick, open hat on 15). Scorer targets set for the main section
  (3.8 kicks, 2.3 snares, 5 hats per bar).
- Broken "a set of unconnected sounds": bars were re-composed from a near-flat profile. Now six
  kick / ghost motifs from the passport's strongest positions (kick 1, 6, 14, 16 ...; snare 5 + 13,
  ghosts 8, 16, 3, 7), repeated with an answer bar (every other bar or the phrase end) and a hat motif.
- Two-Step ("more or less fine") unchanged.
- Drum selection pool widened (tolerance 0.10, temperature 0.05): 300 seeds use all six Broken motifs.
  Core tests pass; lab 0 failures.

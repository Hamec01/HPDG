# Dark trap passport — 21 Savage / Metro Boomin type beats (2026-10-28)

Maintainer's request: the 21 Savage beats in `D:/Downloads/mp3` are what Dark Trap should sound like
— learn the rules of the style (not copy the beats) and make DarkTrap recognisable; give DarkTrap
its own 808 (three voices on HPDG's synth), loaded when the substyle is chosen. Numbers only here.

## Corpus and method

Same pipeline as `docs/audit/ATL_PASSPORT.md` (Demucs stems on the GPU, `tools/atmos_passport.py`,
`tools/atl_passport.py`, `tools/atl_hat_onsets.py`, `tools/atl_808_reference.py`).

- 8 files, 7 beats: `TRUST` and `TRUST-1` are the same beat (identical 808 measurement, 154 notes).
- Drums: 5 beats kept (snare on trap beat 3), 228 trap bars; MORAL and Exit read at 145 BPM without
  the snare on 3 are left out of the drum positions (their hats are measured).
- 808 sound: `Barbed` gave 4 "notes" of 26 s (a pad-like bass the meter cannot split) and is left
  out of the sound medians.

## Findings (references vs HPDG DarkTrap, 150 BPM, 1000 seeds)

| | 21 Savage type beats | DarkTrap before | DarkTrap after |
|---|---|---|---|
| hat gaps 1/8 / 1/16 | 0.86 / 0.02 (all 7 beats 0.66-0.96 / 0.00-0.11) | 0.40 / 0.30 | 0.86 / 0.06 |
| hats / bar | 5.9-8.6 | 7.5 | 7.7 |
| hat velocity sd (BreakLab) | 4.2-7.2 | (engine 45-96 range) | even line, 72-84 |
| hat rolls / 4 bars | 0 in 7 of 7 | 0.13 | 0.02 (a rare turnaround) |
| snare | beat 3 0.82 a bar, + 16th 3 0.21, 16th 15 0.14 | beat 3 only | beat 3 + 0.19 / 0.11 |
| kicks / bar | 1.2-2.5 (stem, lower bound); 1, 5, 7, 13, 15 | 2.34 | 2.05 |
| 808 notes / bar | 1.0-5.4 (median 3.1) | 2.06 | 2.54 |
| 808 octave moves | 0.00 in every beat | 0.11 | 0.02 |
| 808 register | D1-F#1 (median E1, 41 Hz) | G1 | G1 |

The style: a metronomic, unbroken 8th hat line at an even level, no rolls; the half-time snare on 3
with an extra snare on the "and" of beat 1 or the bar's last 8th now and then; the 808 busier than
the kick, never jumping octaves, low (around E1).

808 sound (median of track medians at the note's pitch): 2nd harmonic -15 dB, 3rd -14 dB, -12 dB
after 0.75 s; one beat (TRUST) is hard-clipped (-1 / -2 dB), one (OMEN) long and odd-heavy (h2 -33,
h3 -7, 2.8 s).

What sets the ATL / dark trap 808 apart from the other 808s HPDG has (`tools/oneshot_808_profile.py`):
the clean synthesized subs have no harmonics (-80 dB) and never decay; Sub Warm has ATL-like
harmonics but never decays; the Trap pack 808s decay 1.5-2.5x slower (-12 dB after 1.4-2.2 s). The
style 808 is saturated with a strong 2nd harmonic AND falls fast, then rings on.

## Changes (DarkTrap only; ATLClassic / LuxuryTrap measured identical)

- `generateSkeleton`: the 8th hat carrier skips 4x less, at an even velocity (72-84).
- `generateBarAdditions`: off-8th 16th hats at 0.08 of their chance; only a rare turnaround roll
  (8 % of 4th bars); extra snares on 16th 3 (0.21) and 15 (0.14), softer, never on a kick; one
  articulation kick at most (0.6 of its chance); up to three 808 answers (+0.35).
- `assignTrap808Line`: no in-bar octave moves (as ATL).
- Voices (`tools/tune_synth_808.py`, `tools/make_synth_sub.py`), measured at E1:
  Dark 1 (median) h2 -14.8 h3 -12.0 t-12 0.74 s; Dark 2 (hard clip) -2.2 / -6.2 / 0.42 s;
  Dark 3 (long) -33.1 / -9.6 / 2.92 s. DarkTrap starts on Dark 1 (`selectDefaultSynthSubLocked`).
- Test: `testTrapAtlClassicFollowsAtlPassport` checks DarkTrap's 8th line and 808 octaves too.

## Round 2 — after listening (2026-10-28)

Maintainer: the Dark 808 failed in sound and rhythm — "repeats too often, toy-like (like a Dendy),
too high and bright; an octave lower it farts, higher up it is almost a pluck; it plays the root and
then goes right up high". Ask: more sub, a little more low-mid, no highs; drums and drum patterns
matter; it must also work with sample analysis.

Tutorials read (YouTube auto-captions, 5 of 7; one Short and one 32-minute video had no captions):
ListenUpJames "How To Make METRO BOOMIN Type Beats", Connor Beats "How Metro Boomin Makes Dark Moody
Beats", "How To Make Dark Trap Beats Like Metro Boomin & Future's We Don't Trust You", Metro Boomin
himself (FL Studio / Razer "Beats Tutorial"), Pede "How To Make Beats Like Metro Boomin For Future".
Rules taken (not patterns copied):
- the kick is NOT on every 808 hit — "the 808 working one way, the kick in a different direction"
  (Metro Boomin); the 808 has its own rhythm;
- the 808 stays on the root ("I don't even want to change the note of the bass much"), changes at
  the end of a phrase; "quick notes" (a short 808 hit) are essential; a sub-like 808;
- hats: a two-step base, not complex, a few rolls at phrase ends; beats accented, the 8ths between
  softer ("duplicate them and lower the velocity: more bounce"); an open hat before the snare;
- snare on 3, layered, with accent snares between for bounce.

Why the first voice failed (`tools/synth_808_bands.py`, energy against the sub band): it was fitted
to the harmonics of Demucs bass stems, which carry the mix's distortion — Dark 2 had its low-mid
at -1 dB (a square wave: the Dendy sound) and every style voice fell 6 dB within 0.13-0.5 s (the
pluck). The Trap pack 808s hold 1.2-2.3 s with the low-mid at -25 dB.

Changes:
- Voices rebuilt (`tools/make_synth_sub.py`): sine body, soft saturation blended in, low-pass
  260-520 Hz, a 2-6 semitone punch at the attack, long hold. At F1: low-mid -15..-20 dB, mid and
  high under -75 dB, -6 dB after 1.6-4.3 s (was 0.15-0.43 s). Dark voices darker (low-pass 260-360 Hz),
  ATL a little brighter (380-520 Hz).
- 808 line (ATLClassic and DarkTrap): one place per pitch class from a fourth under to a tritone over
  the root (`TrapAlgebraEngine::styleWindow808Pitch`), so no octave jumps and no climbing high —
  also applied after the sample-led 808 (bass composer / follower / drum-loop blend). DarkTrap: the
  bar degree moves only at the end of a 4-bar phrase, in-bar repeats 0.75.
- DarkTrap rhythm: articulation kicks without an 808, other kicks off the phrase anchor with an 808
  half the time; up to three 808 answers (longer, 12-24 ticks); a quick 808 hit a 16th before a
  ringing one in 25 % of bars; kick articulation at 0.35 of its chance; hats by seed — an even 8th
  line, or a two-step (beats 84-96, between 60-72, a 16th slice before the snare / the bar's end);
  an open hat an 8th before the snare in 30 % of bars. Drum-loop blend under DarkTrap: only the first
  kick of a bar takes an 808.

| DarkTrap, 150 BPM, 1000 seeds | first version | round 2 |
|---|---|---|
| kicks / bar | 2.05 | 1.91 |
| 808 notes / bar | 2.54 | 2.38 (more than the kick) |
| 808 octave jumps / > octave | 0.02 / 0.03 | 0 / 0 |
| 808 in-bar repeats | 0.52 | 0.70 |
| hat gaps 1/8 / 1/16 | 0.86 / 0.06 | 0.85 / 0.08 |

With sample analysis (26 samples x 50 seeds, ATLClassic and DarkTrap): 808 octave jumps 0 in every
sample (tonal samples were up to 0.05 before the fold was made unique), 808 span 6.5-7.8 semitones
on average, 9.9 at most; no doubled hats; 90 % of the 808 pitches still come from a tonal sample.

## Open

- Listening by the maintainer.
- 808 notes 2.54 a bar vs 3.1 in the references, 808 length 1.38 vs 1.87 beats: the two pull against
  each other in the engine; left for listening.

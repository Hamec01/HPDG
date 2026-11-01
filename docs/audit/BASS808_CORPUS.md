# HPDG 808 — what pro 808 one-shots are made of (2026-11-01)

Maintainer's request: take HPDG's 808 synth out as its own bass plugin, to be used everywhere, and
build it from the 808s and Serum presets on the maintainer's PC (`D:/Drums`), not from guesses.
Branch `feature/bass-synth`. Numbers only here.

## Corpus and method

`tools/bass808_corpus.py`: 672 unique one-shots (duplicates by content skipped, at most 60 a folder)
from 14 packs — Cymatics Boomin / Bangin / Savage, Southside, [ALONE], DECAP, sc kits, brandobeats,
BWB (5 kits), UTT2, Sonic Specialists Distressed. Per file: settled f0, the pitch at 8-14 ms
against it (punch) and when it settles (within 0.5 st), harmonics 2-6 in the body (0.15-0.6 s), energy
above 1 kHz in the first 25 ms (click), crest factor, -6 / -12 / -24 dB times, length to -50 dB, level
0.5 s after the peak.

Serum presets (Cymatics Boomin / Bangin 808s, `.fxp`): the chunk is zlib; the knob values have no
names, but the wavetable and macro names read: the body is `Analog_BD_Sin` (a kick-drum sine with its
own pitch drop), a second oscillator plays a kick attack (`Attacks_Kick/XF_KikAtk_*`), macros DRIVE,
OVERTONE, DISTORTION, FILTER CUT, 100 Hz BOOST. HPDG 808 has the same parts: sine body with a pitch
punch, a click layer, drive / even / saturation, a key-tracked low-pass.

## The corpus

| | p10 | p25 | p50 | p75 | p90 |
|---|---|---|---|---|---|
| punch, st at 8-14 ms | 0.2 | 4.0 | 10.2 | 14.2 | 16.7 |
| punch settles, ms | 32 | 75 | 130 | 213 | 350 |
| h2 dB | -67 | -39 | -26 | -20 | -13 |
| h3 dB | -38 | -25 | -18 | -12 | -7 |
| h5 dB | -56 | -39 | -29 | -22 | -16 |
| click dB | -67 | -53 | -39 | -28 | -15 |
| -6 dB, s | 0.04 | 0.15 | 0.68 | 1.26 | 2.14 |
| -24 dB, s | 0.60 | 0.96 | 1.64 | 2.39 | 3.63 |
| level 0.5 s after the peak, dB | -18.9 | -7.2 | -3.6 | -1.8 | -0.6 |

Four characters (k-means; a fifth cluster of 41 is tape stops / the Distressed pack, left out):

| character | n | punch st / ms | h2 / h3 / h5 | click | -6 / -24 dB s | at 0.5 s |
|---|---|---|---|---|---|---|
| trap 808 | 315 | 12.6 / 141 | -22 / -14 / -27 | -36 | 0.52 / 1.54 | -4.1 |
| round (odd only) | 154 | 7.2 / 89 | -52 / -19 / -31 | -54 | 0.93 / 1.70 | -2.1 |
| long boom | 79 | 12.8 / 90 | -24 / -17 / -27 | -41 | 2.64 / 4.47 | -1.5 |
| clean | 81 | 1.9 / 165 | -40 / -49 / -60 | -36 | 0.12 / 1.18 | -9.5 |

Against the first synth (2026-10-28): the punch was 2-6 st over ~30 ms (a click, not a "boom"), the
harmonics even (asymmetric saturation; the corpus is odd-led, h3 over h2), no click layer, and a
pure exponential decay ringing 4-9 s to -24 dB (the corpus holds a plateau, then falls).

## HPDG 808 presets, measured the same way (E1)

| preset | from | punch st / ms | h2 / h3 / h5 | click | -6 / -24 dB s | at 0.5 s |
|---|---|---|---|---|---|---|
| ATL 1 | trap 808 | 12.5 / 129 | -19 / -16 / -29 | -36 | 0.65 / 1.70 | -3.1 |
| ATL 2 | trap, grittier | 13.9 / 126 | -14 / -16 / -30 | -37 | 0.58 / 1.40 | -3.2 |
| ATL 3 | trap, longer | 12.4 / 128 | -28 / -14 / -23 | -51 | 1.10 / 2.57 | -0.9 |
| Dark 1 | trap, darker | 14.2 / 144 | -26 / -14 / -24 | -58 | 1.02 / 2.49 | -2.0 |
| Dark 2 | round, pressure | 9.4 / 97 | none / -14 / -23 | -60 | 1.05 / 2.49 | -1.5 |
| Dark 3 | long boom, dark | 11.5 / 93 | -30 / -15 / -25 | -60 | 2.19 / 3.97+ | -0.4 |
| Boom | long boom | 13.4 / 89 | -27 / -15 / -24 | -40 | 2.73 / 3.98+ | -0.1 |
| Round | round | 7.6 / 80 | none / -18 / -31 | -56 | 0.75 / 1.68 | -2.0 |
| Clean | clean | 1.8 / - | -46 / -47 / -77 | -47 | 0.21 / 1.41 | -11.2 |
| Knock | trap, hard click | 14.6 / 144 | -15 / -15 / -28 | -31 | 0.65 / 1.52 | -4.2 |

The same on every note (`HPDG_Synth808Lab`, C1 / F1 / C2): low-mid within 0.1 dB, -6 dB time within
17 %; the pre-rendered WAVs played faster / slower drift 40-100 % in length (ATL 1: 2.62 s at C1,
1.46 s at C2). Highs (above 1 kHz) under -74 dB on every preset.

## Second pass: a bass instrument for every genre (2026-11-01)

Maintainer, after listening to the first pass: a screen that shows the wave, a second screen to mix
waves, samples loaded in the plugin with stretch / pitch / trim (a kick made into an 808), cutoff
filters, the HamloMag saturators from HPEQ (only them, not the plugin), no drawn piano, the HPDG dark
look, and presets for Boom Bap, Techno and DnB too.

Engine (`Source/Bass808/BassEngine`): synth layer (Sine with 1:1 FM growl, Triangle, Saw, Square / PWM,
Reese = two saws 4-50 cents apart; Sub one octave down or at the note; the 808 punch, saturation, tone
and click of the first pass) + sample layer (trim, loop of whole periods of the sample's detected pitch,
tune, follow key or not, LOCK TIME / Stretch by two 45 ms grains started a whole number of periods from
each other, reverse, invert) + handoff (the sample's attack crossfades into the synth) -> envelope with
Sustain -> filter LP12 / LP24 / HP12 / BP / Vowel (three fixed formants: a vowel's resonances do not move
with the note - Helmholtz, from the lecture notes the maintainer sent) with key, envelope, LFO (free or
synced) -> Phantom (harmonics 2-5 of the fundamental from envelope-normalised Chebyshev polynomials, held
one period, so a sub is heard on a phone) -> HamloMag (copied unchanged from HPEQ, `Source/Bass808/Mag`).

### The other genres' one-shots (same script, two new measures)

`bright_early` / `bright_late`: spectral centroid at 10-60 / 200-400 ms in multiples of f0 (the filter
envelope); `h_count`: harmonics within 30 dB of the strongest.

| genre | n | h2 / h3 / h5 | h_count | bright early -> late | click | -6 / -24 dB s | at 0.5 s |
|---|---|---|---|---|---|---|---|
| Boom Bap (9 packs) | 278 | -9 / -17 / -34 | 4 | 1.74 -> 1.33 | -29 | 0.30 / 0.71 | -9.3 |
| Techno (3 packs) | 100 | -20 / -22 / -38 | 3 | 1.58 -> 1.17 | -23 | 0.16 / 0.33 | ended |
| DnB (2 packs) | 71 | -20 / -19 / -26 | 6 | 3.81 -> 2.26 | -17 | 0.39 / 0.68 | -4.2 |

### The genre presets, measured the same way (E1)

| preset | h2 / h3 / h5 | h_count | bright | click | -6 / -24 dB s | at 0.5 s |
|---|---|---|---|---|---|---|
| Boom Bap Upright | -17 / -27 / -37 | 3 | 1.22 -> 1.06 | -31 | 0.22 / 0.74 | -14.4 |
| Boom Bap Dusty | -12 / -22 / -33 | 4 | 1.17 -> 1.08 | -35 | 0.31 / 0.92 | -9.0 |
| Boom Bap Stab | -7 / -14 / -28 | 5 | 1.51 -> 1.26 | -31 | 0.02 / 0.46 | -23.0 |
| Techno Rumble | -14 / -25 / -39 | 3 | 1.50 -> 1.05 | -21 | 0.11 / 0.43 | -30.2 |
| Techno Pulse | -8 / -18 / -27 | 7 | 1.54 -> 1.28 | -21 | 0.14 / 0.44 | -25.6 |
| Techno Acid | -6 / -6 / -24 | 8 | 1.77 -> 1.83 | -19 | (a held line, not a one-shot) | |
| DnB Sub | -15 / -16 / -29 | 5 | 1.57 -> 1.12 | -36 | (held) | -0.3 |
| DnB Reese | -3 / -14 / -17 | 14 | 2.30 -> 2.25 | -21 | (held) | -1.1 |
| DnB Talk | -12 / -10 / -31 | 20 | 4.90 -> 4.86 | -14 | (held) | -1.5 |

All inside the genre's p25-p75 except Boom Bap's brightness (1.17-1.22 against p25 1.20 / p50 1.74:
the presets are on the dark side) and Upright's h2 (-17 against p50 -9, p25 -21).

### Checks (`HPDG_Bass808Lab`, in ctest)

- Every preset at C1 / F1 / C2: finite, peak -0.4 .. -5.3 dBFS. The first pass's auto gain looked at the
  positive half-wave only; with Even the negative one is up to 1.6x larger, so the 808 presets peaked at
  +1 .. +9 dBFS. Now both half-waves (and Sub, Boost) count.
- Sample layer: pitch of a 55 Hz sine 55.00 Hz, of a kick (55 Hz body after a +150 Hz drop) 55.53 Hz;
  55 Hz sample on A2 110.00 Hz; FOLLOW KEY off 55.00 Hz; an octave up 1.22 -> 0.62 s, with LOCK TIME
  1.21 -> 1.20 s at 109.5 Hz; Stretch 2x 1.22 -> 2.42 s at 54.7 Hz (grains started at arbitrary
  positions gave 99 Hz and 72 Hz: they cancelled each other); a loop holds +0.3 dB at 2 s with no step
  (largest sample step 0.6x a sine's slope); a 70 ms handoff keeps the synth 57 dB down before 60 ms.
- Phantom 0.5 on a pure sine: h2..h5 from below -95 dB to -12 / -14 / -18 / -23 dB.
- All six HamloMag models finite at 24 dB drive.

## Open

- Listening by the maintainer: the plugin (`HPDG 808.vst3`, Standalone), renders in `TempAudio/bass808`
  (every preset at C1 / C2, a 140 BPM phrase each, `check_kick_loop.wav`, `check_kick_plus_808.wav`).
- HamloMag's auto gain keeps small signals at unity, so a high Drive is quieter (-16 .. -21 dBFS at
  24 dB): the Gain knob makes it up for now.
- ~~Wiring the engine into HPDG's Sub808 lane~~: done, see below.

## HPDG's Sub808 lane on the engine (2026-11-01)

HPDG's Sub808 lane is now always played by `BassEngine` (`Source/Engine/PreviewEngine.cpp`, live playback,
audition and the WAV exports alike). A lane on a bank sample plays it as a plain sample through the
engine (`sub808SampleOnlySettings`); a lane can instead carry an HPDG 808 sound (`Sub808SoundState`,
saved in the project). Strip under the rack: Filter (left: low-pass 20 kHz -> 200 Hz, right: high-pass
20 -> 400 Hz, 24 dB/oct), Drive (HamloMag), Length, Gain. EDIT / a click on the lane name opens the
HPDG 808 editor over the window. Presets saved there go to `Documents/HamloProd/Bass808 Presets/*.hpbass`,
read by both HPDG and the plugin.

The saturator's latency is 242 samples at 48 kHz (constant, the largest model); Sub808 notes are sent
that much earlier, so the bass lands on the grid.

### Same sound as before (`HPDG_Sub808EngineCheck`, in ctest)

Every Sub808 bank sample of Boom Bap, Trap, Drill, DnB and Techno (77) plays a two-bar phrase (retriggers,
a repeated key, two slides, legato, a long note) on the old sample player and through the engine:

| | first run | after the fixes |
|---|---|---|
| failed (corr <= 0.998 or level >= 0.1 dB) | 76 / 77 | 0 / 77 |
| worst residual | -3.5 dB | -26.9 dB |
| worst level difference | 1.19 dB | 0.04 dB |

Plain notes matched from the start (corr 1.0000, 0.00 dB). Three differences were found and fixed:
- a slide whose previous note ends one sample before it (tick rounding) restarted the sample from its quiet
  start (15-30 ms of silence): HPDG's slides now catch a note in its release, as the old player did;
- a short sample that had played out kept a silent voice alive, so the next slide glided on silence: with
  the synth off, a voice ends with its sample;
- the slide curve: the old player moved the playback rate in a straight line, the engine the pitch;
  HPDG's notes now glide linearly in rate (the plugin keeps its own glide).

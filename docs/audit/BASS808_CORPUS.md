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

## Open

- Listening by the maintainer (renders in `TempAudio/synth808`, the plugin `HPDG 808.vst3`).
- Wiring the voice into HPDG's Sub808 lane (after listening).

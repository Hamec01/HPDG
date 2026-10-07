# DnB stage (roadmap §51, stage 4)

Same method as Boom Bap / Trap: one hypothesis → smallest additive change → same benchmarks before /
after; references are guidance, not truth (several packs + written sources; never copy patterns).

## References

- Ghosthack Upfront Drum and Bass "DnB_Kick_n_Snare" (52 loops, 170-172 BPM): `HPDG_BreakLab analyze
  --bpm <label> --hits`, 16th = round(t / 16th) from the file start → `docs/audit/reference/dnb_kick_bars.tsv`
  (kick bars + snare bars). Snare on 4 (0.90) / 12 (0.64); a "4 + 10" snare variant in 18 % of bars.
- Freaky Loops *Dark Neurofunk and Cinematic DnB* drum loops (26, 174 BPM, full drums): median
  2.2 kicks a bar (1: 20 %, 2: 40 %, 3: 17 %, 4+: 22 %), `0 10` the commonest bar. Not yet in a tsv.
- The earlier DnB calibration (`c7bbaa7`) used another pack with bass MIDI (not on this PC):
  "~2 kicks a bar, downbeat + 2-step".
- Written: the 2-step is kicks on the 1st and 6th 8th (16ths 0 / 10), snares on 2 and 4 (MusicRadar,
  *How to program 6 jungle and DnB grooves*; Native Instruments, *Drum patterns*; Unison).
- Vendor spread: Ghosthack Upfront is busier (3.3 / 4.1 kicks a bar, calibration / validation) than
  Freaky, the old pack and the tutorials (~2), so the kick count was **not** raised to Ghosthack.

## Baseline (1000 seeds, defaults, 4 bars)

| Substyle | kicks/bar | kick-skeleton reuse | distinct kick bars / 4000 | exact / near dup | validation kick L1 |
|---|---|---|---|---|---|
| Modern | 1.87 | 79.4 % | 65 | 0.2 / 2.9 % | 2.48 |
| Roller | 2.87 | 85.7 % | 94 | 0.7 / 12.0 % | 1.81 |
| Liquid | 1.86 | 85.7 % | 59 | 0.6 / 11.3 % | 2.56 |
| Neurofunk | 2.89 | 69.3 % | 142 | 0.1 / 10.8 % | 1.90 |
| Jump-Up | 1.97 | 93.8 % | 30 | 3.3 / 28.5 % | 2.83 |
| Breakbeat | 3.89 | 35.1 % | 392 | 0.0 / 0.4 % | 2.25 |

Cause of the monotony: `addKicks` (DnBGrammar.cpp) budget = minKicks + lround(extra(density)); for
Modern / Liquid / Jump-Up (min 2, max 3) at default density extra = 0.325, so always 2 kicks: 2 in
77-89 % of bars, almost every bar `0 10`.

## Step 1 — a chance of one more kick (accepted, not yet listened to)

Change: the part `lround` drops becomes the chance of one more kick (never fewer kicks than before).
Variants measured and rejected: 2-step anchor weights from the calibration split (10 / 6 equal): no
gain, L1 worse in 3 of 6 substyles; full stochastic rounding: Roller / Neurofunk lost kicks
(2.87 → 2.73) and moved away from the reference.

| Substyle | kick-skeleton reuse | distinct kick bars | validation L1 | reference bars HPDG plays | exact / near dup |
|---|---|---|---|---|---|
| Modern | 79.4 → **59.4 %** | 65 → 127 | 2.48 → 2.25 | 31.7 → 38.3 % | 0.2 / 2.9 → 0.1 / 2.9 % |
| Liquid | 85.7 → **76.9 %** | 59 → 93 | 2.56 → 2.45 | 21.7 → 38.3 % | 0.6 / 11.3 → 0.7 / 10.1 % |
| Jump-Up | 93.8 → **85.7 %** | 30 → 69 | 2.83 → 2.70 | 25.0 → 23.3 % | 3.3 / 28.5 → 2.5 / 24.3 % |
| Roller / Neurofunk / Breakbeat | unchanged | unchanged | unchanged | unchanged | unchanged |

Kicks/bar Modern 1.87 → 2.23, Liquid 1.86 → 2.04, Jump-Up 1.97 → 2.15. Failures 0, determinism 0;
matrix: monotone 17 / 18 (same count as before), "density barely changes" 9 → 8 / 18; Roller /
Neurofunk at density 0.8: 2.9 → 3.2-3.3 kicks. Lane / core / track semantics tests pass.

## Open (next steps)

1. Kick-skeleton reuse still high (Roller 86 %, Jump-Up 86 %): the phrase layer repeats bar 1
   (barSimilarity 0.84-0.92). Check phrase variation against the reference (A / B bars visible in both packs).
2. Hats / ghost snares / bass vs references (Ghosthack "DnB_Top" loops = hats; Freaky stems).
3. The "4 + 10" snare variant (18 % of Ghosthack bars): HPDG never moves the backbone.

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

## Step 1 — a chance of one more kick (accepted, listened)

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

## Step 2 — phrase repetition (measured, no change)

Second reference: 26 Freaky Loops "DNC_174" loops (`docs/audit/reference/dnb_freaky_bars.tsv`,
built with `tools/dnb_phrase.py tsv`; noisy, loop 03 is phase-shifted). Measured with
`tools/dnb_phrase.py phrase` against the step 1 lab run (1000 seeds, defaults, 4 bars).

| Source | bar 1 != bar 2 (kick) | bar 1 != bar 3 | all 4 bars equal |
|---|---|---|---|
| Ghosthack Upfront | 0.79 | 0.13 | 9-16 % |
| Freaky DNC_174 | 0.42 | 0.42 | 9-16 % |
| HPDG Roller | 0.33 | 0.02 | 59 % |
| HPDG Jump-Up | 0.21 | 0.06 | 62 % |
| HPDG Modern / Liquid / Neurofunk / Breakbeat | 0.46-0.55 | | 19-37 % |

Roller and Jump-Up repeat far more than the packs, but the packs are mostly "showcase" loops and the
written sources describe exactly that repetition as the style: rollers are "fairly consistent" and
"roll along without too many variances" (MusicRadar; Attack Magazine), Jump-Up keeps a "strong,
consistent groove" with a fill every eight bars (Preset Drive). RULE 7 (quality > novelty): no change
to Roller / Jump-Up phrase variation. The other substyles already sit inside the reference range.

## Step 3 — a displaced second snare in the answer bar (accepted, listened 2026-10-08)

Reference (third column of both bars files): Ghosthack 4 + 12 in 60 % of bars, 4 + 10 in 16 %, 4 + 14 in
9 % (snare 16th probability p10 0.18 / p12 0.64 / p14 0.12); Freaky 54 / 10 / 2 % (p10 0.12 / p12 0.56).
Ghosthack 4 + 10 bars move the kick to 6 (every bar) and often add 14; 4 + 14 bars keep kicks 0 / 10.
Written: displacing the second snare by an 8th is a common DnB variation (MusicRadar). HPDG: 4 + 12 in
100 % of bars; the scorer and repair hard-coded 16 / 48.

Change (additive; style field `displacedSnareRate`, `DnBGenerationParams::displacedSnareTick`):
- once per seed the engine decides whether the answer bars (2nd bar of each phrase, never a fill) move
  the second snare to 40 (4 + 10, 65 %) or 56 (4 + 14, 35 %); `displaceSecondSnare` drops the ghosts
  of the old snare and clears kicks after 6 (4 + 10: kick on 6, 50 % a kick on 14) or after 10 (4 + 14);
- decided per candidate first: the search filtered it out (Modern 4 + 10 in 2 % of bars for a 30 %
  rate), so the choice moved to the engine (RULE: confidence survives the pipeline);
- scorer / repair / lab read a displaced backbone as the backbone (bit-identical at rate 0: the lab
  CSV equals step 1 except the timing column); lanes regenerated alone or next to a locked snare follow
  the displacement the project already plays (a new kick never lands on an old displaced snare);
- lab: new columns `displacedBackbeatPerBar`, `snareBars`; a DnB bar 4 + 10 / 4 + 14 counts as backbeat.

Rates: Modern 0.30, Roller 0.12, Liquid 0.15, Neurofunk 0.35, Jump-Up 0.10 (rollers / jump-up are
"consistent", step 2), Breakbeat 0 (tried 0.40: kick L1 worse on both packs, validation 2.25 → 2.39 /
2.93 → 2.97; it keeps its own break-detail snares).

| Substyle | 4 + 10 / 4 + 14 bars | snare L1 Ghosthack / Freaky | kick val. L1 Ghosthack | kick val. L1 Freaky | near dup | kick-skeleton reuse |
|---|---|---|---|---|---|---|
| Modern | 0 → 5.1 / 4.4 % | 1.15 → 1.11 / 1.11 → 1.08 | 2.25 → 2.23 | 1.98 → 1.97 | 2.9 → 1.9 % | 59.4 → 61.4 % |
| Roller | 0 → 3.0 / 1.8 % | 1.10 → 1.01 / 1.01 → 0.96 | 1.81 → 1.78 | 2.20 → 2.17 | 12.0 → 9.7 % | 85.7 → 81.9 % |
| Liquid | 0 → 2.1 / 2.0 % | 1.11 → 1.09 / 1.05 → 1.06 | 2.45 → 2.39 | 2.08 → 2.05 | 10.1 → 8.6 % | 76.9 → 73.7 % |
| Neurofunk | 0 → 8.1 / 5.5 % | 1.22 → 1.04 / 1.19 → 1.01 | 1.90 → 1.85 | 2.15 → 2.12 | 10.8 → 7.1 % | 69.3 → 65.1 % |
| Jump-Up | 0 → 1.5 / 1.3 % | 1.10 → 1.05 / 1.00 → 1.00 | 2.70 → 2.66 | 2.29 → 2.24 | 24.3 → 21.6 % | 85.7 → 83.1 % |
| Breakbeat | unchanged | unchanged | unchanged | unchanged | unchanged | unchanged |

Failures 0 / 1000 in every substyle, determinism 0. Matrix (300 seeds, density 0.2 / default / 0.8,
2 / 4 / 8 bars): failures only Breakbeat at density 0.8 (3.3 / 10.3 / 12.3 %), as in step 1. Core /
lane tests pass (the DnB invariant test now accepts 40 / 56 in an answer bar and checks no kick sits on
any backbone snare). Modern kick-skeleton reuse rose 59.4 → 61.4 % (the 4 + 10 bar has fewer kick
choices). Summaries: `docs/audit/dnb/step3_*`.

## Open (next steps)

1. (step 2: done, no change - Roller / Jump-Up repetition is genre-true.)
2. Hats / ghost snares / bass vs references (Ghosthack "DnB_Top" loops = hats; Freaky stems).
3. (step 3: done.) The DnB bass still ducks its stabs under 48, not under a displaced snare
   (`DnBBass.cpp` nearSnare / Stab); the frame from the project could pass the real snare ticks.

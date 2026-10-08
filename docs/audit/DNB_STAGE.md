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

## Step 4 — the bass ducks under a displaced snare (accepted)

`DnBBass.cpp` kept its stab ducking / snare-clash / sustained-onset rules on 16 / 48. Now the second
snare is read from the drum frame per bar (48, or 40 / 56 when the bar plays no 48). Only patterns with
a displaced snare change (227 of 1032). New lab column `bassOnSnareRate` (bass attacks on a non-ghost
snare 16th). In patterns with a displaced snare:

| Substyle | bass on snare | bass notes / bar | bass on kick |
|---|---|---|---|
| Modern | 8.7 → 6.5 % | 1.37 → 1.39 | 0.71 → 0.70 |
| Roller | 6.3 → 2.7 % | 1.50 → 1.50 | 0.71 → 0.73 |
| Liquid | 9.2 → 6.1 % | 1.89 → 1.84 | 0.53 → 0.55 |
| Neurofunk | 5.4 → 3.3 % | 1.05 → 1.03 | 0.87 → 0.89 |
| Jump-Up | 5.0 → 3.6 % | 1.02 → 1.01 | 0.83 → 0.84 |

## Step 5 — hats and ghost snares vs stems (hats: accepted; ghosts: measured, no change)

References: Ghost Syndicate *FAZE* drum loop stems (174 BPM): 32 hat stems → `docs/audit/reference/dnb_faze_hat_bars.tsv`,
20 kick & snare stems → `dnb_faze_snare_bars.tsv` (`tools/dnb_stems.py`; step = round(t / 16th) from the
file start - BreakLab's own grid phase-shifted the hat stems that open off the beat). Second check:
the 26 Freaky full loops (hat lane, conf >= 0.3; noisy).

Hats. Carrier per loop (the commonest bar shape): FAZE 8ths + 16th pickups 12, 16ths 8, 8ths 7,
broken / sparse 5, **offbeat-only 0** of 32; Freaky offbeat-only 1 of 26. Written: in DnB "the hi-hat
works to stabilise the rhythm by every single eighth note" (Attack Magazine, *Programming drum 'n' bass
in 5/4*). HPDG chose the offbeat-only carrier (16ths 2 / 6 / 10 / 14) in 10-40 % of patterns, so its
hat on beats 1-4 sat at 0.48-0.88 where the stems hold 0.88-0.96.
Change: offbeat-only carrier weight → rolling 8ths (Modern .15 → .05, Roller .10 → .05, Liquid .25 →
.10, Neurofunk .20 → .05, Jump-Up .40 → .10; Breakbeat had 0). Other carriers unchanged.

| Substyle | hat L1 calibration | hat L1 validation | hats / bar (FAZE 8.5-9.5) | near dup |
|---|---|---|---|---|
| Modern | 2.23 → 2.05 | 2.35 → 2.19 | 9.55 → 9.60 | 1.9 → 2.1 % |
| Roller | 3.13 → 2.93 | 3.93 → 3.73 | 10.91 → 10.97 | 9.7 → 9.5 % |
| Liquid | 2.63 → 2.32 | 2.70 → 2.13 | 8.18 → 8.62 | 8.6 → 7.9 % |
| Neurofunk | 3.33 → 2.67 | 4.03 → 3.24 | 10.87 → 10.71 | 7.1 → 6.2 % |
| Jump-Up | 2.68 → 2.20 | 2.62 → 1.79 | 9.10 → 9.32 | 21.6 → 24.6 % |
| Breakbeat | unchanged | unchanged | unchanged | unchanged |

Cost: Jump-Up near duplicates 21.6 → 24.6 % (the hat layer is more uniform). Still open: Roller /
Neurofunk / Breakbeat play the odd 16ths at 0.45-0.67 (FAZE 0.15-0.50) - 16th / broken carriers.

Ghost snares: not measurable from these stems. The quiet onsets BreakLab finds in the kick & snare
stems sit on 4 / 12 at full level (double triggers of the snare), and the off-backbone snares are loud
(velocity 79-127, 0.29 a bar, mostly on 16th 7). HPDG's loud off-backbone snares, 0.16-0.37 a bar,
are inside that. No change to ghosts without a reference.

## Step 6 — broken-16th hat mask; Breakbeat "kick spam" re-measured (hats: accepted, listened 2026-10-08)

**Second hat pack.** Ghosthack Upfront "DnB_Top" (53 loops, hats + percussion) →
`docs/audit/reference/dnb_ghosthack_top_bars.tsv` (`tools/dnb_stems.py hats`). It agrees with FAZE:
a hat on the beats in 0.80-1.00 of bars (16ths 4 / 12, under the snare: 0.88-0.92), odd 16ths ≈ 0.35.
HPDG (step 5): beats 4 / 12 at 0.48-0.65, odd 16ths ≈ 0.55 in Roller / Neurofunk / Breakbeat.

Two causes in `DnBGrammar.cpp`: the broken-16th carrier (`brokenMask`) kept every non-offbeat 16th with
one chance 0.55 (beats and odd 16ths alike), and `addCarrier` dropped the hat under the snare with 25 %.

| Variant (1000 seeds, defaults) | hat L1 val. FAZE (6 substyles) | hat L1 val. Ghosthack Top | near dup Jump-Up / Roller / Liquid | exact dup Jump-Up |
|---|---|---|---|---|
| step 5 | 1.79-3.88 | 2.00-3.16 | 24.6 / 9.5 / 7.9 % | 2.5 % |
| A: no drop under the snare | 1.26-3.63 | 1.66-2.91 | 41.6 / 19.7 / 12.9 % | 5.5 % |
| A': drop 10 % + B | 1.43-3.15 | 1.65-2.09 | 32.1 / 15.6 / 10.8 % | 4.2 % |
| **B: broken mask beats 0.90 / odd 16ths 0.40** | **1.66-3.38** | **1.91-2.33** | **23.6 / 10.6 / 8.0 %** | **2.5 %** |

Accepted: B only — closer to both packs in all 6 substyles with no duplicate cost (Breakbeat FAZE
3.88 → 3.03, Top 3.16 → 2.32; Roller 3.73 → 3.38 / 2.82 → 2.33). A / A' rejected for now: the random
drop under the snare is the only variation of the rolling-8th hat (Jump-Up, Liquid), and removing it
nearly doubles near duplicates (RULES 38 / 45) — a listening decision, not a measured win.

Other measures unchanged: failures 0, determinism 0, kicks / bass untouched; matrix "density barely
changes" 11 / 18 (as in step 5), three ±0.1 events non-monotone cells (noise of 300 seeds in Liquid /
Jump-Up, which pick the broken carrier in 10 % of patterns); lane / core / track semantics tests pass.

**Breakbeat "kick spam" (lab).** Since the Phase 0 baseline the lab flagged Breakbeat at density 0.8 as
failing (3.3 / 10.3 / 12.3 % at 2 / 4 / 8 bars): every flagged pattern averages 5.25 kicks a bar
(one bar of six), e.g. `0 1 2 5 10 15`, against a DnB limit of 5. Played chopped breaks reach that:
Ghosthack Upfront 3 / 52 loops average > 5 kicks a bar (up to 9.5), Freaky 1 / 26 (FAZE 0 / 20). RULE 10
— not a true failure at the top of the density range: the lab limit for DnB Breakbeat is now 6 (Boom
Bap's), 5 for the other DnB substyles. No generator change; the matrix now reports no failures.

## Step 7 — density leans the hat carrier (accepted, listened 2026-10-08)

**Finding.** From density 0.2 to 0.8, Modern / Liquid / Neurofunk / Jump-Up gained only +0.3-0.8 events
a bar ("density barely changes" 11 / 18 matrix cells). Density added kicks (+0.6-1.0) but nothing to the
hats (flat or falling), and the hat carrier is the largest lane. A probe with `pruneSecondary` switched
off changed nothing (removed afterwards) - the cause is in the grammar: the carrier (8ths / 16ths /
broken 16ths / ride ...) was picked from fixed substyle weights at any density.

**Written.** Calm liquid plays the hat on 8ths (Netsky, Future Music); 16th hats and rolls are the
intensity tool (Production Expert / Gearspace hat programming threads); the same lever as Trap step 3.

**Change.** Carrier weights lean with density around the substyle's default density:
busy carriers (16th shaker, broken 16ths) x (1 + lean), the rest x (1 - lean), lean = 1.2 x (density -
default), clamped to ±0.6. At the default density nothing changes (1000-seed lab CSV identical to step 6
in all 6 substyles; a first version neutral at 0.5 moved Breakbeat, default 0.6, and its hat L1 got worse).

| Substyle (300 seeds, 4 bars) | events/bar spread 0.2 → 0.8 | hats / bar 0.2 / default / 0.8 |
|---|---|---|
| Modern | +0.32 → **+2.17** | 9.6 / 9.4 / 9.3 → 8.4 / 9.4 / 10.0 |
| Roller | +1.57 → **+3.33** | 10.5 / 10.7 / 11.2 → 9.4 / 10.7 / 12.0 |
| Liquid | +0.61 → **+2.16** | 8.4 / 8.4 / 8.2 → 7.7 / 8.4 / 9.7 |
| Neurofunk | +0.82 → **+2.53** | 10.6 / 10.4 / 10.5 → 9.5 / 10.4 / 11.1 |
| Jump-Up | +0.56 → **+2.17** | 9.4 / 9.1 / 9.3 → 8.9 / 9.1 / 10.3 |
| Breakbeat | +2.45 → +4.17 | 9.8 / 10.4 / 10.4 → 8.7 / 10.4 / 10.9 |

Matrix: "density barely changes" 11 → **0 / 18**, monotone 18 / 18, failures none. Duplicates move only
at the density ends, both ways, within a few points (e.g. Liquid 0.8 / 4 bars near dup 9.7 → 15.3 %,
Roller 0.2 / 8 bars 9.3 → 4.3 %). Lane / core / track semantics tests pass.

## Open (next steps)

1. (step 2: done, no change - Roller / Jump-Up repetition is genre-true.)
2. (steps 5-6: hats done with FAZE + Ghosthack Top; ghosts not measurable from stems.) Hat under the snare
   (A / A') waits for listening.
4. (step 7: done - density leans the hat carrier.)
3. (steps 3-4: done.)

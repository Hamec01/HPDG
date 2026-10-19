# Rack Intensity 1-5 (2026-10-17)

Maintainer: a [1|2|3|4|5] switch in the Instrument Rack header, a generation mode. Pressing it changes
nothing that already plays; the next Generate / Mutate / lane RG writes in that mode. 3 = the engines as
they are; the lane Strength knobs keep working on the result (more right adds, more left removes).

History: the first version applied the level live to the current pattern (a strength shift on every
lane). In the plugin it did nothing (VST3 opens `Vst3EditorContent` in PluginProcessor.cpp, only
`PluginEditor` had been wired) and, once wired, it was the wrong behaviour: the maintainer wants a mode
for the next generation. A fresh Generate also skipped it (only lanes with a turned knob were re-shaped
after a pass). Replaced by the generation mode below.

## What each level writes (`LaneStrength::shapeForIntensity`, applied in `runGenerationPass`)

- 1, the primitive beat (`LaneStrength::primitive`), every note straight on the grid (no swing /
  humanize offsets): backbeat snare; first 808 of a bar ringing on; ghost kick, ghost snare, hat
  accents, open hats, ride, cymbal, perc silent.
  - Boom Bap: hats on quarter notes ("x---x---x---x---"), kick on 1 and 3.
  - Atmospheric: hats on quarter notes, kicks on quarters plus the first kick of a bar.
  - Trap, DnB, Techno: hats on every straight 8th, kicks on quarters plus the first kick of a bar.
- 2, clearly fewer: ghost notes and rolls dropped (not on the 808 lane: Techno marks soft bass notes as
  ghosts), ghost lanes silent, every lane thinned by -0.6 x its weight (kick / 808 0.6, snare 0.5,
  others 1.0); hats thinned on the grid (16ths, then 8ths between quarters, each position in every bar
  at once), so a straight pulse is left.
- 3, the engines as they are (byte-identical to before: same counts as the earlier baseline).
- 4 / 5, more: hits added from the lane's reserve (+0.35 / +0.70 x weight); the reserve (5 engine
  variants per lane) is built during that Generate, ~1 s.

Written lanes: every unlocked, enabled lane of a full pass, or the RG lane, also when a locked seed
gives the same notes again. Saved with the project (`intensity`). Notes are shaped before the sample
guide pass, which re-captures the base from them.

## Measured (`HPDG_LaneStrengthLab --intensity`: setIntensity + Generate, same seed per level, 4 bars)

Levels 1-3 over 1000 seeds per genre (`intensity_down_<genre>.csv`), 1-5 over 200 seeds for Trap
(`intensity_full_trap.csv`). Hits per bar, all lanes (range over substyles):

| genre        | 1          | 2           | 3           | 4           | 5           |
|--------------|------------|-------------|-------------|-------------|-------------|
| Boom Bap     | 8.0        | 8.4-9.4     | 12.8-13.8   |             |             |
| Trap         | 11.4-11.6  | 9.6-11.2    | 14.8-18.9   | 20.3-25.1   | 25.7-31.1   |
| DnB          | 11.7-12.1  | 10.5-12.6   | 15.8-21.0   |             |             |
| Techno       | 13.9-14.8  | 12.0-19.2   | 19.9-38.7   |             |             |
| Atmospheric  | 8.0-10.0   | 7.3-8.5     | 10.1-13.8   |             |             |

- Boom Bap level 1 is exactly 8 a bar in every substyle: kick 2, snare 2, hats 4.
- Bars with a kick and with a snare are the same at every level in every substyle; bars with an 808
  too (Techno Hard lost its bass at level 2 before the 808 exception, fixed).
- Level 1 can count more hits than level 2 where hats are filled to straight 8ths (Trap, DnB,
  Techno): level 1 is simpler, level 2 is sparser.
- Atmospheric level 2 leaves 0.8-1.7 hats a bar (its hats are mostly ghost / off-grid).
- Trap hat Strength knob unchanged by the refactor into `removeHatsByGrid` (1000-seed CSV identical).

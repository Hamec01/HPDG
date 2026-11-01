# Genre grid and explicit Trap rolls

## Findings

The VST3 Snap selector only repainted the grid, which always drew 1/16 lines.
Generation already used a 960 PPQ clock, 1/32, 1/64 and triplet subdivisions;
the visual Snap setting did not quantize its output. DarkTrap intentionally had
rare turnaround rolls. Frequent straight hats and Hat Accent notes are not
necessarily rolls, so the lab now counts semantic `roll` events separately.

## Changes

- On genre changes: Trap/Drill select 1/32; Boom Bap/Rap/DnB/Techno/Atmospheric
  select 1/16. Both editors apply the selection; users can override it afterward.
- The VST3 grid draws the selected subdivision, including triplets, at exact PPQ
  coordinates. Roll notes have a distinct colour and narrower minimum width.
- Trap Rolls: Off disables generated rolls; Style preserves the original grammar;
  Fills adds a short burst at phrase endings (also the ending of a 1/2-bar pattern).
  Fills requests at least four notes; zone boundaries or later processing may
  reduce this. Tested output retains at least three fast roll notes.
- Guide keeps the generated hat phrase ending in Fills mode instead of replacing
  it with sample hats. Exact Copy Break remains an exact-copy path.
- The new parameter is appended to preserve existing automation indices and is
  saved through APVTS. Style remains the default for existing and new sessions.

Snap is an editing/display choice, not a command to snap generated swing or
triplets onto one straight grid. Genre grammar chooses musical subdivisions.

## Measurement

Same product-path benchmark, 4 bars, default density per substyle, seeds 1–1000.
Three runs: original baseline, new Style, new Fills (18,000 generated patterns).
Seven timing/pitch signature columns matched in all 6000 original/Style rows.

`patternsWithRoll` means at least three semantic roll notes across HiHat and
Hat Accent in a pattern. It excludes ordinary fast carriers and non-roll accents.

| Substyle | Style patterns with rolls | Fills patterns with rolls |
| --- | ---: | ---: |
| ATLClassic | 24.9% | 100% |
| DarkTrap | 4.1% | 100% |
| CloudTrap | 26.9% | 100% |
| RageTrap | 72.2% | 100% |
| MemphisTrap | 45.8% | 100% |
| LuxuryTrap | 28.9% | 100% |

Structural failures: 0/6000 per run. Determinism failures: 0 in all configurations.
New Style/Fills runs were concurrent with compilation/testing; their timings are
not a controlled performance comparison against the serial baseline.

Additional checks: all 37 lane-boundary tests passed; DarkTrap Fills retained at
least three roll notes with sub-1/16 intervals in 64/64 seeds, and Off had zero
generated roll events in 64/64 seeds. VST3 Snap 1/32T reaches the grid as 80 ticks.
The full test suite also covers existing tempo modes, MIDI export, sample bass
alignment, sample copy and lane lock semantics.

Raw artifacts: `quality-lab/trap-rolls/{before,style,fills}_{patterns.csv,summary.json}`.
UI snapshot: `quality-lab/trap-rolls/grid-ui.png`.

Reproduce with HPDG_GenerationQualityLab:

```
--genre trap --seeds 1000 --bars 4 --rolls 1 --tag style --out <absolute-output-folder>
--genre trap --seeds 1000 --bars 4 --rolls 2 --tag fills --out <absolute-output-folder>
```

## Listening / use

Build the VST3, select Trap, choose Trap Rolls = Fills, then Generate or regenerate
the hats. Intensity 1/2 and reduced hat Strength intentionally remove rolls, so
compare with Intensity 3 and neutral Strength. Listen across several seeds and
substyles after building; listening approval is still pending. The VST3 bundle
was not built or installed in this task, at the user's request.

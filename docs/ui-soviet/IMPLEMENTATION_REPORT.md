# HPDG Soviet / Black Steel — implementation report

## Result

- Added `Soviet` as a fourth UI skin without changing the numeric values of Sketch, Steel or Dark.
- New installations default to Soviet; an existing saved skin selection remains respected.
- Genre and Substyle use the same amber LCD selector with left/right affordances and a popup list.
- The genre control is populated by the existing UI/parameter binding. It currently exposes the five product-visible genres: Boom Bap, Trap, DnB, Techno and Atmospheric. Future items use the same selector without adding fixed dial labels.
- Added the supplied Black Steel assets and drawing primitives: worn steel, hardware knobs, lamps, LCDs and CRT treatment.
- Applied the Soviet treatment to the header, Sample Analysis strip, instrument rack and VST3 pattern grid. The grid is rendered as an aged perforated card while keeping its existing click/edit behavior.
- Sketch, Steel and Dark remain available and are rendered by the existing paths.

## Scope guard

Only UI/theme/layout/drawing files and embedded UI assets were changed. Generator engines, genre/substyle IDs, generation callbacks, audio, analysis algorithms, export logic and project serialization were not changed.

## Rendered checks

- [Soviet](after/Soviet.png)
- [Sketch](after/Sketch.png)
- [Steel](after/Steel.png)
- [Dark](after/Dark.png)

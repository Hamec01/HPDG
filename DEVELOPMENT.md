# Development Notes

## Lane-First Boundary Stabilization

- `5b1a0e9` moved processor command APIs to lane-first overloads.
- `3ada633` moved editor command flow to lane-first calls.
- `74597f3` completed the lane-aware sample/export/temp-MIDI command boundary.
- Generation logic, preview playback internals, and serialization compatibility are intentionally unchanged at this point.

## Runtime Metadata Contract

- Run `HPDG_RuntimeMetadataContract` from the repository root.
- Running it from `build/Release` can fail to resolve repo-relative reference data under `StyleLabReferences` and produce false failures such as `BrooklynDrill reference directory is missing`.

## Safe Semantic Migration Freeze

- Current decision: `FREEZE CURRENT HYBRID STATE`.
- Completed in the safe migration phase:
	- lane-first processor/editor boundary
	- lane-aware sample/export boundary
	- `TrackSemantics` layer
	- semantic helper adoption in BoomBap, Rap, Drill, and Trap
	- role-based write-side bias alias
	- one successful controlled read-side experiment in Rap ride generation
- We stop here because further migration is behavior-sensitive and would move into output-critical generator paths.
- The current state already captures the main structural benefits without forcing broader generator refactors.
- If future work resumes, it must be treated as a new behavior-sensitive refactor phase. The first recommended theme is `perc texture lanes`.

## Architecture Status

- The current architecture is intentionally hybrid.
- Semantic abstractions are introduced where they are structurally safe.
- `TrackType` remains in output-sensitive generator paths by design.
- This is an intentional stopping point, not unfinished cleanup.
- The safe semantic migration phase is complete.
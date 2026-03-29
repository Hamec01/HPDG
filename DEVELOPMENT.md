# Development Notes

## Lane-First Boundary Stabilization

- `5b1a0e9` moved processor command APIs to lane-first overloads.
- `3ada633` moved editor command flow to lane-first calls.
- `74597f3` completed the lane-aware sample/export/temp-MIDI command boundary.
- Generation logic, preview playback internals, and serialization compatibility are intentionally unchanged at this point.

## Runtime Metadata Contract

- Run `HPDG_RuntimeMetadataContract` from the repository root.
- Running it from `build/Release` can fail to resolve repo-relative reference data under `StyleLabReferences` and produce false failures such as `BrooklynDrill reference directory is missing`.
# HPDG

HPDG (HamloProd Drum Generator) is a JUCE-based VST3 and Standalone instrument for fast drum pattern generation and editing.

The plugin is focused on modern beat production workflows: it generates genre-aware drum parts, lets you edit them inside the plugin, preview the result instantly, and export patterns for further work in a DAW.

## What HPDG Does

- Generates drum patterns for four genres:
  - Boom Bap
  - Rap
  - Trap
  - Drill
- Supports multiple substyles per genre.
- Works as both:
  - VST3 plugin
  - Standalone desktop app
- Provides an internal editor for pattern tweaking after generation.
- Supports sample-aware analysis and reference-driven workflows.
- Exports MIDI and loop audio for drag-and-drop or further arrangement.

## Core Features

### Genre-Aware Generation

HPDG builds patterns through separate engines for each genre. Each engine has its own timing, density, accent, support-lane and phrase logic, so the result is not just a single generic drum randomizer with different labels.

Supported genre families in the current project:

- Boom Bap
  - Classic
  - Dusty
  - Jazzy
  - Aggressive
  - LaidBack
  - BoomBapGold
  - RussianUnderground
  - LofiRap
- Rap
  - EastCoast
  - WestCoast
  - DirtySouthClassic
  - GermanStreetRap
  - RussianRap
  - RnBRap
  - HardcoreRap
- Trap
  - ATLClassic
  - DarkTrap
  - CloudTrap
  - RageTrap
  - MemphisTrap
  - LuxuryTrap
- Drill
  - UKDrill
  - BrooklynDrill
  - NYDrill
  - DarkDrill

### Pattern Control

The header and editor workflow expose the main musical controls directly in the plugin:

- BPM
- DAW sync
- Swing
- Velocity amount
- Timing amount
- Humanize amount
- Density amount
- Bar length: 1 / 2 / 4 / 8 / 16
- Genre and substyle selection
- Seed and seed lock
- Key root and scale mode

This makes HPDG useful both for quick idea generation and for controlled iteration on a repeatable seed.

### Editing Workflow

HPDG is not limited to one-click generation. After generating a pattern, you can continue shaping it inside the UI:

- grid editor for drum lanes
- lane-based track rack
- per-lane enable / mute / solo / lock
- sample switching per lane
- lane volume and sound controls
- microtiming and velocity editing
- dedicated Sub808 piano roll workflow
- regenerate / mutate / clear operations

### Preview, Export and Drag Workflow

The plugin includes direct production-oriented output actions:

- pattern preview playback
- loop-range playback mode
- full pattern export
- loop WAV export
- drag export workflow
- temporary MIDI export services for DAW handoff

### Sample Analysis and StyleLab

HPDG also contains infrastructure for analysis-driven generation and internal reference tooling:

- sample analysis panel
- audio file analysis support
- live input / audio file source modes
- analyze-only mode
- generate-from-sample mode
- StyleLab reference save / browse / apply workflow
- reference metadata and catalog services

This part of the project is intended for building more informed generation decisions from sample/reference material instead of relying only on static defaults.

## Tech Stack

- C++17
- JUCE
- CMake
- Visual Studio 2022 generator preset

Plugin target configuration in the current repository:

- Product name: `HPDG`
- Company: `BoomBap Labs`
- Formats: `VST3`, `Standalone`
- MIDI input: enabled
- MIDI output: enabled

## Build

### Requirements

- Windows
- Visual Studio 2022
- CMake 3.22+
- JUCE available in the repository under `JUCE/`

### Configure

```powershell
cmake --preset vs2022-x64
```

### Build Release

```powershell
cmake --build --preset release
```

Artifacts are generated in the `build/` directory.

## Project Structure

- `Source/Plugin` - JUCE processor/editor entry points
- `Source/Engine` - genre engines, style logic, preview, export, sample bank logic
- `Source/Core` - project model, serialization, track registry, runtime lane model
- `Source/UI` - editor components, grid, lane rack, StyleLab UI, analysis UI
- `Source/Services` - reference browser, temp MIDI export, import/export support
- `Source/Analysis` - sample analysis and feature extraction
- `Tests` - regression and contract-oriented tests
- `StyleLabReferences` - reference data used by StyleLab and validation flows
- `Samples` - sample content used by the instrument workflow

## Current Positioning

HPDG is designed as a beat-generation tool for producers who want:

- a fast starting point instead of drawing every hit from scratch
- genre-specific rhythmic behavior
- editable lanes after generation
- export-ready output for arrangement in a DAW
- room for more advanced sample/reference-driven generation workflows

## Development Notes

The repository currently contains:

- production code for the plugin and standalone app
- runtime contract tooling
- regression tests
- a documented hybrid generation architecture frozen after a safe semantic migration phase

For internal architecture notes, see `DEVELOPMENT.md`.

## Status

This repository is an active codebase for HPDG, not just a prototype UI. The current implementation already includes complete genre engines, editing UI, export flow, preview playback, and analysis/reference infrastructure.

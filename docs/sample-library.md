# Sample library names

Audio files in `Samples` use a genre prefix, a lane code and a number starting
at 1 in each folder. BoomBap uses `B`, Trap and Techno use `T` (in their separate
genre directories), DnB uses `D`, Rap uses `R` and Drill uses `DR`.

| Lane | Code | BoomBap example |
| --- | --- | --- |
| Kick | Kk | BKk1.wav |
| ClapGhost | CG | BCG1.wav |
| HiHat | HH | BHH1.wav |
| Perc | PC | BPC1.wav |
| Snare | SN | BSN1.wav |
| GhostKick | GK | BGK1.wav |
| HatFX | HF | BHF1.wav |
| OpenHat | OH | BOH1.wav |
| Ride | RD | BRD1.wav |
| Cymbal | CY | BCY1.wav |
| Sub808 | SB | BSB1.wav |

Nested source-kit folders follow the same convention; synth sounds use `SY`.
File extensions are preserved. Text documents are not renamed.

Each folder's `sample-names.json` maps the new filename to its original name.
Ship that file with the audio: the loader uses it to preserve sample ordering
(and saved selection indices), original style tags and bass root-note tuning.
The UI displays the compact name. New files without metadata use their filename
as before.

`tools/RenameSamples.ps1` performs the migration, verifies unchanged SHA256 hashes
and skips already mapped folders. Existing mapped folders with unmapped additions
require an explicit metadata update rather than silently renumbering samples.

## Techno

The own Ghosthack kit supplies Kick, ClapGhost, HiHat and Perc. Since the Techno
grammar writes its clap backbeat to Snare, the loader uses Techno/ClapGhost for
that lane when Techno/Snare is absent. GhostKick similarly uses Techno/Kick when
no separate ghost-kick kit exists. Dedicated lane folders always take priority.

A second Techno kit sits next to it: Sample Magic "Deep Melodic Tech-House 2" drum
hits (Kick TKk31+, Snare/claps TSN, HiHat THH26+, OpenHat TOH, Cymbal TCY, Perc
TPC26+, HatFX shakers THF, GhostKick soft low kicks TGK) and three Production
Music Live "Overdrive" bass one-shots re-pitched to C (TSB4-TSB6, measured
C2 / C1 / C1). Original names are kept in each sample-names.json.

Ride still uses the existing DnB, then BoomBap fallback (neither Techno kit has
rides).
The CMake post-build step copies the whole Samples tree, including metadata,
into both VST3 and Standalone outputs.

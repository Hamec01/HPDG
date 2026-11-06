# LuxuryTrap rhythm review after rejected listening result

Scope: only Trap substyle 5. The owner rejected the kick/808 notes and rhythm, with
and without a sample. Timbre changes and other substyle generators are outside this correction.

## What the previous checks missed

Audible output, deterministic generation and a valid C root do not establish musical
similarity. The previous implementation transferred analyzer timestamps directly into
143 phrase templates. Only 2% of their bass attacks coincided with their detected drum
kicks; 53.4% occurred up to one sixteenth later. These are diagnostics of the rejected
catalogue, not a ground-truth musical score.

The complete stem census contains 33 stems (~102.6 min), but automatic transcription
was not verified by ear. The earlier report and successful sound-check must not be
described as evidence that the groove sounds right.

## Timing evidence and reconstruction

`tools/luxury_stem_alignment.py` compares low-band envelope changes against original
mixes in consecutive cached 24-second sections. `tools/luxury_drum_alignment.py`
independently checks the drum timeline in the high band. Measurements are saved in
`quality-lab/luxurytrap-2026-11/stem_alignment.json`.

| Recording | Bass delay relative to mix |
| --- | ---: |
| ELEGANT (10) | 105 ms |
| SNAKE (11) | 50 ms |
| KINGS (5, held out) | 115 ms |
| Trap God (7) | 50 ms |
| LUXURY (8) | 90 ms |

These delays repeat across independent sections. Other reliable recordings measured
zero; recording 9 did not have sufficiently consistent alignment and is excluded
from the generated grammar. Original media is unchanged.

The revised builder removes measured delay, refines neural onsets against analytic
waveform amplitude, fits the clock to repeated snare transients and reconstructs the
sixteenth carrier. Unsupported grid fits are excluded rather than retained as
arbitrary 32nd-note delays. Fundamental periodicity resolves neural octave duplicates:
a loud second harmonic alone no longer creates a played octave jump.

Full-mix low-end detections are retained as timing evidence. A missing kick in the
drum stem does not prove a rest, and an 808 transient does not prove a separate kick.
Uncertain detections are no longer randomly added as extra kicks.

The revised catalogue has 105 training and 25 held-out four-bar phrases, split by
recording; the near-duplicate recording 6 is excluded from training. All source
recordings remain in the audit even when individual sections fail reconstruction.
This is not a claim of exact transcription of every supplied track.

## Runtime review

- A generated four-style-bar phrase begins with a joint kick/808 anchor. Later bars
  retain source rests and counter notes; this is not four-on-the-floor.
- Kick pruning no longer deletes an independently authored Luxury bass note.
- Generic backbeat avoidance no longer shifts Luxury bass notes to a neighbouring
  sixteenth. This caused duplicate starts and failed the monophonic-gate regression
  in the expanded catalogue; the corrected gate checks pass.
- Bass-only regeneration stays in the source family of the written kick. It no
  longer applies the generic nearest-kick snap to counter notes.
  Near-equivalent compatible bass phrases can rotate without changing that kick.
- Sample guidance transposes a contour using a duration-weighted root per bar,
  rather than replacing every pitch with an unrelated instantaneous detection.
  Its stored base pitch makes repeated guidance idempotent.
- Melodic-sample transient accents do not inject new kicks into the Luxury phrase.
- The existing Luxury hat carrier/ratchet system is retained. Tests cover duplicate
  closed hats, fine roll timing, Rolls Off and preservation during sample blending.
- The generic skeleton supplies no Luxury ghost-kick sequence. Main-kick legality,
  pruning, bass gates, post-generation validation and performance timing were traced.
- Bass synthesis, imported audio, tuning, Play/Stop and crackle fixes are unchanged
  by this rhythm correction.

Every runtime behavior above is guarded by Luxury substyle 5. Shared followers return
through the pre-existing path for other genres/substyles. Exact before/after note
comparison and current build/test results are recorded in the handoff after verification.

## Listening and delivery boundary

The owner's current FL instance was observed loading the installed module at
`C:/Program Files/Common Files/VST3/HPDG.vst3/Contents/x86_64-win/HPDG.vst3`
(06 Nov, 16:53). Later project builds were not installed into that running host.
Keep built artifacts, installed artifacts and actually loaded artifacts distinct.

Passing these checks establishes repaired timing/routing invariants, not listener
approval. Export current production-path audio for review before declaring musical
similarity successful. Do not claim that the agent listened to the stems by ear.

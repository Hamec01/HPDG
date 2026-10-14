# Atmospheric engine — spec (from the Izzamuzzic passport)

Source of every number: `ATMOS_PASSPORT.md` (52 tracks, 1457 bars, Demucs stems, confidence >= 0.5)
plus the maintainer's listening of 2026-10-14. Nothing here is copied from a track: the engine
generates from profiles, it never stores reference bars (RULES: references guide, they are not truth).

## 1. Scope and boundaries

- New genre `GenreType::Atmospheric`, **appended** after `Techno` (saved projects and parameter
  indices of the other genres stay the same). Shown in the genre list after Techno.
- Self-contained module like Techno: `Source/Engine/AtmosphericEngine.{h,cpp}` +
  `Source/Engine/Atmospheric/` (types, style profile, grammar, bass). It touches only the project it is
  given; no shared formula of Boom Bap / Trap / DnB / Techno changes (CLAUDE.md).
- Integration points (as Techno): the genre choice and `GenreType` mapping in `PluginProcessor`
  (choice index 6), a substyle parameter `atmosphericSubstyle`, the generate / generateTrackNew /
  regenerateTrackVariation / mutatePattern / mutateTrack dispatch, `genreHasActiveBassLocked`,
  `StyleDefaults`, `TempoInterpretation` (tempo range), `GenrePerformanceProfile`,
  `SampleLibraryManager` (kit folders), project serialization (genre and substyle by name).
- Lanes: Kick, Snare (clap), HiHat, Open hat, Perc, Sub808 (bass). Same `NoteEvent` model (ticks,
  micro offset, length, glide).

## 2. Substyles

Three substyles in the first version; dubstep (family C, 5 tracks) is left out: it overlaps Trap's
half-time and its tempo octave is not confirmed.

| substyle | from family | default / range BPM | character |
|---|---|---|---|
| **Pulse** | A (27 tracks) | 128 / 118-132 | straight kick, clap 2 / 4 with sections without it, off-beat 8th hats, sidechain bass |
| **Two-Step** | B (9 tracks) | 170 / 120-174 | kick 1 + 11, snare 5 + 13, sparse unducked bass |
| **Broken** | D1 (6 tracks) | 128 / 75-150 | 2 / 4 anchor, kicks and claps re-placed bar by bar, melodic bass |

Difference from the existing engines (must stay true, checked in the lab):
- Pulse vs Techno: Techno is a dense grammar with Euclidean percussion and an accent model; Pulse is
  sparse (hats 3.6 / bar vs Techno's 16th carriers), with clap drop-outs and a bass written against
  sidechain.
- Two-Step vs DnB: same bar skeleton, but Two-Step keeps 2.1 snares / bar with few ghosts, hats
  3.5 / bar on 8ths (DnB: broken 16ths), and its bass is 1.5 notes / bar (DnB: reese / stabs).

## 3. Drum grammar per substyle (targets = passport numbers)

Probabilities are per bar, 16th positions 1-16. The engine samples bars from these, then the scorer
checks the whole phrase.

**Pulse**
- Kick: bar types "four" (1-5-9-13) and "half" (1-9), mixed per section; kick off the beat 0 %.
  Targets: kick 1 0.72, 5 0.50, 9 0.70, 13 0.51; 2.6 kicks / bar.
- Clap: 5 + 13 when on; sections without the clap (passport: 0.25-0.28 per position over all bars,
  i.e. about half the bars). Snares off 2 / 4: up to 0.50 (flams / pickups on 4, 12, 15).
- Hats: 3.6 / bar, 83 % on 8ths, strongest on the off-beat 8ths 3, 7, 11, 15 (0.41-0.45).
- Repetition: kick bar = previous in ~54 %; a 2- or 4-bar phrase repeated, change at phrase ends.

**Two-Step**
- Kick 1 (0.65) and 11 (0.67), 1.8 / bar; off the beat 54 %.
- Snare 5 and 13 (0.72), 2.1 / bar; off 2 / 4 35 % (pickups on 3, 8, 10, 15).
- Hats 3.5 / bar, 76 % on 8ths, strongest on 1 and 9 (0.45, 0.55).
- Kick bar = previous ~50 %.

**Broken**
- Snare anchor on 5 / 13 (~0.58) plus 2-3 more claps / snares per bar anywhere (3.7 / bar, 70 % off
  2 / 4).
- Kick 2.5 / bar, 68 % off the beat, no fixed position (max share 0.30).
- Kick bar = previous only ~11 %, ~44 % of bars heard once: generate bar-by-bar variations of a motif,
  not a loop. Hats 4.3 / bar, 51 % on 8ths.
- Guard (quality > novelty): every bar keeps the 2 / 4 anchor or a clear replacement, the kick never
  stacks on the clap more than once per bar, and the phrase has a recognisable motif (shared
  positions between bars >= a threshold to be set in the lab).

## 4. Bass (Sub808 lane)

One bass composer, written against the kicks already chosen (same order as Techno: drums, then the
bass is searched against the written kicks), with a phrase plan of 2 or 4 bars.

| | Pulse | Two-Step | Broken |
|---|---|---|---|
| notes / bar (pitch changes) | 2.4 (1.6) | 1.5 (1.0) | 2.3 (2.1) |
| median length, share >= 2 beats | 0.96 beats, 0.26 | 0.79, 0.11 | 1.27, 0.35 |
| intervals | steps 0.29, repeats 0.22, 4th / 5th 0.19, thirds 0.14, octave 0.07 | repeats 0.32, steps 0.25, thirds 0.19 | thirds 0.35, steps 0.32, 4th / 5th 0.18 |
| vs kick | away 0.31, after 0.26, on 0.23, before 0.20 | away 0.47, on 0.30 | away 0.39, on 0.34 |
| register | around A#1 (MIDI 34) | A#1 | C2 (MIDI 36) |

- Does **not** double the kick: share of bass starts on a kick ~0.23-0.34.
- Sidechain (Pulse): MIDI notes are written on the grid; the pump is a playback option of the
  Sub808 lane (duck depth ~16 dB, release ~220 ms, triggered by the kick lane), default on for
  Pulse only. Shifting MIDI notes to imitate the pump is not allowed.
- Pitch: key / chord roots from the sample when key confidence >= 0.4 (as the other engines,
  `SampleBassFollower`), otherwise the project key; scale degrees weighted by the interval mix above.
- Glides: not measured in the passport — off by default; at most one per 4 bars when enabled.

## 5. Regenerating one lane

- Generate Kick: the existing bass is a constraint; the new kicks are scored against it.
- Generate Bass: drums stay; the bass is searched against them (and the sample, if loaded).
- Generate (all): drums then bass from one phrase plan.

## 6. Quality lab and acceptance

`HPDG_GenerationQualityLab --genre atmospheric --seeds 1000` per substyle, plus a passport comparison
(a new `tools/atmos_compare.py`: the same per-bar statistics on generated patterns as on the
references). Acceptance, per substyle:
- hard validity 100 %; no regression of other genres (their lab summaries identical);
- position profiles: L1 distance to the passport profile below that of the nearest existing engine
  (Pulse vs Techno, Two-Step vs DnB);
- the trait numbers within ±25 % of the passport (kicks / bar, snares / bar, off-beat share,
  repetition, bass notes / bar, interval mix);
- duplicates across seeds no worse than Techno's;
- the maintainer listens and approves (blind against Techno / DnB for Pulse / Two-Step).

## 7. Steps

1. Skeleton: genre, substyle parameter, empty engine wired through, serialization tests; other
   genres' lab output identical.
2. Pulse drums + lab comparison.
3. Bass composer (Pulse) + sidechain playback option.
4. Two-Step drums + bass.
5. Broken drums + bass (motif-variation generator).
6. Sample analysis lens (key / roots), Guide behaviour.
7. Listening, tuning, report.

# Research passport: Izzamuzzic (drums + bass), for a possible Atmospheric engine

Measurement only: no generator code was changed. Proposal under discussion: a separate
`Atmospheric` genre whose rhythm grammars and bass model come from measured references, not from the
categories in the external write-up (its "Atmospheric Half-Time / Broken / Electronic Pulse" table
was a guess and does not match what the corpus shows).

## Corpus and method

- 53 unique tracks (duplicates across releases removed, the newest release kept): Azzi (2011),
  Animal EP and The Drum'Ma (2013), Mauzzer and Young Blood (2014), Instinct EP (2017). The list is
  `izzamuzzic_tracks.txt`; audio stays on the dev PC (`D:/Downloads`). 52 gave usable drums.
- The later tracks the external write-up named (Shootout, line, Raindrop) are **not** in the corpus.
- Each mp3 is split into stems with Demucs (htdemucs). On the full mix the transcriber is not usable
  for whole songs (Instinct: K 0 / S 127, tempo conf 0.60); on the drums stem the same track gives
  tempo conf 0.90-1.00 and clean K / S / H.
- `tools/atmos_passport.py run`: tempo from the whole drums stem (as the plugin, two windows); two
  40 s windows at 30 % and 60 % of the song; drums transcribed at that tempo
  (`HPDG_BreakLab analyze --bpm T --cut --hits`), hits with confidence >= 0.5 only; bass transcribed
  from the bass stem (`HPDG_BreakLab lines --print`); repeated notes of one pitch closer than 120 ms are
  merged (a sidechain pump re-attacks the same note).
- **Beat 1.** A straight kick read an 8th / 16th off puts its kicks on 3-7-11-15 or 2-6-10-14. Each
  window is shifted by 0-3 16ths when that puts clearly more kicks on the beat and snares on 2 / 4
  (score = kicks on a beat + 2 x snares on 5 / 13; shift only with >= 8 kicks and >= 1.5 x the
  unshifted score). 14 tracks have a shifted window; the 7 straight-kick tracks that were off (Fly,
  Feala, Catch Me, Say, Cherry, Breathe In, Black Magic) now read 1-5-9-13; dubstep (Exit, Shutdown:
  kick 1-4-11-12 with a clear 2 / 4 snare) is not shifted.
- Ducking: the bass stem's level below 250 Hz around confident kicks (median over kicks), dip depth
  and the time until it is back within 3 dB.
- `tools/atmos_passport.py report` gives the per-track table and the two most common kick / snare bars
  of every track (`izzamuzzic_passport_tables.md`). `tools/atmos_families.py` groups the tracks into
  families from those bars (`izzamuzzic_families.txt`, with the 16-step profiles).

1457 bars in total. Grid positions are 16ths 1-16 of a bar at the measured tempo.

## Maintainer's listening (2026-10-14)

- Ayrton Senna, Crystal, I Feel: not rap; drum & bass-like, the fast tempo is right. Family B is the
  two-step bar, not hip-hop.
- Family D (the broken tracks): "a style of its own, and a cool one". Split into D1 (broken) and D2
  (sparse windows).
- Beat-1 shifts: not judged by ear; handled by the rule above.

## Result: five families

| | A straight + clap 2 / 4 | B two-step / DnB bar | C dubstep / half-time | D1 broken | D2 sparse |
|---|---|---|---|---|---|
| tracks / bars | 27 / 761 | 9 / 249 | 5 / 121 | 6 / 181 | 5 / 145 |
| tempo | 118-132 (19 at 128-130) | 120-170 | 75 (x4), 150 | 75-150 | 87-128 |
| kick (16ths, share of bars) | 1 0.72, 5 0.50, 9 0.70, 13 0.51 | 1 0.65, **11 0.67** | 1 0.77, 4 0.47, 11 0.54, 12 0.40 | spread: 14 0.30, 1 0.25, 6 0.23, 16 0.23 | sparse, 1.2 / bar |
| kicks off the beat | 0.00 | 0.54 | 0.66 | **0.68** | 0.38 |
| snare | 5 0.25, 13 0.28 (whole sections without) | 5 0.72, 13 0.72 | 5 0.68, 13 0.60 | 5 0.59, 13 0.57 + everywhere | 5 0.54 |
| snares / bar, off 2 / 4 | 1.1, 0.50 | 2.1, 0.35 | 1.7, 0.08 | **3.7, 0.70** | 2.7, 0.57 |
| hats / bar, on 8ths | 3.6, 0.83 | 3.5, 0.76 | **9.0**, 0.47 | 4.3, 0.51 | 2.8, 0.91 |
| kick bar = previous bar | 0.54 | 0.50 | 0.15 | **0.11** | 0.41 |
| bass notes / bar, pitch changes | 2.4, 1.6 | 1.5, 1.0 | 3.1, 3.0 | 2.3, 2.1 | 2.3, 1.8 |
| bass length (median), >= 2 beats | 0.96 beats, 0.26 | 0.79, 0.11 | 0.66, 0.12 | 1.27, 0.35 | 0.69, 0.05 |
| bass intervals | steps 0.29, repeats 0.22, 4th / 5th 0.19 | repeats 0.32, steps 0.25 | leaps > 5th 0.36 | **thirds 0.35**, steps 0.32 | steps 0.53 |
| bass vs kick | away 0.31, after 0.26, on 0.23, before 0.20 | away 0.47, on 0.30 | on 0.41, before 0.24 | away 0.39, on 0.34 | away 0.66 |
| bass ducking under kicks | **16 dB**, back after 222 ms | 0 dB | 4 dB | 3 dB | 10 dB (2 of 4) |

What the numbers say:

1. **Family A is the core** (27 of 52): 128-130 BPM, kick on every beat or on 1 / 3, clap on 2 / 4 that
   is absent for whole sections, hats on the 8ths (mostly the off-beat 8ths: 3, 7, 11, 15), heavy
   sidechain on the bass. Bars repeat (kick bar = previous in 54 %).
2. **Family B, two-step**: kick on 1 and 11, snare on 5 and 13; the drum & bass bar, also used at
   120-142. Bass sparse (1.5 notes / bar), repeats a lot, not ducked.
3. **Family C, dubstep**: 75 BPM, busy kick (1, 4, 11, 12), snare 2 / 4, dense hats (9 per bar), jumpy
   bass (leaps > a fifth 36 %).
4. **Family D1, broken**: the snare still marks 2 / 4 (0.57-0.59) but 70 % of snares / claps fall
   elsewhere (3.7 per bar), 68 % of kicks are off the beat, and a kick bar repeats the previous one
   only 11 % of the time: the groove is re-composed bar by bar. The bass is the most melodic: thirds
   35 %, steps 32 %, longer notes (median 1.27 beats, 35 % >= 2 beats), no sidechain.
5. **Bass vs kick** (A): the bass does not double the kick. Under 16 dB ducking, "on the kick" in MIDI
   sounds 50-150 ms late, so a generator should write the note on the kick and leave the pump to the
   sound (a sidechain / envelope option), not shift the MIDI.

## Known limits

- **Tempo octave** of C (75 vs 150) is not confirmed by ear yet.
- **D1** is 6 tracks / 181 bars with tempo conf 0.75 (A 0.82, B 0.90); part of its spread may be
  transcription noise. It needs more material (more tracks of this kind) before its profile is a
  grammar; the trait numbers (off-beat share, unique bars, snare count) are the robust part.
- **Sections.** Two 40 s windows per song; intros and outros are skipped, breakdowns are not. D2 is
  mostly windows that fell on sparse sections; it is not used as a style.
- **Bass stem.** Demucs puts some low synth / 808 into "bass" and some bass into "other"; "Say it"
  (87 BPM, tempo conf 0.17) has no usable bass. Glides are not measured.

## Next

1. Spec for the engine from these numbers: substyles A (straight / sidechain), B (two-step), D1
   (broken), possibly C (dubstep); per substyle the kick / snare / hat profiles, repetition rate,
   syncopation, and the bass model (notes per bar, interval mix, relation to the kick, sidechain as a
   sound option). B and C overlap the existing DnB and Trap engines; the spec has to say what is
   different.
2. More D1 references if the maintainer has them (tracks of the same broken kind).
3. Optional: the later tracks (Shootout, line, Raindrop, ...) if the target sound is the 2018+ one.

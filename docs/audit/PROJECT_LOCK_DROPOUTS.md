# Project lock dropouts (2026-11-03)

`processBlock` only try-locks `projectMutex`; when the message thread holds it, the whole block comes
out silent (a 5–10 ms hole, heard as a crackle). This pass removes the message thread's lock holds
during playback that did not need the lock. Generation is not touched (no genre engine changed).

## What held the lock while the beat plays (message thread)

| Path | How often | Hold before | Now |
| --- | --- | --- | --- |
| Editor timer: `isPreviewPlaying`, `getLastTransportSnapshot`, `getPreviewPlayheadStep` | 3 locks, 15 / s | µs each | no lock: published by the audio block (atomics; transport behind a SpinLock the audio thread only try-locks) |
| `getProjectSnapshot()` (editor refreshes, history, gestures, Sub808 lane menu, 808 overlay → strip) | on events; the 808 overlay up to 15 / s | full `PatternProject` copy, 0.1–0.8 ms | lock-free while the project is unchanged: a cached immutable `shared_ptr<const PatternProject>` keyed by a project revision; the first read after a change copies once under the lock |
| 808 overlay → strip refresh after each change | up to 15 / s | full project copy | the lane's sound only (`getSub808Sound`, µs) |
| HPDG Link fill (4 Hz, on a revision change) | on pattern / parameter changes | lock + fill | reads the cached view, no lock |
| `refreshSub808Sound` (strip knob, overlay, AsyncUpdater) | per sound change | 2 short locks | unchanged (2 short locks, no revision bump) |
| Strip knob → `setSub808Sound`, `getSub808Sound` | per change | short | unchanged |

How the revision works: every lock now goes through `lockProject()`. From a non-const member it returns a
`ProjectEditLock` that bumps `projectRevision` just before it unlocks (still held); from a const member it
is a plain lock. `lockProjectForRead()` is the plain lock for non-const members that do not touch
`project` (`stopPreview`, `refreshSub808Sound`). The audio thread bumps the revision only when it really
changed the project: the live params differ (`sameGeneratorParams`), a lane got its performance base,
or Start-with-DAW started the preview. Any edit invalidates the cache; nothing can read a stale project.

## Measurement (Tests/Sub808CrackleLab.cpp, host mode, genre 0 Boom Bap, seed 1, 8 s per case)

Same lab for before and after. The before binary is the same lab built on the unchanged sources (75eb30f).

Method changes in the lab (disclosed, applied to both sides):
- The message thread now also makes the editor timer's three reads each tick (`HPDG_CRACKLE_NO_TIMER`
  leaves them out = the old method); with knobs, the snapshot comes after the knob change, as the 808 overlay
  did. `HPDG_CRACKLE_HOST=n` repeats the cases n times.
- `HPDG_CRACKLE_STRESS`: the audio thread plays blocks back to back for the same 8 s. A block then finds the
  lock busy exactly as often as the message thread holds it, so this number does not depend on the
  machine's load or the Windows timer. **This is the number to compare.**
- `HPDG_CRACKLE_PRECISE`: the audio thread waits for its deadline by yielding instead of sleeping.

### Stress (failed try-locks / blocks)

| Case | Before | After |
| --- | --- | --- |
| bass off | 8 746 / 168 713 (5.2 %), 13 121 / 166 190 (7.9 %) | 103 / 154 831 (0.07 %), 228 / 148 428 (0.15 %) |
| bass on | 12 747 / 105 573 (12.1 %), 14 738 / 122 248 (12.1 %) | 195 / 109 013 (0.18 %), 102 / 100 513 (0.10 %) |
| bass on + knobs (lab takes a snapshot after each change) | 14 010 / 87 539 (16 %), 13 277 / 89 097 (14.9 %) | 9 363 / 84 493 (11.1 %), 10 945 / 83 387 (13.1 %) |
| bass on + knobs, no snapshot (= the editor now) | 2 535 / 78 288 (3.2 %) | 2 093 / 72 689 (2.9 %) |

Static playback: about 60× fewer busy-lock blocks. With the knobs, the project copy after each change is
gone from the real editor, and what is left is the 808 sound path below.

### Real time, 750 blocks of 512 at 48 kHz (dropouts)

| Run | bass off | bass on | bass on + knobs |
| --- | --- | --- | --- |
| Before, first session (sleeping audio thread) | 61, 49, 54 | 71, 49, 59 | 18, 16, 18 |
| Before, old method (no timer reads) | 63, 53 | 27, 60 | 16, 30 |
| After, same session (sleeping audio thread) | 0, 0, 0 | 0, 0, 0 | 17, 23, 18 |
| After, old method | 0, 0 | 0, 0 | 21, 18 |
| Before, later (sleeping) | 2, 0, 4 | 2, 1, 1 | 3, 5, 1 |
| After, later (sleeping) | 0, 0, 0 | 0, 0, 0 | 6, 2, 7 |
| Before, later, precise | 1, 0, 1 | 0, 0, 1 | 2, 1, 1 |
| After, later, precise | 0, 0, 0 | 0, 0, 0 | 0, 0, 0 |

The real-time count is not stable between sessions: the same unchanged before binary gave 49–71
dropouts in the first session and 2–7 an hour later. The likely cause is the Windows timer and the
machine's load. When the lab's audio thread sleeps, it oversleeps to a timer tick and plays the missed
blocks back to back exactly when the message thread wakes, so any lock taken right after waking is hit.
A temporary trace confirmed that 15 of 17 such failures fell inside a 3 µs `getSub808Sound` hold. With
the precise wait the after build has 0 everywhere. Hence the stress mode for the comparison.

## Left (not done here)

- The 808 sound path still takes short locks per knob change: `getSub808Sound`, `setSub808Sound`, two
  in `refreshSub808Sound`, and the AsyncUpdater rebuild. It accounts for about 3 % in stress, the same
  before and after. Making it lock-free would mean the lane's `Sub808SoundState` and `sub808Sound`
  handed to the audio thread through an atomic `shared_ptr` swap, not under `projectMutex`.
- Any edit still copies the project once under the lock on the next UI read (0.1–0.8 ms), so roughly
  one in 15–100 edits made during playback can still cost one block.
- Host tempo automation (DAW sync with a tempo ramp) changes `project.params` every block, which
  invalidates the cache every block; the editor then copies the project on each of its reads.

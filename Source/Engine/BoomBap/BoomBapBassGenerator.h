#pragma once

#include <random>
#include <vector>

#include "../../Core/NoteEvent.h"
#include "../../Core/PatternProject.h"

namespace bbg
{
// Boom Bap bass line (written into the Sub808 lane, which Boom Bap keeps off by default).
//
// Unlike a trap 808 (kick-doubling hits, slides, octave jumps), a boom bap bass is a line:
//   - the chord root on beat 1 of every bar, held until just before the next note
//   - extra notes only where the kick plays in the second half of the bar (locked with the
//     kick, never on the snare backbeat), mostly root, sometimes fifth / octave
//   - an optional approach note at the end of a bar leading into the next bar's root
//   - harmony from the key controls via a common progression; in guide mode the sample's own
//     bass then re-pitches the notes (SampleBassFollower)
// Notes carry real lengths: the bass plays exactly as long as the note in the pattern.
class BoomBapBassGenerator
{
public:
    // Returns the bass notes for the whole pattern (does not touch the project).
    static std::vector<NoteEvent> generate(const PatternProject& project, std::mt19937& rng);

    // Chord root pitch class used for each bar (exposed for tests / debug).
    static std::vector<int> progressionRoots(const GeneratorParams& params, std::mt19937& rng);
};
} // namespace bbg

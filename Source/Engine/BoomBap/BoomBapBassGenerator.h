#pragma once

#include <random>
#include <vector>

#include "../../Core/NoteEvent.h"
#include "../../Core/PatternProject.h"

namespace bbg
{
// Boom Bap bass line (written into the Sub808 lane, which Boom Bap keeps off by default).
//
// Every Generate picks one of several bass styles, each a set of randomized rules rather than a
// fixed riff, so consecutive generations sound different:
//   Classic      - the original line: chord root on beat 1, extra notes locked to the kick
//   SparseLow    - a few long, very low notes: root, the fifth below held, a late figure
//   Pump         - steady eighths on root / fifth / octave / b7, the odd ghost 16th
//   ChromaticWalk- eighths that walk chromatically into the next bar's root
//   KickRiff     - a low held root, then a high riff exactly on the kicks
//   SwingMelodic - a swung-16th melodic line in the upper octave
// The non-classic styles build a 1-bar motif, answer it in the next bar (A / A') and arrange the
// phrase with a variation bar and an ending bar (A A' A V A A' A E), the way real boom bap bass
// loops are played. Harmony comes from the key controls via a common progression; in guide mode
// the sample's own bass then re-pitches the notes (SampleBassFollower).
// Notes carry real lengths: the bass plays exactly as long as the note in the pattern.
class BoomBapBassGenerator
{
public:
    enum class Style
    {
        Classic = 0,
        SparseLow,
        Pump,
        ChromaticWalk,
        KickRiff,
        SwingMelodic,
        Count
    };

    // Returns the bass notes for the whole pattern (does not touch the project).
    static std::vector<NoteEvent> generate(const PatternProject& project, std::mt19937& rng);

    // Same, with the style forced (tests / debug).
    static std::vector<NoteEvent> generate(const PatternProject& project, std::mt19937& rng, Style style);

    // With a loaded sample the style follows its mood: calm / sad / sustained samples get
    // spacious lines (Sparse Low, Classic) and almost never the busy swung styles.
    // amount: the lane's bass-amount knob (0 Low, 1 More, 2 Full - see Sub808LaneSettings).
    static Style pickStyle(std::mt19937& rng, const SampleMood& mood = {}, int amount = 0);

    // How spacious the line is (0 busy .. 1 sparse) for a sample mood and the amount knob.
    static float effectiveCalm(const SampleMood& mood, int amount);

    // The amount knob of the project's bass lane.
    static int bassAmountOf(const PatternProject& project);
    static const char* styleName(Style style);

    // Chord root pitch class used for each bar (exposed for tests / debug).
    static std::vector<int> progressionRoots(const GeneratorParams& params, std::mt19937& rng);
};
} // namespace bbg

#pragma once

#include <random>

#include <juce_core/juce_core.h>

#include "../Analysis/SampleHarmonyAnalyzer.h"
#include "../Core/PatternProject.h"

namespace bbg
{
struct SampleBassLineReport
{
    bool applied = false;
    int notes = 0;
    int fromSampleBass = 0;  // notes taken from the sample's own bass line
    int fromMelody = 0;      // notes playing the sample's melody, brought down to the bass
    int onKick = 0;          // notes starting with a kick
    int withSnare = 0;       // short answers on the snare
    juce::String plan;       // bar roles, e.g. "B K B M"
};

// Bass mode [2] (the lane's middle amount button) over an analysed musical sample, Boom Bap and
// Trap: the line is composed FROM the sample rather than re-pitched afterwards. Each bar takes
// one role, arranged as a phrase (a 2-bar cell repeated, the 4th bar a variation):
//   B  follow the sample's bass line (its notes and rhythm; onsets near a kick lock to it)
//   K  ride the kicks: a note on every kick, pitched on what the sample's bass plays there
//   M  play the sample's melody, its contour brought down to the bass register (an 808
//      glides between melody notes now and then in Trap)
// plus, now and then, a short note answering the snare. Notes start on the sample's grid with
// its timing feel; the result is in the sample's key because every pitch comes from it.
class SampleBassLineComposer
{
public:
    // The lane is in mode [2], the genre is Boom Bap or Trap and the sample has lines to use.
    static bool wants(const PatternProject& project, const SampleHarmony& harmony);

    // bpm / originSeconds map the sample's timeline onto pattern ticks (octave-aligned bpm).
    static SampleBassLineReport compose(PatternProject& project,
                                        const SampleHarmony& harmony,
                                        double bpm,
                                        double originSeconds,
                                        std::mt19937& rng);
};

juce::String describeSampleBassLineReport(const SampleBassLineReport& report);
} // namespace bbg

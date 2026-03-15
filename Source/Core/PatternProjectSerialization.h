#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "PatternProject.h"

namespace bbg
{
class PatternProjectSerialization
{
public:
    static constexpr int kPatternSchemaVersion = 3;

    static juce::ValueTree serialize(const PatternProject& project);
    static bool deserialize(const juce::ValueTree& rootState, PatternProject& projectOut);
    static void validate(PatternProject& project);

private:
    static juce::ValueTree serializeTrack(const TrackState& track);
    static juce::ValueTree serializeNote(const NoteEvent& note);
};
} // namespace bbg

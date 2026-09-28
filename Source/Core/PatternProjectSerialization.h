#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "PatternProject.h"

namespace bbg
{
class PatternProjectSerialization
{
public:
    // v11: NoteEvent/Sub808NoteEvent became tick-native (gridTick/timingOffsetTicks/lengthTicks).
    // Notes saved by v10 and earlier are migrated on load: gridTick = step * 240,
    // timingOffsetTicks = micro_offset, lengthTicks = length * 240 (see deserializeNote).
    static constexpr int kPatternSchemaVersion = 11;

    static juce::ValueTree serialize(const PatternProject& project);
    static bool deserialize(const juce::ValueTree& rootState, PatternProject& projectOut);
    static void validate(PatternProject& project);

private:
    static juce::ValueTree serializeTrack(const TrackState& track);
    static juce::ValueTree serializeNote(const NoteEvent& note);
};
} // namespace bbg

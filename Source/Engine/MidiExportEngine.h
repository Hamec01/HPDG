#pragma once

#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Core/PatternProject.h"

namespace bbg
{
class MidiExportEngine
{
public:
    static juce::MidiMessageSequence patternToSequence(const PatternProject& project,
                                                       std::optional<TrackType> onlyTrack,
                                                       int ppq = 960,
                                                       bool honorMute = true,
                                                       bool honorSolo = true);

    static juce::MidiMessageSequence trackToSequence(const TrackState& track,
                                                     int bars,
                                                     int ppq = 960);

    static bool saveMidiFile(const PatternProject& project,
                             const juce::File& file,
                             std::optional<TrackType> onlyTrack,
                             int ppq = 960,
                             bool honorMute = true,
                             bool honorSolo = true);

    static bool saveMultiTrackMidiFile(const PatternProject& project,
                                       const juce::File& file,
                                       int ppq = 960);
};
} // namespace bbg

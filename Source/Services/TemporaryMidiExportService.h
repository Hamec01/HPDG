#pragma once

#include <optional>

#include <juce_core/juce_core.h>

#include "../Core/PatternProject.h"

namespace bbg
{
class TemporaryMidiExportService
{
public:
    static juce::File createTempFileForPattern(const PatternProject& project);
    static juce::File createTempFileForTrack(const PatternProject& project, TrackType trackType);

private:
    static juce::File createTempFile(const juce::String& prefix);
    static juce::File exportTemp(const PatternProject& project, std::optional<TrackType> onlyTrack, const juce::String& prefix);
};
} // namespace bbg

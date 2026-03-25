#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/NoteEvent.h"
#include "../Core/TrackType.h"

namespace bbg
{
struct MidiImportResult
{
    bool success = false;
    juce::String errorMessage;
    juce::String summary;
    std::vector<NoteEvent> notes;
    int importedNoteCount = 0;
    int requiredBars = 1;
};

class MidiImportService
{
public:
    static MidiImportResult importLaneFromFile(const juce::File& midiFile,
                                               TrackType targetTrack,
                                               int fallbackBars);
};
} // namespace bbg
#include "MidiImportService.h"

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Core/TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
int defaultPitchForTrack(TrackType type)
{
    if (const auto* info = TrackRegistry::find(type))
        return info->defaultMidiNote;

    return 36;
}

int internalTickFromSourceTick(double sourceTick, int sourcePpq)
{
    const auto ratio = static_cast<double>(kInternalPpq) / static_cast<double>(juce::jmax(1, sourcePpq));
    return static_cast<int>(std::lround(sourceTick * ratio));
}

// Imported notes keep their true tick position — no forced quantization to the nearest 1/16.
// A genuine 1/32, 1/64 or triplet position in the source MIDI stays exactly that after import.
void setNoteStartFromTick(NoteEvent& note, int tick)
{
    note.gridTick = juce::jmax(0, tick);
    note.timingOffsetTicks = 0;
}
}

MidiImportResult MidiImportService::importLaneFromFile(const juce::File& midiFile,
                                                       TrackType targetTrack,
                                                       int fallbackBars)
{
    MidiImportResult result;

    if (!midiFile.existsAsFile())
    {
        result.errorMessage = "MIDI file does not exist.";
        return result;
    }

    if (!midiFile.hasFileExtension("mid;midi"))
    {
        result.errorMessage = "Select a .mid or .midi file.";
        return result;
    }

    juce::FileInputStream input(midiFile);
    if (!input.openedOk())
    {
        result.errorMessage = "Failed to open MIDI file.";
        return result;
    }

    juce::MidiFile midi;
    if (!midi.readFrom(input))
    {
        result.errorMessage = "Failed to read MIDI data.";
        return result;
    }

    const int sourcePpq = midi.getTimeFormat();
    if (sourcePpq <= 0)
    {
        result.errorMessage = "Only PPQ-based MIDI files are supported.";
        return result;
    }

    std::vector<NoteEvent> importedNotes;
    importedNotes.reserve(128);

    int maxEndTick = 0;
    const int lanePitch = defaultPitchForTrack(targetTrack);

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        if (const auto* sourceSequence = midi.getTrack(trackIndex))
        {
            auto sequence = *sourceSequence;
            sequence.updateMatchedPairs();

            for (int eventIndex = 0; eventIndex < sequence.getNumEvents(); ++eventIndex)
            {
                const auto* holder = sequence.getEventPointer(eventIndex);
                if (holder == nullptr || !holder->message.isNoteOn()) 
                    continue;

                const int startTick = internalTickFromSourceTick(holder->message.getTimeStamp(), sourcePpq);
                const int endTick = holder->noteOffObject != nullptr
                    ? internalTickFromSourceTick(holder->noteOffObject->message.getTimeStamp(), sourcePpq)
                    : (startTick + ticksPerStep());

                NoteEvent note;
                note.pitch = targetTrack == TrackType::Sub808
                    ? juce::jlimit(24, 84, holder->message.getNoteNumber())
                    : lanePitch;
                note.velocity = juce::jlimit(1, 127, static_cast<int>(holder->message.getVelocity()));
                note.lengthTicks = juce::jmax(1, endTick - startTick);
                setNoteStartFromTick(note, juce::jmax(0, startTick));

                importedNotes.push_back(note);
                maxEndTick = juce::jmax(maxEndTick, endTick);
            }
        }
    }

    if (importedNotes.empty())
    {
        result.errorMessage = "No note-on events were found in the MIDI file.";
        return result;
    }

    std::sort(importedNotes.begin(), importedNotes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.gridTick != b.gridTick)
            return a.gridTick < b.gridTick;
        if (a.timingOffsetTicks != b.timingOffsetTicks)
            return a.timingOffsetTicks < b.timingOffsetTicks;
        return a.velocity > b.velocity;
    });

    importedNotes.erase(std::unique(importedNotes.begin(), importedNotes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.gridTick == b.gridTick
            && a.timingOffsetTicks == b.timingOffsetTicks
            && a.pitch == b.pitch
            && a.lengthTicks == b.lengthTicks;
    }), importedNotes.end());

    const int barTicks = kInternalPpq * 4;
    result.success = true;
    result.importedNoteCount = static_cast<int>(importedNotes.size());
    result.requiredBars = juce::jmax(fallbackBars,
                                     juce::jmax(1, static_cast<int>(std::ceil(static_cast<double>(juce::jmax(1, maxEndTick))
                                                                              / static_cast<double>(barTicks)))));
    result.summary = "Imported " + juce::String(result.importedNoteCount)
        + " MIDI notes from " + midiFile.getFileName();
    result.notes = std::move(importedNotes);
    return result;
}
} // namespace bbg
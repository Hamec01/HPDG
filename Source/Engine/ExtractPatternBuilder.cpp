#include "ExtractPatternBuilder.h"

#include <algorithm>

namespace bbg
{
namespace
{
NoteEvent toNoteEvent(const TranscribedEvent& event, bool bassEvent)
{
    NoteEvent note;
    note.pitch = event.pitch;
    note.step = event.step;
    note.length = juce::jmax(1, event.lengthSteps);
    note.velocity = juce::jlimit(1, 127, event.velocity);
    note.isGhost = event.ghost;
    note.semanticRole = bassEvent ? "sample_copy_bass" : "sample_copy";
    return note;
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        if (left.step != right.step)
            return left.step < right.step;
        if (left.pitch != right.pitch)
            return left.pitch < right.pitch;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        return left.step == right.step && left.pitch == right.pitch;
    }), notes.end());
}
}

ExtractedPatternData ExtractPatternBuilder::build(const SampleAnalysisBundle& bundle)
{
    ExtractedPatternData result;
    result.bars = juce::jlimit(0, 16, bundle.summary.analyzedBars);
    result.phraseBoundaries = bundle.summary.phraseBoundaryBars;

    int highestStep = -1;

    for (const auto& event : bundle.transcription.drumEvents)
    {
        const auto laneIndex = static_cast<size_t>(trackTypeIndex(event.lane));
        if (laneIndex >= result.laneNotes.size())
            continue;

        result.laneNotes[laneIndex].push_back(toNoteEvent(event, false));
        highestStep = juce::jmax(highestStep, event.step + juce::jmax(0, event.lengthSteps - 1));
    }

    for (const auto& event : bundle.transcription.bassEvents)
    {
        auto note = toNoteEvent(event, true);
        note.semanticRole = "sample_copy";
        result.laneNotes[static_cast<size_t>(trackTypeIndex(TrackType::Sub808))].push_back(note);
        highestStep = juce::jmax(highestStep, event.step + juce::jmax(0, event.lengthSteps - 1));
    }

    for (auto& lane : result.laneNotes)
        dedupeAndSort(lane);

    std::sort(result.phraseBoundaries.begin(), result.phraseBoundaries.end());
    result.phraseBoundaries.erase(std::unique(result.phraseBoundaries.begin(), result.phraseBoundaries.end()),
                                  result.phraseBoundaries.end());

    if (result.bars <= 0 && highestStep >= 0)
        result.bars = juce::jlimit(1, 16, highestStep / 16 + 1);

    return result;
}
} // namespace bbg
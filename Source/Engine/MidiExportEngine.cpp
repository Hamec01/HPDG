#include "MidiExportEngine.h"

#include <algorithm>

#include "../Core/TrackRegistry.h"
#include "../Utils/MidiHelpers.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
bool hasSoloTracks(const PatternProject& project)
{
    for (const auto& track : project.tracks)
    {
        if (track.solo)
            return true;
    }

    return false;
}

bool shouldIncludeTrack(const PatternProject& project,
                        const TrackState& track,
                        std::optional<TrackType> onlyTrack,
                        bool honorMute,
                        bool honorSolo)
{
    if (onlyTrack.has_value() && track.type != *onlyTrack)
        return false;

    if (!track.enabled)
        return false;

    if (honorMute && track.muted)
        return false;

    const bool soloExists = honorSolo && hasSoloTracks(project);
    if (soloExists && !track.solo)
        return false;

    return true;
}

int exportMidiPitch(const TrackState& track, const NoteEvent& note)
{
    // FL workflow: keep all drum lanes on one piano key (C5) for quick lane routing.
    // Preserve musical pitch only for Sub808.
    if (track.type != TrackType::Sub808)
        return 60; // C5 in FL Studio note naming

    const auto* info = TrackRegistry::find(track.type);
    return std::clamp(note.pitch > 0 ? note.pitch : (info != nullptr ? info->defaultMidiNote : 34), 0, 127);
}

const TrackState* findTrackState(const PatternProject& project, TrackType type)
{
    for (const auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

void addTrackNameEvent(juce::MidiMessageSequence& sequence, const juce::String& name)
{
    auto trackName = juce::MidiMessage::textMetaEvent(0x03, name);
    trackName.setTimeStamp(0.0);
    sequence.addEvent(trackName);
}
} // namespace

juce::MidiMessageSequence MidiExportEngine::trackToSequence(const TrackState& track, int, int ppq)
{
    juce::MidiMessageSequence sequence;

    for (const auto& note : track.notes)
    {
        const int midiNote = exportMidiPitch(track, note);

        const int baseTick = stepToTicks(note.step, ppq);
        const int startTick = baseTick + note.microOffset;
        const int endTick = startTick + std::max(1, note.length) * ticksPerStep(ppq);

        auto on = juce::MidiMessage::noteOn(1, midiNote, static_cast<juce::uint8>(clampVelocity(note.velocity)));
        auto off = juce::MidiMessage::noteOff(1, midiNote);
        on.setTimeStamp(static_cast<double>(std::max(0, startTick)));
        off.setTimeStamp(static_cast<double>(std::max(1, endTick)));

        sequence.addEvent(on);
        sequence.addEvent(off);
    }

    sequence.sort();
    sequence.updateMatchedPairs();
    return sequence;
}

juce::MidiMessageSequence MidiExportEngine::patternToSequence(const PatternProject& project,
                                                              std::optional<TrackType> onlyTrack,
                                                              int ppq,
                                                              bool honorMute,
                                                              bool honorSolo)
{
    juce::MidiMessageSequence sequence;

    for (const auto& track : project.tracks)
    {
        if (!shouldIncludeTrack(project, track, onlyTrack, honorMute, honorSolo))
            continue;

        auto trackSequence = trackToSequence(track, project.params.bars, ppq);
        for (int i = 0; i < trackSequence.getNumEvents(); ++i)
        {
            if (auto* event = trackSequence.getEventPointer(i))
                sequence.addEvent(event->message);
        }
    }

    sequence.sort();
    sequence.updateMatchedPairs();
    return sequence;
}

bool MidiExportEngine::saveMidiFile(const PatternProject& project,
                                    const juce::File& file,
                                    std::optional<TrackType> onlyTrack,
                                    int ppq,
                                    bool honorMute,
                                    bool honorSolo)
{
    try
    {
        auto sequence = patternToSequence(project, onlyTrack, ppq, honorMute, honorSolo);

        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(ppq);
        midi.addTrack(sequence);

        if (auto stream = file.createOutputStream())
        {
            stream->setPosition(0);
            stream->truncate();
            return midi.writeTo(*stream, 1);
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

bool MidiExportEngine::saveMultiTrackMidiFile(const PatternProject& project,
                                              const juce::File& file,
                                              int ppq)
{
    try
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(ppq);

        for (const auto& info : TrackRegistry::all())
        {
            juce::MidiMessageSequence laneSequence;
            addTrackNameEvent(laneSequence, info.displayName);

            if (const auto* track = findTrackState(project, info.type); track != nullptr)
            {
                const auto noteSequence = trackToSequence(*track, project.params.bars, ppq);
                for (int i = 0; i < noteSequence.getNumEvents(); ++i)
                {
                    if (const auto* event = noteSequence.getEventPointer(i))
                        laneSequence.addEvent(event->message);
                }
            }

            laneSequence.sort();
            laneSequence.updateMatchedPairs();
            midi.addTrack(laneSequence);
        }

        if (auto stream = file.createOutputStream())
        {
            stream->setPosition(0);
            stream->truncate();
            return midi.writeTo(*stream, 1);
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

} // namespace bbg

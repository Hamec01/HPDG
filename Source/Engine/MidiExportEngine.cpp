#include "MidiExportEngine.h"

#include <algorithm>

#include "../Core/TrackRegistry.h"
#include "../Utils/MidiHelpers.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
bool isHatChokeTrack(TrackType type)
{
    return type == TrackType::HiHat || type == TrackType::HatFX;
}

bool isNoteWithinVisibleBars(const NoteEvent& note, int bars, int ppq)
{
    const int startTick = stepToTicks(note.step, ppq) + note.microOffset;
    const int visibleTickLimit = std::max(1, bars) * ticksPerStep(ppq) * 16;
    return startTick >= 0 && startTick < visibleTickLimit;
}

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

std::vector<int> collectHatChokeStarts(const PatternProject& project,
                                       std::optional<TrackType> onlyTrack,
                                       int bars,
                                       int ppq,
                                       bool honorMute,
                                       bool honorSolo)
{
    std::vector<int> starts;

    for (const auto& track : project.tracks)
    {
        if (!shouldIncludeTrack(project, track, onlyTrack, honorMute, honorSolo))
            continue;
        if (!isHatChokeTrack(track.type))
            continue;

        for (const auto& note : track.notes)
        {
            if (isNoteWithinVisibleBars(note, bars, ppq))
                starts.push_back(std::max(0, stepToTicks(note.step, ppq) + note.microOffset));
        }
    }

    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    return starts;
}

std::vector<int> collectHatChokeStartsAll(const PatternProject& project, int bars, int ppq)
{
    std::vector<int> starts;

    for (const auto& track : project.tracks)
    {
        if (!track.enabled || !isHatChokeTrack(track.type))
            continue;

        for (const auto& note : track.notes)
        {
            if (isNoteWithinVisibleBars(note, bars, ppq))
                starts.push_back(std::max(0, stepToTicks(note.step, ppq) + note.microOffset));
        }
    }

    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    return starts;
}

void addTrackNameEvent(juce::MidiMessageSequence& sequence, const juce::String& name)
{
    auto trackName = juce::MidiMessage::textMetaEvent(0x03, name);
    trackName.setTimeStamp(0.0);
    sequence.addEvent(trackName);
}
} // namespace

juce::MidiMessageSequence MidiExportEngine::trackToSequence(const TrackState& track,
                                                            int bars,
                                                            int ppq,
                                                            const std::vector<int>* chokeStartTicks)
{
    juce::MidiMessageSequence sequence;

    std::vector<int> localStarts;
    localStarts.reserve(track.notes.size());
    for (const auto& note : track.notes)
    {
        if (isNoteWithinVisibleBars(note, bars, ppq))
            localStarts.push_back(std::max(0, stepToTicks(note.step, ppq) + note.microOffset));
    }
    std::sort(localStarts.begin(), localStarts.end());
    localStarts.erase(std::unique(localStarts.begin(), localStarts.end()), localStarts.end());

    const bool applyHatCutoff = isHatChokeTrack(track.type);
    const auto* startsRef = chokeStartTicks != nullptr ? chokeStartTicks : &localStarts;

    for (const auto& note : track.notes)
    {
        if (!isNoteWithinVisibleBars(note, bars, ppq))
            continue;

        const int midiNote = exportMidiPitch(track, note);

        const int baseTick = stepToTicks(note.step, ppq);
        const int startTick = baseTick + note.microOffset;
        int gateTicks = std::max(1, note.length) * ticksPerStep(ppq);
        if (applyHatCutoff)
            gateTicks = std::min(gateTicks, std::max(1, ticksPerStep(ppq) / 2));

        int endTick = startTick + gateTicks;
        if (!startsRef->empty())
        {
            auto it = std::upper_bound(startsRef->begin(), startsRef->end(), startTick);
            if (it != startsRef->end())
            {
                const int nextStart = *it;
                endTick = std::min(endTick, nextStart - 1);
            }
        }
        endTick = std::max(startTick + 1, endTick);

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
    const auto hatChokeStarts = collectHatChokeStarts(project, onlyTrack, project.params.bars, ppq, honorMute, honorSolo);

    for (const auto& track : project.tracks)
    {
        if (!shouldIncludeTrack(project, track, onlyTrack, honorMute, honorSolo))
            continue;

        const std::vector<int>* chokeRef = isHatChokeTrack(track.type) ? &hatChokeStarts : nullptr;
        auto trackSequence = trackToSequence(track, project.params.bars, ppq, chokeRef);
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
        const auto hatChokeStarts = collectHatChokeStartsAll(project, project.params.bars, ppq);

        for (const auto& info : TrackRegistry::all())
        {
            juce::MidiMessageSequence laneSequence;
            addTrackNameEvent(laneSequence, info.displayName);

            if (const auto* track = findTrackState(project, info.type); track != nullptr)
            {
                const std::vector<int>* chokeRef = isHatChokeTrack(track->type) ? &hatChokeStarts : nullptr;
                const auto noteSequence = trackToSequence(*track, project.params.bars, ppq, chokeRef);
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

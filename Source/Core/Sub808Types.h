#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "NoteEvent.h"

namespace bbg
{
enum class Sub808OverlapMode
{
    Retrigger = 0,
    Legato = 1,
    Glide = 2
};

enum class Sub808ScaleSnapPolicy
{
    Off = 0,
    HighlightOnly = 1,
    ForceToScale = 2
};

struct Sub808NoteEvent
{
    int pitch = 36;
    int gridTick = 0;
    int timingOffsetTicks = 0;
    int lengthTicks = TimingGrid::Sixteenth;
    int velocity = 100;
    juce::String semanticRole;
    bool isSlide = false;
    bool isLegato = false;
    bool glideToNext = false;

    int startTick() const noexcept { return gridTick + timingOffsetTicks; }
    int endTick() const noexcept { return startTick() + juce::jmax(1, lengthTicks); }
};

struct Sub808LaneSettings
{
    bool mono = true;
    bool cutItself = true;
    int glideTimeMs = 120;
    Sub808OverlapMode overlapMode = Sub808OverlapMode::Retrigger;
    Sub808ScaleSnapPolicy scaleSnapPolicy = Sub808ScaleSnapPolicy::ForceToScale;
    int bassAmount = 0; // Boom Bap bass: 0 Low (a few long notes), 1 More, 2 Full (plays all the way)
};

inline Sub808NoteEvent toSub808NoteEvent(const NoteEvent& note)
{
    Sub808NoteEvent result;
    result.pitch = note.pitch;
    result.gridTick = note.gridTick;
    result.timingOffsetTicks = note.timingOffsetTicks;
    result.lengthTicks = note.lengthTicks;
    result.velocity = note.velocity;
    result.semanticRole = note.semanticRole;
    result.isSlide = note.isSlide;
    result.isLegato = note.isLegato;
    result.glideToNext = note.glideToNext;
    return result;
}

inline NoteEvent toLegacyNoteEvent(const Sub808NoteEvent& note)
{
    NoteEvent result;
    result.pitch = note.pitch;
    result.gridTick = note.gridTick;
    result.timingOffsetTicks = note.timingOffsetTicks;
    result.lengthTicks = note.lengthTicks;
    result.velocity = note.velocity;
    result.semanticRole = note.semanticRole;
    result.isSlide = note.isSlide;
    result.isLegato = note.isLegato;
    result.glideToNext = note.glideToNext;
    return result;
}

inline std::vector<Sub808NoteEvent> toSub808NoteEvents(const std::vector<NoteEvent>& notes)
{
    std::vector<Sub808NoteEvent> result;
    result.reserve(notes.size());
    for (const auto& note : notes)
        result.push_back(toSub808NoteEvent(note));
    return result;
}

inline std::vector<NoteEvent> toLegacyNoteEvents(const std::vector<Sub808NoteEvent>& notes)
{
    std::vector<NoteEvent> result;
    result.reserve(notes.size());
    for (const auto& note : notes)
        result.push_back(toLegacyNoteEvent(note));
    return result;
}
} // namespace bbg
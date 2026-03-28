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
    int step = 0;
    int length = 1;
    int velocity = 100;
    int microOffset = 0;
    juce::String semanticRole;
    bool isSlide = false;
    bool isLegato = false;
    bool glideToNext = false;
};

struct Sub808LaneSettings
{
    bool mono = true;
    bool cutItself = true;
    int glideTimeMs = 120;
    Sub808OverlapMode overlapMode = Sub808OverlapMode::Retrigger;
    Sub808ScaleSnapPolicy scaleSnapPolicy = Sub808ScaleSnapPolicy::ForceToScale;
};

inline Sub808NoteEvent toSub808NoteEvent(const NoteEvent& note)
{
    Sub808NoteEvent result;
    result.pitch = note.pitch;
    result.step = note.step;
    result.length = note.length;
    result.velocity = note.velocity;
    result.microOffset = note.microOffset;
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
    result.step = note.step;
    result.length = note.length;
    result.velocity = note.velocity;
    result.microOffset = note.microOffset;
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
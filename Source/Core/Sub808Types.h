#pragma once

#include <vector>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

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
    int transposeSemitones = 0; // the lane's quick +/- (octave or semitone): playback and MIDI export
};

// The Sub808 lane's sound: always played by the HPDG 808 engine (Source/Bass808).
struct Sub808SoundState
{
    // false: the lane's bank sample (Sub808Dark1 ...) through the engine as a plain sample, as it always
    // sounded; true: an HPDG 808 sound (a preset or the editor's own), `settings` + `sampleWav`
    bool custom = false;
    juce::String name;              // the preset it came from, shown on the lane
    juce::ValueTree settings;       // "BassSettings" (Bass808/BassSettingsIO.h)
    juce::MemoryBlock sampleWav;    // the sample layer's file (24-bit WAV), empty: none

    // the strip under the rack, on top of either
    float filter = 0.0f;            // -1 .. 1: left closes the highs, right cuts the lows, 0 off
    float drive = 0.0f;             // 0 .. 1 more HamloMag
    int driveModel = 2;             // the model when the sound has none: 1 Classic .. 6 Vinyl (2 Tape)
    float length = 1.0f;            // 0 .. 1, 1: as the sound
    float gainDb = 0.0f;            // -24 .. +12
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
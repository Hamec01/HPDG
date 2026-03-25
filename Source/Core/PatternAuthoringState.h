#pragma once

#include <map>
#include <vector>

#include <juce_core/juce_core.h>

#include "RuntimeLaneProfile.h"

namespace bbg
{
struct NoteAuthoringKey
{
    int step = 0;
    int microOffset = 0;
    int pitch = 36;
    int length = 1;
    bool isGhost = false;
};

struct NoteAuthoringState
{
    NoteAuthoringKey noteKey;
    bool anchorLocked = false;
    int importanceWeight = 50;
};

struct TrackAuthoringPreferences
{
    float preferredDensity = 0.5f;
    float anchorPreserveRatio = 1.0f;
    float allowedSilence = 0.25f;
    float humanizeAmount = 0.0f;
    float repetitionTolerance = 0.5f;
    float fillAggressiveness = 0.5f;
};

struct PhraseBlock
{
    juce::Range<int> tickRange;
    juce::String role;
};

struct PatternAuthoringState
{
    std::map<RuntimeLaneId, std::vector<NoteAuthoringState>> noteMetadataByLane;
    std::map<RuntimeLaneId, TrackAuthoringPreferences> trackPreferencesByLane;
    std::vector<PhraseBlock> phraseBlocks;
};

inline int defaultImportanceWeightForSemanticRole(const juce::String& role)
{
    const auto normalized = role.trim().toLowerCase();
    if (normalized == "anchor")
        return 100;
    if (normalized == "support")
        return 70;
    if (normalized == "fill")
        return 50;
    if (normalized == "accent")
        return 80;
    return 30;
}
} // namespace bbg
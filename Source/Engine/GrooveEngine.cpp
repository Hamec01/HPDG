#include "GrooveEngine.h"

#include <algorithm>

#include "../Utils/MathHelpers.h"

namespace bbg
{
namespace
{
struct SwingRange
{
    float min = 56.0f;
    float max = 58.0f;
};

SwingRange preferredSwingRange(BoomBapSubstyle style)
{
    switch (style)
    {
        case BoomBapSubstyle::Classic: return { 56.0f, 58.0f };
        case BoomBapSubstyle::Dusty: return { 57.0f, 60.0f };
        case BoomBapSubstyle::Jazzy: return { 60.0f, 64.0f };
        case BoomBapSubstyle::Aggressive: return { 54.0f, 57.0f };
        case BoomBapSubstyle::LaidBack: return { 58.0f, 61.0f };
        case BoomBapSubstyle::BoomBapGold: return { 57.0f, 60.0f };
        case BoomBapSubstyle::RussianUnderground: return { 54.0f, 58.0f };
        case BoomBapSubstyle::LofiRap: return { 54.0f, 58.0f };
        default: return { 56.0f, 58.0f };
    }
}
}

void GrooveEngine::applySwing(PatternProject& project,
                              const BoomBapStyleProfile& style,
                              const std::unordered_set<TrackType>& mutableTracks)
{
    const auto range = preferredSwingRange(style.substyle);
    const float styleClamped = std::clamp(style.swingPercent, range.min, range.max);
    const float userClamped = std::clamp(project.params.swingPercent, range.min - 1.0f, range.max + 1.5f);
    float effectiveSwing = (userClamped * 0.52f) + (styleClamped * 0.48f);

    const float interpretedRef = interpretedReferenceTempo(style);
    if (style.grooveReferenceBpm >= 150)
        effectiveSwing += juce::jmap(interpretedRef, 75.0f, 105.0f, 0.6f, 1.4f);

    effectiveSwing = std::clamp(effectiveSwing, 54.0f, 67.0f);
    const float normalized = normalizeRange(effectiveSwing, 50.0f, 75.0f);
    const int maxSwingTicks = static_cast<int>(normalized * 120.0f);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.find(track.type) == mutableTracks.end())
            continue;

        float trackWeight = 1.0f;
        if (track.type == TrackType::Snare)
            trackWeight = 0.45f;
        else if (track.type == TrackType::ClapGhostSnare)
            trackWeight = 0.35f;
        else if (track.type == TrackType::Kick)
            trackWeight = 0.55f;
        else if (track.type == TrackType::Ride)
            trackWeight = 1.25f;
        else if (track.type == TrackType::GhostKick || track.type == TrackType::Perc)
            trackWeight = 1.10f;

        for (auto& note : track.notes)
        {
            const int stepInBar = note.step % 16;
            if ((note.step % 2) == 1)
                note.microOffset += static_cast<int>(maxSwingTicks * trackWeight);

            if (track.type == TrackType::Snare)
            {
                if (stepInBar == 4)
                    note.microOffset = std::max(4, note.microOffset + 2);
                else if (stepInBar == 12)
                    note.microOffset = std::max(1, note.microOffset - 1);
                else
                    note.microOffset = std::max(0, note.microOffset);
            }

            if (track.type == TrackType::ClapGhostSnare)
                note.microOffset = std::max(note.microOffset, 8);

            if (track.type == TrackType::Kick)
            {
                const bool anchor = stepInBar == 0 || stepInBar == 8;
                if (anchor)
                    note.microOffset = std::clamp(note.microOffset, -2, 3);
                else if (stepInBar == 15)
                    note.microOffset = std::clamp(note.microOffset + 4, -6, 18);
                else
                    note.microOffset = std::clamp(note.microOffset, -6, 12);
            }

            if (track.type == TrackType::HiHat)
            {
                if (stepInBar >= 14)
                    note.microOffset += 2;
                note.microOffset = std::clamp(note.microOffset, -10, 14);
            }

            if (track.type == TrackType::Ride)
                note.microOffset = std::clamp(note.microOffset, -8, 16);

            if (note.isGhost)
                note.microOffset = std::clamp(note.microOffset, -16, 20);
        }
    }
}
} // namespace bbg
